/* ota_update.h — manual esp_ota_* firmware update loop (items 51-54), mirror
 * of legacy lib/update/UpdateFirmware.h.
 *
 * Runs ON cycle_task (no own task); fetch and OTA are serialized by
 * construction, so all TLS/HTTP objects and the single pinned-TLS context are
 * never used concurrently with todo_fetch (§1, R14).
 *
 * Why a manual loop and not esp_https_ota: v3.4's esp_https_ota helper has no
 * custom-header hook (we must send X-Device-Firmware-Version) and no progress
 * callback, and it cannot walk the endpoint-candidate / leaf-pin fallback list
 * (R9). So this drives esp_http_client open/fetch_headers itself, dispatches
 * 304/200, and streams the body straight into esp_ota_write.
 *
 * Server contract (R9 / §3.4): the RTOS line uses a SEPARATE channel
 * CONFIG_EINKTODO_OTA_URL (default https://www.einktodo.com/api/update/rtos/
 * firmware) — NEVER the frozen legacy /api/update/firmware. The device sends
 * "X-Device-Firmware-Version: <PROJECT_VER>" (from esp_app_desc_t.version) and
 * the server replies 304 when it already matches the pinned rtos-v* release,
 * else streams firmware-rtos.bin. esp_ota_end()'s image-magic validation is
 * the defense-in-depth guard against ever flashing an Arduino .bin.
 *
 * UpdateFiles.h / certs.ar / FILE_UPDATE_URL are NOT ported (deviation D4):
 * CA trust is compiled into the image and refreshed via firmware OTA, so the
 * generic file-update channel has no remaining consumer.
 */
#ifndef EINKTODO_OTA_UPDATE_H
#define EINKTODO_OTA_UPDATE_H

#include "esp_err.h"
#include "endpoint_fallback.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check the RTOS firmware channel and, if a newer image is offered,
 *        download + flash it and esp_restart() (does not return on success).
 *
 * No-op (returns ESP_OK) unless CONFIG_EINKTODO_ENABLE_OTA is set — exact
 * parity with the legacy "#ifdef ENABLE_OTA" guard.
 *
 * Throttled to one network check per 10 days (60 s under
 * CONFIG_EINKTODO_DEBUG_OTA) via the pvalue NVS key "fw_last_check"; pass
 * force=true to bypass the throttle (e.g. a button-driven manual check).
 *
 * Walks the supplied candidate list in order (already prioritized by the
 * caller via endpoint_prioritize_last_good); attaches the curated CA bundle
 * for the primary and the leaf pin for fallbacks, exactly like the fetch path.
 * On a reachable HTTP code it persists the winning endpoint via
 * endpoint_remember_success(), matching legacy UpdateFirmware behaviour.
 *
 * @param cands  candidate list (typically built for CONFIG_EINKTODO_OTA_URL
 *               and "/api/update/rtos/firmware")
 * @param n      number of valid candidates
 * @param force  bypass the 10-day throttle
 * @return ESP_OK when the check completed without flashing (no update, OTA
 *         disabled, throttled, or all candidates failed); does not return on a
 *         successful flash (esp_restart). An esp_err_t on a local error.
 */
esp_err_t ota_update_check(const endpoint_candidate_t *cands, int n, bool force);

#ifdef __cplusplus
}
#endif

#endif /* EINKTODO_OTA_UPDATE_H */
