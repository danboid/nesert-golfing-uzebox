//
// NESert Golfing for Uzebox (video mode 6)
//
// Port of "NESert Golfing" by Brad Smith (2019-2022), CC BY 4.0
//   https://github.com/bbbradsmith/NESertGolfing
// which is itself derived from Desert Golfing by Justin Smith (2014).
//
#ifndef NESERT_H
#define NESERT_H

#include <stdint.h>
#include <string.h>
#include "nesert_pgm.h"

typedef uint8_t  uint8;
typedef int8_t   sint8;
typedef uint16_t uint16;
typedef int16_t  sint16;
typedef uint32_t uint32;
typedef int32_t  sint32;

// ---------------------------------------------------------------- geometry
// The NES game works on a 256x240 playfield. Mode 6 gives 240x224, so:
//  - x is not shifted: NES columns 0-29 are shown, 30-31 are cut off
//  - y is shifted by Y_OFF: NES rows 1-28 (y 8..231) are shown, i.e. the usual NTSC overscan crop
#define SCR_W     240
#define SCR_COLS  30
#define SCR_ROWS  28
// VRAM row stride = VRAM_TILES_H of the mode 6 kernel. 30 (240px) is the colour renderer, selected by
// kernel_patch/ with -DM6_HIRES=0. The kernel default (M6_HIRES=1) is 36 (288px hires, black/white only): build with -DVRAM_STRIDE=36
// and the ground is extended flat across the columns right of the 240px playfield (the physics' own off-edge model).
#ifndef VRAM_STRIDE
#define VRAM_STRIDE 30
#endif
#define Y_OFF     8

// Terrain ring buffer. Coordinates ("cx") are circular mod 512 exactly as on the NES,
// but only the 256 pixels around the camera are stored (index = cx & 255).
#define FLOOR_N    256
#define FI(cx)     ((cx) & (FLOOR_N - 1))

// Mode 6 ramtile budget. Index 0 is the blank tile, 1 solid, 2 water dither.
#ifndef RAMTILES
#define RAMTILES   128      // measured: base layer <= ~75, overlays <= ~48 (see README)
#endif
// Title logo (108 unique tiles) + menu + terrain needs ~160 ramtiles; below this the title is split in two pages.
#define TITLE_SPLIT (RAMTILES < 168)
#define RT_BLANK   0
#define RT_SOLID   1
#define RT_WATER   2
#define RT_FIRST   3
#ifndef DYN_MAX
#define DYN_MAX    48      // max overlay (sprite/particle) tiles touched per frame
#endif
#define WEATHER_N  32      // rain/snow particles (NES uses 32 sprites)
#define SPR_MAX    20      // sprite list per frame

// Colours are 3:3:2 (RGB332), same layout as the Uzebox video DAC.
// !! If the byte layout on your kernel differs, this is the only place to change. !!
#define RGB332(r,g,b) ((uint8_t)((((b)>>6)<<6) | (((g)>>5)<<3) | ((r)>>5)))

// NES pad bit layout, kept as in the original source.
#define PAD_A      0x80
#define PAD_B      0x40
#define PAD_SELECT 0x20
#define PAD_START  0x10
#define PAD_UP     0x08
#define PAD_DOWN   0x04
#define PAD_LEFT   0x02
#define PAD_RIGHT  0x01

// game constants (from dgolf.c)
#define GLOBAL_FLOOR 222
#define OCEAN_FLOOR  224


// RGB332 channel access (keep in sync with RGB332 above)
#define C_R(c) ((c) & 7)
#define C_G(c) (((c) >> 3) & 7)
#define C_B(c) (((c) >> 6) & 3)
#define C_MAKE(r,g,b) ((uint8)(((b) << 6) | ((g) << 3) | (r)))

// Vector shapes carried in the sprite list (tile >= 0x80). (x,y) is the NES 8x8 cell origin, attr = parameter.
// They are drawn as horizontal/vertical line runs by render.c instead of from bitmaps.
#define T_VPOLE 0x80   // flagpole (2px wide, 19px tall); (x,y) = top of the pole
#define T_VTIP  0x81   // pennant tip (7 scanlines)
#define T_VTOP  0x82   // flag top edge
#define T_VDISC 0x83   // highlighted ball: filled disc, radius 3
#define T_VRING 0x84   // ring, radius 3; attr 0 = full ring, 1..4 = rotating half-arc
#define T_VGLOW 0x85   // glow marker; attr 0..3 = pulse shape

// ---------------------------------------------------------------- platform interface
void     plat_init(void);
void     plat_vsync(void);                 // wait for vertical blank (60 Hz)
uint8    plat_pad(void);                   // PAD_* bits
uint16   plat_entropy(void);               // anything that varies between power-ups
void     plat_colors(uint8 bg, uint8 fg, uint8 fg_band, uint8 band_y0, uint8 band_y1);
void     plat_note(uint8 channel, uint8 patch, uint8 note, uint8 vol);   // TriggerNote(); vol 0 = note off
void     plat_soft_reset(void);
extern uint8 *plat_vram;                   // SCR_COLS*SCR_ROWS tile indices
extern uint8 *plat_tiles;                  // RAMTILES * 8 bytes, MSB = leftmost pixel

// ---------------------------------------------------------------- shared game state
extern uint8  floor_y[FLOOR_N];
extern uint8  floor_a[FLOOR_N];
extern uint16 scroll_cx;
extern uint8  hole, ocean_attribute, players, player, strokes[5*4];
extern uint8  status_on, status_w, hole_digits[3];
extern uint8  frame_count;

// weather particles (NES: OAM entries 32..63). y >= 240 = inactive; spd = pixels/frame
extern uint8  wp_y[WEATHER_N], wp_x[WEATHER_N], wp_spd[WEATHER_N];

// sprite list, filled by the game each hole_draw(), consumed by the renderer
extern uint8  spr_n;
extern uint8  spr_tile[SPR_MAX], spr_x[SPR_MAX], spr_y[SPR_MAX], spr_attr[SPR_MAX];

// ---------------------------------------------------------------- renderer (render.c)
enum { SCENE_GAME = 0, SCENE_TITLE = 1, SCENE_HELP = 2 };
extern uint8 scene_mode;
extern uint8 scene_logo;                   // draw the title logo
extern uint8 scene_flat;                   // draw a flat ground strip instead of the terrain (costs no ramtiles)
extern uint8 scene_dirty;                  // terrain/status/text changed: rebuild the base layer
extern uint8 col_bg, col_fg, col_text;     // current colours
extern uint8 band_y0, band_y1;             // screen scanlines drawn in the text colour
extern const char *scene_text_a;           // title/help text, NES tile coordinates
extern uint8 scene_text_col, scene_text_row;

void render_init(void);
void render_frame(void);

// statistics, used by the host tests and handy on hardware with a debugger
extern uint8 rt_peak_base, rt_peak_total, rt_base_overflow, rt_dyn_overflow;
extern uint8 rt_last_total;                // base + overlay ramtiles used in the last frame
extern uint8 rt_base_top;                  // ramtiles used by the base layer in the last rebuild

// ---------------------------------------------------------------- game (game.c)
void game_boot(void);                      // title, then the endless hole loop
extern uint8 dbg_state;
extern uint8 render_enabled;
void hole_play(void);
void frame(void);

// sfx (sfx.c)
void sfx_init(void);
void sfx_play(uint8 id);
void sfx_tick(void);

#endif
