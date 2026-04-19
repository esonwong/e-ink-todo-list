/* urlencode.c — see urlencode.h. Legacy parity notes:
 * - alnum test is the C-locale [A-Za-z0-9] set, spelled out so host tests
 *   and device builds can never diverge on locale;
 * - high-bit bytes encode identically to the Arduino version ((c >> 4) & 0xf
 *   masked the sign extension there; we use unsigned char throughout).
 */
#include "urlencode.h"

static int is_url_alnum(unsigned char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z');
}

static char hex_digit(unsigned int nibble)
{
    return (char)(nibble > 9 ? nibble - 10 + 'A' : nibble + '0');
}

size_t urlencode(const char *in, char *out, size_t out_len)
{
    size_t written = 0;

    if (out == NULL || out_len == 0) {
        return 0;
    }
    out[0] = '\0';
    if (in == NULL) {
        return 0;
    }

    for (const unsigned char *p = (const unsigned char *)in; *p != '\0'; p++) {
        unsigned char c = *p;
        if (c == ' ') {
            if (written + 1 >= out_len) {
                break;
            }
            out[written++] = '+';
        } else if (is_url_alnum(c)) {
            if (written + 1 >= out_len) {
                break;
            }
            out[written++] = (char)c;
        } else {
            if (written + 3 >= out_len) {
                break;
            }
            out[written++] = '%';
            out[written++] = hex_digit((c >> 4) & 0xf);
            out[written++] = hex_digit(c & 0xf);
        }
    }
    out[written] = '\0';
    return written;
}
