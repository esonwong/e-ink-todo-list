# 04 — Final RTOS Architecture: ESP8266_RTOS_SDK v3.4 Rewrite

**Status: FINAL.** Supersedes the candidate designs; built on the component-mirror design (judges' 2:1 winner), with all judge-flagged grafts applied. Baseline facts from `.migration/03-rtos-risks.md` (verified against the `release/v3.4` SDK tree and the live TLS chain, 2026-06-10). Parity target: `.migration/01-inventory.md` **section 1 only** (items 1–68); that file's section 2 risk register is obsolete (ESP32-targeted).

Target: **ESP8266_RTOS_SDK v3.4** (FreeRTOS, idf.py/CMake), **ESP8266EX / esp12e / 4MB flash**, ~80KB usable heap. New tree at `einktodo-device/rtos/`. The Arduino/PlatformIO tree stays untouched and buildable; the deployed fleet keeps OTAing from it (see §3.4 fleet coexistence).

---

## 0. Decision summary & contradiction resolutions

Architecture: **component-mirror** — one IDF component per legacy module, so every inventory item maps to a named owning file (the structure *is* the parity-audit mechanism). Two of three judges chose this over the event-loop coordinator (closest second: lowest execution-drift but coarser auditability) and the producer/consumer pipeline (most elegant memory design but deliberately rewrites the core bitmap path's observable behavior and holds TLS open during panel clock-in).

Grafts applied from the verdicts:

| # | Graft | Source | Where |
|---|---|---|---|
| G1 | `CMD_API_KEY_UPDATED{api_key[42]}` value-copied message; cycle_task is the **single writer** of the NVS `setting` namespace | judges 1+2 | §1, §2 cycle_task.c, portal flow |
| G2 | Binary `render_done_sem` replaced by `BIT_DISP_DONE` / `BIT_DISP_ABORTED` event bits; `todoLastModified` + `rememberSuccessfulEndpoint` persisted **only after DISP_DONE** | judge 2 | §1, todo_fetch |
| G3 | Render-wait timeout panel-derived (90 s for 750 BWR, not 30 s); display_task signals on **every** exit path; cycle_task timeout = fatal → log + `esp_restart()` (never proceed while display_task may still own the panel) | judge 2 | display_task contract |
| G4 | Truncation policy: received bytes verified against `Content-Length` and `X-Black/Red-Buffer-Length`; mismatch ⇒ **skip render entirely** (panel keeps previous image), no Last-Modified update | judges 1+2+3 | todo_fetch, deviation ledger D5 |
| G5 | Erase only `ceil(content_length/4096)` sectors of the bitmap partition **after** `fetch_headers` (not a full 128KB pre-GET erase) — ~24 sectors ≈ <1 s, bounded well inside the 60 s socket timeout | judge 2 (from pipeline design) | todo_fetch |
| G6 | All portal httpd URI handlers post to queues with **timeout 0** and return immediately (breaks the `httpd_stop`-vs-blocked-handler deadlock) | judge 2 | portal_httpd.c |
| G7 | Value-copy invariant: every queue payload is a fixed-size struct copied by the queue — **no pointers into task-local buffers, ever** — stated in every message-header doc comment | judge 1 | events headers |
| G8 | `rtos/README.md` carries three checked-in audit artifacts: the **event→action→file dispatch table**, the **bring-up gate list**, and the **consolidated deviation ledger** (§6) | judges 1+3 | README.md |
| G9 | `0xE0000` app-size gate is an explicit failing step in `rtos-tag.yml`, not just a doc note | judge 3 | CI |
| G10 | Host-side unit tests of the byte-level layer transforms (BWR black verbatim / BWR red NOT / mono NOT) against captured `curl` output, before hardware bring-up | judge 1 | §7 verification |
| G11 | `gpio16_rtc.c` (verbatim `lib/gpio16` register-poke port) ships buildable behind `CONFIG_EINKTODO_GPIO16_RAWREG` from day one | judge 3 | button_input |
| G12 | Driver band-write API is **panel/source-agnostic** (caller supplies a band-reader callback, not a partition handle) — zero-cost hedge that preserves the streaming-pipeline option | judge 3 | eink_display API |

Contradiction resolutions (stated reasoning):

1. **Who writes `setting/api_key`?** Judge 1 wants cycle_task to receive the key as a message; judge 2 wants NVS `setting`-namespace writes single-owner (not the httpd handler). Resolution: portal save handler → value-copied `wifi_cmd_t{WIFI_CMD_SAVE, {ssid,pass,api_key}}` posted timeout-0 to wifi_ctrl_task → wifi_ctrl_task applies `esp_wifi_set_config` + connect, and forwards `app_cmd_t{CMD_API_KEY_UPDATED, api_key}` to cycle_task → **cycle_task** writes `store_set_api_key()`, runs `cleanRunningValue()`, fetches. One writer per NVS namespace, and the `saveParams→cleanRunningValue→fetch` ordering of items 21/23 is serialized in one place.
2. **Pre-erase whole partition (winner) vs erase-what's-needed-after-headers (pipeline graft).** Adopt the graft (G5): the stall is bounded (<1 s for 96KB) and it cuts flash wear ~4× per render. The "erase must not stall TCP" concern that motivated full pre-erase is satisfied by the bound.
3. **30 s render semaphore (winner) vs worst-case BWR refresh 15–25 s+.** Adopt G3: timeout from panel Kconfig (90 s for 750 BWR), and the timeout path restarts rather than continuing — the original design's silent re-permitting of TLS+page-buffer coexistence on timeout was a real bug.
4. **Component-mirror (10 components) vs judge 3's flat-main/ preference.** Overruled 2:1; judge 3's actual concerns (audit ease, scattered gates) are answered by G8's README artifacts rather than restructuring. The component count stands because the 1:1 module map is what makes a dropped behavior structurally conspicuous.
5. **Pipeline streaming (no bitmap partition).** Rejected as the default path (2 judges: behavior drift on the core feature; TLS open during clock-in). G12 keeps the driver API shaped so a streaming source could be swapped in later without touching the driver.

---

## 1. Task table

App tasks all ≤ prio 6 and block frequently, so the idle task always runs and feeds the HW WDT (R13). One event group `sys_events`, bits: `BIT_WIFI_CONNECTED (1<<0)`, `BIT_PORTAL_ACTIVE (1<<1)`, `BIT_TIME_SYNCED (1<<2)`, `BIT_DISP_DONE (1<<3)`, `BIT_DISP_ABORTED (1<<4)`.

Message structs (all value-copied per G7; doc comment `/* VALUE-COPIED: no pointers into task-local buffers */` on each):

```c
/* main/events.h */
typedef struct {                 /* cmd_queue, depth 4, button/wifi/httpd → cycle_task */
    enum { CMD_FORCE_REFRESH, CMD_DEVICE_EVENT,
           CMD_ERASE_PVALUE, CMD_API_KEY_UPDATED } type;
    char arg[42];                /* event name ("double-click") or api key, NUL-terminated */
} app_cmd_t;

typedef struct {                 /* wifi_cmd_queue, depth 4, button/httpd → wifi_ctrl_task */
    enum { WIFI_CMD_PORTAL_START, WIFI_CMD_PORTAL_STOP, WIFI_CMD_TOGGLE,
           WIFI_CMD_RESET_AND_PORTAL, WIFI_CMD_SAVE } type;
    struct { char ssid[33]; char pass[65]; char api_key[42]; } save;  /* WIFI_CMD_SAVE only */
} wifi_cmd_t;

typedef struct {                 /* disp_queue, depth 4, any task → display_task */
    enum { DISP_LAUNCH, DISP_TEXT_CENTER, DISP_AP_INFO, DISP_WIFI_CONNECTED,
           DISP_AUTH_401, DISP_NO_CONTENT, DISP_PORTAL_TIMEOUT,
           DISP_RENDER_BITMAP, DISP_TEST, DISP_PCB_INFO } kind;
    char text[64];               /* center-text / AP info lines */
    struct { uint16_t w, h; uint32_t black_len, red_len; bool bwr; } bitmap; /* RENDER only */
} display_req_t;                 /* ~120 B */
```

| Task | Responsibility | Stack | Prio | Queues / event bits |
|---|---|---|---|---|
| **cycle_task** (`main/cycle_task.c`) | Mirror of `loop()` (items 8–11). Blocks on `xQueueReceive(cmd_queue, pdMS_TO_TICKS(remaining_until_poll))` — timeout drives the 60 s cadence gated on `time(NULL)`; messages preempt. Per cycle: `ota_update_check()` → `todo_fetch_run()`. Sole owner of: `RunningValue` (RAM-only, item 48), endpoint candidates, all `esp_http_client`/TLS objects, NVS **`setting` writes** (G1), NVS `pvalue` writes from fetch/OTA. Handles 304/401/204/200 dispatch, one-shot `X-Device-Event` (cleared only after a reachable HTTP code — deviation D3), heap logging (item 32). On 200: posts `DISP_RENDER_BITMAP`, then `xEventGroupWaitBits(BIT_DISP_DONE\|BIT_DISP_ABORTED, clear, any, pdMS_TO_TICKS(CONFIG_EINKTODO_RENDER_TIMEOUT_MS))`; persists `todoLastModified` + `rememberSuccessfulEndpoint` **only on DISP_DONE** (G2); on wait timeout: log + `esp_restart()` (G3). `esp_http_client_cleanup()` always happens **before** the render request, so the ~25KB TLS transient and the render buffers never coexist. | 8192 B | 5 | consumes `cmd_queue`; produces `disp_queue`; waits `BIT_DISP_*` |
| **display_task** (`components/eink_display/display_task.cpp`) | Sole owner of the panel — refresh is a multi-second non-reentrant critical section, serialized by single-consumer queue. Renders all screens (items 2, 7, 20, 23, 25, 30, 52, 65) and full bitmaps via band-copy from the `bitmap` partition through the source-agnostic reader (G12, items 41–44, 61–62). Allocates the 4000 B page buffer + 4000 B band buffer only while servicing a request; frees after the panel sleep sequence (item 64). Sets exactly one of `BIT_DISP_DONE`/`BIT_DISP_ABORTED` on **every** exit path, including driver errors (G3). | 4096 B | 4 | consumes `disp_queue`; sets `BIT_DISP_DONE`/`BIT_DISP_ABORTED` |
| **button_task** (`components/button_input`) | 10 ms-poll gesture FSM (items 13–17) with OneButton 2.5.0 constants (debounce 50 ms, multi-click window 400 ms, press-start 800 ms, one-shot long action at 6000 ms held; timebase `esp_timer_get_time()/1000`). GPIO16: `gpio_config` `GPIO_MODE_OUTPUT` + `gpio_set_level(16,1)`, poll `gpio_get_level` readback; `gpio16_rtc.c` register-poke fallback behind `CONFIG_EINKTODO_GPIO16_RAWREG` (G11). Pins ≠ 16: input + pull-up. Never blocks, never touches display/sockets/NVS. | 2048 B | 6 | produces `cmd_queue` + `wifi_cmd_queue`; reads `BIT_PORTAL_ACTIVE`/`BIT_WIFI_CONNECTED` to pick the single-click action |
| **wifi_ctrl_task** (`components/net_portal`) | Owns every `esp_wifi` call + portal lifecycle FSM (items 18–25): APSTA, hostname, 3×30 s connect retries, connect-failed → stop portal + `vTaskDelay(3000)` + reopen (item 24), 600 s portal one-shot `xTimer` → stop httpd/dns + `esp_wifi_stop()` + timeout screen (item 25). esp_event handlers stay tiny (set bits / post only). On GOT_IP: set `BIT_WIFI_CONNECTED`, queue "Connected to WiFi" screen, `sntp_clock_start()` (items 23, 26). On `WIFI_CMD_SAVE`: apply STA config, connect, forward `CMD_API_KEY_UPDATED` to cycle_task (G1). | 3072 B | 5 | consumes `wifi_cmd_queue`; produces `disp_queue`, `cmd_queue`; sets/clears `BIT_WIFI_CONNECTED`/`BIT_PORTAL_ACTIVE` |
| **dns_task** (`components/dns_server`, portal-only) | Captive-portal UDP DNS catcher answering every A query with the softAP IP (ported from the IDF `captive_portal` example, pure BSD sockets). Created on portal start, deleted on stop/timeout. | 2048 B | 4 | none (self-contained socket loop) |
| **httpd** (SDK `esp_http_server` internal task, portal-only) | Portal UI: SSID scan list, password + 41-char apiKey form, info/restart/exit actions, catch-all 302 for captive sheets. `max_open_sockets = 3`. URI handlers post `wifi_cmd_t` with **timeout 0** and return immediately (G6); they never touch display, NVS, or fetch state. | 4096 B (CONFIG_HTTPD default) | 5 | produces `wifi_cmd_queue` (timeout 0) |
| SDK system tasks (wifi, tcpip/lwIP, esp_timer, esp_event, idle) | Untouched defaults. Idle feeds the HW WDT; `CONFIG_TASK_WDT=y`; belt-and-braces `esp_task_wdt_reset()` in the OTA write loop and the page-send loop. | SDK defaults | — | — |

Resident stack cost: 8192 + 4096 + 2048 + 3072 ≈ 17.5 KB; portal adds 4096 + 2048 transiently. Queues + event group < 1 KB.

---

## 2. Component layout & file plan

```
einktodo-device/rtos/
  CMakeLists.txt
  partitions.csv
  sdkconfig.defaults                  (+ .production / .preview / .development / .pcb-test / .local.example)
  README.md
  main/
  certs/ca_roots.pem
  components/
    net_portal/  dns_server/  endpoint_fallback/  todo_fetch/  eink_display/
    button_input/  store/  ota_update/  sntp_clock/  pinned_tls/
    esp_http_client/  tcp_transport/        (patched SDK shadow copies)
  test/host/                          (host-side transform/endpoint/urlencode unit tests, G10)
```

Module map (legacy → new): `network.h`→`net_portal`(+`dns_server`), `fallbackNetwork.h`→`endpoint_fallback`, `todoList.h`→`todo_fetch`, `display.h`+`lib/display-driver/`→`eink_display`, `button.h`+`lib/gpio16/`→`button_input`, `store.h`→`store`, `lib/update/UpdateFirmware.h`→`ota_update`, `clock.h`→`sntp_clock`, `main.cpp`→`main/{app_main.c,cycle_task.c}`.

### File plan (per-file content + public signatures)

**Top level**

- `rtos/CMakeLists.txt` — idf project `einktodo-rtos`; `PROJECT_VER` from `git describe --tags --always --dirty` (→ `esp_app_desc_t.version` → `X-Device-Firmware-Version` + launch screen).
- `rtos/partitions.csv` — §3.1.
- `rtos/sdkconfig.defaults*` — §3.2/§3.3.
- `rtos/README.md` — build matrix, **dispatch table**, **bring-up gates**, **deviation ledger** (G8), fleet-coexistence warning in bold.

**main/**

- `main/CMakeLists.txt` — registers srcs; `EMBED_TXTFILES` n/a here (portal HTML lives in net_portal).
- `main/Kconfig.projbuild` — every `EINKTODO_*` knob (§3.3 table).
- `main/events.h` — `app_cmd_t`, `wifi_cmd_t` (shared), queue/event-group handles as `extern`; G7 invariant comment.
- `main/app_main.c` — boot order mirror of `setup()` (items 1–7):
  `void app_main(void)` — 2 s `vTaskDelay` settle → `store_init()` → load apiKey → `display_init()` + `DISP_LAUNCH` → `button_input_start()` → `net_portal_init()` → `cycle_task_start()`. DEBUG dump (chip info, `esp_ota_get_app_description()` version+sha256, running partition, `store_dump()`); pcb-test branch.
- `main/cycle_task.c/.h` —
  `void cycle_task_start(QueueHandle_t cmd_q);`
  internal: cadence loop, cmd dispatch (`CMD_FORCE_REFRESH`→`todo_fetch_clean_running_value()`+fetch; `CMD_DEVICE_EVENT`→stash one-shot header; `CMD_ERASE_PVALUE`→`store_erase_pvalue()`; `CMD_API_KEY_UPDATED`→`store_set_api_key()`+clean+fetch (G1)); render handshake (G2/G3).
- `main/device_id.c/.h` —
  `uint32_t device_id_get(void);` /* (mac[3]<<16)|(mac[4]<<8)|mac[5] from esp_efuse_mac_get_default(); bench gate vs ESP.getChipId() */
  `const char *device_id_str(void);` `const char *device_id_hostname(void);` /* "e-ink-todo-list-%06x" */
- `main/led.c/.h` — dead-code parity (item 12), unwired:
  `void led_init(void); void led_on(void); void led_off(void); void led_blink(uint32_t interval_ms);`
- `main/pcb_test.c/.h` — `void pcb_test_run(void);` /* DISP_PCB_INFO + 5-min vTaskDelay block (item 7) */

**components/net_portal/** (items 18–25)

- `include/net_portal.h` —
  `esp_err_t net_portal_init(QueueHandle_t wifi_cmd_q, QueueHandle_t cmd_q, QueueHandle_t disp_q, EventGroupHandle_t sys_events);`
  `BaseType_t net_portal_post(const wifi_cmd_t *cmd, TickType_t ticks);`
  `bool net_portal_wifi_saved(void);` /* esp_wifi_get_config sta.ssid[0] != 0 */
- `wifi_ctrl.c` — wifi_ctrl_task FSM, esp_event relay handlers, retry counter + 30 s attempt timer, 600 s portal timer, `esp_wifi_restore()` for reset, `tcpip_adapter_set_hostname` (v3.4 is tcpip_adapter, not esp_netif).
- `portal_httpd.c` — URI handlers: `GET /` (scan list + form), `POST /save` (41-char apiKey → `WIFI_CMD_SAVE`, timeout 0, G6), `/info`, `/restart`, `/exit`, catch-all 302. Blocking `esp_wifi_scan_start` only while portal open and no fetch in flight.
- `portal.html` — embedded via `EMBED_TXTFILES`; title "E-ink Todo List", menu [wifi, info, restart, exit].

**components/dns_server/**

- `include/dns_server.h` — `esp_err_t dns_server_start(ip4_addr_t resolve_to); void dns_server_stop(void);`
- `dns_server.c` — ported IDF captive_portal example.

**components/endpoint_fallback/** (items 34–38)

- `include/endpoint_fallback.h` —
  ```c
  typedef struct { char id[16]; char url[128]; uint8_t pin[32]; uint8_t pin_len; bool use_pin; } endpoint_candidate_t;
  int  endpoint_build_candidates(const char *primary, const char *path, endpoint_candidate_t out[3]);
  void endpoint_prioritize_last_good(endpoint_candidate_t *c, int n);   /* pvalue net_lg_ep */
  void endpoint_remember_success(const endpoint_candidate_t *c);        /* net_lg_ep + net_lg_pin */
  bool endpoint_is_reachable_code(int http_code);                       /* {200,304,204,401} */
  ```
- `endpoint_fallback.c`.

**components/todo_fetch/** (items 27–33, 41–44 download half, 66)

- `include/todo_fetch.h` —
  ```c
  typedef struct { int http_code; bool rendered; display_req_t render_req; } todo_fetch_result_t;
  esp_err_t todo_fetch_run(const char *api_key, const char *device_id,
                           const char *device_event_or_null, todo_fetch_result_t *out);
  void      todo_fetch_clean_running_value(void);
  ```
- `todo_fetch.c` — request headers (item 28), `HTTP_EVENT_ON_HEADER` capture of the 9 response headers into a fixed case-insensitive struct (`Last-Modified` ≤ 29 chars), 304/401/204/200 dispatch, **erase `ceil(content_length/4096)` sectors after `fetch_headers`** (G5), stream body in 4 KB chunks via `esp_partition_write` (4-byte aligned, final chunk padded; black at 0, red at `black_len` rounded up to 4), **length verification** (G4): bytes received must equal `Content-Length` and per-layer lengths must match `X-Black/Red-Buffer-Length` — else log, cleanup, return without posting a render and without touching `todoLastModified`. Progress % + heap logging.
- `urlencode.c/.h` — `size_t urlencode(const char *in, char *out, size_t out_len);` (item 66, `yield()` removed).

**components/eink_display/** (items 41–44 render half, 55–65)

- `include/eink_display.h` —
  ```c
  /* G12: band source is caller-supplied — driver knows nothing about partitions */
  typedef struct {
      esp_err_t (*read)(void *ctx, uint32_t layer /*0=black,1=red*/,
                        uint32_t offset, uint8_t *buf, size_t len);
      void *ctx;
  } band_source_t;
  esp_err_t  display_init(void);
  BaseType_t display_request(const display_req_t *req, TickType_t ticks);
  /* display_task internals call: */
  esp_err_t  display_render_bands(const band_source_t *src, uint16_t w, uint16_t h,
                                  uint32_t black_len, uint32_t red_len, bool bwr);
  ```
- `display_task.cpp` — queue consumer; `DISP_RENDER_BITMAP` builds a `band_source_t` over `esp_partition_read` of the `bitmap` partition; bit semantics exact: BWR black verbatim, BWR red bitwise-NOT, mono bitwise-NOT into the black pass (red pass white); honors w/h smaller than panel by row-stride copy. Sets `BIT_DISP_DONE`/`BIT_DISP_ABORTED` on every path (G3).
- `hal_epd.c` + `include/hal_epd.h` — the exact Arduino-call shim for the surface `display_750_driver.cpp` actually uses (verified inventory R12: `Serial.print*`, `pinMode`, `digitalWrite`, `digitalRead`, `delay`, SPI begin/transaction/transfer/end, `String` overload, `pgm_read_byte`, `ESP.getFreeHeap`):
  ```c
  void epd_pin_init(void);                        /* RST=2, DC=4, BUSY=5 (+CS if bitbang) */
  void epd_write(uint8_t pin, uint8_t level);     /* gpio_set_level */
  int  epd_read(uint8_t pin);                     /* gpio_get_level */
  void epd_delay_ms(uint32_t ms);                 /* vTaskDelay >=10ms, ets_delay_us below */
  void epd_spi_init(void);                        /* HSPI: SCK=14 MOSI=13 CS=15, MODE0, MSB, 10MHz */
  void epd_spi_send(const uint8_t *buf, size_t n);/* chunked <=64 B per spi_trans (v3.4 limit) */
  void epd_spi_end(void);
  ```
  `ESP_LOGI` replaces Serial; `String` overload deleted; bit-bang fallback behind `CONFIG_EINKTODO_EPD_BITBANG`.
- `base_display_driver.cpp/.h`, `display_750_driver.cpp/.h`, `display_290_driver.cpp/.h` — ported: ~30 call-site edits onto `hal_epd`; init/refresh/sleep command sequences, page math, `waitUntilIdle` (200 ms then 100 ms `vTaskDelay` polls — WDT-safe) **byte-identical** (items 56–62, 64); `testDisplay()` kept (65).
- `flash_font.h` — `PROGMEM` → `__attribute__((section(".irom0.text"), aligned(4)))`; `pgm_read_byte` → aligned 32-bit-read-and-extract macro (**mandatory** — byte loads from ICACHE-mapped flash fault on ESP8266).
- `fonts.h`, `font12.cpp`, `font20.cpp`, `font24.cpp` — copied **byte-identical** from `lib/display-driver/` (item 63); ~15 KB moves DRAM→irom (verify in `.map`).
- `CMakeLists.txt` — C++ component; font objects forced into irom.

**components/button_input/** (items 13–17)

- `include/button_input.h` —
  `esp_err_t button_input_start(QueueHandle_t cmd_q, QueueHandle_t wifi_q, EventGroupHandle_t sys_events);`
- `button_input.c` — poll FSM + gesture dispatch (1-click portal-stop / force-refresh / portal-start by event bits; 2→`CMD_DEVICE_EVENT("double-click")`; 4→`esp_restart()`; 5→`CMD_ERASE_PVALUE`; 6→`WIFI_CMD_TOGGLE`; 6 s hold→`CMD_ERASE_PVALUE`+`WIFI_CMD_RESET_AND_PORTAL`, once per press).
- `gpio16_rtc.c` — verbatim `lib/gpio16` register-poke port (`WRITE_PERI_REG`/`RTC_GPIO_*` exist in v3.4 `eagle_soc.h`), behind `CONFIG_EINKTODO_GPIO16_RAWREG` (G11).

**components/store/** (items 45–49) — NVS only, no task (NVS is internally thread-safe; namespace-writer discipline per G1)

- `include/store.h` —
  ```c
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
  void      store_dump(void);                       /* CONFIG_EINKTODO_DEBUG_STORE: nvs_entry_find */
  ```
- `store.c` — keys (15-char NVS limit forces renames): `net_lg_ep`, `net_lg_pin`, `fw_last_check`, `pcb_test`, debug keys.

**components/ota_update/** (items 51–54) — no own task; runs on cycle_task (fetch/OTA serialization by construction)

- `include/ota_update.h` —
  `esp_err_t ota_update_check(const endpoint_candidate_t *cands, int n, bool force);`
- `ota_update.c` — throttle (10 d / 60 s under `CONFIG_EINKTODO_DEBUG_OTA` via `fw_last_check`); GET with `X-Device-Firmware-Version`; 304→no-update; 200→`DISP_TEXT_CENTER("Updating Firmware")` → `esp_ota_begin(esp_ota_get_next_update_partition(NULL), OTA_SIZE_UNKNOWN)` → chunked `esp_ota_write` (%-on-change log, `esp_task_wdt_reset`) → `esp_ota_end` (image validation) → `esp_ota_set_boot_partition` → "Updated Firmware Success" + 2 s + `esp_restart()`. `UpdateFiles.h`/certs.ar flow **not ported** (deviation D4).

**components/sntp_clock/** (item 26) — no task

- `include/sntp_clock.h` — `void sntp_clock_start(void);`
- `sntp_clock.c` — `SNTP_OPMODE_POLL`, servers `time.asia.apple.com` / `cn.pool.ntp.org` / `pool.ntp.org` (verify `CONFIG_LWIP_SNTP_MAX_SERVERS ≥ 3`), `setenv("TZ","CST-8",1)+tzset()`, `sntp_set_time_sync_notification_cb` → `BIT_TIME_SYNCED`. The 100×1 s blocking poll is replaced by the event bit; pre-sync epoch tolerated as today.

**components/pinned_tls/** (item 40)

- `include/pinned_tls.h` —
  ```c
  esp_err_t pinned_tls_attach(void *mbedtls_conf);      /* signature-compatible with crt_bundle_attach */
  void      pinned_tls_set(const uint8_t *hash, size_t len);  /* 20=SHA-1, 32=SHA-256 */
  ```
- `pinned_tls.c` — `mbedtls_ssl_conf_authmode(VERIFY_REQUIRED)` + `mbedtls_ssl_conf_verify(cb)`: at depth==0 hash `crt->raw.p/raw.len`, constant-time compare; match → `*flags = 0` (clears UNTRUSTED + CN-mismatch + EXPIRED/FUTURE — BearSSL fingerprint-mode parity, keeps the IP fallback usable pre-NTP); mismatch → `MBEDTLS_X509_BADCERT_OTHER` + nonzero; depth>0 → return 0. Single static pin context (cycle_task serializes all TLS), assert-guarded.

**components/esp_http_client/**, **components/tcp_transport/** — project-local shadow copies of the v3.4 SDK components (+ `upstream.diff` each): add the `crt_bundle_attach` passthrough field to `esp_http_client_config_t` and `esp_transport_ssl_crt_bundle_attach()` wiring to `esp_tls_cfg_t.crt_bundle_attach` (field verified honored by v3.4 `esp_tls_mbedtls.c`). ~30 lines total; this one hook serves both the CA bundle and `pinned_tls_attach`.

**certs/** — `ca_roots.pem`: curated input for `gen_crt_bundle.py` — GTS Root R1–R4, GlobalSign Root CA, ISRG Root X1/X2, SSL.com RSA+ECC (~15 KB flash). Live chain 2026-06-10: `einktodo.com ← GTS WE1 ← GTS Root R4 ← GlobalSign Root CA`; Cloudflare rotates issuers among GTS/ISRG/SSL.com.

**test/host/** (G10) — `transform_test.c` (BWR black/red/mono transforms vs `curl`-captured fixtures), `endpoint_test.c` (candidate build/prioritize/remember), `urlencode_test.c`. Plain-C, compiled with the host cc, run by `make -C rtos/test/host`.

**CI** — `einktodo-device/.github/workflows/rtos-tag.yml`: tags `rtos-v*` → host tests → `idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.production" build` → **explicit size-gate step failing if app .bin > 0xE0000** (G9) → upload `firmware-rtos.bin` + `firmware-rtos.sha256` to the GitHub Release. Existing `tag.yml` untouched.

---

## 3. Partitions, sdkconfig, build knobs

### 3.1 partitions.csv (4 MB dual-OTA)

```
# Name,     Type, SubType,  Offset,   Size
nvs,        data, nvs,      0x9000,   0x4000
otadata,    data, ota,      0xd000,   0x2000
phy_init,   data, phy,      0xf000,   0x1000
ota_0,      app,  ota_0,    0x10000,  0xF0000
ota_1,      app,  ota_1,    0x110000, 0xF0000
bitmap,     data, 0x40,     0x200000, 0x20000
# ~1.8 MB spare above 0x220000
```

Each app slot sits at the same offset inside its own 1 MB block and never crosses a 1 MB boundary (ESP8266 1 MB cache-window constraint; mirrors stock `partitions_two_ota.csv`). `CONFIG_ESP8266_BOOT_COPY_APP` is marked "not released" in v3.4 — never relied on. `bitmap` (128 KB raw) replaces `/todo.bitmap` + `/todo_red.bitmap`.

### 3.2 sdkconfig.defaults essentials (shared baseline)

```
CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200        # v3.4 default is 74880 — must override
CONFIG_ESP8266_DEFAULT_CPU_FREQ_160=y          # halves the ~2-4 s soft-P256 handshake
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_MBEDTLS_V3=y                            # tier-1 TLS; tier-2 (v2 + global CA store) Kconfig-selectable
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y            # custom list from certs/ca_roots.pem
CONFIG_MBEDTLS_DYNAMIC_BUFFER=y                # all three mandatory: static 16+16 KB SSL buffers OOM
CONFIG_MBEDTLS_DYNAMIC_FREE_PEER_CERT=y
CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA=y
CONFIG_TASK_WDT=y
```

### 3.3 Kconfig / CMake mapping (every build knob)

All app knobs live in `main/Kconfig.projbuild` under menu "E-ink Todo".

| Arduino `-D` flag | RTOS knob | Default / notes |
|---|---|---|
| `DEFAULT_API_URL` | `CONFIG_EINKTODO_API_URL` (string) | per-env overlay |
| `FIRMWARE_UPDATE_URL` | `CONFIG_EINKTODO_OTA_URL` (string) | points at the **new** `/api/update/rtos/firmware` channel |
| `FILE_UPDATE_URL` | **dropped** (certs.ar flow retired, deviation D4) | re-add if a file channel returns |
| `API_KEY` | `CONFIG_EINKTODO_API_KEY` (string, default "") | dev only — set in git-ignored `sdkconfig.defaults.local`, never in committed prod defaults |
| `BUTTON_PIN` | `CONFIG_EINKTODO_BUTTON_PIN` (int, default **16**) | pcb-test overlay = 0 |
| `FALLBACK_BASE_URL_1/2` | `CONFIG_EINKTODO_FALLBACK_URL_1/2` (string, default "") | |
| `FALLBACK_TLS_FINGERPRINT_1/2` | `CONFIG_EINKTODO_FALLBACK_PIN_1/2` (hex string) | 40 hex chars = SHA-1, 64 = SHA-256 (length disambiguates) |
| `GIT_VERSION` | CMake `PROJECT_VER` from `git describe --tags --always --dirty` | → `esp_app_desc_t.version` → header + launch screen |
| `DEBUG` | `CONFIG_EINKTODO_DEBUG` + `CONFIG_LOG_DEFAULT_LEVEL` | |
| `DEBUG_UPDATE_FIREWARE` | `CONFIG_EINKTODO_DEBUG_OTA` (60 s OTA throttle) | |
| `DEBUG_PERSISTENT_STORAGE` | `CONFIG_EINKTODO_DEBUG_STORE` | |
| `E_INK_750`/`E_INK_290`/`E_INK_75Z90`/`E_INK_750_BW`/`E_INK_750_V1` | `choice EINKTODO_PANEL` {750_BWR, 750_BW, 750_V1, 75Z90, 290} | sets dims, `X-Device-Colors` (bwr/bw), driver variant, `CONFIG_EINKTODO_RENDER_TIMEOUT_MS` (90000 for BWR, 60000 for BW) |
| `ENABLE_OTA` | `CONFIG_EINKTODO_ENABLE_OTA` (bool) | |
| `ENABLE_PCB_TEST` | `CONFIG_EINKTODO_PCB_TEST` (bool) | |
| `DISPLAY_CHECK_INTERVAL_SECONDS` | `CONFIG_EINKTODO_CHECK_INTERVAL` (int, default 60) | |
| `WIFI_SSID`/`WIFI_PASS` (local.ini) | `CONFIG_EINKTODO_DEV_WIFI_SSID/PASS` | git-ignored `sdkconfig.defaults.local`; template `sdkconfig.defaults.local.example` checked in (G12-adjacent, judge-3 graft) |
| — (new) | `CONFIG_EINKTODO_TLS_INSECURE` (bool, default n) | debug-only bring-up knob; replaces the old "0 certs → insecure" branch |
| — (new) | `CONFIG_EINKTODO_EPD_BITBANG`, `CONFIG_EINKTODO_GPIO16_RAWREG` | bring-up fallback gates |

Env matrix (item 67), built as `idf.py -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.<env>" build`:

| Overlay | Contents |
|---|---|
| `.production` | `EINKTODO_API_URL="https://www.einktodo.com"`, `OTA_URL=".../api/update/rtos/firmware"`, `ENABLE_OTA=y`, log WARN |
| `.preview` | `preview.einktodo.com`, `EINKTODO_DEBUG=y`, `EINKTODO_DEBUG_OTA=y` (60 s OTA soak channel) |
| `.development` | `dev.einktodo.com`, `DEBUG=y`, OTA off; pairs with `sdkconfig.defaults.local` |
| `.pcb-test` | `EINKTODO_PCB_TEST=y`, `EINKTODO_BUTTON_PIN=0` |

### 3.4 Storage, TLS, OTA decisions (binding)

- **Storage: NVS only.** No SPIFFS, no LittleFS port. Namespaces: `setting` {`api_key`} and `pvalue` {`net_lg_ep`, `net_lg_pin`, `fw_last_check`, `pcb_test`, debug keys}. Gestures: 5-click → `nvs_erase_all(pvalue)`; 6 s hold → same + `esp_wifi_restore()` + portal. `api_key` survives both — exact `removePersistentValue()` parity. Recovery: `nvs_flash_erase` + retry on `NO_FREE_PAGES`/`NEW_VERSION_FOUND`. WiFi creds in the WiFi stack's own NVS (`WIFI_STORAGE_FLASH`). No migration from the Arduino LittleFS (different layout; first boot loses only endpoint cache + throttle timestamps).
- **TLS: two-tier.** Tier 1 (default): mbedTLS v3 + curated crt bundle (~15 KB flash, ~0 resident RAM) through the patched attach hook. Tier 2 (Kconfig fallback if v3 misbehaves on 8266): mbedTLS v2 + `esp_tls_set_global_ca_store` of the same PEMs (8–12 KB resident, no SDK patch). Fingerprint pinning for IP fallbacks via `pinned_tls` on the same hook. CA refresh rides firmware OTA (roots live decades; 10-day check cadence).
- **OTA: manual `esp_ota_*` loop** (v3.4 `esp_https_ota` has no custom-header or progress hook), symmetric dual slots, no rollback in v3.4 — compensated by mandatory preview-channel soak before any production pin bump. **Fleet coexistence (binding for the `einktodo/` repo, out of code scope here):** the Arduino fleet's `GET /api/update/firmware` contract (`x-ESP8266-sketch-md5` vs the release `md5sum` asset, per `app/api/update/firmware/route.ts`) plus `/api/update/certs.ar` and the `firmware.bin`/`md5sum` asset names stay **byte-for-byte frozen forever**. The RTOS line gets a NEW route `GET /api/update/rtos/firmware`, negotiating on `X-Device-Firmware-Version` vs a separately pinned `rtos-v*` release, serving `firmware-rtos.bin` (+`.sha256`). Distinct URL + distinct asset names + distinct tag prefixes are the triple guard against cross-flashing a brick; `esp_ota_end`'s image-magic check rejects an Arduino .bin as defense in depth. **The web route must merge before the first RTOS OTA is attempted.**

### 3.5 Memory budget

Heap (~80 KB usable after WiFi up): resident = stacks ~17.5 KB + queues <1 KB + CA 0 KB (flash) + fonts 0 KB (irom). Peak transient per cycle: mbedTLS handshake 20–30 KB (dynamic buffers) + http client 1–2 KB + 4 KB stream buffer; bitmap body **never heap-resident**. `esp_http_client_cleanup()` strictly before the render request; display buffers (4000 B page + 4000 B band) only after. Worst case ≈ 55 KB during handshake → >20 KB headroom. Portal transient ~8 KB, never concurrent with fetch. **Acceptance bar: `esp_get_minimum_free_heap_size() > 8 KB`** across handshake + 96 KB BWR download + OTA check in one cycle. Flash: app estimate 600–700 KB vs 960 KB slot; CI gate 0xE0000.

---

## 4. Risk-register resolution table

Every entry of `.migration/03-rtos-risks.md` → concrete code-level mitigation:

| Risk | Resolution (owning files) |
|---|---|
| **R1** WiFiManager gone | Hand-rolled APSTA portal: `net_portal/wifi_ctrl.c` FSM + `portal_httpd.c` + `dns_server.c` + WiFi-NVS creds + `esp_wifi_restore()`; all five callbacks/timings mapped (items 18–25); `max_open_sockets=3`; handlers post timeout-0 (G6) |
| **R2** OneButton timing | `button_input.c` 10 ms poll FSM, OneButton 2.5.0 constants (50/400/800 ms + 6000 ms one-shot); bench gesture table incl. 2-click-suppresses-1-click |
| **R3** GPIO16 no pull-up/IRQ | Primary: `driver/gpio` OUTPUT-driving-HIGH + `gpio_get_level` readback (same physics as `lib/gpio16`); fallback `button_input/gpio16_rtc.c` verbatim register-poke behind `CONFIG_EINKTODO_GPIO16_RAWREG`, buildable day one (G11); polling-only matches today |
| **R4** BearSSL CertStore gone | Tier-1 curated crt bundle (`certs/ca_roots.pem` → `gen_crt_bundle.py`) under `CONFIG_MBEDTLS_V3`; tier-2 mbedTLS-v2 global CA store behind Kconfig until a week of soak; certs.ar flow retired (D4); `/api/update/certs.ar` frozen fleet-only |
| **R5** No mbedTLS fingerprint API | `components/pinned_tls`: `mbedtls_ssl_conf_verify` leaf-DER hash callback via the same patched attach hook; clears EXPIRED/FUTURE/CN flags on match (BearSSL parity, pre-NTP usability); unit-tested correct-pin/flipped-byte |
| **R6** ESP8266HTTPClient gone | `todo_fetch.c`: `esp_http_client` open/fetch_headers/read, 60 s timeout, 9-header `ON_HEADER` capture (case-insensitive, fixed buffers), Content-Length ≤0 → next candidate, cleanup between candidates; X-Device-Event cleared after reachable code (D3) |
| **R7** 96 KB BWR vs 80 KB heap | Stream to raw `bitmap` partition in 4 KB aligned chunks; **erase only needed sectors after headers** (G5); **length-verified render** (G4); band-copy render 4000 B/page via source-agnostic reader (G12); exact bit semantics (black verbatim / red NOT / mono NOT); host transform tests (G10) + esptool byte-compare |
| **R8** LittleFS JSON gone | `store.c` NVS two namespaces, ≤15-char key renames, `nvs_erase_all(pvalue)` gestures, `setting` untouched by gestures, erase-retry recovery; NVS journal covers power-pull |
| **R9** OTA contract / fleet brick | Separate `/api/update/rtos/firmware` channel + `rtos-v*` tags + `firmware-rtos.bin/.sha256`; legacy route/assets frozen; manual `esp_ota_*` loop in `ota_update.c`; negative test: RTOS device at legacy URL must fail image-magic |
| **R10** 1 MB cache window | Symmetric `ota_0@0x10000` / `ota_1@0x110000` both 0xF0000; never `BOOT_COPY_APP`; CI size gate 0xE0000 (G9); boot-from-both-slots check |
| **R11** loop()/delay()/millis() | `cycle_task.c` queue-timeout cadence gated on `time(NULL)`; `vTaskDelay` ≥10 ms / `ets_delay_us` below; `esp_timer_get_time()/1000` int64 (kills 49-day wrap); `goToSleep` stays deleted (D2) |
| **R12** Driver Arduino surface | `hal_epd.c` shim over the verified exact call list; HSPI ≤64 B/trans chunking; `CONFIG_EINKTODO_EPD_BITBANG` fallback; fonts → `.irom0.text` + mandatory aligned-word accessor; command sequences byte-identical; `testDisplay()` kept |
| **R13** Task WDT | `CONFIG_TASK_WDT=y`; BUSY-wait keeps 100 ms `vTaskDelay` polls; `esp_task_wdt_reset()` in OTA-write + page-send loops; app prios ≤6 with frequent blocking so idle feeds the HW WDT; 48 h soak + deliberate idle-starve test build |
| **R14** Heap/fragmentation | mbedTLS dynamic buffers (3 flags) mandatory; one TLS connection ever; cleanup-before-render enforced by the G2/G3 handshake; fixed `snprintf` buffers; min-free >8 KB gate; 160 MHz; 7-day soak |
| **R15** NTP | `sntp_clock.c`: 3 servers (verify `CONFIG_LWIP_SNTP_MAX_SERVERS ≥3`), `TZ=CST-8`, sync-notification → `BIT_TIME_SYNCED`; pre-sync fetch unconditional as today; pinned fallback works pre-NTP (R5 clears validity flags); boot-order dependency documented in README |
| **R16** Console & identity | `CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200` explicit; 2 s settle kept; version+elf-sha256+running-partition replace sketch-MD5; **bench gate**: `device_id_get()` must equal Arduino `ESP.getChipId()` on one unit before any server-visible run |
| **R17** Build knobs | Full Kconfig map §3.3; `FILE_UPDATE_URL` consciously dropped; overlays per env; secrets in git-ignored `sdkconfig.defaults.local` (+ `.example`); new `rtos-tag.yml`, Arduino `tag.yml` untouched |
| **R18** Misc | mDNS include deleted; no ArduinoJson (cJSON available); `String` globals → fixed `char[]` with task ownership; `led.c` unwired parity; PCB-test via `esp_chip_info`/`esp_wifi_get_mac` + 5-min `vTaskDelay`; urlencode plain C; `X-Device-Colors` from panel Kconfig |
| **Residual 1** SDK patch drift | Shadow components + checked-in `upstream.diff`; the one ~30-line hook serves both bundle and pinning |
| **Residual 2** mbedTLS v3 maturity | Tier-2 stays buildable behind Kconfig until 1-week soak |
| **Residual 3** HSPI CS framing | `CONFIG_EINKTODO_EPD_BITBANG` gate decided at bring-up |
| **Residual 4** GPIO16 readback | `gpio16_rtc.c` one Kconfig flip away (G11) |
| **Residual 5** Device identity | README bring-up gate, blocks server-visible testing |
| **Residual 6** No OTA rollback | Preview-channel soak mandatory before production pin bump |
| **Residual 7** Server coexistence | New web route is a hard prerequisite for the first RTOS OTA; tracked in README |

---

## 5. Feature-ownership matrix (parity-audit driver)

| Item(s) | Behavior | Owner (file / task) |
|---|---|---|
| 1 | 115200 + 2000 ms settle | `sdkconfig.defaults` + `main/app_main.c` |
| 2 | Launch screen (version, DEBUG marker) | `app_main.c` → `eink_display/display_task.cpp` (`DISP_LAUNCH`) |
| 3 | DEBUG boot dump | `app_main.c` (+ `store_dump()`) — sketch-MD5 → version+sha256+partition (D1) |
| 4 | DeviceID / X-Device-Id / hostname | `main/device_id.c` (bench gate R16) |
| 5 | Boot order | `app_main.c` |
| 6 | FS-mount recovery | `store/store.c` (`nvs_flash_erase` retry) |
| 7 | PCB-test mode | `main/pcb_test.c` + `DISP_PCB_INFO` |
| 8–11 | Loop cadence, `updating` gate, check-time semantics | `main/cycle_task.c` (single consumer = re-entrancy impossible) |
| 12 | LED dead-code parity | `main/led.c` (unwired) |
| 13–17 | Button gestures incl. GPIO16 + BUTTON_PIN knob | `components/button_input` (`button_input.c`, `gpio16_rtc.c`) |
| 18 | Hostname | `net_portal/wifi_ctrl.c` + `device_id.c` |
| 19 | Portal config (non-blocking, 600 s, 30 s×3, menu, title) | `net_portal/wifi_ctrl.c` + `portal.html` |
| 20 | AP creds + AP-info screen + captive | `net_portal` + `dns_server` + `DISP_AP_INFO` |
| 21 | 41-char apiKey param + save flow | `portal_httpd.c` → `WIFI_CMD_SAVE` → `CMD_API_KEY_UPDATED` → `cycle_task.c` (G1) |
| 22 | autoConnect / saved-creds check / dev preload | `net_portal_wifi_saved()` + `CONFIG_EINKTODO_DEV_WIFI_*` |
| 23 | trying-flag, "Connected to WiFi", setClock | `wifi_ctrl.c` (GOT_IP → screen + `sntp_clock_start()`) |
| 24 | Connect-failed: stop, 3000 ms, reopen | `wifi_ctrl.c` |
| 25 | Portal timeout: WIFI_OFF + screen | `wifi_ctrl.c` (xTimer) + `DISP_PORTAL_TIMEOUT` |
| 26 | NTP servers/TZ/poll | `components/sntp_clock` |
| 27 | GET /api/display/v3 w/h urlencoded | `todo_fetch.c` + `urlencode.c` |
| 28 | Request headers incl. one-shot X-Device-Event | `todo_fetch.c` (timing fix = D3) |
| 29 | 9 response headers collected | `todo_fetch.c` (`ON_HEADER` struct) |
| 30 | 304/401/204/200/other dispatch | `todo_fetch.c` + `cycle_task.c` + `DISP_AUTH_401`/`DISP_NO_CONTENT` |
| 31 | 60 s timeouts | `todo_fetch.c` (`.timeout_ms=60000`) |
| 32 | Heap logging around TLS | `todo_fetch.c` |
| 33 | delay(50) before render | superseded by the G2/G3 queue+event-bit handshake (documented mapping) |
| 34–38 | Endpoint candidates / prioritize / remember / reachable codes / shared by fetch+OTA | `components/endpoint_fallback` (used by `todo_fetch.c`, `ota_update.c`) |
| 39 | CA validation (+ old insecure branch) | crt bundle (`certs/` + sdkconfig); insecure branch removed → `CONFIG_EINKTODO_TLS_INSECURE` debug knob (D6) |
| 40 | Fingerprint pinning | `components/pinned_tls` |
| 41 | BWR/mono negotiation | `todo_fetch.c` |
| 42–43 | Streaming + bit semantics + split layers | download: `todo_fetch.c` (partition); render: `eink_display/display_task.cpp` (band copy, G4/G12) |
| 44 | Progress % + render duration logs | `todo_fetch.c` + `display_task.cpp` |
| 45 | setting.json apiKey | `store.c` ns `setting` |
| 46 | persistentValue.json keys + remove | `store.c` ns `pvalue` + `store_erase_pvalue()` |
| 47 | FS begin/end churn → mount-once | `store_init()` (NVS, init-once) |
| 48 | RunningValue RAM-only | `cycle_task.c` private |
| 49 | DEBUG store dump | `store_dump()` |
| 50 | updateFiles/certs.ar | **NOT PORTED** — deviation D4 |
| 51–53 | OTA throttle / screens / candidate loop | `components/ota_update` |
| 54 | Release pipeline parity | `.github/workflows/rtos-tag.yml` (`rtos-v*`) |
| 55–62, 64 | Panel variants, SPI, pins, reset, init/refresh/sleep sequences, drawPixel | `eink_display/{display_750_driver,display_290_driver,base_display_driver}.cpp` + `hal_epd.c` (byte-identical sequences) |
| 63 | Fonts via pgm_read_byte | `eink_display/{flash_font.h,font12,font20,font24}.cpp` (byte-identical data, irom section) |
| 65 | testDisplay | `display_task.cpp` (`DISP_TEST`) |
| 66 | urlencode | `todo_fetch/urlencode.c` |
| 67 | Build env matrix + GIT_VERSION | `Kconfig.projbuild` + overlays + `CMakeLists.txt` + `rtos-tag.yml` |
| 68 | goToSleep dead code | **deleted, never reactivated** — deviation D2 |

---

## 6. Deviation ledger (consolidated — copied into rtos/README.md, G8)

The closed set of intentional behavior changes vs the Arduino tree. Anything not listed here must be bit-for-bit parity.

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

---

## 7. Ordered implementation steps

Each step sized for one focused agent, with exclusive file ownership. Steps marked ∥ can run in parallel within their phase once the preceding phase merged.

**Phase 0 — scaffold (sequential, blocks everything)**

1. **Scaffold**: `rtos/CMakeLists.txt`, `partitions.csv`, all `sdkconfig.defaults*` (+ `.local.example`), `main/CMakeLists.txt`, `main/Kconfig.projbuild` (all knobs), `main/events.h`, stub `app_main.c` printing version at 115200. Gate: `idf.py build` green for all four overlays.

**Phase 1 — leaf components (all ∥, depend only on step 1)**

2. ∥ **store**: `components/store/*` + erase-retry + dump. Owner: `components/store/`.
3. ∥ **endpoint_fallback** + **urlencode** + host tests for both. Owner: `components/endpoint_fallback/`, `components/todo_fetch/urlencode.c`, `test/host/{endpoint,urlencode}_test.c`.
4. ∥ **button_input**: poll FSM + GPIO16 paths (`gpio16_rtc.c` buildable, G11). Owner: `components/button_input/`.
5. ∥ **eink_display**: `hal_epd` shim, driver ports (byte-identical sequences), fonts→irom + aligned accessor, `display_task.cpp` with `band_source_t` API (G12), DONE/ABORTED bits on every exit (G3). Owner: `components/eink_display/`. Largest step; may split shim/driver vs task if needed.
6. ∥ **dns_server** + **net_portal**: wifi_ctrl FSM, portal httpd (timeout-0 posts, G6), `portal.html`. Owner: `components/{dns_server,net_portal}/`.
7. ∥ **sntp_clock** + **device_id** + **led** + **pcb_test**. Owner: `components/sntp_clock/`, `main/{device_id,led,pcb_test}.c`.
8. ∥ **TLS plumbing**: shadow `esp_http_client` + `tcp_transport` copies + `upstream.diff`s, `pinned_tls`, `certs/ca_roots.pem`, tier-2 Kconfig path. Owner: `components/{esp_http_client,tcp_transport,pinned_tls}/`, `certs/`.

**Phase 2 — data plane (sequential after 2,3,5,8)**

9. **todo_fetch**: headers, dispatch, sector-erase-after-headers (G5), stream-to-partition, length verification (G4), transform host tests vs curl fixtures (G10). Owner: `components/todo_fetch/`, `test/host/transform_test.c`.
10. **ota_update**: manual esp_ota loop, throttle, screens. Owner: `components/ota_update/`. (Needs 3, 8; ∥ with 9 if fixtures are split.)

**Phase 3 — integration (sequential)**

11. **cycle_task + app_main wiring**: boot order, cadence, cmd dispatch incl. `CMD_API_KEY_UPDATED` (G1), G2/G3 render handshake, heap logging. Owner: `main/{app_main,cycle_task}.c`.
12. **Portal↔cycle integration**: WIFI_CMD_SAVE→API_KEY_UPDATED flow, single-click portal logic against event bits, end-to-end on dev board.

**Phase 4 — CI + docs (∥ with phase 3)**

13. ∥ **rtos-tag.yml**: host tests → production build → explicit 0xE0000 size gate (G9) → release assets.
14. ∥ **README.md**: build matrix, dispatch table, bring-up gates, deviation ledger (G8), fleet warning.

**Phase 5 — bring-up & soak (hardware, sequential)**

15. Bring-up gates on the bench unit (see §8), HSPI-vs-bitbang and GPIO16 decisions, 48 h WDT soak, 7-day fragmentation soak, OTA slot-cycling on the preview channel.

Cross-repo prerequisite (tracked, not part of these steps): `einktodo/` web repo adds `GET /api/update/rtos/firmware` before step 15's OTA test against a real server.

---

## 8. Verification plan

**Build gates (every PR):**
- `idf.py build` green for all four `SDKCONFIG_DEFAULTS` overlays (production/preview/development/pcb-test) and for the 750-BWR + 290 panel choices.
- Host tests (`test/host/`) pass: layer transforms vs curl-captured fixtures (G10), endpoint candidate logic, urlencode.
- `.map` check: font tables in `.irom0.text`, DRAM rodata down ~15 KB.
- App .bin ≤ 0xE0000 (CI step, G9).

**Parity audit method:** walk §5's matrix item-by-item; for each item, diff the legacy module against its owning file (the 1:1 map makes unported behavior structurally conspicuous) and tick it off in `rtos/README.md`'s dispatch table. Any behavior change must appear in the deviation ledger (§6) — the ledger is a *closed* set; an undocumented difference is a bug.

**Real-device serial verification — attached ESP8266EX dev board at `/dev/cu.usbserial-10`:**

> ⚠️ `idf.py -p /dev/cu.usbserial-10 flash` writes a different bootloader/partition map and **erases the Arduino firmware on this board**. That is acceptable for the dev board (it is not fleet hardware); restoring Arduino later = `pio run -e development -t upload` from the old tree.

Bring-up gate checklist (mirrored in README; each must pass before the next):
1. **Console**: `idf.py -p /dev/cu.usbserial-10 flash monitor` — app banner at **115200** (boot ROM chirp at 74880 is expected), 2 s settle, version string = `git describe` output.
2. **Identity gate (blocks all server-visible testing)**: flash the Arduino build once to log `ESP.getChipId()`, then the RTOS build to log `device_id_get()` — values must be equal.
3. **NVS**: store init, set/get apiKey, 5-click erases `pvalue` only (apiKey survives), power-pull mid-commit recovers.
4. **GPIO16 button**: all gestures from the README table on BUTTON_PIN=16 (1/2/4/5/6 clicks, 6 s hold fires once); no spurious reset holding the button through boot; if readback misbehaves flip `CONFIG_EINKTODO_GPIO16_RAWREG`.
5. **Display**: `testDisplay()` pattern on the 750 panel; init/refresh/sleep timings vs Arduino serial logs; HSPI-vs-bitbang decision (`CONFIG_EINKTODO_EPD_BITBANG`).
6. **Portal**: phone joins "E-ink Todo List AP", captive sheet pops (iOS + Android probes), scan/save works, AP-info → connected screens, 600 s timeout screen, power-cycle auto-connects, long-press forgets SSID.
7. **TLS/fetch**: handshake against `www.einktodo.com` + `preview.einktodo.com` + a Let's Encrypt host (proves ISRG roots for the next Cloudflare rotation); pinned fallback connects with the correct pin and refuses a flipped byte; pre-NTP fetch behaves (pinned path works, primary fails validity until sync — documented).
8. **Bitmap parity**: full BWR 200 → `esptool.py read_flash` of the `bitmap` partition byte-compares against `curl` of the same URL with the same headers; visual black/red alignment; truncation test (kill server mid-body) → no render, old image retained, next poll retries unconditionally (D5/D10).
9. **Heap**: logged min-free > 8 KB across handshake + 96 KB BWR + OTA check in one cycle; 7-day soak with a flat min-free trend.
10. **Cadence/status paths**: 304 on second poll, 401 screen with a bad key, 204 screen; forced refresh from button < 500 ms to HTTP start; 24 h cadence drift vs server `lastSeen`.
11. **WDT**: 48 h soak with button mashing during refresh/OTA; deliberate idle-starve test build confirms the WDT actually fires.
12. **OTA** (after the web route lands): preview-channel cycle — flash ota_0, server pin bump, device updates and boots from ota_1 (`esp_ota_get_running_partition()` logged), then back; power-pull mid-download leaves otadata untouched; **negative test**: RTOS device pointed at the legacy `/api/update/firmware` URL must fail image validation and not flash.
