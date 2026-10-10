/* SPDX-License-Identifier: Apache-2.0 */
#include "scanner_zxc.h"
#include "fixture.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FCC(a,b,c,d) ((uint32_t)(a) | ((uint32_t)(b)<<8) | ((uint32_t)(c)<<16) | ((uint32_t)(d)<<24))
static uint8_t gray[800 * 480], rgb[800 * 480 * 2];
#if ZXC_USE_HEAP
#include "shared_heap_test.h"
#else
static zxc_result_t items[8];
static uint8_t payload[16384];
static zxc_segment_t segments[128];
static zxc_metadata_t metadata[128];
#endif

static void scan(zxc_context_t *ctx, unsigned width, unsigned height, const char *expected)
{
	zxc_plane8_t image = ZXC_PLANE8_INIT;
	image.data = gray; image.width = image.stride = width; image.height = height;
	zxc_options_t options;
	zxc_options_init(&options);
	options.max_symbols = 8;
	zxc_output_t out;
	zxc_output_init(&out);
#if !ZXC_USE_HEAP
	out.items = items; out.capacity = 8;
	out.payload = payload; out.payload_capacity = sizeof(payload);
	out.segments = segments; out.segment_capacity = 128;
	out.metadata = metadata; out.metadata_capacity = 128;
#endif
	zxc_status_t status = zxc_scan_gray8(ctx, &image, &options, &out);
	assert(ZXC_STATUS_HAS_OUTPUT(status));
	if (!expected) { assert(out.count == 0); }
	int found = 0;
	for (size_t i = 0; expected && i < out.count; i++) {
		if (out.items[i].decode_status == ZXC_DECODE_VALID &&
		    out.items[i].payload_length == strlen(expected) &&
		    !memcmp(out.payload + out.items[i].payload_offset, expected, strlen(expected))) {
			found = 1;
		}
	}
	assert(!expected || found);
#if ZXC_USE_HEAP
	zxc_output_release(ctx, &out);
#endif
}

int main(void)
{
	zxc_context_t *ctx;
#if ZXC_USE_HEAP
	assert(zxc_create(&ctx) == ZXC_OK);
#else
	zxc_limits_t limits;
	fixture_limits(&limits);
	limits.max_width = 800; limits.max_height = 480;
	limits.max_symbols = 32; limits.max_format_selectors = 8;
	limits.max_linear_states = 32; limits.max_qr_candidates = 128;
	limits.max_dm_candidates = 128; limits.max_aztec_candidates = 32;
	limits.max_pdf_rows = 802; limits.max_maxicode_candidates = 8;
	limits.max_dedup_records = 64;
	size_t size = zxc_workspace_size(&limits), alignment = zxc_workspace_alignment();
	printf("ZXC host workspace: %zu bytes\n", size);
	assert(size > 0 && size <= 6291456);
	void *workspace = aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
	assert(workspace && zxc_init(&ctx, workspace, size, &limits) == ZXC_OK);
#endif

	/* QR through RGB565 rows with padding, then exact decoded payload. */
	const unsigned side = FIXTURE_SIDE * 4, pitch = side * 2 + 8;
	memset(rgb, 0x55, sizeof(rgb));
	for (unsigned y = 0; y < side; y++) {
		for (unsigned x = 0; x < side; x++) {
			unsigned i = (y / 4) * FIXTURE_SIDE + x / 4;
			uint8_t value = fixture_bits[i / 8] & (1u << (i % 8)) ? 0 : 255;
			rgb[y * pitch + 2 * x] = rgb[y * pitch + 2 * x + 1] = value;
		}
	}
	uint32_t w, h, scale;
	/* Channel orders and native luma planes must agree on red/green/blue. */
	static const struct { uint32_t fourcc; unsigned bpp; uint8_t pixels[12]; } colors[] = {
		{FCC('R','G','B','P'), 2, {0,248,224,7,31,0}},
		{FCC('R','G','B','3'), 3, {255,0,0,0,255,0,0,0,255}},
		{FCC('B','G','R','3'), 3, {0,0,255,0,255,0,255,0,0}},
		{FCC('B','A','2','4'), 4, {7,255,0,0,7,0,255,0,7,0,0,255}},
		{FCC('R','A','2','4'), 4, {0,0,0,255,0,0,255,0,0,255,0,0}},
		{FCC('A','B','2','4'), 4, {255,0,0,7,0,255,0,7,0,0,255,7}},
		{FCC('A','R','2','4'), 4, {0,0,255,7,0,255,0,7,255,0,0,7}},
	};
	for (size_t i = 0; i < sizeof(colors)/sizeof(colors[0]); i++) {
		assert(scanner_zxc_gray(gray, 800, 480, colors[i].pixels, sizeof(colors[i].pixels),
			colors[i].fourcc, 3, 1, 3 * colors[i].bpp, &w, &h, &scale) == 0);
		assert(gray[0] == 77 && gray[1] == 149 && gray[2] == 29);
	}
	static const uint32_t planar[] = {FCC('N','V','1','2'), FCC('N','V','2','1'),
		FCC('N','V','1','6'), FCC('N','V','6','1'), FCC('Y','U','1','2'), FCC('Y','V','1','2')};
	const uint8_t luminance[] = {10,30,80,240,128,128,128,128};
	for (size_t i = 0; i < sizeof(planar)/sizeof(planar[0]); i++) {
		assert(scanner_zxc_gray(gray, 800, 480, luminance, sizeof(luminance),
			planar[i], 2, 2, 4, &w, &h, &scale) == 0);
		assert(!memcmp(gray, luminance, 4));
	}
	const uint8_t packed_y[] = {10,128,30,128,80,128,240,128};
	assert(scanner_zxc_gray(gray, 800, 480, packed_y, sizeof(packed_y),
		FCC('Y','U','Y','V'), 2, 2, 4, &w, &h, &scale) == 0);
	assert(!memcmp(gray, luminance, 4));
	assert(scanner_zxc_gray(gray, 800, 480, packed_y, sizeof(packed_y),
		FCC('Y','V','Y','U'), 2, 2, 4, &w, &h, &scale) == 0);
	assert(!memcmp(gray, luminance, 4));
	assert(scanner_zxc_gray(gray, 800, 480, luminance, 4,
		FCC('G','R','E','Y'), 2, 2, 2, &w, &h, &scale) == 0);
	assert(!memcmp(gray, luminance, 4));
	assert(scanner_zxc_gray(gray, 800, 480, rgb, pitch * side,
		FCC('R','G','B','P'), side, side, pitch, &w, &h, &scale) == 0);
	scan(ctx, w, h, fixture_payload);
	assert(scanner_zxc_gray(gray, 100, 100, rgb, pitch * side,
		FCC('R','G','B','P'), side, side, pitch, &w, &h, &scale) == 0);
	assert(w == 82 && h == 82 && scale == 2);
	scan(ctx, w, h, fixture_payload);
	assert(scanner_zxc_gray(gray, 800, 480, rgb, 10,
		FCC('R','G','B','P'), side, side, pitch, &w, &h, &scale) == -EINVAL);
	assert(scanner_zxc_gray(gray, 800, 480, rgb, sizeof(rgb),
		FCC('J','P','E','G'), side, side, pitch, &w, &h, &scale) == -ENOTSUP);

	/* Code 128 run lengths from ZXC's reverse-row regression fixture. */
	static const unsigned runs[] = {
		70,8,4,3,8,4,14,4,11,3,12,7,3,5,10,15,3,5,3,4,14,4,8,7,4,4,3,7,8,4,
		15,7,4,7,8,7,7,4,7,12,7,7,4,8,7,11,7,4,4,10,5,3,7,8,7,4,4,4,14,7,8,8,11,11,4,3,4,7,65
	};
	w = 0;
	for (size_t i = 0; i < sizeof(runs) / sizeof(runs[0]); i++) { w += runs[i]; }
	assert(w <= 800);
	memset(gray, 255, sizeof(gray));
	for (unsigned y = 20; y < 100; y++) {
		unsigned x = 0;
		for (size_t i = 0; i < sizeof(runs) / sizeof(runs[0]); i++) {
			memset(gray + y * w + x, i % 2 ? 0 : 255, runs[i]); x += runs[i];
		}
	}
	scan(ctx, w, 120, "Code 128");
	memset(gray, 255, sizeof(gray));
	scan(ctx, 800, 480, NULL);
#if ZXC_USE_HEAP
	zxc_destroy(ctx);
	assert(heap_test_live_bytes == 0);
	printf("ZXC heap1 allocator peak: %zu bytes\n", heap_test_peak_bytes);
#else
	free(workspace);
#endif
	puts("PASS: padded RGB565 QR, resized QR, Code128, blank, invalid and unsupported frames");
	return 0;
}
