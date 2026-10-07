// SPDX-License-Identifier: Apache-2.0 OR GPL-2.0-or-later
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <hal/display/display_engine.h>
#include <dpy/dpy_os.h>

#include "f101_display.h"
#include "f101_ll.h"

#define FB_PLANE	4	/* UI channel 0, layer 0 */
#define MBUS_BASE	0x03102000u

/* the display engine gets the highest priority at the memory bus, the core a bandwidth limit */
static void mbus_setup(void)
{
	uint32_t msc = MBUS_BASE + 0x210 + 16 * 0x10;	/* master 16: display engine */
	uint32_t bwlr = MBUS_BASE + 0x218 + 39 * 0x10;	/* master 39: RISC-V */

	F101_REG32(msc) = (F101_REG32(msc) & ~(3u << 2)) | (3u << 2);
	F101_REG32(bwlr) = (F101_REG32(bwlr) & ~((0xfffu << 16) | (1u << 31))) |
			   ((256u * 500u / 252u) << 16) | (1u << 31);
}

int f101_display_init(uint32_t *width, uint32_t *height, uint32_t *refresh_hz)
{
	struct display_mode mode;

	mbus_setup();
	if (display_probe() || display_wait_ready(5000) || display_get_mode(&mode))
		return -1;
	*width = mode.width;
	*height = mode.height;
	*refresh_hz = mode.refresh_hz;
	return 0;
}

int f101_display_show(uintptr_t fb, uint32_t stride, uint32_t width, uint32_t height)
{
	struct display_pipeline_state st;
	struct display_plane_state *pl;

	display_pipeline_state_init(&st);
	st.plane_count = 0;
	pl = &st.planes[st.plane_count++];
	memset(pl, 0, sizeof(*pl));
	pl->enable = true;
	pl->plane_id = FB_PLANE;
	pl->alpha = 0xff;
	pl->blend_mode = DISPLAY_BLEND_NONE;
	pl->framebuffer.address = fb;
	pl->framebuffer.plane_address[0] = fb;
	pl->framebuffer.plane_stride[0] = stride;
	pl->framebuffer.plane_count = 1;
	pl->framebuffer.format = DISPLAY_FORMAT_ARGB8888;
	pl->framebuffer.width = width;
	pl->framebuffer.height = height;
	pl->framebuffer.stride = stride;
	pl->destination.width = width;
	pl->destination.height = height;
	return display_submit(&st);
}

void f101_display_backlight(uint32_t level)
{
	display_set_backlight(level);
}

void f101_display_blank(int blank)
{
	display_blank(blank != 0);
}
