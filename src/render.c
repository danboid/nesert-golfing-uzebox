//
// render.c - Mode 6 (1bpp ramtile) renderer
//
// Two layers:
//  * BASE  : terrain heightmap + text, rasterised into VRAM / ramtiles. Rebuilt only when
//            something static changes (scroll step, status text, tee raise, scene change).
//  * OVERLAY: sprites and weather, drawn copy-on-write into extra ramtiles on top of the
//            base, and undone next frame by restoring the touched VRAM cells (undo log).
//
#include "nesert.h"
#include "data_gfx.h"


uint8 scene_mode = SCENE_GAME;
uint8 scene_dirty = 1;
uint8 scene_logo, scene_flat;
uint8 col_bg, col_fg, col_text;
uint8 band_y0, band_y1;
const char *scene_text_a;
uint8 scene_text_col, scene_text_row;

uint8 rt_peak_base, rt_peak_total, rt_base_overflow, rt_dyn_overflow;
uint8 rt_base_top;
uint8 rt_last_total;

static uint8 rt_top;        // next ramtile the base layer will allocate (wraps in the game scene, see base_rebuild)
static uint8 rt_count;      // base ramtiles allocated by the current/last rebuild
static uint8 rt_start;      // first base ramtile of the current/last rebuild
static uint8 rt_ring;       // 1 = game scene: base tiles allocated as a ring in [RT_FIRST, base_limit)
static uint8 base_limit = RAMTILES - DYN_MAX;   // game scene keeps DYN_MAX tiles for overlays; text scenes need few
static uint8 rt_dyn_base;   // first overlay ramtile (overlay tiles are >= this)
static uint8 rt_dyn_top;    // next free ramtile (overlay)
static uint8 colors_dirty = 1;

typedef struct { uint16 pos; uint8 old; } dlog_t;
static dlog_t dlog[DYN_MAX];
static uint8 dlog_n;

#define TILE(i)  (&plat_tiles[(uint16)(i) * 8])
#define VRAM(c,r) (plat_vram[(uint16)(r) * VRAM_STRIDE + (c)])

// ------------------------------------------------------------------ tiles
void render_init(void)
{
	memset(plat_vram, 0, VRAM_STRIDE * SCR_ROWS);
	memset(TILE(RT_BLANK), 0x00, 8);
	memset(TILE(RT_SOLID), 0xFF, 8);
	for (uint8 i = 0; i < 8; i += 2) { TILE(RT_WATER)[i] = 0xAA; TILE(RT_WATER)[i+1] = 0x55; }
	rt_top = rt_dyn_top = rt_dyn_base = rt_start = RT_FIRST;
	rt_count = rt_ring = 0;
	dlog_n = 0;
	scene_dirty = 1;
	colors_dirty = 1;
}

// find an existing identical base tile or add one (used for text/logo, where tiles repeat a lot)
// allocate one base ramtile (0 = none left). In the game scene the base tiles form a ring, so a
// rebuild first uses the tiles the previous rebuild left free and only then reuses the previous
// build's tiles, oldest (= leftmost columns, already rewritten) first.
static uint8 rt_alloc(const uint8 *bits)
{
	if (rt_count >= base_limit - RT_FIRST) { ++rt_base_overflow; return 0; }
	uint8 t = rt_top;
	if (++rt_top >= base_limit) rt_top = RT_FIRST;
	++rt_count;
	memcpy(TILE(t), bits, 8);
	return t;
}

// find an existing identical base tile of this rebuild or add one (used for text/logo, where tiles repeat a lot)
static uint8 rt_intern(const uint8 *bits)
{
	if (!(bits[0] | bits[1] | bits[2] | bits[3] | bits[4] | bits[5] | bits[6] | bits[7])) return RT_BLANK;
	uint8 t = rt_start;
	for (uint8 n = 0; n < rt_count; ++n) {
		if (memcmp(TILE(t), bits, 8) == 0) return t;
		if (++t >= base_limit) t = RT_FIRST;
	}
	t = rt_alloc(bits);
	return t ? t : RT_BLANK;
}

static void put_tile(uint8 col, uint8 nes_row, const uint8 *bits)
{
	if (col >= SCR_COLS || nes_row < 1 || nes_row > SCR_ROWS) return;
	if (VRAM(col, nes_row - 1) != RT_BLANK) return;   // terrain already owns this cell (text is drawn after it)
	VRAM(col, nes_row - 1) = rt_intern(bits);
}

static void put_glyph(uint8 col, uint8 nes_row, uint8 ch)
{
	uint8 b[8];
	for (uint8 i = 0; i < 8; ++i) b[i] = RD8(&font_tab[(uint16)(ch & 127) * 8 + i]);
	put_tile(col, nes_row, b);
}

static void put_logo(uint8 col, uint8 nes_row, uint8 tile)
{
	uint8 b[8];
	for (uint8 i = 0; i < 8; ++i) b[i] = RD8(&logo_tab[(uint16)tile * 8 + i]);
	put_tile(col, nes_row, b);
}

static void put_string(const char *s, uint8 col0, uint8 row0)    // s lives in flash (PROGMEM)
{
	uint8 c = col0, r = row0, ch;
	for (; (ch = RD8(s)) != 0; ++s) {
		if (ch == '\n') { c = col0; ++r; }
		else { if (ch != ' ') put_glyph(c, r, ch); ++c; }
	}
}

// ------------------------------------------------------------------ base layer
static void base_text(void)
{
	if (scene_logo) {
		uint8 t = 16;
		for (uint8 j = 0; j < 7; ++j)
			for (uint8 k = 0; k < 16; ++k) put_logo(8 + k, 4 + j, t++);
		put_logo(8 + 13, 4 + 7, 13);
		put_logo(8 + 14, 4 + 7, 14);
		put_logo(8 + 15, 4 + 7, 15);
	}
	if (scene_text_a) put_string(scene_text_a, scene_text_col, scene_text_row);
	if (scene_mode == SCENE_GAME && status_on) {
		uint8 k = 4;
		for (uint8 p = 0; p < players; ++p) {
			uint8 col = status_w + 7 * p, j;
			for (j = 0; j < 4; ++j) { if (strokes[k]) break; --k; }
			for (; j < 5; ++j) { put_glyph(col++, 2, '0' | strokes[k]); --k; }
			k += 10;
		}
	}
}

// Terrain: for every 8px column, find the tile rows the surface passes through; those get a
// unique ramtile, rows above are blank and rows below are the shared solid tile.
// Bit-sliced row build: for a tile row r, pixel i has its surface at d = h[i]-8r:
//   d <= 0 -> set in all 8 lines, 0 < d < 8 -> set from line d downward, d >= 8 -> clear.
static void base_terrain(void)
{
	uint8 prev_row_tile[SCR_ROWS + 2];   // left neighbour's tile per row, for flat-run reuse
	memset(prev_row_tile, 0, sizeof prev_row_tile);

	for (uint8 col = 0; col < VRAM_STRIDE; ++col) {
		uint8 h[8], hmin = 255, hmax = 0;
		for (uint8 i = 0; i < 8; ++i) {
			uint16 xp = (uint16)col * 8 + i;
			if (xp > SCR_W - 1) xp = SCR_W - 1;          // columns right of the playfield: flat extension of the last column
			uint8 v = floor_y[FI(scroll_cx + xp)];
			h[i] = v;
			if (v < hmin) hmin = v;
			if (v > hmax) hmax = v;
		}
		uint8 first = hmin >> 3, last = hmax >> 3;
		uint8 ocean_col = (hmin >= OCEAN_FLOOR);

		for (uint8 r = 1; r <= SCR_ROWS; ++r) {
			uint8 t;
			if (r < first) { VRAM(col, r - 1) = RT_BLANK; prev_row_tile[r] = 0; continue; }   // sky (written, not pre-cleared)
			if (r > last) {                                // fully below the surface
				t = (ocean_col && r == SCR_ROWS) ? RT_WATER : RT_SOLID;
				VRAM(col, r - 1) = t;
				prev_row_tile[r] = 0;
				continue;
			}
			uint8 eq[8] = {0,0,0,0,0,0,0,0}, base = 0, bits[8];
			for (uint8 i = 0; i < 8; ++i) {
				sint16 d = (sint16)h[i] - (sint16)(r << 3);
				uint8 m = 0x80 >> i;
				if (d <= 0) base |= m;
				else if (d < 8) eq[d] |= m;
			}
			uint8 acc = base, any = 0, all = 0xFF;
			for (uint8 k = 0; k < 8; ++k) { acc |= eq[k]; bits[k] = acc; any |= acc; all &= acc; }
			if (!any) { VRAM(col, r - 1) = RT_BLANK; prev_row_tile[r] = 0; continue; }
			if (all == 0xFF) t = (ocean_col && r == SCR_ROWS) ? RT_WATER : RT_SOLID;
			else {
				uint8 pl = prev_row_tile[r];
				if (pl && memcmp(TILE(pl), bits, 8) == 0) t = pl;    // flat run: share with left neighbour
				else if (!(t = rt_alloc(bits))) t = RT_SOLID;
			}
			VRAM(col, r - 1) = t;
			prev_row_tile[r] = (t >= RT_FIRST) ? t : 0;
		}
	}
}

// No up-front clear of VRAM: the rebuild takes several frames in mode 6 and the display would show
// the cleared, half-built screen (visible as a wipe during the hole scroll). Instead every cell is
// written exactly once with its final value, terrain first, then text on top of the sky cells.
// (Original behaviour preserved: text only shows where the terrain left the cell blank.)
static void base_rebuild(void)
{
	uint8 ring = (scene_mode == SCENE_GAME);
	if (!(ring && rt_ring)) rt_top = RT_FIRST;      // entering/outside the game scene: plain allocation from the start
	rt_ring = ring;
	rt_start = rt_top;
	rt_count = 0;
	dlog_n = 0;
	base_limit = ring ? RAMTILES - DYN_MAX : (scene_logo ? RAMTILES : RAMTILES - 12);
	if (scene_flat) {
		for (uint8 r = 1; r <= SCR_ROWS; ++r)            // plain ground strip under the help text
			for (uint8 c = 0; c < VRAM_STRIDE; ++c) VRAM(c, r - 1) = (r >= 24) ? RT_SOLID : RT_BLANK;
	} else base_terrain();
	base_text();
	rt_dyn_base = rt_ring ? base_limit : RT_FIRST + rt_count;
	rt_dyn_top = rt_dyn_base;
	rt_base_top = RT_FIRST + rt_count;            // same meaning as before: base tiles in use (+ the fixed ones)
	if (rt_base_top > rt_peak_base) rt_peak_base = rt_base_top;
}

// ------------------------------------------------------------------ overlay
static void overlay_rollback(void)
{
	while (dlog_n) { --dlog_n; plat_vram[dlog[dlog_n].pos] = dlog[dlog_n].old; }
	rt_dyn_top = rt_dyn_base;
}

// writable tile for a VRAM cell (copy-on-write), or NULL when out of ramtiles
static uint8 *dyn_tile(uint8 col, uint8 row)
{
	uint16 pos = (uint16)row * VRAM_STRIDE + col;
	uint8 idx = plat_vram[pos];
	if (idx >= rt_dyn_base) return TILE(idx);              // already ours this frame
	if (rt_dyn_top >= RAMTILES || dlog_n >= DYN_MAX) { ++rt_dyn_overflow; return 0; }
	uint8 n = rt_dyn_top++;
	memcpy(TILE(n), TILE(idx), 8);
	dlog[dlog_n].pos = pos; dlog[dlog_n].old = idx; ++dlog_n;
	plat_vram[pos] = n;
	if (rt_base_top + (rt_dyn_top - rt_dyn_base) > rt_peak_total) rt_peak_total = rt_base_top + (rt_dyn_top - rt_dyn_base);
	return TILE(n);
}

enum { OP_OR, OP_XOR };

static void line_bits(sint16 x, sint16 sy, uint8 b, uint8 op)
{
	if (!b || sy < 0 || sy >= SCR_ROWS * 8) return;
	uint8 row = sy >> 3, ln = sy & 7;
	sint16 cx = x >> 3;               // arithmetic shift = floor for negatives
	uint8 sh = x & 7;
	uint8 left = b >> sh, right = sh ? (uint8)(b << (8 - sh)) : 0;
	if (left && cx >= 0 && cx < SCR_COLS) {
		uint8 *t = dyn_tile(cx, row);
		if (t) { if (op == OP_XOR) t[ln] ^= left; else t[ln] |= left; }
	}
	if (right && cx + 1 >= 0 && cx + 1 < SCR_COLS) {
		uint8 *t = dyn_tile(cx + 1, row);
		if (t) { if (op == OP_XOR) t[ln] ^= right; else t[ln] |= right; }
	}
}


// ------------------------------------------------------------------ vector shapes
// Own line primitives on top of the overlay tiles (not the kernel's SetPixel/line routines, which keep their
// own ramtile bookkeeping and would fight the base/overlay layering here).
static void hline(sint16 x0, sint16 x1, sint16 y, uint8 op)
{
	while (x0 <= x1) {
		sint16 n = 8 - (x0 & 7);
		if (n > x1 - x0 + 1) n = x1 - x0 + 1;
		line_bits(x0, y, (uint8)(0xFF00 >> n), op);       // n leading ones, MSB aligned at x0
		x0 += n;
	}
}

static void vline(sint16 x, sint16 y0, sint16 y1, uint8 op)
{
	for (; y0 <= y1; ++y0) line_bits(x, y0, 0x80, op);
}

static const uint8 DISC3_HW[7] PROGMEM = { 1, 2, 3, 3, 3, 2, 1 };          // half widths of a 7x7 disc
static const sint8 RING3[7][4] PROGMEM = {                                    // two spans per row (a1>b1 = none)
	{-1, 1, 1, 0}, {-2, 2, 1, 0}, {-3,-2, 2, 3}, {-3,-2, 2, 3}, {-3,-2, 2, 3}, {-2, 2, 1, 0}, {-1, 1, 1, 0}
};

static void span_clip(sint16 cx, sint16 y, sint8 a, sint8 b, uint8 arc, sint8 dy, uint8 op)
{
	if (a > b) return;
	if (arc == 1 && dy > 0)  return;                       // top half
	if (arc == 2 && b < 0)   return;                       // right half
	if (arc == 3 && dy < 0)  return;                       // bottom half
	if (arc == 4 && a > 0)   return;                       // left half
	if (arc == 2 && a < 0) a = 0;
	if (arc == 4 && b > 0) b = 0;
	hline(cx + a, cx + b, y, op);
}

static void vector_draw(uint8 tile, uint8 x, uint8 y, uint8 attr)
{
	sint16 sx = x, sy = (sint16)y - Y_OFF, cx = sx + 3, cy = sy + 3;
	switch (tile) {
	case T_VPOLE:                                          // 2px wide pole, 19 rows / 16 rows
		vline(sx + 6, sy, sy + 18, OP_OR);
		vline(sx + 7, sy, sy + 15, OP_OR);
		break;
	case T_VTIP: {                                         // pennant: widths 2,4,6,8,6,4,2
		static const uint8 w[7] PROGMEM = { 2, 4, 6, 8, 6, 4, 2 };
		for (uint8 k = 0; k < 7; ++k) hline(sx, sx + RD8(&w[k]) - 1, sy + k, OP_OR);
		break; }
	case T_VTOP:
		hline(sx, sx + 7, sy + 7, OP_OR);
		break;
	case T_VDISC:
		for (sint8 d = -3; d <= 3; ++d) { sint8 hw = RD8(&DISC3_HW[d + 3]); hline(cx - hw, cx + hw, cy + d, OP_XOR); }
		break;
	case T_VRING:
		for (sint8 d = -3; d <= 3; ++d) {
			span_clip(cx, cy + d, (sint8)RD8((const uint8 *)&RING3[d + 3][0]), (sint8)RD8((const uint8 *)&RING3[d + 3][1]), attr, d, OP_XOR);
			span_clip(cx, cy + d, (sint8)RD8((const uint8 *)&RING3[d + 3][2]), (sint8)RD8((const uint8 *)&RING3[d + 3][3]), attr, d, OP_XOR);
		}
		break;
	case T_VGLOW:
		if (attr & 1) { hline(cx - 1, cx + 1, cy, OP_XOR); vline(cx, cy - 1, cy - 1, OP_XOR); vline(cx, cy + 1, cy + 1, OP_XOR); }   // plus
		else          { hline(cx - 1, cx + 1, cy - 1, OP_XOR); hline(cx - 1, cx + 1, cy, OP_XOR); hline(cx - 1, cx + 1, cy + 1, OP_XOR); } // square
		break;
	}
}

static uint8 rev8(uint8 b)
{
	b = (b >> 4) | (b << 4);
	b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
	return ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
}

// NES sprite at (x,y) in NES coordinates; attr bit 6 = flip H, bit 7 = flip V
static void sprite_draw(uint8 tile, uint8 x, uint8 y, uint8 attr)
{
	if (x >= SCR_W) return;
	if (tile >= 0x80) { vector_draw(tile, x, y, attr); return; }
	sint16 sy0 = (sint16)y - Y_OFF;
	uint8 op = (tile >= 0x1D && tile <= 0x1F) || (tile >= 0x29 && tile <= 0x2B) ||
	           (tile >= 0x2D && tile <= 0x2F) || tile == 0x2A || tile >= 0x3D ? OP_XOR : OP_OR;
	for (uint8 k = 0; k < 8; ++k) {
		uint8 b = RD8(&spr_tab[(uint16)(tile & 63) * 8 + ((attr & 0x80) ? 7 - k : k)]);
		if (attr & 0x40) b = rev8(b);
		line_bits(x, sy0 + k, b, op);
	}
}

static void particles_draw(void)
{
	for (uint8 i = 0; i < WEATHER_N; ++i) {
		if (wp_y[i] >= 240 || wp_x[i] >= SCR_W - 1) continue;
		sint16 sy = (sint16)wp_y[i] + 1 - Y_OFF;
		line_bits(wp_x[i], sy,     0xC0, OP_OR);           // 2x2 particle (sprite tiles 0x38/0x39)
		line_bits(wp_x[i], sy + 1, 0xC0, OP_OR);
	}
}

// ------------------------------------------------------------------ frame
static uint8 cur_bg, cur_fg, cur_text, cur_y0, cur_y1;

uint8 render_enabled = 1;

void render_frame(void)
{
	if (!render_enabled) return;
	if (scene_dirty) { base_rebuild(); scene_dirty = 0; }
	else overlay_rollback();

	for (uint8 i = 0; i < spr_n; ++i) sprite_draw(spr_tile[i], spr_x[i], spr_y[i], spr_attr[i]);
	particles_draw();

	if (cur_bg != col_bg || cur_fg != col_fg || cur_text != col_text || cur_y0 != band_y0 || cur_y1 != band_y1 || colors_dirty) {
		cur_bg = col_bg; cur_fg = col_fg; cur_text = col_text; cur_y0 = band_y0; cur_y1 = band_y1;
		colors_dirty = 0;
		plat_colors(col_bg, col_fg, col_text, band_y0, band_y1);
	}
	rt_last_total = rt_base_top + (rt_dyn_top - rt_dyn_base);   // tiles in use (base + overlay)
	if (rt_last_total > rt_peak_total) rt_peak_total = rt_last_total;
}
