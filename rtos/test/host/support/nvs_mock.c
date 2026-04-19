/* In-memory NVS mock for host tests (single namespace, str + u8 entries). */
#include "nvs.h"

#include <string.h>

#define MOCK_MAX_ENTRIES 16
#define MOCK_KEY_LEN     16
#define MOCK_STR_LEN     64

typedef struct {
    char    key[MOCK_KEY_LEN];
    int     is_str;
    char    str[MOCK_STR_LEN];
    uint8_t u8;
    int     used;
} mock_entry_t;

static mock_entry_t s_entries[MOCK_MAX_ENTRIES];
static int s_commit_count;
static int s_fail_open;

void mock_nvs_reset(void)
{
    memset(s_entries, 0, sizeof(s_entries));
    s_commit_count = 0;
    s_fail_open = 0;
}

int mock_nvs_commit_count(void)
{
    return s_commit_count;
}

void mock_nvs_fail_open(int fail)
{
    s_fail_open = fail;
}

static mock_entry_t *find_entry(const char *key)
{
    for (int i = 0; i < MOCK_MAX_ENTRIES; i++) {
        if (s_entries[i].used && strcmp(s_entries[i].key, key) == 0) {
            return &s_entries[i];
        }
    }
    return NULL;
}

static mock_entry_t *find_or_create(const char *key)
{
    mock_entry_t *e = find_entry(key);
    if (e != NULL) {
        return e;
    }
    for (int i = 0; i < MOCK_MAX_ENTRIES; i++) {
        if (!s_entries[i].used) {
            memset(&s_entries[i], 0, sizeof(s_entries[i]));
            strncpy(s_entries[i].key, key, MOCK_KEY_LEN - 1);
            s_entries[i].used = 1;
            return &s_entries[i];
        }
    }
    return NULL;
}

esp_err_t nvs_open(const char *name, nvs_open_mode open_mode, nvs_handle *out_handle)
{
    (void)name;
    (void)open_mode;
    if (s_fail_open) {
        return ESP_FAIL;
    }
    *out_handle = 1;
    return ESP_OK;
}

esp_err_t nvs_get_str(nvs_handle handle, const char *key, char *out_value, size_t *length)
{
    (void)handle;
    mock_entry_t *e = find_entry(key);
    if (e == NULL || !e->is_str) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    size_t need = strlen(e->str) + 1;
    if (out_value == NULL) {
        *length = need;
        return ESP_OK;
    }
    if (*length < need) {
        *length = need;
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    memcpy(out_value, e->str, need);
    *length = need;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle handle, const char *key, const char *value)
{
    (void)handle;
    if (strlen(value) >= MOCK_STR_LEN) {
        return ESP_FAIL;
    }
    mock_entry_t *e = find_or_create(key);
    if (e == NULL) {
        return ESP_FAIL;
    }
    e->is_str = 1;
    strcpy(e->str, value);
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle handle, const char *key, uint8_t *out_value)
{
    (void)handle;
    mock_entry_t *e = find_entry(key);
    if (e == NULL || e->is_str) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *out_value = e->u8;
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle handle, const char *key, uint8_t value)
{
    (void)handle;
    mock_entry_t *e = find_or_create(key);
    if (e == NULL) {
        return ESP_FAIL;
    }
    e->is_str = 0;
    e->u8 = value;
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle handle)
{
    (void)handle;
    s_commit_count++;
    return ESP_OK;
}

void nvs_close(nvs_handle handle)
{
    (void)handle;
}
