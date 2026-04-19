# einktodo-device / rtos — ESP8266_RTOS_SDK v3.4 firmware

RTOS rewrite of the Arduino/PlatformIO e-ink firmware, targeting **ESP8266_RTOS_SDK
v3.4** (FreeRTOS, idf.py/CMake) on **ESP8266EX / esp12e / 4 MB flash** (~80 KB usable
heap). The binding architecture is `.migration/04-rtos-architecture.md`; SDK porting
facts are in `.migration/03-rtos-risks.md` (R1–R18); parity target is
`.migration/01-inventory.md` **section 1 only** (items 1–68).

This file is one of the three checked-in audit artifacts mandated by the architecture
(graft G8): the **build matrix**, the **event→action→file dispatch table**, the
**bring-up gate checklist**, and the **consolidated deviation ledger**.

---

## ⚠️ FLEET-COEXISTENCE WARNING (read before any OTA work)

> **The RTOS image must NEVER be served on the legacy `/api/update/firmware` channel.**
> It carries a different bootloader and partition map; flashing it onto an Arduino-fleet
> device **bricks** that device.
>
> The Arduino fleet's `GET /api/update/firmware` contract (`x-ESP8266-sketch-md5` vs the
> release `md5sum` asset, per `einktodo/app/api/update/firmware/route.ts`), plus
> `/api/update/certs.ar` and the `firmware.bin` / `md5sum` asset names, are **frozen
> byte-for-byte forever**.
>
> The RTOS line gets a **new** channel: `GET /api/update/rtos/firmware`, negotiating on
> `X-Device-Firmware-Version` against a separately pinned `rtos-v*` release, serving
> `firmware-rtos.bin` (+ `firmware-rtos.sha256`). Distinct URL + distinct asset names +
> distinct tag prefix are the triple guard against cross-flashing a brick;
> `esp_ota_end()`'s image-magic check rejects an Arduino `.bin` as defense in depth.
>
> **The web route `GET /api/update/rtos/firmware` must merge before the first RTOS OTA
> is attempted** (tracked as Residual 7 / bring-up gate 12).

---

## Build matrix

Toolchain (installed + verified): `source ~/esp/rtos-env.sh` sets `IDF_PATH`
(`~/esp/ESP8266_RTOS_SDK`, v3.4), the xtensa toolchain, the python venv, and
`CMAKE_POLICY_VERSION_MINIMUM=3.5`. Then `cd einktodo-device/rtos`.

The shared `sdkconfig.defaults` is layered with exactly one per-environment overlay. The
four environment builds (item 67), **verbatim**:

```sh
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.production"  build
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.preview"     build
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.development" build
idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.pcb-test"    build
```

| Overlay | Contents |
|---|---|
| `.production` | `EINKTODO_API_URL="https://www.einktodo.com"`, `OTA_URL=".../api/update/rtos/firmware"`, `ENABLE_OTA=y`, log WARN |
| `.preview` | `preview.einktodo.com`, `EINKTODO_DEBUG=y`, `EINKTODO_DEBUG_OTA=y` (60 s OTA soak channel) |
| `.development` | `dev.einktodo.com`, `DEBUG=y`, OTA off; pairs with git-ignored `sdkconfig.defaults.local` |
| `.pcb-test` | `EINKTODO_PCB_TEST=y`, `EINKTODO_BUTTON_PIN=0` |

Panel choice (`choice EINKTODO_PANEL`) sets dimensions, `X-Device-Colors` (bwr/bw),
driver variant, and `CONFIG_EINKTODO_RENDER_TIMEOUT_MS` (90000 for 750 BWR, 60000 for
BW). Build gates require **750-BWR + 290** to compile (§8 build gates).

Concurrency note: when running builds in parallel, always pass your own `-B build_<name>`
so agents do not stomp each other's build dir.

Secrets (`API_KEY`, dev WiFi creds, `EINKTODO_TLS_INSECURE`) live only in the
git-ignored `sdkconfig.defaults.local`; the checked-in `sdkconfig.defaults.local.example`
is the template. Never put a real key in a committed prod default.

### CI

`einktodo-device/.github/workflows/rtos-tag.yml`: tags `rtos-v*` → host tests
(`test/host/`) → `idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.production" build`
→ **explicit size-gate step failing if the app `.bin` > `0xE0000`** (G9) → upload
`firmware-rtos.bin` + `firmware-rtos.sha256` to the GitHub Release. The Arduino
`tag.yml` is untouched.

---

## Tasks & sync objects

One event group `sys_events`, bits: `BIT_WIFI_CONNECTED (1<<0)`,
`BIT_PORTAL_ACTIVE (1<<1)`, `BIT_TIME_SYNCED (1<<2)`, `BIT_DISP_DONE (1<<3)`,
`BIT_DISP_ABORTED (1<<4)`, `BIT_DISP_STARTED (1<<5)` (display_task dequeued the
bitmap request — separates queue-drain time from the panel-derived render timeout).

Queues (all payloads fixed-size structs copied by value per G7 — **no pointers into
task-local buffers, ever**):

- `cmd_queue` (depth 4): `app_cmd_t` — button/wifi/httpd → **cycle_task**.
- `wifi_cmd_queue` (depth 4): `wifi_cmd_t` — button/httpd → **wifi_ctrl_task**.
- `disp_queue` (depth 4): `display_req_t` — any task → **display_task**.

| Task | File | Prio | Stack |
|---|---|---|---|
| cycle_task | `main/cycle_task.c` | 5 | 8192 |
| display_task | `components/eink_display/display_task.cpp` | 4 | 4096 |
| button_task | `components/button_input/button_input.c` | 6 | 2048 |
| wifi_ctrl_task | `components/net_portal/wifi_ctrl.c` | 5 | 3072 |
| dns_task (portal-only) | `components/dns_server/dns_server.c` | 4 | 2048 |
| httpd (SDK, portal-only) | `components/net_portal/portal_httpd.c` | 5 | 4096 |

---

## Event → action → file dispatch table

Every externally observable input mapped to its handler action and the owning file.
This table is the parity-audit checklist (§5 feature-ownership matrix): each row is
diffed against the legacy module to confirm no behavior was silently dropped.

### Boot

| Trigger | Action | File |
|---|---|---|
| Power-on | 2 s settle → `store_init()` → load apiKey → `display_init()` + `DISP_LAUNCH` → `button_input_start()` → `net_portal_init()` → `cycle_task_start()` | `main/app_main.c` |
| Boot, `EINKTODO_DEBUG` | Dump chip info + app version + elf-sha256 + running partition + `store_dump()` (D1) | `main/app_main.c`, `store/store.c` |
| Boot, `EINKTODO_PCB_TEST` | `pcb_test_run()` → `DISP_PCB_INFO` + 5-min block | `main/pcb_test.c` |
| NVS mount fails (`NO_FREE_PAGES`/`NEW_VERSION_FOUND`) | `nvs_flash_erase` + retry recovery | `store/store.c` |

### Cycle (cadence + HTTP dispatch — cycle_task is the sole consumer ⇒ no re-entrancy)

| Trigger | Action | File |
|---|---|---|
| Poll timer elapsed (gated on `time(NULL)`, default 60 s) | `ota_update_check()` → `todo_fetch_run()` | `main/cycle_task.c` |
| HTTP 304 | No render; keep image | `todo_fetch.c` → `cycle_task.c` |
| HTTP 401 | `DISP_AUTH_401` screen | `todo_fetch.c` → `display_task.cpp` |
| HTTP 204 | `DISP_NO_CONTENT` screen | `todo_fetch.c` → `display_task.cpp` |
| HTTP 200 | Erase `ceil(len/4096)` sectors after headers (G5) → stream body to `bitmap` partition in 4 KB chunks → **length-verify** vs `Content-Length` + `X-Black/Red-Buffer-Length` (G4); on mismatch skip render, keep image, no `todoLastModified` update (D5) | `todo_fetch.c` |
| Render needed (200, verified) | Post `DISP_RENDER_BITMAP`; wait `BIT_DISP_STARTED` (budget `RENDER_TIMEOUT_MS` × (queued screens + 1) — boot/WiFi status screens ahead of us each take a full refresh), then `BIT_DISP_DONE`/`BIT_DISP_ABORTED` (timeout `CONFIG_EINKTODO_RENDER_TIMEOUT_MS`) | `cycle_task.c` |
| `BIT_DISP_DONE` | Persist `todoLastModified` + `rememberSuccessfulEndpoint` (G2) | `cycle_task.c`, `endpoint_fallback.c` |
| `BIT_DISP_ABORTED` | Do NOT persist `todoLastModified` — server retries next poll (G2/D10) | `cycle_task.c` |
| Render wait timeout | Log + `esp_restart()` — fatal (G3) | `cycle_task.c` |

### `cmd_queue` (`app_cmd_t` → cycle_task)

| Message | Action | File |
|---|---|---|
| `CMD_FORCE_REFRESH` | `todo_fetch_clean_running_value()` + immediate fetch | `cycle_task.c` |
| `CMD_DEVICE_EVENT{arg}` | Stash one-shot `X-Device-Event` header (e.g. "double-click"); cleared only after a reachable HTTP code (D3) | `cycle_task.c`, `todo_fetch.c` |
| `CMD_ERASE_PVALUE` | `store_erase_pvalue()` (`pvalue` only; `setting`/apiKey survives) | `cycle_task.c`, `store.c` |
| `CMD_API_KEY_UPDATED{arg=api_key}` | `store_set_api_key()` (cycle_task is the sole `setting`-ns writer, G1) → clean running value → fetch | `cycle_task.c`, `store.c` |

### `wifi_cmd_queue` (`wifi_cmd_t` → wifi_ctrl_task)

| Message | Action | File |
|---|---|---|
| `WIFI_CMD_PORTAL_START` | Open APSTA portal (httpd + dns) | `net_portal/wifi_ctrl.c` |
| `WIFI_CMD_PORTAL_STOP` | Stop httpd/dns, `esp_wifi_stop()` portion of FSM | `wifi_ctrl.c` |
| `WIFI_CMD_TOGGLE` | Toggle portal on/off | `wifi_ctrl.c` |
| `WIFI_CMD_RESET_AND_PORTAL` | `esp_wifi_restore()` + reopen portal | `wifi_ctrl.c` |
| `WIFI_CMD_SAVE{ssid,pass,api_key}` | Apply STA config + connect; forward `CMD_API_KEY_UPDATED` to cycle_task (G1) | `wifi_ctrl.c` → `cycle_task.c` |

### Button gestures (`button_input.c`; never blocks, never touches display/sockets/NVS)

| Gesture | Action | File |
|---|---|---|
| 1 click | Event-bit-selected: portal-active → stop portal; connected → force-refresh; else → portal-start | `button_input.c` (→ `cmd_queue`/`wifi_cmd_queue`) |
| 2 clicks | `CMD_DEVICE_EVENT("double-click")` | `button_input.c` |
| 4 clicks | `esp_restart()` (device reboot gesture) | `button_input.c` |
| 5 clicks | `CMD_ERASE_PVALUE` | `button_input.c` |
| 6 clicks | `WIFI_CMD_TOGGLE` | `button_input.c` |
| 6 s hold (one-shot) | `CMD_ERASE_PVALUE` + `WIFI_CMD_RESET_AND_PORTAL` | `button_input.c` |

### WiFi / portal events

| Trigger | Action | File |
|---|---|---|
| AP started | AP creds + `DISP_AP_INFO` screen; DNS captive catch-all | `net_portal/wifi_ctrl.c`, `dns_server.c` |
| `GET /` (portal) | Scan list + SSID/password/41-char apiKey form | `portal_httpd.c` |
| `POST /save` | `WIFI_CMD_SAVE` posted timeout-0 (G6), return immediately | `portal_httpd.c` |
| GOT_IP | Set `BIT_WIFI_CONNECTED`, `DISP_WIFI_CONNECTED` screen, `sntp_clock_start()` | `wifi_ctrl.c`, `sntp_clock.c` |
| Connect failed (3×30 s) | Stop portal + 3000 ms delay + reopen (item 24) | `wifi_ctrl.c` |
| Portal 600 s timer | Stop httpd/dns + `esp_wifi_stop()` + `DISP_PORTAL_TIMEOUT` screen | `wifi_ctrl.c` |
| NTP sync notification | Set `BIT_TIME_SYNCED` | `sntp_clock.c` |

### OTA (runs on cycle_task; serialized with fetch by construction)

| Trigger | Action | File |
|---|---|---|
| Cycle, throttle elapsed (10 d / 60 s under `DEBUG_OTA`, via `fw_last_check`) | GET `OTA_URL` with `X-Device-Firmware-Version` | `ota_update.c` |
| OTA 304 | No update | `ota_update.c` |
| OTA 200 | `DISP_TEXT_CENTER("Updating Firmware")` → `esp_ota_begin/write/end` (with `esp_task_wdt_reset`) → `esp_ota_set_boot_partition` → success screen → 2 s → `esp_restart()` | `ota_update.c` |

---

## Bring-up gate checklist (§8)

Hardware bring-up on the attached dev board at `/dev/cu.usbserial-10`.

> ⚠️ `idf.py -p /dev/cu.usbserial-10 flash` writes a different bootloader/partition map
> and **erases the Arduino firmware on this board**. Acceptable for the dev board (not
> fleet hardware); restore Arduino later via `pio run -e development -t upload` from the
> old tree.

Each gate must pass before the next:

1. **Console** — `idf.py -p /dev/cu.usbserial-10 flash monitor`: app banner at **115200**
   (boot ROM chirp at 74880 is expected), 2 s settle, version string = `git describe`
   output.
2. **Identity gate (blocks all server-visible testing)** — flash the Arduino build once to
   log `ESP.getChipId()`, then the RTOS build to log `device_id_get()`; values must be
   equal.
3. **NVS** — store init, set/get apiKey, 5-click erases `pvalue` only (apiKey survives),
   power-pull mid-commit recovers.
4. **GPIO16 button** — all gestures from the dispatch table on `BUTTON_PIN=16`
   (1/2/4/5/6 clicks, 6 s hold fires once); no spurious reset holding the button through
   boot; if readback misbehaves flip `CONFIG_EINKTODO_GPIO16_RAWREG`.
5. **Display** — `testDisplay()` pattern on the 750 panel; init/refresh/sleep timings vs
   Arduino serial logs. **HSPI-vs-bitbang: DECIDED → bit-bang** (`CONFIG_EINKTODO_EPD_BITBANG=y`
   in `sdkconfig.defaults`, all overlays). Bench-verified 2026-06-12: HSPI `spi_trans` word
   packing did not drive the panel (firmware logged a full render but the e-ink held its old
   image); bit-bang is byte-exact to the legacy Arduino SPI stream and updates the panel. ✅
6. **Portal** — phone joins "E-ink Todo List AP", captive sheet pops (iOS + Android
   probes), scan/save works, AP-info → connected screens, 600 s timeout screen,
   power-cycle auto-connects, long-press forgets SSID.
7. **TLS/fetch** — handshake against `www.einktodo.com` + `preview.einktodo.com` + a
   Let's Encrypt host (proves ISRG roots for the next Cloudflare rotation); pinned
   fallback connects with the correct pin and refuses a flipped byte; pre-NTP fetch
   behaves (pinned path works, primary fails validity until sync — documented).
8. **Bitmap parity** — full BWR 200 → `esptool.py read_flash` of the `bitmap` partition
   byte-compares against `curl` of the same URL with the same headers; visual black/red
   alignment; truncation test (kill server mid-body) → no render, old image retained,
   next poll retries unconditionally (D5/D10).
9. **Heap** — logged min-free > 8 KB across handshake + 96 KB BWR + OTA check in one
   cycle; 7-day soak with a flat min-free trend.
10. **Cadence/status paths** — 304 on second poll, 401 screen with a bad key, 204 screen;
    forced refresh from button < 500 ms to HTTP start; 24 h cadence drift vs server
    `lastSeen`.
11. **WDT** — 48 h soak with button mashing during refresh/OTA; deliberate idle-starve
    test build confirms the WDT actually fires.
12. **OTA** (after the web route lands) — preview-channel cycle: flash ota_0, server pin
    bump, device updates and boots from ota_1 (`esp_ota_get_running_partition()` logged),
    then back; power-pull mid-download leaves otadata untouched; **negative test**: RTOS
    device pointed at the legacy `/api/update/firmware` URL must fail image validation and
    not flash.

---

## Consolidated deviation ledger

The **closed** set of intentional behavior changes vs the Arduino tree. Anything not
listed here must be bit-for-bit parity; an undocumented difference is a bug.

### Architecture ledger (§6 — binding)

| # | Deviation | Rationale |
|---|---|---|
| D1 | Sketch-MD5 boot dump → app version + elf-sha256 + running partition | MD5's only consumer was the legacy OTA route; not used by the RTOS channel |
| D2 | `goToSleep()` deleted | Was dead code; sleep never reintroduced |
| D3 | `X-Device-Event` cleared **after** a reachable HTTP code (was: cleared before send) | Bug fix — event no longer lost on connection failure |
| D4 | certs.ar / `updateFiles()` / `FILE_UPDATE_URL` retired; CA trust ships in-image, refreshed via firmware OTA | No CertStore on mbedTLS; roots live decades; 10-day OTA cadence suffices; `/api/update/certs.ar` stays frozen for the fleet |
| D5 | Truncated/short bitmap download ⇒ **skip render**, keep previous image (was: render partial file) | Strictly better; length-verified against Content-Length + layer-length headers (G4) |
| D6 | "0 certs → proceed insecure" branch removed | Trust is compiled in and can't be missing; `CONFIG_EINKTODO_TLS_INSECURE` remains debug-only |
| D7 | `delay(50)` download→render gap replaced by queue + DONE/ABORTED event-bit handshake | Same serialization intent, explicit and testable (item 33 mapping) |
| D8 | No LittleFS data migration; first RTOS boot starts clean | Different flash layout; loses only endpoint cache + throttle timestamps; apiKey re-enters via portal / Kconfig |
| D9 | NVS key renames (`net_lg_ep`, `net_lg_pin`, `fw_last_check`) | NVS 15-char key limit |
| D10 | Failed render no longer updates `todoLastModified` (server retried next poll) | G2 — an aborted render must not 304-suppress its own retry |

### Implementation-discovered deviations (reported by implementation agents)

These were surfaced during implementation and are NOT in the original §6 closed set. They
are recorded here per the iron rule that any behavior difference must be logged. Integrator
action is flagged where relevant.

| # | Deviation | Source / note |
|---|---|---|
| ID1 | `store_dump()` walks a **fixed table of known keys** (`setting`/`api_key`; `pvalue` `net_lg_ep`, `net_lg_pin`, `fw_last_check`, `pcb_test`, plus legacy debug keys `text`/`number`/`bool`) instead of iterating. | ESP8266_RTOS_SDK v3.4 has **no** `nvs_entry_find` / `nvs_iterator_t` API in `components/nvs_flash` (the 04 doc §2 and 03-risks R8 assume it exists — **verified absent**). Observable difference: a `pvalue` key written outside this known set would not appear in the dump. **Integrator: if siblings add new `pvalue` keys, extend the `s_dump_keys` table in `store.c`** (they may not edit it under file ownership). |
| ID2 | 290 driver: added **no-op stub overrides** for `drawChar` / `drawString(const char*)` / `getStringBounds`. | Legacy `display_290_driver.h` left these as inherited pure-virtuals, so `DisplayDriver290` was abstract and the `E_INK_290` path never actually compiled in the legacy tree. Stubs let the panel choice instantiate it in this single-component build. Not a parity change to any working legacy path — the 290 driver is a non-functional stub (`drawPixel`/`refresh`/`sendPageData` are all no-ops in legacy too). |
| ID3 | Non-290 7.5" panel variants (`750_BW`, `750_V1`, `75Z90`) currently **bind to `DisplayDriver750`** via `display_task.cpp`'s panel typedef. | Only the byte-identical 750 BWR and 290 drivers were specified for this step; per-variant dimension/sequence divergence is a bring-up-gate decision (§8 gate 5). **Integrator: the typedef, not separate drivers, backs those choices today.** |
| ID4 | Portal `/restart` menu action posts `WIFI_CMD_RESET_AND_PORTAL` (close+reopen portal) instead of `ESP.restart()` (full reboot). | A portal httpd handler must not own a device reboot; the device-level reboot gesture is the 4-click button path. Not in §6. **Integrator: if exact parity is wanted, add an internal WIFI_CMD that calls `esp_restart()` in task context, or treat `/restart` as cosmetic.** |
| ID5 | A new project component `components/esp_crt_bundle/` was created (not in the §2 file plan, which named only `esp_http_client`/`tcp_transport`/`pinned_tls`/`certs`). | Stock v3.4 declares `esp_tls_cfg_t.crt_bundle_attach` and the guarded call in `esp_tls_mbedtls.c` but **never shipped** the `esp_crt_bundle` component, header, `gen_crt_bundle.py`, or the `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` / `CONFIG_MBEDTLS_V3` symbols (the arch doc assumed step 8 introduces them). The new component supplies `esp_crt_bundle.h` + `esp_crt_bundle_attach()` and declares the two Kconfig symbols — minimal way to honor the one attach hook. Shadows nothing; no agent owns it. |
| ID6 | `CONFIG_MBEDTLS_V3` is **informational only**; crypto runs on the in-tree **mbedTLS v2**. | v3.4 ships mbedTLS v2 sources exclusively (no `mbedtls_v3` tree); the doc's "tier-1 = mbedTLS v3" is not literally achievable. The full TLS feature set (crt_bundle attach, `mbedtls_ssl_conf_verify` leaf pinning, `mbedtls_x509_crt` CA chain, SHA1/SHA256) all use v2 APIs and compile+link. `MBEDTLS_V3=y` is kept so the committed `sdkconfig.defaults` baseline configures without an "unknown symbol" warning. |
| ID7 | `esp_crt_bundle_attach` does **not** pre-parse `certs/ca_roots.pem` into `conf->ca_chain`. It binds an empty chain plus a `mbedtls_ssl_conf_verify` callback that, for the topmost peer cert (flagged `NOT_TRUSTED` by mbedtls after it has linked the rest of the chain itself), walks the PEM one root at a time with `mbedtls_pem_read_buffer` + `mbedtls_x509_crt_verify`. | Bench 2026-10-27: nine pre-parsed roots cost ~17 KB of heap for the whole SERVER_CERTIFICATE step and pushed min-free-heap to ~3.7 KB, where the P-384 chain verify failed intermittently (`-0x2700 NOT_TRUSTED` on later polls). Lazy anchoring raised min-free-heap to ~17.8 KB over 5 consecutive full handshakes. Same trust decision; the root is resident only during its own check. |
| ID8 | `esp_crt_bundle/CMakeLists.txt` injects its include dir onto the `esp-tls` component library target (`target_include_directories` on `idf_component_get_property(esp-tls COMPONENT_LIB)`, gated by `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE`) so `esp_tls_mbedtls.c`'s guarded `#include` resolves. | Touches only the new component's own `CMakeLists`; the SDK `esp-tls`/`mbedtls` trees are NOT edited. Assumes `esp-tls`'s component lib target is registered before project components are processed (true in v3.4); harmless no-op if the property is empty. |
| ID9 | SDK `test/` subdirs dropped from both shadow copies (`esp_http_client/test`, `tcp_transport/test`). | Keeps the components lean and avoids pulling unity/host-test deps into the firmware build. Noted in each `upstream.diff` header. |

---

## Component layout

```
einktodo-device/rtos/
  CMakeLists.txt   partitions.csv   README.md
  sdkconfig.defaults(+ .production/.preview/.development/.pcb-test/.local.example)
  main/            certs/ca_roots.pem
  components/
    net_portal/  dns_server/  endpoint_fallback/  todo_fetch/  eink_display/
    button_input/  store/  ota_update/  sntp_clock/  pinned_tls/
    esp_http_client/  tcp_transport/   (patched SDK shadow copies)
    esp_crt_bundle/                    (project component — see ledger ID5)
  test/host/                           (host-side transform/endpoint/urlencode unit tests)
```

Module map (legacy → new): `network.h`→`net_portal`(+`dns_server`),
`fallbackNetwork.h`→`endpoint_fallback`, `todoList.h`→`todo_fetch`,
`display.h`+`lib/display-driver/`→`eink_display`, `button.h`+`lib/gpio16/`→`button_input`,
`store.h`→`store`, `lib/update/UpdateFirmware.h`→`ota_update`, `clock.h`→`sntp_clock`,
`main.cpp`→`main/{app_main.c,cycle_task.c}`.
