
/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __FONTS_H
#define __FONTS_H

/* Max size of bitmap will based on a font24 (17x24) */
#define MAX_HEIGHT_FONT 41
#define MAX_WIDTH_FONT 32
#define OFFSET_BITMAP 54

#ifdef __cplusplus
extern "C"
{
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
    // #include <avr/pgmspace.h>
    // ASCII
    typedef struct _tFont
    {
        const uint8_t *table;
        uint16_t Width;
        uint16_t Height;

    } sFONT;

    extern sFONT Font24;
    extern sFONT Font20;
    extern sFONT Font12;

#ifdef __cplusplus
}
#endif

#endif /* __FONTS_H */
