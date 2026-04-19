/* cycle_task.h — the loop() mirror (architecture §1/§2, items 8-11).
 *
 * cycle_task is the single consumer of cmd_queue and the sole owner of:
 *   - RunningValue (RAM-only, item 48: last-check time + If-Modified-Since,
 *     the latter lives inside todo_fetch and is driven via its accessors),
 *   - all esp_http_client / TLS objects (by construction — ota_update_check()
 *     and todo_fetch_run() both run on this task, never concurrently),
 *   - NVS "setting" writes (G1: CMD_API_KEY_UPDATED -> store_set_api_key) and
 *     the NVS "pvalue" writes that flow from fetch/OTA (last-good endpoint,
 *     OTA throttle timestamp).
 *
 * Cadence: blocks on xQueueReceive(cmd_queue, pdMS_TO_TICKS(remaining)) where
 * `remaining` counts down to the next 60 s poll gated on time(NULL) (parity
 * with legacy `time(nullptr) - displayContentLastCheckTime > INTERVAL`, R11).
 * A queued command preempts the wait (force refresh etc.).
 *
 * Render handshake (G2/G3/D7): on a 200 the fetch returns a DISP_RENDER_BITMAP
 * request; cycle_task posts it to disp_queue and waits on
 * BIT_DISP_DONE / BIT_DISP_ABORTED with a panel-derived timeout
 * (CONFIG_EINKTODO_RENDER_TIMEOUT_MS). A wait timeout is fatal -> esp_restart()
 * (the display_task may still own the panel; never proceed). todoLastModified
 * and endpoint_remember_success() are persisted ONLY after BIT_DISP_DONE.
 */
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create cycle_task (8192 B stack, prio 5). cmd_q is the shared cmd_queue;
 * the task consumes it and produces onto disp_queue / waits on BIT_DISP_*. */
void cycle_task_start(QueueHandle_t cmd_q);

#ifdef __cplusplus
}
#endif
