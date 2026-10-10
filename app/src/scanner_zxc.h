/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef APP_SCANNER_ZXC_H
#define APP_SCANNER_ZXC_H
#include <stddef.h>
#include <stdint.h>

struct video_buffer;
struct video_format;

/* Convert a bounded native frame to grayscale without modifying the source.
 * scale is an integer sampling factor; the caller owns dst (max_w * max_h).
 */
int scanner_zxc_gray(uint8_t *dst, uint32_t max_w, uint32_t max_h,
	const uint8_t *src, size_t bytes, uint32_t fourcc, uint32_t width,
	uint32_t height, uint32_t pitch, uint32_t *out_w, uint32_t *out_h,
	uint32_t *scale);

#ifdef CONFIG_APP_CAMERA_BARCODE
#define SCANNER_ZXC_MAX_SYMBOLS 8
struct scanner_zxc_overlay {
	uint32_t width, height;
	size_t count;
	struct { int32_t x[4], y[4]; } symbols[SCANNER_ZXC_MAX_SYMBOLS];
};
/* id=0: periodic preview scan; id>0: prioritize this HU snapshot. */
void scanner_zxc_submit(const struct video_buffer *buffer,
			   const struct video_format *format, int id);
/* Text and corners are copied from one completed scan under the same lock. */
void scanner_zxc_label(char *text, size_t capacity, struct scanner_zxc_overlay *overlay);
#else
static inline void scanner_zxc_submit(const struct video_buffer *buffer,
					const struct video_format *format, int id)
{
	(void)buffer; (void)format; (void)id;
}
#endif
#endif
