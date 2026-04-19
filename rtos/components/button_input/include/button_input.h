/* button_input.h — public API of the button gesture component (items 13-17).
 *
 * Spawns button_task (2048 B stack, prio 6): a 10 ms-poll FSM reproducing
 * OneButton 2.5.0 gesture timing on CONFIG_EINKTODO_BUTTON_PIN. Posts
 * value-copied app_cmd_t / wifi_cmd_t to the supplied queues with timeout 0;
 * reads BIT_PORTAL_ACTIVE / BIT_WIFI_CONNECTED from sys_events to pick the
 * single-click action. Never blocks, never touches display/sockets/NVS.
 */
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t button_input_start(QueueHandle_t cmd_q, QueueHandle_t wifi_q, EventGroupHandle_t sys_events);

#ifdef __cplusplus
}
#endif
