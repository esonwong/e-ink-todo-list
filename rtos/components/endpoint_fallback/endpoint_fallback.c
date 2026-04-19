/* endpoint_fallback.c — see include/endpoint_fallback.h.
 * Port of legacy src/fallbackNetwork.h (items 34–38).
 *
 * Persistence note: NVS is internally thread-safe; both keys live in the
 * "pvalue" namespace and are only ever written from cycle_task context
 * (todo_fetch_run / ota_update_check run on cycle_task), preserving the
 * task-level writer discipline of §1.
 */
#include "endpoint_fallback.h"

#include <string.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "endpoint";

#define PVALUE_NAMESPACE       "pvalue"
#define KEY_LAST_GOOD_ENDPOINT "net_lg_ep"  /* legacy network_last_good_endpoint (D9) */
#define KEY_LAST_GOOD_USES_PIN "net_lg_pin" /* legacy network_last_good_uses_fingerprint (D9) */

/* Bounded copy with silent truncation (id field is identity-by-prefix). */
static void copy_trunc(char *dst, size_t dst_len, const char *src)
{
    size_t i = 0;
    if (dst_len == 0) {
        return;
    }
    while (src[i] != '\0' && i < dst_len - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* Parse a leaf-cert pin: 40 hex digits = SHA-1 (20 bytes), 64 = SHA-256
 * (32 bytes). ':' separators tolerated (legacy FALLBACK_TLS_FINGERPRINT
 * values were colon-separated). Returns false on any other input. */
static bool parse_pin(const char *hex, uint8_t out[32], uint8_t *out_len)
{
    uint8_t bytes[32];
    size_t digits = 0;
    int hi = -1;

    if (hex == NULL) {
        return false;
    }
    for (const char *p = hex; *p != '\0'; p++) {
        if (*p == ':') {
            continue;
        }
        int nib = hex_nibble(*p);
        if (nib < 0) {
            return false;
        }
        if (digits >= 64) {
            return false;
        }
        if (hi < 0) {
            hi = nib;
        } else {
            bytes[digits / 2] = (uint8_t)((hi << 4) | nib);
            hi = -1;
        }
        digits++;
    }
    if (digits != 40 && digits != 64) {
        return false;
    }
    *out_len = (uint8_t)(digits / 2);
    memcpy(out, bytes, *out_len);
    return true;
}

/* Legacy appendFallbackCandidate: emit only when base URL and pin are both
 * valid (legacy checked non-empty; we additionally require the pin to parse
 * since mbedTLS needs the raw digest bytes). */
static int append_fallback(endpoint_candidate_t *out, int count,
                           const char *base, const char *pin_hex,
                           const char *path)
{
    endpoint_candidate_t *c;

    if (count >= ENDPOINT_MAX_CANDIDATES || base == NULL || base[0] == '\0') {
        return count;
    }
    if (pin_hex == NULL || pin_hex[0] == '\0') {
        return count; /* legacy isValidFallbackFingerprint: empty -> skip */
    }
    c = &out[count];
    if (!parse_pin(pin_hex, c->pin, &c->pin_len)) {
        ESP_LOGW(TAG, "fallback %s: invalid TLS pin (need 40/64 hex chars), skipped", base);
        return count;
    }
    if (strlen(base) + strlen(path) >= sizeof(c->url)) {
        ESP_LOGW(TAG, "fallback %s: url too long, skipped", base);
        return count;
    }
    strcpy(c->url, base);
    strcat(c->url, path);
    copy_trunc(c->id, sizeof(c->id), base); /* identity-by-prefix, 15 chars */
    c->use_pin = true;
    return count + 1;
}

int endpoint_build_candidates(const char *primary, const char *path,
                              endpoint_candidate_t out[3])
{
    int count = 0;

    if (out == NULL) {
        return 0;
    }
    memset(out, 0, sizeof(endpoint_candidate_t) * ENDPOINT_MAX_CANDIDATES);
    if (path == NULL) {
        path = "";
    }

    /* Primary: base URL (Kconfig EINKTODO_API_URL minus path) + display path,
     * mirroring how the fallbacks below append `path` to their base URL.
     * Legacy DEFAULT_API_URL carried the full path; here the path is appended so
     * the final request is <base>/api/display/v3?width=...&height=... (parity). */
    if (primary != NULL && primary[0] != '\0') {
        if (strlen(primary) + strlen(path) < sizeof(out[count].url)) {
            copy_trunc(out[count].id, sizeof(out[count].id), ENDPOINT_PRIMARY_ID);
            strcpy(out[count].url, primary);
            strcat(out[count].url, path);
            out[count].use_pin = false;
            out[count].pin_len = 0;
            count++;
        } else {
            ESP_LOGE(TAG, "primary url too long, skipped");
        }
    }

    count = append_fallback(out, count, CONFIG_EINKTODO_FALLBACK_URL_1,
                            CONFIG_EINKTODO_FALLBACK_PIN_1, path);
    count = append_fallback(out, count, CONFIG_EINKTODO_FALLBACK_URL_2,
                            CONFIG_EINKTODO_FALLBACK_PIN_2, path);
    return count;
}

void endpoint_prioritize_last_good(endpoint_candidate_t *c, int n)
{
    char last_good[sizeof(c->id)];
    nvs_handle h;

    if (c == NULL || n <= 0) {
        return;
    }

    /* Default "primary" — legacy getPersistentValue(key, "primary"). */
    copy_trunc(last_good, sizeof(last_good), ENDPOINT_PRIMARY_ID);
    if (nvs_open(PVALUE_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = sizeof(last_good);
        if (nvs_get_str(h, KEY_LAST_GOOD_ENDPOINT, last_good, &len) != ESP_OK) {
            copy_trunc(last_good, sizeof(last_good), ENDPOINT_PRIMARY_ID);
        }
        nvs_close(h);
    }

    for (int i = 0; i < n; i++) {
        if (strcmp(c[i].id, last_good) == 0) {
            if (i > 0) {
                endpoint_candidate_t selected = c[i]; /* value copy */
                for (int j = i; j > 0; j--) {
                    c[j] = c[j - 1];
                }
                c[0] = selected;
            }
            return;
        }
    }
}

void endpoint_remember_success(const endpoint_candidate_t *c)
{
    nvs_handle h;
    bool dirty = false;
    char cur_id[sizeof(c->id)];
    size_t cur_id_len = sizeof(cur_id);
    uint8_t cur_pin = 0xFF;

    if (c == NULL) {
        return;
    }
    if (nvs_open(PVALUE_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open(%s) failed, last-good endpoint not persisted",
                 PVALUE_NAMESPACE);
        return;
    }

    /* Read-compare-write to avoid pointless flash churn on every poll. */
    if (nvs_get_str(h, KEY_LAST_GOOD_ENDPOINT, cur_id, &cur_id_len) != ESP_OK ||
        strcmp(cur_id, c->id) != 0) {
        if (nvs_set_str(h, KEY_LAST_GOOD_ENDPOINT, c->id) == ESP_OK) {
            dirty = true;
        } else {
            ESP_LOGW(TAG, "nvs_set_str(%s) failed", KEY_LAST_GOOD_ENDPOINT);
        }
    }
    if (nvs_get_u8(h, KEY_LAST_GOOD_USES_PIN, &cur_pin) != ESP_OK ||
        cur_pin != (uint8_t)c->use_pin) {
        if (nvs_set_u8(h, KEY_LAST_GOOD_USES_PIN, (uint8_t)c->use_pin) == ESP_OK) {
            dirty = true;
        } else {
            ESP_LOGW(TAG, "nvs_set_u8(%s) failed", KEY_LAST_GOOD_USES_PIN);
        }
    }
    if (dirty) {
        nvs_commit(h);
        ESP_LOGI(TAG, "last-good endpoint -> %s (pin=%d)", c->id, (int)c->use_pin);
    }
    nvs_close(h);
}

bool endpoint_is_reachable_code(int http_code)
{
    /* Legacy isReachableHttpCode: OK / Not Modified / No Content / 401. */
    return http_code == 200 || http_code == 304 || http_code == 204 ||
           http_code == 401;
}
