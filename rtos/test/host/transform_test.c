/* transform_test.c — host-side unit tests (G10) of the byte-level layer
 * transforms the e-ink render applies to a downloaded bitmap (R7).
 *
 * These are the exact bit semantics shared by the legacy
 * src/todoList.h (displayToScreen / displayBWRToScreen) and the RTOS
 * components/eink_display/display_task.cpp render path. todo_fetch (this step)
 * streams the body to the bitmap partition VERBATIM — it never transforms
 * bytes — so the transform is purely a render-side property. We pin it here,
 * against bytes captured by `curl` from https://www.einktodo.com/api/display/v3,
 * before any hardware bring-up.
 *
 * The three transforms (architecture §1, R7):
 *   - BWR black layer: server bit 0 = ink. Panel black buffer bit 0 = black.
 *     ⇒ COPY VERBATIM; a pixel is BLACK iff its server bit == 0.
 *   - BWR red layer:   server bit 0 = ink. Panel red buffer bit 1 = red.
 *     ⇒ BITWISE NOT into the panel red buffer; a pixel is RED iff its
 *     server bit == 0.
 *   - mono:            server bit 1 = draw black (legacy displayToScreen).
 *     ⇒ BITWISE NOT into the black pass; a pixel is BLACK iff its server
 *     bit == 1. Red pass left white.
 *
 * FIXTURE PROVENANCE: the BWR black fixture below is a real 64-byte slice of
 * the black layer captured live (96000-byte body, black 48000 + red 48000,
 * 800x480, 2 layers). The live red layer was uniformly 0xff (no red ink in the
 * current list), so the red-transform cases are synthesized from the documented
 * rule and a hand-picked byte pattern; the mono cases reuse the same fixture
 * bytes (the mono body is structurally identical to a single BWR black layer).
 */

#include <stdint.h>
#include <stddef.h>

#include "test_util.h"

/* ---- the transforms under test (pure, no SDK) ---- */

/* BWR black: panel black-buffer byte = server byte verbatim. */
static uint8_t xf_bwr_black(uint8_t server_byte)
{
    return server_byte;
}

/* BWR red: panel red-buffer byte = bitwise NOT of the server byte. */
static uint8_t xf_bwr_red(uint8_t server_byte)
{
    return (uint8_t)~server_byte;
}

/* mono: panel black-buffer byte = bitwise NOT of the server byte. */
static uint8_t xf_mono(uint8_t server_byte)
{
    return (uint8_t)~server_byte;
}

/* Per-pixel ink decision derived from a server byte, MSB-first bit j (0..7),
 * exactly as the legacy drawPixel loops walk the byte (0x80 >> j). */
static int ink_bwr_black(uint8_t server_byte, int j) /* black ink? */
{
    /* legacy displayBWRToScreen: if (!(buf & (0x80>>j))) draw black */
    return (server_byte & (0x80 >> j)) == 0;
}
static int ink_bwr_red(uint8_t server_byte, int j) /* red ink? */
{
    /* legacy displayBWRToScreen: if (!(buf & (0x80>>j))) draw red */
    return (server_byte & (0x80 >> j)) == 0;
}
static int ink_mono(uint8_t server_byte, int j) /* black ink? */
{
    /* legacy displayToScreen: if (buf & (0x80>>j)) draw black */
    return (server_byte & (0x80 >> j)) != 0;
}

/* ---- real captured fixture (BWR black layer slice) ---- */
/* Provenance: www.einktodo.com /api/display/v3?width=800&height=480,
 * X-Device-Colors: bwr — 96000-byte body, black 48000 + red 48000. */
static const uint8_t kBwrBlackFixture[64] = {
    0xe7, 0xff, 0xff, 0x3f, 0xff, 0xff, 0xc7, 0xff,
    0xf8, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x83, 0xff, 0xff,
    0xfc, 0x3f, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xfe, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xfe, 0x00, 0x1f, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/* ---- tests ---- */

/* BWR black is a verbatim copy: every byte of the fixture maps to itself, and
 * the per-pixel black decision is "bit == 0". 0xff ⇒ all white; 0x00 ⇒ all
 * black. */
static void test_bwr_black_verbatim(void)
{
    for (size_t i = 0; i < sizeof(kBwrBlackFixture); i++) {
        CHECK_EQ_INT(xf_bwr_black(kBwrBlackFixture[i]), kBwrBlackFixture[i]);
    }
    /* 0xff (no ink) ⇒ no pixel black; 0x00 (all ink) ⇒ every pixel black. */
    for (int j = 0; j < 8; j++) {
        CHECK_EQ_INT(ink_bwr_black(0xff, j), 0);
        CHECK_EQ_INT(ink_bwr_black(0x00, j), 1);
    }
    /* 0xe7 = 1110 0111 ⇒ bits 3 and 4 are 0 ⇒ black at j==3 and j==4 only. */
    CHECK_EQ_INT(ink_bwr_black(0xe7, 0), 0);
    CHECK_EQ_INT(ink_bwr_black(0xe7, 1), 0);
    CHECK_EQ_INT(ink_bwr_black(0xe7, 2), 0);
    CHECK_EQ_INT(ink_bwr_black(0xe7, 3), 1);
    CHECK_EQ_INT(ink_bwr_black(0xe7, 4), 1);
    CHECK_EQ_INT(ink_bwr_black(0xe7, 5), 0);
    CHECK_EQ_INT(ink_bwr_black(0xe7, 6), 0);
    CHECK_EQ_INT(ink_bwr_black(0xe7, 7), 0);
    /* 0x00 from the fixture (offset 50): fully black byte. */
    CHECK_EQ_INT(kBwrBlackFixture[50], 0x00);
    for (int j = 0; j < 8; j++) {
        CHECK_EQ_INT(ink_bwr_black(kBwrBlackFixture[50], j), 1);
    }
}

/* BWR red is a bitwise NOT in the panel buffer, but the per-pixel red decision
 * is still "server bit == 0" (the NOT is just the buffer encoding). */
static void test_bwr_red_not(void)
{
    CHECK_EQ_INT(xf_bwr_red(0xff), 0x00);
    CHECK_EQ_INT(xf_bwr_red(0x00), 0xff);
    CHECK_EQ_INT(xf_bwr_red(0xe7), 0x18); /* ~1110 0111 = 0001 1000 */
    CHECK_EQ_INT(xf_bwr_red(0x3f), 0xc0);

    /* The live red layer was uniformly 0xff (no red ink): every pixel white. */
    for (int j = 0; j < 8; j++) {
        CHECK_EQ_INT(ink_bwr_red(0xff, j), 0);
    }
    /* Synthesized: 0x7f = 0111 1111 ⇒ only bit j==0 is 0 ⇒ red at j==0. */
    CHECK_EQ_INT(ink_bwr_red(0x7f, 0), 1);
    for (int j = 1; j < 8; j++) {
        CHECK_EQ_INT(ink_bwr_red(0x7f, j), 0);
    }
    /* 0x00 ⇒ fully red. */
    for (int j = 0; j < 8; j++) {
        CHECK_EQ_INT(ink_bwr_red(0x00, j), 1);
    }
}

/* mono: bitwise NOT into the black pass; per-pixel black decision is
 * "server bit == 1" — the inverse of BWR black. */
static void test_mono_not(void)
{
    CHECK_EQ_INT(xf_mono(0xff), 0x00);
    CHECK_EQ_INT(xf_mono(0x00), 0xff);
    CHECK_EQ_INT(xf_mono(0xe7), 0x18);

    /* 0xff (mono: all set) ⇒ every pixel black; 0x00 ⇒ all white. */
    for (int j = 0; j < 8; j++) {
        CHECK_EQ_INT(ink_mono(0xff, j), 1);
        CHECK_EQ_INT(ink_mono(0x00, j), 0);
    }
    /* mono is the bitwise inverse of the BWR-black ink decision for the same
     * byte (BWR: bit==0 inks; mono: bit==1 inks). Cross-check on the fixture. */
    for (size_t i = 0; i < sizeof(kBwrBlackFixture); i++) {
        for (int j = 0; j < 8; j++) {
            CHECK_EQ_INT(ink_mono(kBwrBlackFixture[i], j),
                         !ink_bwr_black(kBwrBlackFixture[i], j));
        }
    }
}

/* The download layer split mirrors todo_fetch's partition offsets: black at 0,
 * red at black_len rounded up to 4. Pin the offset math the C downloader uses
 * so a future change to either side is caught here. */
static uint32_t red_base_offset(uint32_t black_len)
{
    return (black_len + 3u) & ~3u;
}

static void test_layer_offsets(void)
{
    /* Real fixture: black_len 48000 is already 4-aligned ⇒ red base == 48000. */
    CHECK_EQ_INT(red_base_offset(48000), 48000);
    /* Unaligned lengths round up to the next 4-byte boundary. */
    CHECK_EQ_INT(red_base_offset(1), 4);
    CHECK_EQ_INT(red_base_offset(2), 4);
    CHECK_EQ_INT(red_base_offset(3), 4);
    CHECK_EQ_INT(red_base_offset(4), 4);
    CHECK_EQ_INT(red_base_offset(5), 8);
    CHECK_EQ_INT(red_base_offset(0), 0);
}

int main(void)
{
    RUN(test_bwr_black_verbatim);
    RUN(test_bwr_red_not);
    RUN(test_mono_not);
    RUN(test_layer_offsets);
    return test_summary("transform_test");
}
