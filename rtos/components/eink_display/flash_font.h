/* flash_font.h — irom placement + aligned flash accessor for the font tables.
 *
 * On the ESP8266 the default Arduino core keeps `const` rodata in DRAM; here we
 * force the ~15 KB of font bitmaps into ICACHE-mapped flash (.irom0.text) to
 * keep them out of the 80 KB heap (item 63, R12). Mandatory companion: byte
 * loads from ICACHE-mapped flash fault on this chip, so `pgm_read_byte` is
 * reimplemented as an aligned 32-bit read + byte extract (the same trick the
 * Arduino core uses). Verify in the .map that the tables landed in .irom0.text
 * and DRAM rodata dropped ~15 KB.
 */
#pragma once

#include <stdint.h>

/* The font .cpp files declare their tables `const uint8_t X[] PROGMEM`. Map
 * PROGMEM onto the flash text section + 4-byte alignment so every aligned
 * 32-bit read below is well-defined. */
#ifdef PROGMEM
#undef PROGMEM
#endif
#define PROGMEM __attribute__((section(".irom0.text"), aligned(4)))

/* Aligned-word read of a single byte from flash-mapped rodata. Reading the
 * containing 32-bit word (address masked to a 4-byte boundary) and shifting out
 * the wanted byte avoids the unaligned-byte-load fault on ICACHE flash. */
static inline uint8_t einktodo_pgm_read_byte(const void *addr)
{
    uint32_t a = (uint32_t)addr;
    uint32_t w = *(const uint32_t *)(a & ~(uint32_t)3);
    return (uint8_t)(w >> ((a & 3) * 8));
}

#ifdef pgm_read_byte
#undef pgm_read_byte
#endif
#define pgm_read_byte(addr) einktodo_pgm_read_byte((const void *)(addr))
