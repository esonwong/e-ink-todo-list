/* esp_crt_bundle.h — header expected by esp_tls_mbedtls.c when
 * CONFIG_MBEDTLS_CERTIFICATE_BUNDLE is enabled.
 *
 * Stock ESP8266_RTOS_SDK v3.4 declares the esp_tls_cfg_t.crt_bundle_attach hook
 * and the guarded call site in esp_tls_mbedtls.c, but never shipped the
 * esp_crt_bundle component itself (no header, no implementation, no
 * gen_crt_bundle.py). This project component supplies the missing piece so the
 * one attach hook (R4/R5) carries the curated CA trust store, while pinned_tls
 * reuses the identical signature for leaf-fingerprint pinning.
 *
 * This is the tier-1 TLS trust path: the curated roots in certs/ca_roots.pem are
 * embedded into the firmware image. They are NOT pre-parsed into the CA chain
 * (nine parsed roots cost ~17 KB of heap for the whole certificate step and
 * starved the P-384 chain math); the handshake runs with an empty CA chain and
 * a verify callback that anchors the top of the peer chain on one embedded
 * root at a time. Tier-2 (esp_tls global CA store) stays Kconfig-selectable;
 * see esp_crt_bundle.c.
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Attach the curated CA bundle to an mbedtls_ssl_config.
 *
 * Signature is the contract for esp_tls_cfg_t.crt_bundle_attach /
 * esp_http_client_config_t.crt_bundle_attach. Sets
 * MBEDTLS_SSL_VERIFY_REQUIRED, an empty CA chain and the verify callback that
 * anchors the peer chain on the embedded certs/ca_roots.pem.
 *
 * @param  conf  pointer to mbedtls_ssl_config (void* to match the hook type)
 * @return ESP_OK on success, error otherwise.
 */
esp_err_t esp_crt_bundle_attach(void *conf);

/**
 * @brief Kept for API symmetry; nothing stays resident between handshakes.
 */
void esp_crt_bundle_detach(void *conf);

#ifdef __cplusplus
}
#endif
