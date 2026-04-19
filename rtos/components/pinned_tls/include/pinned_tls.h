/* pinned_tls — leaf-certificate fingerprint pinning for the E-ink Todo RTOS
 * firmware (R5). mbedTLS has no fingerprint API, so this builds one on top of
 * the mbedtls_ssl_conf_verify() callback. The attach function is
 * signature-compatible with esp_crt_bundle_attach / the esp_http_client and
 * esp_tls crt_bundle_attach hook (see components/{esp_http_client,tcp_transport}
 * upstream.diff), so the same patched hook carries both the curated CA bundle
 * and the leaf pin.
 *
 * Behaviour mirrors BearSSL setFingerprint() (legacy src/fallbackNetwork.h):
 * at chain depth 0 the raw leaf DER is hashed (SHA-1 for a 20-byte pin,
 * SHA-256 for a 32-byte pin) and constant-time compared against the configured
 * pin. On match all verify flags are cleared (UNTRUSTED + CN mismatch +
 * EXPIRED/FUTURE) — exactly like BearSSL fingerprint mode, which keeps the IP
 * fallback endpoints usable before the first NTP sync. On mismatch the
 * handshake is aborted.
 *
 * One TLS connection exists at a time (cycle_task serializes all HTTP), so a
 * single static pin context is safe; it is assert-guarded.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PINNED_TLS_SHA1_LEN   20
#define PINNED_TLS_SHA256_LEN 32
#define PINNED_TLS_MAX_LEN    PINNED_TLS_SHA256_LEN

/**
 * @brief Attach the leaf-fingerprint verifier to an mbedtls_ssl_config.
 *
 * Signature-compatible with esp_crt_bundle_attach(void *conf) so it plugs into
 * the patched esp_http_client_config_t.crt_bundle_attach hook. Sets the conf
 * authmode to MBEDTLS_SSL_VERIFY_REQUIRED and installs pinned_verify_cb. The
 * pin must already have been set via pinned_tls_set().
 *
 * @param  mbedtls_conf  pointer to the mbedtls_ssl_config (passed as void* to
 *                       keep the bundle-attach signature)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if no pin is configured,
 *         ESP_ERR_INVALID_ARG if conf is NULL.
 */
esp_err_t pinned_tls_attach(void *mbedtls_conf);

/**
 * @brief Set the active leaf-certificate pin (raw bytes).
 *
 * @param hash  pin bytes (20 = SHA-1, 32 = SHA-256)
 * @param len   PINNED_TLS_SHA1_LEN or PINNED_TLS_SHA256_LEN
 */
void pinned_tls_set(const uint8_t *hash, size_t len);

/**
 * @brief Parse a human-readable fingerprint string into raw pin bytes.
 *
 * Colon/space tolerant, like BearSSL setFingerprint(): accepts
 * "3E:EC:..:5B", "3eec..5b", or space-separated hex. 40 hex digits => SHA-1
 * (20 bytes), 64 hex digits => SHA-256 (32 bytes); any other length is
 * rejected. Case-insensitive.
 *
 * @param str      NUL-terminated fingerprint string
 * @param out      destination buffer, at least PINNED_TLS_MAX_LEN bytes
 * @param out_cap  capacity of out
 * @param out_len  receives the number of bytes written (20 or 32)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on malformed input.
 */
esp_err_t pinned_tls_parse_fingerprint(const char *str, uint8_t *out,
                                       size_t out_cap, size_t *out_len);

/**
 * @brief Convenience: parse a fingerprint string and set it as the active pin.
 *
 * @param str  NUL-terminated fingerprint string (see pinned_tls_parse_fingerprint)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on malformed input.
 */
esp_err_t pinned_tls_set_fingerprint(const char *str);

/**
 * @brief Clear the active pin (subsequent pinned_tls_attach fails until set).
 */
void pinned_tls_clear(void);

#ifdef __cplusplus
}
#endif
