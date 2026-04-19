# sntp_clock — SNTP init + setClock() semantics (legacy src/clock.h, item 26).
# lwip / log / freertos are in the SDK's common requires.
# ../../main is a header-only path for events.h (sys_events/BIT_TIME_SYNCED);
# no REQUIRES cycle — the sys_events definition links in from main.
idf_component_register(SRCS "sntp_clock.c"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "../../main")
