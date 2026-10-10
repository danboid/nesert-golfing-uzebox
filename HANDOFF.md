## Corrections made along the way (so they are not repeated)

* `bgcolors[]` is defined by the application (VectorDemo does); it is not a kernel omission.
* The kernel's own `SetPixel2`/`Line2` OR into the tile a VRAM cell already points at (no copy-on-write); the port draws its own line runs.
* Mode 6 as shipped = 288 px black/white hires with per-line colour through DDRC; `Set*Color` do nothing in that path.
* The overlay-overflow rate depends on the holes played: 0.55 % over the 120-hole test, 1.1 % over 40 holes. Compare like with like.
* `packrom` does not cut the author at `;`: the `.uze` author field is 32 characters.
* The original NES game supports the SNES mouse; the earlier port had dropped it. Its code (`new_poll`, title menu, swing loop in
  `dgolf.c`) is the reference for the mouse behaviour.
* Kernel `SetMouseSensitivity()` loops forever if the mouse never reports the requested sensitivity (`retries++`): don't call it.
* `EnableSnesMouse()` maps a sprite and is not needed to read a mouse: the VSYNC handler reads the extra 16 bits whenever a mouse
  signature is present.
* Uzem only emulates a mouse in port 1 (from the host mouse); cuzebox-ex scripts either port through its API, so the Uzem patch
  for port 2 was dropped.
* `make` does not rebuild kernel objects when `KERNEL_OPTIONS` change unless they depend on the Makefile (they now do): a build
  once kept `CONTROLLERS_VSYNC_READ=1` from a stale object.
* The Hyperkin reports sensitivity as 0 always and ignores software sensitivity cycling (SNESdev wiki).
