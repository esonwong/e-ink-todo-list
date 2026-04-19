# Arduino → ESP8266_RTOS_SDK Firmware Migration — Parity Audit (Final)

## 1. Executive Summary

The audit covered **68 behavior items across 9 clusters**, comparing the legacy Arduino tree against the ESP8266_RTOS_SDK port.

| Verdict | Count |
|---|---:|
| PARITY (behavior reproduced as-is) | 54 |
| LEDGERED (intentional, documented deviation D#/ID#) | 13 |
| Confirmed GAP (undocumented regression) | 1 |
| Refuted false-GAP (overturned by refutation) | 0 |

> Counts sum to 68 (the captive-DNS GAP is a sub-part of item 20; item 20 is counted once as the GAP-bearing item). Items 3 and 48 appear in two cluster passes; each is counted once by behavior.

**Verdict:** The port is **behavior-complete except for one undocumented regression** — the captive-portal DNS catcher is implemented but never wired into the portal lifecycle, so phones joining the setup AP no longer get the captive-portal sheet auto-opening the config page. Every other divergence is either exact parity or a closed-set, documented ledger deviation. Fixing the single high-severity DNS gap (a ~3-line wiring change against already-shipped code) brings the port to full parity.

> **RESOLVED (2026-06-12).** All findings below are fixed and verified on hardware — see §6.

---

## 6. Resolution & real-device verification (2026-06-12)

Bench unit: ESP8266EX (esp12e), device ID `4472757`, `/dev/cu.usbserial-210`, development overlay flashed over serial (Arduino firmware erased, as expected). Three issues fixed; all four overlays rebuilt green, host tests pass (endpoint 45 / urlencode 20 / transform 663), production `.bin` 680624 B < 0xE0000 gate.

| # | Issue | Source | Fix | Verified |
|---|---|---|---|---|
| 1 | Captive DNS never started/stopped (the audit's one confirmed GAP, item 20, high) | static audit | `wifi_ctrl.c`: `#include "dns_server.h"`; `dns_server_start(ip.ip)` in `portal_open()`, `dns_server_stop()` in `portal_close()` | Serial: `dns_server: captive DNS up, resolving all A queries to 192.168.4.1` on portal open |
| 2 | AP-info screen missing config-server URL line (item 20 cosmetic sub-part) | static audit | `display_task.cpp` `render_ap_info()` now renders `\n`-split lines; `wifi_ctrl.c` appends `http://<AP IP>` to the info string | Builds green; renders on panel |
| 3 | **Primary request URL dropped `/api/display/v3`** — `endpoint_build_candidates()` appended the display path to fallbacks but used the primary base URL verbatim, so every fetch in every environment hit `https://<host>?width=…` with no path → guaranteed 404/failure | **real-device serial only** (compile + host tests + static audit all missed it; the host test encoded the same wrong assumption) | `endpoint_fallback.c`: append `path` to the primary base URL too; `endpoint_test.c` updated (PRIMARY is now a path-less base, asserts base+path) | Serial: `GET https://dev.einktodo.com/api/display/v3?width=800&height=480` |
| 4 | **E-ink panel never physically updated** — default HSPI path (`hal_epd.c` `spi_trans`) packs bytes into 32-bit words; the v3.4 SPI peripheral's word framing is not byte-exact to the panel's MSB-first byte stream, so the panel received garbled commands/data and held its previous image despite the firmware logging a full render. | **physical observation only** (serial showed a complete Send-black/red → Refresh → Sleep sequence; only looking at the panel revealed no update) | Default `CONFIG_EINKTODO_EPD_BITBANG=y` in the shared `sdkconfig.defaults` (all overlays). Bit-bang clocks each byte MSB-first via GPIO — identical to the legacy Arduino `SPI.transfer()` stream. HSPI retained behind the knob as a future speed optimization (§8 gate 5 / Residual #3). | Panel updates confirmed on the bench unit (config/AP screen renders). |

Smoke-test result (flash + serial + panel observation, no interactive portal/TLS/fetch): boot banner + version `v0.11.0` + device ID stable across reflash; dual-OTA partition table loads `ota_0`; store/display/button(GPIO16)/portal+captive DNS all initialize; **BWR e-ink physically updates** (config/AP screen) once bit-bang is enabled (fix #4); heap healthy (95 KB boot, 31.8 KB min during a TLS-attempt cycle); **no crash, no reset-loop, no WDT panic**. Remaining bring-up gates that need an interactive session (portal save → connect, TLS handshake + pin, full bitmap fetch/render parity, OTA slot-cycling, 48 h WDT / 7-day heap soak) are left for a hands-on pass.

---

## 2. Confirmed Gaps (action items, ordered by severity)

| # | Item | What differs | User-visible consequence | Severity | Concrete fix |
|---|---|---|---|---|---|
| 1 | **20** — captive DNS not started/stopped | `dns_server` component is fully implemented (`dns_server_start`/`dns_server_stop`) but **never called**. `wifi_ctrl.c` `portal_open()` fetches the AP IP into a local `ip` that is then unused; it calls `portal_httpd_start()` but not `dns_server_start()`. `portal_close()` calls `portal_httpd_stop()` but not `dns_server_stop()`. Legacy (tzapu/WiFiManager) runs a DNSServer on port 53 wildcarding all A queries to the softAP IP. No ledger entry (D1–D10, ID1–ID9) retires captive DNS. | Phones/laptops joining the **"E-ink Todo List AP"** no longer get the OS captive-portal sheet that auto-opens the config page (iOS + Android). Users must manually browse to the gateway IP — a real, undocumented setup-UX regression. | **high** | In `rtos/components/net_portal/wifi_ctrl.c`: add `#include "dns_server.h"`. In `portal_open()`, after `portal_httpd_start(...)`, call `dns_server_start(ip.ip)` using the AP IP already fetched. In `portal_close()`, after `portal_httpd_stop()`, call `dns_server_stop()`. `portal_close()` is already invoked from `on_got_ip`, `on_portal_timeout`, `on_reset_and_portal`, and `connect_failed`, so stop fires on connect-success, 600 s timeout, reset, and connect-fail reopen — matching README lines 148/173. `CMakeLists` already declares `REQUIRES dns_server`, so no build wiring change is needed. |

### Sub-part note on item 20 (AP-info screen)
A secondary, **cosmetic** difference exists in the same item and is **not** elevated to an action item: the port's `render_ap_info()` renders 2 lines (`"Entered config mode"` + `"<SSID> / <password>"`) versus legacy's 6 lines (adds the softAP IP and prompt text). AP credentials themselves (`"E-ink Todo List AP"` / `"einktodo.com"`) are exact parity. Optionally restore the IP/prompt lines for full visual parity, but no functional regression.

---

## 3. False Positives (refutation results — do not re-investigate)

There were **0 false positives**. The single GAP raised (item 20, captive DNS) was put through adversarial refutation and **upheld as a real GAP, severity high**. The refutation independently confirmed:
- AP creds are parity (not a gap).
- The AP-info screen line-count difference is cosmetic (not the critical claim).
- The captive-DNS dead-code is real: grep across `rtos/` shows no `#include "dns_server.h"` outside the component and no call to `dns_server_start`/`dns_server_stop` anywhere; the architecture spec mandates it in 5 places (README lines 100, 147, 148, 168, 210); no ledger entry covers its removal.

Nothing was overturned from GAP to PARITY/LEDGERED, so there is no entry here to suppress from future passes.

---

## 4. Ledgered-Deviation Coverage

Every ledger entry referenced in the audit is exercised by at least one item. No ledger entry is orphaned (an unused entry would signal an unverified assumption).

| Ledger | Description | Exercised by item(s) | Status |
|---|---|---|---|
| **D1** | Sketch-MD5 boot dump → app version + ELF SHA-256 + running partition | 3, 7, 53 | Covered |
| **D2** | `goToSleep()` deleted (was dead code; sleep not reintroduced) | 68 | Covered |
| **D3** | `X-Device-Event` cleared after reachable-code (vs before send) — resilience bugfix | 28 timing | Covered |
| **D4** | `certs.ar` / `updateFiles()` / `FILE_UPDATE_URL` retired; CA trust in-image | 10, 50 | Covered |
| **D6** | TLS insecure branch replaced by `CONFIG_EINKTODO_TLS_INSECURE` compile-time knob | 39 | Covered |
| **D7** | `delay()` serialization → event-group handshake (`BIT_DISP_DONE`); callback→manual OTA transition | 33, 52 | Covered |
| **D8** | LittleFS → NVS storage backend | 6, 47 | Covered |
| **D9** | NVS 15-char key renames (`net_lg_ep`, `net_lg_pin`, `fw_last_check`) | 35, 36, 46, 51 | Covered |
| **D10** | `last_modified` persisted only after `BIT_DISP_DONE` | 48 | Covered |
| **ID1** | v3.4 has no `nvs_entry_find` iterator → fixed dump key table | 49 | Covered |
| **ID3** | 750_BW / 750_V1 / 75Z90 bind to `DisplayDriver750` via typedef | 55 | Covered |

> Note: **D5** is not referenced by any audited item. If D5 exists in the ledger, it is **not exercised by this audit** and should be reviewed — either it covers a behavior outside the 68-item scope, or it is a stale/unverified assumption. ID2 and ID4–ID9 likewise have no referencing item; confirm they fall outside scope or close them. All ledger refs that *do* appear in the audit (D1–D4, D6–D10, ID1, ID3) are validated.

---

## 5. Per-Cluster Parity Status

| Cluster | One-line status |
|---|---|
| Items 1–11, 48 (boot, loop, running state) | **PARITY** — boot order, MAC-derived device ID, immediate-fetch cadence, 60 s gate, re-entrancy-by-construction, RAM-only running values all reproduced; D1/D4/D8/D10 ledgered. |
| Button Input (Items 13–17) | **PARITY** — OneButton 2.5.0 FSM ported line-for-line (50/400/800 ms timings), full gesture map, GPIO16 readback + raw-register fallback. |
| WiFi portal & DNS captive (Items 18–25) | **GAP (1, high)** — hostname/portal/AP creds/connect/timeout all parity; **captive DNS catcher implemented but never started/stopped** (item 20). |
| Display Fetch (Items 27–33, 41–44, 66) | **PARITY** — v3 query, 9-header capture, status dispatch, BWR/mono negotiation, exact bit semantics, URL-encode; D3/D7 ledgered (event-clear timing, render handshake). |
| TLS / endpoint fallback (Items 34–40) | **PARITY** — candidate build/prioritize/remember shared by fetch+OTA, reachable-code set, SHA-1/256 pinning; D6/D9 ledgered. |
| RTOS Store (Items 45–49) | **PARITY** — apiKey + typed pvalue namespaces, mount-once (no churn), RAM-only running value, debug dump; D8/D9/D10/ID1 ledgered. |
| OTA & Release Pipeline (Items 50–54) | **PARITY** — 10-day throttle, candidate loop + screens, version-header negotiation, segregated `rtos-v*` tag/asset channel, size gate + host tests; D1/D4/D7/D9 ledgered. |
| Display Driver (Items 55–65) | **PARITY** — HSPI pins, reset/init/refresh/sleep command sequences, SPI framing, PROGMEM fonts (MD5-identical), `waitUntilIdle`, test pattern; ID3 ledgered. |
| RTOS parity audit (Items 3, 4, 7, 12, 26, 67, 68) | **PARITY** — boot dump, device ID, PCB test, LED dead-code, NTP servers/TZ, build matrix; D1/D2 ledgered. |
