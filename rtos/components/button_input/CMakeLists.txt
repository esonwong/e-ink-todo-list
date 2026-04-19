# button_input — 10 ms-poll OneButton 2.5.0 gesture FSM + GPIO16 paths
# (items 13-17, G11). gpio16_rtc.c is compiled unconditionally so the
# CONFIG_EINKTODO_GPIO16_RAWREG fallback stays buildable from day one.
#
# PRIV_INCLUDE_DIRS pulls in main/events.h (shared app_cmd_t / wifi_cmd_t /
# sys_events bits) without a REQUIRES edge onto main (which would be circular:
# main implicitly requires every component).
idf_component_register(SRCS "button_input.c" "gpio16_rtc.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "." "../../main"
                       REQUIRES esp8266 freertos log)
