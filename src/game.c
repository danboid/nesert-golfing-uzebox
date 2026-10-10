//
// game.c - port of dgolf.c from NESert Golfing (Brad Smith, CC BY 4.0).
//
// The structure and variable names follow the NES source on purpose, so the two can be read
// side by side. Hardware specific parts (PPU, nametables, OAM, APU) were replaced by:
//   sprite_add()   -> sprite list consumed by render.c
//   ppu_*          -> scene_* flags in render.c
//   sound_play()   -> sfx_play()
//   palette[]      -> col_bg / col_fg / col_text (monochrome mode: sky / ground / text)
// Not ported in this version: Tournament Edition, the help screen slide-in.
// The SNES mouse is supported as in the NES game (plat_mouse(); on the Uzebox it may be in either port).
//
#include "nesert.h"
#include "data_slopes.h"
#include "data_pal.h"
#include "data_sfx_ids.h"

#ifndef LAST_HOLE_TEST
#define LAST_HOLE_TEST 0     // set to e.g. 3 to reach the ocean hole quickly
#endif
#define te 0

// ---------------------------------------------------------------- state
uint8  floor_y[FLOOR_N];
uint8  floor_a[FLOOR_N];
uint16 scroll_cx;
uint8  hole, ocean_attribute, players, player, strokes[5*4];
uint8  status_on, status_w, hole_digits[3];
uint8  frame_count;
uint8  wp_y[WEATHER_N], wp_x[WEATHER_N], wp_spd[WEATHER_N];
uint8  spr_n;
uint8  spr_tile[SPR_MAX], spr_x[SPR_MAX], spr_y[SPR_MAX], spr_attr[SPR_MAX];

static uint32 seed = 0x00654321UL;
static uint8  i, j, k, l;
static uint16 mx, nx, ox, px;
static sint16 ux;
static uint16 tx, ty;
static sint16 tsx, tsy;
static uint8  gamepad, gamepad_last, gamepad_new;
static uint8  mouse1, mouse_last, mouse_new, mouse_steady;   // mouse1 = MOUSE_* buttons
static sint8  mouse2, mouse3, mouse_sx, mouse_sy;            // mouse2 = y motion, mouse3 = x motion (NES names)

static uint32 ball_x, ball_y;
static sint32 ball_vx, ball_vy;
static uint32 balls_x[4];
static uint8  balls_y[4];
static sint16 norm_x, norm_y;

static uint8  weather_tile, weather_attribute;
static sint8  weather_wind_dir;
static uint8  weather_wind_p, weather_rate_min, weather_rate_mask;
static uint8  weather_wait, weather_next;

static uint16 tee_cx, hole_cx, tee_sx, hole_sx, status_sx;
static uint8  tee_sy, hole_sy, tee_s, ball_s, splash_s, balls_draw[4];
static uint8  flag_remove, swinging;
static sint16 swing_x, swing_y;
static uint8  cleared, ring_glow, roll_sx, roll_sy, soll_sx, soll_sy;
static uint16 timeout;
static uint8  rollout, valley, old_lip[2], lip_valid, first_stroke, next_player;

static uint16 fg_min, fg_max, fg_cx, fg_last, fg_min_soft, fg_max_soft, fg_next;
static uint8  fg_hold_mask, fg_angle_mask, fg_angle, fg_hold;

static uint8  transition, transition_time, day;
static sint8  weather_wind_fade_dir;
static uint8  weather_wind_fade_p;
static uint16 weather_wind_timeout;
static uint8  pal_floor, pal_sky, pal_text, course, field_set, title_menu;

#define BX_LX(p)  ((uint8)(balls_x[p] >> 8))
#define BX_HX(p)  ((uint8)(balls_x[p] >> 16))
#define BX_WX(p)  ((uint16)(balls_x[p] >> 8))
#define BX_SET_HX(p,v) (balls_x[p] = (balls_x[p] & 0xFF00FFFFUL) | ((uint32)(v) << 16))

#define SWING_FIX 16
#define SWING_MIN (SWING_FIX*3)
#define SWING_MAX (SWING_FIX*256)
#define WATER_COLOUR_OCEAN 1

#define SET_F(x) RD8(&set_f[x])
#define SET_S(x) RD8(&set_s[x])
#define SET_T(x) RD8(&set_t[x])
#define SET_W(x) RD8(&set_w[x])

// ---------------------------------------------------------------- common library
uint8 dbg_state;   // 1 = waiting for stroke, 2 = swinging, 3 = ball in flight, 0 = other (test hook)

static uint8 prng1(void)
{
	uint32 s = seed;
	uint8 c = (uint8)((s >> 23) & 1);
	s = (s << 1) & 0x00FFFFFFUL;
	if (c) s ^= 0x1B;
	seed = s;
	return (uint8)s;
}

static uint8 prng(void)
{
	uint8 n;
	for (n = 0; n < 8; ++n) prng1();
	return (uint8)seed;
}

// (a * b) / 256, truncated to 16 bits, rounding toward -inf (matches the asm fmult)
static sint16 fmult(sint16 a, sint16 b)
{
	return (sint16)(((sint32)a * (sint32)b) >> 8);
}

static uint16 read_slope(uint8 idx)  { return (uint16)RD16(&slope_tab[idx]); }
static sint16 read_norm_x(uint8 idx) { return RD16(&norm_x_tab[idx]); }
static sint16 read_norm_y(uint8 idx) { return RD16(&norm_y_tab[idx]); }

static uint8 blend_w(uint8 a, uint8 b, uint8 wa)    // wa/4 of a, rest of b
{
	uint8 wb = 4 - wa;
	uint8 r = (C_R(a) * wa + C_R(b) * wb + 2) / 4;
	uint8 g = (C_G(a) * wa + C_G(b) * wb + 2) / 4;
	uint8 bl = (C_B(a) * wa + C_B(b) * wb + 2) / 4;
	return C_MAKE(r, g, bl);
}
static uint8 blend50(uint8 a, uint8 b) { return blend_w(a, b, 2); }
static uint8 blend25(uint8 a, uint8 b) { return blend_w(a, b, 3); }   // 75% a, 25% b

static void input_poll(void)
{
	gamepad = plat_pad();
	mouse1 = plat_mouse(&mouse3, &mouse2);
	if ((gamepad & (PAD_A | PAD_B | PAD_SELECT | PAD_START)) == (PAD_A | PAD_B | PAD_SELECT | PAD_START))
		plat_soft_reset();
}

static void new_poll(void)
{
	input_poll();
	gamepad_new = gamepad & (gamepad ^ gamepad_last);
	mouse_new = mouse1 & (mouse1 ^ mouse_last);
	gamepad_last = gamepad;
	mouse_last = mouse1;

	// filter out new mouse motions until it's been stationary for a few frames
	#define MOUSE_STEADY_TIME 24
	#define MOUSE_STEADY_MOVE 12
	mouse_sx = 0;
	mouse_sy = 0;
	if (mouse_steady) {
		--mouse_steady;
	} else {
		if (mouse3 >= MOUSE_STEADY_MOVE || mouse3 <= -MOUSE_STEADY_MOVE) {
			mouse_sx = mouse3;
			mouse_steady = MOUSE_STEADY_TIME;
		}
		if (mouse2 >= MOUSE_STEADY_MOVE || mouse2 <= -MOUSE_STEADY_MOVE) {
			mouse_sy = mouse2;
			mouse_steady = MOUSE_STEADY_TIME;
		}
	}
}

static void sound_play(uint8 id) { sfx_play(id); }

// ---------------------------------------------------------------- colours / sprites
static void palette_generate(uint8 sky, uint8 ground, uint8 text)
{
	col_bg = sky; col_fg = ground; col_text = text;
}

static void ball_draw_setup(void)
{
	balls_draw[0] = player;
	for (i = 1; i < 4; ++i) {
		if (i >= players) balls_draw[i] = 4;     // hidden
		else balls_draw[i] = (player + i) % players;
	}
}

static void sprite_begin(void) { spr_n = 0; }
static void sprite_end(void) {}
static void sprite_add(uint8 tile, uint8 x, uint8 y, uint8 attrib)
{
	if (spr_n >= SPR_MAX) return;
	spr_tile[spr_n] = tile; spr_x[spr_n] = x; spr_y[spr_n] = y; spr_attr[spr_n] = attrib;
	++spr_n;
}

// Monochrome: player identity is carried by the ball's shape instead of its colour.
static uint8 ball_tile(uint8 p, uint8 rolling)
{
	return (p == 0) ? (uint8)(0x1D + rolling) : (uint8)(0x2C + (p & 3));
}

// ---------------------------------------------------------------- floor generation
static void floor_build_pixel_calculate_range(void)
{
	fg_min_soft = (fg_min / 4) + (fg_max / 4) + (fg_min / 2);
	fg_max_soft = (fg_min / 4) + (fg_max / 4) + (fg_max / 2);
}

static void floor_build_pixel_angle(void)
{
	fg_hold = (prng() & fg_hold_mask) + 8;
	fg_angle = prng() & fg_angle_mask;
	if      (fg_last < fg_min_soft) fg_angle &= 0x7F;
	else if (fg_last > fg_max_soft) fg_angle |= 0x80;
}

static void floor_build_pixel_next(void)
{
	fg_next = fg_last + read_slope(fg_angle);
}

static const uint8 HOLE_ANGLE[8] PROGMEM = { 4, 4, 0, 0, 0x80+0, 0x80+0, 0x80+4, 0x80+4 };

static void floor_build_pixel(void)
{
	uint16 h;
	if (ocean_attribute != 255) goto build;
	h = fg_cx - hole_cx;
	if (h < 8) {
		if (hole == 0) {
			ocean_attribute = 0;
			fg_last = fg_next = OCEAN_FLOOR * 256U;
			fg_angle = 0;
			goto build;
		}
		fg_hold = 0;
		fg_angle = RD8(&HOLE_ANGLE[(uint8)h]);
		floor_build_pixel_next();
		goto build;
	}
	if (fg_hold) --fg_hold;
	else floor_build_pixel_angle();
	floor_build_pixel_next();
	if (fg_next < fg_min) {
		floor_build_pixel_angle();
		fg_angle &= 0x7F;
		floor_build_pixel_next();
	} else if (fg_next > fg_max) {
		floor_build_pixel_angle();
		fg_angle |= 0x80;
		floor_build_pixel_next();
	}
build:
	floor_y[FI(fg_cx)] = (uint8)(((fg_last / 2) + (fg_next / 2)) / 256);
	floor_a[FI(fg_cx)] = fg_angle;
	fg_last = fg_next;
	fg_cx = (fg_cx + 1) & 511;
	scene_dirty = 1;
}

// ---------------------------------------------------------------- weather
static void weather_shift(uint8 shift)
{
	for (uint8 n = 0; n < WEATHER_N; ++n) wp_x[n] = (uint8)(wp_x[n] + shift);
}

static void weather_animate(void)
{
	uint8 n;
	if (weather_rate_min) {
		if (weather_wait) { --weather_wait; }
		else {
			uint8 idx = weather_next & (WEATHER_N - 1);
			if (wp_y[idx] >= 240) {
				wp_y[idx] = 0;
				wp_spd[idx] = (weather_attribute & 0x1C) >> 2;
				wp_x[idx] = prng();
				weather_wait = (prng() & weather_rate_mask) + weather_rate_min;
			}
			weather_next = (idx + 1) & (WEATHER_N - 1);
		}
	}
	for (n = 0; n < WEATHER_N; ++n) {
		uint8 y = wp_y[n], s, w;
		if (y >= 240) continue;
		s = wp_spd[n];
		y = (uint8)(y + s);
		wp_y[n] = y;
		for (w = s; w; --w)
			if (prng1() < weather_wind_p) wp_x[n] = (uint8)(wp_x[n] + (uint8)weather_wind_dir);
		if (floor_y[FI((uint16)wp_x[n] + scroll_cx)] <= y) wp_y[n] = 240;
	}
}

static void weather_fade_automask(void)
{
	if ((weather_rate_min & (weather_rate_min - 1)) == 0)
		weather_rate_mask = weather_rate_min - 1;
}

static void weather_wind_random(void)
{
	weather_wind_fade_dir = 1 - ((prng1() & 1) * 2);
	weather_wind_fade_p = prng();
	weather_wind_timeout = ((prng() & 15) + 32) * 64;
}

static void weather_fade(void)
{
	if (weather_wind_timeout == 0) weather_wind_random();
	else --weather_wind_timeout;
	if (weather_wind_dir != weather_wind_fade_dir) {
		if (weather_wind_p == 0) weather_wind_dir = weather_wind_fade_dir;
		else --weather_wind_p;
	} else {
		if      (weather_wind_p < weather_wind_fade_p) ++weather_wind_p;
		else if (weather_wind_p > weather_wind_fade_p) --weather_wind_p;
	}
	if (frame_count & 15) return;
	if (SET_W(field_set)) {
		if (weather_rate_min == 0) { weather_rate_min = 64; weather_fade_automask(); }
		else if (weather_rate_min > 4) { --weather_rate_min; weather_fade_automask(); }
	} else if (weather_rate_min != 0) {
		++weather_rate_min;
		weather_fade_automask();
		if (weather_rate_min >= 65) weather_rate_min = 0;
	}
}

static void weather_attribute_set(void)
{
	weather_attribute = (weather_tile == 0x38) ? ((4<<2)|3) : ((1<<2)|2);
}

// sfx.c reads these two
uint8 sfx_weather_rate(void) { return weather_rate_min; }
uint8 sfx_weather_tile(void) { return weather_tile; }

// ---------------------------------------------------------------- hole generation
#define AM0 0x9F
#define AM1 0xBF
#define AM2 0xFF
#define HM0 15
#define HM1 31
#define HM2 63
static const uint8 course_hm[18] PROGMEM = { HM1,HM0,HM1,HM0,HM1,HM1,HM2,HM1,HM1, HM1,HM1,HM2,HM1,HM0,HM2,HM1,HM2,HM1 };
#ifdef USE_COURSE_AM
static const uint8 course_am[18] PROGMEM = { AM0,AM0,AM1,AM1,AM1,AM2,AM1,AM1,AM2, AM1,AM2,AM1,AM2,AM2,AM2,AM1,AM2,AM1 };
#endif

#define TRANSITION_TIME 16
#define DAY_TIME 8

static void hole_next(void)
{
	++hole;
	for (i = 0; i < 3; ++i) {
		++hole_digits[i];
		if (hole_digits[i] < 10) break;
		hole_digits[i] = 0;
	}
#if LAST_HOLE_TEST
	if (hole == LAST_HOLE_TEST) hole = 0;
#endif
	fg_hold_mask = RD8(&course_hm[course]);
#ifdef USE_COURSE_AM
	fg_angle_mask = RD8(&course_am[course]);
#else
	fg_angle_mask = RD8(&course_hm[course]);   // sic: upstream NES source uses course_hm for both (course_am is unused)
#endif
	++course; if (course >= 18) course = 0;

	mx = hole_cx - scroll_cx;
	if (hole_cx < scroll_cx) mx += 512;
	i = (mx / 8) + 7;                // min screen column of next hole
	j = i + 13;                      // max screen column of next hole
	if (!hole) j = i + 1;            // "last hole" should be close
	if (i < SCR_COLS + 2) i = SCR_COLS + 2;   // at least 2 tiles offscreen
	k = j - i;
	if (k == 0 || k >= 16) k = 1;
	l = i + (prng() % k);

	if (!hole) {
		while (((scroll_cx + (l * 8)) & 24) != 0) ++l;
		fg_min = (GLOBAL_FLOOR - 8) * 256U;     // force terrain to go down toward the ocean
		floor_build_pixel_calculate_range();
	}

	tee_cx = hole_cx;
	hole_cx = (scroll_cx + (l * 8)) & 511;
	hole_sx = l * 8;
	if (!hole) hole_sx = 2048;
}

// ---------------------------------------------------------------- common animation
static void transition_animate(void)
{
	uint8 s, f;
	--transition;
	if (transition == ((TRANSITION_TIME*3)/4)) {
		s = blend25(pal_sky,   SET_S(field_set));
		f = blend25(pal_floor, SET_F(field_set));
		palette_generate(s, f, s);
	} else if (transition == ((TRANSITION_TIME*2)/4)) {
		s = blend50(pal_sky,   SET_S(field_set));
		f = blend50(pal_floor, SET_F(field_set));
		palette_generate(s, f, s);
	} else if (transition == ((TRANSITION_TIME*1)/4)) {
		s = blend25(SET_S(field_set), pal_sky);
		f = blend25(SET_F(field_set), pal_floor);
		palette_generate(s, f, s);
	} else if (transition == 0) {
		pal_sky   = SET_S(field_set);
		pal_floor = SET_F(field_set);
		palette_generate(pal_sky, pal_floor, pal_sky);
	}
}

static void flag_animate(void)
{
	uint8 t;
	if (flag_remove == 0) return;
	t = hole_sy - 32;
	if (t > hole_sy) { hole_sy = 255; flag_remove = 0; return; }
	hole_sy = t;
}

void frame(void)
{
	if (transition) transition_animate();
	weather_fade();
	weather_animate();
	flag_animate();
	plat_vsync();
	render_frame();
	sfx_tick();
	++frame_count;
}

static void frame_double(void)
{
	weather_fade();
	weather_animate();
	flag_animate();
	plat_vsync();
	render_frame();
	sfx_tick();
	++frame_count;
}

static void frames(uint8 count) { while (count--) frame(); }

// ---------------------------------------------------------------- menus
static const char help_text[] PROGMEM =
	"       NESert Golfing\n"
	"      Brad Smith, 2019\n"
	"   http://rainwarrior.ca\n"
	"\n"
	"GAMEPAD CONTROL:\n"
	"  Hold A to begin swing,\n"
	"  use directions to aim,\n"
	"  release to stroke.\n"
	"  Hold B for fine control.\n"
	"\n"
	"MOUSE CONTROL:\n"
	"  Hold left button to begin\n"
	"  swing, drag to aim,\n"
	"  release to stroke.\n"
	"  Hold right button for\n"
	"  fine control.\n"
	"\n"
	" NESert Golfing was derived\n"
	"   from the original game\n"
	"       Desert Golfing\n"
	"   by Justin Smith, 2014.";

static const char title_text[] PROGMEM =
	"         Play\n"
	"\n"
	"How many?   1  2  3  4\n"
	"\n"
	"         Help";

static void scene_set(uint8 mode, const char *text, uint8 c, uint8 r, uint8 y0, uint8 y1)
{
	scene_logo = 0; scene_flat = (mode == SCENE_HELP);
	scene_mode = mode; scene_text_a = text; scene_text_col = c; scene_text_row = r;
	band_y0 = y0; band_y1 = y1;
	scene_dirty = 1;
}

static const char splash_text[] PROGMEM = "       Press START";
static const char splash_text_mouse[] PROGMEM = "Press START or click";   // while a mouse is plugged in (cols 8-27, clear of the right edge)

// Menu page (terrain + menu text).
static void title_scene(void) { scene_set(SCENE_TITLE, title_text, 5, 15, 0, 168); }

// Splash page: logo only. With few ramtiles the logo cannot share the screen with the menu text
// and terrain, so it gets a page of its own over a flat ground strip.
static void splash_scene(uint8 mouse)
{
	scene_set(SCENE_TITLE, mouse ? splash_text_mouse : splash_text, 8, 14, 0, 192 - Y_OFF);
	scene_logo = 1; scene_flat = 1;
}

static void help(void)
{
	scene_set(SCENE_HELP, help_text, 2, 3, 0, 192 - Y_OFF);
	sprite_begin(); sprite_end();
	frames(2);
	new_poll();
	for (;;) {
		frame();
		new_poll();
		if (gamepad_new || mouse_new) break;
	}
	title_scene();
}

#define MENU_Y0 (15*8)
#define MENU_Y1 (17*8)
#define MENU_Y2 (19*8)
#define MENU_X0 (12*8)
#define MENU_X1 (19*8)
#define MENU_XP1 ((16-3)*8)

static void title(void)
{
	players = 1;
	frame_count = 0;
	ocean_attribute = 255;
	flag_remove = 0;
	course = 0;
	field_set = prng() & 15;
	pal_floor    = SET_F(field_set);
	pal_sky      = SET_S(field_set);
	pal_text     = SET_T(field_set);
	weather_tile = SET_W(field_set);
	weather_rate_mask = 3;
	weather_rate_min = weather_tile ? 4 : 0;
	weather_wind_random();
	weather_wind_p = weather_wind_fade_p;
	weather_wind_dir = weather_wind_fade_dir;
	weather_attribute_set();
	transition = 0;
	transition_time = (prng() & 7) + 3;
	day = prng() & 7;
	for (i = 0; i < WEATHER_N; ++i) wp_y[i] = 255;
	status_on = 0;
	hole_cx = 512;           // keep hole and tee off the field for the menu
	tee_cx = 512;

	// initial floor: the title page. (The NES also builds the help page's floor; here the help
	// page is text over a flat strip, so only one screen of terrain is needed.)
	fg_cx = 0;
	fg_max = GLOBAL_FLOOR * 256U;
	fg_min = (176+3) * 256U;                 // 3 pixels below the menu
	fg_angle_mask = 0x80 | 0x3F;             // shallow hills
	floor_build_pixel_calculate_range();
	fg_hold_mask = 7;                        // short hills
	fg_last = fg_min + (prng() % (fg_max - fg_min));
	fg_hold = 0;
	for (mx = 0; mx < SCR_W; ++mx) floor_build_pixel();
	fg_min = 48 * 256U;                      // global minimum
	floor_build_pixel_calculate_range();

	palette_generate(pal_sky, pal_floor, pal_text);
	title_menu = 0;
	gamepad_last = gamepad;
	mouse_last = mouse1;
	mouse_steady = 0;
	mouse_sx = 0;
	mouse_sy = 0;
#if TITLE_SPLIT
	{
		// Port addition: START or a left click on the logo page starts a 1-player game straight away (the NES title
		// has "Play" preselected, so START starts there too); A, B, SELECT, DOWN or a right click open the menu page.
		uint8 quick = 0, mouse_shown = plat_mouse_present();
		splash_scene(mouse_shown);
		sprite_begin(); sprite_end();
		for (;;) {                              // page 1: logo
			frame();
			new_poll();
			prng();
			if ((gamepad_new & PAD_START) || (mouse_new & MOUSE_L)) { quick = 1; break; }
			if ((gamepad_new & (PAD_A | PAD_B | PAD_SELECT | PAD_DOWN)) || (mouse_new & MOUSE_R)) break;
			if (plat_mouse_present() != mouse_shown) { mouse_shown ^= 1; splash_scene(mouse_shown); }
		}
		title_scene();
		if (quick) { sprite_begin(); sprite_end(); frame(); goto title_done; }
	}
#else
	title_scene();
	scene_logo = 1;
#endif

	for (;;) {
		sprite_begin();
		if (title_menu != 1) {
			uint8 yy = (title_menu == 0) ? MENU_Y0 : MENU_Y2;
			sprite_add(0x3A, MENU_X0, yy, 0x00);
			sprite_add(0x3A, MENU_X1, yy, 0x40);
		}
		sprite_add(0x3A, MENU_XP1 + (players * 24),      MENU_Y1, 0x01);
		sprite_add(0x3A, MENU_XP1 + 16 + (players * 24), MENU_Y1, 0x41);
		sprite_end();
		frame();
		new_poll();
		if ((mouse_new & MOUSE_L) || (gamepad_new & PAD_START)) {          // START or LMB starts game unless help is selected
			if (title_menu != 2) break; else help();
		} else if (mouse_new & MOUSE_R) {                                   // RMB cycles players
			players = (players & 3) + 1;
		} else if ((title_menu != 1) && (gamepad_new & (PAD_A | PAD_B))) {
			if (title_menu != 2) break; else help();
		} else if ((gamepad_new & (PAD_DOWN | PAD_SELECT)) || mouse_sy > 0) {
			++title_menu; if (title_menu > 2) title_menu = 0;
		} else if ((gamepad_new & PAD_UP) || mouse_sy < 0) {
			--title_menu; if (title_menu > 2) title_menu = 2;
		} else if (title_menu == 1) {
			if ((gamepad_new & (PAD_A | PAD_RIGHT)) || mouse_sx > 0) players = (players & 3) + 1;
			else if ((gamepad_new & PAD_LEFT) || mouse_sx < 0)        players = ((players + 2) & 3) + 1;
		}
		prng();     // build up entropy
	}
#if TITLE_SPLIT
title_done:
#endif
	seed ^= (uint32)plat_entropy() << 4;       // extra hardware entropy at the moment of starting
	if ((seed & 0xFFFFFFUL) == 0) seed = 1;
	seed &= 0xFFFFFFUL;

	// prepare for game: clear sprites, fade out the text
	sprite_begin(); sprite_end();
	i = pal_text;
	col_text = blend25(i, pal_sky); frames(2);
	col_text = blend50(i, pal_sky); frames(2);
	col_text = blend25(pal_sky, i); frames(2);
	col_text = pal_sky;
	scene_set(SCENE_GAME, 0, 0, 0, 8, 16);   // status row = NES y 16..23

	// create first tee (flat ground), just beyond the right edge of the title page
	hole_cx = SCR_W;
	tee_sx = SCR_W;
	tee_sy = fg_last / 256;
	tee_s = 7;
	for (i = 0; i < 8; ++i) {
		floor_y[FI(fg_cx + i)] = tee_sy;
		floor_a[FI(fg_cx + i)] = 0;
	}
	fg_cx += 8;
	scroll_cx = 0;
	hole = 0;
	hole_digits[0] = hole_digits[1] = hole_digits[2] = 0;
	hole_next();                 // hole_cx converts to tee_cx, new hole_cx is created
	scene_dirty = 1;
	frames(2);

	status_sx = 256;
	balls_x[0] = balls_x[1] = balls_x[2] = balls_x[3] = 256 * 256UL;
	balls_y[0] = balls_y[1] = balls_y[2] = balls_y[3] = tee_sy - 6;
	ball_s = 0;
	splash_s = 0;
	player = 0;
	next_player = 0;
	swinging = 0;
	status_w = 0;
	ring_glow = 0;
	lip_valid = 0;
	first_stroke = (players > 1) ? 0 : 1;
	status_w = (SCR_COLS/2) - (((7 * players) - 2) / 2);   // centre the stroke counters
	memset(strokes, 0, sizeof strokes);
	ball_draw_setup();
}

// ---------------------------------------------------------------- hole play
static void hole_shift(void)
{
	--tee_sx;
	--hole_sx;
	for (uint8 p = 0; p < 4; ++p) {
		uint16 w = (uint16)(balls_x[p] >> 8);
		--w;
		balls_x[p] = (balls_x[p] & 0xFF0000FFUL) | ((uint32)w << 8);
	}
}

static void hole_draw_flag(void)
{
	if (hole_sy == 255) return;
	j = (uint8)hole_sx;
	i = j + 2;
	if (i < j) return;
	sprite_add(T_VPOLE, i, hole_sy - 17, 0x02);   // flagpole: two line runs (was three bitmap sprites)
	i += 8;
	k = hole_sy - 16;
	l = hole_sy - 24;
	if (i < j) return;
	if (hole >= 100) {
		sprite_add(0x10 | hole_digits[2], i, k, 0x02);
		sprite_add(T_VTOP, i, l, 0x02);
		i += 8;
		if (i < j) return;
	}
	if (hole >= 10) {
		sprite_add(0x10 | hole_digits[1], i, k, 0x02);
		sprite_add(T_VTOP, i, l, 0x02);
		i += 8;
		if (i < j) return;
	}
	sprite_add(0x10 | hole_digits[0], i, k, 0x02);   // numeral
	sprite_add(T_VTOP, i, l, 0x02);                  // top line
	i += 8;
	if (i < j) return;
	sprite_add(T_VTIP, i, k, 0x02);                  // flag tip
}

static const uint8 RING_OFFX [4] PROGMEM = {    0,    0,    1,    1 };
static const uint8 RING_OFFY [4] PROGMEM = {    0,    1,    1,    0 };

// clobbers i,j,k,l,mx,nx,ox,px
static void hole_draw(void)
{
	static uint8 order;
	ox = (uint16)(balls_x[player] >> 8);
	px = balls_y[player];
	sprite_begin();
	if (swinging) {
		sprite_add(T_VDISC, ox, px, 0x00);      // highlighted ball
		if (swing_x >= SWING_MIN || swing_x <= -SWING_MIN || swing_y >= SWING_MIN || swing_y <= -SWING_MIN) {
			tsx = (swing_x + (SWING_FIX/2)) / SWING_FIX;
			tsy = (swing_y + (SWING_FIX/2)) / SWING_FIX;
			order = ((swing_x + swing_y) / (SWING_FIX / 4)) & 3;
			tx = ox - tsx - RD8(&RING_OFFX[order]);
			ty = px - tsy - RD8(&RING_OFFY[order]);
			if (tx < 256 && ty < 256) sprite_add(T_VRING, (uint8)tx, (uint8)ty, order + 1);   // rotating half-arc
			tx = ox + tsx;
			ty = px + tsy;
			if (tx < 256 && ty < 256) sprite_add(T_VRING, (uint8)tx, (uint8)ty, 0x00);        // full ring
			ring_glow += 13;
			#define SWING_FRAMES (1<<4)
			order = frame_count & (SWING_FRAMES-1);
			mx = (tsx * (sint16)order) / (SWING_FRAMES-1);
			nx = (tsy * (sint16)order) / (SWING_FRAMES-1);
			tx = ox - mx;
			ty = px - nx;
			if (tx < 256 && ty < 256) sprite_add(T_VGLOW, (uint8)tx, (uint8)ty, (ring_glow/32)&3);
			tx += tsx;
			ty += tsy;
			if (tx < 256 && ty < 256) sprite_add(T_VGLOW, (uint8)tx, (uint8)ty, (ring_glow/32)&3);
		}
	} else if (ox < 256) {
		if (splash_s) {
			sprite_add(0x1F + splash_s, ox, px, 0x03);
			if (splash_s < 4)
				sprite_add(ball_tile(player, ball_s/2), ox, px + splash_s - 1, 0x00);
		} else {
			sprite_add(ball_tile(player, ball_s/2), ox, px, 0x00);
		}
	}
	if (tee_sx < 256) sprite_add(0x30 | tee_s, (uint8)tee_sx, tee_sy, 0x02);
	if (status_sx < 256) sprite_add(0x3A, (uint8)status_sx, 16, 0x00);
	for (order = 4; order != 0; --order) {
		i = (frame_count + order) & 3;
		switch (i) {
			case 0:
				if (hole_sx < SCR_W) hole_draw_flag();
				break;
			default:
				j = balls_draw[i];
				k = j * 4;
				if (j < players && !BX_HX(j))
					sprite_add(ball_tile(j, 0), BX_LX(j), balls_y[j], 0x01);
				break;
		}
	}
	sprite_end();
}

static void stroke_add(void)
{
	i = player * 5;
	for (j = 0; j < 5; ++j) {
		++strokes[i];
		if (strokes[i] < 10) return;
		strokes[i] = 0;
		++i;
	}
	i -= 5;
	strokes[i] = strokes[i+1] = strokes[i+2] = strokes[i+3] = strokes[i+4] = 9;
}

static void status_draw(void) { status_on = 1; scene_dirty = 1; }

static void status_player(void)
{
	if (players > 1) status_sx = ((status_w + (player * 7)) - 1) * 8;
}
#define status_player_hide() { status_sx = 256; }

static void hole_splash(void)
{
	uint8 t;
	sound_play(SFX_SPLASH);
	for (t = 0; t < 18; ++t) {
		splash_s = (t/2) + 1;
		balls_y[player] = OCEAN_FLOOR - 6;
		hole_draw();
		frame();
	}
}

static const uint8 COLLIDE_PRIORITY[5*4] PROGMEM = {
	 8, 12, 13, 13,
	 3,  5, 11, 13,
	 0,  1,  6, 13,
	 2,  4,  9, 13,
	 7, 10, 13, 13,
};

static void status_bar_fade_in(void)
{
	pal_text = SET_T(field_set);
	status_draw();
	hole_draw(); frame_double();
	palette_generate(pal_sky, pal_floor, blend25(pal_sky, pal_text)); hole_draw(); frames(2);
	palette_generate(pal_sky, pal_floor, blend50(pal_sky, pal_text)); hole_draw(); frames(2);
	palette_generate(pal_sky, pal_floor, blend25(pal_text, pal_sky)); hole_draw(); frame();
	input_poll(); hole_draw(); frame();           // clear pending input
	palette_generate(pal_sky, pal_floor, pal_text);
}

#define GRAVITY 12
#define WIND 3
#define DRAG 6
#define STICK 128
#define BALL_RADIUS (2*256)
#define EJECT_MAX (BALL_RADIUS*2)
#define TIMEOUT (20*60)
#define ROLLOUT 16
#define PSWING_COARSE 32
#define PSWING_FINE    1
#define MSWING_COARSE  8
#define MSWING_FINE    1
#define OFFSCREEN_X ((uint32)SCR_W * 256UL)

void hole_play(void)
{
	uint16 t = (tee_cx - (6*8)) & 511;     // target scroll position
	while (scroll_cx != t) {
		floor_build_pixel();                // one more pixel of floor, scroll one pixel
		weather_shift(255);
		hole_shift();
		scroll_cx = (scroll_cx + 1) & 511;
		scene_dirty = 1;
		if (hole_sx < SCR_W) hole_sy = floor_y[FI(hole_cx + 4)] - 8;
		hole_draw();
		frame();
	}
	// 3. raise the tee
	if (hole != 1) {
		sound_play(SFX_TEE);
		tee_sy = floor_y[FI(tee_cx + 4)] - 8;
		tee_sx = (tee_cx - scroll_cx) & 511;
		for (t = 0; t < 8; ++t) {
			tee_s = t;
			i = tee_sy + 1 - t;
			for (j = 0; j < 4; ++j)
				if (balls_y[j] > i) balls_y[j] = i;
			hole_draw();
			frames(2);
		}
		for (i = 0; i < 8; ++i) {
			floor_a[FI(tee_cx + i)] = 0;
			floor_y[FI(tee_cx + i)] = tee_sy;
		}
		scene_dirty = 1;
		if (lip_valid) {                    // restore the lip
			floor_a[FI(tee_cx - 1)] = old_lip[0];
			floor_a[FI(tee_cx + 8)] = old_lip[1];
			lip_valid = 0;
		}
	}
	// add a little "lip" for the hole to help balls fall in
	// (only when its terrain is on screen: the ring buffer only stores what the camera sees)
	if (hole != 0 && hole_sx < SCR_W) {
		old_lip[0] = floor_a[FI(hole_cx - 1)];
		old_lip[1] = floor_a[FI(hole_cx + 8)];
		lip_valid = 1;
		floor_a[FI(hole_cx - 1)] = 0x01;
		floor_a[FI(hole_cx + 8)] = 0x81;
	}
	while (transition) { hole_draw(); frame(); }
	// 4. fade in status bar
	if (!first_stroke) status_bar_fade_in();
	// 5. play hole: find leading player (after the one who played first last)
	for (t = 1; t < 4; ++t) {
		j = (next_player + t) & 3;
		if (j >= players) continue;
		for (i = 0; i < 5; ++i) {
			k = strokes[(j*5)+4-i];
			l = strokes[(next_player)*5+4-i];
			if (k > l) break;
			if (k < l) { next_player = j; break; }
		}
	}
	player = next_player;
	next_player = (next_player + 1) % players;
	cleared = 0;
	for (i = 0; i < 4; ++i)
		if (i >= players) cleared |= (1 << i);
	ball_draw_setup();
	if (players > 1) {
		hole_draw(); frames(2);
		sound_play(SFX_PROMPT0 + player);
	}
	while (cleared < 0x10) {
		// if centre of ball is offscreen, put it back on the tee
		if (balls_x[player] >= ((uint32)(SCR_W - 3) * 256UL) || balls_x[player] < (5 * 256UL)) {
			if (players > 1) {
				hole_draw(); frames(2); hole_draw(); frames(2);
				hole_draw(); frames(2); hole_draw(); frames(2);
			}
			sound_play(SFX_RESPAWN);
			balls_x[player] = tee_sx * 256UL;
			balls_y[player] = tee_sy - 6;
		}
	stroke_wait:
		status_player();
		dbg_state = 1;
		do {
			input_poll();
			prng1();
			hole_draw();
			frame();
		} while (!(gamepad & (PAD_B | PAD_A)) && !(mouse1 & (MOUSE_L | MOUSE_R)));
		dbg_state = 2;
		swinging = 1;
		swing_x = 0;
		swing_y = 0;
		do {
			input_poll();
			if (gamepad & PAD_B) {
				if (gamepad & PAD_LEFT ) swing_x -= PSWING_FINE;
				if (gamepad & PAD_RIGHT) swing_x += PSWING_FINE;
				if (gamepad & PAD_UP   ) swing_y -= PSWING_FINE;
				if (gamepad & PAD_DOWN ) swing_y += PSWING_FINE;
			} else {
				if (gamepad & PAD_LEFT ) swing_x -= PSWING_COARSE;
				if (gamepad & PAD_RIGHT) swing_x += PSWING_COARSE;
				if (gamepad & PAD_UP   ) swing_y -= PSWING_COARSE;
				if (gamepad & PAD_DOWN ) swing_y += PSWING_COARSE;
			}
			if (mouse1 & MOUSE_R) {
				swing_x += mouse3 * MSWING_FINE;
				swing_y += mouse2 * MSWING_FINE;
			} else {
				swing_x += mouse3 * MSWING_COARSE;
				swing_y += mouse2 * MSWING_COARSE;
			}
			if (swing_x >  SWING_MAX) swing_x =  SWING_MAX;
			if (swing_x < -SWING_MAX) swing_x = -SWING_MAX;
			if (swing_y >  SWING_MAX) swing_y =  SWING_MAX;
			if (swing_y < -SWING_MAX) swing_y = -SWING_MAX;
			prng1();
			hole_draw();
			frame();
		} while ((gamepad & (PAD_B | PAD_A)) || (mouse1 & (MOUSE_L | MOUSE_R)));
		swinging = 0;
		if (swing_x < SWING_MIN && swing_x > -SWING_MIN && swing_y < SWING_MIN && swing_y > -SWING_MIN)
			goto stroke_wait;
		stroke_add();
		if (!first_stroke) status_draw();
		status_player_hide();
		hole_draw();
		if (!first_stroke) frame_double(); else frame();
		// ball_fly:
		dbg_state = 3;
		sound_play(SFX_STROKE);
		ball_x = balls_x[player];
		ball_y = balls_y[player] * 256UL;
		ball_vx = -swing_x / 2;
		ball_vy = -swing_y / 2;
		timeout = 0;
		rollout = 0;
		do {
			soll_sx = roll_sx;
			soll_sy = roll_sy;
			roll_sx = BX_LX(player);
			roll_sy = balls_y[player];
			ball_x += ball_vx;
			ball_y += ball_vy;
			ball_vy += GRAVITY;
			balls_x[player] = ball_x;
			balls_y[player] = ball_y / 256;
			if (ball_y >= (256*256UL)) {
				BX_SET_HX(player, 1);               // offscreen
				goto collide_skip;
			}
			// final hole water test
			if (hole == 0 && ball_y >= ((OCEAN_FLOOR-6)*256U)) {
				hole_splash();
				cleared |= (1 << player);
				splash_s = 0;
				ball_x = 256*256UL;
				BX_SET_HX(player, 1);
				break;
			}
			// check 5 columns of the ball for any pixel overlap with the field
			mx = (ball_x / 256) + 1;
			l = (ball_y / 256) + 5;
			px = 0;
			k = 13;
			valley = 0;
			for (i = 0; i < (5*4); i += 4) {
				nx = mx;
				if (mx >= 0x8000) nx = 0;
				else if (mx >= SCR_W) nx = SCR_W - 1;
				nx = (nx + scroll_cx) & 511;
				++mx;
				j = floor_y[FI(nx)];
				if (l >= j) {
					j = l - j;
					if (j >= 2) { j = RD8(&COLLIDE_PRIORITY[i]); }
					else        { j = RD8(&COLLIDE_PRIORITY[i + (2 - j)]); }
					if (j < 13) {
						if      (i <  (2*4)) valley |= 1;
						else if (i >= (3*4)) valley |= 2;
					}
					if (j < k) { k = j; px = nx; }
				}
			}
			if (k >= 13) goto collide_skip;
			if (ball_x >= OFFSCREEN_X) break;     // lands offscreen: don't try to collide
			i = floor_a[FI(px)];
			norm_x = read_norm_x(i);
			norm_y = read_norm_y(i);
			mx = (((px - scroll_cx) & 255) * 256) + 128;
			nx = floor_y[FI(px)] * 256U;
			tsx = (uint16)ball_x + 768 - mx;
			tsy = (uint16)ball_y + 896 - nx;
			ux = BALL_RADIUS - (fmult(norm_x, tsx) + fmult(norm_y, tsy));
			if (ux > 0) {
				if (ux > EJECT_MAX) ux = EJECT_MAX;
				tsx = (norm_x * (sint32)ux) / 256;
				tsy = (norm_y * (sint32)ux) / 256;
				ball_x += tsx;
				ball_y += tsy;
			}
			balls_x[player] = ball_x;
			balls_y[player] = ball_y / 256;
			tsy = fmult(norm_x, ball_vx) + fmult(norm_y, ball_vy);
			tsx = fmult(norm_y, ball_vx) - fmult(norm_x, ball_vy);
			if (tsy > -STICK && tsy < STICK) {
				if (tsy < 0) tsx -= tsx / 8;
				if      (tsx >  DRAG) tsx -= DRAG;
				else if (tsx < -DRAG) tsx += DRAG;
				else                  tsx  =    0;
				tsy = 0;
			} else if (tsy < 0) {
				if      (tsy < -400) sound_play(SFX_BOUNCE2);
				else if (tsy < -200) sound_play(SFX_BOUNCE1);
				else                 sound_play(SFX_BOUNCE0);
				tsy /= -2;
				tsx /= 2;
			}
			if (tsx == 0 && tsy == 0) break;      // ball has stopped
			ball_vx = fmult(norm_x, tsy) + fmult(norm_y, tsx);
			ball_vy = fmult(norm_y, tsy) - fmult(norm_x, tsx);
			if (valley >= 3) { if (ball_vy > 0) ball_vy = 0; }
			goto collide_done;
		collide_skip:
			if (weather_rate_min && (prng1() < weather_wind_p))
				ball_vx += weather_wind_dir * WIND;
		collide_done:
			if (BX_LX(player) > roll_sx) {
				ball_s -= 1; if (ball_s >= 6) ball_s = 5;
				if (BX_LX(player) != soll_sx || balls_y[player] != soll_sy) rollout = 0; else ++rollout;
			} else if (BX_LX(player) != roll_sx || balls_y[player] != roll_sy) {
				ball_s += 1; if (ball_s >= 6) ball_s = 0;
				if (BX_LX(player) != soll_sx || balls_y[player] != soll_sy) rollout = 0; else ++rollout;
			} else {
				++rollout;
			}
			++timeout;
			if (timeout >= TIMEOUT || rollout >= ROLLOUT) {
				if ((BX_HX(player) != 0) || (ball_y >= (256*256UL))) {
					balls_x[player] = 256*256UL;
					break;
				}
				t = (floor_y[FI(BX_WX(player) + scroll_cx + 3)] - 6) & 255;
				while (balls_y[player] < t) {
					ball_y += 256;
					++balls_y[player];
					hole_draw();
					frame();
				}
				break;
			}
			hole_draw();
			frame();
		} while (1);
		// ball_landed:
		dbg_state = 0;
		ball_s = 0;
		hole_draw();
		frame();
		hole_draw(); frames(2);
		if ((hole != 0) && ((ball_x/256) > (uint32)(hole_sx - 3)) && ((ball_x/256) < (uint32)(hole_sx + 5)))
			cleared |= (1 << player);
		balls_x[player] = ball_x & 0x00FFFFFFUL;
		balls_y[player] = ball_y / 256;
		for (i = 0; i < 4; ++i) {
			player = (player + 1) & 3;
			if (!(cleared & (1 << player))) break;
		}
		if (i >= 4) break;
		if (players > 1) sound_play(SFX_PROMPT0 + player);
		if (first_stroke) { status_bar_fade_in(); first_stroke = 0; }
		ball_draw_setup();
		hole_draw();
		input_poll();
		frame();
	}
	// end of game
	while (hole == 0) { hole_draw(); frame(); input_poll(); }
	// 6. fade out status bar, remove flag
	sound_play(SFX_FLAG);
	flag_remove = 1;
	if (!first_stroke) {
		palette_generate(pal_sky, pal_floor, blend25(pal_text, pal_sky)); hole_draw(); frame(); hole_draw(); frame();
		palette_generate(pal_sky, pal_floor, blend50(pal_sky, pal_text)); hole_draw(); frame(); hole_draw(); frame();
		palette_generate(pal_sky, pal_floor, blend25(pal_sky, pal_text)); hole_draw(); frame(); hole_draw(); frame();
		palette_generate(pal_sky, pal_floor, pal_sky);
	} else first_stroke = 0;
	while (flag_remove) { hole_draw(); frame(); }
	status_on = 0; scene_dirty = 1;
	// 7. prepare next hole
	hole_next();
	if (day) --day;
	if (day == 0) {
		day = DAY_TIME;
		field_set ^= 8;
		goto field_change;
	} else if (transition_time == 0) {
		field_set = (prng() & 7) | (field_set & 8);
		transition_time = (prng() & 7) + 3;
	field_change:
		transition = TRANSITION_TIME;
		i = SET_W(field_set);
		if (i) { weather_tile = i; weather_attribute_set(); }
	} else {
		--transition_time;
	}
	weather_wind_random();
	if (hole == 0 && weather_wind_fade_dir < 0) weather_wind_fade_dir = 1;
}

void game_boot(void)
{
	if (seed == 0x00800000UL) seed = 0x00654321UL;
	sfx_init();
	render_init();
	title();
}
