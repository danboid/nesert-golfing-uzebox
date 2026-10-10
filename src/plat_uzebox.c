//
// plat_uzebox.c - Uzebox platform layer (video mode 6)
//
// Built with avr-gcc against the Uzebox kernel and run in Uzem (see README, "Testing in Uzem"); not yet run on
// hardware. Everything above this layer (game.c, render.c, sfx.c) is also built and played on the host against a
// simulated mode 6 screen (see host/).
//
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <stdbool.h>
#include <util/atomic.h>
#include <util/delay.h>
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
// Controllers: a SNES pad and/or a SNES mouse, in either port (the intended setup is pad in port 1, mouse in port 2).
//
// The port reads the controllers itself, once per VSYNC from the pre-VSYNC callback, instead of using the kernel's
// reader (built with CONTROLLERS_VSYNC_READ=0 and SNES_MOUSE=0). Reason: the Hyperkin "Hyper Click" clone mouse returns
// corrupted reports when it is clocked too fast (SNESdev wiki, "Mouse"): it needs at least 170 SNES master cycles
// (7.9 us) between bit reads and at least 336 (15.6 us) between bits 16 and 17. The kernel clocks the first 16 bits
// about 1 us apart. Here bits are at least 9 us apart (10.6 us measured in cuzebox-ex), with 20 us before bit 17, which suits the Hyperkin, the Nintendo
// mouse and pads alike. Cost: about 0.2 ms per frame (0.4 ms with a mouse plugged in), inside the VSYNC interrupt.
//
// Each port's 16-bit report is classified: bit 15 clear = pad; bits 0-7 and 12-14 clear with bit 15 set = mouse
// (signature 0001, then 16 more bits are read: Y and X, each sign + 7-bit magnitude); anything else = a corrupted
// read, ignored. A port that reported a mouse in the last second is never read as a pad, so a bad read cannot turn
// into pad presses (a mouse's buttons are bits 8/9, a pad's A/X). Mouse motion is summed every VSYNC and handed
// out by plat_mouse(), so frames that span several VSYNCs lose nothing.

#if CONTROLLERS_VSYNC_READ != 0 || SNES_MOUSE != 0
  #error "build with -DCONTROLLERS_VSYNC_READ=0 -DSNES_MOUSE=0 (default/Makefile): plat_uzebox.c reads the controllers itself"
#endif

#define CTR_BIT_US      9      // between bit reads (Hyperkin needs >= 7.9)
#define CTR_GAP_US     20      // between bits 16 and 17 (Hyperkin needs >= 15.6)
#define MOUSE_HOLD     60      // VSYNCs a port stays "mouse" after its last valid mouse report

static volatile unsigned int pad_word[2];        // last valid pad report per port (0 = none)
static volatile uint8  mouse_btn;                // MOUSE_* of the mouse port, from the last valid report
static volatile uint8  mouse_hold[2];            // > 0: this port holds a mouse
static volatile sint16 mouse_acc_x, mouse_acc_y; // motion summed since plat_mouse() last took it

static sint8 mouse_axis(uint8 v) { return (v & 0x80) ? -(sint8)(v & 0x7F) : (sint8)(v & 0x7F); }
static sint16 clamp_acc(sint16 v) { return (v > 1000) ? 1000 : (v < -1000) ? -1000 : v; }

// clock one bit out of both ports: same edge order as the kernel (clock low, sample, clock high).
// Returns bit 0 = port 1 data, bit 1 = port 2 data (1 = line low = bit set).
static inline uint8 ctr_bit(void)
{
	uint8 r;
	JOYPAD_OUT_PORT &= ~_BV(JOYPAD_CLOCK_PIN);
	_delay_us(2);
	r = ~JOYPAD_IN_PORT & (_BV(JOYPAD_DATA1_PIN) | _BV(JOYPAD_DATA2_PIN));
	JOYPAD_OUT_PORT |= _BV(JOYPAD_CLOCK_PIN);
	_delay_us(CTR_BIT_US - 2);
	return r;
}

static void ctr_vsync(void)                      // pre-VSYNC callback (interrupt context, interrupts enabled)
{
	unsigned int lo[2] = { 0, 0 }, hi[2] = { 0, 0 };
	uint8 i, b, p, mouse_now = 0;

	JOYPAD_OUT_PORT |= _BV(JOYPAD_LATCH_PIN);
	_delay_us(12);
	JOYPAD_OUT_PORT &= ~_BV(JOYPAD_LATCH_PIN);
	_delay_us(6);
	for (i = 0; i < 16; ++i) {                   // bit n of the report -> bit n of lo[]
		b = ctr_bit();
		lo[0] >>= 1; lo[1] >>= 1;
		if (b & _BV(JOYPAD_DATA1_PIN)) lo[0] |= 0x8000;
		if (b & _BV(JOYPAD_DATA2_PIN)) lo[1] |= 0x8000;
	}
	for (p = 0; p < 2; ++p)
		if ((lo[p] & 0xF0FF) == 0x8000) mouse_now |= 1 << p;   // valid mouse signature and empty first byte
	if (mouse_now) {
		_delay_us(CTR_GAP_US);
		for (i = 0; i < 16; ++i) {               // first bit read -> bit 15 of hi[] (Y in the high byte, X in the low)
			b = ctr_bit();
			hi[0] <<= 1; hi[1] <<= 1;
			if (b & _BV(JOYPAD_DATA1_PIN)) hi[0] |= 1;
			if (b & _BV(JOYPAD_DATA2_PIN)) hi[1] |= 1;
		}
	}
	for (p = 0; p < 2; ++p) {
		if (mouse_now & (1 << p)) mouse_hold[p] = MOUSE_HOLD;
		else if (mouse_hold[p]) --mouse_hold[p];
		// a pad: bit 15 clear, and not a port that recently held a mouse (then the read is ignored)
		pad_word[p] = (!(lo[p] & 0x8000) && !mouse_hold[p]) ? lo[p] : 0;
	}
	p = (mouse_now & 1) ? 0 : (mouse_now & 2) ? 1 : 2;           // first port with a valid mouse report
	if (p < 2) {
		mouse_btn = ((lo[p] & 0x0200) ? MOUSE_L : 0) | ((lo[p] & 0x0100) ? MOUSE_R : 0);
		mouse_acc_x = clamp_acc(mouse_acc_x + mouse_axis((uint8)hi[p]));
		mouse_acc_y = clamp_acc(mouse_acc_y + mouse_axis((uint8)(hi[p] >> 8)));
	} else if (!mouse_hold[0] && !mouse_hold[1]) {
		mouse_btn = 0;                           // mouse gone (a single bad read keeps the last buttons)
	}
}

void   plat_init(void) { SetUserPreVsyncCallback(ctr_vsync); }
void   plat_vsync(void) { WaitVsync(1); }

uint8 plat_pad(void)
{
	uint8 p = 0;
	for (uint8 port = 0; port < 2; ++port) {
		unsigned int b;
		ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { b = pad_word[port]; }
		if (b & BTN_A)      p |= PAD_A;
		if (b & BTN_B)      p |= PAD_B;
		if (b & BTN_SELECT) p |= PAD_SELECT;
		if (b & BTN_START)  p |= PAD_START;
		if (b & BTN_UP)     p |= PAD_UP;
		if (b & BTN_DOWN)   p |= PAD_DOWN;
		if (b & BTN_LEFT)   p |= PAD_LEFT;
		if (b & BTN_RIGHT)  p |= PAD_RIGHT;
	}
	return p;
}

uint8 plat_mouse_present(void) { return (mouse_hold[0] | mouse_hold[1]) ? 1 : 0; }

static sint8 clamp127(sint16 v) { return (v > 127) ? 127 : (v < -127) ? -127 : (sint8)v; }

uint8 plat_mouse(sint8 *dx, sint8 *dy)
{
	uint8 m;
	sint16 x, y;
	// like the NES game at most +-127 per poll; any excess stays for the next poll. Plain loads/stores only, so
	// interrupts are off for a few dozen cycles.
	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		m = mouse_btn;
		x = mouse_acc_x; y = mouse_acc_y;
		*dx = clamp127(x); *dy = clamp127(y);
		mouse_acc_x = x - *dx; mouse_acc_y = y - *dy;
	}
	return m;
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
