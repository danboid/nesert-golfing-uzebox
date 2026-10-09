# NESert Golfing for Uzebox

This is port of **NESert Golfing** by Brad Smith (2019–2022), itself derived from *Desert Golfing* by
Justin Smith (2014), to the Uzebox using a modified **video mode 6** (monochrome 1bpp RAM-tile bitmap).

This port was magicked here under the supervision of Dan MacDonald, Based on the source code downloaded from: <https://github.com/bbbradsmith/NESertGolfing> (CC BY 4.0).

Building this game requires patching video mode 6's kernel source as described in the **Building** section below.

## Status

| Part | State |
|---|---|
| `src/game.c` (game logic) | Ported from `dgolf.c`/`dgolf.s`. **Built and played on the host** (120 holes, 132 strokes, bot-driven). |
| `src/render.c` (mode 6 renderer) | **Built and tested** against a simulated mode 6 screen; screenshots in `docs/`. |
| `src/sfx.c` + generated patches | The 12 NES effects are converted at generation time into native Uzebox patches (pitch/volume/noise steps from the original scripts) and started with `TriggerNote`. Host: runs, no audio. **Not heard on hardware yet.** |
| `default/Makefile` + all sources | **Built successfully with `avr-gcc` (by the user)** with the `M6_HIRES=0` kernel patch: no errors, one warning (since fixed). Not yet run in Uzem or on hardware. |
| `src/plat_uzebox.c` | Type-checked against the real kernel headers, and **assembled/linked by the user's `avr-gcc` build**. Not yet run in Uzem or on hardware. |
| `kernel_patch/` | Opt-in `M6_HIRES` switch for the kernel's mode 6 (default = unchanged upstream behaviour; `-DM6_HIRES=0` = the 240px colour renderer). Verified to apply to the provided files and (by preprocessing) to leave the default output identical to upstream; **not assembled or run**. |
| `src/patches.inc` | The Uzebox patch table, built from the generated NES-derived patches (`data_sfx_cmd.h`). |

This was developed without `avr-gcc` (host simulation + header type-checks); the user then built it successfully with the real
toolchain. **What has not happened yet is running it**, in Uzem or on hardware, so on-screen behaviour, timing and sound are unverified.
`default/Makefile` follows the kernel demos' layout.

![host simulation](docs/host_simulation_screens.png)

## How it works

The NES draws a scrolling heightmap by streaming tiles into CHR-RAM. Mode 6 has no scrolling and no
sprites, so the port is a small software renderer over mode 6's tile map:

* **Terrain**: the 256-entry ring buffer `floor_y[]` (+ `floor_a[]`, the slope index used by physics) is rasterised
  straight into ramtiles. Per 8px column only the tile rows the surface passes through get a unique
  ramtile (bit-sliced build, ~16 ops per tile); rows above are blank (index 0), rows below share one
  solid tile; flat runs reuse the left neighbour's tile. Rebuilt only when something static changes.
* **Sprites / weather**: drawn copy-on-write into extra ramtiles on top of the base layer and undone next frame from a
  small undo log. XOR for balls/rings (visible on both sky and ground), OR for the rest.
* **Vector shapes**: the flagpole, pennant tip and top edge, the swing rings (full ring and rotating half-arc), the
  highlighted ball (filled disc) and the glow markers are drawn as horizontal/vertical **line runs**
  (`vector_draw()` in `render.c`, shape codes `T_V*` in `nesert.h`), not from bitmaps. These use this port's own line
  primitives rather than the kernel's mode 6 `SetPixel`/line routines, because the kernel keeps its own ramtile
  bookkeeping that would conflict with the base/overlay layering. The balls (rolling highlight) and flag numerals
  remain 1bpp bitmaps.
* **Colour**: a mono mode, so sky = background, ground = foreground, and a per-scanline foreground
  override for the band holding the status/menu text. Day/night, fades and weather palettes are all
  preserved as colour changes. Player identity is by ball *shape* (disc / ring / ring+dot / ring+cross).
* **Game code** mirrors the NES source function by function (same names, same fixed-point maths,
  same PRNG), so the two can be read side by side. `fmult` is `(a*b)>>8`, identical to the asm.

`tools/gen_data.py` regenerates all tables/graphics from the NES sources (slopes use the original algorithm;
font/logo/sprites come from the original PNGs).

## Measured (host simulation, 120 holes, real physics)

| | |
|---|---|
| Base layer ramtiles, in game | peak 63, mean 32 |
| Base + overlay peak | 85 |
| Title logo page | 119 (logo has 108 unique tiles) |
| Frames dropping a few raindrop pixels | 0.6 % at `DYN_MAX=48`; 0 % at 64 |

**RAM, measured on a real `avr-gcc` build** (`.data` + `.bss`, with `-DM6_HIRES=0 -DRAM_TILES_COUNT=128 -DSOUND_MIXER=1`):
3539 B of 4096 (`.data` 418 + `.bss` 3121), leaving 557 B for the stack; flash 28,378 B. 406 B of that `.data` were this port's three
text strings (a plain `const char[]` is copied into RAM on AVR); they are now `PROGMEM`, which should bring the total to about
3133 B (about 960 B of stack room). *That second figure is arithmetic from the first measurement, not a new build.*
The parts: `RAMTILES`×8 = 1024 B, VRAM 840 B, the game (~1.15 KB: floor 512, particles 96, sprite list 80, undo log 144, other globals),
and the kernel with the inline mixer. **Build with
`-DSOUND_MIXER=1` (inline):** the default VSYNC mixer needs a 524 B buffer and does its mixing during vblank, which is also
when this port draws. Optionally `-DSOUND_CHANNEL_5_ENABLE=0` drops the unused PCM channel (about 23 B; not tried, the demos keep it on). Levers if
short: `WEATHER_N`, `DYN_MAX`, `SPR_MAX`, `RAMTILES` (the title splash needs ~120).

## Deliberate deviations from the NES game

* **Playfield 240×224** instead of 256×240: columns 30–31 and the top/bottom 8 lines are cropped (the NTSC overscan).
  Hole placement uses 32 columns as the "2 tiles off-screen" minimum instead of 34.
* **Two-page title**: logo page, then menu page, because logo + menu + terrain needs ~160 ramtiles.
  Builds with `RAMTILES >= 168` get the original single page (`TITLE_SPLIT`).
* **Help page** is text over a flat strip (no scroll-in animation, no second terrain).
* **Not ported**: Tournament Edition, NES mouse support (so the mouse text was removed from help).
* **Ocean** is a dithered water strip (mono can't colour it blue). Splash/ball shading is simplified to 1bpp.
* The 512-pixel terrain ring is stored as 256 pixels (coordinates stay mod 512). The hole "lip" is only
  applied when the hole is on screen (off-screen terrain isn't stored).

## Upstream quirk, preserved

`hole_next()` in the NES source sets **both** `fg_hold_mask` and `fg_angle_mask` from `course_hm[]`;
`course_am[]` is never used. That is how the shipped ROM generates terrain, so it is reproduced.
Define `USE_COURSE_AM` to use the table that looks like it was intended (host tests pass either way).

## Building

Host tests (gcc + Python/Pillow): `./run_tests.sh` — plays 120 holes (and the forced ocean hole) with a bot that
`fork()`s the game, tries swings headless, and plays the best. Screenshots land in `test_out/` as PPM.

Uzebox: copy this folder to `uzebox/demos/NESert/` (so `../../../kernel` resolves), apply `kernel_patch/` to the kernel once
(see `kernel_patch/README`; restore the original `videoMode6` files first if you applied an earlier version), then `cd default && make`. `tools/typecheck_kernel.sh <kernel dir>` re-checks `plat_uzebox.c`
against your kernel headers without a toolchain.
`tools/typecheck_kernel.sh <kernel dir>` to re-check `plat_uzebox.c` against your kernel checkout.

## Licence

NESert Golfing (game logic, graphics, sound scripts): **CC BY 4.0**, © Brad Smith. This port is a derivative
work and keeps that attribution. The Uzebox kernel is GPLv3; a built ROM links it, so distributing a ROM
carries the GPLv3 obligations for the combined work.

## Mode 6 kernel: what the provided source says, and the patch

Read from `videoMode6.s`, `videoMode6.h`, `videoMode6.def.h`:

* **Two renderers are in the file.** The one currently called (`render_tile_line_hires`) draws **288 px (36 columns), black
  and white only**: it outputs `0x00`/`0xFF` and never uses `bg_color`/`fg_color`, so `SetBackgroundColor()` /
  `SetForegroundColor()` do nothing visible. Its only colour hook, `mycallback`, writes a per-line foreground into `DDRC`
  from a `bgcolors[]` array that the **application must define** (224 B of RAM; the kernel's `VectorDemo` defines it and shows "288X224 RESOLUTION"), with the background fixed black. That is the kernel's intended hires design: one colour per scanline, on black.
  The original renderer (`render_tile_line`) is still there: **240 px (30 columns), real foreground and background
  colours**, 6 cycles/pixel. Both take 1440 cycles per line, so the swap is timing-neutral.
* **`kernel_patch/`** adds an opt-in `M6_HIRES` build option. The default (`1`) leaves the kernel exactly as it was (VectorDemo and other
  mode 6 programs are unaffected). `-DM6_HIRES=0` calls the 240 px renderer already in the file and sets 30 columns (168 B less RAM).
  `mycallback` is kept; a weak reference to `bgcolors` lets programs that don't define it link. This port builds with `-DM6_HIRES=0`.
  Unpatched/`M6_HIRES=1` builds of this port (`-DVRAM_STRIDE=36`) are black/white only.
  (An earlier version of this patch hard-switched the kernel and disabled `mycallback`, which broke linking VectorDemo
  (`undefined reference to 'mycallback'`). That was a bad design; restore the original files before applying this one.)
* Tile format: `ramTiles` is `RAM_TILES_COUNT`×8 bytes, one byte per line, bit 7 = leftmost pixel, set bit = foreground,
  `vram` index 0 = blank (`ramTiles[0..7]` must stay zero). This matches what `render.c` writes.
* The kernel's own drawing: `SetPixel2` ORs into the tile its VRAM cell already points at and only allocates
  (from `nextFreeRamTile`, starting at 1) when the cell is 0, so it cannot copy-on-write over shared base tiles; `ClearBuffer`
  starts 6 rows into `vram` and writes 36×28 bytes (6 rows past the end). The port uses neither, and draws its own
  line runs. `videoMode6.h` declares `SetRamTileBaseDrawingIndex`, but the `.s` exports `SetUserRamTileIndex` instead.
* The hsync hook (`SetHsyncCallback`) is called **every line** and must take exactly 32 cycles including the `ret`, so it is
  assembly only. A per-line text colour therefore isn't implemented: text uses the ground colour. (Possible later as a
  small cycle-counted asm routine writing `fg_color`.)
* Kernel options: `RAM_TILES_COUNT` must equal `RAMTILES` in `src/nesert.h` (checked by `#error` in `plat_uzebox.c`).

## Sound (checked against the kernel's `uzeboxSoundEngine.c`)

* Patches are `{type, pcmData, cmdStream, loopStart, loopEnd}` with `(delta frames, command, param)` command streams
  (`PC_WAVE`, `PC_PITCH` = MIDI note, `PC_ENV_VOL`, `PC_NOISE_PARAMS`, `PC_NOTE_CUT`). `tools/gen_data.py` simulates each NES APU
  script frame by frame and emits an equivalent patch: pulse effects use wave 4 (50% square) or wave 3 (25%) on channel 0,
  noise effects use channel 3. NES noise periods are mapped onto the kernel's LFSR divider (it shifts every `divider+1`
  mixer samples at 15.7 kHz, so NES periods faster than that are clamped).
* Effects are started once with `TriggerNote`; they are not retriggered per frame, because `TriggerNote` resets the wave to
  sine until the first patch command runs (one frame later). Consequence: an effect can start with up to ~16 ms of sine at
  the right pitch before the square wave takes over. Not verified by ear.
* On the noise channel `TriggerNote` takes the *note* argument as the **patch index**, so the port passes the patch id twice.
* The NES's quiet rain ambience is reproduced on the noise channel only while no noise effect is playing.
* Pulse duty 12.5% / 75% are approximated by the 25% wave. Triangle/DMC are unused by the game.

## Build options (verified in `kernel/defines.h`)

`-DVIDEO_MODE=6 -DINTRO_LOGO=0 -DRAM_TILES_COUNT=128 -DSOUND_MIXER=1 -DM6_HIRES=0` (VectorDemo's options, plus this port's tile count and the 240 px renderer).
The joypad is the SNES layout by default (`JOYSTICK=0`): the port uses `BTN_A` as A and `BTN_B` as B.
