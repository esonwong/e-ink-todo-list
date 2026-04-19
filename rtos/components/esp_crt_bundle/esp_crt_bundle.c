/* esp_crt_bundle.c — curated CA trust attach for the E-ink Todo RTOS firmware
 * (R4, tier-1). See include/esp_crt_bundle.h.
 *
 * Heap-lean trust anchoring. The roots in certs/ca_roots.pem (GTS Root R1-R4,
 * GlobalSign Root CA, ISRG Root X1/X2, SSL.com RSA+ECC) are NOT parsed into
 * conf->ca_chain up front. With mbedtls 2.16.5 that chain stays resident for
 * the whole SERVER_CERTIFICATE step (esp_mbedtls_free_cacert only runs after
 * the verify), and all nine parsed roots cost ~17 KB of heap — during the very
 * step that runs the ECDSA P-384 chain math. On this chip's ~50 KB free heap
 * that pushed min-free-heap to ~3.7 KB and the P-384 verify failed
 * intermittently with MPI_ALLOC_FAILED, surfacing as x509 -0x2700
 * BADCERT_NOT_TRUSTED on the second and later polls (bench, 2026-06-21).
 *
 * Instead the handshake runs with an empty (trusts-nothing) CA chain plus a
 * verify callback. mbedtls still links the peer chain on its own — signature,
 * CA bit, pathlen and profile for every link — and flags the topmost cert
 * NOT_TRUSTED because no anchor matched. The callback (invoked top-down) takes
 * each NOT_TRUSTED cert and verifies it against the embedded roots one at a
 * time with mbedtls_x509_crt_verify, so name / CA-bit / key-size / signature
 * checks are mbedtls's own. A root lives on the heap only for the duration of
 * its one check (~1-3 KB). Certs whose own link signature failed keep that
 * NOT_TRUSTED flag (their issuer is an intermediate, never a bundle root), so a
 * broken chain still fails closed. Hostname (CN_MISMATCH) and every other flag
 * are never touched here.
 *
 * Note: stock v3.4 ships no gen_crt_bundle.py / indexed-DER bundle format, so we
 * walk the PEM with mbedtls_pem_read_buffer rather than the IDF indexed
 * lookup. Functionally identical trust decision; slightly larger flash.
 */

#include <string.h>

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_system.h"

#include "mbedtls/pem.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

static const char *TAG = "esp_crt_bundle";

/* Embedded PEM (certs/ca_roots.pem), NUL-terminated via EMBED_TXTFILES. */
extern const uint8_t ca_roots_pem_start[] asm("_binary_ca_roots_pem_start");
extern const uint8_t ca_roots_pem_end[]   asm("_binary_ca_roots_pem_end");

#define PEM_HDR "-----BEGIN CERTIFICATE-----"
#define PEM_FTR "-----END CERTIFICATE-----"

/* Deliberately empty CA chain. mbedtls refuses a VERIFY_REQUIRED handshake
 * with ca_chain == NULL (MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED, -0x7680) even when
 * the verify callback cleared every flag; an empty chain trusts nothing, so
 * the callback stays the sole trust decision. Re-initialized per attach
 * because CONFIG_MBEDTLS_DYNAMIC_FREE_CONFIG_DATA frees (zeroizes) and NULLs
 * conf->ca_chain. Same pattern as pinned_tls. */
static mbedtls_x509_crt s_empty_chain;

/* Try to anchor `top` (a peer cert mbedtls found no parent for) on one of the
 * embedded roots. Parses one root at a time; returns 0 on the first root that
 * mbedtls_x509_crt_verify accepts as the issuer. */
static int anchor_on_bundle(mbedtls_x509_crt *top, int depth)
{
    const unsigned char *p = ca_roots_pem_start;
    int idx = 0;

    while (1) {
        mbedtls_pem_context pem;
        size_t use_len = 0;
        mbedtls_pem_init(&pem);
        int ret = mbedtls_pem_read_buffer(&pem, PEM_HDR, PEM_FTR, p, NULL, 0, &use_len);
        if (ret != 0) {
            mbedtls_pem_free(&pem);
            if (ret != MBEDTLS_ERR_PEM_NO_HEADER_FOOTER_PRESENT) {
                ESP_LOGW(TAG, "root #%d: PEM read failed -0x%04x", idx, -ret);
            }
            break; /* end of bundle */
        }
        p += use_len;

        mbedtls_x509_crt root;
        mbedtls_x509_crt_init(&root);
        ret = mbedtls_x509_crt_parse_der(&root, pem.buf, pem.buflen);
        mbedtls_pem_free(&pem);
        if (ret != 0) {
            ESP_LOGW(TAG, "root #%d: DER parse failed -0x%04x", idx, -ret);
            mbedtls_x509_crt_free(&root);
            idx++;
            continue;
        }

        /* trust_ca = this single root. A non-issuer fails fast on the name
         * compare; the issuer gets the real signature check. No hostname
         * (cn == NULL): esp-tls already checks that on the leaf. */
        uint32_t vflags = 0;
        ret = mbedtls_x509_crt_verify(top, &root, NULL, NULL, &vflags, NULL, NULL);
        if (ret == 0) {
            char dn[96];
            if (mbedtls_x509_dn_gets(dn, sizeof(dn), &root.subject) < 0) {
                dn[0] = '\0';
            }
            ESP_LOGI(TAG, "depth %d anchored on root #%d [%s], free heap %u",
                     depth, idx, dn, (unsigned)esp_get_free_heap_size());
            mbedtls_x509_crt_free(&root);
            return 0;
        }
        if (vflags != MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
            /* Name matched but something else is wrong (bad signature, key
             * size, pathlen...). Worth seeing; keep looking anyway. */
            ESP_LOGW(TAG, "root #%d: verify -0x%04x flags 0x%08x", idx, -ret,
                     (unsigned)vflags);
        }
        mbedtls_x509_crt_free(&root);
        idx++;
    }
    return -1;
}

/* mbedtls calls this once per chain item, topmost cert first. */
static int bundle_verify_cb(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags)
{
    (void)ctx;
    if ((*flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) == 0) {
        return 0;
    }
    if (anchor_on_bundle(crt, depth) == 0) {
        *flags &= ~MBEDTLS_X509_BADCERT_NOT_TRUSTED;
    } else {
        ESP_LOGW(TAG, "depth %d: no embedded root anchors this cert (flags 0x%08x)",
                 depth, (unsigned)*flags);
    }
    return 0;
}

esp_err_t esp_crt_bundle_attach(void *conf)
{
    if (conf == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    mbedtls_ssl_config *ssl_conf = (mbedtls_ssl_config *)conf;
    mbedtls_ssl_conf_authmode(ssl_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_x509_crt_init(&s_empty_chain);
    mbedtls_ssl_conf_ca_chain(ssl_conf, &s_empty_chain, NULL);
    mbedtls_ssl_conf_verify(ssl_conf, bundle_verify_cb, NULL);
    return ESP_OK;
}

void esp_crt_bundle_detach(void *conf)
{
    /* Nothing stays resident between handshakes. */
    (void)conf;
}
