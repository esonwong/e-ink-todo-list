/* hal_epd.c — Arduino-call shim implementation (R12).
 *
 * Maps the narrow Arduino surface used by the e-ink drivers onto
 * ESP8266_RTOS_SDK driver/gpio + driver/spi. The byte-level SPI framing is kept
 * identical to the legacy code: the SPI peripheral runs with hardware CS
 * DISABLED, and CS (GPIO15) is a plain output toggled by the driver around each
 * command/data burst (epd_write(EPD_CS_PIN, ...)). This preserves the legacy
 * "CS low for the whole multi-byte page" framing that a per-transaction
 * hardware CS would otherwise split.
 *
 * Bit-bang fallback (CONFIG_EINKTODO_EPD_BITBANG): software SCK/MOSI toggling at
 * MODE0/MSB-first, zero SPI-driver dependency (Residual 3).
 */
#include "hal_epd.h"

#include "sdkconfig.h"
#include "esp_log.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifdef CONFIG_EINKTODO_EPD_BITBANG
#include "esp8266/eagle_soc.h"
#else
#include "driver/spi.h"
#endif

#include "rom/ets_sys.h" /* ets_delay_us */

static const char *TAG = "hal_epd";

void epd_pin_init(void)
{
    /* Mirror DisplayDriver750::initialize(): BUSY input, RST/DC outputs.
     * CS is configured in epd_spi_init (output, manual toggle). */
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << EPD_RST_PIN) | (1ULL << EPD_DC_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out);

    gpio_config_t in = {
        .pin_bit_mask = (1ULL << EPD_BUSY_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&in);
}

void epd_write(uint8_t pin, uint8_t level)
{
    gpio_set_level((gpio_num_t)pin, level ? 1 : 0);
}

int epd_read(uint8_t pin)
{
    return gpio_get_level((gpio_num_t)pin);
}

void epd_delay_ms(uint32_t ms)
{
    /* >=10 ms blocks via the scheduler (idle runs, feeds the HW WDT). Sub-tick
     * waits busy-spin via ets_delay_us so the driver's delay(1)/delay(2) keep
     * their effective timing (R11/R13). */
    if (ms >= 10) {
        vTaskDelay(pdMS_TO_TICKS(ms));
    } else if (ms > 0) {
        ets_delay_us(ms * 1000);
    }
}

#ifdef CONFIG_EINKTODO_EPD_BITBANG

void epd_spi_init(void)
{
    gpio_config_t out = {
        .pin_bit_mask = (1ULL << EPD_SCK_PIN) | (1ULL << EPD_MOSI_PIN) | (1ULL << EPD_CS_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&out);
    gpio_set_level(EPD_CS_PIN, 1);
    gpio_set_level(EPD_SCK_PIN, 0); /* MODE0: clock idles low */
    ESP_LOGI(TAG, "EPD SPI: bit-bang mode");
}

void epd_spi_send(const uint8_t *buf, size_t n)
{
    /* MODE0, MSB first: data sampled on rising edge. */
    for (size_t i = 0; i < n; i++) {
        uint8_t b = buf[i];
        for (int bit = 7; bit >= 0; bit--) {
            gpio_set_level(EPD_MOSI_PIN, (b >> bit) & 1);
            gpio_set_level(EPD_SCK_PIN, 1);
            gpio_set_level(EPD_SCK_PIN, 0);
        }
    }
}

void epd_spi_end(void) {}

#else /* hardware HSPI */

void epd_spi_init(void)
{
    /* CS is a manual GPIO output (driver toggles it). */
    gpio_config_t cs = {
        .pin_bit_mask = (1ULL << EPD_CS_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cs);
    gpio_set_level(EPD_CS_PIN, 1);

    spi_config_t cfg = {0};
    cfg.interface.val = SPI_DEFAULT_INTERFACE; /* MSB first, MODE0 base */
    cfg.interface.cs_en = 0;                   /* manual CS framing (legacy parity) */
    cfg.interface.miso_en = 0;                 /* write-only bus */
    cfg.interface.cpol = SPI_CPOL_LOW;         /* MODE0 */
    cfg.interface.cpha = SPI_CPHA_LOW;
    cfg.interface.bit_tx_order = SPI_BIT_ORDER_MSB_FIRST;
    cfg.interface.byte_tx_order = SPI_BYTE_ORDER_MSB_FIRST;
    cfg.intr_enable.val = SPI_MASTER_DEFAULT_INTR_ENABLE;
    cfg.mode = SPI_MASTER_MODE;
    cfg.clk_div = SPI_10MHz_DIV; /* legacy SPI_CLOCK_DIV4 ~= 20 MHz; arch starts at 10 MHz, bring-up may raise to SPI_20MHz_DIV */
    cfg.event_cb = NULL;
    esp_err_t err = spi_init(HSPI_HOST, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_init failed: %d", err);
    }
}

void epd_spi_send(const uint8_t *buf, size_t n)
{
    /* <=64 B per spi_trans (v3.4 SPI limit; bits.mosi is a bit count). The mosi
     * buffer must be uint32_t-addressed, so copy into an aligned scratch. */
    uint32_t word[16]; /* 64 bytes */
    size_t off = 0;
    while (off < n) {
        size_t chunk = n - off;
        if (chunk > sizeof(word)) {
            chunk = sizeof(word);
        }
        for (size_t i = 0; i < chunk; i++) {
            ((uint8_t *)word)[i] = buf[off + i];
        }
        spi_trans_t trans = {0};
        trans.bits.mosi = chunk * 8;
        trans.mosi = word;
        spi_trans(HSPI_HOST, &trans);
        off += chunk;
    }
}

void epd_spi_end(void)
{
    spi_deinit(HSPI_HOST);
}

#endif /* CONFIG_EINKTODO_EPD_BITBANG */
