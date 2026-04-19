/* urlencode — port of legacy src/todoList.h urlencode() (item 66); the
 * yield() call is dropped (R18: own task, no cooperative scheduler).
 *
 * Encoding is byte-for-byte legacy-identical: ' ' -> '+', [A-Za-z0-9]
 * verbatim, every other byte -> "%XX" with uppercase hex.
 *
 * out is always NUL-terminated when out_len > 0. Encoding stops before the
 * first whole unit (1 byte verbatim/'+', or 3 bytes "%XX") that would not
 * fit, so output is never cut mid-escape. Returns the number of bytes
 * written to out, excluding the terminating NUL.
 */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t urlencode(const char *in, char *out, size_t out_len);

#ifdef __cplusplus
}
#endif
