/* display_task.cpp — sole owner of the e-ink panel (items 2,7,20,23,25,30,52,
 * 65 screens + full-bitmap render). Single-consumer queue makes panel refresh
 * (a multi-second non-reentrant critical section) re-entrancy-impossible.
 *
 * G3 (binding): exactly one of BIT_DISP_DONE / BIT_DISP_ABORTED is set on EVERY
 * exit path of a serviced DISP_RENDER_BITMAP, including driver/source errors.
 * Other screens signal nothing: a status screen's DONE left in the group (or
 * one queued ahead of the bitmap) would otherwise satisfy cycle_task's wait
 * before the bitmap ever rendered.
 * G12 (binding): full bitmaps render through a caller-supplied band_source_t —
 * the driver never sees a partition handle. DISP_RENDER_BITMAP wraps an
 * esp_partition_read of the `bitmap` partition behind that callback.
 *
 * Bit semantics (R7, mirrors legacy displayBWRToScreen / displayToScreen):
 *   - BWR black layer: server bit 0 = ink -> draw black            (verbatim)
 *   - BWR red layer:   server bit 0 = ink -> draw red              (NOT)
 *   - mono:            server bit 1 = draw black                   (NOT into black)
 */
#include "eink_display.h"
#include "hal_epd.h"

#include <string.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

/* Panel driver selection from the Kconfig panel choice. The 750 driver carries
 * the byte-identical BWR command sequences; the 290 is a stub. The other 7.5"
 * variants reuse the 750 driver for now (dims differ but the command path is
 * the same family — bring-up gate decides any per-panel divergence). */
#if defined(CONFIG_EINKTODO_PANEL_290)
#include "display_290_driver.h"
typedef DisplayDriver290 PanelDriver;
#else
#include "display_750_driver.h"
typedef DisplayDriver750 PanelDriver;
#endif

static const char *TAG = "display_task";

static PanelDriver s_panel;
static TaskHandle_t s_task = NULL;
static bool s_inited = false;

extern "C" {

/* ---- band render (G12) ------------------------------------------------- */

esp_err_t display_render_bands(const band_source_t *src, uint16_t w, uint16_t h,
                               uint32_t black_len, uint32_t red_len, bool bwr)
{
    if (src == NULL || src->read == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int64_t start = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "Render bands: w=%u h=%u black=%u red=%u bwr=%d",
             (unsigned)w, (unsigned)h, (unsigned)black_len, (unsigned)red_len, (int)bwr);

    /* The driver's display() drives 12 pages * 2 color passes; drawPixel filters
     * by currentSendingColor / currentSendingPage, so the λ may scan the whole
     * layer on each call exactly like the legacy file re-scan. We stream the
     * layer(s) in 128-byte chunks through the band source and drawPixel — the
     * same pixel walk as displayBWRToScreen / displayToScreen. */
    esp_err_t scan_err = ESP_OK;

    s_panel.display([&](BaseDisplayDriver &dd) {
        // Black layer.
        {
            uint16_t x = 0, y = 0;
            uint8_t buf[128];
            uint32_t off = 0;
            while (off < black_len) {
                uint32_t n = black_len - off;
                if (n > sizeof(buf)) n = sizeof(buf);
                if (src->read(src->ctx, 0 /*black*/, off, buf, n) != ESP_OK) {
                    scan_err = ESP_FAIL;
                    return;
                }
                for (uint32_t i = 0; i < n; i++) {
                    for (int j = 0; j < 8; j++) {
                        if (x >= w) { x = 0; y++; }
                        if (y >= h) break;
                        if (bwr) {
                            // BWR black: bit=0 means black ink ON.
                            if (!(buf[i] & (0x80 >> j)))
                                dd.drawPixel(x, y, DISPLAY_COLOR_BLACK);
                        } else {
                            // mono: bit=1 means draw black (NOT semantics).
                            if (buf[i] & (0x80 >> j))
                                dd.drawPixel(x, y, DISPLAY_COLOR_BLACK);
                        }
                        x++;
                    }
                }
                off += n;
            }
        }

        // Red layer (BWR only; mono red pass stays white).
        if (bwr && red_len > 0) {
            uint16_t x = 0, y = 0;
            uint8_t buf[128];
            uint32_t off = 0;
            while (off < red_len) {
                uint32_t n = red_len - off;
                if (n > sizeof(buf)) n = sizeof(buf);
                if (src->read(src->ctx, 1 /*red*/, off, buf, n) != ESP_OK) {
                    scan_err = ESP_FAIL;
                    return;
                }
                for (uint32_t i = 0; i < n; i++) {
                    for (int j = 0; j < 8; j++) {
                        if (x >= w) { x = 0; y++; }
                        if (y >= h) break;
                        // BWR red: bit=0 means red ink ON.
                        if (!(buf[i] & (0x80 >> j)))
                            dd.drawPixel(x, y, DISPLAY_COLOR_RED);
                        x++;
                    }
                }
                off += n;
            }
        }
    });

    ESP_LOGI(TAG, "Render bands done in %lld ms (err=%d)",
             (long long)(esp_timer_get_time() / 1000 - start), (int)scan_err);
    return scan_err;
}

} // extern "C"

/* ---- screen renderers (mirror legacy display.h / network.h / todoList.h) - */

static void render_launch(void)
{
    s_panel.display([](BaseDisplayDriver &dd) {
        const char *name = "E-Ink Todo List";
        const char *author = "by @Eson";
        uint16_t y = dd.drawString(dd.width / 2, dd.height / 5, name, &Font24, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER);
        y = dd.drawString(dd.width / 2, y + 10, author, &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER);
#ifdef CONFIG_EINKTODO_DEBUG
        dd.drawString(dd.width / 2, y + 10, "DEBUG MODE", &Font20, DISPLAY_COLOR_RED, TEXT_ALIGN_CENTER);
#endif
        // GIT_VERSION parity: PROJECT_VER lands in esp_app_desc_t.version.
        const char *version = esp_ota_get_app_description()->version;
        dd.drawString(dd.width - dd.padding, dd.height - dd.padding - Font12.Height - 10, version, &Font12, DISPLAY_COLOR_RED, TEXT_ALIGN_RIGHT);
    });
}

static void render_text_center(const char *text, sFONT *font, DisplayColor color)
{
    s_panel.display([text, font, color](BaseDisplayDriver &dd) {
        uint16_t x = dd.width / 2;
        uint16_t y = dd.height / 2;
        uint16_t w = 0;
        uint16_t h = 0;
        dd.getStringBounds(text, font, &x, &y, &w, &h);
        y = (dd.height - h) / 2;
        dd.drawString(dd.width / 2, y, text, font, color, TEXT_ALIGN_CENTER);
    });
}

static void render_401(void)
{
    s_panel.display([](BaseDisplayDriver &dd) {
        uint16_t y = dd.drawString(dd.width / 2, dd.height / 5, "API Key authorization failed!", &Font24, DISPLAY_COLOR_RED, TEXT_ALIGN_CENTER);
        dd.drawString(dd.width / 2, y + 10, "Please long press the button to enter config mode!", &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER);
    });
}

static void render_portal_timeout(void)
{
    s_panel.display([](BaseDisplayDriver &dd) {
        uint16_t y = dd.drawString(dd.width / 2, dd.height / 5, "Config portal timeout!", &Font24, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER);
        dd.drawString(dd.width / 2, y, "Press the button to restart", &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER);
    });
}

/* AP info screen: the text field carries the AP SSID / password / web-server
 * lines packed by the producer; renders the legacy configModeCallback layout.
 * The producer formats `text` as up-to-three NUL/newline-delimited lines. */
static void render_ap_info(const char *text)
{
    s_panel.display([text](BaseDisplayDriver &dd) {
        uint16_t y = dd.drawString(dd.width / 2, dd.height / 5, "Entered config mode", &Font24, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER);
        /* text may carry multiple '\n'-separated lines (SSID/password, config URL). */
        char buf[64];
        snprintf(buf, sizeof(buf), "%s", text);
        char *line = buf;
        while (line) {
            char *nl = strchr(line, '\n');
            if (nl) {
                *nl = '\0';
            }
            y = dd.drawString(dd.width / 2, y, line, &Font20, DISPLAY_COLOR_BLACK, TEXT_ALIGN_CENTER);
            line = nl ? nl + 1 : NULL;
        }
    });
}

/* DISP_RENDER_BITMAP band source over the `bitmap` data partition (subtype
 * 0x40). Black layer at offset 0, red layer at black_len rounded up to 4 (the
 * download half writes the layers there — todo_fetch). */
typedef struct {
    const esp_partition_t *part;
    uint32_t red_base; // partition offset where the red layer starts
} bitmap_ctx_t;

static esp_err_t bitmap_band_read(void *ctx, uint32_t layer, uint32_t offset,
                                  uint8_t *buf, size_t len)
{
    bitmap_ctx_t *bc = (bitmap_ctx_t *)ctx;
    uint32_t base = (layer == 0) ? 0 : bc->red_base;
    return esp_partition_read(bc->part, base + offset, buf, len);
}

static esp_err_t render_bitmap_from_partition(const display_req_t *req)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, (esp_partition_subtype_t)0x40, "bitmap");
    if (part == NULL) {
        ESP_LOGE(TAG, "bitmap partition not found");
        return ESP_FAIL;
    }
    bitmap_ctx_t bc;
    bc.part = part;
    bc.red_base = (req->bitmap.black_len + 3u) & ~3u; // 4-byte aligned, matches download
    band_source_t src;
    src.read = bitmap_band_read;
    src.ctx = &bc;
    return display_render_bands(&src, req->bitmap.w, req->bitmap.h,
                                req->bitmap.black_len, req->bitmap.red_len, req->bitmap.bwr);
}

/* ---- task ------------------------------------------------------------- */

static void display_task(void *arg)
{
    display_req_t req;
    for (;;) {
        if (xQueueReceive(disp_queue, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        esp_err_t err = ESP_OK;
        /* The display_req_t `kind` enum is unnamed and nested in the struct, so
         * in C++ its enumerators are members of display_req_t (events.h is
         * shared C/C++ and owned elsewhere — qualify rather than edit it). */
        switch (req.kind) {
        case display_req_t::DISP_LAUNCH:
            render_launch();
            break;
        case display_req_t::DISP_TEXT_CENTER:
            render_text_center(req.text, &Font20, DISPLAY_COLOR_BLACK);
            break;
        case display_req_t::DISP_AP_INFO:
            render_ap_info(req.text);
            break;
        case display_req_t::DISP_WIFI_CONNECTED:
            render_text_center("Connected to WiFi", &Font20, DISPLAY_COLOR_BLACK);
            break;
        case display_req_t::DISP_AUTH_401:
            render_401();
            break;
        case display_req_t::DISP_NO_CONTENT:
            render_text_center("No Content", &Font24, DISPLAY_COLOR_BLACK);
            break;
        case display_req_t::DISP_PORTAL_TIMEOUT:
            render_portal_timeout();
            break;
        case display_req_t::DISP_RENDER_BITMAP:
            err = render_bitmap_from_partition(&req);
            break;
        case display_req_t::DISP_TEST:
            s_panel.testDisplay();
            break;
        case display_req_t::DISP_PCB_INFO:
            render_text_center(req.text, &Font20, DISPLAY_COLOR_BLACK);
            break;
        default:
            err = ESP_ERR_INVALID_ARG;
            break;
        }

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "display request kind=%d failed (err=%d)", (int)req.kind, (int)err);
        }
        // G3: exactly one bit set on every exit path of a bitmap render.
        if (req.kind == display_req_t::DISP_RENDER_BITMAP) {
            xEventGroupSetBits(sys_events, err == ESP_OK ? BIT_DISP_DONE : BIT_DISP_ABORTED);
        }
    }
}

extern "C" esp_err_t display_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }
    if (disp_queue == NULL || sys_events == NULL) {
        ESP_LOGE(TAG, "disp_queue / sys_events not created before display_init");
        return ESP_ERR_INVALID_STATE;
    }
    // Panel pins/SPI are brought up lazily inside the driver's initialize()
    // (called from display()), matching the legacy per-render init/sleep cycle.
    BaseType_t ok = xTaskCreate(display_task, "display_task", 4096, NULL, 4, &s_task);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create display_task");
        return ESP_ERR_NO_MEM;
    }
    s_inited = true;
    return ESP_OK;
}

extern "C" BaseType_t display_request(const display_req_t *req, TickType_t ticks)
{
    if (disp_queue == NULL || req == NULL) {
        return pdFALSE;
    }
    return xQueueSend(disp_queue, req, ticks);
}
