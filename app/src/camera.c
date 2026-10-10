/*
 * Copyright (c) 2024 Charles Dias <charlesdias.cd@outlook.com>
 * Copyright (c) 2026 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/video/video.h>
#include <zephyr/cache.h>
#ifdef CONFIG_VIDEO_BUFFER_USE_SHARED_MULTI_HEAP
#include <zephyr/multi_heap/shared_multi_heap.h>
#endif
#include <hu/hupacket.h>
#include <zephyr/sys/crc.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <lvgl.h>
#include "scanner_zxc.h"

LOG_MODULE_REGISTER(camera, CONFIG_LOG_DEFAULT_LEVEL);

#if !DT_HAS_CHOSEN(zephyr_camera)
#error No camera chosen in devicetree. Missing "--shield" or "--snippet video-sw-generator" flag?
#endif

#if !DT_HAS_CHOSEN(zephyr_display)
#error No display chosen in devicetree. Missing "--shield" flag?
#endif

#ifdef CONFIG_VIDEO_BUFFER_POOL_ZEPHYR_REGION
#define SNAPSHOT_SECTION Z_GENERIC_SECTION(CONFIG_VIDEO_BUFFER_POOL_ZEPHYR_REGION_NAME)
#else
#define SNAPSHOT_SECTION __noinit
#endif

struct camera_pixel_format {
	const char *name;
	uint32_t fourcc;
	uint8_t bytes_per_pixel;
	uint8_t red, green, blue;
};

/* Channel offsets describe byte order in memory, matching Zephyr formats.h. */
static const struct camera_pixel_format pixel_formats[] = {
	{"RGB565_LE", VIDEO_PIX_FMT_RGB565, 2, 0, 0, 0},
	{"RGB888", VIDEO_PIX_FMT_RGB24, 3, 0, 1, 2},
	{"BGR888", VIDEO_PIX_FMT_BGR24, 3, 2, 1, 0},
	{"ARGB8888", VIDEO_PIX_FMT_ARGB32, 4, 1, 2, 3},
	{"ABGR8888", VIDEO_PIX_FMT_ABGR32, 4, 3, 2, 1},
	{"RGBA8888", VIDEO_PIX_FMT_RGBA32, 4, 0, 1, 2},
	{"BGRA8888", VIDEO_PIX_FMT_BGRA32, 4, 2, 1, 0},
	{"YUYV", VIDEO_PIX_FMT_YUYV, 2, 0, 0, 0},
	{"YVYU", VIDEO_PIX_FMT_YVYU, 2, 0, 0, 0},
	{"GREY", VIDEO_PIX_FMT_GREY, 1, 0, 0, 0},
	{"NV12", VIDEO_PIX_FMT_NV12, 0, 0, 0, 0},
	{"NV21", VIDEO_PIX_FMT_NV21, 0, 0, 0, 0},
	{"NV16", VIDEO_PIX_FMT_NV16, 0, 0, 0, 0},
	{"NV61", VIDEO_PIX_FMT_NV61, 0, 0, 0, 0},
	{"YU12", VIDEO_PIX_FMT_YUV420, 0, 0, 0, 0},
	{"YV12", VIDEO_PIX_FMT_YVU420, 0, 0, 0, 0},
};
static const struct camera_pixel_format *camera_format;
#ifdef CONFIG_APP_CAMERA_BARCODE
static lv_obj_t *barcode_outline[SCANNER_ZXC_MAX_SYMBOLS];
static lv_obj_t *barcode_line[SCANNER_ZXC_MAX_SYMBOLS];
static lv_obj_t *barcode_number[SCANNER_ZXC_MAX_SYMBOLS];
static lv_point_precise_t barcode_points[SCANNER_ZXC_MAX_SYMBOLS][5];
static const uint32_t barcode_colors[] = {
	0x39ff70, 0x00d9ff, 0xffd54f, 0xff80df, 0xff914d, 0xb6a0ff, 0xffffff, 0x9de34f
};

static void barcode_overlay_create(void)
{
	for (size_t i = 0; i < SCANNER_ZXC_MAX_SYMBOLS; i++) {
		barcode_outline[i] = lv_line_create(lv_screen_active());
		lv_obj_set_style_line_color(barcode_outline[i], lv_color_black(), 0);
		lv_obj_set_style_line_width(barcode_outline[i], 5, 0);
		barcode_line[i] = lv_line_create(lv_screen_active());
		lv_obj_set_style_line_color(barcode_line[i], lv_color_hex(barcode_colors[i]), 0);
		lv_obj_set_style_line_width(barcode_line[i], 2, 0);
		barcode_number[i] = lv_label_create(lv_screen_active());
		lv_label_set_text_fmt(barcode_number[i], "%u", (unsigned)i + 1);
		lv_obj_set_style_bg_color(barcode_number[i], lv_color_black(), 0);
		lv_obj_set_style_bg_opa(barcode_number[i], LV_OPA_COVER, 0);
		lv_obj_set_style_text_color(barcode_number[i], lv_color_hex(barcode_colors[i]), 0);
		lv_obj_set_style_pad_all(barcode_number[i], 3, 0);
		lv_obj_set_hidden(barcode_outline[i], true);
		lv_obj_set_hidden(barcode_line[i], true);
		lv_obj_set_hidden(barcode_number[i], true);
	}
}

static void barcode_overlay_update(lv_obj_t *image, lv_obj_t *label,
				   const struct video_format *fmt, bool visible)
{
	static struct scanner_zxc_overlay overlay;
	char text[384];
	scanner_zxc_label(text, sizeof(text), &overlay);
	lv_label_set_text(label, text);
	bool valid = visible && overlay.width == fmt->width && overlay.height == fmt->height;
	int32_t scale = lv_image_get_scale(image);
	/* Match LV_IMAGE_ALIGN_CONTAIN, including its fixed-point scale rounding. */
	int32_t ox = lv_obj_get_x(image) +
		(lv_obj_get_width(image) - (int32_t)fmt->width * scale / LV_SCALE_NONE) / 2;
	int32_t oy = lv_obj_get_y(image) +
		(lv_obj_get_height(image) - (int32_t)fmt->height * scale / LV_SCALE_NONE) / 2;
	for (size_t i = 0; i < SCANNER_ZXC_MAX_SYMBOLS; i++) {
		bool show = valid && i < overlay.count;
		lv_obj_set_hidden(barcode_outline[i], !show);
		lv_obj_set_hidden(barcode_line[i], !show);
		lv_obj_set_hidden(barcode_number[i], !show);
		if (!show) { continue; }
		int32_t left = INT_MAX, top = INT_MAX;
		for (int j = 0; j < 4; j++) {
			int32_t x = ox + overlay.symbols[i].x[j] * scale / LV_SCALE_NONE;
			int32_t y = oy + overlay.symbols[i].y[j] * scale / LV_SCALE_NONE;
			barcode_points[i][j] = (lv_point_precise_t){x, y};
			left = MIN(left, x); top = MIN(top, y);
		}
		barcode_points[i][4] = barcode_points[i][0];
		lv_line_set_points(barcode_outline[i], barcode_points[i], 5);
		lv_line_set_points(barcode_line[i], barcode_points[i], 5);
		lv_obj_set_pos(barcode_outline[i], 0, 0);
		lv_obj_set_pos(barcode_line[i], 0, 0);
		lv_obj_set_pos(barcode_number[i], left, MAX(oy, top - 24));
	}
}
#endif
#define FRAME_CAPACITY ROUND_UP(CONFIG_APP_CAMERA_FRAME_CAPACITY, 32)
#define PREVIEW_SIZE   (CONFIG_APP_CAMERA_MAX_WIDTH * CONFIG_APP_CAMERA_MAX_HEIGHT * 2)
static K_MUTEX_DEFINE(camera_control_lock);
static struct video_caps camera_caps = {.type = VIDEO_BUF_TYPE_OUTPUT};
static bool camera_ready;
static bool config_pending;
static int config_id;
static int config_result;
static struct video_format config_wanted;
static struct video_format current_format;

static bool is_yuv420(uint32_t fourcc)
{
	return fourcc == VIDEO_PIX_FMT_NV12 || fourcc == VIDEO_PIX_FMT_NV21 ||
	       fourcc == VIDEO_PIX_FMT_YUV420 || fourcc == VIDEO_PIX_FMT_YVU420;
}

static uint32_t parse_format(const char *name)
{
	for (size_t i = 0; i < ARRAY_SIZE(pixel_formats); i++) {
		if (strcmp(name, pixel_formats[i].name) == 0) {
			return pixel_formats[i].fourcc;
		}
	}
	return strlen(name) == 4 ? VIDEO_FOURCC_FROM_STR(name) : 0;
}

static int validate_format(const struct video_format *fmt)
{
	if (fmt->width == 0 || fmt->height == 0 || fmt->width > CONFIG_APP_CAMERA_MAX_WIDTH ||
	    fmt->height > CONFIG_APP_CAMERA_MAX_HEIGHT) {
		return -EINVAL;
	}
	if (is_yuv420(fmt->pixelformat) && ((fmt->width | fmt->height) & 1U)) {
		return -EINVAL;
	}
	for (const struct video_format_cap *c = camera_caps.format_caps; c->pixelformat; c++) {
		if (c->pixelformat != fmt->pixelformat) {
			continue;
		}
		if (fmt->width < c->width_min || fmt->width > c->width_max ||
		    fmt->height < c->height_min || fmt->height > c->height_max ||
		    (c->width_step && (fmt->width - c->width_min) % c->width_step) ||
		    (c->height_step && (fmt->height - c->height_min) % c->height_step)) {
			return -EINVAL;
		}
		uint32_t bpp = video_bits_per_pixel(fmt->pixelformat);
		if (bpp && ROUND_UP(DIV_ROUND_UP(fmt->width * bpp, 8), 32) * fmt->height >
				   FRAME_CAPACITY) {
			return -ENOMEM;
		}
		return 0;
	}
	return -ENOTSUP;
}
#ifdef CONFIG_VIDEO_BUFFER_USE_SHARED_MULTI_HEAP
static uint16_t *preview;
#else
static uint16_t SNAPSHOT_SECTION
	__aligned(32) preview[CONFIG_APP_CAMERA_MAX_WIDTH * CONFIG_APP_CAMERA_MAX_HEIGHT];
#endif

static const void *camera_preview(const uint8_t *buffer, const struct video_format *fmt)
{
	if (fmt->pixelformat == VIDEO_PIX_FMT_RGB565) {
		return buffer;
	}
	if (camera_format == NULL) {
		return NULL; /* Raw transfer remains available without a preview decoder. */
	}
	bool planar = VIDEO_FMT_IS_SEMI_PLANAR(fmt->pixelformat) ||
		      VIDEO_FMT_IS_FULL_PLANAR(fmt->pixelformat);
	for (uint32_t y = 0; y < fmt->height; y++) {
		const uint8_t *row = buffer + y * (planar ? fmt->width : fmt->pitch);

		for (uint32_t x = 0; x < fmt->width; x++) {
			const uint8_t *pixel = row + x * camera_format->bytes_per_pixel;
			int red, green, blue;

			if (fmt->pixelformat == VIDEO_PIX_FMT_GREY) {
				red = green = blue = pixel[0];
			} else if (planar || fmt->pixelformat == VIDEO_PIX_FMT_YUYV ||
				   fmt->pixelformat == VIDEO_PIX_FMT_YVYU) {
				int u, v, luma;
				if (planar) {
					uint32_t cy = is_yuv420(fmt->pixelformat) ? y / 2 : y;
					uint32_t ys = fmt->width * fmt->height;
					if (VIDEO_FMT_IS_FULL_PLANAR(fmt->pixelformat)) {
						uint32_t ci = cy * (fmt->width / 2) + x / 2;
						u = buffer[ys + ci];
						v = buffer[ys + ys / 4 + ci];
					} else {
						uint32_t ci = cy * fmt->width + (x & ~1U);
						u = buffer[ys + ci];
						v = buffer[ys + ci + 1];
					}
					if (fmt->pixelformat == VIDEO_PIX_FMT_NV21 ||
					    fmt->pixelformat == VIDEO_PIX_FMT_NV61 ||
					    fmt->pixelformat == VIDEO_PIX_FMT_YVU420) {
						int swap = u;
						u = v;
						v = swap;
					}
					luma = row[x];
					u -= 128;
					v -= 128;
				} else {
					const uint8_t *pair = row + (x & ~1U) * 2;
					bool yuyv = fmt->pixelformat == VIDEO_PIX_FMT_YUYV;
					u = pair[yuyv ? 1 : 3] - 128;
					v = pair[yuyv ? 3 : 1] - 128;
					luma = pixel[0];
				}

				/* BT.601 full range, matching DCMIPP's RGB to YUV matrix. */
				red = CLAMP(luma + ((359 * v + 128) >> 8), 0, 255);
				green = CLAMP(luma - ((88 * u + 183 * v + 128) >> 8), 0, 255);
				blue = CLAMP(luma + ((454 * u + 128) >> 8), 0, 255);
			} else {
				red = pixel[camera_format->red];
				green = pixel[camera_format->green];
				blue = pixel[camera_format->blue];
			}
			preview[y * fmt->width + x] =
				((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
		}
	}
	return preview;
}

#ifdef CONFIG_APP_CAMERA_CAPTURE
#define SNAPSHOT_SIZE      FRAME_CAPACITY
#define CHUNK_SIZE         MIN(512, (CONFIG_HU_PACKET_SIZE - 128) / 2)
#define SNAPSHOT_IDLE_MS   30000
#define CAPTURE_TIMEOUT_MS 3000
BUILD_ASSERT(CONFIG_HU_PACKET_SIZE >= 256);

#ifdef CONFIG_VIDEO_BUFFER_USE_SHARED_MULTI_HEAP
static uint8_t *snapshot;
#else
static uint8_t SNAPSHOT_SECTION __aligned(32) snapshot[SNAPSHOT_SIZE];
#endif
static K_MUTEX_DEFINE(snapshot_lock);
static enum {
	SNAPSHOT_IDLE,
	SNAPSHOT_PENDING,
	SNAPSHOT_READY
} state;
static int snapshot_id;
static int64_t requested_ms;
static int64_t accessed_ms;
static uint32_t snapshot_crc;
static uint32_t snapshot_width;
static uint32_t snapshot_height;
static uint32_t snapshot_size;
static uint32_t snapshot_pitch;
static char snapshot_fourcc[5];

static int parse_number(const char *text, int *value)
{
	char *end;
	unsigned long result;

	if (*text < '0' || *text > '9') {
		return -EINVAL;
	}
	errno = 0;
	result = strtoul(text, &end, 10);
	if (errno || *end || result > INT_MAX) {
		return -EINVAL;
	}
	*value = (int)result;
	return 0;
}

/* Bound the echoed header before using the HU library's append helpers. */
static bool valid_header(struct hup_handle *h)
{
	if ((h->id && strlen(h->id) > 16) || (h->sequence && strlen(h->sequence) > 16)) {
		h->id = NULL;
		h->sequence = NULL;
		hupacket_nak_response(h, NULL, -EMSGSIZE);
		hupacket_send_buffer(h, NULL);
		return false;
	}
	return true;
}

static void expire_snapshot(void)
{
	if (state != SNAPSHOT_IDLE && k_uptime_get() - accessed_ms >= SNAPSHOT_IDLE_MS) {
		state = SNAPSHOT_IDLE;
	}
}

static void camera_capture_frame(const struct video_buffer *vbuf, const struct video_format *fmt)
{
	if (k_mutex_lock(&snapshot_lock, K_NO_WAIT) != 0) {
		return;
	}
	if (state == SNAPSHOT_PENDING && k_uptime_get() - requested_ms < CAPTURE_TIMEOUT_MS &&
	    vbuf->bytesused > 0 && vbuf->bytesused <= SNAPSHOT_SIZE) {
		snapshot_width = fmt->width;
		snapshot_height = fmt->height;
		snapshot_pitch = fmt->pitch;
		snapshot_size = vbuf->bytesused;
		memcpy(snapshot_fourcc, VIDEO_FOURCC_TO_STR(fmt->pixelformat), 5);
		memcpy(snapshot, vbuf->buffer, snapshot_size);
		snapshot_crc = crc32_ieee(snapshot, snapshot_size);
		scanner_zxc_submit(vbuf, fmt, snapshot_id);
		state = SNAPSHOT_READY;
	}
	k_mutex_unlock(&snapshot_lock);
}

static void capture_command(void *handle, int argc, const char **argv)
{
	struct hup_handle *h = handle;
	int id;
	int rc = -EINVAL;

	if (!valid_header(h)) {
		return;
	}
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	expire_snapshot();
	if (argc != 2 || parse_number(argv[1], &id) != 0 || id == 0) {
		goto out;
	}
	if (state != SNAPSHOT_IDLE && id != snapshot_id) {
		rc = -EBUSY;
		goto out;
	}
	if (state == SNAPSHOT_IDLE) {
		snapshot_id = id;
		requested_ms = k_uptime_get();
		state = SNAPSHOT_PENDING;
	}
	accessed_ms = k_uptime_get();
	if (state == SNAPSHOT_PENDING) {
		rc = -EAGAIN;
		if (accessed_ms - requested_ms >= CAPTURE_TIMEOUT_MS) {
			state = SNAPSHOT_IDLE;
			rc = -ETIMEDOUT;
		}
		goto out;
	}
	hupacket_ack_response(h, NULL);
	hupacket_record_int(h, NULL, snapshot_id);
	hupacket_record_int(h, NULL, snapshot_width);
	hupacket_record_int(h, NULL, snapshot_height);
	hupacket_record_int(h, NULL, snapshot_pitch);
	hupacket_record_int(h, NULL, snapshot_size);
	hupacket_record_str(h, NULL, snapshot_fourcc);
	hupacket_record_hex(h, NULL, snapshot_crc);
	hupacket_record_int(h, NULL, CHUNK_SIZE);
	rc = 0;
out:
	if (rc) {
		hupacket_nak_response(h, NULL, rc);
	}
	k_mutex_unlock(&snapshot_lock);
	hupacket_send_buffer(h, NULL);
}
DEFINE_HUP_CMD(hup_cmd_cam_capture, "cam_capture", capture_command);

static void read_command(void *handle, int argc, const char **argv)
{
	struct hup_handle *h = handle;
	static const char hex[] = "0123456789abcdef";
	int id, offset, count;
	int rc = -EINVAL;

	if (!valid_header(h)) {
		return;
	}
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	expire_snapshot();
	if (argc != 4 || parse_number(argv[1], &id) || parse_number(argv[2], &offset) ||
	    parse_number(argv[3], &count) || count == 0 || count > CHUNK_SIZE) {
		goto out;
	}
	if (state == SNAPSHOT_IDLE || id != snapshot_id) {
		rc = -ESTALE;
		goto out;
	}
	if (state != SNAPSHOT_READY) {
		rc = -EAGAIN;
		goto out;
	}
	if ((uint32_t)offset >= snapshot_size || (uint32_t)count > snapshot_size - offset) {
		goto out;
	}
	accessed_ms = k_uptime_get();
	hupacket_ack_response(h, NULL);
	hupacket_record_int(h, NULL, snapshot_id);
	hupacket_record_int(h, NULL, offset);
	hupacket_record_str(h, NULL, "");
	char *dst = h->tx_buffer + strlen(h->tx_buffer);

	for (int i = 0; i < count; i++) {
		uint8_t byte = snapshot[offset + i];

		*dst++ = hex[byte >> 4];
		*dst++ = hex[byte & 15];
	}
	*dst = '\0';
	rc = 0;
out:
	if (rc) {
		hupacket_nak_response(h, NULL, rc);
	}
	k_mutex_unlock(&snapshot_lock);
	hupacket_send_buffer(h, NULL);
}
DEFINE_HUP_CMD(hup_cmd_cam_read, "cam_read", read_command);

static void release_command(void *handle, int argc, const char **argv)
{
	struct hup_handle *h = handle;
	int id;
	int rc = -EINVAL;

	if (!valid_header(h)) {
		return;
	}
	k_mutex_lock(&snapshot_lock, K_FOREVER);
	if (argc == 2 && parse_number(argv[1], &id) == 0 && id != 0) {
		rc = -ESTALE;
		if (id == snapshot_id) {
			state = SNAPSHOT_IDLE;
			rc = 0;
		}
	}
	if (rc) {
		hupacket_nak_response(h, NULL, rc);
	} else {
		hupacket_ack_response(h, NULL);
		hupacket_record_int(h, NULL, id);
	}
	k_mutex_unlock(&snapshot_lock);
	hupacket_send_buffer(h, NULL);
}
DEFINE_HUP_CMD(hup_cmd_cam_release, "cam_release", release_command);

static void caps_command(void *handle, int argc, const char **argv)
{
	struct hup_handle *h = handle;
	int index;
	int rc = -EINVAL;
	if (!valid_header(h)) {
		return;
	}
	k_mutex_lock(&camera_control_lock, K_FOREVER);
	if (!camera_ready) {
		rc = -ENODEV;
		goto out;
	}
	if (argc != 2 || parse_number(argv[1], &index)) {
		goto out;
	}
	int count = 0;
	while (camera_caps.format_caps[count].pixelformat) {
		count++;
	}
	if (index >= count) {
		goto out;
	}
	const struct video_format_cap *c = &camera_caps.format_caps[index];
	hupacket_ack_response(h, NULL);
	hupacket_record_int(h, NULL, index);
	hupacket_record_int(h, NULL, count);
	hupacket_record_str(h, NULL, VIDEO_FOURCC_TO_STR(c->pixelformat));
	hupacket_record_int(h, NULL, c->width_min);
	hupacket_record_int(h, NULL, c->width_max);
	hupacket_record_int(h, NULL, c->width_step);
	hupacket_record_int(h, NULL, c->height_min);
	hupacket_record_int(h, NULL, c->height_max);
	hupacket_record_int(h, NULL, c->height_step);
	hupacket_record_int(h, NULL, CONFIG_APP_CAMERA_MAX_WIDTH);
	hupacket_record_int(h, NULL, CONFIG_APP_CAMERA_MAX_HEIGHT);
	hupacket_record_int(h, NULL, FRAME_CAPACITY);
	rc = 0;
out:
	if (rc) {
		hupacket_nak_response(h, NULL, rc);
	}
	k_mutex_unlock(&camera_control_lock);
	hupacket_send_buffer(h, NULL);
}
DEFINE_HUP_CMD(hup_cmd_cam_caps, "cam_caps", caps_command);

static void config_command(void *handle, int argc, const char **argv)
{
	struct hup_handle *h = handle;
	int id, width, height;
	int rc = -EINVAL;
	struct video_format wanted = {.type = VIDEO_BUF_TYPE_OUTPUT};
	if (!valid_header(h)) {
		return;
	}
	k_mutex_lock(&camera_control_lock, K_FOREVER);
	if (!camera_ready) {
		rc = -ENODEV;
		goto out;
	}
	if (argc != 5 || parse_number(argv[1], &id) || id == 0 || parse_number(argv[3], &width) ||
	    parse_number(argv[4], &height)) {
		goto out;
	}
	wanted.pixelformat = parse_format(argv[2]);
	wanted.width = width;
	wanted.height = height;
	rc = validate_format(&wanted);
	if (rc) {
		goto out;
	}
	if (config_pending && id != config_id) {
		rc = -EBUSY;
		goto out;
	}
	if (id != config_id) {
		config_id = id;
		config_wanted = wanted;
		config_pending = true;
		config_result = -EAGAIN;
	} else if (wanted.pixelformat != config_wanted.pixelformat ||
		   wanted.width != config_wanted.width || wanted.height != config_wanted.height) {
		rc = -EINVAL;
		goto out;
	}
	rc = config_result;
	if (rc) {
		goto out;
	}
	hupacket_ack_response(h, NULL);
	hupacket_record_int(h, NULL, id);
	hupacket_record_str(h, NULL, VIDEO_FOURCC_TO_STR(current_format.pixelformat));
	hupacket_record_int(h, NULL, current_format.width);
	hupacket_record_int(h, NULL, current_format.height);
	hupacket_record_int(h, NULL, current_format.pitch);
	hupacket_record_int(h, NULL, current_format.size);
out:
	if (rc) {
		hupacket_nak_response(h, NULL, rc);
	}
	k_mutex_unlock(&camera_control_lock);
	hupacket_send_buffer(h, NULL);
}
DEFINE_HUP_CMD(hup_cmd_cam_config, "cam_config", config_command);

#else
static void camera_capture_frame(const struct video_buffer *vbuf, const struct video_format *fmt)
{
	ARG_UNUSED(vbuf);
	ARG_UNUSED(fmt);
}
#endif

static int set_camera_layout(const struct device *dev, struct video_format *fmt)
{
	int rc = validate_format(fmt);
	if (rc) {
		return rc;
	}
	struct video_selection sel = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.target = VIDEO_SEL_TGT_COMPOSE,
		.rect = {.width = fmt->width, .height = fmt->height},
	};
	rc = video_set_selection(dev, &sel);
	if (rc && rc != -ENOSYS) {
		return rc;
	}
	rc = video_set_format(dev, fmt);
	if (rc) {
		return rc;
	}
	if (fmt->size == 0 || fmt->size > FRAME_CAPACITY) {
		return -ENOMEM;
	}
	return validate_format(fmt);
}

static int queue_camera_buffers(const struct device *dev, struct video_buffer **buffers)
{
	for (int i = 0; i < CONFIG_VIDEO_BUFFER_POOL_NUM_MAX; i++) {
		buffers[i]->type = VIDEO_BUF_TYPE_OUTPUT;
		buffers[i]->bytesused = 0;
		int rc = video_enqueue(dev, buffers[i]);
		if (rc) {
			return rc;
		}
	}
	return video_stream_start(dev, VIDEO_BUF_TYPE_OUTPUT);
}

/* Called only by the camera thread, after returning its dequeued buffer. */
static int apply_camera_config(const struct device *dev, struct video_format *fmt,
			       struct video_buffer **buffers)
{
	int rc = 0;
	k_mutex_lock(&camera_control_lock, K_FOREVER);
	if (!config_pending) {
		goto out;
	}
	struct video_format previous = *fmt;
	rc = video_stream_stop(dev, VIDEO_BUF_TYPE_OUTPUT);
	if (rc) {
		goto failed;
	}
	struct video_buffer *discard = &(struct video_buffer){.type = VIDEO_BUF_TYPE_OUTPUT};
	while (video_dequeue(dev, &discard, K_NO_WAIT) == 0) {
	}
	*fmt = config_wanted;
	config_result = set_camera_layout(dev, fmt);
	if (config_result) {
		*fmt = previous;
		rc = set_camera_layout(dev, fmt);
		if (rc) {
			goto failed;
		}
	}
	rc = queue_camera_buffers(dev, buffers);
	if (rc) {
		goto failed;
	}
	current_format = *fmt;
	config_pending = false;
	LOG_INF("Camera format %s %ux%u pitch=%u size=%u", VIDEO_FOURCC_TO_STR(fmt->pixelformat),
		fmt->width, fmt->height, fmt->pitch, fmt->size);
	goto out;
failed:
	config_result = rc;
	config_pending = false;
	camera_ready = false;
out:
	k_mutex_unlock(&camera_control_lock);
	return rc;
}

static int _main(void)
{
	struct video_buffer *buffers[CONFIG_VIDEO_BUFFER_POOL_NUM_MAX] = {0};
	struct video_buffer initial_buffer = {.type = VIDEO_BUF_TYPE_OUTPUT};
	struct video_buffer *vbuf = &initial_buffer;
	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	const struct device *video_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
	struct video_format fmt = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = parse_format(CONFIG_APP_CAMERA_PIXEL_FORMAT),
		.width = CONFIG_VIDEO_WIDTH,
		.height = CONFIG_VIDEO_HEIGHT,
	};
	int rc;

	if (!device_is_ready(display_dev) || !device_is_ready(video_dev)) {
		LOG_ERR("Camera or display device is not ready");
		return 0;
	}
	rc = video_get_caps(video_dev, &camera_caps);
	if (rc) {
		LOG_ERR("Unable to retrieve camera capabilities (%d)", rc);
		return 0;
	}
#if CONFIG_VIDEO_SOURCE_CROP_WIDTH && CONFIG_VIDEO_SOURCE_CROP_HEIGHT
	struct video_selection crop = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.target = VIDEO_SEL_TGT_CROP,
		.rect = {.left = CONFIG_VIDEO_SOURCE_CROP_LEFT,
			 .top = CONFIG_VIDEO_SOURCE_CROP_TOP,
			 .width = CONFIG_VIDEO_SOURCE_CROP_WIDTH,
			 .height = CONFIG_VIDEO_SOURCE_CROP_HEIGHT},
	};
	rc = video_set_selection(video_dev, &crop);
	if (rc) {
		LOG_ERR("Unable to set camera crop (%d)", rc);
		return 0;
	}
#endif
	rc = set_camera_layout(video_dev, &fmt);
	if (rc) {
		LOG_ERR("Unable to configure camera (%d)", rc);
		return 0;
	}
	for (int i = 0; i < ARRAY_SIZE(buffers); i++) {
		/* Fixed capacity supports changing layouts without reallocating DMA memory. */
		buffers[i] = video_buffer_aligned_alloc(
			FRAME_CAPACITY, MAX(CONFIG_VIDEO_BUFFER_POOL_ALIGN, 32), K_NO_WAIT);
		if (!buffers[i]) {
			LOG_ERR("Unable to allocate camera buffer %d", i);
			goto free_buffers;
		}
		sys_cache_data_flush_and_invd_range(buffers[i]->buffer, buffers[i]->size);
	}
#ifdef CONFIG_VIDEO_BUFFER_USE_SHARED_MULTI_HEAP
	preview = shared_multi_heap_aligned_alloc(CONFIG_VIDEO_BUFFER_SMH_ATTRIBUTE, 32,
						  ROUND_UP(PREVIEW_SIZE, 32));
	if (preview == NULL) {
		LOG_ERR("Unable to allocate camera preview");
		goto free_buffers;
	}
#ifdef CONFIG_APP_CAMERA_CAPTURE
	snapshot = shared_multi_heap_aligned_alloc(CONFIG_VIDEO_BUFFER_SMH_ATTRIBUTE, 32,
						   SNAPSHOT_SIZE);
	if (snapshot == NULL) {
		LOG_ERR("Unable to allocate camera snapshot");
		shared_multi_heap_free(preview);
		preview = NULL;
		goto free_buffers;
	}
#endif
#endif
	struct video_control ctrl = {.id = VIDEO_CID_HFLIP, .val = 1};
	if (IS_ENABLED(CONFIG_VIDEO_HFLIP)) {
		video_set_ctrl(video_dev, &ctrl);
	}
	if (IS_ENABLED(CONFIG_VIDEO_VFLIP)) {
		ctrl.id = VIDEO_CID_VFLIP;
		video_set_ctrl(video_dev, &ctrl);
	}
	rc = queue_camera_buffers(video_dev, buffers);
	if (rc) {
		LOG_ERR("Unable to start capture (%d)", rc);
		return 0;
	}
	display_blanking_off(display_dev);
	lv_image_dsc_t video_img = {.header.magic = LV_IMAGE_HEADER_MAGIC,
				    .header.cf = LV_COLOR_FORMAT_RGB565};
	lv_obj_t *screen = lv_image_create(lv_screen_active());
	lv_obj_set_size(screen, lv_display_get_horizontal_resolution(NULL),
			lv_display_get_vertical_resolution(NULL));
	lv_image_set_inner_align(screen, LV_IMAGE_ALIGN_CONTAIN);
	lv_obj_center(screen);
#ifdef CONFIG_APP_CAMERA_BARCODE
	barcode_overlay_create();
	lv_obj_t *barcode_label = lv_label_create(lv_screen_active());
	lv_obj_set_width(barcode_label, lv_display_get_horizontal_resolution(NULL));
	lv_obj_align(barcode_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
	lv_obj_set_style_bg_color(barcode_label, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(barcode_label, LV_OPA_80, 0);
	lv_obj_set_style_text_color(barcode_label, lv_color_white(), 0);
	lv_obj_set_style_pad_all(barcode_label, 6, 0);
	lv_label_set_text(barcode_label, "ZXC: starting...");
#endif
	k_mutex_lock(&camera_control_lock, K_FOREVER);
	current_format = fmt;
	camera_ready = true;
	k_mutex_unlock(&camera_control_lock);
	LOG_INF("Capture started: %s %ux%u", VIDEO_FOURCC_TO_STR(fmt.pixelformat), fmt.width,
		fmt.height);

	while (1) {
		rc = apply_camera_config(video_dev, &fmt, buffers);
		if (rc) {
			LOG_ERR("Camera reconfiguration failed (%d)", rc);
			return 0;
		}
		rc = video_dequeue(video_dev, &vbuf, K_MSEC(100));
		if (rc == -EAGAIN) {
			continue;
		}
		if (rc) {
			LOG_ERR("Unable to dequeue frame (%d)", rc);
			return 0;
		}
		if (vbuf->bytesused > 0 && vbuf->bytesused <= fmt.size) {
			sys_cache_data_invd_range(vbuf->buffer, ROUND_UP(vbuf->bytesused, 32));
			/* Preserve native bytes, including planes/padding, before any LCD
			 * conversion. */
			camera_capture_frame(vbuf, &fmt);
			if (vbuf->bytesused == fmt.size) {
				scanner_zxc_submit(vbuf, &fmt, 0);
			}
			camera_format = NULL;
			for (size_t i = 0; i < ARRAY_SIZE(pixel_formats); i++) {
				if (pixel_formats[i].fourcc == fmt.pixelformat) {
					camera_format = &pixel_formats[i];
					break;
				}
			}
			if (vbuf->bytesused == fmt.size) {
				video_img.data = camera_preview(vbuf->buffer, &fmt);
			} else {
				video_img.data = NULL;
			}
			if (video_img.data) {
				video_img.header.w = fmt.width;
				video_img.header.h = fmt.height;
				video_img.header.stride = fmt.pixelformat == VIDEO_PIX_FMT_RGB565
								  ? fmt.pitch
								  : fmt.width * 2;
				video_img.data_size = fmt.pixelformat == VIDEO_PIX_FMT_RGB565
							      ? fmt.size
							      : fmt.width * fmt.height * 2;
				lv_obj_set_hidden(screen, false);
				lv_image_set_src(screen, &video_img);
			} else {
				lv_obj_set_hidden(screen, true);
			}
#ifdef CONFIG_APP_CAMERA_BARCODE
			barcode_overlay_update(screen, barcode_label, &fmt, video_img.data != NULL);
#endif
			lv_timer_handler();
			lv_refr_now(NULL);
			lv_draw_wait_for_finish();
		}
		rc = video_enqueue(video_dev, vbuf);
		if (rc) {
			LOG_ERR("Unable to requeue frame (%d)", rc);
			return 0;
		}
	}
free_buffers:
	for (int i = 0; i < ARRAY_SIZE(buffers); i++) {
		if (buffers[i] != NULL) {
			video_buffer_release(buffers[i]);
		}
	}
	return 0;
}
K_THREAD_DEFINE(camera, 4096, _main, NULL, NULL, NULL, 7, 0, 0);
