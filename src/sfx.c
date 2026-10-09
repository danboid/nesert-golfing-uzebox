//
// sfx.c - sound effects.
//
// The NES effects were APU register scripts. tools/gen_data.py converts each one into a native Uzebox patch
// (data_sfx_cmd.h); here an effect is started once with plat_note() (= TriggerNote) and the kernel's sound engine
// runs the pitch/volume steps. The NES "weather overlay" (quiet rain noise whenever no effect uses the noise
// voice) is reproduced as a separate ambient on the noise channel.
//
#include "nesert.h"
#include "data_sfx_meta.h"

uint8 sfx_weather_rate(void);
uint8 sfx_weather_tile(void);

#define CH_PULSE 0          // wave channel used for pulse effects (the music player is not used)
#define CH_NOISE 3
#define RAIN_PATCH SFX_COUNT        // patch index of the rain noise patch (see data_sfx_cmd.h)

static uint8 noise_busy;           // frames left of a noise effect
static uint8 rain_on;
static uint8 sound_s0, sound_s1;   // 16-bit LFSR for rain, as in the NES sound code

static uint8 sound_prng(void)
{
	uint8 c0 = sound_s0 >> 7, c1 = sound_s1 >> 7;
	uint8 a = (uint8)(sound_s0 << 1);
	sound_s1 = (uint8)((sound_s1 << 1) | c0);
	if (c1) a ^= 0xD7;
	sound_s0 = a;
	return a;
}

void sfx_init(void)
{
	noise_busy = 0; rain_on = 0;
	sound_s0 = sound_s1 = 1;
}

void sfx_play(uint8 id)
{
	if (RD8(&sfx_voice[id])) {
		// noise channel: for TriggerNote on this channel the "note" argument is the patch index
		plat_note(CH_NOISE, id, id, RD8(&sfx_peak[id]));
		noise_busy = RD8(&sfx_len[id]);
		rain_on = 0;
	} else {
		plat_note(CH_PULSE, id, RD8(&sfx_note0[id]), RD8(&sfx_peak[id]));
	}
}

void sfx_tick(void)
{
	if (noise_busy) { --noise_busy; return; }               // an effect owns the noise voice
	uint8 audible = 0;
	if (sfx_weather_rate() && sfx_weather_tile() == 0x38) { // rain only (snow is silent, as on the NES)
		uint8 a = (uint8)((sound_prng() & 63) + 4);
		audible = (a >= sfx_weather_rate());
	}
	if (audible != rain_on) {
		rain_on = audible;
		plat_note(CH_NOISE, RAIN_PATCH, RAIN_PATCH, audible ? 17 : 0);   // volume 0 = note off
	}
}
