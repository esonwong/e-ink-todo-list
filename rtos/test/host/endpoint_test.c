/* Host unit test for components/endpoint_fallback/endpoint_fallback.c
 * (items 34-38, G10). Compiled with the two fallback candidates injected via
 * -D (matching the device's baked-in Kconfig values); see the Makefile.
 *
 * Expected injected config (see Makefile; hostnames stay distinct within the
 * first 15 chars so their truncated ids don't collide):
 *   CONFIG_EINKTODO_FALLBACK_URL_1 = "https://fb1.example.com"
 *   CONFIG_EINKTODO_FALLBACK_PIN_1 = "AA:BB:...:DD"   (40 hex = SHA-1)
 *   CONFIG_EINKTODO_FALLBACK_URL_2 = "https://fb2.example.com"
 *   CONFIG_EINKTODO_FALLBACK_PIN_2 = "<64 hex>"       (SHA-256)
 */
#include "test_util.h"
#include "endpoint_fallback.h"
#include "nvs.h"

#include <string.h>

/* PRIMARY is a BASE url (Kconfig EINKTODO_API_URL, path-less); the display path
 * is appended by endpoint_build_candidates, same as the fallbacks. */
#define PRIMARY "https://www.einktodo.com"
#define PATH    "/api/display/v3"

/* fallback base urls as injected; ids are these truncated to 15 chars. */
#define FB1_URL "https://fb1.example.com"
#define FB2_URL "https://fb2.example.com"

/* id field is char[16] -> 15 chars + NUL, copy_trunc of the base url. */
static void expected_id(const char *base, char *out)
{
    strncpy(out, base, 15);
    out[15] = '\0';
}

static void test_build_primary_first(void)
{
    endpoint_candidate_t c[3];
    int n = endpoint_build_candidates(PRIMARY, PATH, c);
    CHECK_EQ_INT(n, 3);
    CHECK_STR(c[0].id, ENDPOINT_PRIMARY_ID);
    CHECK_STR(c[0].url, PRIMARY PATH); /* base url + display path appended */
    CHECK_EQ_INT(c[0].use_pin, 0);
    CHECK_EQ_INT(c[0].pin_len, 0);
}

static void test_build_fallback_url_is_base_plus_path(void)
{
    endpoint_candidate_t c[3];
    char id1[16];
    char id2[16];
    int n = endpoint_build_candidates(PRIMARY, PATH, c);
    CHECK_EQ_INT(n, 3);

    expected_id(FB1_URL, id1);
    CHECK_STR(c[1].id, id1);
    CHECK_STR(c[1].url, FB1_URL PATH);
    CHECK_EQ_INT(c[1].use_pin, 1);
    CHECK_EQ_INT(c[1].pin_len, 20); /* 40 hex = SHA-1 */

    expected_id(FB2_URL, id2);
    CHECK_STR(c[2].id, id2);
    CHECK_STR(c[2].url, FB2_URL PATH);
    CHECK_EQ_INT(c[2].use_pin, 1);
    CHECK_EQ_INT(c[2].pin_len, 32); /* 64 hex = SHA-256 */
}

static void test_pin_bytes_parsed_correctly(void)
{
    endpoint_candidate_t c[3];
    endpoint_build_candidates(PRIMARY, PATH, c);
    /* PIN_1 first/last bytes: AA .. DD */
    CHECK_EQ_INT(c[1].pin[0], 0xAA);
    CHECK_EQ_INT(c[1].pin[19], 0xDD);
}

static void test_no_primary_omits_slot(void)
{
    endpoint_candidate_t c[3];
    int n = endpoint_build_candidates("", PATH, c);
    /* Empty primary -> only the two fallbacks. */
    CHECK_EQ_INT(n, 2);
    CHECK_EQ_INT(c[0].use_pin, 1);
}

static void test_null_out_is_zero(void)
{
    CHECK_EQ_INT(endpoint_build_candidates(PRIMARY, PATH, NULL), 0);
}

static void test_null_path_treated_as_empty(void)
{
    endpoint_candidate_t c[3];
    char id1[16];
    endpoint_build_candidates(PRIMARY, NULL, c);
    CHECK_STR(c[0].url, PRIMARY); /* primary: base only, no path */
    expected_id(FB1_URL, id1);
    CHECK_STR(c[1].id, id1);
    CHECK_STR(c[1].url, FB1_URL); /* base only, no path */
}

static void test_prioritize_default_is_primary_noop(void)
{
    endpoint_candidate_t c[3];
    mock_nvs_reset();
    endpoint_build_candidates(PRIMARY, PATH, c);
    /* No persisted last-good -> default "primary" -> already first -> no-op. */
    endpoint_prioritize_last_good(c, 3);
    CHECK_STR(c[0].id, ENDPOINT_PRIMARY_ID);
}

static void test_prioritize_moves_fallback_to_front(void)
{
    endpoint_candidate_t c[3];
    char id1[16];
    char id2[16];
    expected_id(FB1_URL, id1);
    expected_id(FB2_URL, id2);

    mock_nvs_reset();
    endpoint_build_candidates(PRIMARY, PATH, c);

    /* Persist fallback-1 as last good. */
    endpoint_remember_success(&c[1]);

    endpoint_build_candidates(PRIMARY, PATH, c);
    endpoint_prioritize_last_good(c, 3);

    /* fb1 moves to front; primary and fb2 keep relative order behind it. */
    CHECK_STR(c[0].id, id1);
    CHECK_STR(c[1].id, ENDPOINT_PRIMARY_ID);
    CHECK_STR(c[2].id, id2);
}

static void test_remember_persists_id_and_pin_flag(void)
{
    endpoint_candidate_t c[3];
    nvs_handle h;
    char id[16];
    size_t len = sizeof(id);
    uint8_t uses_pin = 0xFF;

    mock_nvs_reset();
    endpoint_build_candidates(PRIMARY, PATH, c);
    endpoint_remember_success(&c[1]); /* a fallback: use_pin == 1 */

    CHECK_EQ_INT(nvs_open("pvalue", NVS_READONLY, &h), ESP_OK);
    CHECK_EQ_INT(nvs_get_str(h, "net_lg_ep", id, &len), ESP_OK);
    {
        char id1[16];
        expected_id(FB1_URL, id1);
        CHECK_STR(id, id1);
    }
    CHECK_EQ_INT(nvs_get_u8(h, "net_lg_pin", &uses_pin), ESP_OK);
    CHECK_EQ_INT(uses_pin, 1);
    nvs_close(h);
}

static void test_remember_primary_clears_pin_flag(void)
{
    endpoint_candidate_t c[3];
    nvs_handle h;
    uint8_t uses_pin = 0xFF;

    mock_nvs_reset();
    endpoint_build_candidates(PRIMARY, PATH, c);
    endpoint_remember_success(&c[0]); /* primary: use_pin == 0 */

    CHECK_EQ_INT(nvs_open("pvalue", NVS_READONLY, &h), ESP_OK);
    CHECK_EQ_INT(nvs_get_u8(h, "net_lg_pin", &uses_pin), ESP_OK);
    CHECK_EQ_INT(uses_pin, 0);
    nvs_close(h);
}

static void test_remember_avoids_redundant_commit(void)
{
    endpoint_candidate_t c[3];
    mock_nvs_reset();
    endpoint_build_candidates(PRIMARY, PATH, c);

    endpoint_remember_success(&c[1]);
    int after_first = mock_nvs_commit_count();
    CHECK(after_first >= 1);

    /* Same endpoint again -> read-compare-write skips the flash write. */
    endpoint_remember_success(&c[1]);
    CHECK_EQ_INT(mock_nvs_commit_count(), after_first);
}

static void test_remember_open_failure_is_safe(void)
{
    endpoint_candidate_t c[3];
    mock_nvs_reset();
    endpoint_build_candidates(PRIMARY, PATH, c);
    mock_nvs_fail_open(1);
    endpoint_remember_success(&c[1]); /* must not crash, nothing persisted */
    CHECK_EQ_INT(mock_nvs_commit_count(), 0);
}

static void test_reachable_codes(void)
{
    CHECK(endpoint_is_reachable_code(200));
    CHECK(endpoint_is_reachable_code(304));
    CHECK(endpoint_is_reachable_code(204));
    CHECK(endpoint_is_reachable_code(401));
    CHECK(!endpoint_is_reachable_code(500));
    CHECK(!endpoint_is_reachable_code(404));
    CHECK(!endpoint_is_reachable_code(0));
    CHECK(!endpoint_is_reachable_code(403));
}

int main(void)
{
    RUN(test_build_primary_first);
    RUN(test_build_fallback_url_is_base_plus_path);
    RUN(test_pin_bytes_parsed_correctly);
    RUN(test_no_primary_omits_slot);
    RUN(test_null_out_is_zero);
    RUN(test_null_path_treated_as_empty);
    RUN(test_prioritize_default_is_primary_noop);
    RUN(test_prioritize_moves_fallback_to_front);
    RUN(test_remember_persists_id_and_pin_flag);
    RUN(test_remember_primary_clears_pin_flag);
    RUN(test_remember_avoids_redundant_commit);
    RUN(test_remember_open_failure_is_safe);
    RUN(test_reachable_codes);
    return test_summary("endpoint_test");
}
