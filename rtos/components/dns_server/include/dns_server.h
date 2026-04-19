/* dns_server — captive-portal UDP DNS catcher (architecture §2, item 20).
 *
 * Answers EVERY A query with the softAP IP so any hostname the phone probes
 * (captive.apple.com, connectivitycheck.gstatic.com, …) resolves to the
 * device and the OS pops the captive sheet. Ported from the IDF
 * captive_portal example (pure BSD sockets on lwIP). Created on portal start,
 * deleted on stop/timeout — the task self-exits when the socket is closed.
 */
#pragma once

#include "esp_err.h"
#include "lwip/ip4_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the DNS catcher task, answering all A queries with resolve_to (the
 * softAP IP). Idempotent: a second start with the server already running is a
 * no-op. */
esp_err_t dns_server_start(ip4_addr_t resolve_to);

/* Stop the DNS catcher: closes the socket, the task observes the close and
 * exits, then deletes itself. Safe to call when not running. */
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
