# endpoint_fallback — legacy src/fallbackNetwork.h (items 34-38).
# Leaf component: depends only on SDK nvs_flash (pvalue namespace keys
# net_lg_ep / net_lg_pin) so phase-1 steps stay parallel.
idf_component_register(SRCS "endpoint_fallback.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash log)
