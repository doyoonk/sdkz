#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/fs/fs.h>

#include <py/mphal.h>
#include <py/gc.h>
#include <py/builtin.h>
#include <py/compile.h>
#include <py/mperrno.h>
#include <shared/runtime/pyexec.h>
#include <shared/runtime/gchelper.h>

static char heap[CONFIG_MICROPYTHON_HEAP_SIZE];
static struct ring_buf rx_rb;
static uint8_t rx_buf[CONFIG_MICROPYTHON_RX_BUF_SIZE];
static const struct shell *active_sh;

extern void z_shell_write(const struct shell *sh, const void *data, size_t length);

static inline void z_shell_unlock(const struct shell *sh)
{
	k_sem_give(&sh->ctx->lock_sem);
}

int mp_hal_stdin_rx_chr(void)
{
	uint8_t val;

	while (1) {
		if (ring_buf_get(&rx_rb, &val, 1)) {
			break;
		}

		shell_process(active_sh);
		k_msleep(1);
	}

	return (int)val;
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len)
{
	/* Shell module only officially supports rx bypass, bypass tx anyway */
	z_shell_write(active_sh, str, len);
	return len;
}

#if CONFIG_MICROPYTHON_FILE_SYSTEM

mp_import_stat_t mp_import_stat(const char *path)
{
	int ret;
	struct fs_dirent dirent;

	ret = fs_stat(path, &dirent);
	if (ret < 0) {
		return MP_IMPORT_STAT_NO_EXIST;
	}

	if (dirent.type == FS_DIR_ENTRY_FILE) {
		return MP_IMPORT_STAT_FILE;
	}

	if (dirent.type == FS_DIR_ENTRY_DIR) {
		return MP_IMPORT_STAT_DIR;
	}

	return MP_IMPORT_STAT_NO_EXIST;
}

static mp_uint_t file_readbyte(void *data)
{
	struct fs_file_t *file = data;
	int ret;
	uint8_t val;

	ret = fs_read(file, &val, 1);
	if (ret < 0) {
		mp_raise_OSError(MP_EIO);
	}

	if (ret == 0) {
		return MP_READER_EOF;
	}

	return (mp_uint_t)val;
}

static void file_close(void *data)
{
	struct fs_file_t *file = data;
	int ret;

	ret = fs_close(file);
	m_del_obj(struct fs_file_t, file);
	if (ret) {
		mp_raise_OSError(MP_EIO);
	}
}

mp_lexer_t *mp_lexer_new_from_file(qstr filename)
{
	struct fs_file_t *file;
	int ret;
	mp_reader_t reader;

	file = m_new_obj(struct fs_file_t);
	fs_file_t_init(file);
	ret = fs_open(file, qstr_str(filename), FS_O_READ);
	if (ret) {
		m_del_obj(struct fs_file_t, file);
		mp_raise_OSError(MP_ENOENT);
	}

	reader.data = file;
	reader.readbyte = file_readbyte;
	reader.close = file_close;
	return mp_lexer_new(filename, reader);
}

#else

mp_import_stat_t mp_import_stat(const char *path)
{
	return MP_IMPORT_STAT_NO_EXIST;
}

mp_lexer_t *mp_lexer_new_from_file(qstr filename)
{
	mp_raise_OSError(MP_ENOENT);
}

#endif

void nlr_jump_fail(void *val)
{
	while (1)
	{
	}
}

void gc_collect(void)
{
	gc_collect_start();
	gc_helper_collect_regs_and_stack();
	gc_collect_end();
}

static void bypass_callback(const struct shell *sh, uint8_t *data, size_t len)
{
	uint32_t size;

	size = ring_buf_put(&rx_rb, data, len);
	if (size < len) {
		shell_error(sh, "failed to buffer received data");
	}
}

static int cmd_python(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 2) {
		shell_error(sh, "invalid number of arguments");
		return -EINVAL;
	}

	ring_buf_init(&rx_rb, sizeof(rx_buf), rx_buf);
	mp_cstack_init_with_sp_here(2048);
	gc_init(heap, heap + sizeof(heap));
	mp_init();
	active_sh = sh;
	shell_set_bypass(sh, bypass_callback);
	if (argc == 1) {
		pyexec_friendly_repl();
	} else if (argc == 2) {
		pyexec_file_if_exists(argv[1]);
	}
	shell_set_bypass(sh, NULL);
	/* Bug in bypass mode locks shell, force unlock it */
	z_shell_unlock(sh);
	gc_sweep_all();
	mp_deinit();
	return 0;
}

SHELL_CMD_REGISTER(python, NULL, "Python", cmd_python);
