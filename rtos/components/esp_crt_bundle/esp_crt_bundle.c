/* esp_crt_bundle.c — curated CA trust attach for the E-ink Todo RTOS firmware
 * (R4, tier-1). See include/esp_crt_bundle.h.
 *
 * The embedded certs/ca_roots.pem (GTS Root R1-R4, GlobalSign Root CA,
 * ISRG Root X1/X2, SSL.com RSA+ECC) is parsed into an mbedtls cert chain and
 * bound to each handshake config. ~12 KB of PEM lives in flash. The chain is
 * re-parsed on EVERY attach: CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA makes the
 * SDK (esp_mbedtls_free_cacert) mbedtls_x509_crt_free() conf->ca_chain once
 * the handshake no longer needs it, so a chain parsed once and cached would be
 * empty from the second connection on (every later handshake: -0x2700).
 *
 * Note: stock v3.4 ships no gen_crt_bundle.py / indexed-DER bundle format, so we
 * parse PEM directly with mbedtls_x509_crt_parse rather than the IDF indexed
 * lookup. Functionally identical trust decision; slightly larger flash. See the
 * step-8 deviation note in the structured return.
 */

#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

static const char *TAG = "esp_crt_bundle";

/* Embedded PEM (certs/ca_roots.pem), NUL-terminated via EMBED_TXTFILES. */
extern const uint8_t ca_roots_pem_start[] asm("_binary_ca_roots_pem_start");
extern const uint8_t ca_roots_pem_end[]   asm("_binary_ca_roots_pem_end");

static mbedtls_x509_crt s_ca_chain;
static bool s_parsed = false;

static esp_err_t ensure_parsed(void)
{
    /* Free whatever a previous connection left behind (a handshake that
     * failed before the dynamic free point keeps its chain); freeing a chain
     * the SDK already freed is a no-op, as mbedtls zeroizes the head. */
    mbedtls_x509_crt_free(&s_ca_chain);
    s_parsed = false;
    mbedtls_x509_crt_init(&s_ca_chain);

    /* EMBED_TXTFILES appends a NUL, so length must include it for PEM parsing. */
    size_t pem_len = (size_t)(ca_roots_pem_end - ca_roots_pem_start);
    int ret = mbedtls_x509_crt_parse(&s_ca_chain, ca_roots_pem_start, pem_len);
    if (ret < 0) {
        ESP_LOGE(TAG, "ca_roots.pem parse failed: -0x%04x", -ret);
        mbedtls_x509_crt_free(&s_ca_chain);
        return ESP_FAIL;
    }
    if (ret > 0) {
        ESP_LOGW(TAG, "%d certs in ca_roots.pem skipped (parse warnings)", ret);
    }
    s_parsed = true;
    ESP_LOGD(TAG, "CA bundle parsed");
    return ESP_OK;
}

esp_err_t esp_crt_bundle_attach(void *conf)
{
    if (conf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = ensure_parsed();
    if (err != ESP_OK) {
        return err;
    }
    mbedtls_ssl_config *ssl_conf = (mbedtls_ssl_config *)conf;
    mbedtls_ssl_conf_authmode(ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(ssl_conf, &s_ca_chain, NULL);
    return ESP_OK;
}

void esp_crt_bundle_detach(void *conf)
{
    (void)conf;
    if (s_parsed) {
        mbedtls_x509_crt_free(&s_ca_chain);
        s_parsed = false;
    }
}
