/* gpio16_rtc.h — private header for the verbatim lib/gpio16 register-poke
 * port (G11). Used by button_input.c only when CONFIG_EINKTODO_GPIO16_RAWREG
 * is set; the implementation is compiled unconditionally so the fallback is
 * buildable from day one (Residual 4).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void gpio16_output_conf(void);
void gpio16_output_set(uint8_t value);
void gpio16_input_conf(void);
uint8_t gpio16_input_get(void);

#ifdef __cplusplus
}
#endif
