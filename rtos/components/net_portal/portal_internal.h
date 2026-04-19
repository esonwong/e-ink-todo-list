/* portal_internal.h — net_portal component-private glue between wifi_ctrl.c
 * (FSM/lifecycle) and portal_httpd.c (UI handlers). Not part of the public API.
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* AP credentials — legacy AP_SSID / AP_PASSWORD (config.h). */
#define PORTAL_AP_SSID     "E-ink Todo List AP"
#define PORTAL_AP_PASSWORD "einktodo.com"
#define PORTAL_TITLE       "E-ink Todo List"

/* Start the portal httpd. Handlers post WIFI_CMD_SAVE to wifi_cmd_q with
 * timeout 0 (G6). Idempotent. */
esp_err_t portal_httpd_start(QueueHandle_t wifi_cmd_q);

/* Stop the portal httpd. Safe to call when not running. */
void portal_httpd_stop(void);

#ifdef __cplusplus
}
#endif
