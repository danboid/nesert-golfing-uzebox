// Uzebox patch table, built from the NES sound scripts (see tools/gen_data.py).
// Index = SFX_* id for the 12 effects, then PATCH_RAIN. Included by plat_uzebox.c only.
#include <avr/pgmspace.h>
#include "data_sfx_cmd.h"

const struct PatchStruct patches[] PROGMEM = {
	SFX_PATCH_ROWS
	PATCH_RAIN_ROW
};
