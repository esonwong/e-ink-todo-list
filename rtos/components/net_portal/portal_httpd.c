/* portal_httpd.c — config-portal HTTP UI (architecture §1/§2, items 19-21).
 *
 * Pages: GET / (portal.html, scan + save form), GET /scan (JSON SSID list),
 * POST /save (SSID + password + 41-char apiKey -> WIFI_CMD_SAVE), GET /info,
 * GET /restart, GET /exit, plus a set of OS captive-probe URIs that 302 to the
 * portal root so the captive sheet pops on iOS/Android/Windows.
 *
 * G6 (binding): every URI handler that mutates WiFi state posts a wifi_cmd_t
 * to wifi_cmd_queue with TIMEOUT 0 and returns immediately. Handlers never
 * touch the display, NVS, or fetch state — this breaks the httpd_stop-vs-
 * blocked-handler deadlock. The blocking esp_wifi_scan_start only runs while
 * the portal is open and no fetch is in flight (the portal is up only before
 * WiFi connects, so cycle_task fetches never overlap it — R1).
 *
 * v3.4 esp_http_server has no uri_match_fn / err-handler hooks, so the captive
 * catch-all is a fixed list of the well-known probe paths rather than a
 * wildcard 404 handler.
 */
#include "portal_internal.h"

#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "events.h"
#include "device_id.h"

static const char *TAG = "portal_httpd";

#define PORTAL_SCAN_MAX 16

/* portal.html embedded via EMBED_TXTFILES (NUL-terminated). */
extern const char portal_html_start[] asm("_binary_portal_html_start");
extern const char portal_html_end[]   asm("_binary_portal_html_end");

static httpd_handle_t  s_server;
static QueueHandle_t   s_wifi_cmd_q;

/* ---- helpers ----------------------------------------------------------- */

static esp_err_t send_html(httpd_req_t *req, const char *html)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, html, strlen(html));
}

/* URL-decode in place (form fields arrive percent-encoded; '+' is space). */
static void url_decode(char *s)
{
    char *o = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') {
            *o++ = ' ';
        } else if (*p == '%' && p[1] && p[2]) {
            int hi = p[1], lo = p[2];
            hi = (hi <= '9') ? hi - '0' : (hi | 0x20) - 'a' + 10;
            lo = (lo <= '9') ? lo - '0' : (lo | 0x20) - 'a' + 10;
            *o++ = (char)((hi << 4) | lo);
            p += 2;
        } else {
            *o++ = *p;
        }
    }
    *o = '\0';
}

/* ---- handlers ---------------------------------------------------------- */

static esp_err_t root_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    /* -1 drops the embedded NUL terminator EMBED_TXTFILES appends. */
    httpd_resp_send(req, portal_html_start,
                    portal_html_end - portal_html_start - 1);
    return ESP_OK;
}

static esp_err_t scan_get(httpd_req_t *req)
{
    /* Blocking scan — safe here: portal is only open before WiFi connects, so
     * no fetch is ever in flight (architecture §1 / R1). APSTA scan works
     * while the AP is up. */
    wifi_scan_config_t cfg = { 0 };
    esp_err_t err = esp_wifi_scan_start(&cfg, true);

    httpd_resp_set_type(req, "application/json");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan_start failed: %s", esp_err_to_name(err));
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }

    uint16_t num = PORTAL_SCAN_MAX;
    wifi_ap_record_t recs[PORTAL_SCAN_MAX];
    if (esp_wifi_scan_get_ap_records(&num, recs) != ESP_OK) {
        httpd_resp_send(req, "[]", 2);
        return ESP_OK;
    }

    httpd_resp_send_chunk(req, "[", 1);
    for (uint16_t i = 0; i < num; i++) {
        /* Escape quotes/backslashes so the JSON stays valid. */
        char ssid[2 * 33];
        size_t o = 0;
        for (size_t j = 0; recs[i].ssid[j] && o < sizeof(ssid) - 2; j++) {
            char c = (char)recs[i].ssid[j];
            if (c == '"' || c == '\\') {
                ssid[o++] = '\\';
            }
            ssid[o++] = c;
        }
        ssid[o] = '\0';

        char line[128];
        int n = snprintf(line, sizeof(line),
                         "%s{\"ssid\":\"%s\",\"rssi\":%d}",
                         i ? "," : "", ssid, recs[i].rssi);
        if (n > 0) {
            httpd_resp_send_chunk(req, line, n);
        }
    }
    httpd_resp_send_chunk(req, "]", 1);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t save_post(httpd_req_t *req)
{
    /* Body is small (ssid + pass + 41-char key, percent-encoded). */
    char buf[256];
    int total = 0;
    while (total < (int)sizeof(buf) - 1) {
        int r = httpd_req_recv(req, buf + total, sizeof(buf) - 1 - total);
        if (r <= 0) {
            break;
        }
        total += r;
    }
    buf[total > 0 ? total : 0] = '\0';

    wifi_cmd_t cmd = { .type = WIFI_CMD_SAVE };
    /* Fields default to empty; parse the three known keys. */
    if (httpd_query_key_value(buf, "s", cmd.save.ssid, sizeof(cmd.save.ssid)) == ESP_OK) {
        url_decode(cmd.save.ssid);
    }
    if (httpd_query_key_value(buf, "p", cmd.save.pass, sizeof(cmd.save.pass)) == ESP_OK) {
        url_decode(cmd.save.pass);
    }
    /* api_key buffer holds up to 41 chars + NUL (legacy WiFiManagerParameter
     * length 41). */
    if (httpd_query_key_value(buf, "k", cmd.save.api_key, sizeof(cmd.save.api_key)) == ESP_OK) {
        url_decode(cmd.save.api_key);
        cmd.save.api_key[sizeof(cmd.save.api_key) - 1] = '\0';
    }

    ESP_LOGI(TAG, "save: ssid='%s' apiKeyLen=%u",
             cmd.save.ssid, (unsigned)strlen(cmd.save.api_key));

    /* G6: post with timeout 0 and return immediately — never block the handler
     * (it would deadlock against portal_httpd_stop()). */
    if (s_wifi_cmd_q != NULL) {
        if (xQueueSend(s_wifi_cmd_q, &cmd, 0) != pdTRUE) {
            ESP_LOGW(TAG, "wifi_cmd_queue full, save dropped");
        }
    }

    return send_html(req,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>E-ink Todo List</title></head><body>"
        "<h1>Saving&hellip;</h1><p>Trying to connect to the network. "
        "Watch the e-ink screen.</p></body></html>");
}

static esp_err_t info_get(httpd_req_t *req)
{
    char body[256];
    int n = snprintf(body, sizeof(body),
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>E-ink Todo List</title></head><body>"
        "<h1>Device Info</h1>"
        "<p>Hostname: %s<br>Device ID: %s</p>"
        "<p><a href=\"/\">back</a></p></body></html>",
        device_id_hostname(), device_id_str());
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, body, n);
    return ESP_OK;
}

static esp_err_t restart_get(httpd_req_t *req)
{
    /* G6: request the portal restart-with-portal flow (close + reopen) with
     * timeout 0; the device-level reboot gesture is the 4-click button path. */
    if (s_wifi_cmd_q != NULL) {
        wifi_cmd_t cmd = { .type = WIFI_CMD_RESET_AND_PORTAL };
        xQueueSend(s_wifi_cmd_q, &cmd, 0);
    }
    return send_html(req,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>E-ink Todo List</title></head><body>"
        "<h1>Restarting&hellip;</h1></body></html>");
}

static esp_err_t exit_get(httpd_req_t *req)
{
    /* G6: timeout 0. WIFI_CMD_PORTAL_STOP closes httpd + dns. */
    if (s_wifi_cmd_q != NULL) {
        wifi_cmd_t cmd = { .type = WIFI_CMD_PORTAL_STOP };
        xQueueSend(s_wifi_cmd_q, &cmd, 0);
    }
    return send_html(req,
        "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        "<title>E-ink Todo List</title></head><body>"
        "<h1>Closing portal</h1></body></html>");
}

/* Captive-probe catch: 302 to the portal root so the OS captive sheet pops.
 * v3.4 httpd has no wildcard matcher, so we register this for the known probe
 * URIs explicitly. */
static esp_err_t captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" PORTAL_AP_PASSWORD "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* ---- lifecycle --------------------------------------------------------- */

esp_err_t portal_httpd_start(QueueHandle_t wifi_cmd_q)
{
    if (s_server != NULL) {
        return ESP_OK;
    }
    s_wifi_cmd_q = wifi_cmd_q;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 3;        /* R1: keep <=3 on the 8266 lwIP socket budget */
    config.lru_purge_enable = true;
    config.max_uri_handlers = 12;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        s_server = NULL;
        return err;
    }

    static const httpd_uri_t uris[] = {
        { .uri = "/",        .method = HTTP_GET,  .handler = root_get },
        { .uri = "/scan",    .method = HTTP_GET,  .handler = scan_get },
        { .uri = "/save",    .method = HTTP_POST, .handler = save_post },
        { .uri = "/info",    .method = HTTP_GET,  .handler = info_get },
        { .uri = "/restart", .method = HTTP_GET,  .handler = restart_get },
        { .uri = "/exit",    .method = HTTP_GET,  .handler = exit_get },
        /* OS captive-portal probes -> 302 to root. */
        { .uri = "/hotspot-detect.html",   .method = HTTP_GET, .handler = captive_redirect },
        { .uri = "/generate_204",          .method = HTTP_GET, .handler = captive_redirect },
        { .uri = "/gen_204",               .method = HTTP_GET, .handler = captive_redirect },
        { .uri = "/connecttest.txt",       .method = HTTP_GET, .handler = captive_redirect },
        { .uri = "/ncsi.txt",              .method = HTTP_GET, .handler = captive_redirect },
        { .uri = "/redirect",              .method = HTTP_GET, .handler = captive_redirect },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        httpd_register_uri_handler(s_server, &uris[i]);
    }

    ESP_LOGI(TAG, "portal httpd started");
    return ESP_OK;
}

void portal_httpd_stop(void)
{
    if (s_server != NULL) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "portal httpd stopped");
    }
}
