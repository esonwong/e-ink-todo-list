/* wifi_ctrl.c — WiFi control FSM + portal lifecycle (architecture §1, items
 * 18-25; legacy src/network.h + WiFiManager).
 *
 * wifi_ctrl_task is the single consumer of wifi_cmd_queue and owns every
 * esp_wifi call. esp_event handlers stay tiny (post a wifi_cmd_t / set bits)
 * and the real work runs in the task so nothing blocks the event loop.
 *
 * Behavior parity map (legacy -> here):
 *   initWifiWithManager      -> net_portal_init (hostname, APSTA, saved-or-portal)
 *   setConnectTimeout(30)    -> 30 s per-attempt timer (s_attempt_timer)
 *   setConnectRetries(3)     -> 3 attempts before connect-failed
 *   onWifiConnected          -> GOT_IP: BIT_WIFI_CONNECTED + "Connected to WiFi"
 *                               + sntp_clock_start (item 23/26)
 *   onWifiConnectFailed      -> stop portal, vTaskDelay(3000), reopen (item 24)
 *   setConfigPortalTimeout(600) -> 600 s one-shot xTimer -> stop httpd/dns +
 *                               esp_wifi_stop + "Config portal timeout" (item 25)
 *   configModeCallback       -> DISP_AP_INFO on portal start (item 20)
 *   setSaveParamsCallback    -> WIFI_CMD_SAVE -> esp_wifi_set_config + connect
 *                               + forward CMD_API_KEY_UPDATED to cycle_task (G1)
 *   resetSettings            -> esp_wifi_restore (WIFI_CMD_RESET_AND_PORTAL)
 */
#include "net_portal.h"
#include "portal_internal.h"
#include "dns_server.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "tcpip_adapter.h"

#include "device_id.h"
#include "sntp_clock.h"

static const char *TAG = "wifi_ctrl";

#define CONNECT_TIMEOUT_MS   30000      /* setConnectTimeout(30) */
#define CONNECT_RETRIES      3          /* setConnectRetries(3) */
#define PORTAL_TIMEOUT_MS    600000     /* setConfigPortalTimeout(600) */
#define CONNECT_FAIL_PAUSE_MS 3000      /* onWifiConnectFailed delay(3000) */

/* Internal control events posted from esp_event handlers into the task via the
 * shared wifi_cmd_queue. These reuse the wifi_cmd_t envelope with task-private
 * type values above the public WIFI_CMD_* enumerators. */
enum {
    WIFI_CMD_EV_GOT_IP = 100,
    WIFI_CMD_EV_DISCONNECTED,
    WIFI_CMD_EV_ATTEMPT_TIMEOUT,
    WIFI_CMD_EV_PORTAL_TIMEOUT,
    WIFI_CMD_EV_CONNECT_BEGIN,    /* kick a connect session in task context */
};

static QueueHandle_t        s_wifi_cmd_q;
static QueueHandle_t        s_cmd_q;        /* -> cycle_task (CMD_API_KEY_UPDATED) */
static QueueHandle_t        s_disp_q;
static EventGroupHandle_t   s_sys_events;

static TimerHandle_t        s_attempt_timer;   /* per-attempt 30 s connect timeout */
static TimerHandle_t        s_portal_timer;    /* 600 s portal one-shot */

static bool s_portal_active;
static bool s_connecting;
static int  s_attempt;

/* ---- small helpers ----------------------------------------------------- */

static void post_screen(int kind, const char *text)
{
    if (s_disp_q == NULL) {
        return;
    }
    display_req_t req = { .kind = kind };
    if (text != NULL) {
        strlcpy(req.text, text, sizeof(req.text));
    }
    /* Screens are advisory; don't block the FSM if display_task is busy. */
    xQueueSend(s_disp_q, &req, 0);
}

static void post_internal(int type)
{
    wifi_cmd_t cmd = { .type = (int)type };
    xQueueSend(s_wifi_cmd_q, &cmd, 0);
}

/* ---- esp_event handlers (tiny: set bits / post only) ------------------- */

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_STA_DISCONNECTED) {
        post_internal(WIFI_CMD_EV_DISCONNECTED);
    }
}

static void ip_event_handler(void *arg, esp_event_base_t base,
                             int32_t id, void *data)
{
    (void)arg; (void)base; (void)data;
    if (id == IP_EVENT_STA_GOT_IP) {
        post_internal(WIFI_CMD_EV_GOT_IP);
    }
}

static void attempt_timer_cb(TimerHandle_t t)
{
    (void)t;
    post_internal(WIFI_CMD_EV_ATTEMPT_TIMEOUT);
}

static void portal_timer_cb(TimerHandle_t t)
{
    (void)t;
    post_internal(WIFI_CMD_EV_PORTAL_TIMEOUT);
}

/* ---- portal + connect lifecycle (task context only) -------------------- */

static void portal_open(void)
{
    if (s_portal_active) {
        return;
    }
    s_portal_active = true;
    xEventGroupSetBits(s_sys_events, BIT_PORTAL_ACTIVE);

    /* Ensure the SoftAP is up. on_got_ip() drops us to STA-only after a
     * successful connect, so re-assert APSTA whenever the portal (re)opens
     * (idempotent at boot, which already starts in APSTA). */
    esp_wifi_set_mode(WIFI_MODE_APSTA);

    /* Captive DNS catcher -> softAP IP. Wildcards every A query to the AP IP so
     * the OS captive-portal sheet auto-opens the config page (WiFiManager parity). */
    tcpip_adapter_ip_info_t ip = { 0 };
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_AP, &ip);

    portal_httpd_start(s_wifi_cmd_q);
    dns_server_start(ip.ip);

    /* configModeCallback parity: show AP join info + config server URL on the e-ink. */
    char info[64];
    snprintf(info, sizeof(info), "%s / %s\nhttp://" IPSTR,
             PORTAL_AP_SSID, PORTAL_AP_PASSWORD, IP2STR(&ip.ip));
    post_screen(DISP_AP_INFO, info);

    /* 600 s one-shot portal timeout. */
    xTimerStart(s_portal_timer, 0);
    ESP_LOGI(TAG, "portal opened");
}

static void portal_close(void)
{
    if (!s_portal_active) {
        return;
    }
    s_portal_active = false;
    xEventGroupClearBits(s_sys_events, BIT_PORTAL_ACTIVE);
    xTimerStop(s_portal_timer, 0);
    portal_httpd_stop();
    dns_server_stop();
    ESP_LOGI(TAG, "portal closed");
}

static void connect_begin(void)
{
    s_connecting = true;
    s_attempt = 0;
    post_screen(DISP_TEXT_CENTER, "Trying to connect to WiFi");
    esp_wifi_connect();
    xTimerStart(s_attempt_timer, 0);
}

/* onWifiConnectFailed parity: stop portal, pause, reopen. */
static void connect_failed(void)
{
    s_connecting = false;
    xTimerStop(s_attempt_timer, 0);
    post_screen(DISP_TEXT_CENTER, "WiFi connect failed");

    if (s_portal_active) {
        portal_close();
        vTaskDelay(pdMS_TO_TICKS(CONNECT_FAIL_PAUSE_MS));
    }
    portal_open();
}

/* WIFI_EVENT_STA_DISCONNECTED / attempt timeout while connecting. */
static void on_connect_setback(const char *why)
{
    if (!s_connecting) {
        return;     /* a stray disconnect after we already gave up/connected */
    }
    s_attempt++;
    ESP_LOGW(TAG, "connect %s (attempt %d/%d)", why, s_attempt, CONNECT_RETRIES);
    if (s_attempt >= CONNECT_RETRIES) {
        connect_failed();
        return;
    }
    /* Retry within the same connect session. */
    xTimerStop(s_attempt_timer, 0);
    esp_wifi_connect();
    xTimerStart(s_attempt_timer, 0);
}

static void on_got_ip(void)
{
    s_connecting = false;
    xTimerStop(s_attempt_timer, 0);
    portal_close();
    /* STA is up — drop the SoftAP so it stops beaconing and frees its RAM.
     * portal_close() only stops httpd/dns; staying in APSTA permanently kept
     * the SoftAP up and starved the heap, which made TLS handshakes
     * intermittently OOM during ECDSA P-384 cert verification. portal_open()
     * restores APSTA whenever the config portal is needed again. */
    esp_wifi_set_mode(WIFI_MODE_STA);
    xEventGroupSetBits(s_sys_events, BIT_WIFI_CONNECTED);

    /* onWifiConnected parity: connected screen + setClock(). */
    post_screen(DISP_WIFI_CONNECTED, "Connected to WiFi");
    sntp_clock_start();
    ESP_LOGI(TAG, "WiFi connected, IP acquired");
}

/* WIFI_CMD_SAVE: apply STA config + connect, forward the api key to cycle_task
 * (G1 — cycle_task is the single NVS `setting` writer). */
static void on_save(const wifi_cmd_t *cmd)
{
    ESP_LOGI(TAG, "WIFI_CMD_SAVE ssid='%s'", cmd->save.ssid);

    wifi_config_t sta = { 0 };
    strlcpy((char *)sta.sta.ssid, cmd->save.ssid, sizeof(sta.sta.ssid));
    strlcpy((char *)sta.sta.password, cmd->save.pass, sizeof(sta.sta.password));
    esp_wifi_set_config(ESP_IF_WIFI_STA, &sta);

    /* preSaveConfigCallback parity: "trying to connect to a new WiFi" screen. */
    post_screen(DISP_TEXT_CENTER, "Trying to connect to a new WiFi");

    /* Forward the api key so cycle_task persists it + cleanRunningValue + fetch
     * (G1). Empty keys are forwarded too (matches setSaveParamsCallback, which
     * strcpy'd whatever the field held). */
    if (s_cmd_q != NULL) {
        app_cmd_t kc = { .type = CMD_API_KEY_UPDATED };
        strlcpy(kc.arg, cmd->save.api_key, sizeof(kc.arg));
        xQueueSend(s_cmd_q, &kc, 0);
    }

    /* Begin a fresh connect session against the new creds. connect_begin()
     * resets the attempt counter; esp_wifi_connect() applies the new config and
     * (re)associates without a manual disconnect first — a manual disconnect
     * here would race a STA_DISCONNECTED event into the new session and burn a
     * retry. */
    connect_begin();
}

static void on_reset_and_portal(void)
{
    ESP_LOGI(TAG, "reset settings + reopen portal");
    s_connecting = false;
    xTimerStop(s_attempt_timer, 0);
    xEventGroupClearBits(s_sys_events, BIT_WIFI_CONNECTED);
    esp_wifi_disconnect();
    esp_wifi_restore();                 /* forget saved STA creds */
    /* esp_wifi_restore() resets mode to STA default; restore APSTA + AP cfg. */
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    portal_close();
    portal_open();
}

static void on_portal_timeout(void)
{
    /* configPortalTimeoutCallback parity: WIFI_OFF + "press button to restart". */
    ESP_LOGI(TAG, "portal timeout");
    portal_close();
    esp_wifi_stop();
    post_screen(DISP_PORTAL_TIMEOUT, "Config portal timeout");
}

/* ---- task -------------------------------------------------------------- */

static void wifi_ctrl_task(void *arg)
{
    (void)arg;
    wifi_cmd_t cmd;
    while (1) {
        if (xQueueReceive(s_wifi_cmd_q, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        /* cmd.type may carry a task-private WIFI_CMD_EV_* value above the
         * public enumerators; cast to int so -Werror=switch is satisfied. */
        switch ((int)cmd.type) {
        case WIFI_CMD_PORTAL_START:        portal_open();         break;
        case WIFI_CMD_PORTAL_STOP:         portal_close();        break;
        case WIFI_CMD_TOGGLE:
            if (s_portal_active) {
                portal_close();
            } else {
                portal_open();
            }
            break;
        case WIFI_CMD_RESET_AND_PORTAL:    on_reset_and_portal(); break;
        case WIFI_CMD_SAVE:                on_save(&cmd);         break;

        /* task-private control events */
        case WIFI_CMD_EV_CONNECT_BEGIN:    connect_begin();       break;
        case WIFI_CMD_EV_GOT_IP:           on_got_ip();           break;
        case WIFI_CMD_EV_DISCONNECTED:     on_connect_setback("disconnected"); break;
        case WIFI_CMD_EV_ATTEMPT_TIMEOUT:  on_connect_setback("timeout");      break;
        case WIFI_CMD_EV_PORTAL_TIMEOUT:   on_portal_timeout();   break;
        default:
            ESP_LOGW(TAG, "unknown wifi_cmd type %d", (int)cmd.type);
            break;
        }
    }
}

/* ---- public API -------------------------------------------------------- */

BaseType_t net_portal_post(const wifi_cmd_t *cmd, TickType_t ticks)
{
    if (s_wifi_cmd_q == NULL) {
        return pdFALSE;
    }
    return xQueueSend(s_wifi_cmd_q, cmd, ticks);
}

bool net_portal_wifi_saved(void)
{
    wifi_config_t cfg = { 0 };
    if (esp_wifi_get_config(ESP_IF_WIFI_STA, &cfg) != ESP_OK) {
        return false;
    }
    return cfg.sta.ssid[0] != 0;
}

esp_err_t net_portal_init(QueueHandle_t wifi_cmd_q, QueueHandle_t cmd_q,
                          QueueHandle_t disp_q, EventGroupHandle_t sys_events)
{
    s_wifi_cmd_q = wifi_cmd_q;
    s_cmd_q      = cmd_q;
    s_disp_q     = disp_q;
    s_sys_events = sys_events;

    tcpip_adapter_init();
    /* esp_event_loop_create_default() is idempotent enough for our single
     * caller; tolerate "already created" if app_main set it up. */
    esp_err_t lerr = esp_event_loop_create_default();
    if (lerr != ESP_OK && lerr != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "event loop create failed: %s", esp_err_to_name(lerr));
        return lerr;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID,
                                               &ip_event_handler, NULL));

    /* APSTA: AP for the portal, STA for the connect. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    wifi_config_t ap = { 0 };
    strlcpy((char *)ap.ap.ssid, PORTAL_AP_SSID, sizeof(ap.ap.ssid));
    ap.ap.ssid_len      = strlen(PORTAL_AP_SSID);
    strlcpy((char *)ap.ap.password, PORTAL_AP_PASSWORD, sizeof(ap.ap.password));
    ap.ap.authmode      = WIFI_AUTH_WPA_WPA2_PSK;
    ap.ap.max_connection = 4;
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap));

    ESP_ERROR_CHECK(esp_wifi_start());

    /* Hostname must be set after the interface exists (start) and before
     * connect — "e-ink-todo-list-<chipid hex>" (item 18). */
    tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, device_id_hostname());

    s_attempt_timer = xTimerCreate("wifi_attempt", pdMS_TO_TICKS(CONNECT_TIMEOUT_MS),
                                   pdFALSE, NULL, attempt_timer_cb);
    s_portal_timer  = xTimerCreate("portal_to", pdMS_TO_TICKS(PORTAL_TIMEOUT_MS),
                                   pdFALSE, NULL, portal_timer_cb);
    if (s_attempt_timer == NULL || s_portal_timer == NULL) {
        ESP_LOGE(TAG, "timer create failed");
        return ESP_FAIL;
    }

    /* prio 5, stack 3072 per architecture §1 task table. */
    if (xTaskCreate(wifi_ctrl_task, "wifi_ctrl", 3072, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "wifi_ctrl_task create failed");
        return ESP_FAIL;
    }

    /* wifiManager.preloadWiFi(WIFI_SSID, WIFI_PASS) parity: with no saved
     * creds, seed the STA config from the Kconfig preset so autoConnect below
     * connects instead of opening the portal. */
    if (!net_portal_wifi_saved() && CONFIG_EINKTODO_DEV_WIFI_SSID[0] != '\0') {
        wifi_config_t sta = { 0 };
        strlcpy((char *)sta.sta.ssid, CONFIG_EINKTODO_DEV_WIFI_SSID, sizeof(sta.sta.ssid));
        strlcpy((char *)sta.sta.password, CONFIG_EINKTODO_DEV_WIFI_PASS,
                sizeof(sta.sta.password));
        esp_wifi_set_config(ESP_IF_WIFI_STA, &sta);
        ESP_LOGI(TAG, "preloaded WiFi creds for '%s'", CONFIG_EINKTODO_DEV_WIFI_SSID);
    }

    /* autoConnect parity: saved creds -> connect; else open portal. The task
     * owns all state transitions, so kick it via the queue. */
    if (net_portal_wifi_saved()) {
        ESP_LOGI(TAG, "WiFi creds saved, connecting");
        /* connect_begin() must run in task context (it owns esp_wifi); kick it
         * via the private connect-begin event. */
        post_internal(WIFI_CMD_EV_CONNECT_BEGIN);
    } else {
        ESP_LOGI(TAG, "no WiFi creds, opening portal");
        wifi_cmd_t open = { .type = WIFI_CMD_PORTAL_START };
        xQueueSend(s_wifi_cmd_q, &open, 0);
    }

    return ESP_OK;
}
