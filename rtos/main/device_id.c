/* device_id.c — chip identity derived from the factory MAC (item 4, R16).
 *
 * The ESP8266 Arduino core's ESP.getChipId() returns
 * system_get_chip_id() == (mac[3]<<16) | (mac[4]<<8) | mac[5] of the factory
 * (station) MAC. esp_efuse_mac_get_default() yields that same factory MAC in
 * the RTOS SDK, so the value is bit-identical — verified on the bench unit
 * before any server-visible run (bring-up gate 2).
 */
#include "device_id.h"

#include <stdbool.h>
#include <stdio.h>

#include "esp_system.h"

static bool s_init;
static uint32_t s_id;
static char s_id_str[11];     /* max "4294967295" + NUL */
static char s_hostname[28];   /* "e-ink-todo-list-" + 6+ hex + NUL */

static void ensure_init(void)
{
    if (s_init) {
        return;
    }

    uint8_t mac[6] = { 0 };
    esp_efuse_mac_get_default(mac);
    s_id = ((uint32_t)mac[3] << 16) | ((uint32_t)mac[4] << 8) | (uint32_t)mac[5];

    /* Legacy: DeviceID = String(ESP.getChipId()) — decimal. */
    snprintf(s_id_str, sizeof(s_id_str), "%u", (unsigned)s_id);
    /* Legacy: "e-ink-todo-list-" + String(chipId, HEX) — lowercase hex. */
    snprintf(s_hostname, sizeof(s_hostname), "e-ink-todo-list-%x", (unsigned)s_id);

    s_init = true;
}

uint32_t device_id_get(void)
{
    ensure_init();
    return s_id;
}

const char *device_id_str(void)
{
    ensure_init();
    return s_id_str;
}

const char *device_id_hostname(void)
{
    ensure_init();
    return s_hostname;
}
