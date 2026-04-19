# ESP8266 → ESP32/FreeRTOS Migration Inventory

Source: parallel module analyses of `einktodo-device` (main.cpp, network.h, fallbackNetwork.h, todoList.h, button.h, clock.h, store.h, led.h, config.h, display.h, lib/display-driver/, lib/update/, lib/gpio16/, platformio.ini).

---

## 1. Feature parity checklist

Every behavior below must survive the port and is independently verifiable against the current code.

### Boot & identity

1. Serial init at **115200 baud** with a **2000 ms delay** before first output (`main.cpp` setup).
2. Launch screen rendered at boot; shows `GIT_VERSION` and a "DEBUG MODE" marker when `DEBUG` is defined.
3. `DEBUG` build logs: chip ID, Sketch MD5 (`ESP.getSketchMD5()`), LittleFS file listing, settings dump.
4. `DeviceID = String(chip id)` computed once at setup (`main.cpp:26`); used verbatim in the `X-Device-Id` request header and (hex form) in the WiFi hostname. **Identity continuity across the port must be decided explicitly** (see risk register #7).
5. Startup order: `initStore()` → `loadSetting()` → `loadRunningValue()` → display init → button init → WiFi manager init.
6. `initStore()` mounts LittleFS and calls `formatLittleFS()` (begin + format) if mount fails.
7. `ENABLE_PCB_TEST` mode: renders device info screen (Device ID, Sketch MD5, free heap, flash chip ID, WiFi SSID/IP/MAC, current time, a test persistent value) and blocks **5 × 60 × 1000 ms** before continuing.

### Main loop cadence

8. `loop()` runs `buttonLoop()` (`button.tick()`) and `wifiLoop()` (`wifiManager.process()`) on every iteration with no delay.
9. Every `DISPLAY_CHECK_INTERVAL_SECONDS` (**default 60**, build-flag overridable), gated on `time(nullptr) - runningValue.displayContentLastCheckTime` and the global `updating` flag: run `updateFiles()` → `updateFireWare()` → set `updating=true` → `downloadAndDrawTodo()` → `updating=false`.
10. `updating` boolean prevents concurrent/re-entrant fetches (also set by button single-click forced refresh).
11. `runningValue.displayContentLastCheckTime = time(nullptr)` is set at the **start** of `downloadAndDrawTodo()`.

### LED

12. `LED_PIN = 2` (config.h:10), `initLED()` sets OUTPUT; `turnOnLed()`/`turnOffLed()` write HIGH/LOW; `blinkLed(interval)` toggles via `millis() - lastBlinkTime >= interval`. Currently dead code (never called from main) — keep API compiling, do not wire to new behavior.

### Button gestures (OneButton, mathertel @ 2.5.0)

13. Single click: if `wifiManager.getConfigPortalActive()` → stop portal (+ clear display); else if `WiFi.status()==WL_CONNECTED` → force todo refresh (`cleanRunningValue()`, set `updating`, `downloadAndDrawTodo()`, clear `updating`); else → start config portal.
14. Double click: set `pendingDeviceEvent = "double-click"` — **raw event only**, forwarded as `X-Device-Event` header on next fetch; all business semantics stay server-side.
15. Long press: `buttonLongPressStart()` clears `longPressActionDone`; during press, when `button.getPressedMs() > 6000` and not yet done → set flag, `removePersistentValue()`, reset WiFi settings, start config portal. Fires exactly once per press.
16. Multi-click: **4 clicks → restart**, **5 clicks → `removePersistentValue()`**, **6 clicks → toggle WiFi**.
17. `BUTTON_PIN` is a build flag: 16 for production/preview/development, 0 for pcb-test. (GPIO16 special init path is ESP8266-only and is dropped — see risk register #5.)

### WiFi & config portal (WiFiManager, tzapu)

18. Hostname: `"e-ink-todo-list-" + <chip id in HEX>`.
19. Portal config: `setConfigPortalBlocking(false)`, `setConfigPortalTimeout(600)` s, `setConnectTimeout(30)` s, `setConnectRetries(3)`, menu `[wifi, info, restart, exit]`, portal title "E-ink Todo List".
20. AP credentials: SSID **"E-ink Todo List AP"**, password **"einktodo.com"** (`AP_SSID`/`AP_PASSWORD` in config.h). `configModeCallback` renders AP SSID, password, and config server IP on the e-ink screen.
21. Custom `WiFiManagerParameter` "apiKey" (**41 chars max**); `setSaveParamsCallback` persists it to `/setting.json` via `saveSetting()`. Compile-time `API_KEY` define can override for development.
22. `autoConnect()` path when credentials saved (`getWiFiIsSaved()`), with optional `WIFI_SSID`/`WIFI_PASS` compile-time preload in DEBUG builds.
23. `preSaveConfigCallback` sets `isTringToConnectWiFi = true`; `onWifiConnected()` clears it, shows "Connected to WiFi", calls `setClock()`.
24. `onWifiConnectFailed()` (portal active + `isTringToConnectWiFi`): stop portal, **delay 3000 ms**, restart portal.
25. `configPortalTimeoutCallback`: `WiFi.mode(WIFI_OFF)` + display "Press button to restart".

### Clock / NTP

26. `setClock()` on WiFi connect: `configTime(TZ_SEC=28800, DST_SEC=0, "time.asia.apple.com", "cn.pool.ntp.org", "pool.ntp.org")`; polls `time(nullptr)` up to **100 × delay(1000)** until epoch ≥ `8*3600*2`; uses `gmtime_r()`/UTC+8 semantics.

### Display content fetch (`downloadAndDrawTodo()`)

27. `GET {endpoint}/api/display/v3?width=<w>&height=<h>` (values url-encoded via `urlencode()`).
28. Request headers sent:
    - `If-Modified-Since` **and** `X-If-Modified-Since` = `runningValue.todoLastModified`
    - `Authorization: Bearer <apiKey>`
    - `X-Device-Id: <DeviceID>`
    - `X-Device-Colors: bwr` if `E_INK_750` else `bw` (`DEVICE_COLOR_SUPPORT` compile-time macro)
    - `X-Device-Firmware-Version: <GIT_VERSION>` (only if defined)
    - `X-Device-Event: <pendingDeviceEvent>` (only if non-empty; **cleared immediately after adding the header**, i.e. single-use per fetch)
29. Response headers collected (`collectHeaders`): `Content-Picture-Width`, `Content-Picture-Height`, `API-Version`, `Last-Modified` (fallback `X-Data-Modified`), `X-Bitmap-Layers`, `X-Black-Buffer-Length`, `X-Red-Buffer-Length`, `X-Device-Config-Last-Modified`.
30. Response code handling:
    - **304** → remember endpoint, return without touching display
    - **401** → `show401()` ("API Key authorization failed!"), remember endpoint, return
    - **204** → `showNoContent()` ("No Content"), remember endpoint, return
    - **200** → stream body, save `Last-Modified` into `runningValue.todoLastModified` (strcpy into char[30]), render, remember endpoint
    - other reachable codes → log + stop; connection-level failures → try next endpoint candidate
31. Timeouts: `client.setTimeout(60000)` **and** `https.setTimeout(60000)` (60 s).
32. Free heap logged before and after HTTPS setup (diagnostics — keep equivalent logging).
33. `delay(50)` between download completion and render start.

### Fallback endpoint system (fallbackNetwork.h)

34. `buildEndpointCandidates(primaryUrl, fallbackPath, maxCount=3)`: candidate[0] = primary (`DEFAULT_API_URL`, id `"primary"`, no fingerprint); then `FALLBACK_BASE_URL_1`+`FALLBACK_TLS_FINGERPRINT_1` and `FALLBACK_BASE_URL_2`+`FALLBACK_TLS_FINGERPRINT_2` when non-empty (compile-time defines, empty-string defaults).
35. `prioritizeLastGoodEndpoint()`: reorder so the endpoint whose id matches persistent key **`network_last_good_endpoint`** (default `"primary"`) is tried first.
36. `rememberSuccessfulEndpoint()`: persist **`network_last_good_endpoint`** (String id) and **`network_last_good_uses_fingerprint`** (bool) after any reachable response.
37. `isReachableHttpCode()` = `{200, 304, 204, 401}`.
38. Same candidate machinery is shared by the display fetch, file update, and firmware OTA flows.

### TLS validation

39. CA store loaded from LittleFS **`/certs.idx` + `/certs.ar`** per request; if 0 certs loaded → log "No certs found. Need to restart to get certs." and proceed **insecure**; otherwise validate against store. (Mechanism is replaced on ESP32 — behavior to preserve: *primary endpoint gets real CA validation; degraded-but-functional fallback when validation material is missing*.)
40. Fallback candidates with a configured fingerprint pin the TLS connection to that server instead of using the CA store (functional equivalent required, see risk register #2).

### Bitmap download & render

41. Format negotiation: BWR iff `X-Bitmap-Layers == 2` **and** `X-Black-Buffer-Length > 0` **and** `X-Red-Buffer-Length > 0`; else mono.
42. Mono: stream body in **128-byte chunks** to LittleFS **`/todo.bitmap`**; 8 pixels/byte, **MSB first**; `displayToScreen(file, w, h, color)` unpacks and calls `drawPixel()` inside a `display.display(lambda)` callback.
43. BWR: first `blackBufferLength` bytes → `/todo.bitmap`, next `redBufferLength` bytes → **`/todo_red.bitmap`**; bit semantics **0 = ink ON, 1 = ink OFF**; `displayBWRToScreen()` draws black layer then red layer in one display pass.
44. Download progress percentage logged; render duration logged via `millis()` delta.

### Persistent storage (store.h)

45. **`/setting.json`**: `{ "apiKey": <41-char string> }`; `saveSetting()`/`loadSetting()` via ArduinoJson.
46. **`/persistentValue.json`**: flat key-value JSON; template `getPersistentValue<T>(key, default)` / `savePersistentValue<T>(key, value)` (read whole doc, update, seek(0), truncate, rewrite); `removePersistentValue()` deletes the whole file. Known keys:
    - `network_last_good_endpoint` (String)
    - `network_last_good_uses_fingerprint` (bool)
    - `certs.ar_last_check_time` (per-file pattern `{name}_last_check_time`)
    - `certs.ar_etag` (pattern `{name}_etag`)
    - `lastFirmwareCheck`
    - `PCB_TEST` and `text`/`number`/`float`/`bool` (debug-only)
47. `LittleFS.begin()`/`LittleFS.end()` calls are balanced around every FS operation today (port should switch to mount-once, see risk #13, but data layout and keys must be preserved or migrated).
48. `RunningValue` is **RAM-only** (`char todoLastModified[30]`, `time_t displayContentLastCheckTime`); `cleanRunningValue()` resets both; intentionally **not** persisted across reboots (first fetch after boot is always unconditional).
49. `DEBUG_PERSISTENT_STORAGE` flag prints file contents after each save.

### File updates (lib/update/UpdateFiles.h)

50. `updateFiles()` → `updateFile("certs.ar", checkInterval=86400)` s. Per file: skip if `now - {name}_last_check_time < interval` (logged); else save check time, `GET {endpoint}/api/update/{name}` with `If-None-Match: <{name}_etag>` (+ `X-Device-Firmware-Version` if `GIT_VERSION`); 304 → remember endpoint; 200 → stream **128-byte chunks** to LittleFS file, save new `etag` header, remember endpoint; errors → next candidate.

### OTA firmware (lib/update/UpdateFirmware.h)

51. `updateFireWare(url=FIRMWARE_UPDATE_URL)` compiled only under `ENABLE_OTA`. Throttle via `lastFirmwareCheck`: **10×24×60×60 s (10 days)**, or **60 s** when `DEBUG_UPDATE_FIREWARE`.
52. Display/serial feedback: onStart → `showTextOnScreenCenter("Updating Firmware")`; onProgress → percent `(current*100)/total`, printed only on % change; onEnd → "Updated Firmware Success" + **delay(2000)**.
53. Tries the same 3 endpoint candidates; handles `HTTP_UPDATE_FAILED` (log `getLastError()`/`getLastErrorString()`), `HTTP_UPDATE_NO_UPDATES`, `HTTP_UPDATE_OK` (implicit reboot). Client stopped after each attempt.
54. Release pipeline parity: tag `v*` → CI builds `production` env → `firmware.bin` + md5 uploaded to GitHub Release, served by backend at `/api/update/firmware`.

### Display driver (lib/display-driver/)

55. Panel variants selected at compile time: `E_INK_750` (800×480, 12 pages, BWR), `E_INK_290` (296×128, 4 pages, BW); plus `E_INK_75Z90`, `E_INK_750_BW`, `E_INK_750_V1` envs in platformio.ini. `pages = height / display_buffer_pages`, `pageByteLength = (width * (height/pages)) / 8`.
56. SPI: MODE0, MSBFIRST, `SPI_CLOCK_DIV4`; `beginTransaction`/`endTransaction` wrap every `sendCommand`/`sendData`.
57. 750 pin map: SCK=14, MOSI=13, CS=15, RST=2, DC=4, BUSY=5 (BUSY input, **active-low = busy**; DC LOW=command HIGH=data).
58. Reset pulse: HIGH → delay(200) → LOW → delay(2) → HIGH → delay(200).
59. Panel init command sequence (750): `0x01` POWER SETTING (0x07,0x17,0x3f,0x3f) → `0x04` POWER ON → delay(200) → waitUntilIdle → `0x00` PANEL SETTING (0x0F) → `0x61` resolution (800×480) → `0x15` dual SPI (0x00) → `0x60` TCON (0x22) → `0x50` VCOM (0x11,0x07).
60. `waitUntilIdle()`: mandatory 200 ms delay, then poll BUSY every 100 ms until idle. `refresh()`: `0x12` → waitUntilIdle → delay(300).
61. Full render: black pass (page buffer filled 0xFF, command `0x10`) then red pass (filled 0x00, command `0x13`), pages streamed 0..N, single `refresh()` after both passes.
62. `drawPixel()` ignores pixels outside current color pass / page band; BLACK clears bit, RED sets bit.
63. Text: `drawString()` multiline with LEFT/CENTER/RIGHT align; `drawChar()` reads Font12/Font20/Font24 PROGMEM tables via `pgm_read_byte()`; `getStringBounds()` for centering.
64. Panel sleep after render: `0x02` power off → waitUntilIdle → delay(500) → `0x07` + data `0xA5` deep sleep; `SPI.end()`; `freeMemory()` deallocates page buffer via vector swap; heap logged before/after.
65. `testDisplay()` test pattern (rects + chars + "Hello World" in both colors) preserved for bring-up.

### Misc

66. `urlencode()`: space→`+`, alphanumeric pass-through, others `%HH`; cooperative yield inside the loop.
67. Build env matrix preserved: `production` (einktodo.com, `ENABLE_OTA=1`), `preview` (preview.einktodo.com, `DEBUG=1`, OTA), `development` (dev.einktodo.com, `DEBUG=1`, embedded `API_KEY`/WiFi via local.ini), `pcb-test` (`ENABLE_PCB_TEST=1`, `BUTTON_PIN=0`). `GIT_VERSION` injected via `!echo ... git describe --tags --always --dirty`.
68. `goToSleep()` light-sleep path is **dead code** (never called) — port may delete it; do not silently activate sleep behavior.

---

## 2. Porting risk register

| # | ESP8266 mechanism (where) | ESP32/FreeRTOS replacement strategy |
|---|---|---|
| 1 | `<ESP8266HTTPClient.h>` (todoList.h, fallbackNetwork.h, lib/update) | `<HTTPClient.h>` (arduino-esp32). API largely identical (`begin(client,url)`, `addHeader`, `collectHeaders`, `GET`, `getSize`, `getStreamPtr`, `headerName(i)`/`header(i)`, `errorToString`, `HTTP_CODE_*` enums all exist). Verify header collection and `errorToString` against the pinned core version; otherwise near drop-in. |
| 2 | BearSSL fingerprint pinning — `BearSSL::WiFiClientSecure::setFingerprint()` (fallbackNetwork.h:110) | mbedTLS-based `WiFiClientSecure`/`NetworkClientSecure` has **no usable fingerprint API**. Options: (a) replace `FALLBACK_TLS_FINGERPRINT_n` build flags with `FALLBACK_CA_PEM_n` and pin via `setCACert(pem)` per candidate; (b) `setInsecure()` for fallback candidates only (they exist for outage scenarios; document the downgrade). Keep `NetworkEndpointCandidate` struct, just swap `fingerprint` for `caPem`/`insecure`. The persisted `network_last_good_uses_fingerprint` key semantics must be redefined or dropped (one-time migration). |
| 3 | `BearSSL::CertStore` + `/certs.idx`/`/certs.ar` archive (`initCertStore`, `setCertStore`) (todoList.h:290-309, UpdateFiles.h:51, UpdateFirmware.h:75) | No CertStore on ESP32. Preferred: built-in **esp_crt_bundle** — `client.setCACertBundle(x509_crt_imported_bundle_bin_start)` or `esp_crt_bundle_attach`; covers Mozilla roots with zero runtime files. Alternative keeping the OTA-refreshable-trust feature: serve a PEM bundle file (e.g. `/ca.pem` via the existing `updateFile()` ETag flow, replacing `certs.ar`) and load with `setCACert()`. The ESP8266 `.ar` archive format is **not transferable**; the backend `/api/update/certs.ar` endpoint needs a PEM sibling if runtime refresh is kept. Preserve behavior: 0 certs available → log + `setInsecure()` fallback. |
| 4 | `<ESP8266httpUpdate.h>` / `ESPhttpUpdate.update(client,url)`, `onStart/onProgress/onEnd`, `getLastError(String)`, `t_httpUpdate_return` (UpdateFirmware.h) | `<HTTPUpdate.h>` on arduino-esp32: global `httpUpdate` object with the **same** `update(client,url)`, `onStart/onProgress/onEnd/onError`, `getLastError()/getLastErrorString()` and `HTTP_UPDATE_FAILED/NO_UPDATES/OK` enum. Mostly a rename (`ESPhttpUpdate` → `httpUpdate`) plus partition table with `app0`/`app1` OTA slots. Re-verify automatic reboot on `HTTP_UPDATE_OK`. |
| 5 | GPIO16 RTC register hack — `lib/gpio16/` (`ets_sys.h`, `osapi.h`, `ICACHE_FLASH_ATTR`, `WRITE_PERI_REG`/`READ_PERI_REG`, `PAD_XPD_DCDC_CONF`, `RTC_GPIO_CONF/ENABLE/OUT/IN_DATA`) + `button.h:137-142` special case | **Delete `lib/gpio16/` entirely.** On ESP32 the chosen button pin is a normal GPIO: `pinMode(BUTTON_PIN, INPUT_PULLUP)` and let OneButton handle it. Pick the new `BUTTON_PIN` per board layout (any GPIO with internal pull-up; avoid strap pins 0/2/12/15 unless intentional). If sleep-wake is ever added: `esp_sleep_enable_ext0_wakeup(gpio, 0)`. Remove the `BUTTON_PIN == 16` branch in button.h. |
| 6 | `wifi_set_opmode(NULL_MODE)`, `wifi_fpm_set_sleep_type`, `wifi_fpm_open`, `wifi_fpm_set_wakeup_cb`, `wifi_fpm_do_sleep` (button.h:20-25, `goToSleep()` — dead code) | No ESP32 equivalents. Since `goToSleep()` is never called, **delete it**. If sleep is wanted later: `WiFi.mode(WIFI_OFF)` / `esp_wifi_stop()` + `esp_sleep_enable_timer_wakeup()` / `esp_light_sleep_start()`. |
| 7 | `ESP.getChipId()` (main.cpp:26, network.h:98) — 32-bit ID feeding `X-Device-Id` and hostname | ESP32: derive from `ESP.getEfuseMac()` (48-bit), e.g. `(uint32_t)(mac >> 24)` or full hex MAC. **Server-side device identity will change** — the `Device` table tracks by `X-Device-Id`, so either accept a new device row per migrated unit, or generate the ID once and persist it in `/persistentValue.json` so it stays stable regardless of the derivation formula. Decide before flashing fleet. |
| 8 | `ESP.getSketchMD5()` (main.cpp:45,101) | Exists in arduino-esp32 (`ESP.getSketchMD5()`). Drop-in. |
| 9 | `ESP.getFlashChipId()` (main.cpp:103, PCB test only) | No direct equivalent; use `ESP.getFlashChipSize()`/`ESP.getFlashChipMode()` or omit from PCB-test screen. |
| 10 | `ESP.getFreeHeap()`, `ESP.restart()` | Same API on ESP32. Drop-in. |
| 11 | `<ESP8266mDNS.h>` (main.cpp:11 — included, unused) | Remove the include (or `<ESPmDNS.h>` if mDNS is ever needed). |
| 12 | `PROGMEM`, `PSTR()`, `pgm_read_byte()` (config.h cert, font tables, `initCertStore` paths) | All are compatibility no-ops on ESP32 (flash is memory-mapped). Keep as-is for portability; no code change needed. |
| 13 | LittleFS with `begin()`/`end()` wrapped around every operation; `board_build.filesystem = littlefs` | ESP32 has built-in `LittleFS` lib with the same file API, but mount/unmount is heavier and **not safe to churn across concurrent tasks**. Port to: mount **once** at boot, never `end()`; serialize all FS access behind one mutex (see §3). Requires `partitions.csv` (app0/app1 OTA + littlefs data partition) and `board_build.filesystem = littlefs` in the new env. Data files (`/setting.json`, `/persistentValue.json`, bitmaps) keep paths/format so OTA-migrated state survives if FS image is carried over; otherwise first boot regenerates them (acceptable: only apiKey + endpoint cache are lost, and apiKey lives in WiFiManager re-config). |
| 14 | `SPI.setClockDivider(SPI_CLOCK_DIV4)` / `setDataMode` / `setBitOrder` (display_750_driver.cpp:97-99) | Use `SPISettings(<freq Hz>, MSBFIRST, SPI_MODE0)` inside `beginTransaction` only (DIV4 on 80 MHz ESP8266 ≈ 20 MHz; pick ≤20 MHz per panel datasheet). `SPI.begin(SCK, MISO, MOSI, SS)` with explicit ESP32 pins — the 8266 pin numbers (14/13/15/2/4/5) must be remapped to the new board layout; keep them as build-flag-overridable `EPD_*_PIN` defines. |
| 15 | Busy-wait `waitUntilIdle()` (poll every 100 ms with `delay`) and pervasive `delay()` in driver + flows (200/300/500/2000/3000 ms, 5-min PCB block, 100×1 s NTP poll) | `delay()` on ESP32 == `vTaskDelay()` (yields), so nothing hard-blocks the scheduler — but these calls must live in a **dedicated task**, never in callbacks shared with the WiFi/portal or button tasks. Subscribe long operations (e-ink refresh ~1-5 s, OTA download) to the task WDT or `esp_task_wdt_reset()` as appropriate. |
| 16 | `yield()` per character in `urlencode()` (todoList.h:66) | `yield()` maps to `vPortYield()` on ESP32; harmless. Can be removed since the encoder runs in its own task. |
| 17 | OneButton @ 2.5.0 with `tick()` in tight loop | Library is ESP32-compatible. Run `tick()` from a dedicated button task or a 10 ms FreeRTOS timer; remove the gpio16 init branch (risk #5). |
| 18 | WiFiManager (tzapu) non-blocking portal, `process()` each loop, `getWiFiIsSaved()`, `resetSettings()` | WiFiManager supports ESP32 (credentials live in NVS instead of 8266 flash sectors). Verify: non-blocking `process()` cadence from its own task, `getWiFiIsSaved()` semantics on first boot, portal timeout callback, and custom-parameter save flow. `WiFi.mode(WIFI_OFF)` works on both. |
| 19 | `configTime()` + `time(nullptr)` + `gmtime_r()` (clock.h) | Same API on ESP32 (lwIP SNTP). Keep servers `time.asia.apple.com`, `cn.pool.ntp.org`, `pool.ntp.org` and TZ offset 28800. Convert the 100×1 s blocking poll into a wait inside the network task (or `sntp_set_time_sync_notification_cb`). |
| 20 | `HTTP_CODE_*` and `HTTP_UPDATE_*` enums | Same names/values in arduino-esp32; compile-check only. |
| 21 | platformio.ini: `platform = espressif8266`, `board = esp12e`, `monitor_filters = esp8266_exception_decoder` | New envs: `platform = espressif32`, target board (e.g. `esp32dev`/C3/S3 variant), `monitor_filters = esp32_exception_decoder`, `board_build.partitions = partitions.csv`. Keep the per-panel env matrix, `GIT_VERSION` shell injection, `extra_configs = local.ini`, and all `-D` feature flags (`E_INK_*`, `BUTTON_PIN`, `ENABLE_OTA`, `DEFAULT_API_URL`, `FALLBACK_*`, `DEBUG*`). Update `.github/workflows/tag.yml` `production` env accordingly. |
| 22 | Arduino `String` globals (`pendingDeviceEvent`, `DeviceID`, urlencode results) | Works on ESP32; heap fragmentation pressure is lower (more RAM) but cross-task `String` mutation is unsafe — covered by §3 ownership rules rather than type changes. |
| 23 | Single-threaded assumption: OneButton callbacks mutate state read by the network path "safely" because ESP8266 Arduino is one context | **This is the core architectural change.** On FreeRTOS, button callbacks, WiFi events, and the fetch/render pipeline can run on different tasks/cores. All shared globals in §3 need explicit ownership, mutexes, or queues. |
| 24 | `display.display(lambda)` callbacks capture LittleFS `File` handles opened in outer scope | With mount-once FS (risk #13) the `LittleFS.end()` lifetime hazard disappears, but file handles must still outlive the synchronous render call; verify the ESP32 driver port keeps `display()` fully synchronous. |

---

## 3. Shared mutable state map

Globals touched by more than one concern. Proposed RTOS design: four tasks — **NetTask** (fetch/render/update pipeline), **UiTask** (button + LED), **WifiTask** (WiFiManager `process()` + portal callbacks), and the display driver owned exclusively by NetTask (UI/WiFi screens routed through it via a request queue). Plus one global **FS mutex**.

| Global (location) | Written by | Read by | Protection strategy |
|---|---|---|---|
| `updating` (main.cpp) | main loop (NetTask), button single-click forced refresh | main loop gate | Replace flag with a **single NetTask + FreeRTOS queue of refresh requests**; button click just enqueues `FORCE_REFRESH`. If kept as a flag: `std::atomic<bool>` / `portMUX` critical section. |
| `pendingDeviceEvent` (todoList.h:20, `String`) | button double-click callback (UiTask) | NetTask reads + clears when building `X-Device-Event` header | **FreeRTOS queue of event strings** (UiTask → NetTask); preserves single-use semantics and raw-event-only contract. Never mutate a `String` cross-task. |
| `runningValue.todoLastModified[30]` (store.h:17) | NetTask after HTTP 200 (`strcpy`); button `cleanRunningValue()` | NetTask If-Modified-Since header | Make `RunningValue` **NetTask-private**; button sends `FORCE_REFRESH` message that makes NetTask call `cleanRunningValue()` itself. |
| `runningValue.displayContentLastCheckTime` (store.h) | NetTask at start of each fetch; `cleanRunningValue()` | main-loop cadence check | Same as above — NetTask owns its own cadence timer (`esp_timer` or `vTaskDelayUntil`). |
| `setting.apiKey` / `Setting` struct (store.h:21) | WiFiManager save-params callback (WifiTask) → `saveSetting()` | NetTask `Authorization` header, every request | Mutex around `Setting`, or WifiTask posts the new key to NetTask via queue; NetTask takes a local copy per request. |
| `wifiManager` + `isTringToConnectWiFi` (network.h:11-12) | WifiTask (`process()`, callbacks), button callbacks (start/stop portal), main loop | portal-active checks in button logic | Confine **all** WiFiManager calls to WifiTask; button sends `PORTAL_START`/`PORTAL_STOP`/`WIFI_TOGGLE` commands via queue; expose portal-active and WL_CONNECTED as atomics updated by WifiTask. |
| LittleFS (`/setting.json`, `/persistentValue.json`, `/todo.bitmap`, `/todo_red.bitmap`, `/certs.*`) | store.h (all tasks today), NetTask bitmap writes, update flows, button `removePersistentValue()` | display render lambdas, cert loading | Mount once at boot; **one global FS mutex** wrapping every open/read/write/remove. `/persistentValue.json` read-modify-rewrite is non-atomic — must hold the mutex across the whole transaction. |
| `/persistentValue.json` keys (`network_last_good_*`, `*_etag`, `*_last_check_time`, `lastFirmwareCheck`) | NetTask (endpoint memory, etags, throttles), UiTask (`removePersistentValue` on long-press/5-click) | NetTask | Covered by FS mutex; additionally, removal during an in-flight fetch should be serialized by routing the erase through NetTask's queue (or accepted as harmless because each access re-reads the file). |
| Display driver (`display`, `currentPageData`, `state`, `currentSendingColor`, panel SPI bus) | NetTask render; button/WiFi/OTA feedback screens (`showTextOnScreenCenter`, portal screen, "Connected to WiFi", 401/204 screens, OTA progress) | — | **Single owner: NetTask** (or a dedicated DisplayTask). All other concerns submit screen requests via queue. E-ink refresh is a multi-second blocking critical section — never callable from two tasks. |
| `DeviceID` (`String`, main.cpp) | once in `setup()` before tasks start | NetTask headers, hostname, PCB test | Write-once-before-scheduler → safe; mark `const` after init. |
| `longPressActionDone` (button.h) | UiTask only (press-start + during-press) | UiTask | Task-local — no protection needed once button handling lives in one task. |
| `lastBlinkTime` (led.h) | LED blink (dead code) | LED | UiTask-local if ever activated. |
| `updateFireWareProgress` (UpdateFirmware.h:13) | OTA progress callback (runs inside NetTask's `httpUpdate.update()`) | same task's serial logging | Task-local to NetTask — no protection needed. |
| `certStore` / secure clients (per-request locals in todoList.h/UpdateFiles.h/UpdateFirmware.h) | created/destroyed per attempt | same call | Keep stack-local per request inside NetTask; never share a `WiFiClientSecure` across tasks. |
| System time (`time(nullptr)` after `setClock()`) | WifiTask (NTP sync on connect) | NetTask cadence + throttle math | lwIP SNTP updates system time atomically; safe. Guard only the "time not yet synced" boot window (NetTask should tolerate pre-sync epoch, as today's `displayContentLastCheckTime=0` default already does). |

**Queue summary (UiTask/WifiTask → NetTask):** `FORCE_REFRESH`, `DEVICE_EVENT(string)`, `ERASE_PERSISTENT`, `API_KEY_UPDATED(string)`; **(UiTask → WifiTask):** `PORTAL_START`, `PORTAL_STOP`, `WIFI_TOGGLE`, `RESTART`. NetTask is the sole owner of: display driver, RunningValue, endpoint candidates, HTTP/TLS clients, OTA/file-update flows.
