# esp_crt_bundle — project component supplying the CA-bundle attach hook that
# stock ESP8266_RTOS_SDK v3.4 declares but never shipped. Provides
# esp_crt_bundle.h (which esp_tls_mbedtls.c #includes under
# CONFIG_MBEDTLS_CERTIFICATE_BUNDLE) and embeds the curated CA roots.
#
# The PEM lives in rtos/certs/ (owned by the TLS step). EMBED_TXTFILES NUL-
# terminates it so it parses directly as a PEM chain.
idf_component_register(SRCS "esp_crt_bundle.c"
                    INCLUDE_DIRS "include"
                    REQUIRES mbedtls
                    EMBED_TXTFILES "${CMAKE_CURRENT_LIST_DIR}/../../certs/ca_roots.pem")

# esp_tls_mbedtls.c does `#include "esp_crt_bundle.h"` under
# CONFIG_MBEDTLS_CERTIFICATE_BUNDLE, but esp-tls only REQUIRES mbedtls, so it
# does not see this component's public include dir through the normal REQUIRES
# graph. In stock IDF the header ships inside the mbedtls component; v3.4 never
# shipped it. Inject our include dir onto esp-tls's component library so the
# guarded include resolves, without editing the SDK tree. (esp-tls's library is
# registered before project components are processed, so the target exists.)
if(CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
    idf_component_get_property(esp_tls_lib esp-tls COMPONENT_LIB)
    if(esp_tls_lib)
        target_include_directories(${esp_tls_lib} PRIVATE "${CMAKE_CURRENT_LIST_DIR}/include")
    endif()
endif()
