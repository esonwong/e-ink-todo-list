/* Host unit test for components/todo_fetch/urlencode.c (item 66, G10).
 * Verifies byte-for-byte parity with the legacy Arduino urlencode():
 * ' ' -> '+', [A-Za-z0-9] verbatim, every other byte -> "%XX" uppercase hex,
 * plus the RTOS-added out_len bounds contract (never cut mid-escape, always
 * NUL-terminated). */
#include "test_util.h"
#include "urlencode.h"

/* Encode into a generous buffer and return written count. */
static size_t enc(const char *in, char *out, size_t out_len)
{
    return urlencode(in, out, out_len);
}

static void test_alnum_passthrough(void)
{
    char out[64];
    size_t n = enc("AZaz09", out, sizeof(out));
    CHECK_STR(out, "AZaz09");
    CHECK_EQ_INT(n, 6);
}

static void test_space_becomes_plus(void)
{
    char out[64];
    size_t n = enc("a b c", out, sizeof(out));
    CHECK_STR(out, "a+b+c");
    CHECK_EQ_INT(n, 5);
}

static void test_reserved_chars_percent_encoded(void)
{
    char out[64];
    /* '/' = 0x2F, ':' = 0x3A, '?' = 0x3F, '&' = 0x26, '=' = 0x3D */
    enc("/:?&=", out, sizeof(out));
    CHECK_STR(out, "%2F%3A%3F%26%3D");
}

static void test_uppercase_hex(void)
{
    char out[64];
    /* '~' = 0x7E -> upper nibble 7, lower nibble E (must be 'E', not 'e'). */
    enc("~", out, sizeof(out));
    CHECK_STR(out, "%7E");
}

static void test_high_bit_byte(void)
{
    char out[64];
    /* 0xC3 0xA9 = UTF-8 'é'; sign-extension must not corrupt the nibbles. */
    const char in[] = { (char)0xC3, (char)0xA9, '\0' };
    enc(in, out, sizeof(out));
    CHECK_STR(out, "%C3%A9");
}

static void test_dims_query_realistic(void)
{
    char out[64];
    /* Mirrors the /api/display/v3 w/h query usage (item 27). */
    enc("800x480", out, sizeof(out));
    CHECK_STR(out, "800x480");
}

static void test_empty_and_null(void)
{
    char out[8];
    CHECK_EQ_INT(enc("", out, sizeof(out)), 0);
    CHECK_STR(out, "");
    CHECK_EQ_INT(enc(NULL, out, sizeof(out)), 0);
    CHECK_STR(out, "");
}

static void test_zero_len_out(void)
{
    /* out_len == 0: nothing written, returns 0, no OOB store. */
    char canary = 'X';
    CHECK_EQ_INT(urlencode("abc", &canary, 0), 0);
    CHECK_EQ_INT(canary, 'X');
}

static void test_truncation_never_mid_escape(void)
{
    /* "%2F" needs 3 bytes + NUL. With out_len == 3 it cannot fit the whole
     * escape, so nothing of it is written and the string stays empty. */
    char out[3];
    size_t n = enc("/", out, sizeof(out));
    CHECK_EQ_INT(n, 0);
    CHECK_STR(out, "");
}

static void test_truncation_stops_cleanly(void)
{
    /* out_len 4 fits exactly one "%XX" (3 bytes) + NUL; the second escape is
     * dropped whole. */
    char out[4];
    size_t n = enc("//", out, sizeof(out));
    CHECK_EQ_INT(n, 3);
    CHECK_STR(out, "%2F");
}

static void test_truncation_alnum_boundary(void)
{
    /* out_len 4 -> 3 literal chars + NUL; 4th dropped. */
    char out[4];
    size_t n = enc("abcd", out, sizeof(out));
    CHECK_EQ_INT(n, 3);
    CHECK_STR(out, "abc");
}

int main(void)
{
    RUN(test_alnum_passthrough);
    RUN(test_space_becomes_plus);
    RUN(test_reserved_chars_percent_encoded);
    RUN(test_uppercase_hex);
    RUN(test_high_bit_byte);
    RUN(test_dims_query_realistic);
    RUN(test_empty_and_null);
    RUN(test_zero_len_out);
    RUN(test_truncation_never_mid_escape);
    RUN(test_truncation_stops_cleanly);
    RUN(test_truncation_alnum_boundary);
    return test_summary("urlencode_test");
}
