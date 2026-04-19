# ESP8266_RTOS_SDK v3.4 Rewrite — Risk Register

Scope: full rewrite of the Arduino/PlatformIO firmware onto **ESP8266_RTOS_SDK v3.4** (FreeRTOS, idf.py/CMake), chip **ESP8266EX / esp12e / 4MB flash**, new project tree at `einktodo-device/rtos/`. The existing Arduino tree stays untouched and buildable; the deployed fleet keeps OTAing from it.

- Feature parity target: `.migration/01-inventory.md` **section 1 only**. Section 2 of that file (ESP32 risk register) is **obsolete and superseded by this document** — we stay on ESP8266, so chip-identity, pin-remap and arduino-esp32 advice there does not apply.
- SDK facts below were verified against the `release/v3.4` branch of `espressif/ESP8266_RTOS_SDK` (component headers/Kconfig) and docs.espressif.com; live TLS chain inspected 2026-06-10 with `openssl s_client`.

---

## 0. Target architecture summary

### Project layout (`rtos/`)

```
rtos/
  CMakeLists.txt              # idf project; sets PROJECT_VER from `git describe --tags --always --dirty`
  partitions.csv
  sdkconfig.defaults          # shared baseline (115200 console, mbedTLS dynamic buffers, partition file)
  sdkconfig.defaults.production / .preview / .development / .pcb-test   # env overlays (SDKCONFIG_DEFAULTS list)
  main/                       # app: net task, button task, portal, store, clock, ota
    Kconfig.projbuild         # all EINKTODO_* knobs (URLs, API key, button pin, fallbacks, intervals)
  components/
    eink_display/             # ported lib/display-driver (C++), fonts reused as-is
    pinned_tls/               # leaf-fingerprint pinning via mbedtls verify callback (R5)
    esp_http_client/          # project-local copy of SDK component + crt_bundle_attach passthrough (R4/R5)
    tcp_transport/            # ditto (adds esp_transport_ssl_crt_bundle_attach)
    dns_server/               # captive-portal DNS catcher (ported from IDF captive_portal example)
```

(Project-local `components/<name>` shadows the SDK component of the same name — standard IDF mechanism; keeps the SDK checkout pristine.)

### Tasks (FreeRTOS)

| Task | Owns | Stack | Prio | Notes |
|---|---|---|---|---|
| `net_task` | HTTP/TLS clients, fetch→render pipeline, OTA, RunningValue, endpoint candidates, **display driver** | **8192 B** | 5 | TLS handshake + x509 verify is the deepest call path on this chip; 8 KB verified in SDK https examples. Receives commands via queue. |
| `button_task` | GPIO16 poll + gesture FSM | 2048 B | 6 | 10 ms poll loop (`vTaskDelay(pdMS_TO_TICKS(10))`). Emits events to `net_task`/`wifi` queues; never touches display or sockets itself. |
| `httpd` (portal) | esp_http_server internal task | 4096 B (CONFIG_HTTPD default) | 5 | Only alive while portal active. |
| `dns_task` (portal) | UDP DNS catcher | 2048 B | 4 | Only alive while portal active. |
| sys tasks | wifi, tcpip, esp_event loop, idle | SDK defaults | — | Event handlers must stay tiny: set bits / post to queues only. |

Cross-task contract (carried over from inventory §3, adapted): `net_task` is the **single owner of the e-ink driver** (refresh is a multi-second non-reentrant critical section). All screens (portal info, "Connected to WiFi", 401, OTA progress, launch) are display *requests* queued to `net_task`. Queue messages: `FORCE_REFRESH`, `DEVICE_EVENT("double-click")`, `ERASE_PVALUE`, `SHOW_SCREEN(id, text)`, `PORTAL_*` (to wifi control), `RESTART`. The old `updating` flag disappears: a single consumer task makes re-entrancy impossible by construction.

### Partition table (4MB, `partitions.csv`)

```
# Name,     Type, SubType,  Offset,   Size
nvs,        data, nvs,      0x9000,   0x4000
otadata,    data, ota,      0xd000,   0x2000
phy_init,   data, phy,      0xf000,   0x1000
ota_0,      app,  ota_0,    0x10000,  0xF0000
ota_1,      app,  ota_1,    0x110000, 0xF0000
bitmap,     data, 0x40,     0x200000, 0x20000
# ~1.8 MB spare above 0x220000 for future (ca-bundle refresh partition, logs, …)
```

Mirrors the SDK's stock `partitions_two_ota.csv` for the app slots (see R10 for the 1 MB-window constraint). `bitmap` (128 KB raw) replaces LittleFS `/todo.bitmap` + `/todo_red.bitmap` (R7). No SPIFFS/LittleFS at all (R8).

### Memory budget (≈80 KB usable heap)

| Item | Peak | When |
|---|---|---|
| mbedTLS handshake, **CONFIG_MBEDTLS_DYNAMIC_BUFFER** on | ~20–30 KB transient | per HTTPS request |
| CA trust material | ~0 KB resident (crt-bundle in flash) or 8–12 KB resident (global CA store fallback) — see R4 | boot→forever |
| esp_http_client rx/tx buffers | 1–2 KB | per request |
| Display page buffer | 4,000 B (800×40/8) | render only — allocated **after** TLS teardown |
| Bitmap body (48 KB mono / 96 KB BWR) | **never in RAM** — streamed in ≤4 KB chunks to the `bitmap` partition | download |

Rule preserved from today’s code: the HTTP client is fully torn down (`esp_http_client_cleanup`) **before** the display driver allocates its page buffer, so TLS and render never coexist on the heap.

---

## 1. Risk register

Format: **Today (Arduino/NONOS)** → **RTOS SDK v3.4 replacement** / known v3.4 limitations / verification notes.

### R1. WiFiManager (tzapu) config portal

**Today** — `network.h`: non-blocking portal (`process()` per loop), AP `"E-ink Todo List AP"`/`"einktodo.com"`, portal timeout 600 s, connect timeout 30 s × 3 retries, custom 41-char `apiKey` parameter persisted via `saveSetting()`, menu/title, callbacks (`configModeCallback` draws AP info on e-ink, `preSaveConfigCallback`, `saveConfigCallback`, `setSaveParamsCallback`, `configPortalTimeoutCallback` → `WiFi.mode(WIFI_OFF)` + "Press button to restart"), `getWiFiIsSaved()`, `resetSettings()`, hostname `e-ink-todo-list-<chipid hex>`.

**Replacement** — hand-rolled portal:
- AP: `esp_wifi_set_mode(WIFI_MODE_APSTA)` + `esp_wifi_set_config(ESP_IF_WIFI_AP, …)` (SSID/password from Kconfig, WPA2).
- Web UI: `esp_http_server` (`httpd_start`, `httpd_register_uri_handler`). One page: scanned SSID list (`esp_wifi_scan_start` blocking + `esp_wifi_scan_get_ap_records` — run the scan from a worker, APSTA scan works while AP is up), password field, 41-char API-key field, save/restart actions. Static HTML embedded with `EMBED_TXTFILES`.
- Captive redirect: custom UDP DNS server answering every A query with the softAP IP (port the self-contained `dns_server.c` from the IDF `captive_portal` example — pure BSD sockets, compiles on lwIP/8266 unchanged) + httpd catch-all 302.
- Credential store: `esp_wifi_set_storage(WIFI_STORAGE_FLASH)` (default) keeps STA creds in NVS. `getWiFiIsSaved()` ⇒ `esp_wifi_get_config(ESP_IF_WIFI_STA, &cfg)` and check `cfg.sta.ssid[0] != 0`. `resetSettings()` ⇒ `esp_wifi_restore()`.
- Connect state machine: `esp_event` handlers for `WIFI_EVENT_STA_START/DISCONNECTED` + `IP_EVENT_STA_GOT_IP`; retry counter (3 × 30 s) then "WiFi connect failed" screen + reopen portal (parity with `onWifiConnectFailed`, including the 3000 ms pause). Portal timeout: a `xTimerCreate` 600 s one-shot → stop httpd/dns, `esp_wifi_stop()`, queue "Press button to restart" screen.
- Hostname: `tcpip_adapter_set_hostname(TCPIP_ADAPTER_IF_STA, "e-ink-todo-list-%06x")` before `esp_wifi_connect()` (v3.4 is tcpip_adapter-based, not esp_netif).
- apiKey save → NVS `setting/api_key` (R8) + `cleanRunningValue()` semantics via `FORCE_REFRESH` to net_task.

**v3.4 limitations** — no WPS-style autoconnect helper, nothing remembers *multiple* networks (WiFiManager didn’t either); `esp_wifi_scan` while an HTTP request is in flight stalls STA traffic — scan only when portal is open and no fetch running. esp_http_server on 8266 defaults to few sockets (CONFIG_LWIP max sockets) — keep `max_open_sockets` ≤ 3.

**Verify** — phone connects to AP, captive sheet pops (iOS `captive.apple.com` / Android `connectivitycheck`), form saves, device joins WiFi, e-ink shows the same three screens as today (config-mode AP info / trying / connected). Power-cycle: auto-connects without portal. `esp_wifi_restore()` after long-press actually forgets the SSID.

### R2. OneButton (mathertel 2.5.0) timing semantics

**Today** — `button.h`: `tick()` polled in tight loop; single click (portal-stop / force-refresh / portal-start), double-click → `pendingDeviceEvent="double-click"`, multi-click 4=restart 5=erase-pvalue 6=wifi-toggle, long-press with `getPressedMs() > 6000` one-shot action (erase + wifi reset + portal). OneButton 2.5.0 defaults: debounce **50 ms**, click/multi-click window **400 ms**, long-press threshold **800 ms**.

**Replacement** — small poll-based FSM in `button_task` (10 ms `vTaskDelay` cadence), constants copied from OneButton: debounce 50 ms, inter-click window 400 ms, press-start at 800 ms, app-level long action fired once at 6000 ms held. Click-count dispatch identical (1/2/4/5/6). Callbacks become queue posts; the FSM never blocks. Timebase: `esp_timer_get_time()/1000`.

**v3.4 limitations** — **GPIO16 has no interrupt capability** (see R3), so an ISR-driven OneButton port is impossible anyway; polling is the only option and matches today's behavior (Arduino loop polled too). 10 ms poll preserves OneButton's effective resolution.

**Verify** — bench-test every gesture against the README table; especially that 2 clicks don't fire 1-click, that 6 s hold fires exactly once per press, and that a click during portal closes the portal (queue ordering: button → wifi-control must preempt a pending fetch screen).

### R3. GPIO16 input (BUTTON_PIN=16, `lib/gpio16/`)

**Today** — production button on **GPIO16** (the RTC pad). Arduino can’t pull it up, so `lib/gpio16/` pokes `PAD_XPD_DCDC_CONF` / `RTC_GPIO_CONF` / `RTC_GPIO_ENABLE` / `RTC_GPIO_OUT` to configure it as **output driving HIGH**; the button shorts the (externally resistored) pad to GND and OneButton reads the level back. pcb-test builds use GPIO0 instead.

**Replacement** — RTOS SDK `driver/gpio` **does support GPIO16** (`GPIO_NUM_16`, "input and output" per the gpio docs), with hard limitations confirmed in the v3.4 docs: *"The GPIO of esp8266 can not be pulled down except RTC GPIO which can not be pulled up"* — i.e. GPIO16 has **no internal pull-up**, and RTC IO supports **no interrupts/wakeup**. Two options, in order of preference:
1. Replicate today's trick with the official driver: `gpio_config{ .pin_bit_mask = 1ULL<<16, .mode = GPIO_MODE_OUTPUT }`, `gpio_set_level(16, 1)`, then poll `gpio_get_level(16)`. On the RTC pad the input register (`RTC_GPIO_IN_DATA`) reflects the pin level even in output mode — same physics the Arduino hack relies on.
2. If (1)'s readback misbehaves, port `lib/gpio16/gpio16.cpp` verbatim — `WRITE_PERI_REG`/`READ_PERI_REG` and the RTC register addresses exist in RTOS SDK (`esp8266/include/esp8266/eagle_soc.h`, `rom/ets_sys.h`); only the `ICACHE_FLASH_ATTR`/`osapi.h` includes change. This is ~40 lines and is the guaranteed-equivalent path.

`BUTTON_PIN` stays a knob (`CONFIG_EINKTODO_BUTTON_PIN`, default 16; pcb-test overlay sets 0). For pins ≠ 16, plain `gpio_config` with `GPIO_PULLUP_ENABLE` input.

**v3.4 limitations** — no interrupt on 16 ⇒ polling only (consistent with R2). Driving the pad high adds ~µA, same as today.

**Verify** — scope/bench: confirm `gpio_get_level(16)` toggles while configured as output-high with the button pressed; confirm no spurious resets (GPIO16 is also the deep-sleep-wake strap — we never sleep, but check reset behavior when holding the button during boot).

### R4. BearSSL CertStore (`/certs.idx` + `/certs.ar` Mozilla bundle)

**Today** — per-request `certStore.initCertStore(LittleFS, "/certs.idx", "/certs.ar")`; 0 certs ⇒ log + `setInsecure()`; bundle refreshed daily-ish from `/api/update/certs.ar` (ETag flow).

**Replacement (CA strategy decision)** — drop the runtime-refreshable `.ar` archive; trust material ships **inside the app image** and is refreshed by firmware OTA (10-day check cadence makes CA rotation latency acceptable; roots live for decades).

Live chain (checked 2026-06-10): `CN=einktodo.com` ← **Google Trust Services WE1** ← **GTS Root R4** ← (cross-sign) **GlobalSign Root CA**. einktodo.com is Cloudflare-fronted; Cloudflare rotates edge issuers among **Google Trust Services**, **Let's Encrypt (ISRG)** and **SSL.com**. So "only the chain in play" = GTS R4 today, but that *will* rot. Concrete plan, two tiers:

1. **Preferred: `esp_crt_bundle`** — verified present in v3.4 (`components/mbedtls/mbedtls_v3/esp_crt_bundle/`, Kconfig `MBEDTLS_CERTIFICATE_BUNDLE` with FULL / CMN / custom-list options) — **but only under `CONFIG_MBEDTLS_V3`** (v3.4 defaults to mbedTLS v2; the choice exists in `components/mbedtls/Kconfig`). The bundle stays in flash; verification does an indexed issuer lookup per handshake, ~zero resident RAM. Use the **custom list** (`gen_crt_bundle.py --input` curated PEM): GTS Root R1–R4, GlobalSign Root CA, ISRG Root X1/X2, SSL.com RSA+ECC roots ⇒ ~15 KB flash. Wiring: `esp_tls_cfg_t.crt_bundle_attach` exists in v3.4 and `esp_tls_mbedtls.c` honors it (`cfg->crt_bundle_attach(&tls->conf)` — verified in source), **but `esp_http_client_config_t` has no `crt_bundle_attach` field in v3.4** (verified) and `esp_transport_ssl.h` lacks the attach call. ⇒ project-local copies of `esp_http_client` + `tcp_transport` adding the passthrough (mirror the later-IDF commit; ~30 lines). This same hook carries the fingerprint pinning (R5), so the patch pays for itself twice.
2. **Fallback if mbedTLS v3 misbehaves on 8266**: stay on `MBEDTLS_V2`, embed the curated PEM (~14 KB) and load it **once at boot** into the esp-tls global CA store (`esp_tls_init_global_ca_store()` + `esp_tls_set_global_ca_store(pem, len)`), then set `use_global_ca_store = true` in every `esp_http_client_config_t` (field verified present in v3.4). Cost: ~8–12 KB resident heap (parsed DER roots). No SDK patch needed for this tier.

Degraded mode parity ("0 certs ⇒ insecure"): with the bundle compiled in, the material can't be missing, so the insecure branch **goes away** — log-and-proceed-insecure is replaced by hard verification. Keep a `CONFIG_EINKTODO_TLS_INSECURE` debug-only knob for bring-up.

Server side: `/api/update/certs.ar` stays for the Arduino fleet; the RTOS line never calls it. The whole `updateFile()/certs.ar` flow and its `certs.ar_etag` / `certs.ar_last_check_time` keys are **dropped** (the generic `updateFile` machinery has no remaining users; do not port it — the `/api/update/{file}` route remains fleet-only).

**v3.4 limitations** — `MBEDTLS_V3` disables the openssl compat layer (unused by us) and is the less-exercised path on 8266 — hence tier 2. Full Mozilla bundle (`cacrt_all.pem`, ~130 roots) is *possible* flash-wise but pointless and slows lookup; never parse a full bundle into RAM on this chip (would be >100 KB).

**Verify** — handshake against `www.einktodo.com`, `preview.einktodo.com` and a Let's Encrypt staging host (to prove ISRG roots work for the next Cloudflare rotation); pull the negotiated chain with `openssl s_client` after each Cloudflare cert renewal in CI (`e2e-schedule` is a natural place). Measure `esp_get_free_heap_size()` before/during/after handshake.

### R5. BearSSL SHA-1 fingerprint pinning (IP fallback endpoints)

**Today** — `fallbackNetwork.h`: candidates from `FALLBACK_BASE_URL_1/2` + `FALLBACK_TLS_FINGERPRINT_1/2` (SHA-1 of the **leaf** cert, colon-separated, e.g. `3E:EC:…:5B` for `https://107.173.82.8:16845`); `client.setFingerprint()` replaces CA validation for those candidates. Persisted `network_last_good_endpoint` / `network_last_good_uses_fingerprint`.

**Replacement** — mbedTLS has no fingerprint API; build it from the verify callback:
- `components/pinned_tls`: `esp_err_t pinned_tls_attach(void *conf)` (signature-compatible with `crt_bundle_attach`, so it plugs into the same patched hook from R4). It calls `mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED)` and `mbedtls_ssl_conf_verify(conf, pinned_verify_cb, &s_pin_ctx)`.
- `pinned_verify_cb(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)`: at `depth == 0`, hash the **raw leaf DER** (`crt->raw.p`, `crt->raw.len`) with `mbedtls_sha1()` (SDK port `esp_sha1.c` present; also accept SHA-256 pins — pin string length disambiguates 20 vs 32 bytes), constant-time compare against the parsed build-flag pin; on match `*flags = 0` (clears UNTRUSTED/CN errors — the IP-vs-CN mismatch is expected for IP endpoints, equivalent to BearSSL fingerprint mode), on mismatch set `MBEDTLS_X509_BADCERT_OTHER` and return nonzero to abort the handshake. For `depth > 0` return 0 leaving flags — chain trust is irrelevant under a leaf pin.
- One connection at a time (net_task serializes all HTTP) ⇒ a single static pin context is safe; assert-guard it anyway.
- Endpoint candidate machinery ports 1:1 (`id`, `url`, `pin`, `usePin`; `prioritizeLastGoodEndpoint` / `rememberSuccessfulEndpoint` / `isReachableHttpCode {200,304,204,401}` unchanged), persistence keys remapped per R8.

**v3.4 limitations** — without the R4 hook patch there is **no** way to reach `mbedtls_ssl_conf_verify` through `esp_http_client`; the patch is mandatory for pinning (this is the single most port-specific piece — budget review time). `mbedtls_ssl_conf_verify` requires `MBEDTLS_X509_CRT_PARSE_C` (on by default). Note the pin still breaks at every leaf renewal — same operational property as today (pin is a build flag refreshed per release).

**Verify** — unit-test the callback against the real `107.173.82.8:16845` endpoint: correct pin connects; flipped byte refuses; expired-window NTP-less clock still connects (leaf pinning ignores validity? **No** — mbedtls sets `BADCERT_EXPIRED/FUTURE` flags which our callback clears only on hash match at depth 0; decide and test explicitly: today BearSSL fingerprint mode ignores validity, so clear those flags too for parity — this also keeps the fallback usable before first NTP sync).

### R6. ESP8266HTTPClient: If-Modified-Since flow, header collection, streaming

**Today** — `todoList.h`: `https.begin(client, url)`, `addHeader` (If-Modified-Since, X-If-Modified-Since, Authorization Bearer, X-Device-Id, X-Device-Colors, X-Device-Firmware-Version, X-Device-Event one-shot), `collectHeaders` of 9 response headers, `GET()`, `getSize()`, `getStreamPtr()->readBytes` in 128-byte chunks; 60 s timeouts; status dispatch 304/401/204/200/other.

**Replacement** — `esp_http_client` (v3.4 verified API):
- config: `{ .url, .timeout_ms = 60000, .event_handler = on_http_event, .buffer_size = 1024, .transport_type = HTTP_TRANSPORT_OVER_SSL }` + trust per R4/R5.
- request headers: `esp_http_client_set_header()` for each (same names; `X-Device-Event` cleared after `esp_http_client_open` succeeds — preserve single-use semantics on connection-level failure by clearing only after a *reachable* HTTP code, which is actually a small behavior fix vs today's clear-before-send; note it in the changelog).
- streaming GET: `esp_http_client_open(client, 0)` → `esp_http_client_fetch_headers()` → `esp_http_client_get_status_code()` / `get_content_length()` → loop `esp_http_client_read(client, buf, n)`.
- response headers: **v3.4 has no response-header getter** — capture in the event handler on `HTTP_EVENT_ON_HEADER` (key/value strings) into a small struct keyed by the 9 known names (`Content-Picture-Width/Height`, `API-Version`, `Last-Modified`, `X-Data-Modified`, `X-Bitmap-Layers`, `X-Black/Red-Buffer-Length`, `X-Device-Config-Last-Modified`; fixed-size value buffers, `Last-Modified` ≤ 29 chars as today).
- `urlencode()` ports as a plain C function (no `yield()` needed — own task); width/height are numeric anyway.
- redirects: server doesn't redirect device routes; leave `disable_auto_redirect=false` defaults.

**v3.4 limitations** — `esp_http_client_fetch_headers` returns chunked length as 0/−1; the display route always sets Content-Length (server streams a fixed-size buffer), keep today's "Content-Length ≤ 0 ⇒ try next candidate" guard. Keep-alive across candidates: always `esp_http_client_cleanup()` between attempts (one client object per candidate, as today).

**Verify** — 304 path (second poll), 401 (bad key ⇒ 401 screen), 204, BWR 200, mono 200 (`X-Device-Colors: bw` build), header capture under header-name case variance (server may emit lowercase — match case-insensitively; ESP8266HTTPClient was case-insensitive too).

### R7. Bitmap streaming + LittleFS bitmap cache (48 KB mono / 96 KB BWR)

**Today** — body streamed in 128-byte chunks to `/todo.bitmap` (+ `/todo_red.bitmap` split at `blackBufferLength`); render re-reads the file(s) through `drawPixel` inside the page-banded `display(λ)` callback (λ runs 12 pages × 2 color passes = 24 full-file scans).

**Replacement** — stream to the raw **`bitmap` partition** (no filesystem):
- `esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "bitmap")`; `esp_partition_erase_range` (whole 128 KB or just `ceil(total/4096)` sectors) **before** the GET (erase is slow, ~10s of ms/sector — doing it pre-request keeps the TCP window from stalling); `esp_partition_write` per chunk. Use a 1–4 KB read buffer (heap is free during download; bigger chunks = fewer flash ops than today's 128 B). Black layer at offset 0, red layer at offset `blackBufferLength` rounded up to 4 — store the exact lengths + `w/h/isBWR` in a tiny header sector or RAM (RAM is fine: RunningValue is net_task-private and the cache is only read in the same call, exactly like today — the file was never reused across boots either, first fetch after boot is unconditional).
- Render: keep the `display(λ)` callback design, but replace per-pixel re-scan with **band copy**: for page *p* of color pass *c*, `esp_partition_read(part, layer_offset + p*4000, bandbuf, 4000)` and merge into the page buffer bytewise. Bit semantics (preserve exactly):
  - v3 BWR black layer: server bit 0 = ink → panel black buffer bit 0 = black ⇒ **copy verbatim**.
  - v3 BWR red layer: server bit 0 = ink → panel red buffer bit 1 = red ⇒ **bitwise NOT**.
  - mono: server bit 1 = draw black (per `displayToScreen`) ⇒ **bitwise NOT** into the black pass, red pass left white.
  Keep the slow `drawPixel` path compiled for the text screens; the band copy is only for full-screen bitmaps. Honor `w`/`h` smaller than panel (today's x/y clamping) by row-wise copy with `w/8` stride.
- Progress logging % preserved (serial only).

**v3.4 limitations** — `esp_partition_write` needs 4-byte-aligned sizes/offsets: pad the final chunk; red-layer offset alignment handled by the round-up. Flash writes during active WiFi RX are fine but add latency — 96 KB ≈ 24 sector-writes, negligible vs network time.

**Verify** — byte-compare a downloaded layer (`esptool.py read_flash` of the bitmap partition) against `curl` output of the same URL; visual check black/red alignment on the 750 panel; mono build on the 290-class config; 800×480 BWR end-to-end with <10 KB free-heap dip during download.

### R8. LittleFS settings (`/setting.json`, `/persistentValue.json`) + 5-click / long-press clears

**Today** — ArduinoJson read-modify-rewrite files; `LittleFS.begin()/end()` churned around every op; 5-click and 6 s-long-press call `removePersistentValue()` (deletes `persistentValue.json` **only** — `setting.json`/apiKey survives both gestures; long-press additionally wipes WiFi creds and reopens the portal).

**Replacement** — **NVS only** (`nvs_flash_init`, mount-once at boot; no SPIFFS, no LittleFS port). Two namespaces preserve today's blast-radius semantics:

| Namespace | Keys (NVS limit: **15 chars**) | Was |
|---|---|---|
| `setting` | `api_key` (str, ≤41) | `/setting.json` `apiKey` |
| `pvalue` | `net_lg_ep` (str — endpoint id/url), `net_lg_fpr` (u8 bool), `fw_last_check` (i32 epoch), `pcb_test` (str, pcb-test only), debug keys | `/persistentValue.json` |

- 15-char key limit forces renames (`network_last_good_endpoint` is 26 chars) — keys above are all ≤13. There is no data migration (different flash layout; first RTOS boot starts clean — acceptable: only the endpoint cache and throttle timestamps are lost, and the apiKey is re-entered via portal or `CONFIG_EINKTODO_API_KEY` for dev).
- Gestures: 5-click ⇒ `nvs_erase_all(handle_pvalue)` + `nvs_commit`. Long-press ⇒ same + `esp_wifi_restore()` + portal start. apiKey untouched by both — exact parity.
- `removeSetting()` (unused today) ⇒ `nvs_erase_key(setting, "api_key")`.
- `RunningValue` stays RAM-only/net_task-private (`todoLastModified[30]`, `displayContentLastCheckTime`); `cleanRunningValue()` triggered via queue.
- The generic `getPersistentValue<T>/savePersistentValue<T>` template becomes typed nvs get/set wrappers; `DEBUG_PERSISTENT_STORAGE` dump ⇒ `nvs_entry_find` iteration (supported in v3.4).
- `formatLittleFS()` recovery ⇒ standard `nvs_flash_init()` retry: on `ESP_ERR_NVS_NO_FREE_PAGES`/`NEW_VERSION_FOUND` ⇒ `nvs_flash_erase()` + re-init.

**v3.4 limitations** — NVS writes are atomic per key (an improvement over the non-atomic JSON rewrite); string values ≤ 1984 B (irrelevant here). nvs partition 16 KB is plenty (<10 keys).

**Verify** — power-pull mid-`nvs_commit` doesn't corrupt (NVS journal handles it); 5-click then poll re-tries primary endpoint first; long-press leaves apiKey readable after re-provisioning WiFi.

### R9. ESPhttpUpdate vs server contract + fleet coexistence

**Today** — `ESPhttpUpdate.update(client, url)` hits `GET /api/update/firmware`. **Server contract (read from `einktodo/app/api/update/firmware/route.ts`)**: reads request header **`x-ESP8266-sketch-md5`** (auto-sent by ESPhttpUpdate along with the other `x-ESP8266-*` headers), compares against the md5sum asset of the pinned GitHub release (`config.ts: currentFirmwareVersion`, prod `v0.11.0`), returns **304** on match else proxies `firmware.bin` (requires Content-Length). Client throttles checks to 10 days (`lastFirmwareCheck`), draws progress screens, reboots on OK.

**Replacement** —
- **Do not reuse `/api/update/firmware`.** The RTOS image (IDF app-image format, different bootloader at 0x0, different partition map) would **brick an Arduino-fleet device** that flashed it, and the Arduino `firmware.bin` would brick an RTOS device. Server-side coexistence (out of code scope here, but binding for the web repo): a **separate channel** — `GET /api/update/rtos/firmware` — with its own pinned release/asset name (`firmware-rtos.bin` + `firmware-rtos.sha256` uploaded by a new tag workflow in `rtos/`, e.g. tags `rtos-v*`). The legacy route, its `x-ESP8266-sketch-md5` contract, the GitHub asset names, and `/api/update/certs.ar` stay byte-for-byte unchanged so the deployed fleet keeps working forever. Version negotiation on the new channel: client sends `X-Device-Firmware-Version: <PROJECT_VER>` (it already sends this header today on display fetches); server returns 304 when equal to the pinned RTOS version, else the image. No MD5-of-flash gymnastics — `esp_https_ota` verifies the image header/magic, and `PROJECT_VER` comparison replaces sketch-MD5 identity.
- Client: v3.4 `esp_https_ota(&http_config)` exists (single-call, verified header) **but** accepts only an `esp_http_client_config_t` — no hook to add custom headers and no progress callback in v3.4. Since we need `X-Device-Firmware-Version`, progress %, and the endpoint-candidate loop, implement the **manual loop** instead: `esp_http_client` (open/fetch_headers, 304/200 dispatch, same candidates + pinning as R5) → `esp_ota_begin(esp_ota_get_next_update_partition(NULL), OTA_SIZE_UNKNOWN)` → chunked `esp_ota_write` with %-change logging + "Updating Firmware" screen (queued before the loop, matching `onStart`) → `esp_ota_end` → `esp_ota_set_boot_partition` → "Updated Firmware Success" screen + 2 s delay + `esp_restart()`. Throttle (10 days / 60 s under `CONFIG_EINKTODO_DEBUG_OTA`) via `fw_last_check` (R8). `HTTP_UPDATE_NO_UPDATES` ⇔ 304.
- Rollback: v3.4 has **no** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`/`esp_ota_mark_app_valid_cancel_rollback` (that's IDF ≥4.x). Mitigate: only call `esp_ota_set_boot_partition` after `esp_ota_end` validates the image; keep the 10-day cadence so a bad release is caught in preview first (preview overlay uses 60 s checks against `preview.einktodo.com`).

**v3.4 limitations** — see rollback above; also `esp_https_ota` on 8266 streams whole-image (fine); image must fit 0xF0000 (R10).

**Verify** — full OTA preview-channel cycle on hardware: old RTOS build → server pin bumped → device updates, boots from the *other* slot (`esp_ota_get_running_partition()` logged at boot), survives power-pull mid-download (otadata untouched until `set_boot_partition`). Negative test: point an RTOS device at the **legacy** URL — it must fail image validation (Arduino .bin lacks the IDF app magic) and not flash; document loudly anyway.

### R10. OTA partition layout / ESP8266 flash-mapping constraint

**Today** — Arduino single-image OTA via eboot (no slots).

**Replacement** — dual-slot per the stock `partitions_two_ota.csv` pattern (verified in v3.4 tree): `ota_0 @ 0x10000` and `ota_1 @ 0x110000`, both `0xF0000`. ESP8266 maps flash through a **1 MB cache window**; the v3 bootloader selects the MB containing the active slot, so one binary runs from either slot **as long as each slot sits at the same offset inside its own 1 MB block and never crosses a 1 MB boundary** — which the layout above satisfies. Max app size ≈ 960 KB (current feature set + fonts + CA bundle should land ~600–700 KB; check `idf.py size` in CI).

**v3.4 limitations** — `CONFIG_ESP8266_BOOT_COPY_APP` exists but is marked **"The function is not released"** in the v3.4 Kconfig — do **not** rely on copy-mode; use the symmetric two-slot layout above. Changing `ota_0`'s offset requires matching `CONFIG_PARTITION_TABLE_OFFSET`-adjacent Kconfig (noted in the CSV header comment).

**Verify** — boot from both slots (flash to ota_0, OTA to ota_1, OTA again back to ota_0); `esp_ota_get_running_partition()` in the boot log; binary size gate in CI at 0xE0000 (safety margin).

### R11. `millis()` / `delay()` / Ticker / tight `loop()`

**Today** — `loop()` spins `button.tick()` + `wifiManager.process()`; cadence gate `time(nullptr) - displayContentLastCheckTime > 60`; `millis()` for durations; `delay()` sprinkled everywhere (50/100/200/300/500/2000/3000 ms, 2 s boot, 5-min PCB block, 100×1 s NTP poll); `yield()` in urlencode; no Ticker in use; `goToSleep()`/`wifi_fpm_*` is dead code — **delete, do not re-introduce sleep**.

**Replacement** — `loop()` disappears: net_task blocks on `xQueueReceive(cmd_queue, &msg, pdMS_TO_TICKS(remaining_until_next_poll))` — timeout drives the 60 s cadence (`CONFIG_EINKTODO_CHECK_INTERVAL`, gated on `time(nullptr)` like today so NTP jumps behave identically), queue messages preempt it (force refresh etc.). `delay(n)` → `vTaskDelay(pdMS_TO_TICKS(n))` (driver included, see R12); `millis()` → `esp_timer_get_time()/1000` (`int64_t` — kills the 49-day wrap, keep printf formats in sync); `yield()` → deleted. LED API (dead code) ports as `gpio_set_level` wrappers to keep parity #12 compiling.

**v3.4 limitations** — FreeRTOS tick default 100 Hz ⇒ `vTaskDelay` resolution 10 ms; the driver's `delay(1)`/`delay(2)` become `ets_delay_us(1000/2000)` busy-waits (sub-tick, intentional).

**Verify** — cadence drift over 24 h vs server `lastSeen`; forced refresh latency from button press < 500 ms to HTTP start.

### R12. Display driver (`lib/display-driver/`) Arduino-call inventory → shim/rewrite

**Today** — `display_750_driver.cpp` uses exactly: `Serial.print*`, `pinMode`, `digitalWrite`, `digitalRead`, `delay`, `millis` (in callers), `SPI.begin/end/setDataMode(SPI_MODE0)/setBitOrder(MSBFIRST)/setClockDivider(SPI_CLOCK_DIV4 ≈ 20 MHz)/beginTransaction(spiSettings)/endTransaction/transfer`, `String` overload of `drawString`, `pgm_read_byte` on font tables, `ESP.getFreeHeap()`, plus portable C++ (`std::vector`, `std::function`, `max`). Pins: SCK=14, MOSI=13, CS=15, RST=2, DC=4, BUSY=5 (active-low busy). Panel command sequences, page math (12 pages × 4000 B), `waitUntilIdle` (200 ms then 100 ms polls), sleep sequence — all pure logic, ports unchanged.

**Replacement** — port the component as C++ (`components/eink_display`), replacing the thin Arduino layer (no global shim header; ~30 call sites):
- GPIO: `gpio_config`/`gpio_set_level`/`gpio_get_level` (`driver/gpio.h`) for RST/DC/BUSY (+ CS if software-CS).
- SPI: the 7.5" pin map is exactly the **HSPI** fixed pinout (SCK=GPIO14, MOSI=GPIO13, CS=GPIO15) ⇒ use `driver/spi.h` (`spi_init(HSPI_HOST, &spi_config)` with `SPI_MODE0`-equivalent clk polarity defaults, MSB first, `SPI_10MHz_DIV`–`SPI_20MHz_DIV`; panel datasheet caps at 20 MHz, start at 10 MHz). Hardware CS is asserted per `spi_trans` — replaces the manual CS toggles. `sendCommand` = DC low + 1-byte trans; `sendData` = DC high + 1-byte trans; `sendDataWithLen` = DC high + chunked transfers. **v3.4 SPI driver limitation: ≤ 64 bytes (512 bits) per `spi_trans`** ⇒ chunk the 4000-byte page sends into 64-byte transfers (62 trans/page — overhead fine, e-ink refresh dominates). If the driver's CS framing glitches the panel (some EPDs want CS held across a multi-byte burst): fall back to **GPIO bit-bang** (the panel tolerates slow clocks; 800 KHz bit-bang sends a page in ~40 ms, 24 pages ≈ 1 s — acceptable, and zero SDK dependency). Decision gate at bring-up; keep both behind `CONFIG_EINKTODO_EPD_BITBANG`.
- `delay` → `vTaskDelay` (≥10 ms) / `ets_delay_us` (<10 ms); BUSY wait loop keeps `vTaskDelay(100 ms)` polls so idle task runs (WDT, R13).
- `Serial` → `printf`/`ESP_LOGI`; `ESP.getFreeHeap()` → `esp_get_free_heap_size()`; `String` overloads → `std::string`/`const char*`.
- **Fonts** (`font12/20/24.cpp` ≈ 15 KB of tables): reuse files as-is, but on ESP8266 `.rodata` lives in **DRAM** by default — 15 KB of precious heap-adjacent RAM. Define `PROGMEM` → `__attribute__((section(".irom0.text"), aligned(4)))` and `pgm_read_byte(p)` → aligned 32-bit-read-and-extract helper (same trick as the Arduino core), keeping tables in mapped flash. **Verification note:** byte loads from ICACHE-mapped flash fault on ESP8266 — the aligned-word accessor is mandatory; check the `.map` file confirms the tables landed in irom and DRAM usage dropped ~15 KB.
- Concurrency: driver instance owned by net_task only (see §0); `display()` stays fully synchronous; the render λ's data source is now an `esp_partition_read` band copy (R7), so the LittleFS file-handle-lifetime hazard disappears.

**Verify** — `testDisplay()` pattern (kept) on real 750 panel; init/refresh/sleep timing vs Arduino logs; current draw in deep-sleep (panel cmd 0x07/0xA5) unchanged.

### R13. Watchdogs (task WDT) during e-ink refresh, TLS, OTA

**Today** — Arduino core feeds the SW/HW WDTs inside `delay()`/`yield()`; the multi-second e-ink busy-waits and 100×1 s NTP poll survive because they `delay()`.

**Replacement** — v3.4 task WDT (`esp_task_wdt.h`: only `esp_task_wdt_init()` + `esp_task_wdt_reset()`; **no per-task subscribe API like IDF** — the 8266 TWDT watches the tasks that feed it / relies on idle). Keep `CONFIG_TASK_WDT=y` (+ `CONFIG_TASK_WDT_PANIC` per env). Rules: every long operation in net_task either blocks in the SDK (sockets/`vTaskDelay` — fine, idle runs) or loops with `vTaskDelay` inside (BUSY poll, download loop already does I/O-blocking reads); add `esp_task_wdt_reset()` in the OTA write loop and the page-send loop as belt-and-braces. **Never** busy-spin on `gpio_get_level` without delay. The hardware WDT (always on) is fed by the idle task — starving idle with a priority-6+ compute loop is the one real foot-gun; all our heavy work sits at prio 5 with frequent blocking.

**Verify** — soak: force a 60 s server stall (timeout path) + full BWR refresh + OTA download concurrently with button mashing; no WDT reset in 48 h. Deliberately starve idle in a test build to confirm the WDT actually fires (panic+reboot per `CONFIG_ESP_PANIC_PRINT_REBOOT`).

### R14. Heap fragmentation / TLS memory on 80 KB

**Today** — BearSSL clients ≈ 22 KB/connection, stack-local per attempt; free-heap logged around HTTPS setup.

**Replacement / hard requirements** —
- `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y`, `CONFIG_MBEDTLS_DYNAMIC_FREE_PEER_CERT=y`, `CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y` (all verified present in v3.4 for both mbedTLS v2 and v3 Kconfigs). Without these, mbedTLS wants 16 KB+16 KB static SSL buffers + handshake state ⇒ OOM next to lwIP on this chip. With them, peak ≈ 20–30 KB transient.
- One TLS connection at a time, ever (net_task serialization gives this for free).
- Sequencing per request: build candidates → TLS+HTTP → stream to flash → `esp_http_client_cleanup()` → **then** allocate the 4 KB page buffer and render (carries over today's accidental-but-correct ordering, now explicit).
- Keep allocations chunky and short-lived: 1–4 KB I/O buffer reused across the request; no `String` concat churn (use `snprintf` into fixed buffers for URLs/headers); fonts/CA material in flash (R4, R12).
- lwIP: default TCP WND/MSS on 8266 are already heap-conscious; don't raise them. Log `esp_get_free_heap_size()` + `esp_get_minimum_free_heap_size()` at the same points as today (parity #32).
- ECDSA handshake (Cloudflare WE1 chain) is CPU-slow on 8266 (~2–4 s software P-256) — set 160 MHz (`CONFIG_ESP8266_DEFAULT_CPU_FREQ_160=y`) to halve it.

**Verify** — `esp_get_minimum_free_heap_size()` > 8 KB across: handshake + 96 KB BWR download + OTA check in one cycle; 7-day soak for fragmentation creep (min-free trend flat).

### R15. NTP / `configTime` / timezone

**Today** — `configTime(28800, 0, "time.asia.apple.com", "cn.pool.ntp.org", "pool.ntp.org")` on connect; blocking poll up to 100 s for epoch ≥ 57600.

**Replacement** — lwIP SNTP (verified in v3.4: `lwip/apps/sntp`, `esp_sntp.h`): `sntp_setoperatingmode(SNTP_OPMODE_POLL)`, `sntp_setservername(0..2, …)` (3 servers — check `SNTP_MAX_SERVERS`/`CONFIG_LWIP_SNTP_MAX_SERVERS` ≥ 3, raise if needed), `sntp_init()` from the GOT_IP handler. TZ: `setenv("TZ","CST-8",1); tzset();` so `localtime` matches today's UTC+8 math (`gmtime_r` call sites keep their semantics — today the code mixes gmtime+offset; port faithfully). Replace the blocking poll with an event-group bit set by `sntp_set_time_sync_notification_cb` (present in v3.4 `esp_sntp.h`) — net_task tolerates pre-sync epoch exactly as today (`displayContentLastCheckTime=0` ⇒ first fetch unconditional; If-Modified-Since starts empty anyway).

**Verify** — time correct after boot ≤ 15 s on normal networks; fetch pipeline runs even when NTP is blocked (firewalled test AP) — TLS validity-window failures in that state: primary endpoint will fail cert validation until time syncs (same as today's BearSSL behavior with CertStore — BearSSL also checks validity); pinned fallback still works if R5 clears validity flags. Document this boot-order dependency.

### R16. Serial console & diagnostics

**Today** — 115200 baud, 2 s settle delay, chip-id/version/MD5 dump, exception decoder in monitor.

**Replacement** — `CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200` (verified Kconfig name; **v3.4 default is 74880** — must override in `sdkconfig.defaults`). Boot ROM still chirps at 74880 before the app — unchanged from Arduino. Keep the 2 s startup delay (`vTaskDelay`) for parity #13. `ESP.getSketchMD5()` → `esp_ota_get_app_description()` (`esp_app_desc_t.version/project_name` + `app_elf_sha256`); print version + running partition instead of MD5 (the MD5 had one consumer — the legacy OTA route, gone per R9). `ESP.getChipId()` → derive from `esp_efuse_mac_get_default()`: `(mac[3]<<16)|(mac[4]<<8)|mac[5]` — **same silicon, same MAC**, so `X-Device-Id` and the server's `Device` row continuity is preserved *if the formula matches*; **verification note:** print both old (Arduino build) and new values on one bench unit and confirm equality before any fleet-adjacent testing (NONOS `system_get_chip_id()` is documented as MAC-derived but has an offset quirk on some batches). `ESP.getFlashChipId()` (pcb-test only) → `spi_flash` JEDEC id or drop per inventory note. Monitor: `idf.py monitor` gives addr2line decoding (replaces `esp8266_exception_decoder`).

### R17. Build-time knobs → Kconfig / CMake mapping

| Arduino `-D` flag | RTOS replacement | Notes |
|---|---|---|
| `DEFAULT_API_URL` | `CONFIG_EINKTODO_API_URL` (string) | per-env overlay |
| `FIRMWARE_UPDATE_URL` | `CONFIG_EINKTODO_OTA_URL` | points at the **new** `/api/update/rtos/firmware` channel (R9) |
| `FILE_UPDATE_URL` | dropped (certs.ar flow removed, R4) | re-add if a file channel returns |
| `API_KEY` (dev only) | `CONFIG_EINKTODO_API_KEY` (string, default "") | dev overlay / local `sdkconfig` — never in defaults files committed for production |
| `BUTTON_PIN` | `CONFIG_EINKTODO_BUTTON_PIN` (int, default 16) | pcb-test overlay = 0 |
| `FALLBACK_BASE_URL_1/2`, `FALLBACK_TLS_FINGERPRINT_1/2` | `CONFIG_EINKTODO_FALLBACK_URL_1/2`, `CONFIG_EINKTODO_FALLBACK_PIN_1/2` | pin accepts SHA-1 (20B) or SHA-256 (32B) hex (R5) |
| `GIT_VERSION` | CMake: `set(PROJECT_VER …)` from `git describe --tags --always --dirty` in top CMakeLists → lands in `esp_app_desc_t.version`; exposed to code via `esp_ota_get_app_description()` and a generated define | feeds `X-Device-Firmware-Version` + launch screen |
| `DEBUG`, `DEBUG_UPDATE_FIREWARE`, `DEBUG_PERSISTENT_STORAGE` | `CONFIG_EINKTODO_DEBUG`, `CONFIG_EINKTODO_DEBUG_OTA`, `CONFIG_EINKTODO_DEBUG_STORE` + `CONFIG_LOG_DEFAULT_LEVEL` | |
| `E_INK_750` / `E_INK_290` / `E_INK_75Z90` / `E_INK_750_BW` / `E_INK_750_V1` | `choice EINKTODO_PANEL` in Kconfig.projbuild (sets color support `bwr`/`bw`, dims, driver variant) | one `sdkconfig` per panel replaces the platformio env-per-panel matrix |
| `ENABLE_OTA` | `CONFIG_EINKTODO_ENABLE_OTA` (bool) | |
| `ENABLE_PCB_TEST` | `CONFIG_EINKTODO_PCB_TEST` | |
| `DISPLAY_CHECK_INTERVAL_SECONDS` | `CONFIG_EINKTODO_CHECK_INTERVAL` (default 60) | |
| `WIFI_SSID`/`WIFI_PASS` (local.ini) | `CONFIG_EINKTODO_DEV_WIFI_SSID/PASS` in a git-ignored `sdkconfig.defaults.local` | preserves the "local.ini stays local" convention |

Env builds: `idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.production" build` (likewise preview / development / pcb-test). Shared `sdkconfig.defaults` pins: partition CSV, 115200 console, 160 MHz CPU, mbedTLS dynamic buffers (+`CONFIG_MBEDTLS_V3` + `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` custom list per R4 tier 1), `CONFIG_TASK_WDT=y`. CI: new workflow building the production overlay on `rtos-v*` tags, uploading `firmware-rtos.bin` + sha256 (R9) — `tag.yml` for the Arduino line is untouched.

### R18. Misc small ports

- **`ESP8266mDNS.h` include** (unused) — delete; SDK `mdns` component exists if ever wanted.
- **ArduinoJson** — gone with the JSON files (R8). If JSON is ever needed, SDK ships `cJSON` (`json` component).
- **`String` globals** (`pendingDeviceEvent`, `DeviceID`) — fixed `char[]` owned per §0 task rules; `DeviceID` computed once before tasks start.
- **LED (`led.h`, dead code)** — `gpio` wrappers, still unwired (parity #12).
- **PCB test screen** — same info via `esp_chip_info`/`esp_get_free_heap_size`/`esp_wifi_get_mac`; the 5-minute block becomes `vTaskDelay`.
- **`isReachableHttpCode` / endpoint candidate structs** — direct C port (R5).
- **`X-Device-Colors`** — from the panel Kconfig choice (`bwr` iff 750 BWR variant), parity #28.

---

## 2. Residual / open risks (track during bring-up)

1. **esp_http_client/tcp_transport local patch (R4/R5)** — the only SDK-source-touching piece; isolate as project components with an upstream-diff file checked in, so SDK minor bumps are re-appliable.
2. **mbedTLS v3 on 8266** maturity (R4 tier 1) — keep tier 2 (global CA store on mbedTLS v2) buildable behind a Kconfig until tier 1 has a week of soak.
3. **HSPI hardware-CS framing vs panel expectations (R12)** — bit-bang fallback ready.
4. **GPIO16 read-back-in-output-mode via official driver (R3)** — register-poke port is the proven fallback.
5. **Device-identity formula (R16)** — must be bench-verified equal to `ESP.getChipId()` before any device that matters polls the server.
6. **No OTA rollback in v3.4 (R9)** — preview-channel soak is the compensating control; never pin production to an untested release.
7. **Fleet/server coexistence (R9)** — requires the new web route + release-asset naming to land in `einktodo/` before the first RTOS OTA; legacy route remains frozen.
