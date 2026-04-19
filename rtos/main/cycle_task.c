/* cycle_task.c — the loop() mirror (architecture §1/§2, items 8-11).
 *
 * Single consumer of cmd_queue; sole owner of RunningValue (RAM-only), all
 * TLS/HTTP objects, and the NVS writes that flow from fetch/OTA/api-key. See
 * cycle_task.h for the full contract.
 *
 * Legacy mapping (src/main.cpp loop(), src/todoList.h downloadAndDrawTodo,
 * src/button.h buttonClick/buttonDoubleClick/buttonMultiClick):
 *   - 60 s cadence gate `time(nullptr) - displayContentLastCheckTime > N`
 *     -> queue-receive timeout countdown gated on time(NULL) (R11).
 *   - `updating` re-entrancy flag -> impossible by construction (single
 *     consumer task); the flag is dropped, no behavior change.
 *   - updateFiles() -> NOT PORTED (deviation D4).
 *   - updateFireWare() -> ota_update_check().
 *   - downloadAndDrawTodo() -> todo_fetch_run() + the G2/G3 render handshake.
 */
#include "cycle_task.h"

#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_system.h"

#include "events.h"
#include "device_id.h"

#include "store.h"
#include "endpoint_fallback.h"
#include "todo_fetch.h"
#include "ota_update.h"
#include "eink_display.h"

static const char *TAG = "cycle_task";

/* RunningValue, RAM-only (item 48). The legacy struct also held
 * todoLastModified, but that field now lives inside todo_fetch and is driven
 * through its accessors (todo_fetch_clean_running_value /
 * todo_fetch_set_last_modified) per the G2/D10 split. */
static time_t s_last_check_time;          /* displayContentLastCheckTime */

/* One-shot raw device event to forward on the next fetch (legacy
 * pendingDeviceEvent, e.g. "double-click"). Cleared by todo_fetch once it has
 * been sent to a reachable endpoint — but kept here too so a fetch that is
 * deferred (no WiFi) does not lose it. */
static char s_pending_event[42];

/* The bearer api key cycle_task fetches with, refreshed on CMD_API_KEY_UPDATED
 * and re-loaded from NVS at task start. */
static char s_api_key[42];

/* ------------------------------------------------------------------ */
/* Render handshake (G2/G3/D7)                                        */
/* ------------------------------------------------------------------ */

/* Post the result's render request and, for a DISP_RENDER_BITMAP, wait for the
 * panel to finish. Returns true when a 200-bitmap actually rendered
 * (BIT_DISP_DONE) so the caller may persist Last-Modified + endpoint (G2). The
 * status screens (DISP_AUTH_401 / DISP_NO_CONTENT) are posted the same way but
 * need no handshake-gated persistence. A wait timeout is fatal (G3). */
static bool post_render_and_wait(const display_req_t *req)
{
    /* display_task runs at a lower priority, so clear before posting — a bit
     * left over from an earlier render must not satisfy this wait. */
    xEventGroupClearBits(sys_events, BIT_DISP_DONE | BIT_DISP_ABORTED);
    if (display_request(req, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "disp_queue full, dropping render request");
        return false;
    }

    /* Only a full bitmap render signals DONE/ABORTED; the status screens need
     * no handshake-gated persistence, so it does not wait. */
    if (req->kind != DISP_RENDER_BITMAP) {
        return false;
    }

    EventBits_t bits = xEventGroupWaitBits(
        sys_events, BIT_DISP_DONE | BIT_DISP_ABORTED,
        pdTRUE /* clear on exit */, pdFALSE /* any */,
        pdMS_TO_TICKS(CONFIG_EINKTODO_RENDER_TIMEOUT_MS));

    if ((bits & (BIT_DISP_DONE | BIT_DISP_ABORTED)) == 0) {
        /* G3: the panel never signalled within the panel-derived window. The
         * display_task may still own the panel; the only safe action is to
         * restart rather than risk TLS + page buffers coexisting next cycle. */
        ESP_LOGE(TAG, "render wait timed out (%d ms) — restarting",
                 CONFIG_EINKTODO_RENDER_TIMEOUT_MS);
        esp_restart();
    }

    if (bits & BIT_DISP_ABORTED) {
        ESP_LOGW(TAG, "render aborted by display_task");
        return false;   /* D10: a failed render must not advance Last-Modified */
    }
    return true;        /* BIT_DISP_DONE */
}

/* ------------------------------------------------------------------ */
/* One fetch cycle (legacy downloadAndDrawTodo)                        */
/* ------------------------------------------------------------------ */

static void run_fetch(void)
{
    /* Legacy set displayContentLastCheckTime at the top of
     * downloadAndDrawTodo(); mirror that so a force-refresh also resets the
     * cadence clock. */
    s_last_check_time = time(NULL);

    ESP_LOGI(TAG, "Free memory before fetch: %u", esp_get_free_heap_size());

    todo_fetch_result_t res;
    memset(&res, 0, sizeof(res));

    const char *event = s_pending_event[0] != '\0' ? s_pending_event : NULL;

    esp_err_t err = todo_fetch_run(s_api_key, device_id_str(), event, &res);
    if (err != ESP_OK) {
        /* Every candidate failed at the transport level: keep the pending
         * event so the next poll re-sends it (D3), keep the previous image. */
        ESP_LOGW(TAG, "todo_fetch_run: all candidates unreachable");
        return;
    }

    /* A reachable HTTP code was seen, so the one-shot event has been delivered
     * (todo_fetch only clears its own copy; clear ours too). */
    s_pending_event[0] = '\0';

    if (res.rendered) {
        /* 200 with a verified bitmap, OR a status screen (401/204). For the
         * bitmap case post + wait; persist only on DISP_DONE (G2/D10). */
        bool done = post_render_and_wait(&res.render_req);

        if (res.render_req.kind == DISP_RENDER_BITMAP) {
            if (done) {
                todo_fetch_set_last_modified(res.last_modified);
                if (res.endpoint_valid && !res.persist_endpoint_now) {
                    endpoint_remember_success(&res.endpoint);
                }
            }
            /* aborted/timeout-restart: nothing persisted */
            return;
        }
        /* 401 / 204 status screen: legacy persisted the endpoint immediately
         * (handled via persist_endpoint_now below); no Last-Modified update. */
    }

    /* Reachable non-render codes (304/401/204/other-reachable): legacy called
     * rememberSuccessfulEndpoint() immediately and did not touch
     * Last-Modified. */
    if (res.persist_endpoint_now && res.endpoint_valid) {
        endpoint_remember_success(&res.endpoint);
    }
}

/* ------------------------------------------------------------------ */
/* One full cycle: OTA check then fetch (legacy loop() body)           */
/* ------------------------------------------------------------------ */

static void run_cycle(void)
{
    /* OTA first, exactly like loop() ran updateFireWare() before
     * downloadAndDrawTodo() — they then never contend for the single TLS
     * context (§1). ota_update_check() esp_restart()s on a successful flash
     * and otherwise returns ESP_OK. It is a no-op unless ENABLE_OTA. */
    endpoint_candidate_t ota_cands[ENDPOINT_MAX_CANDIDATES];
    int n = endpoint_build_candidates(CONFIG_EINKTODO_OTA_URL,
                                      "/api/update/rtos/firmware", ota_cands);
    endpoint_prioritize_last_good(ota_cands, n);
    ota_update_check(ota_cands, n, false /* honor throttle */);

    run_fetch();

    ESP_LOGI(TAG, "Cycle done, min free heap: %u",
             esp_get_minimum_free_heap_size());
}

/* ------------------------------------------------------------------ */
/* Command dispatch (legacy button handlers)                          */
/* ------------------------------------------------------------------ */

static void handle_cmd(const app_cmd_t *cmd)
{
    switch (cmd->type) {
    case CMD_FORCE_REFRESH:
        /* Single-click force update (buttonClick WL_CONNECTED branch): the
         * portal-vs-refresh-vs-portal decision is made in button_input from
         * the event bits, so a CMD_FORCE_REFRESH here always means "fetch". */
        ESP_LOGI(TAG, "CMD_FORCE_REFRESH");
        todo_fetch_clean_running_value();
        run_fetch();
        break;

    case CMD_DEVICE_EVENT:
        /* buttonDoubleClick: stash the raw event for the next request, then
         * clean + fetch. cmd->arg holds e.g. "double-click". */
        ESP_LOGI(TAG, "CMD_DEVICE_EVENT: %s", cmd->arg);
        strncpy(s_pending_event, cmd->arg, sizeof(s_pending_event) - 1);
        s_pending_event[sizeof(s_pending_event) - 1] = '\0';
        todo_fetch_clean_running_value();
        run_fetch();
        break;

    case CMD_ERASE_PVALUE:
        /* 5-click / part of 6 s hold (removePersistentValue): wipe the pvalue
         * namespace only; api key in "setting" survives. */
        ESP_LOGI(TAG, "CMD_ERASE_PVALUE");
        store_erase_pvalue();
        break;

    case CMD_API_KEY_UPDATED:
        /* G1: cycle_task is the sole "setting" writer. Mirror legacy
         * setSaveParamsCallback (strcpy apiKey + saveSetting + cleanRunningValue)
         * then fetch with the new key. */
        ESP_LOGI(TAG, "CMD_API_KEY_UPDATED");
        store_set_api_key(cmd->arg);
        strncpy(s_api_key, cmd->arg, sizeof(s_api_key) - 1);
        s_api_key[sizeof(s_api_key) - 1] = '\0';
        todo_fetch_clean_running_value();
        run_fetch();
        break;

    default:
        ESP_LOGW(TAG, "unknown cmd type %d", (int)cmd->type);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Task body                                                          */
/* ------------------------------------------------------------------ */

static void cycle_task(void *arg)
{
    QueueHandle_t cmd_q = (QueueHandle_t)arg;

    /* Load the bearer key (store applies the CONFIG_EINKTODO_API_KEY override
     * RAM-only; empty when nothing stored). */
    if (store_get_api_key(s_api_key) != ESP_OK) {
        s_api_key[0] = '\0';
    }

    const int interval = CONFIG_EINKTODO_CHECK_INTERVAL;

    /* Run an initial cycle as soon as WiFi is up (legacy first loop() iteration
     * fires at once, but legacy connects WiFi in setup() *before* loop()). This
     * task races STA association at boot, so block on BIT_WIFI_CONNECTED first:
     * fetching pre-connect fails DNS and then the unreachable IP fallback blocks
     * ~30 s, leaving the screen stuck on "Connected to WiFi". With no saved creds
     * the device is in portal mode and STA never connects, so this stays blocked
     * harmlessly until the user configures WiFi and GOT_IP sets the bit.
     *
     * Also wait for BIT_TIME_SYNCED: TLS validates the server cert's not-before/
     * not-after against the clock, and a pre-NTP clock (1970) fails that check
     * (mbedtls handshake -0x2700 = X509_CERT_VERIFY_FAILED). Legacy setClock()
     * likewise blocked on NTP before the first fetch. */
    xEventGroupWaitBits(sys_events, BIT_WIFI_CONNECTED | BIT_TIME_SYNCED,
                        pdFALSE /* keep */, pdTRUE /* all */, portMAX_DELAY);
    run_cycle();

    for (;;) {
        /* Cadence: time until the next poll, gated on time(NULL) like legacy
         * so an NTP jump behaves identically (R11). */
        time_t now = time(NULL);
        time_t elapsed = now - s_last_check_time;
        TickType_t wait;
        if (elapsed >= interval) {
            wait = 0;
        } else {
            wait = pdMS_TO_TICKS((interval - elapsed) * 1000);
        }

        app_cmd_t cmd;
        if (xQueueReceive(cmd_q, &cmd, wait) == pdTRUE) {
            /* A command preempts the cadence wait. */
            handle_cmd(&cmd);
        } else {
            /* Timed out: the 60 s interval elapsed — run a normal cycle, but
             * only while connected. If a disconnect is in progress, block for
             * reconnect rather than fetch into a dead network (and stall on the
             * IP fallback). The cadence resumes on the next iteration. */
            const EventBits_t need = BIT_WIFI_CONNECTED | BIT_TIME_SYNCED;
            if ((xEventGroupGetBits(sys_events) & need) != need) {
                xEventGroupWaitBits(sys_events, need, pdFALSE, pdTRUE, portMAX_DELAY);
            }
            run_cycle();
        }
    }
}

void cycle_task_start(QueueHandle_t cmd_q)
{
    xTaskCreate(cycle_task, "cycle_task", 8192, (void *)cmd_q, 5, NULL);
}
