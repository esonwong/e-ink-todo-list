/* todo_fetch — display fetch + bitmap download half of the legacy
 * src/todoList.h (inventory items 27-33, 41-44 download half, 66).
 *
 * Owned by cycle_task (the single TLS/HTTP owner). One call to
 * todo_fetch_run() walks the endpoint candidate list, issues the
 * GET /api/display/v3?width=&height= request with the legacy headers
 * (api key, X-Device-Id, X-Device-Event one-shot, If-Modified-Since /
 * X-If-Modified-Since, X-Device-Firmware-Version, X-Device-Colors),
 * dispatches on the HTTP status (304 / 401 / 204 / 200 / other) and, for a
 * 200, streams the body straight into the raw `bitmap` partition (no
 * filesystem, no heap-resident body — R7) with the exact legacy layer byte
 * semantics preserved.
 *
 * Render handshake split (G2/G3/D7): todo_fetch does NOT touch the panel.
 * On a successful 200 download it fills out->render_req with a
 * DISP_RENDER_BITMAP request describing the bitmap partition; cycle_task
 * posts it to disp_queue and waits on BIT_DISP_DONE / BIT_DISP_ABORTED.
 *
 * Persistence split (G2/D10): todo_fetch NEVER writes NVS. The last-modified
 * string it parsed and the candidate that answered are returned to the
 * caller in out->last_modified / out->endpoint; cycle_task persists
 * todoLastModified and calls endpoint_remember_success() ONLY after
 * BIT_DISP_DONE for a 200-render cycle (and immediately for the reachable
 * non-render codes 304/401/204, where out->persist_endpoint_now is set).
 *
 * Length verification (G4/D5): the bytes received must equal Content-Length
 * AND the per-layer lengths must match X-Black/Red-Buffer-Length. Any
 * mismatch ⇒ skip render entirely (out->rendered = false, out->http_code
 * still 200), keep the previous image, do NOT advance Last-Modified.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#include "events.h"              /* display_req_t */
#include "endpoint_fallback.h"   /* endpoint_candidate_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Result of one todo_fetch_run(). VALUE-COPIED by the caller; no pointers
 * into todo_fetch-local buffers (G7). */
typedef struct {
    int  http_code;          /* last HTTP status reached, or <=0 on transport
                                failure across every candidate */

    bool rendered;           /* true ⇒ render_req is a valid DISP_RENDER_BITMAP
                                request the caller must post + handshake (G2);
                                false ⇒ caller posts a status screen itself or
                                does nothing (304) */
    display_req_t render_req;/* RENDER request (only when rendered == true) */

    /* Endpoint persistence hand-off (G2). endpoint_valid is true whenever a
     * candidate produced a reachable HTTP code; endpoint holds that
     * candidate. persist_endpoint_now is true for the reachable NON-render
     * codes (304/401/204) where legacy persisted immediately; for a 200 it is
     * false and the caller persists only after BIT_DISP_DONE. */
    bool                 endpoint_valid;
    bool                 persist_endpoint_now;
    endpoint_candidate_t endpoint;

    /* Last-Modified (or X-Data-Modified fallback) parsed from a 200 response,
     * NUL-terminated, ≤29 chars like legacy. Empty when no 200 body was
     * downloaded. cycle_task copies this into RunningValue.todoLastModified
     * ONLY after BIT_DISP_DONE (D10). */
    char last_modified[30];
} todo_fetch_result_t;

/* Run one fetch cycle. api_key is the bearer token, device_id the decimal
 * X-Device-Id string, device_event_or_null the one-shot X-Device-Event raw
 * input (e.g. "double-click") or NULL/"" when none is pending. The current
 * If-Modified-Since value is read from RunningValue via
 * todo_fetch_running_value (set by cycle_task through the accessor below).
 *
 * Returns ESP_OK whenever a candidate was reached (out->http_code set);
 * ESP_FAIL when every candidate failed at the transport level.
 */
esp_err_t todo_fetch_run(const char *api_key, const char *device_id,
                         const char *device_event_or_null,
                         todo_fetch_result_t *out);

/* Reset the RAM-only RunningValue last-modified so the next fetch is
 * unconditional (legacy cleanRunningValue() for the If-Modified-Since field).
 * cycle_task calls this on CMD_FORCE_REFRESH / CMD_API_KEY_UPDATED. */
void todo_fetch_clean_running_value(void);

/* Set the If-Modified-Since value todo_fetch_run() will send on the next
 * request. cycle_task calls this after a render completes (BIT_DISP_DONE) with
 * the value from out->last_modified, mirroring the legacy
 * RunningValue.todoLastModified write timing. NULL/"" clears it. */
void todo_fetch_set_last_modified(const char *last_modified);

#ifdef __cplusplus
}
#endif
