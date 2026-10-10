#!/bin/sh
# Type-checks src/plat_uzebox.c against a real Uzebox kernel checkout (no avr-gcc needed; uses stub AVR headers).
# usage: tools/typecheck_kernel.sh /path/to/uzebox/kernel   (apply kernel_patch/ first: M6_HIRES=0 gives the 30 column mode 6)
set -e
K="$1"; [ -d "$K" ] || { echo "usage: $0 <kernel dir>"; exit 1; }
HERE="$(cd "$(dirname "$0")/.." && pwd)"
S="$(mktemp -d)"; mkdir -p "$S/avr"
cat > "$S/avr/io.h" <<EOF2
#pragma once
#include <stdint.h>
extern volatile uint16_t TCNT1;
#define PORTA0 0
#define PORTA1 1
#define PORTA2 2
#define PORTA3 3
#define PORTA4 4
#define PORTA5 5
#define PORTA6 6
#define PORTA7 7
#define PORTB0 0
#define PORTB1 1
#define PORTB2 2
#define PORTB3 3
#define PORTB4 4
#define PORTB5 5
#define PORTB6 6
#define PORTB7 7
#define _BV(b) (1 << (b))
#define PA0 0
#define PA1 1
#define PA2 2
#define PA3 3
extern volatile uint8_t PORTA, PORTB, PORTC, PORTD, DDRA, DDRB, DDRC, DDRD, PINA, PINB, PINC, PIND;
#define _SFR_IO_ADDR(x) 0
#define _SFR_MEM_ADDR(x) 0
EOF2
printf '#pragma once\n#include <stdint.h>\n#define PROGMEM\n#define pgm_read_byte(p) (*(const uint8_t*)(p))\n#define pgm_read_word(p) (*(const uint16_t*)(p))\n#define pgm_read_ptr(p) (*(void* const*)(p))\n' > "$S/avr/pgmspace.h"
printf '#pragma once\n' > "$S/avr/interrupt.h"
mkdir -p "$S/util"
printf '#pragma once\nstatic inline void _delay_us(double us) { (void)us; }\n' > "$S/util/delay.h"
printf '#pragma once\n#define ATOMIC_RESTORESTATE 0\n#define ATOMIC_BLOCK(t) for (int atomic_once_ = 1; atomic_once_; atomic_once_ = 0)\n' > "$S/util/atomic.h"
gcc -fsyntax-only -std=gnu99 -Wall -Wextra -Wno-unused-parameter -DVIDEO_MODE=6 -DM6_HIRES=0 -DRAM_TILES_COUNT=128 -DSOUND_MIXER=1 -DINTRO_LOGO=0 -DCONTROLLERS_VSYNC_READ=0 -DSNES_MOUSE=0 -I"$S" -I"$K" -I"$HERE/src" "$HERE/src/plat_uzebox.c" && echo "plat_uzebox.c type-checks against $K"
