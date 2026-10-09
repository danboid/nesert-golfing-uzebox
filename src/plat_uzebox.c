//
// plat_uzebox.c - Uzebox platform layer (video mode 6)
//
// Type-checked against the real kernel headers (tools/typecheck_kernel.sh) but NOT assembled, linked or run:
// there was no avr-gcc where this was written. Everything above this layer (game.c, render.c, sfx.c) was built
// and played on the host against a simulated mode 6 screen (see host/).
//
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdbool.h>
#include <uzebox.h>
#include "nesert.h"
#include "patches.inc"      // const struct PatchStruct patches[]

// ---------------------------------------------------------------- video memory
// Checked against videoMode6.s: `vram` (VRAM_TILES_H x 28 bytes, 0 = "unallocated"/blank) is followed by
// `ramTiles` (RAM_TILES_COUNT x 8 bytes); tile data is one byte per line, bit 7 = leftmost pixel, set bit =
// foreground colour. That is exactly the layout render.c writes.
extern u8 vram[];
extern u8 ramTiles[];
uint8 *plat_vram  = vram;
uint8 *plat_tiles = ramTiles;

#if VRAM_TILES_H != VRAM_STRIDE
  #error "VRAM_STRIDE (nesert.h) must equal VRAM_TILES_H of the mode 6 kernel: apply kernel_patch/ and build with -DM6_HIRES=0 (30 columns)."
#endif
#if RAM_TILES_COUNT != RAMTILES
  #error "RAM_TILES_COUNT (kernel option) must equal RAMTILES (nesert.h)"
#endif
// Note: nothing here calls the kernel's own ClearBuffer/SetPixel2/Line2. SetPixel2 ORs into the tile its VRAM
// cell already points at (no copy-on-write), and ClearBuffer starts 6 rows into vram and runs 6 rows past its end.

// ---------------------------------------------------------------- colours
// With M6_HIRES=0 (the 240px renderer, via kernel_patch/), bg_color/fg_color are loaded at the start of every scanline, so
// SetBackgroundColor()/SetForegroundColor() set sky and ground. The text colour is simply the ground colour.
// (The kernel's hsync hook must be 32-cycle assembly - a C callback cannot be used for a per-line text colour.)
// In the kernel's default M6_HIRES=1 path these calls have no visible effect (output is 0x00/0xFF).
void plat_colors(uint8 bg, uint8 fg, uint8 fg_band, uint8 y0, uint8 y1)
{
	(void)fg_band; (void)y0; (void)y1;
	SetBackgroundColor(bg);
	SetForegroundColor(fg);
}

// ---------------------------------------------------------------- input / timing
void   plat_init(void) {}
void   plat_vsync(void) { WaitVsync(1); }

uint8 plat_pad(void)
{
	unsigned int b = ReadJoypad(0);       // BTN_* from kernel/defines.h (values depend on JOYSTICK: 0 = SNES pad, default)
	uint8 p = 0;
	if (b & BTN_A)      p |= PAD_A;
	if (b & BTN_B)      p |= PAD_B;
	if (b & BTN_SELECT) p |= PAD_SELECT;
	if (b & BTN_START)  p |= PAD_START;
	if (b & BTN_UP)     p |= PAD_UP;
	if (b & BTN_DOWN)   p |= PAD_DOWN;
	if (b & BTN_LEFT)   p |= PAD_LEFT;
	if (b & BTN_RIGHT)  p |= PAD_RIGHT;
	return p;
}

uint16 plat_entropy(void) { return (uint16)TCNT1; }   // free running video timer: varies with when START is pressed
void   plat_soft_reset(void) { SoftReset(); }

// ---------------------------------------------------------------- sound
// Checked against uzebox.h / kernel.h / uzeboxSoundEngine.c: TriggerNote(channel, patch, note, volume), patches as
// {type, pcmData, cmdStream, loopStart, loopEnd}, command ids in defines.h. Pulse effects play on wave channel 0
// (waves 3/4 = 25%/50% square from data/sounds.inc), noise effects on channel 3. On the noise channel the kernel
// takes the *note* argument as the patch index, which is why sfx.c passes the patch id twice.
// Patches are generated from the NES scripts by tools/gen_data.py (data_sfx_cmd.h); see patches.inc.
void plat_note(uint8 channel, uint8 patch, uint8 note, uint8 vol)
{
	TriggerNote(channel, patch, note, vol);
}

// ---------------------------------------------------------------- entry point
int main(void)
{
	InitMusicPlayer(patches);
	plat_init();
	game_boot();
	for (;;) hole_play();
}
