/* sntp_clock.c — SNTP init, legacy src/clock.h setClock() parity (item 26).
 *
 * Servers and TZ are binding (architecture §2):
 *   time.asia.apple.com / cn.pool.ntp.org / pool.ntp.org, TZ=CST-8
 * (legacy configTime offset 8*3600 s, DST 0). The legacy 100x1 s blocking
 * wait is replaced by the sync-notification callback setting BIT_TIME_SYNCED.
 */
#include "sntp_clock.h"

#include <stdlib.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_sntp.h" /* lwip wrapper: lwip/apps/sntp.h + esp sync-cb API */

#include "events.h" /* sys_events / BIT_TIME_SYNCED (../../main, header-only) */

/* Three servers are binding; the SDK default is 1 — sdkconfig.defaults must
 * carry CONFIG_LWIP_DHCP_MAX_NTP_SERVERS=3 (architecture §2 sntp_clock:
 * "verify CONFIG_LWIP_SNTP_MAX_SERVERS >= 3"; the v3.4 Kconfig symbol is
 * LWIP_DHCP_MAX_NTP_SERVERS). */
#if CONFIG_LWIP_DHCP_MAX_NTP_SERVERS < 3
#error "CONFIG_LWIP_DHCP_MAX_NTP_SERVERS must be >= 3 (see sdkconfig.defaults)"
#endif

static const char *TAG = "sntp_clock";

/* Runs in the lwIP/tcpip task context — keep it tiny: set the bit, log. */
static void time_sync_cb(struct timeval *tv)
{
    if (sys_events != NULL) {
        xEventGroupSetBits(sys_events, BIT_TIME_SYNCED);
    }

    /* Legacy: Serial.println("Current time: " + asctime(gmtime(...))). */
    time_t now = tv->tv_sec;
    struct tm timeinfo;
    char buf[26];
    gmtime_r(&now, &timeinfo);
    asctime_r(&timeinfo, buf);
    ESP_LOGI(TAG, "Current time: %s", buf);
}

void sntp_clock_start(void)
{
    /* TZ=CST-8 == legacy configTime(8*3600, 0, ...) (UTC+8, no DST). */
    setenv("TZ", "CST-8", 1);
    tzset();

    /* Arduino configTime() stops a running SNTP session before re-init —
     * mirror that so reconnects don't double-start (sntp_stop() is a no-op
     * when not started). */
    sntp_stop();

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    /* lwIP keeps the pointers — string literals only. */
    sntp_setservername(0, "time.asia.apple.com");
    sntp_setservername(1, "cn.pool.ntp.org");
    sntp_setservername(2, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(time_sync_cb);
    sntp_init();

    /* Legacy printed "Waiting for time" then block-polled; we return and let
     * BIT_TIME_SYNCED report completion. */
    ESP_LOGI(TAG, "Waiting for time");
}
