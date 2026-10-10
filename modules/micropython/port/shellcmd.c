/**
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(micropy, LOG_LEVEL_INF);

#include <zephyr/shell/shell.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>

#ifdef CONFIG_MICROPY_CONFIGFILE
#include CONFIG_MICROPY_CONFIGFILE
#endif

#include "py/mperrno.h"
#include <py/builtin.h>
#include "py/compile.h"
#include "py/runtime.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/mphal.h"
#include "py/stream.h"
#include "shared/runtime/gchelper.h"
#include "shared/runtime/pyexec.h"
#include "shared/readline/readline.h"
#include "extmod/modbluetooth.h"
#include "extmod/modmachine.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if MICROPY_ENABLE_GC
#if MICROPY_GC_SPLIT_HEAP && DT_HAS_COMPAT_STATUS_OKAY(micropython_heap)

#include <zephyr/linker/devicetree_regions.h>

#define MICROPY_HEAP_NAME(node) CONCAT(DT_NODE_FULL_NAME_TOKEN(node), _heap)

#define MICROPY_HEAP_DEFINE(node) \
    static char MICROPY_HEAP_NAME(node)[DT_PROP(node, size)] \
    Z_GENERIC_SECTION(LINKER_DT_NODE_REGION_NAME(DT_PROP(node, memory_region)));

DT_FOREACH_STATUS_OKAY(micropython_heap, MICROPY_HEAP_DEFINE)

#define MICROPY_HEAP_ADD(node) \
    gc_add((void *)&MICROPY_HEAP_NAME(node), \
    (void *)&(MICROPY_HEAP_NAME(node)[DT_PROP(node, size) - 1]));

#elif DT_HAS_COMPAT_STATUS_OKAY(micropython_heap)
#error Has additional Heap but split heap is not enabled
#endif
#endif

#include "modmachine.h"
#include "modzephyr.h"

#if CONFIG_SHARED_MULTI_HEAP
#include <zephyr/multi_heap/shared_multi_heap.h>
static  char* heap;
static struct ring_buf* rx_rb;
#else
static __noinit char heap[CONFIG_MICROPY_HEAP_SIZE];
static __noinit struct ring_buf rx_rb;
static __noinit uint8_t rx_buf[CONFIG_MICROPY_RX_BUF_SIZE];
#endif
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
	z_shell_write(active_sh, str, len);
	return len;
}

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags)
{
	uintptr_t ret = 0;
	if (poll_flags & MP_STREAM_POLL_RD) {
		ret |= MP_STREAM_POLL_RD;
	}
	if (poll_flags & MP_STREAM_POLL_WR) {
		ret |= MP_STREAM_POLL_WR;
	}
	return ret;
}

#if !MICROPY_VFS

mp_import_stat_t mp_import_stat(const char *path)
{
	return MP_IMPORT_STAT_NO_EXIST;
}

mp_lexer_t *mp_lexer_new_from_file(qstr filename)
{
	mp_raise_OSError(MP_ENOENT);
}

mp_obj_t mp_builtin_open(size_t n_args, const mp_obj_t *args, mp_map_t *kwargs)
{
    return mp_const_none;
}
MP_DEFINE_CONST_FUN_OBJ_KW(mp_builtin_open_obj, 1, mp_builtin_open);

#endif

#if MICROPY_PY_THREAD
void gc_collect(void)
{
    gc_collect_start();
    gc_helper_collect_regs_and_stack();
    #if MICROPY_PY_THREAD
    mp_thread_gc_others();
    #endif
    gc_collect_end();
}
#endif

MP_NORETURN void nlr_jump_fail(void *val)
{
    while (1) {
        ;
    }
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr)
{
    LOG_ERR("Assertion '%s' failed, at file %s:%d", expr, file, line);
    __fatal_error("Assertion failed");
}
#endif

static void _bypass_cb(const struct shell *sh, uint8_t *data, size_t len, void *user_data)
{
	uint32_t size = ring_buf_put(&rx_rb, data, len);
	if (size < len) {
		shell_error(sh, "failed to buffer received data");
	}
}

static int _python(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 2) {
		shell_error(sh, "invalid number of arguments");
		return -EINVAL;
	}

    struct k_thread *z_thread = (struct k_thread *)k_current_get();

    #if CONFIG_SHARED_MULTI_HEAP
    heap = shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, 16, MICROPY_HEAP_SIZE);
    if (heap == NULL) {
        LOG_ERR("Failed to get heap to shared multi heap");
        return -1;
    }
    LOG_INF("MicroPython running on Zephyr. stack size=%d, heap as shared", z_thread->stack_info.size);

	rx_rb = shared_multi_heap_aligned_alloc(SMH_REG_ATTR_EXTERNAL, 16, sizeof(struct ring_buf) + CONFIG_MICROPY_RX_BUF_SIZE);
    if (rx_rb == NULL) {
        LOG_ERR("Failed to get ring buffer to shared multi heap");
		shared_multi_heap_free(heap);
        return -1;
    }
	ring_buf_init(rx_rb, CONFIG_MICROPY_RX_BUF_SIZE, rx_rb + 1);
	#else
    LOG_INF("MicroPython running on Zephyr. stack size=%d, heap as system", z_thread->stack_info.size);
	ring_buf_init(&rx_rb, sizeof(rx_buf), rx_buf);
    #endif

    #if MICROPY_PY_THREAD
    mp_thread_init((void *)z_thread->stack_info.start, z_thread->stack_info.size / sizeof(uintptr_t));
    #endif

	mp_cstack_init_with_sp_here(CONFIG_MICROPY_STACK_SIZE);

	#if MICROPY_ENABLE_GC
	gc_init(heap, heap + MICROPY_HEAP_SIZE);
	#if MICROPY_GC_SPLIT_HEAP && DT_HAS_COMPAT_STATUS_OKAY(micropython_heap)
	DT_FOREACH_STATUS_OKAY(micropython_heap, MICROPY_HEAP_ADD)
	#endif
	#endif

	mp_init();
	active_sh = sh;
	shell_set_bypass(sh, _bypass_cb, NULL);
	if (argc == 1) {
		pyexec_friendly_repl();
	} else if (argc == 2) {
		pyexec_file_if_exists(argv[1]);
	}
	shell_set_bypass(sh, NULL, NULL);
	z_shell_unlock(sh);

    #if MICROPY_PY_BLUETOOTH
    mp_bluetooth_deinit();
    #endif
    #if MICROPY_PY_MACHINE
    machine_pin_deinit();
    #endif
    #if MICROPY_PY_MACHINE_I2C_TARGET
    mp_machine_i2c_target_deinit_all();
    #endif

    #if MICROPY_PY_THREAD
    mp_thread_deinit();
    gc_collect();
    #endif

	gc_sweep_all();
	mp_deinit();
    #if CONFIG_SHARED_MULTI_HEAP
	shared_multi_heap_free(rx_rb);
	shared_multi_heap_free(heap);
	#endif
	return 0;
}
SHELL_CMD_REGISTER(python, NULL, "Python", _python);

static void _init_zephyr(void)
{
}

static int _init_shellcmd()
{
    _init_zephyr();
    mp_hal_init();
	return 0;
}
SYS_INIT(_init_shellcmd, APPLICATION, 99);
