/* pcb_test.c — factory PCB-test info screen (item 7).
 *
 * Legacy (src/main.cpp, ENABLE_PCB_TEST): composes test info (version,
 * device id, sketch MD5, free heap, flash chip id, WiFi SSID/ip/mac,
 * persistent-value round-trip), shows it centered on the panel, then
 * delay(5 * 60 * 1000).
 *
 * RTOS mapping:
 *  - Sketch MD5 -> app version + elf sha256 + running partition (D1).
 *  - Flash chip id -> esp_chip_info + flash size (R18: no public
 *    spi_flash_get_id in v3.4).
 *  - Full info goes to the log; the DISP_PCB_INFO request carries a compact
 *    subset (display_req_t.text is 64 bytes by binding contract).
 *  - delay(5*60*1000) -> 5 x 60 s vTaskDelay.
 */
#include "pcb_test.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "spi_flash.h"
#include "tcpip_adapter.h"

#include "device_id.h"
#include "events.h"

/* store component API (components/store/include/store.h, step 2). The
 * fallback externs match the binding signatures from architecture §2 so this
 * file compiles before the store component lands; pcb_test.o is only linked
 * once app_main wires pcb_test_run() in step 11, by which time store exists. */
#if defined(__has_include) && __has_include("store.h")
#include "store.h"
#else
esp_err_t store_pvalue_set_str(const char *k, const char *v);
esp_err_t store_pvalue_get_str(const char *k, char *out, size_t n, const char *dflt);
#endif

static const char *TAG = "pcb_test";

void pcb_test_run(void)
{
    const esp_app_desc_t *app_desc = esp_ota_get_app_description();
    const esp_partition_t *running = esp_ota_get_running_partition();

    /* elf sha256, first 8 bytes as hex (D1 replacement for sketch MD5) */
    char sha_hex[17];
    for (int i = 0; i < 8; i++) {
        snprintf(&sha_hex[i * 2], 3, "%02x", app_desc->app_elf_sha256[i]);
    }

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    /* Network info — tolerate "WiFi not up yet" gracefully. */
    uint8_t mac[6] = { 0 };
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    tcpip_adapter_ip_info_t ip_info;
    memset(&ip_info, 0, sizeof(ip_info));
    tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);

    /* Legacy computed the current time string here (dead: never appended to
     * testInfo) — log it for parity. */
    time_t now = time(NULL);
    struct tm timeinfo;
    char time_str[20];
    localtime_r(&now, &timeinfo);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);

    /* Persistent-value round-trip (legacy key "PCB_TEST" -> NVS "pcb_test",
     * D9 rename family). */
    char pv[48];
    store_pvalue_set_str("pcb_test", "PCB test persistent value is OK!");
    store_pvalue_get_str("pcb_test", pv, sizeof(pv),
                         "PCB test persistent value is not OK!");

    /* Full test info to the console (legacy showed all of it on the panel;
     * display_req_t.text is 64 B, so the panel gets the compact subset). */
    ESP_LOGI(TAG, "Test Mode");
    ESP_LOGI(TAG, "Version: %s", app_desc->version);
    ESP_LOGI(TAG, "Device ID: %s", device_id_str());
    ESP_LOGI(TAG, "App ELF SHA256: %s..., partition: %s", sha_hex,
             running != NULL ? running->label : "?");
    ESP_LOGI(TAG, "Free Heap: %u", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Chip: rev %d, %d core(s), flash %u bytes", chip.revision,
             chip.cores, spi_flash_get_chip_size());
    ESP_LOGI(TAG, "WiFi SSID: %s", CONFIG_EINKTODO_DEV_WIFI_SSID);
    ESP_LOGI(TAG, "WiFi ip: " IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "WiFi mac: %02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
             mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Time: %s", time_str);
    ESP_LOGI(TAG, "Persistent Value: %s", pv);

    /* Compact info screen — VALUE-COPIED payload (G7). */
    display_req_t req;
    memset(&req, 0, sizeof(req));
    req.kind = DISP_PCB_INFO;
    snprintf(req.text, sizeof(req.text), "Test Mode\nV:%s\nID:%s\nHeap:%u\nPV:%s",
             app_desc->version, device_id_str(),
             (unsigned)esp_get_free_heap_size(),
             strcmp(pv, "PCB test persistent value is OK!") == 0 ? "OK" : "FAIL");
    xQueueSend(disp_queue, &req, portMAX_DELAY);

    /* Legacy: delay(5 * 60 * 1000). */
    for (int i = 0; i < 5; i++) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
    }
}
