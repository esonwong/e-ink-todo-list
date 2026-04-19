# ota_update — manual esp_ota_* firmware update loop (legacy
# lib/update/UpdateFirmware.h, items 51-54, R9). No own task: runs on
# cycle_task, so it shares the single TLS context with todo_fetch by
# construction.
#
# PRIV_INCLUDE_DIRS pulls in main/events.h (display_req_t / DISP_TEXT_CENTER /
# disp_queue), reached transitively through eink_display.h — same pattern as the
# eink_display component, no REQUIRES edge onto main.
idf_component_register(SRCS "ota_update.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "../../main"
                       REQUIRES endpoint_fallback
                       PRIV_REQUIRES store eink_display pinned_tls esp_crt_bundle
                                     esp_http_client app_update bootloader_support
                                     esp8266 freertos log)
