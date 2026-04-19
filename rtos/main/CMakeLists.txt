# main component. Sources are globbed so later steps (cycle_task.c,
# device_id.c, led.c, pcb_test.c — steps 7/11) drop in without editing this
# file; each agent configures a fresh build dir, so the glob is re-evaluated.
file(GLOB MAIN_SRCS "${CMAKE_CURRENT_LIST_DIR}/*.c")

# REQUIRES the components whose public headers app_main.c / cycle_task.c
# include. None of these REQUIRE main (they reach events.h via
# PRIV_INCLUDE_DIRS ../../main), so there is no circular edge. esp_ota_ops /
# nvs_flash etc. come transitively; the explicit ones below are the
# component-mirror modules cycle_task drives.
idf_component_register(SRCS ${MAIN_SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES store eink_display button_input net_portal
                                todo_fetch endpoint_fallback ota_update
                                sntp_clock app_update esp8266 freertos
                                nvs_flash spi_flash pthread log)
