# store — NVS settings (legacy src/store.h mirror, items 45-49)
idf_component_register(SRCS "store.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES nvs_flash log)
