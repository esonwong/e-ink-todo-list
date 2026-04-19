/* led.c — dead-code parity with legacy src/led.h (item 12, unwired).
 *
 * Legacy: LED_PIN = 2 (config.h), pinMode OUTPUT, digitalWrite HIGH/LOW,
 * blinkLed toggles when millis() - lastBlinkTime >= interval. Timebase here
 * is esp_timer_get_time()/1000 (int64, no 49-day wrap — R11).
 */
#include "led.h"

#include <stdbool.h>

#include "driver/gpio.h"
#include "esp_timer.h"

#define LED_PIN GPIO_NUM_2 /* legacy config.h: int LED_PIN = 2 */

static int64_t s_last_blink_ms;
static bool s_level;

void led_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << LED_PIN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

void led_on(void)
{
    s_level = true;
    gpio_set_level(LED_PIN, 1);
}

void led_off(void)
{
    s_level = false;
    gpio_set_level(LED_PIN, 0);
}

void led_blink(uint32_t interval_ms)
{
    int64_t now_ms = esp_timer_get_time() / 1000;

    if (now_ms - s_last_blink_ms >= (int64_t)interval_ms) {
        s_level = !s_level; /* legacy: digitalWrite(LED_PIN, !digitalRead(LED_PIN)) */
        gpio_set_level(LED_PIN, s_level ? 1 : 0);
        s_last_blink_ms = now_ms;
    }
}
