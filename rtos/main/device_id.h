/* device_id — device identity (legacy ESP.getChipId(), inventory item 4).
 *
 * R16 bench gate (binding): device_id_get() MUST equal the Arduino
 * ESP.getChipId() value on the same unit before any server-visible run.
 * The Arduino core's chip id is the low 3 bytes of the factory station MAC:
 * (mac[3]<<16) | (mac[4]<<8) | mac[5].
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Chip id, computed once from esp_efuse_mac_get_default(). */
uint32_t device_id_get(void);

/* Decimal string, parity with legacy `DeviceID = String(ESP.getChipId())`
 * — this exact form goes into the X-Device-Id request header.
 * Returns a static buffer; do not free. */
const char *device_id_str(void);

/* Hostname "e-ink-todo-list-%06x" (lowercase hex; legacy
 * "e-ink-todo-list-" + String(chipId, HEX)). Static buffer; do not free. */
const char *device_id_hostname(void);

#ifdef __cplusplus
}
#endif
