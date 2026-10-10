/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "scanner_zxc.h"
#include <errno.h>

#if defined(CONFIG_ZXC_USE_HEAP) && defined(CONFIG_ZXC_ALLOCATOR_CUSTOM) && \
    defined(CONFIG_VIDEO_BUFFER_USE_SHARED_MULTI_HEAP)
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <zxc/zxc_allocator_backend.h>

/* Use the video's heap attribute without adding a header or moving payloads.
 * The shared heap API serializes access across threads internally. */
void *zxc_custom_malloc(size_t size)
{
	return size ? shared_multi_heap_alloc(CONFIG_VIDEO_BUFFER_SMH_ATTRIBUTE, size) : NULL;
}

void zxc_custom_free(void *pointer)
{
	if (pointer) {
		shared_multi_heap_free(pointer);
	}
}

void *zxc_custom_calloc(size_t count, size_t size)
{
	if (!size || count > SIZE_MAX / size) {
		return NULL;
	}
	size_t bytes = count * size;
	void *pointer = zxc_custom_malloc(bytes);
	if (pointer) {
		memset(pointer, 0, bytes);
	}
	return pointer;
}

void *zxc_custom_realloc(void *pointer, size_t size)
{
	if (!pointer) {
		return zxc_custom_malloc(size);
	}
	if (!size) {
		zxc_custom_free(pointer);
		return NULL;
	}
	return shared_multi_heap_realloc(CONFIG_VIDEO_BUFFER_SMH_ATTRIBUTE, pointer, size);
}
#endif

#define FCC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | \
	((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

int scanner_zxc_gray(uint8_t *dst, uint32_t max_w, uint32_t max_h,
	const uint8_t *src, size_t bytes, uint32_t fourcc, uint32_t width,
	uint32_t height, uint32_t pitch, uint32_t *out_w, uint32_t *out_h,
	uint32_t *scale)
{
	unsigned bpp = 1, r = 0, g = 1, b = 2;
	int rgb = 0;
	if (!dst || !src || !out_w || !out_h || !scale || !width || !height ||
	    !max_w || !max_h || width > 65535 || height > 65535) {
		return -EINVAL;
	}
	switch (fourcc) {
	case FCC('R','G','B','P'): bpp = 2; rgb = 565; break;
	case FCC('R','G','B','3'): bpp = 3; rgb = 1; break;
	case FCC('B','G','R','3'): bpp = 3; rgb = 1; r = 2; b = 0; break;
	case FCC('B','A','2','4'): bpp = 4; rgb = 1; r = 1; g = 2; b = 3; break;
	case FCC('R','A','2','4'): bpp = 4; rgb = 1; r = 3; g = 2; b = 1; break;
	case FCC('A','B','2','4'): bpp = 4; rgb = 1; break;
	case FCC('A','R','2','4'): bpp = 4; rgb = 1; r = 2; b = 0; break;
	case FCC('Y','U','Y','V'):
	case FCC('Y','V','Y','U'): bpp = 2; break;
	case FCC('G','R','E','Y'): break;
	case FCC('N','V','1','2'):
	case FCC('N','V','2','1'):
	case FCC('N','V','1','6'):
	case FCC('N','V','6','1'):
	case FCC('Y','U','1','2'):
	case FCC('Y','V','1','2'):
		/* STM32 DCMIPP planar formats have contiguous, width-byte Y rows. */
		pitch = width;
		break;
	default: return -ENOTSUP;
	}
	if (pitch < width * bpp || (uint64_t)(height - 1) * pitch + width * bpp > bytes) {
		return -EINVAL;
	}
	*scale = (width - 1) / max_w + 1;
	uint32_t sy = (height - 1) / max_h + 1;
	if (sy > *scale) { *scale = sy; }
	*out_w = width / *scale;
	*out_h = height / *scale;
	if (!*out_w || !*out_h) { return -EINVAL; }
	for (uint32_t y = 0; y < *out_h; y++) {
		for (uint32_t x = 0; x < *out_w; x++) {
			const uint8_t *p = src + (size_t)(y * *scale + *scale / 2) * pitch +
				(x * *scale + *scale / 2) * bpp;
			unsigned red, green, blue, gray;
			if (rgb == 565) {
				unsigned value = p[0] | ((unsigned)p[1] << 8);
				red = (value >> 11) * 255 / 31;
				green = ((value >> 5) & 63) * 255 / 63;
				blue = (value & 31) * 255 / 31;
				gray = (77 * red + 150 * green + 29 * blue + 128) >> 8;
			} else if (rgb) {
				gray = (77 * p[r] + 150 * p[g] + 29 * p[b] + 128) >> 8;
			} else {
				gray = p[0];
			}
			dst[(size_t)y * *out_w + x] = gray;
		}
	}
	return 0;
}

/* Thread, LCD and HU integration; host tests exercise the converter above. */
#ifdef CONFIG_APP_CAMERA_BARCODE
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/multi_heap/shared_multi_heap.h>
#include <zephyr/sys/util.h>
#include <hu/hupacket.h>
#include <zxc/zxc.h>
#include <zephyr/video/video.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

LOG_MODULE_REGISTER(scanner_zxc, CONFIG_LOG_DEFAULT_LEVEL);
#define MAX_SYMBOLS SCANNER_ZXC_MAX_SYMBOLS
#define PAYLOAD_SIZE 16384
#define DATA_CHUNK 256
#define SCAN_WIDTH CONFIG_APP_CAMERA_BARCODE_WIDTH
#define SCAN_HEIGHT CONFIG_APP_CAMERA_BARCODE_HEIGHT
BUILD_ASSERT(CONFIG_HU_PACKET_SIZE >= 1024);

struct scan_job {
	int id;
	uint32_t ticket, width, height, source_width, source_height, scale;
	int64_t captured_ms;
};
struct scan_result {
	struct scan_job job;
	int generation, error, status;
	uint32_t flags, elapsed_ms;
	size_t count;
	zxc_result_t items[MAX_SYMBOLS];
	uint8_t payload[PAYLOAD_SIZE];
};

static K_MUTEX_DEFINE(input_lock);
static K_MUTEX_DEFINE(result_lock);
static K_SEM_DEFINE(work_ready, 0, 1);
static uint8_t *input_pixels, *scan_pixels;
static struct scan_job pending_job;
static bool pending, ready;
static int init_error = -EAGAIN;
static uint32_t next_ticket;
static int64_t next_periodic_ms;
static struct scan_result latest, snapshot_result, working;
#if !ZXC_USE_HEAP
static zxc_result_t output_items[MAX_SYMBOLS];
static uint8_t output_payload[PAYLOAD_SIZE];
static zxc_segment_t output_segments[128];
static zxc_metadata_t output_metadata[128];
#endif

static const char *format_name(zxc_format_t format)
{
	switch (format) {
	case ZXC_FORMAT_CODABAR: return "CODABAR";
	case ZXC_FORMAT_CODE39_STD: return "CODE39_STD";
	case ZXC_FORMAT_CODE39_EXT: return "CODE39_EXT";
	case ZXC_FORMAT_CODE32: return "CODE32";
	case ZXC_FORMAT_PZN: return "PZN";
	case ZXC_FORMAT_CODE93: return "CODE93";
	case ZXC_FORMAT_CODE128: return "CODE128";
	case ZXC_FORMAT_ITF: return "ITF";
	case ZXC_FORMAT_ITF14: return "ITF14";
	case ZXC_FORMAT_DATABAR_OMNI: return "DATABAR_OMNI";
	case ZXC_FORMAT_DATABAR_STACKED: return "DATABAR_STACKED";
	case ZXC_FORMAT_DATABAR_STACKED_OMNI: return "DATABAR_STACKED_OMNI";
	case ZXC_FORMAT_DATABAR_LIMITED: return "DATABAR_LIMITED";
	case ZXC_FORMAT_DATABAR_EXPANDED: return "DATABAR_EXPANDED";
	case ZXC_FORMAT_DATABAR_EXPANDED_STACKED: return "DATABAR_EXPANDED_STACKED";
	case ZXC_FORMAT_EAN13: return "EAN13";
	case ZXC_FORMAT_EAN8: return "EAN8";
	case ZXC_FORMAT_ISBN: return "ISBN";
	case ZXC_FORMAT_UPCA: return "UPCA";
	case ZXC_FORMAT_UPCE: return "UPCE";
	case ZXC_FORMAT_TELEPEN_ALPHA: return "TELEPEN_ALPHA";
	case ZXC_FORMAT_TELEPEN_NUMERIC: return "TELEPEN_NUMERIC";
	case ZXC_FORMAT_DX_FILM_EDGE: return "DX_FILM_EDGE";
	case ZXC_FORMAT_PDF417: return "PDF417";
	case ZXC_FORMAT_COMPACT_PDF417: return "COMPACT_PDF417";
	case ZXC_FORMAT_MICRO_PDF417: return "MICRO_PDF417";
	case ZXC_FORMAT_AZTEC_CODE: return "AZTEC_CODE";
	case ZXC_FORMAT_AZTEC_RUNE: return "AZTEC_RUNE";
	case ZXC_FORMAT_QR_M1: return "QR_M1";
	case ZXC_FORMAT_QR_M2: return "QR_M2";
	case ZXC_FORMAT_MICRO_QR: return "MICRO_QR";
	case ZXC_FORMAT_RMQR: return "RMQR";
	case ZXC_FORMAT_DATA_MATRIX: return "DATA_MATRIX";
	case ZXC_FORMAT_MAXICODE: return "MAXICODE";
	case ZXC_FORMAT_CODE39: return "CODE39";
	case ZXC_FORMAT_DATABAR: return "DATABAR";
	case ZXC_FORMAT_EAN_UPC: return "EAN_UPC";
	case ZXC_FORMAT_TELEPEN: return "TELEPEN";
	case ZXC_FORMAT_OTHER_BARCODE: return "OTHER_BARCODE";
	case ZXC_FORMAT_AZTEC: return "AZTEC";
	case ZXC_FORMAT_QR: return "QR";
	default: return "UNKNOWN";
	}
}

void scanner_zxc_submit(const struct video_buffer *buffer,
			   const struct video_format *format, int id)
{
	int64_t now = k_uptime_get();
	/* Snapshot requests must be accepted even if a periodic job is queued.
	 * Only the camera thread submits; the worker owns the other pixel buffer. */
	k_mutex_lock(&input_lock, K_FOREVER);
	if (id == 0 && (!ready || pending || now < next_periodic_ms)) {
		goto out;
	}
	struct scan_job job = {
		.id = id, .ticket = ++next_ticket, .source_width = format->width,
		.source_height = format->height, .captured_ms = now,
	};
	int rc = ready ? scanner_zxc_gray(input_pixels, SCAN_WIDTH, SCAN_HEIGHT,
		buffer->buffer, buffer->bytesused, format->pixelformat,
		format->width, format->height, format->pitch,
		&job.width, &job.height, &job.scale) : init_error;
	if (id > 0) {
		k_mutex_lock(&result_lock, K_FOREVER);
		snapshot_result.job = job;
		snapshot_result.error = rc ? rc : -EAGAIN;
		snapshot_result.count = 0;
		k_mutex_unlock(&result_lock);
	}
	if (rc) {
		if (id == 0) {
			k_mutex_lock(&result_lock, K_FOREVER);
			latest.job = job;
			latest.error = rc;
			latest.count = 0;
			k_mutex_unlock(&result_lock);
		}
		goto out;
	}
	pending_job = job;
	pending = true;
	next_periodic_ms = now + CONFIG_APP_CAMERA_BARCODE_INTERVAL_MS;
	k_sem_give(&work_ready);
out:
	k_mutex_unlock(&input_lock);
}

static void scan_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	zxc_options_t options;
	zxc_output_t output;
	zxc_context_t *context = NULL;
	k_mutex_lock(&input_lock, K_FOREVER);
#if ZXC_USE_HEAP
	zxc_status_t status = zxc_create(&context);
	init_error = status == ZXC_OK ? 0 : (status == ZXC_ENOMEM ? -ENOMEM : -EINVAL);
#else
	zxc_limits_t limits;
	zxc_limits_init(&limits);
	limits.max_width = SCAN_WIDTH;
	limits.max_height = SCAN_HEIGHT;
	limits.max_symbols = 32;
	limits.max_format_selectors = 8;
	limits.max_linear_states = 32;
	limits.max_qr_candidates = 128;
	limits.max_dm_candidates = 128;
	limits.max_aztec_candidates = 32;
	limits.max_pdf_rows = MAX(SCAN_WIDTH, SCAN_HEIGHT) + 2;
	limits.max_maxicode_candidates = 8;
	limits.max_dedup_records = 64;
	size_t bytes = zxc_workspace_size(&limits);
	size_t alignment = MAX((size_t)32, zxc_workspace_alignment());
	void *workspace = NULL;
	if (bytes && bytes <= CONFIG_APP_CAMERA_BARCODE_WORKSPACE_LIMIT) {
		workspace = shared_multi_heap_aligned_alloc(CONFIG_VIDEO_BUFFER_SMH_ATTRIBUTE,
			alignment, ROUND_UP(bytes, alignment));
	}
	init_error = !workspace ? -ENOMEM :
		(zxc_init(&context, workspace, bytes, &limits) == ZXC_OK ? 0 : -EINVAL);
#endif
	if (!init_error) {
		input_pixels = shared_multi_heap_aligned_alloc(CONFIG_VIDEO_BUFFER_SMH_ATTRIBUTE,
			32, ROUND_UP(SCAN_WIDTH * SCAN_HEIGHT, 32));
		scan_pixels = shared_multi_heap_aligned_alloc(CONFIG_VIDEO_BUFFER_SMH_ATTRIBUTE,
			32, ROUND_UP(SCAN_WIDTH * SCAN_HEIGHT, 32));
	}
	if (!init_error && (!input_pixels || !scan_pixels)) {
		init_error = -ENOMEM;
	}
	ready = !init_error;
	if (!ready) {
#if ZXC_USE_HEAP
		if (context) { zxc_destroy(context); }
#else
		if (workspace) { shared_multi_heap_free(workspace); }
#endif
		if (input_pixels) { shared_multi_heap_free(input_pixels); input_pixels = NULL; }
		if (scan_pixels) { shared_multi_heap_free(scan_pixels); scan_pixels = NULL; }
		LOG_ERR("ZXC heap%d init failed (%d)", ZXC_USE_HEAP, init_error);
		k_mutex_lock(&result_lock, K_FOREVER);
		latest.error = init_error;
		k_mutex_unlock(&result_lock);
		k_mutex_unlock(&input_lock);
		return;
	}
	k_mutex_unlock(&input_lock);
	LOG_INF("ZXC heap%d ready: gray %ux%u, all readable formats",
		ZXC_USE_HEAP, SCAN_WIDTH, SCAN_HEIGHT);
	zxc_options_init(&options);
	options.max_symbols = MAX_SYMBOLS;
	zxc_output_init(&output);
#if !ZXC_USE_HEAP
	output.items = output_items; output.capacity = ARRAY_SIZE(output_items);
	output.payload = output_payload; output.payload_capacity = sizeof(output_payload);
	output.segments = output_segments; output.segment_capacity = ARRAY_SIZE(output_segments);
	output.metadata = output_metadata; output.metadata_capacity = ARRAY_SIZE(output_metadata);
#endif
	int generation = 0;
	while (true) {
		k_sem_take(&work_ready, K_FOREVER);
		k_mutex_lock(&input_lock, K_FOREVER);
		if (!pending) {
			k_mutex_unlock(&input_lock);
			continue;
		}
		memset(&working, 0, sizeof(working));
		working.job = pending_job;
		uint8_t *swap = scan_pixels;
		scan_pixels = input_pixels;
		input_pixels = swap;
		pending = false;
		k_mutex_unlock(&input_lock);
		zxc_plane8_t image = ZXC_PLANE8_INIT;
		image.data = scan_pixels;
		image.width = image.stride = working.job.width;
		image.height = working.job.height;
		int64_t start = k_uptime_get();
		working.status = zxc_scan_gray8(context, &image, &options, &output);
		working.elapsed_ms = k_uptime_get() - start;
		working.flags = zxc_last_capacity_flags(context);
		working.generation = generation = generation == INT_MAX ? 1 : generation + 1;
		if (ZXC_STATUS_HAS_OUTPUT(working.status)) {
			size_t payload_used = MIN(output.payload_used, sizeof(working.payload));
			if (output.payload_used > payload_used || output.count > MAX_SYMBOLS) {
				working.status = ZXC_PARTIAL;
				working.flags |= ZXC_CAPACITY_OUTPUT;
			}
			if (payload_used) { memcpy(working.payload, output.payload, payload_used); }
			for (size_t i = 0; i < output.count && working.count < MAX_SYMBOLS; i++) {
				const zxc_result_t *result = &output.items[i];
				if (result->decode_status != ZXC_DECODE_VALID ||
				    !result->upstream_format.symbology ||
				    result->payload_offset > payload_used ||
				    result->payload_length > payload_used - result->payload_offset) {
					continue;
				}
				working.items[working.count++] = *result;
			}
		} else {
			working.error = working.status == ZXC_ENOMEM ? -ENOMEM : -EIO;
		}
#if ZXC_USE_HEAP
		zxc_output_release(context, &output);
#endif
		k_mutex_lock(&result_lock, K_FOREVER);
		latest = working;
		if (working.job.id > 0 && snapshot_result.job.ticket == working.job.ticket) {
			snapshot_result = working;
		}
		k_mutex_unlock(&result_lock);
		LOG_DBG("Scan %d: %zu symbols, %u ms, status %d flags %x",
			generation, working.count, working.elapsed_ms, working.status, working.flags);
	}
}
K_THREAD_DEFINE(camera_decoder, CONFIG_APP_CAMERA_BARCODE_STACK_SIZE,
		scan_thread, NULL, NULL, NULL, 8, 0, 0);

void scanner_zxc_label(char *text, size_t capacity, struct scanner_zxc_overlay *overlay)
{
	if (!capacity) { return; }
	k_mutex_lock(&result_lock, K_FOREVER);
	int64_t age = k_uptime_get() - latest.job.captured_ms;
	memset(overlay, 0, sizeof(*overlay));
	overlay->width = latest.job.source_width;
	overlay->height = latest.job.source_height;
	if (!latest.error && latest.generation &&
	    age <= MAX(5000, 2 * CONFIG_APP_CAMERA_BARCODE_INTERVAL_MS + latest.elapsed_ms)) {
		overlay->count = latest.count;
		for (size_t i = 0; i < latest.count; i++) {
			for (int j = 0; j < 4; j++) {
				int x = latest.items[i].position.corner[j].x * (int)latest.job.scale +
					latest.job.scale / 2;
				int y = latest.items[i].position.corner[j].y * (int)latest.job.scale +
					latest.job.scale / 2;
				overlay->symbols[i].x[j] = CLAMP(x, 0, (int)overlay->width - 1);
				overlay->symbols[i].y[j] = CLAMP(y, 0, (int)overlay->height - 1);
			}
		}
	}
	int n;
	if (!latest.generation && !latest.error) {
		n = snprintf(text, capacity, "ZXC: initializing / scanning...");
	} else if (latest.error) {
		n = snprintf(text, capacity, "ZXC scan unavailable (%d)", latest.error);
	} else {
		n = snprintf(text, capacity, "ZXC: %zu symbol(s) | %u ms | age %lld ms%s",
			latest.count, latest.elapsed_ms, (long long)age,
			latest.status == ZXC_PARTIAL ? " | partial" : "");
	}
	size_t used = n > 0 ? MIN((size_t)n, capacity - 1) : 0;
	for (size_t i = 0; i < latest.count && i < 3 && used + 1 < capacity; i++) {
		const zxc_result_t *r = &latest.items[i];
		n = snprintf(text + used, capacity - used, "\n%zu: %s: ", i + 1, format_name(r->format));
		if (n < 0) { break; }
		used += MIN((size_t)n, capacity - used - 1);
		size_t count = MIN(r->payload_length, (size_t)72);
		for (size_t j = 0; j < count && used + 1 < capacity; j++) {
			uint8_t ch = latest.payload[r->payload_offset + j];
			text[used++] = ch >= 32 && ch <= 126 ? ch : '.';
		}
		text[used] = '\0';
		if (count < r->payload_length && used + 4 < capacity) {
			memcpy(text + used, "...", 4);
			used += 3;
		}
	}
	k_mutex_unlock(&result_lock);
}

static int number(const char *value, int *out)
{
	char *end;
	if (!value || *value < '0' || *value > '9') { return -EINVAL; }
	errno = 0;
	unsigned long n = strtoul(value, &end, 10);
	if (errno || *end || n > INT_MAX) { return -EINVAL; }
	*out = n;
	return 0;
}

/* cam_decode <snapshot id (0=latest)> <generation> <index (0=summary)> <offset>
 * Generation guards multi-packet results; snapshot results survive later live scans.
 * Payload is chunked hexadecimal, preserving binary/GS1 data and HU delimiters.
 */
static void decode_command(void *handle, int argc, const char **argv)
{
	struct hup_handle *h = handle;
	int id, generation, index, offset, rc = -EINVAL;
	if ((h->id && strlen(h->id) > 16) || (h->sequence && strlen(h->sequence) > 16)) {
		h->id = NULL; h->sequence = NULL;
		hupacket_nak_response(h, NULL, -EMSGSIZE);
		hupacket_send_buffer(h, NULL);
		return;
	}
	k_mutex_lock(&result_lock, K_FOREVER);
	if (argc != 5 || number(argv[1], &id) || number(argv[2], &generation) ||
	    number(argv[3], &index) || number(argv[4], &offset)) { goto out; }
	struct scan_result *s = id ? &snapshot_result : &latest;
	if ((id && s->job.id != id) || (!id && !s->generation)) {
		rc = id ? -ESTALE : -EAGAIN; goto out;
	}
	if (s->error) { rc = s->error; goto out; }
	if (index > s->count || (index == 0 && (offset || generation))) { goto out; }
	if (index && generation != s->generation) { rc = -ESTALE; goto out; }
	if (index && (size_t)offset > s->items[index - 1].payload_length) { goto out; }
	hupacket_ack_response(h, NULL);
	hupacket_record_int(h, NULL, id);
	hupacket_record_int(h, NULL, s->generation);
	if (index == 0) {
		hupacket_record_int(h, NULL, s->count);
		hupacket_record_int(h, NULL, s->status);
		hupacket_record_hex(h, NULL, s->flags);
		hupacket_record_int(h, NULL, s->job.source_width);
		hupacket_record_int(h, NULL, s->job.source_height);
		hupacket_record_int(h, NULL, s->job.width);
		hupacket_record_int(h, NULL, s->job.height);
		hupacket_record_int(h, NULL, s->elapsed_ms);
		hupacket_record_int(h, NULL, MIN(k_uptime_get() - s->job.captured_ms, INT_MAX));
	} else {
		const zxc_result_t *r = &s->items[index - 1];
		hupacket_record_int(h, NULL, index);
		hupacket_record_str(h, NULL, format_name(r->format));
		hupacket_record_str(h, NULL, r->symbology_id);
		hupacket_record_int(h, NULL, r->payload_length);
		hupacket_record_int(h, NULL, offset);
		hupacket_record_str(h, NULL, "");
		size_t bytes = MIN((size_t)DATA_CHUNK, r->payload_length - offset);
		hupacket_append_hex_bytes(h, NULL, s->payload + r->payload_offset + offset, bytes);
		for (int i = 0; i < 4; i++) {
			int x = r->position.corner[i].x * (int)s->job.scale + s->job.scale / 2;
			int y = r->position.corner[i].y * (int)s->job.scale + s->job.scale / 2;
			hupacket_record_int(h, NULL, CLAMP(x, 0, (int)s->job.source_width - 1));
			hupacket_record_int(h, NULL, CLAMP(y, 0, (int)s->job.source_height - 1));
		}
		hupacket_record_int(h, NULL, r->has_eci);
	}
	rc = 0;
out:
	if (rc) { hupacket_nak_response(h, NULL, rc); }
	k_mutex_unlock(&result_lock);
	hupacket_send_buffer(h, NULL);
}
DEFINE_HUP_CMD(hup_cmd_cam_decode, "cam_decode", decode_command);
#endif /* CONFIG_APP_CAMERA_BARCODE */
