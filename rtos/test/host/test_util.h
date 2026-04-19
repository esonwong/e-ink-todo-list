/* Tiny host-test assertion helpers (no framework dependency). Each test()
 * is a void(void); main() lists them. The CHECK macros record failures and
 * keep going so one run reports every broken case. */
#pragma once

#include <stdio.h>
#include <string.h>

static int g_checks;
static int g_fails;

#define CHECK(cond)                                                         \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(cond)) {                                                      \
            g_fails++;                                                      \
            fprintf(stderr, "FAIL %s:%d: CHECK(%s)\n", __FILE__, __LINE__,  \
                    #cond);                                                 \
        }                                                                   \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                      \
    do {                                                                    \
        g_checks++;                                                         \
        long _a = (long)(actual);                                           \
        long _e = (long)(expected);                                         \
        if (_a != _e) {                                                     \
            g_fails++;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s == %ld, expected %ld\n",        \
                    __FILE__, __LINE__, #actual, _a, _e);                   \
        }                                                                   \
    } while (0)

#define CHECK_STR(actual, expected)                                         \
    do {                                                                    \
        g_checks++;                                                         \
        if (strcmp((actual), (expected)) != 0) {                           \
            g_fails++;                                                      \
            fprintf(stderr, "FAIL %s:%d: %s == \"%s\", expected \"%s\"\n",  \
                    __FILE__, __LINE__, #actual, (actual), (expected));     \
        }                                                                   \
    } while (0)

#define RUN(fn)                                                             \
    do {                                                                    \
        fprintf(stderr, "RUN  %s\n", #fn);                                  \
        fn();                                                               \
    } while (0)

/* Call from main() after RUN()ing every test; returns process exit code. */
static int test_summary(const char *suite)
{
    if (g_fails == 0) {
        fprintf(stderr, "PASS %s (%d checks)\n", suite, g_checks);
        return 0;
    }
    fprintf(stderr, "FAILED %s: %d/%d checks failed\n", suite, g_fails,
            g_checks);
    return 1;
}
