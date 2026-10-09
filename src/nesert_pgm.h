// Flash (PROGMEM) access abstraction so the same code builds for AVR and for the host test harness.
#ifndef NESERT_PGM_H
#define NESERT_PGM_H
#include <stdint.h>

#ifdef __AVR__
  #include <avr/pgmspace.h>
  #define RD8(p)   pgm_read_byte(p)
  #define RD16(p)  ((int16_t)pgm_read_word(p))
  #define RDPTR(p) ((const uint8_t *)pgm_read_ptr(p))
#else
  #define PROGMEM
  #define RD8(p)   (*(const uint8_t *)(p))
  #define RD16(p)  (*(const int16_t *)(p))
  #define RDPTR(p) (*(const uint8_t * const *)(p))
#endif

#endif
