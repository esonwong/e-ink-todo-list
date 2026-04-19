/* sntp_clock — SNTP init + setClock() semantics (legacy src/clock.h, item 26).
 *
 * Legacy: configTime(8*3600, 0, "time.asia.apple.com", "cn.pool.ntp.org",
 * "pool.ntp.org") followed by a 100x1 s blocking poll for the epoch to pass
 * 8*3600*2. RTOS: same servers + TZ=CST-8, non-blocking; the sync
 * notification callback sets BIT_TIME_SYNCED on sys_events instead of the
 * blocking poll (architecture §2 sntp_clock; pre-sync epoch tolerated as
 * today).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Idempotent: safe to call on every (re)connect, mirroring the legacy
 * setClock() call from the WiFi-connected path. Stops a previous SNTP
 * session first (Arduino configTime() restart semantics). */
void sntp_clock_start(void);

#ifdef __cplusplus
}
#endif
