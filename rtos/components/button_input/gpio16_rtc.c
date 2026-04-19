/* gpio16_rtc.c — verbatim port of the Arduino tree's lib/gpio16 (G11).
 *
 * GPIO16 is an RTC-domain pad with no internal pull-up and no IRQ; the legacy
 * firmware drove it as an output set HIGH and read the level back via
 * RTC_GPIO_IN_DATA (the button shorts the pad to GND). This file reproduces
 * the exact register pokes; WRITE_PERI_REG / READ_PERI_REG and the RTC_GPIO_*
 * register addresses come from v3.4's esp8266/eagle_soc.h.
 *
 * Source: einktodo-device/lib/gpio16/gpio16.cpp. Changes vs the original:
 * ICACHE_FLASH_ATTR dropped (RTOS code runs from irom by default), Arduino
 * uint8/uint32 typedefs -> stdint. Register expressions are byte-identical.
 *
 * Compiled unconditionally so it can never bit-rot; the call sites in
 * button_input.c select it behind CONFIG_EINKTODO_GPIO16_RAWREG (Residual 4:
 * one Kconfig flip away if the driver/gpio readback misbehaves at bring-up).
 */
#include <stdint.h>

#include "esp8266/eagle_soc.h"

#include "gpio16_rtc.h"

void gpio16_output_conf(void)
{
    WRITE_PERI_REG(PAD_XPD_DCDC_CONF,
                   (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc) | (uint32_t)0x1); // mux configuration for XPD_DCDC to output rtc_gpio0

    WRITE_PERI_REG(RTC_GPIO_CONF,
                   (READ_PERI_REG(RTC_GPIO_CONF) & (uint32_t)0xfffffffe) | (uint32_t)0x0); // mux configuration for out enable

    WRITE_PERI_REG(RTC_GPIO_ENABLE,
                   (READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32_t)0xfffffffe) | (uint32_t)0x1); // out enable
}

void gpio16_output_set(uint8_t value)
{
    WRITE_PERI_REG(RTC_GPIO_OUT,
                   (READ_PERI_REG(RTC_GPIO_OUT) & (uint32_t)0xfffffffe) | (uint32_t)(value & 1));
}

void gpio16_input_conf(void)
{
    WRITE_PERI_REG(PAD_XPD_DCDC_CONF,
                   (READ_PERI_REG(PAD_XPD_DCDC_CONF) & 0xffffffbc) | (uint32_t)0x1); // mux configuration for XPD_DCDC and rtc_gpio0 connection

    WRITE_PERI_REG(RTC_GPIO_CONF,
                   (READ_PERI_REG(RTC_GPIO_CONF) & (uint32_t)0xfffffffe) | (uint32_t)0x0); // mux configuration for out enable

    WRITE_PERI_REG(RTC_GPIO_ENABLE,
                   READ_PERI_REG(RTC_GPIO_ENABLE) & (uint32_t)0xfffffffe); // out disable
}

uint8_t gpio16_input_get(void)
{
    return (uint8_t)(READ_PERI_REG(RTC_GPIO_IN_DATA) & 1);
}
