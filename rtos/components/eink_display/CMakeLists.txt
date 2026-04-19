# eink_display — ported lib/display-driver/* (C++) + hal_epd Arduino-call shim
# (R12, items 41-44, 55-65). display_task is the panel's single consumer; full
# bitmaps render via the band_source_t callback (G12). Fonts are placed in
# .irom0.text by the PROGMEM section attribute in flash_font.h (verify in .map);
# no extra CMake placement needed.
#
# PRIV_INCLUDE_DIRS pulls in main/events.h (shared display_req_t / disp_queue /
# sys_events bits) without a REQUIRES edge onto main (which would be circular).
idf_component_register(SRCS "hal_epd.c"
                            "base_display_driver.cpp"
                            "display_750_driver.cpp"
                            "display_290_driver.cpp"
                            "display_task.cpp"
                            "font12.cpp"
                            "font20.cpp"
                            "font24.cpp"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "." "../../main"
                       REQUIRES esp8266 freertos
                       PRIV_REQUIRES spi_flash app_update log)
