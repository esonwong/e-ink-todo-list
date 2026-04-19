# dns_server — captive-portal UDP DNS catcher (ported from IDF captive_portal, item 20)
idf_component_register(SRCS "dns_server.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES lwip log)
