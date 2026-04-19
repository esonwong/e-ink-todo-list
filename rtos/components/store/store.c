/* store.c — NVS-backed settings, mirror of legacy src/store.h (items 45-49).
 *
 * See include/store.h for the namespace / key map. Legacy LittleFS JSON files
 * become two NVS namespaces; the per-operation LittleFS.begin()/end() churn
 * becomes a single nvs_flash_init() at boot (item 47).
 */
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "store.h"

static const char *TAG = "store";

#define NS_SETTING   "setting"
#define NS_PVALUE    "pvalue"
#define KEY_API_KEY  "api_key"

/* Legacy Setting.apiKey was char[41] -> max 41 chars + NUL = 42 (matches the
 * char[42] buffers in events.h message payloads). */
#define API_KEY_BUF_LEN 42

/* Local bounded copy (always NUL-terminates; truncates like legacy strlcpy). */
static void store_strcpy_bounded(char *dst, const char *src, size_t n)
{
    if (n == 0) {
        return;
    }
    snprintf(dst, n, "%s", src);
}

/* ------------------------------------------------------------------ init */

esp_err_t store_init(void)
{
    ESP_LOGI(TAG, "Init store");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES
#ifdef ESP_ERR_NVS_NEW_VERSION_FOUND /* not defined in ESP8266_RTOS_SDK v3.4 */
        || err == ESP_ERR_NVS_NEW_VERSION_FOUND
#endif
    ) {
        /* Mirror of legacy formatLittleFS() recovery (item 6). */
        ESP_LOGW(TAG, "NVS init failed (%s), erasing and retrying",
                 esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed (%s)", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed (%s)", esp_err_to_name(err));
    }
    return err;
}

/* --------------------------------------------------------------- setting */

esp_err_t store_get_api_key(char out[42])
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    out[0] = '\0';

#ifdef CONFIG_EINKTODO_API_KEY
    /* Dev-only preset, parity with legacy `#ifdef API_KEY` override in
     * initStore(): overrides the stored value in RAM only, never persisted. */
    if (CONFIG_EINKTODO_API_KEY[0] != '\0') {
        store_strcpy_bounded(out, CONFIG_EINKTODO_API_KEY, API_KEY_BUF_LEN);
        ESP_LOGW(TAG, "Using Kconfig preset API key (development only)");
        return ESP_OK;
    }
#endif

    nvs_handle h;
    esp_err_t err = nvs_open(NS_SETTING, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        /* Never saved — legacy loadSetting() default: empty string. */
        return ESP_OK;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can not open setting namespace (%s)", esp_err_to_name(err));
        return err;
    }

    size_t len = API_KEY_BUF_LEN;
    err = nvs_get_str(h, KEY_API_KEY, out, &len);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        out[0] = '\0';
        return ESP_OK;
    }
    if (err != ESP_OK) {
        out[0] = '\0';
        ESP_LOGE(TAG, "Can not read api_key (%s)", esp_err_to_name(err));
    }
    return err;
}

esp_err_t store_set_api_key(const char *key)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Save setting");

    /* Legacy strlcpy into char[41]: silently truncate over-long input. */
    char buf[API_KEY_BUF_LEN];
    if (strlen(key) >= API_KEY_BUF_LEN) {
        ESP_LOGW(TAG, "API key longer than %d chars, truncating",
                 API_KEY_BUF_LEN - 1);
    }
    store_strcpy_bounded(buf, key, sizeof(buf));

    nvs_handle h;
    esp_err_t err = nvs_open(NS_SETTING, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Can not open setting namespace for writing (%s)",
                 esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(h, KEY_API_KEY, buf);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Save setting done");
    } else {
        ESP_LOGE(TAG, "Save setting failed (%s)", esp_err_to_name(err));
    }
    return err;
}

/* ---------------------------------------------------------------- pvalue */

static esp_err_t pvalue_open(nvs_open_mode mode, nvs_handle *h)
{
    esp_err_t err = nvs_open(NS_PVALUE, mode, h);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Can not open pvalue namespace (%s)", esp_err_to_name(err));
    }
    return err;
}

/* Called after every successful set, mirroring the legacy
 * DEBUG_PERSISTENT_STORAGE "print file content after save" behavior. */
static void pvalue_debug_dump_after_save(void)
{
#ifdef CONFIG_EINKTODO_DEBUG_STORE
    store_dump();
#endif
}

esp_err_t store_pvalue_get_str(const char *k, char *out, size_t n, const char *dflt)
{
    if (k == NULL || out == NULL || n == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    store_strcpy_bounded(out, (dflt != NULL) ? dflt : "", n);

    nvs_handle h;
    esp_err_t err = pvalue_open(NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK; /* namespace never written — default, parity */
    }
    if (err != ESP_OK) {
        return err;
    }

    size_t len = n;
    err = nvs_get_str(h, k, out, &len);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        store_strcpy_bounded(out, (dflt != NULL) ? dflt : "", n);
        return ESP_OK; /* key never written — default, parity */
    }
    if (err != ESP_OK) {
        store_strcpy_bounded(out, (dflt != NULL) ? dflt : "", n);
        ESP_LOGE(TAG, "Can not read persistent value %s (%s)", k,
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t store_pvalue_set_str(const char *k, const char *v)
{
    if (k == NULL || v == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle h;
    esp_err_t err = pvalue_open(NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(h, k, v);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Save persistent value %s = %s", k, v);
        pvalue_debug_dump_after_save();
    } else {
        ESP_LOGE(TAG, "Save persistent value %s failed (%s)", k,
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t store_pvalue_get_i32(const char *k, int32_t *out, int32_t dflt)
{
    if (k == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = dflt;

    nvs_handle h;
    esp_err_t err = pvalue_open(NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_i32(h, k, out);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = dflt;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        *out = dflt;
        ESP_LOGE(TAG, "Can not read persistent value %s (%s)", k,
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t store_pvalue_set_i32(const char *k, int32_t v)
{
    if (k == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle h;
    esp_err_t err = pvalue_open(NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i32(h, k, v);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Save persistent value %s = %d", k, v);
        pvalue_debug_dump_after_save();
    } else {
        ESP_LOGE(TAG, "Save persistent value %s failed (%s)", k,
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t store_pvalue_get_u8(const char *k, uint8_t *out, uint8_t dflt)
{
    if (k == NULL || out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = dflt;

    nvs_handle h;
    esp_err_t err = pvalue_open(NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_get_u8(h, k, out);
    nvs_close(h);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        *out = dflt;
        return ESP_OK;
    }
    if (err != ESP_OK) {
        *out = dflt;
        ESP_LOGE(TAG, "Can not read persistent value %s (%s)", k,
                 esp_err_to_name(err));
    }
    return err;
}

esp_err_t store_pvalue_set_u8(const char *k, uint8_t v)
{
    if (k == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle h;
    esp_err_t err = pvalue_open(NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(h, k, v);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Save persistent value %s = %u", k, (unsigned)v);
        pvalue_debug_dump_after_save();
    } else {
        ESP_LOGE(TAG, "Save persistent value %s failed (%s)", k,
                 esp_err_to_name(err));
    }
    return err;
}

/* ----------------------------------------------------------------- erase */

esp_err_t store_erase_pvalue(void)
{
    /* Mirror of legacy removePersistentValue(): wipes the pvalue namespace
     * ONLY — the setting namespace (api_key) survives (R8 gesture parity). */
    ESP_LOGI(TAG, "Remove persistent values");

    nvs_handle h;
    esp_err_t err = nvs_open(NS_PVALUE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Remove persistent values failed (%s)", esp_err_to_name(err));
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Remove persistent values done");
    } else {
        ESP_LOGE(TAG, "Remove persistent values failed (%s)", esp_err_to_name(err));
    }
    return err;
}

/* ------------------------------------------------------------------ dump */

#ifdef CONFIG_EINKTODO_DEBUG_STORE

/* ESP8266_RTOS_SDK v3.4 has no nvs_entry_find() iterator API (that arrived
 * in IDF >= 4.x), so the dump walks the closed set of keys this firmware
 * uses instead — same observable content as the legacy "print file content"
 * dump of persistentValue.json. */
typedef enum { DUMP_STR, DUMP_I32, DUMP_U8 } dump_type_t;

static const struct {
    const char *ns;
    const char *key;
    dump_type_t type;
} s_dump_keys[] = {
    { NS_SETTING, KEY_API_KEY,     DUMP_STR },
    { NS_PVALUE,  "net_lg_ep",     DUMP_STR },
    { NS_PVALUE,  "net_lg_pin",    DUMP_U8  },
    { NS_PVALUE,  "fw_last_check", DUMP_I32 },
    { NS_PVALUE,  "pcb_test",      DUMP_STR },
    /* Legacy DEBUG_PERSISTENT_STORAGE exercise keys. */
    { NS_PVALUE,  "text",          DUMP_STR },
    { NS_PVALUE,  "number",        DUMP_I32 },
    { NS_PVALUE,  "bool",          DUMP_U8  },
};

static void dump_one(nvs_handle h, const char *ns, const char *key,
                     dump_type_t type)
{
    esp_err_t err;
    switch (type) {
    case DUMP_STR: {
        char buf[64];
        size_t len = sizeof(buf);
        err = nvs_get_str(h, key, buf, &len);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "  %s/%s = \"%s\"", ns, key, buf);
            return;
        }
        break;
    }
    case DUMP_I32: {
        int32_t v;
        err = nvs_get_i32(h, key, &v);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "  %s/%s = %d", ns, key, v);
            return;
        }
        break;
    }
    case DUMP_U8:
    default: {
        uint8_t v;
        err = nvs_get_u8(h, key, &v);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "  %s/%s = %u", ns, key, (unsigned)v);
            return;
        }
        break;
    }
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "  %s/%s = (unset)", ns, key);
    } else {
        ESP_LOGI(TAG, "  %s/%s = (error: %s)", ns, key, esp_err_to_name(err));
    }
}

void store_dump(void)
{
    ESP_LOGI(TAG, "Store content:");

    const char *namespaces[] = { NS_SETTING, NS_PVALUE };
    for (size_t n = 0; n < sizeof(namespaces) / sizeof(namespaces[0]); n++) {
        nvs_handle h;
        esp_err_t err = nvs_open(namespaces[n], NVS_READONLY, &h);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "  %s/* = (namespace empty)", namespaces[n]);
            continue;
        }
        if (err != ESP_OK) {
            ESP_LOGI(TAG, "  %s/* = (error: %s)", namespaces[n],
                     esp_err_to_name(err));
            continue;
        }
        for (size_t i = 0; i < sizeof(s_dump_keys) / sizeof(s_dump_keys[0]); i++) {
            if (strcmp(s_dump_keys[i].ns, namespaces[n]) == 0) {
                dump_one(h, s_dump_keys[i].ns, s_dump_keys[i].key,
                         s_dump_keys[i].type);
            }
        }
        nvs_close(h);
    }
}

#else /* !CONFIG_EINKTODO_DEBUG_STORE */

void store_dump(void)
{
    /* No-op unless CONFIG_EINKTODO_DEBUG_STORE (legacy DEBUG_PERSISTENT_STORAGE). */
}

#endif /* CONFIG_EINKTODO_DEBUG_STORE */
