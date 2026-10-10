// Host test platform: emulates the Mode 6 display (vram + 1bpp ramtiles + per-scanline colours)
// and lets the test driver inject pad input and capture frames.
#include <stdio.h>
#include <stdlib.h>
#include "../src/nesert.h"

static uint8 vram_a[VRAM_STRIDE * SCR_ROWS];
static uint8 tiles_a[RAMTILES * 8];
uint8 *plat_vram = vram_a;
uint8 *plat_tiles = tiles_a;

uint8 (*host_pad_fn)(void);
void  (*host_frame_fn)(void);
unsigned long host_frames;
static uint8 c_bg, c_fg, c_fgb, c_y0, c_y1;
unsigned long sound_events;

void   plat_init(void) {}
void   plat_vsync(void) { ++host_frames; if (host_frame_fn) host_frame_fn(); }
uint8  plat_pad(void) { return host_pad_fn ? host_pad_fn() : 0; }
uint8 (*host_mouse_fn)(sint8 *dx, sint8 *dy);     // optional; no mouse when unset
uint8  plat_mouse_present(void) { return host_mouse_fn ? 1 : 0; }
uint8  plat_mouse(sint8 *dx, sint8 *dy) { *dx = *dy = 0; return host_mouse_fn ? host_mouse_fn(dx, dy) : 0; }
uint16 plat_entropy(void) { return 0x1234; }
void   plat_colors(uint8 bg, uint8 fg, uint8 fgb, uint8 y0, uint8 y1) { c_bg = bg; c_fg = fg; c_fgb = fgb; c_y0 = y0; c_y1 = y1; }
void   plat_note(uint8 ch, uint8 patch, uint8 note, uint8 vol) { (void)ch; (void)patch; (void)note; if (vol) ++sound_events; }
void   plat_soft_reset(void) { fprintf(stderr, "soft reset requested\n"); exit(0); }

static void rgb(uint8 c, uint8 *o)
{
	o[0] = (uint8)(C_R(c) * 255 / 7); o[1] = (uint8)(C_G(c) * 255 / 7); o[2] = (uint8)(C_B(c) * 255 / 3);
}

// scale: pixel replication factor
void host_dump_ppm(const char *path, int scale)
{
	FILE *f = fopen(path, "wb");
	if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", VRAM_STRIDE * 8 * scale, SCR_ROWS * 8 * scale);
	for (int y = 0; y < SCR_ROWS * 8; ++y) {
		uint8 bg[3], fg[3];
		rgb(c_bg, bg);
		rgb((y >= c_y0 && y < c_y1) ? c_fgb : c_fg, fg);
		for (int sy = 0; sy < scale; ++sy)
			for (int x = 0; x < VRAM_STRIDE * 8; ++x) {
				uint8 t = vram_a[(y >> 3) * VRAM_STRIDE + (x >> 3)];
				int bit = tiles_a[t * 8 + (y & 7)] & (0x80 >> (x & 7));
				for (int sx = 0; sx < scale; ++sx) fwrite(bit ? fg : bg, 1, 3, f);
			}
	}
	fclose(f);
}
