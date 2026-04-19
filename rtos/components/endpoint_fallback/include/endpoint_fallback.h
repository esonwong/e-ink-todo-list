/* endpoint_fallback — primary + fallback endpoint candidate list with
 * last-good prioritization. Port of legacy src/fallbackNetwork.h
 * (inventory items 34–38); shared by todo_fetch and ota_update.
 *
 * Contract (legacy parity):
 *  - The PRIMARY url is used VERBATIM — callers pass the full URL including
 *    its path (legacy DEFAULT_API_URL / FIRMWARE_UPDATE_URL were full URLs).
 *    `path` is appended only to the fallback base URLs
 *    (CONFIG_EINKTODO_FALLBACK_URL_1/2), mirroring
 *    buildEndpointCandidates(primaryUrl, fallbackPath, ...).
 *  - A fallback candidate is emitted only when both its base URL and its TLS
 *    leaf pin (CONFIG_EINKTODO_FALLBACK_PIN_1/2, 40 hex chars = SHA-1 /
 *    64 hex chars = SHA-256, ':' separators tolerated) are valid.
 *  - Last-good endpoint id is persisted in NVS namespace "pvalue", keys
 *    "net_lg_ep" (string id) and "net_lg_pin" (u8 bool, uses-pin flag) —
 *    renamed from network_last_good_endpoint /
 *    network_last_good_uses_fingerprint (deviation D9, 15-char NVS key
 *    limit). Persist timing: cycle_task calls endpoint_remember_success()
 *    only after BIT_DISP_DONE for 200-render cycles (G2).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENDPOINT_PRIMARY_ID     "primary"  /* legacy NETWORK_PRIMARY_ENDPOINT_ID */
#define ENDPOINT_MAX_CANDIDATES 3

/* VALUE-COPIED everywhere it travels: fixed-size struct, no pointers (G7).
 * id is the candidate identity persisted as the last-good endpoint:
 * "primary" for the primary URL, the fallback base URL (truncated to 15
 * chars) for fallbacks. */
typedef struct {
    char    id[16];
    char    url[128];
    uint8_t pin[32];   /* parsed leaf-cert digest, big-endian byte order   */
    uint8_t pin_len;   /* 20 = SHA-1, 32 = SHA-256, 0 = no pin             */
    bool    use_pin;   /* true on fallbacks: pin replaces CA validation    */
} endpoint_candidate_t;

/* Fill out[] with up to 3 candidates: primary (verbatim, if non-empty) then
 * fallback 1 and 2 (base + path, only if base and pin are both valid).
 * Returns the candidate count. */
int  endpoint_build_candidates(const char *primary, const char *path,
                               endpoint_candidate_t out[3]);

/* Move the candidate whose id matches the persisted pvalue "net_lg_ep"
 * (default "primary") to the front, preserving the relative order of the
 * others. No-op when no candidate matches. */
void endpoint_prioritize_last_good(endpoint_candidate_t *c, int n);

/* Persist c->id ("net_lg_ep") and c->use_pin ("net_lg_pin") so the next
 * poll tries this endpoint first. */
void endpoint_remember_success(const endpoint_candidate_t *c);

/* true for HTTP codes that prove the endpoint is reachable and answering:
 * 200, 304, 204, 401 (legacy isReachableHttpCode). */
bool endpoint_is_reachable_code(int http_code);

#ifdef __cplusplus
}
#endif
