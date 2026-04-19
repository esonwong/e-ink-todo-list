/* Host-test stand-in for the v3.4 nvs.h surface used by endpoint_fallback
 * (v3.4 spells the types nvs_handle / nvs_open_mode — no _t suffix).
 * Backing store is an in-memory table in nvs_mock.c with test hooks. */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int32_t esp_err_t;

#define ESP_OK                     0
#define ESP_FAIL                   (-1)
#define ESP_ERR_NVS_BASE           0x1100
#define ESP_ERR_NVS_NOT_FOUND      (ESP_ERR_NVS_BASE + 0x02)
#define ESP_ERR_NVS_INVALID_LENGTH (ESP_ERR_NVS_BASE + 0x04)

typedef uint32_t nvs_handle;

typedef enum {
    NVS_READONLY,
    NVS_READWRITE
} nvs_open_mode;

esp_err_t nvs_open(const char *name, nvs_open_mode open_mode, nvs_handle *out_handle);
esp_err_t nvs_get_str(nvs_handle handle, const char *key, char *out_value, size_t *length);
esp_err_t nvs_set_str(nvs_handle handle, const char *key, const char *value);
esp_err_t nvs_get_u8(nvs_handle handle, const char *key, uint8_t *out_value);
esp_err_t nvs_set_u8(nvs_handle handle, const char *key, uint8_t value);
esp_err_t nvs_commit(nvs_handle handle);
void      nvs_close(nvs_handle handle);

/* ---- test hooks (mock only) ---- */
void mock_nvs_reset(void);              /* wipe the in-memory store */
int  mock_nvs_commit_count(void);       /* number of nvs_commit() calls */
void mock_nvs_fail_open(int fail);      /* force nvs_open to return ESP_FAIL */

#ifdef __cplusplus
}
#endif
