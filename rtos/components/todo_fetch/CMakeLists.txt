# todo_fetch — landed in two steps: §7 step 3 shipped urlencode.c/.h only;
# step 9 adds todo_fetch.c + include/todo_fetch.h.
#
# The public header (include/todo_fetch.h) pulls in events.h (display_req_t)
# and endpoint_fallback.h, so endpoint_fallback is a public REQUIRES and
# main/events.h is reached via PRIV_INCLUDE_DIRS (same pattern as ota_update —
# no REQUIRES edge onto main). The download half streams to the raw `bitmap`
# data partition (spi_flash) and uses the patched esp_http_client + the TLS
# trust hooks (esp_crt_bundle bundle / pinned_tls leaf pin).
idf_component_register(SRCS "urlencode.c" "todo_fetch.c"
                       INCLUDE_DIRS "include" "."
                       PRIV_INCLUDE_DIRS "../../main"
                       REQUIRES endpoint_fallback
                       PRIV_REQUIRES pinned_tls esp_crt_bundle esp_http_client
                                     spi_flash app_update esp8266 freertos log)
