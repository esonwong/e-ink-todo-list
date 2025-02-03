/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __FONTS_H
#define __FONTS_H

/* Max size of bitmap will based on a font24 (17x24) */
#define MAX_HEIGHT_FONT 41
#define MAX_WIDTH_FONT 32
#define OFFSET_BITMAP 54

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <Arduino.h>

// ASCII
typedef struct _tFont
{
    const uint8_t *table;
    uint16_t Width;
    uint16_t Height;

} sFONT;

extern const uint8_t Font24_Table[] PROGMEM;
extern const uint8_t Font20_Table[] PROGMEM; // Add Font20 table declaration
extern const uint8_t Font12_Table[] PROGMEM;

extern sFONT Font24;
extern sFONT Font20; // Add Font20 declaration
extern sFONT Font12;

#endif /* __FONTS_H */
