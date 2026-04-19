/* led — dead-code parity with legacy src/led.h (inventory item 12).
 *
 * UNWIRED by design: nothing in the legacy firmware calls
 * initLED/turnOnLed/turnOffLed/blinkLed either. Kept API-compatible so the
 * 1:1 module map stays complete (architecture §5 item 12).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void led_init(void);
void led_on(void);
void led_off(void);
void led_blink(uint32_t interval_ms);

#ifdef __cplusplus
}
#endif
