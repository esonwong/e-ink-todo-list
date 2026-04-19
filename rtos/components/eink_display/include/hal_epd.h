/* hal_epd.h — the exact Arduino-call shim the e-ink drivers use (R12).
 *
 * The legacy lib/display-driver drivers call a narrow Arduino surface:
 * pinMode / digitalWrite / digitalRead / delay, and the SPI sequence
 * begin/transaction/transfer/endTransaction. This shim maps that surface onto
 * ESP8266_RTOS_SDK (driver/gpio + driver/spi), keeping the byte-level command
 * framing byte-identical: every command/data byte is still bracketed by the
 * driver's own manual CS toggles (epd_write(CS,...)), so hardware-CS is left
 * DISABLED in the SPI peripheral and CS is a plain GPIO — exactly the legacy
 * framing. A GPIO bit-bang fallback sits behind CONFIG_EINKTODO_EPD_BITBANG.
 *
 * Pins (7.5" HSPI fixed pinout): SCK=14, MOSI=13, CS=15, RST=2, DC=4, BUSY=5.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* EPD control pins — same assignment as the legacy display_750_driver.cpp. */
#define EPD_SCK_PIN  14
#define EPD_MOSI_PIN 13
#define EPD_CS_PIN   15
#define EPD_RST_PIN  2
#define EPD_DC_PIN   4
#define EPD_BUSY_PIN 5 /* 0: busy, 1: idle */

/* Configure RST/DC as outputs and BUSY as input. CS is configured by
 * epd_spi_init (it is a GPIO output, manually toggled by the driver). Mirrors
 * the pinMode() calls in DisplayDriver750::initialize(). */
void epd_pin_init(void);

/* digitalWrite(pin, level). */
void epd_write(uint8_t pin, uint8_t level);

/* digitalRead(pin). */
int epd_read(uint8_t pin);

/* delay(ms): vTaskDelay for >=10 ms (idle runs, feeds WDT — R13); ets_delay_us
 * for sub-tick waits so the short driver delays (1/2 ms) stay accurate. */
void epd_delay_ms(uint32_t ms);

/* SPI.begin()+setDataMode(SPI_MODE0)+setBitOrder(MSBFIRST)+setClockDivider:
 * HSPI master, MODE0, MSB-first, hardware CS DISABLED (driver toggles GPIO15). */
void epd_spi_init(void);

/* SPI byte stream out (DC/CS framing is the caller's responsibility, identical
 * to the legacy SPI.transfer loop). Chunked to <=64 B per spi_trans (v3.4
 * SPI limit). */
void epd_spi_send(const uint8_t *buf, size_t n);

/* SPI.end(). */
void epd_spi_end(void);

#ifdef __cplusplus
}
#endif
