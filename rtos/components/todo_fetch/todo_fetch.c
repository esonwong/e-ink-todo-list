/* todo_fetch.c — display fetch + bitmap download (legacy src/todoList.h
 * downloadAndDrawTodo(), inventory items 27-33, 41-44 download half).
 *
 * Walks the endpoint candidate list, issues GET /api/display/v3 with the
 * legacy headers, dispatches on 304/401/204/200/other, and on a 200 streams
 * the body straight to the raw `bitmap` partition with the exact legacy layer
 * byte semantics. The panel is never touched here (G2/D7): a 200 download
 * produces a DISP_RENDER_BITMAP request in the result; cycle_task posts it and
 * owns the BIT_DISP_DONE handshake and all NVS writes.
 */

#include "todo_fetch.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>

#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "urlencode.h"
#include "pinned_tls.h"

static const char *TAG = "todo_fetch";

/* Display path appended to the fallback base URLs (legacy
 * buildEndpointCandidates(DEFAULT_API_URL, "/api/display/v3", ...)). The
 * primary CONFIG_EINKTODO_API_URL is a base URL, so it also gets the path. */
#define DISPLAY_PATH "/api/display/v3"

/* HTTP read/write streaming buffer. Bigger than the legacy 128 B (heap is free
 * during download) and a multiple of 4 so esp_partition_write offsets stay
 * 4-byte aligned without per-call padding (R7). */
#define STREAM_BUF_SIZE 1024

/* Last-Modified ≤29 chars like legacy. */
#define LAST_MODIFIED_LEN 30

/* ------------------------------------------------------------------ */
/* RAM-only RunningValue mirror (legacy RunningValue.todoLastModified,
 * cycle_task-private). Only the If-Modified-Since field lives here; cycle_task
 * owns the write timing via the accessors. */
static char s_running_last_modified[LAST_MODIFIED_LEN] = "";

void todo_fetch_clean_running_value(void)
{
    s_running_last_modified[0] = '\0';
}

void todo_fetch_set_last_modified(const char *last_modified)
{
    if (last_modified && last_modified[0]) {
        strlcpy(s_running_last_modified, last_modified, sizeof(s_running_last_modified));
    } else {
        s_running_last_modified[0] = '\0';
    }
}

/* ------------------------------------------------------------------ */
/* Response-header capture. v3.4 esp_http_client has no response-header getter
 * (R6) — collect the 9 known headers in the ON_HEADER event into fixed
 * buffers, case-insensitively (the server may emit lowercase; the legacy
 * ESP8266HTTPClient match was case-insensitive too). */
typedef struct {
    char    content_width[16];
    char    content_height[16];
    char    api_version[16];
    char    last_modified[LAST_MODIFIED_LEN];
    char    x_data_modified[LAST_MODIFIED_LEN];
    int32_t bitmap_layers;
    int32_t black_len;
    int32_t red_len;
    char    config_last_modified[LAST_MODIFIED_LEN];
} resp_headers_t;

static void header_copy(char *dst, size_t cap, const char *src)
{
    strlcpy(dst, src ? src : "", cap);
}

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id != HTTP_EVENT_ON_HEADER) {
        return ESP_OK;
    }
    resp_headers_t *h = (resp_headers_t *)evt->user_data;
    const char *k = evt->header_key;
    const char *v = evt->header_value;
    if (!h || !k) {
        return ESP_OK;
    }

    if (strcasecmp(k, "Content-Picture-Width") == 0) {
        header_copy(h->content_width, sizeof(h->content_width), v);
    } else if (strcasecmp(k, "Content-Picture-Height") == 0) {
        header_copy(h->content_height, sizeof(h->content_height), v);
    } else if (strcasecmp(k, "API-Version") == 0) {
        header_copy(h->api_version, sizeof(h->api_version), v);
    } else if (strcasecmp(k, "Last-Modified") == 0) {
        header_copy(h->last_modified, sizeof(h->last_modified), v);
    } else if (strcasecmp(k, "X-Data-Modified") == 0) {
        header_copy(h->x_data_modified, sizeof(h->x_data_modified), v);
    } else if (strcasecmp(k, "X-Bitmap-Layers") == 0) {
        h->bitmap_layers = v ? atoi(v) : 0;
    } else if (strcasecmp(k, "X-Black-Buffer-Length") == 0) {
        h->black_len = v ? atoi(v) : 0;
    } else if (strcasecmp(k, "X-Red-Buffer-Length") == 0) {
        h->red_len = v ? atoi(v) : 0;
    } else if (strcasecmp(k, "X-Device-Config-Last-Modified") == 0) {
        header_copy(h->config_last_modified, sizeof(h->config_last_modified), v);
    }
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
/* Bitmap partition writer. Black layer at offset 0, red layer at
 * (black_len rounded up to 4) — exactly the offsets display_task reads (G12
 * contract). esp_partition_write needs 4-byte-aligned size+offset, so each
 * layer is flushed in 4-byte multiples and the final chunk is zero-padded. */
#define RED_BASE_OFFSET(black_len) (((black_len) + 3u) & ~3u)

typedef struct {
    const esp_partition_t *part;
    uint32_t base;        /* partition byte offset where this layer starts */
    uint32_t written;     /* bytes already written via esp_partition_write   */
    uint8_t  carry[4];    /* sub-word remainder from the previous chunk      */
    uint32_t carry_len;   /* 0..3                                            */
} layer_writer_t;

static void layer_writer_init(layer_writer_t *w, const esp_partition_t *part,
                              uint32_t base)
{
    w->part = part;
    w->base = base;
    w->written = 0;
    w->carry_len = 0;
}

/* Append `len` bytes; only whole 4-byte words are written, the tail is held in
 * carry until the next append or flush. */
static esp_err_t layer_writer_append(layer_writer_t *w, const uint8_t *buf,
                                     size_t len)
{
    /* Stage into a temp word-aligned buffer combining the carry with new data.
     * Process in chunks bounded by a small stack buffer. */
    static uint8_t tmp[STREAM_BUF_SIZE + 4];
    size_t total = w->carry_len + len;
    if (total > sizeof(tmp)) {
        /* Shouldn't happen: caller never appends more than STREAM_BUF_SIZE. */
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(tmp, w->carry, w->carry_len);
    memcpy(tmp + w->carry_len, buf, len);

    size_t whole = total & ~3u;       /* bytes that form complete words */
    size_t rem = total - whole;
    if (whole > 0) {
        esp_err_t err = esp_partition_write(w->part, w->base + w->written,
                                            tmp, whole);
        if (err != ESP_OK) {
            return err;
        }
        w->written += whole;
    }
    memcpy(w->carry, tmp + whole, rem);
    w->carry_len = rem;
    return ESP_OK;
}

/* Flush the held sub-word tail, zero-padding to a 4-byte boundary. */
static esp_err_t layer_writer_finish(layer_writer_t *w)
{
    if (w->carry_len == 0) {
        return ESP_OK;
    }
    uint8_t pad[4] = {0, 0, 0, 0};
    memcpy(pad, w->carry, w->carry_len);
    esp_err_t err = esp_partition_write(w->part, w->base + w->written, pad, 4);
    if (err == ESP_OK) {
        w->written += w->carry_len; /* logical bytes, not padded count */
    }
    w->carry_len = 0;
    return err;
}

/* ------------------------------------------------------------------ */
/* Build the esp_http_client config + per-candidate TLS trust. */
static esp_http_client_handle_t open_candidate(const endpoint_candidate_t *c,
                                               const char *url,
                                               resp_headers_t *hdrs)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 60000,
        .event_handler = on_http_event,
        .user_data = hdrs,
        .buffer_size = STREAM_BUF_SIZE,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
    };

#if CONFIG_EINKTODO_TLS_INSECURE
    /* DEBUG-only bring-up knob (D6): skip server verification entirely. */
    cfg.skip_cert_common_name_check = true;
#else
    if (c->use_pin) {
        /* IP fallback: leaf-fingerprint pinning replaces CA validation (R5).
         * The pin must be loaded before the client connects. */
        pinned_tls_set(c->pin, c->pin_len);
        cfg.crt_bundle_attach = pinned_tls_attach;
    } else {
        pinned_tls_clear();
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }
#endif

    return esp_http_client_init(&cfg);
}

/* ------------------------------------------------------------------ */
/* Stream a 200 body into the bitmap partition with length verification (G4).
 * On any mismatch returns ESP_FAIL and leaves *did_render false. */
static esp_err_t download_to_partition(esp_http_client_handle_t client,
                                       int content_length,
                                       const resp_headers_t *h,
                                       bool is_bwr, todo_fetch_result_t *out)
{
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "bitmap");
    if (!part) {
        ESP_LOGE(TAG, "bitmap partition not found");
        return ESP_FAIL;
    }

    uint32_t black_len = is_bwr ? (uint32_t)h->black_len : (uint32_t)content_length;
    uint32_t red_len = is_bwr ? (uint32_t)h->red_len : 0;
    uint32_t red_base = RED_BASE_OFFSET(black_len);
    uint32_t total = is_bwr ? (black_len + red_len) : black_len;

    if (total == 0 || red_base + red_len > part->size) {
        ESP_LOGE(TAG, "bitmap too large for partition (%u > %u)",
                 (unsigned)(red_base + red_len), (unsigned)part->size);
        return ESP_FAIL;
    }

    /* Erase only ceil(total/4096) sectors AFTER headers (G5): bounded <1 s,
     * keeps the TCP window from stalling. The red layer's aligned base never
     * exceeds total rounded up, so erasing ceil((red_base+red_len)/sector)
     * covers both layers. */
    uint32_t span = red_base + red_len;
    uint32_t erase_bytes = (span + (SPI_FLASH_SEC_SIZE - 1)) & ~(SPI_FLASH_SEC_SIZE - 1);
    ESP_LOGI(TAG, "Erasing %u bytes (%u sectors) of bitmap partition",
             (unsigned)erase_bytes, (unsigned)(erase_bytes / SPI_FLASH_SEC_SIZE));
    esp_err_t err = esp_partition_erase_range(part, 0, erase_bytes);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "partition erase failed: %s", esp_err_to_name(err));
        return err;
    }

    layer_writer_t black_w, red_w;
    layer_writer_init(&black_w, part, 0);
    layer_writer_init(&red_w, part, red_base);

    uint8_t buf[STREAM_BUF_SIZE];
    uint32_t received = 0;
    int last_pct = -1;

    ESP_LOGI(TAG, "Start reading response body (%u bytes, %s)",
             (unsigned)total, is_bwr ? "BWR" : "mono");

    while (received < total) {
        uint32_t remaining = total - received;
        int want = (int)(remaining < sizeof(buf) ? remaining : sizeof(buf));
        int n = esp_http_client_read(client, (char *)buf, want);
        if (n <= 0) {
            break; /* connection closed / error before all bytes arrived */
        }

        /* Split this chunk across the black/red layers at black_len, mirroring
         * the legacy split. */
        uint32_t chunk_off = received;
        size_t consumed = 0;
        if (chunk_off < black_len) {
            uint32_t to_black = black_len - chunk_off;
            if (to_black > (uint32_t)n) {
                to_black = (uint32_t)n;
            }
            err = layer_writer_append(&black_w, buf, to_black);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "black write failed: %s", esp_err_to_name(err));
                return err;
            }
            consumed = to_black;
        }
        if ((size_t)n > consumed) {
            err = layer_writer_append(&red_w, buf + consumed, (size_t)n - consumed);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "red write failed: %s", esp_err_to_name(err));
                return err;
            }
        }

        received += (uint32_t)n;
        int pct = (int)((received * 100ull) / total);
        if (pct != last_pct) {
            last_pct = pct;
            ESP_LOGI(TAG, "Download progress: %d%% , %u/%u bytes", pct,
                     (unsigned)received, (unsigned)total);
        }
        /* Feed the WDT during a long download (R13 belt-and-braces). */
        taskYIELD();
    }

    err = layer_writer_finish(&black_w);
    if (err == ESP_OK) {
        err = layer_writer_finish(&red_w);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "layer flush failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Length verification (G4/D5): received bytes must equal Content-Length and
     * each layer's logical byte count must match its header. Any mismatch ⇒
     * skip render, keep previous image, do NOT advance Last-Modified (D10). */
    if (received != total) {
        ESP_LOGW(TAG, "short download: %u/%u bytes — skip render (D5)",
                 (unsigned)received, (unsigned)total);
        return ESP_FAIL;
    }
    if ((uint32_t)content_length != total) {
        ESP_LOGW(TAG, "Content-Length %d != layer total %u — skip render (D5)",
                 content_length, (unsigned)total);
        return ESP_FAIL;
    }
    if (black_w.written != black_len || (is_bwr && red_w.written != red_len)) {
        ESP_LOGW(TAG, "layer length mismatch (black %u/%u red %u/%u) — skip render",
                 (unsigned)black_w.written, (unsigned)black_len,
                 (unsigned)red_w.written, (unsigned)red_len);
        return ESP_FAIL;
    }

    /* Build the DISP_RENDER_BITMAP request for cycle_task (G2). */
    display_req_t *r = &out->render_req;
    memset(r, 0, sizeof(*r));
    r->kind = DISP_RENDER_BITMAP;
    r->bitmap.w = (uint16_t)atoi(h->content_width);
    r->bitmap.h = (uint16_t)atoi(h->content_height);
    r->bitmap.black_len = black_len;
    r->bitmap.red_len = red_len;
    r->bitmap.bwr = is_bwr;
    out->rendered = true;

    /* Last-Modified for the caller to persist after BIT_DISP_DONE (D10). */
    const char *lm = h->last_modified[0] ? h->last_modified : h->x_data_modified;
    strlcpy(out->last_modified, lm, sizeof(out->last_modified));

    ESP_LOGI(TAG, "Downloaded %u bytes, w=%u h=%u bwr=%d last-modified=%s",
             (unsigned)received, r->bitmap.w, r->bitmap.h, is_bwr, lm);
    return ESP_OK;
}

/* ------------------------------------------------------------------ */
esp_err_t todo_fetch_run(const char *api_key, const char *device_id,
                         const char *device_event_or_null,
                         todo_fetch_result_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->http_code = 0;

    ESP_LOGI(TAG, "Free heap before fetch: %u", (unsigned)esp_get_free_heap_size());

    endpoint_candidate_t candidates[ENDPOINT_MAX_CANDIDATES];
    int count = endpoint_build_candidates(CONFIG_EINKTODO_API_URL, DISPLAY_PATH,
                                          candidates);
    endpoint_prioritize_last_good(candidates, count);

    /* device_event one-shot: pending only for the FIRST candidate that reaches
     * a server (D3 — cleared after a reachable HTTP code, not before send). */
    bool device_event_pending =
        device_event_or_null && device_event_or_null[0];

    /* Build the ?width=&height= query once (numeric, no encode needed, but go
     * through urlencode for parity / future-proofing). */
    char wbuf[8], hbuf[8];
    snprintf(wbuf, sizeof(wbuf), "%d", CONFIG_EINKTODO_PANEL_WIDTH);
    snprintf(hbuf, sizeof(hbuf), "%d", CONFIG_EINKTODO_PANEL_HEIGHT);

    for (int i = 0; i < count; i++) {
        endpoint_candidate_t *c = &candidates[i];

        char url[160];
        snprintf(url, sizeof(url), "%s?width=%s&height=%s", c->url, wbuf, hbuf);

        resp_headers_t hdrs;
        memset(&hdrs, 0, sizeof(hdrs));

        ESP_LOGI(TAG, "GET %s", url);
        ESP_LOGI(TAG, "If-Modified-Since: %s", s_running_last_modified);

        esp_http_client_handle_t client = open_candidate(c, url, &hdrs);
        if (!client) {
            ESP_LOGW(TAG, "client init failed for %s", c->id);
            continue;
        }

        /* Request headers — exact legacy set (item 28). */
        char auth[64];
        snprintf(auth, sizeof(auth), "Bearer %s", api_key ? api_key : "");
        esp_http_client_set_header(client, "If-Modified-Since", s_running_last_modified);
        esp_http_client_set_header(client, "X-If-Modified-Since", s_running_last_modified);
        esp_http_client_set_header(client, "Authorization", auth);
        esp_http_client_set_header(client, "X-Device-Id", device_id ? device_id : "");
        esp_http_client_set_header(client, "X-Device-Colors", CONFIG_EINKTODO_DEVICE_COLORS);
        /* X-Device-Firmware-Version: PROJECT_VER via esp_app_desc_t.version
         * (legacy GIT_VERSION build flag; D1 — same value the OTA channel
         * negotiates on). */
        {
            const esp_app_desc_t *desc = esp_ota_get_app_description();
            if (desc && desc->version[0]) {
                esp_http_client_set_header(client, "X-Device-Firmware-Version",
                                           desc->version);
            }
        }
        if (device_event_pending) {
            ESP_LOGI(TAG, "X-Device-Event: %s", device_event_or_null);
            esp_http_client_set_header(client, "X-Device-Event", device_event_or_null);
        }

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "open failed for %s: %s", c->id, esp_err_to_name(err));
            esp_http_client_cleanup(client);
            continue; /* transport failure — try next candidate, keep event */
        }

        int content_length = esp_http_client_fetch_headers(client);
        int http_code = esp_http_client_get_status_code(client);
        out->http_code = http_code;

        ESP_LOGI(TAG, "HTTPS GET: %d", http_code);
        ESP_LOGI(TAG, "Content-Length: %d", content_length);
        ESP_LOGI(TAG, "Free heap after headers: %u",
                 (unsigned)esp_get_free_heap_size());

        /* D3: a reachable HTTP code clears the one-shot device event. */
        if (endpoint_is_reachable_code(http_code)) {
            device_event_pending = false;
        }

        if (http_code == 304) {
            ESP_LOGI(TAG, "Not Modified");
            out->endpoint_valid = true;
            out->persist_endpoint_now = true;
            out->endpoint = *c;
            out->rendered = false;
            esp_http_client_cleanup(client);
            return ESP_OK;
        }

        if (http_code == 401) {
            ESP_LOGW(TAG, "API Key authorization failed");
            out->endpoint_valid = true;
            out->persist_endpoint_now = true;
            out->endpoint = *c;
            out->rendered = true;
            memset(&out->render_req, 0, sizeof(out->render_req));
            out->render_req.kind = DISP_AUTH_401;
            esp_http_client_cleanup(client);
            return ESP_OK;
        }

        if (http_code == 204) {
            ESP_LOGI(TAG, "No Content");
            out->endpoint_valid = true;
            out->persist_endpoint_now = true;
            out->endpoint = *c;
            out->rendered = true;
            memset(&out->render_req, 0, sizeof(out->render_req));
            out->render_req.kind = DISP_NO_CONTENT;
            esp_http_client_cleanup(client);
            return ESP_OK;
        }

        if (http_code != 200) {
            ESP_LOGW(TAG, "GET failed for %s, code %d", c->id, http_code);
            bool reachable = endpoint_is_reachable_code(http_code);
            esp_http_client_cleanup(client);
            if (reachable) {
                /* Reachable but not actionable — legacy persisted and stopped
                 * (no screen). */
                out->endpoint_valid = true;
                out->persist_endpoint_now = true;
                out->endpoint = *c;
                out->rendered = false;
                return ESP_OK;
            }
            continue; /* unreachable — try next candidate */
        }

        if (content_length <= 0) {
            ESP_LOGW(TAG, "Content-Length not set — try next candidate");
            esp_http_client_cleanup(client);
            continue;
        }

        /* 200 with a body: decide BWR vs mono and stream to the partition. */
        bool is_bwr = (hdrs.bitmap_layers == 2 && hdrs.black_len > 0 &&
                       hdrs.red_len > 0);

        err = download_to_partition(client, content_length, &hdrs, is_bwr, out);
        esp_http_client_cleanup(client);

        /* The candidate answered with a 200 — it is reachable regardless of
         * whether the body verified (G4). cycle_task persists it only after
         * BIT_DISP_DONE for a successful render (persist_endpoint_now stays
         * false). */
        out->endpoint_valid = true;
        out->persist_endpoint_now = false;
        out->endpoint = *c;

        if (err != ESP_OK) {
            /* Truncated / short body — skip render, keep previous image (D5).
             * out->rendered is already false. */
            out->rendered = false;
            out->last_modified[0] = '\0';
        }
        return ESP_OK;
    }

    ESP_LOGW(TAG, "All candidates failed at the transport level");
    return (out->http_code != 0) ? ESP_OK : ESP_FAIL;
}
