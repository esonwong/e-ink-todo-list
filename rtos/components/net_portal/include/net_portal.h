/* net_portal — WiFi control FSM + captive config portal (architecture §1/§2,
 * items 18-25; legacy src/network.h + WiFiManager).
 *
 * Owns every esp_wifi call and the portal lifecycle. The wifi_ctrl_task is the
 * single consumer of wifi_cmd_queue; portal httpd URI handlers post to that
 * queue with timeout 0 and return immediately (G6). On WIFI_CMD_SAVE the task
 * applies the STA config + connect and forwards CMD_API_KEY_UPDATED to
 * cmd_queue so cycle_task (the sole NVS `setting` writer) persists the key (G1).
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#include "esp_err.h"

#include "events.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the WiFi stack (APSTA, hostname), create wifi_ctrl_task, and either
 * auto-connect saved creds or open the portal — mirrors initWifiWithManager().
 *   wifi_cmd_q : button/httpd → wifi_ctrl_task commands
 *   cmd_q      : wifi_ctrl_task → cycle_task (CMD_API_KEY_UPDATED on save)
 *   disp_q     : screen requests (AP info, connected, portal timeout, …)
 *   sys_events : sets/clears BIT_WIFI_CONNECTED / BIT_PORTAL_ACTIVE
 */
esp_err_t net_portal_init(QueueHandle_t wifi_cmd_q, QueueHandle_t cmd_q,
                          QueueHandle_t disp_q, EventGroupHandle_t sys_events);

/* Post a command to wifi_ctrl_task. button_input/httpd use this; pass ticks=0
 * to never block (G6). Returns pdTRUE if queued. */
BaseType_t net_portal_post(const wifi_cmd_t *cmd, TickType_t ticks);

/* True if STA creds are stored (esp_wifi_get_config sta.ssid[0] != 0) —
 * mirrors WiFiManager getWiFiIsSaved(). */
bool net_portal_wifi_saved(void);

#ifdef __cplusplus
}
#endif
