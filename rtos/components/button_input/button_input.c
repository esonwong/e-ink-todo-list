/* button_input.c — button_task: 10 ms-poll gesture FSM (items 13-17).
 *
 * The FSM is a line-for-line port of OneButton 2.5.0's debounce() + _fsm()
 * (mathertel/OneButton, the exact library version pinned in platformio.ini),
 * with the legacy src/button.h callbacks inlined as gesture dispatch:
 *
 *   1 click   -> portal active: WIFI_CMD_PORTAL_STOP
 *                else WiFi connected: CMD_FORCE_REFRESH
 *                else: WIFI_CMD_PORTAL_START
 *   2 clicks  -> WiFi connected: CMD_DEVICE_EVENT("double-click")  (raw event,
 *                semantics decided server-side); else: nothing (legacy parity)
 *   4 clicks  -> esp_restart()
 *   5 clicks  -> CMD_ERASE_PVALUE
 *   6 clicks  -> WIFI_CMD_TOGGLE
 *   6 s hold  -> CMD_ERASE_PVALUE + WIFI_CMD_RESET_AND_PORTAL, exactly once
 *                per press (longPressActionDone parity)
 *
 * OneButton 2.5.0 timing constants (binding, R2): debounce 50 ms, multi-click
 * window 400 ms, press-start 800 ms; the 6000 ms one-shot threshold lives in
 * the legacy during-long-press callback. Timebase: esp_timer_get_time()/1000
 * (int64 ms — kills the 49-day millis() wrap, R11). Because click/double/multi
 * handlers are all attached, OneButton's _maxClicks is 100 — a click burst is
 * resolved 400 ms after the last release (2-click suppresses 1-click, etc.).
 *
 * Queue posts are value-copied app_cmd_t / wifi_cmd_t with timeout 0 (G7);
 * this task never blocks on anything but its own 10 ms poll delay and never
 * touches display, sockets or NVS.
 *
 * GPIO16 (production button, RTC pad, no internal pull-up / IRQ): configured
 * as OUTPUT driving HIGH via driver/gpio, level read back with
 * gpio_get_level() — same physics as the Arduino tree's lib/gpio16 trick
 * (R3). Fallback CONFIG_EINKTODO_GPIO16_RAWREG swaps in the verbatim
 * gpio16_rtc.c register pokes (G11). Pins != 16: input + pull-up. Button is
 * active LOW in all cases.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "events.h"          /* app_cmd_t, wifi_cmd_t, BIT_* (main/events.h) */
#include "gpio16_rtc.h"

#include "button_input.h"

static const char *TAG = "button_input";

/* ---- OneButton 2.5.0 constants (R2, binding) ---------------------------- */
#define OB_DEBOUNCE_MS        50    /* OneButton _debounce_ms */
#define OB_CLICK_MS           400   /* OneButton _click_ms (multi-click window) */
#define OB_PRESS_MS           800   /* OneButton _press_ms (long-press start) */
#define OB_MAX_CLICKS         100   /* attachMultiClick() raises _maxClicks to 100 */
#define LONG_ACTION_MS        6000  /* legacy buttonLongDuringPress threshold */
#define POLL_INTERVAL_MS      10
#define DURING_PRESS_LOG_MS   1000  /* throttle for the held-for log line */

#define BUTTON_PIN            CONFIG_EINKTODO_BUTTON_PIN

/* ---- OneButton FSM state (mirrors OneButton.h members) ------------------ */
typedef enum {
    OCS_INIT,      /* idle, waiting for the level to become active */
    OCS_DOWN,      /* button pressed, waiting for release or press_ms */
    OCS_UP,        /* button released, counts as one short click */
    OCS_COUNT,     /* in the multi-click window */
    OCS_PRESS,     /* long press running, waiting for release */
    OCS_PRESSEND,  /* long press released */
} ob_state_t;

static struct {
    ob_state_t state;
    int        n_clicks;             /* _nClicks */
    int64_t    start_time;           /* _startTime, ms */
    int64_t    now;                  /* refreshed by ob_debounce() each tick */
    int        debounced_level;      /* debouncedPinLevel, -1 = unknown */
    int        last_debounce_level;  /* _lastDebouncePinLevel */
    int64_t    last_debounce_time;   /* _lastDebounceTime, ms */
    bool       long_action_done;     /* legacy longPressActionDone */
    int64_t    last_during_log;      /* log throttle, not part of OneButton */
} s_fsm;

/* Queue / event-group handles captured at start (value-copied messages only
 * flow through them — G7). */
static QueueHandle_t s_cmd_q;
static QueueHandle_t s_wifi_q;
static EventGroupHandle_t s_sys_events;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;  /* int64 ms timebase (R11) */
}

/* ---- raw level ----------------------------------------------------------- */

static esp_err_t button_gpio_init(void)
{
#if CONFIG_EINKTODO_GPIO16_RAWREG
    if (BUTTON_PIN == 16) {
        /* Legacy initButton() pin-16 branch, verbatim path (G11). */
        ESP_LOGI(TAG, "Button pin is 16, configuring gpio16_output_conf (raw RTC registers)...");
        gpio16_output_conf();
        gpio16_output_set(1);
        return ESP_OK;
    }
#endif
    gpio_config_t io = {
        .pin_bit_mask = (1UL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (BUTTON_PIN == 16) {
        /* RTC pad: no pull-up exists — drive it HIGH as an output and poll the
         * input readback, exactly what lib/gpio16 + OneButton did (R3). */
        ESP_LOGI(TAG, "Button pin is 16, configuring output-driving-HIGH readback...");
        io.mode = GPIO_MODE_OUTPUT;
        io.pull_up_en = GPIO_PULLUP_DISABLE;
    }
    esp_err_t err = gpio_config(&io);
    if (err != ESP_OK) {
        return err;
    }
    if (BUTTON_PIN == 16) {
        err = gpio_set_level(GPIO_NUM_16, 1);
    }
    return err;
}

static int button_read_level(void)
{
#if CONFIG_EINKTODO_GPIO16_RAWREG
    if (BUTTON_PIN == 16) {
        return (int)gpio16_input_get();
    }
#endif
    return gpio_get_level((gpio_num_t)BUTTON_PIN);
}

/* ---- gesture dispatch (legacy src/button.h callbacks) -------------------- */

static void post_app_cmd(int type, const char *arg)
{
    app_cmd_t cmd;  /* VALUE-COPIED: no pointers into task-local buffers */
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    if (arg != NULL) {
        strlcpy(cmd.arg, arg, sizeof(cmd.arg));
    }
    if (xQueueSend(s_cmd_q, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "cmd_queue full, dropping app cmd %d", type);
    }
}

static void post_wifi_cmd(int type)
{
    wifi_cmd_t cmd;  /* VALUE-COPIED: no pointers into task-local buffers */
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    if (xQueueSend(s_wifi_q, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "wifi_cmd_queue full, dropping wifi cmd %d", type);
    }
}

/* Legacy buttonClick(): portal-stop / force-refresh / portal-start. The
 * WiFiManager / WiFi.status() checks become sys_events bit reads; the actual
 * work happens in wifi_ctrl_task / cycle_task. */
static void on_click(void)
{
    ESP_LOGI(TAG, "Button clicked");
    EventBits_t bits = xEventGroupGetBits(s_sys_events);
    if (bits & BIT_PORTAL_ACTIVE) {
        ESP_LOGI(TAG, "Close Config Portal");
        post_wifi_cmd(WIFI_CMD_PORTAL_STOP);
    } else if (bits & BIT_WIFI_CONNECTED) {
        ESP_LOGI(TAG, "force update todo");
        post_app_cmd(CMD_FORCE_REFRESH, NULL);
    } else {
        ESP_LOGI(TAG, "Start Config Portal");
        post_wifi_cmd(WIFI_CMD_PORTAL_START);
    }
}

/* Legacy buttonDoubleClick(): raw event forwarded to the server; semantics
 * decided server-side (lib/device-events.ts). Gated on WiFi connected, as in
 * the legacy code. */
static void on_double_click(void)
{
    ESP_LOGI(TAG, "Button double clicked");
    EventBits_t bits = xEventGroupGetBits(s_sys_events);
    if (bits & BIT_WIFI_CONNECTED) {
        post_app_cmd(CMD_DEVICE_EVENT, "double-click");
    }
}

/* Legacy buttonMultiClick(): 4 = restart, 5 = erase pvalue, 6 = toggle WiFi,
 * anything else ignored. */
static void on_multi_click(int n_clicks)
{
    ESP_LOGI(TAG, "Button multi clicked: %d", n_clicks);
    switch (n_clicks) {
    case 4:
        esp_restart();
        break;
    case 5:
        post_app_cmd(CMD_ERASE_PVALUE, NULL);
        break;
    case 6:
        post_wifi_cmd(WIFI_CMD_TOGGLE);
        break;
    default:
        break;
    }
}

/* Legacy buttonLongPressStart(): arm the one-shot. */
static void on_long_press_start(void)
{
    ESP_LOGI(TAG, "Button long press start");
    s_fsm.long_action_done = false;
    s_fsm.last_during_log = 0;
}

/* Legacy buttonLongDuringPress(): fires every tick while held (OneButton
 * _long_press_interval_ms = 0); the 6 s action fires exactly once per press. */
static void on_during_long_press(int64_t pressed_ms)
{
    if (s_fsm.now - s_fsm.last_during_log >= DURING_PRESS_LOG_MS) {
        /* Legacy printed this every tick with \r; throttled to 1 Hz for ESP_LOG. */
        ESP_LOGI(TAG, "Button long pressed for %lld ms", (long long)pressed_ms);
        s_fsm.last_during_log = s_fsm.now;
    }

    if (pressed_ms > LONG_ACTION_MS && !s_fsm.long_action_done) {
        ESP_LOGI(TAG, "Button long pressed");
        ESP_LOGI(TAG, "Erasing configuration, persistent value and resetting WiFi");
        /* removePersistentValue() -> cycle_task; resetSettings()+portal -> wifi_ctrl_task */
        post_app_cmd(CMD_ERASE_PVALUE, NULL);
        post_wifi_cmd(WIFI_CMD_RESET_AND_PORTAL);
        s_fsm.long_action_done = true;  /* one-shot per press */
    }
}

/* Legacy buttonLongPressStop(). */
static void on_long_press_stop(void)
{
    ESP_LOGI(TAG, "Button long press stop");
}

/* ---- OneButton 2.5.0 FSM (line-for-line port) ---------------------------- */

static void ob_reset(void)
{
    s_fsm.state = OCS_INIT;
    s_fsm.n_clicks = 0;
    s_fsm.start_time = now_ms();
}

/* OneButton::debounce(): a level only becomes the debounced level after it
 * has been stable for OB_DEBOUNCE_MS. Also refreshes s_fsm.now (the FSM's
 * single per-tick timestamp, exactly like OneButton). */
static int ob_debounce(int value)
{
    s_fsm.now = now_ms();
    if (s_fsm.last_debounce_level == value) {
        if (s_fsm.now - s_fsm.last_debounce_time >= OB_DEBOUNCE_MS) {
            s_fsm.debounced_level = value;
        }
    } else {
        s_fsm.last_debounce_time = s_fsm.now;
        s_fsm.last_debounce_level = value;
    }
    return s_fsm.debounced_level;
}

/* OneButton::_fsm() with the legacy callbacks inlined. */
static void ob_fsm(bool active_level)
{
    int64_t wait_time = s_fsm.now - s_fsm.start_time;

    switch (s_fsm.state) {
    case OCS_INIT:
        /* (no idle handler attached in the legacy firmware) */
        if (active_level) {
            s_fsm.state = OCS_DOWN;
            s_fsm.start_time = s_fsm.now;
            s_fsm.n_clicks = 0;
        }
        break;

    case OCS_DOWN:
        if (!active_level) {
            s_fsm.state = OCS_UP;
            s_fsm.start_time = s_fsm.now;
        } else if (wait_time > OB_PRESS_MS) {
            on_long_press_start();
            s_fsm.state = OCS_PRESS;
        }
        break;

    case OCS_UP:
        /* count as a short button down */
        s_fsm.n_clicks++;
        s_fsm.state = OCS_COUNT;
        break;

    case OCS_COUNT:
        if (active_level) {
            /* button is down again */
            s_fsm.state = OCS_DOWN;
            s_fsm.start_time = s_fsm.now;
        } else if (wait_time >= OB_CLICK_MS || s_fsm.n_clicks == OB_MAX_CLICKS) {
            /* now we know how many clicks have been made */
            if (s_fsm.n_clicks == 1) {
                on_click();
            } else if (s_fsm.n_clicks == 2) {
                on_double_click();
            } else {
                on_multi_click(s_fsm.n_clicks);
            }
            ob_reset();
        }
        break;

    case OCS_PRESS:
        if (!active_level) {
            s_fsm.state = OCS_PRESSEND;
        } else {
            /* _long_press_interval_ms == 0: during-press fires every tick */
            on_during_long_press(s_fsm.now - s_fsm.start_time);
        }
        break;

    case OCS_PRESSEND:
        on_long_press_stop();
        ob_reset();
        break;

    default:
        s_fsm.state = OCS_INIT;
        break;
    }
}

/* ---- task ----------------------------------------------------------------- */

static void button_task(void *arg)
{
    (void)arg;
    for (;;) {
        /* Button is active LOW (OneButton activeLow default). */
        ob_fsm(ob_debounce(button_read_level()) == 0);
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

esp_err_t button_input_start(QueueHandle_t cmd_q, QueueHandle_t wifi_q, EventGroupHandle_t sys_events)
{
    if (cmd_q == NULL || wifi_q == NULL || sys_events == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing button...");
    ESP_LOGI(TAG, "Button pin: %d", BUTTON_PIN);

    s_cmd_q = cmd_q;
    s_wifi_q = wifi_q;
    s_sys_events = sys_events;

    s_fsm.state = OCS_INIT;
    s_fsm.n_clicks = 0;
    s_fsm.start_time = now_ms();
    s_fsm.debounced_level = -1;
    s_fsm.last_debounce_level = -1;
    s_fsm.last_debounce_time = 0;
    s_fsm.long_action_done = false;
    s_fsm.last_during_log = 0;

    esp_err_t err = button_gpio_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "button GPIO init failed: %s", esp_err_to_name(err));
        return err;
    }

    if (xTaskCreate(button_task, "button_task", 2048, NULL, 6, NULL) != pdPASS) {
        ESP_LOGE(TAG, "failed to create button_task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
