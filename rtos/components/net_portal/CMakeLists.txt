# net_portal — WiFi control FSM + captive config portal (items 18-25).
#
# portal.html is embedded as a text blob (NUL-terminated) referenced via the
# _binary_portal_html_start/_end symbols in portal_httpd.c.
#
# PRIV_INCLUDE_DIRS "../../main" pulls in events.h + device_id.h (shared
# payloads / chip-id helpers) header-only — the device_id_* and sys_events
# symbols link in from main, no REQUIRES edge onto main (which would cycle).
# REQUIRES dns_server + sntp_clock for their public headers; esp_http_server,
# esp8266 (esp_wifi), tcpip_adapter, esp_event for the portal stack.
idf_component_register(SRCS "wifi_ctrl.c" "portal_httpd.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "." "../../main"
                       REQUIRES dns_server sntp_clock esp_http_server esp8266
                                tcpip_adapter esp_event freertos log
                       EMBED_TXTFILES "portal.html")
