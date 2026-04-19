/* app_main.c — boot-order mirror of the legacy setup() (items 1-7).
 *
 * Legacy (src/main.cpp setup()):
 *   Serial.begin(115200); delay(2000);          -> 115200 + 2 s settle (item 1)
 *   DeviceID = String(ESP.getChipId());          -> device_id_str() (item 4)
 *   showLaunchScreen();                           -> DISP_LAUNCH (item 2)
 *   [DEBUG: sketch MD5, FS list, store dump]      -> app desc + sha256 +
 *                                                    running partition (D1) +
 *                                                    store_dump() (item 3)
 *   initStore(); initButton(); initWifiWithManager();
 *                                                 -> store_init / display_init /
 *                                                    button_input_start /
 *                                                    net_portal_init (item 5)
 *   [ENABLE_PCB_TEST block]                       -> pcb_test_run() (item 7)
 *   loop()                                        -> cycle_task (items 8-11)
 *
 * The shared queues + event group are created here before any task that uses
 * them (G7 payloads, value-copied).
 */
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "events.h"
#include "device_id.h"
#include "cycle_task.h"

#include "store.h"
#include "eink_display.h"
#include "button_input.h"
#include "net_portal.h"

#if CONFIG_EINKTODO_PCB_TEST
#include "pcb_test.h"
#endif

static const char *TAG = "app_main";

/* Shared handles declared extern in events.h (G7 payloads, value-copied). */
QueueHandle_t cmd_queue;
QueueHandle_t wifi_cmd_queue;
QueueHandle_t disp_queue;
EventGroupHandle_t sys_events;

static void boot_dump(const esp_app_desc_t *desc)
{
    /* DEBUG boot dump (item 3): sketch-MD5 replaced by app version + elf
     * sha256 + running partition (D1). */
    const esp_partition_t *running = esp_ota_get_running_partition();

    char sha_hex[17];
    for (int i = 0; i < 8; i++) {
        snprintf(&sha_hex[i * 2], 3, "%02x", desc->app_elf_sha256[i]);
    }

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    ESP_LOGI(TAG, "Device ID: %s", device_id_str());
    ESP_LOGI(TAG, "Version: %s", desc->version);
    ESP_LOGI(TAG, "App ELF SHA256: %s..., running partition: %s", sha_hex,
             running != NULL ? running->label : "?");
    ESP_LOGI(TAG, "Chip: rev %d, %d core(s)", chip.revision, chip.cores);
    ESP_LOGI(TAG, "Free heap: %u", esp_get_free_heap_size());
    store_dump();   /* no-op unless CONFIG_EINKTODO_DEBUG_STORE */
}

void app_main(void)
{
    /* 2 s serial settle — parity item 1 (legacy Serial.begin(115200);
     * delay(2000)). Console baud is set to 115200 in sdkconfig.defaults. */
    vTaskDelay(pdMS_TO_TICKS(2000));

    const esp_app_desc_t *app_desc = esp_ota_get_app_description();

    printf("\n");
    printf("E-Ink Todo List (RTOS)\n");
    printf("Version: %s\n", app_desc->version);  /* PROJECT_VER = git describe */
    printf("Device ID: %s\n", device_id_str());
#if CONFIG_EINKTODO_DEBUG
    printf("DEBUG MODE\n");
#endif

    /* Create the shared queues + event group before any consumer task starts
     * (button/wifi/httpd produce onto them; display/cycle consume). */
    cmd_queue      = xQueueCreate(4, sizeof(app_cmd_t));
    wifi_cmd_queue = xQueueCreate(4, sizeof(wifi_cmd_t));
    disp_queue     = xQueueCreate(4, sizeof(display_req_t));
    sys_events     = xEventGroupCreate();
    if (cmd_queue == NULL || wifi_cmd_queue == NULL || disp_queue == NULL ||
        sys_events == NULL) {
        ESP_LOGE(TAG, "queue/event-group creation failed, restarting");
        esp_restart();
    }

    /* Persistent store first — NVS init + erase-retry recovery (item 6). Done
     * before display so the launch screen and everything after can read it. */
    store_init();

#if CONFIG_EINKTODO_DEBUG
    boot_dump(app_desc);
#endif

    /* Launch screen (item 2): display_init() starts display_task (it requires
     * disp_queue + sys_events, created above), then DISP_LAUNCH renders the
     * version banner. */
    display_init();
    {
        display_req_t req = { 0 };
        req.kind = DISP_LAUNCH;
        snprintf(req.text, sizeof(req.text), "%s", app_desc->version);
        display_request(&req, pdMS_TO_TICKS(1000));
    }

    /* Button gestures (item 5) — reads BIT_PORTAL_ACTIVE / BIT_WIFI_CONNECTED
     * to pick the single-click action, posts onto cmd_queue / wifi_cmd_queue. */
    button_input_start(cmd_queue, wifi_cmd_queue, sys_events);

    /* WiFi control + captive portal (items 18-25): brings up APSTA + hostname,
     * auto-connects saved creds or opens the portal. Forwards
     * CMD_API_KEY_UPDATED onto cmd_queue on a portal save (G1). */
    net_portal_init(wifi_cmd_queue, cmd_queue, disp_queue, sys_events);

#if CONFIG_EINKTODO_PCB_TEST
    /* Factory PCB-test branch (item 7): info screen + 5-min block; never
     * returns to start the cycle task (legacy delay(5*60*1000) sat in setup()
     * and the device was power-cycled out of test mode). */
    pcb_test_run();
#endif

    /* loop() replacement — cadence + cmd dispatch + render handshake. */
    cycle_task_start(cmd_queue);
}
