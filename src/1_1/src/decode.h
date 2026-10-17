// Bit extraction for the 212kbps Manchester-sampled receive stream.
// Pure functions without hardware access, so they can be tested on a PC.
#pragma once
#include <stdint.h>

#ifdef __AVR__
#include <avr/pgmspace.h>
#else
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#endif

// Collects bits 7, 5, 3 and 1 of the index into the low 4 bits of the entry.
static const uint8_t odd_bits[256] PROGMEM = {
#define B(n) ((((n) >> 7 & 1) << 3) | (((n) >> 5 & 1) << 2) | (((n) >> 3 & 1) << 1) | ((n) >> 1 & 1))
#define R4(n) B(n), B(n + 1), B(n + 2), B(n + 3)
#define R16(n) R4(n), R4(n + 4), R4(n + 8), R4(n + 12)
#define R64(n) R16(n), R16(n + 16), R16(n + 32), R16(n + 48)
    R64(0), R64(64), R64(128), R64(192)
#undef R64
#undef R16
#undef R4
#undef B
};

// Extract one byte from 3 received bytes.
// The data bits are every second bit of the 24 bit window, starting at bit
// (23 - shift). The shift is a template parameter so that the shifts below
// are compile time constants.
template <int shift>
static inline uint8_t extract_byte_s(uint8_t data1, uint8_t data2, uint8_t data3)
{
    uint16_t w = ((uint16_t)data1 << 8) | data2;
    w = (uint16_t)(w << shift) | (uint8_t)(data3 >> (8 - shift));
    return (pgm_read_byte(&odd_bits[w >> 8]) << 4) | pgm_read_byte(&odd_bits[w & 0xFF]);
}

template <>
inline uint8_t extract_byte_s<0>(uint8_t data1, uint8_t data2, uint8_t)
{
    uint16_t w = ((uint16_t)data1 << 8) | data2;
    return (pgm_read_byte(&odd_bits[w >> 8]) << 4) | pgm_read_byte(&odd_bits[w & 0xFF]);
}
