// Host test driver: runs the real game code headless with a scripted player.
//  - the "player" finds shots by trial: it fork()s the whole game, tries a candidate swing with
//    rendering off, and reports how close the ball got to the hole; the best shot is then played
//    for real in the parent.
//  - collects ramtile statistics and saves screenshots (PPM) at interesting moments.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "../src/game.c"        // statics are needed by the driver

extern uint8 (*host_pad_fn)(void);
extern void (*host_frame_fn)(void);
extern unsigned long host_frames;
extern void host_dump_ppm(const char *path, int scale);

extern unsigned long sound_events;
static unsigned long max_frames = 20000;
static int target_holes = 6;
static const char *outdir = "test_out";
static int shots_taken, holes_done, snap_n;
static int is_child, child_wr;
static sint16 want_x, want_y;      // desired swing (pull) for the current stroke
static uint8 start_pressed;
static unsigned snaps_wanted;

// -------- stats
static unsigned long ovf_frames, ovf_missing, sum_base, n_base, hist[33];
static unsigned peak_base_all, peak_total_all, overflow_frames, rebuilds_seen;
static unsigned long frames_over_budget;

static void snap(const char *tag)
{
	char p[256];
	snprintf(p, sizeof p, "%s/%02d_%s.ppm", outdir, snap_n++, tag);
	host_dump_ppm(p, 2);
	printf("  snapshot %s  (frame %lu, hole %u)\n", p, host_frames, hole);
}

// -------- input script
static uint8 child_done;
static unsigned long child_start;
static uint32 child_dist = 0xFFFFFF;
static int child_hit;

static uint8 pad_script(void)
{
	uint8 pad = 0;
	if (hole == 0 && players == 0) return 0;
	if (dbg_state == 1) {                      // waiting: pick or reuse a shot, press A
		pad = PAD_A;
	} else if (dbg_state == 2) {               // swinging: pull toward (want_x,want_y)
		pad = PAD_A;
		if (swing_x > want_x) pad |= PAD_LEFT;
		else if (swing_x < want_x - 31) pad |= PAD_RIGHT;
		if (swing_y < want_y) pad |= PAD_DOWN;
		else if (swing_y > want_y + 31) pad |= PAD_UP;
		if (!(pad & 0x0F)) pad = 0;            // reached: release
	}
	return pad;
}

static uint8 pad_title(void)
{
	if (getenv("HELP_TEST")) {
		unsigned long f = host_frames;
		if (f == 40 || f == 80 || f == 140) return PAD_START;
		if (f == 60 || f == 64) return PAD_DOWN;
		return 0;
	}
	if ((host_frames > 40 && host_frames < 44) || (host_frames > 80 && host_frames < 84)) return PAD_START;
	return 0;
}

static uint8 pad_dispatch(void)
{
	if (scene_mode != SCENE_GAME) return pad_title();
	return pad_script();
}

// -------- trial child: ends when the stroke finishes
static unsigned long child_frames;
static int child_prev_state;
static void child_frame_hook(void)
{
	++child_frames;
	if (child_prev_state == 3 && dbg_state != 3) {
		uint32 bx = balls_x[player] >> 8;
		int32_t d = (int32_t)((int32_t)hole_sx - (int32_t)(ball_x / 256));
		(void)bx;
		struct { int hit; int32_t d; } r;
		r.d = d;
		r.hit = (d >= -4 && d <= 2);          // same window as the hole test in hole_play
		if (write(child_wr, &r, sizeof r) < 0) {}
		_exit(0);
	}
	child_prev_state = dbg_state;
	if (child_frames > 4000) { struct { int hit; int32_t d; } r = {0, 99999}; if (write(child_wr, &r, sizeof r) < 0) {} _exit(0); }
}

typedef struct { int hit; int32_t d; } trial_t;

static trial_t try_shot(sint16 wx, sint16 wy)
{
	int fd[2];
	trial_t r = {0, 99999};
	if (pipe(fd)) return r;
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {                         // child: play the shot headless
		close(fd[0]); child_wr = fd[1]; is_child = 1;
		render_enabled = 0;
		want_x = wx; want_y = wy;
		child_prev_state = 0; child_frames = 0;
		host_frame_fn = child_frame_hook;
		return r;                           // fall back into the game loop in the child (see caller)
	}
	close(fd[1]);
	if (read(fd[0], &r, sizeof r) != sizeof r) { r.hit = 0; r.d = 99999; }
	close(fd[0]);
	waitpid(pid, 0, 0);
	return r;
}

// -------- called by the game loop each frame (parent)
static int wait_state_prev;
static unsigned long hole_start_frame;
static int last_hole_seen;

static void choose_shot(void)
{
	trial_t best = {0, 99999};
	sint16 bx = -1200, by = 1200;
	int tries = 0;
	// random search then local refinement
	for (int n = 0; n < 160; ++n) {
		sint16 wx = -(64 + (rand() % 90) * 32 / 1), wy = 64 + (rand() % 90) * 32 / 1;
		if (rand() & 1) wy = 32 + (rand() % 40) * 32;
		wx = (sint16)(wx / 32 * 32); wy = (sint16)(wy / 32 * 32);
		if (wx < -SWING_MAX) wx = -SWING_MAX;
		if (wy > SWING_MAX) wy = SWING_MAX;
		trial_t r = try_shot(wx, wy);
		if (is_child) return;
		++tries;
		if (abs(r.d) < abs(best.d) || (r.hit && !best.hit)) { best = r; bx = wx; by = wy; }
		if (best.hit) break;
	}
	for (int n = 0; n < 60 && !best.hit; ++n) {
		sint16 wx = bx + (rand() % 5 - 2) * 32, wy = by + (rand() % 5 - 2) * 32;
		if (wx > -64) wx = -64;
		if (wy < 32) wy = 32;
		trial_t r = try_shot(wx, wy);
		if (is_child) return;
		if (abs(r.d) < abs(best.d) || (r.hit && !best.hit)) { best = r; bx = wx; by = wy; }
	}
	want_x = bx; want_y = by;
	printf("  hole %u shot %d: swing (%d,%d) hole_sx=%u -> %s, miss by %d px (%d trials)\n",
	       hole, shots_taken, bx, by, hole_sx, best.hit ? "HIT" : "no", best.d, tries);
}

static void parent_frame_hook(void)
{
	// ramtile statistics (frame boundary: state of the last render)
	{
		static uint8 last_ovf;
		if (!is_child && rt_dyn_overflow != last_ovf) {
			++ovf_frames; ovf_missing += (uint8)(rt_dyn_overflow - last_ovf);
			int act = 0; for (int q = 0; q < WEATHER_N; ++q) act += wp_y[q] < 240;
			if (getenv("VERBOSE_OVF")) printf("  ovf@%lu: base_top=%u total=%u sprites=%u particles=%d hole=%u\n", host_frames, rt_base_top, rt_last_total, spr_n, act, hole);
			last_ovf = rt_dyn_overflow;
		}
	}
	if (scene_mode == SCENE_GAME && !is_child && host_frames > 120) {
		if (rt_base_top > peak_base_all) peak_base_all = rt_base_top;
		if (rt_last_total > peak_total_all) peak_total_all = rt_last_total;
		sum_base += rt_base_top; ++n_base; ++hist[rt_base_top >> 3];
		if (rt_base_top > 100 && getenv("VERBOSE_BASE")) printf("  big base %u at frame %lu hole %u scroll %u\n", rt_base_top, host_frames, hole, scroll_cx);
	}
	if (host_frames >= max_frames) {
		printf("frame budget reached\n");
		goto done;
	}
	if (hole != last_hole_seen && scene_mode == SCENE_GAME) {
		last_hole_seen = hole;
		++holes_done;
		hole_start_frame = host_frames;
		if (holes_done >= target_holes) goto done;
	}
	// choose a shot as soon as the game starts waiting for input
	if (dbg_state == 1 && wait_state_prev != 1 && scene_mode == SCENE_GAME) {
		choose_shot();
		++shots_taken;
		if (is_child) { wait_state_prev = 1; return; }
	}
	wait_state_prev = dbg_state;

	// screenshots
	if (scene_mode == SCENE_TITLE && host_frames == 30) { snap("title_splash"); printf("  splash page base tiles: %u (overflow so far %u)\n", rt_base_top, rt_base_overflow); }
	if (getenv("HELP_TEST") && scene_mode == SCENE_HELP && host_frames == 110) { snap("help"); }
	if (scene_mode == SCENE_TITLE && host_frames == 70) { snap("title_menu"); printf("  menu page base tiles: %u (overflow so far %u)\n", rt_base_top, rt_base_overflow); }
	if (scene_mode == SCENE_GAME) {
		if (hole == 0 && splash_s == 6 && !(snaps_wanted & 32)) { snaps_wanted |= 32; snap("ocean_splash"); }
		if (hole == 0 && dbg_state == 1 && !(snaps_wanted & 64) && host_frames > hole_start_frame + 200) { snaps_wanted |= 64; snap("ocean_aim"); }
		if (dbg_state == 2 && swing_x < -600 && !(snaps_wanted & 2)) { snaps_wanted |= 2; snap("swing"); }
		if (dbg_state == 3 && (frame_count & 31) == 0 && !(snaps_wanted & 4) && ball_vy < 0) { snaps_wanted |= 4; snap("flight"); }
		if (holes_done == 3 && dbg_state == 1 && !(snaps_wanted & 8) && host_frames > hole_start_frame + 300) { snaps_wanted |= 8; snap("hole3_aim"); }
		if (holes_done == 5 && dbg_state == 1 && !(snaps_wanted & 16) && host_frames > hole_start_frame + 300) { snaps_wanted |= 16; snap("hole5_aim"); }
	}
	return;
done:
	printf("\n==== results ====\n");
	printf("frames run:            %lu\n", host_frames);
	printf("holes reached:         %d (target %d)\n", holes_done, target_holes);
	printf("strokes played:        %d\n", shots_taken);
	printf("game-scene base ramtiles: peak %u, mean %.1f (budget %d of %d, rest reserved for overlays)\n", peak_base_all, n_base ? (double)sum_base / n_base : 0.0, RAMTILES - DYN_MAX, RAMTILES);
	printf("game-scene total ramtiles peak: %u\n", peak_total_all);
	printf("base overflow events:  %u   overlay overflow events: %u\n", rt_base_overflow, rt_dyn_overflow);
	for (int q = 0; q < 33; ++q) if (hist[q]) printf("  base tiles %3d-%3d: %5.1f%% of frames\n", q*8, q*8+7, 100.0*hist[q]/n_base);
	printf("frames with overlay overflow: %lu (%.2f%%)\n", ovf_frames, host_frames ? 100.0*ovf_frames/host_frames : 0.0);
	printf("sound events:          %lu\n", sound_events);
	snap("final");
	exit(0);
}

int main(int argc, char **argv)
{
	for (int a = 1; a < argc; ++a) {
		if (!strncmp(argv[a], "--holes=", 8)) target_holes = atoi(argv[a] + 8);
		else if (!strncmp(argv[a], "--frames=", 9)) max_frames = strtoul(argv[a] + 9, 0, 10);
		else if (!strncmp(argv[a], "--out=", 6)) outdir = argv[a] + 6;
		else if (!strncmp(argv[a], "--seed=", 7)) seed = strtoul(argv[a] + 7, 0, 0) & 0xFFFFFF;
	}
	srand(1);
	plat_init();
	host_pad_fn = pad_dispatch;
	host_frame_fn = parent_frame_hook;
	game_boot();
	for (;;) hole_play();
}
