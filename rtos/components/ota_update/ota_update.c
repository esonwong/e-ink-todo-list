/* ota_update.c — manual esp_ota_* firmware update loop. See ota_update.h.
 * Mirror of legacy lib/update/UpdateFirmware.h (items 51-54), R9.
 *
 * Flow per cycle (driven by cycle_task):
 *   throttle check (fw_last_check, 10 d / 60 s) -> persist check time ->
 *   for each candidate: esp_http_client open + fetch_headers ->
 *     304  => no update, remember endpoint, return
 *     200  => "Updating Firmware" screen -> esp_ota_begin(next slot) ->
 *             chunked esp_ota_write (%-on-change log, esp_task_wdt_reset) ->
 *             esp_ota_end (image validation) -> esp_ota_set_boot_partition ->
 *             "Updated Firmware Success" + 2 s -> esp_restart()
 *     other => log, try next candidate
 *
 * Deviations from legacy already in the §6 ledger:
 *   D1  identity is PROJECT_VER (X-Device-Firmware-Version) not sketch-MD5
 *   D4  certs.ar / updateFiles() / FILE_UPDATE_URL not ported
 *   D7  "Updating Firmware" / "Updated Firmware Success" screens are
 *       value-copied DISP_TEXT_CENTER requests on disp_queue (was direct
 *       showTextOnScreenCenter); same intent, queue-serialized.
 */
#include "ota_update.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_task_wdt.h"
#include "esp_http_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "store.h"
#include "endpoint_fallback.h"
#include "eink_display.h"
#include "events.h"
#include "pinned_tls.h"
#include "esp_crt_bundle.h"

static const char *TAG = "ota";

/* Everything below the throttle macros is only reachable from the OTA-enabled
 * code path; guard the helpers so the OTA-disabled build (the legacy
 * "#ifndef ENABLE_OTA -> return" stub) compiles without unused-function
 * warnings. */
#ifdef CONFIG_EINKTODO_ENABLE_OTA

/* Legacy parity: lib/update/UpdateFirmware.h checkInterval. */
#ifdef CONFIG_EINKTODO_DEBUG_OTA
#define OTA_CHECK_INTERVAL_S 60
#else
#define OTA_CHECK_INTERVAL_S (10 * 24 * 60 * 60)
#endif

#define OTA_HTTP_TIMEOUT_MS   60000 /* legacy 60 s socket timeout (item 31)   */
#define OTA_WRITE_CHUNK       1024  /* heap is free during download           */
#define KEY_FW_LAST_CHECK     "fw_last_check" /* pvalue i32 epoch (D9)         */

/* Post a centred-text screen (value-copied; G7). Non-blocking enqueue. */
static void show_text_center(const char *text)
{
    display_req_t req;
    memset(&req, 0, sizeof(req));
    req.kind = DISP_TEXT_CENTER;
    strncpy(req.text, text, sizeof(req.text) - 1);
    req.text[sizeof(req.text) - 1] = '\0';
    display_request(&req, pdMS_TO_TICKS(100));
}

/* Wire TLS trust for one candidate onto the http client config, mirroring the
 * fetch path: leaf-pin for fallbacks, curated CA bundle (tier 1) or esp-tls
 * global CA store (tier 2) for the primary, nothing for the debug-insecure
 * knob (D6). */
static void configure_tls(esp_http_client_config_t *cfg,
                          const endpoint_candidate_t *c)
{
#if defined(CONFIG_EINKTODO_TLS_INSECURE)
    (void)c;
    /* D6 debug-only: no CA material attached => server cert unverified. */
    cfg->skip_cert_common_name_check = true;
    ESP_LOGW(TAG, "TLS verification DISABLED (CONFIG_EINKTODO_TLS_INSECURE)");
#else
    if (c->use_pin && c->pin_len > 0) {
        pinned_tls_set(c->pin, c->pin_len);
        cfg->crt_bundle_attach = pinned_tls_attach;
    } else {
#if defined(CONFIG_EINKTODO_TLS_TIER2)
        cfg->use_global_ca_store = true; /* loaded once at boot */
#else
        cfg->crt_bundle_attach = esp_crt_bundle_attach;
#endif
    }
#endif
}

/* Stream the 200 body into the next OTA slot and reboot on success.
 * Returns ESP_OK only when no flash happened (handled by caller as "move on");
 * on a successful flash it esp_restart()s and never returns. */
static esp_err_t download_and_flash(esp_http_client_handle_t client,
                                    int content_length)
{
    const esp_partition_t *update_part;
    esp_ota_handle_t ota_handle = 0;
    char *buf;
    int written = 0;
    int progress = -1;
    esp_err_t err;

    update_part = esp_ota_get_next_update_partition(NULL);
    if (update_part == NULL) {
        ESP_LOGE(TAG, "no OTA partition available");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "writing to partition '%s' at 0x%x (content-length=%d)",
             update_part->label, update_part->address, content_length);

    /* onStart parity: screen up before the write loop. */
    show_text_center("Updating Firmware");

    err = esp_ota_begin(update_part, OTA_SIZE_UNKNOWN, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    buf = malloc(OTA_WRITE_CHUNK);
    if (buf == NULL) {
        ESP_LOGE(TAG, "OTA buffer alloc failed");
        esp_ota_end(ota_handle);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Update Firmware Progress: 0%%");
    for (;;) {
        int r = esp_http_client_read(client, buf, OTA_WRITE_CHUNK);
        if (r < 0) {
            ESP_LOGE(TAG, "http read error after %d bytes", written);
            free(buf);
            esp_ota_end(ota_handle);
            return ESP_FAIL;
        }
        if (r == 0) {
            break; /* end of stream */
        }
        err = esp_ota_write(ota_handle, buf, r);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
            free(buf);
            esp_ota_end(ota_handle);
            return err;
        }
        written += r;

        /* %-on-change progress log (legacy onProgress). */
        if (content_length > 0) {
            int newp = (int)(((int64_t)written * 100) / content_length);
            if (newp != progress) {
                progress = newp;
                ESP_LOGI(TAG, "Update Firmware Progress: %d%%, %d/%d bytes",
                         progress, written, content_length);
            }
        }
        /* R13: belt-and-braces WDT feed in the write loop. */
        esp_task_wdt_reset();
    }
    free(buf);

    /* Length sanity: a short body must not be flashed (parity with the
     * legacy contract that the server always sets Content-Length). */
    if (content_length > 0 && written != content_length) {
        ESP_LOGE(TAG, "truncated firmware: got %d of %d bytes, aborting",
                 written, content_length);
        esp_ota_end(ota_handle);
        return ESP_FAIL;
    }

    /* esp_ota_end validates the image header/magic — this is what rejects an
     * Arduino .bin pointed here by mistake (R9 defense in depth). */
    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end (image validation) failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    err = esp_ota_set_boot_partition(update_part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Update Firmware End: %d bytes flashed, rebooting", written);
    show_text_center("Updated Firmware Success");
    vTaskDelay(pdMS_TO_TICKS(2000)); /* onEnd parity */
    esp_restart();
    return ESP_OK; /* unreachable */
}

/* Try a single endpoint. Returns true when this candidate answered with a
 * reachable HTTP code (caller stops walking and persists it). */
static bool try_candidate(const endpoint_candidate_t *c)
{
    esp_http_client_config_t cfg;
    esp_http_client_handle_t client;
    const esp_app_desc_t *desc;
    int status;
    int content_length;
    esp_err_t err;
    bool reachable = false;

    memset(&cfg, 0, sizeof(cfg));
    cfg.url = c->url;
    cfg.timeout_ms = OTA_HTTP_TIMEOUT_MS;
    cfg.transport_type = HTTP_TRANSPORT_OVER_SSL;
    cfg.buffer_size = 1024;
    configure_tls(&cfg, c);

    ESP_LOGI(TAG, "GET %s", c->url);
    client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "http client init failed for %s", c->id);
        return false;
    }

    /* X-Device-Firmware-Version: PROJECT_VER (replaces sketch-MD5, D1).
     * Server returns 304 when this matches the pinned rtos-v* release. */
    desc = esp_ota_get_app_description();
    if (desc != NULL && desc->version[0] != '\0') {
        esp_http_client_set_header(client, "X-Device-Firmware-Version",
                                   desc->version);
    }

    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "open failed for %s: %s", c->id, esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    content_length = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP %d, Content-Length: %d", status, content_length);

    if (status == 304) {
        ESP_LOGI(TAG, "UPDATE Firmware NO_UPDATES");
        reachable = true;
    } else if (status == 200) {
        if (content_length <= 0) {
            /* Legacy "Content-Length <= 0 => try next candidate" guard (R6). */
            ESP_LOGW(TAG, "200 without Content-Length, skipping candidate");
        } else {
            /* download_and_flash() esp_restart()s on success and never
             * returns; any return is a failure for THIS candidate. */
            err = download_and_flash(client, content_length);
            ESP_LOGE(TAG, "firmware flash aborted: %s", esp_err_to_name(err));
            /* The body is consumed/in error; do not retry other candidates
             * with a half-flashed slot — the slot stays unbooted (otadata
             * untouched until set_boot_partition). Treat as reachable to stop
             * the walk; next 10-day cycle retries cleanly. */
            reachable = true;
        }
    } else {
        ESP_LOGW(TAG, "unexpected HTTP %d for %s", status, c->id);
    }

    esp_http_client_cleanup(client);
    return reachable;
}

#endif /* CONFIG_EINKTODO_ENABLE_OTA (helper block) */

esp_err_t ota_update_check(const endpoint_candidate_t *cands, int n, bool force)
{
#ifndef CONFIG_EINKTODO_ENABLE_OTA
    (void)cands;
    (void)n;
    (void)force;
    ESP_LOGD(TAG, "OTA is disabled");
    return ESP_OK;
#else
    int32_t last_check = 0;
    int32_t now = (int32_t)time(NULL);

    if (cands == NULL || n <= 0) {
        ESP_LOGW(TAG, "no OTA candidates");
        return ESP_OK;
    }

    /* Throttle (legacy lastFirmwareCheck): 10 d / 60 s debug. */
    if (!force) {
        store_pvalue_get_i32(KEY_FW_LAST_CHECK, &last_check, 0);
        int32_t diff = now - last_check;
        if (diff < OTA_CHECK_INTERVAL_S) {
            ESP_LOGI(TAG, "Skip update Firmware, will check after %d hours",
                     (OTA_CHECK_INTERVAL_S - diff) / 3600);
            return ESP_OK;
        }
    }

    ESP_LOGI(TAG, "Update Firmware Start");
    /* Legacy persists the check time BEFORE the attempt (even on failure) so a
     * permanently failing endpoint doesn't hammer the server every cycle. */
    store_pvalue_set_i32(KEY_FW_LAST_CHECK, now);

    for (int i = 0; i < n; i++) {
        if (try_candidate(&cands[i])) {
            endpoint_remember_success(&cands[i]);
            ESP_LOGI(TAG, "Update Firmware End");
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "all OTA candidates failed");
    ESP_LOGI(TAG, "Update Firmware End");
    return ESP_OK;
#endif /* CONFIG_EINKTODO_ENABLE_OTA */
}
