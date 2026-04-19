/* pinned_tls.c — leaf-fingerprint pinning via the mbedTLS verify callback (R5).
 * See include/pinned_tls.h for the design rationale and BearSSL parity notes.
 */

#include <string.h>
#include <assert.h>

#include "pinned_tls.h"
#include "esp_log.h"

#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/sha1.h"
#include "mbedtls/sha256.h"

static const char *TAG = "pinned_tls";

/* Single static pin context — one TLS connection at a time (cycle_task
 * serializes all HTTP). */
typedef struct {
    uint8_t pin[PINNED_TLS_MAX_LEN];
    size_t  len;        /* 0 = no pin configured, else 20 or 32 */
} pin_ctx_t;

static pin_ctx_t s_pin;

/* Constant-time memcmp: returns 0 iff equal, non-zero otherwise. Avoids early
 * exit so timing does not leak how many leading bytes matched. */
static int ct_memcmp(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++) {
        diff |= (uint8_t)(a[i] ^ b[i]);
    }
    return diff;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void pinned_tls_set(const uint8_t *hash, size_t len)
{
    if (hash == NULL ||
        (len != PINNED_TLS_SHA1_LEN && len != PINNED_TLS_SHA256_LEN)) {
        ESP_LOGE(TAG, "pinned_tls_set: bad pin length %u", (unsigned)len);
        s_pin.len = 0;
        return;
    }
    memcpy(s_pin.pin, hash, len);
    s_pin.len = len;
}

void pinned_tls_clear(void)
{
    s_pin.len = 0;
    memset(s_pin.pin, 0, sizeof(s_pin.pin));
}

esp_err_t pinned_tls_parse_fingerprint(const char *str, uint8_t *out,
                                       size_t out_cap, size_t *out_len)
{
    if (str == NULL || out == NULL || out_len == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Colon/space tolerant: collect hex nibbles, skip ':' and whitespace,
     * reject any other character (mirrors BearSSL setFingerprint tolerance). */
    size_t n = 0;
    int high = -1;
    for (const char *p = str; *p; p++) {
        char c = *p;
        if (c == ':' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            continue;
        }
        int v = hex_nibble(c);
        if (v < 0) {
            return ESP_ERR_INVALID_ARG;       /* stray non-hex char */
        }
        if (high < 0) {
            high = v;
        } else {
            if (n >= out_cap) {
                return ESP_ERR_INVALID_ARG;   /* too long for buffer */
            }
            out[n++] = (uint8_t)((high << 4) | v);
            high = -1;
        }
    }
    if (high >= 0) {
        return ESP_ERR_INVALID_ARG;           /* odd number of nibbles */
    }
    if (n != PINNED_TLS_SHA1_LEN && n != PINNED_TLS_SHA256_LEN) {
        return ESP_ERR_INVALID_ARG;           /* 40 or 64 hex digits only */
    }
    *out_len = n;
    return ESP_OK;
}

esp_err_t pinned_tls_set_fingerprint(const char *str)
{
    uint8_t buf[PINNED_TLS_MAX_LEN];
    size_t  len = 0;
    esp_err_t err = pinned_tls_parse_fingerprint(str, buf, sizeof(buf), &len);
    if (err != ESP_OK) {
        return err;
    }
    pinned_tls_set(buf, len);
    return ESP_OK;
}

/* mbedtls_ssl_conf_verify callback. At depth 0 hash the raw leaf DER and
 * compare to the pin. Chain trust is irrelevant under a leaf pin, so for
 * depth > 0 we leave flags untouched and return 0. */
static int pinned_verify_cb(void *ctx, mbedtls_x509_crt *crt, int depth,
                            uint32_t *flags)
{
    pin_ctx_t *pin = (pin_ctx_t *)ctx;

    if (depth > 0) {
        /* Intermediate / root: leaf pin makes chain validity irrelevant. */
        *flags = 0;
        return 0;
    }

    /* depth == 0: the leaf certificate. */
    assert(pin != NULL && pin->len != 0);

    uint8_t digest[PINNED_TLS_MAX_LEN];
    int ret;
    if (pin->len == PINNED_TLS_SHA1_LEN) {
        ret = mbedtls_sha1_ret(crt->raw.p, crt->raw.len, digest);
    } else {
        /* SHA-256 (is224 = 0) */
        ret = mbedtls_sha256_ret(crt->raw.p, crt->raw.len, digest, 0);
    }
    if (ret != 0) {
        ESP_LOGE(TAG, "leaf hash failed: -0x%04x", -ret);
        *flags |= MBEDTLS_X509_BADCERT_OTHER;
        return MBEDTLS_X509_BADCERT_OTHER;
    }

    if (ct_memcmp(digest, pin->pin, pin->len) == 0) {
        /* Match: clear every verify flag, exactly as BearSSL fingerprint mode
         * ignores CN / validity. Keeps IP fallbacks usable pre-NTP. */
        *flags = 0;
        ESP_LOGD(TAG, "leaf fingerprint matched");
        return 0;
    }

    ESP_LOGW(TAG, "leaf fingerprint mismatch");
    *flags |= MBEDTLS_X509_BADCERT_OTHER;
    return MBEDTLS_X509_BADCERT_OTHER;
}

/* Deliberately empty CA chain. mbedtls refuses a VERIFY_REQUIRED handshake
 * with ca_chain == NULL (MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED, -0x7680) even when
 * the verify callback cleared every flag; an empty chain trusts nothing, so
 * the leaf pin stays the sole trust decision. Re-initialized per attach because
 * CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA frees (zeroizes) conf->ca_chain. */
static mbedtls_x509_crt s_empty_chain;

esp_err_t pinned_tls_attach(void *mbedtls_conf)
{
    if (mbedtls_conf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_pin.len == 0) {
        ESP_LOGE(TAG, "pinned_tls_attach: no pin configured");
        return ESP_ERR_INVALID_STATE;
    }
    mbedtls_ssl_config *conf = (mbedtls_ssl_config *)mbedtls_conf;
    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_verify(conf, pinned_verify_cb, &s_pin);
    mbedtls_x509_crt_init(&s_empty_chain);
    mbedtls_ssl_conf_ca_chain(conf, &s_empty_chain, NULL);
    return ESP_OK;
}
