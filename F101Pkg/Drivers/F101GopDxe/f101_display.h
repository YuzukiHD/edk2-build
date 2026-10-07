/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/*
 * The display of the board for the graphics output driver: bring up the pipeline (display
 * engine, TCON, RGB, panel, backlight), scan out a 32 bit frame buffer.
 */
#ifndef F101_DISPLAY_H_
#define F101_DISPLAY_H_

#include <stdint.h>

/** Probe the pipeline and report the size of the panel; 0 on success */
int f101_display_init(uint32_t *width, uint32_t *height, uint32_t *refresh_hz);

/** Scan out a B8G8R8X8 frame buffer (stride in bytes) */
int f101_display_show(uintptr_t fb, uint32_t stride, uint32_t width, uint32_t height);

void f101_display_backlight(uint32_t level);	/* 0..255 */
void f101_display_blank(int blank);

#endif /* F101_DISPLAY_H_ */
