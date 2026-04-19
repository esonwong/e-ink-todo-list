/* store.h — NVS-backed settings (mirror of legacy src/store.h, items 45-49).
 *
 * Storage is NVS only (no LittleFS port, deviation D8). Two namespaces keep
 * the legacy blast-radius semantics:
 *   - "setting" {api_key}        <- /setting.json  (survives every gesture)
 *   - "pvalue"  {generic keys}   <- /persistentValue.json (5-click /
 *                                    6 s-hold erase wipes ONLY this namespace)
 *
 * Canonical pvalue key names (NVS 15-char key limit forces the renames,
 * deviation D9):
 *   "net_lg_ep"      str  - last good endpoint id   (was network_last_good_endpoint)
 *   "net_lg_pin"     u8   - last good endpoint used TLS pin
 *   "fw_last_check"  i32  - epoch of last OTA check (was lastFirmwareCheck)
 *   "pcb_test"       str  - pcb-test build only
 *   "text"/"number"/"bool" - DEBUG_STORE exercise keys (legacy DEBUG_PERSISTENT_STORAGE)
 *
 * Thread-safety: NVS is internally thread-safe; the single-writer discipline
 * per namespace (G1: cycle_task is the only "setting" writer) is enforced by
 * the callers, not here.
 */
#ifndef EINKTODO_STORE_H
#define EINKTODO_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t store_init(void);                       /* nvs_flash_init + erase-retry recovery (item 6) */
esp_err_t store_get_api_key(char out[42]);        /* ns "setting", key "api_key" */
esp_err_t store_set_api_key(const char *key);     /* writer: cycle_task only (G1) */
esp_err_t store_pvalue_get_str(const char *k, char *out, size_t n, const char *dflt);
esp_err_t store_pvalue_set_str(const char *k, const char *v);
esp_err_t store_pvalue_get_i32(const char *k, int32_t *out, int32_t dflt);
esp_err_t store_pvalue_set_i32(const char *k, int32_t v);
esp_err_t store_pvalue_get_u8 (const char *k, uint8_t *out, uint8_t dflt);
esp_err_t store_pvalue_set_u8 (const char *k, uint8_t v);
esp_err_t store_erase_pvalue(void);               /* nvs_erase_all(pvalue) + commit; setting untouched */
void      store_dump(void);                       /* CONFIG_EINKTODO_DEBUG_STORE: dump stored values */

#ifdef __cplusplus
}
#endif

#endif /* EINKTODO_STORE_H */
