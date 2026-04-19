/* eink_display.h — public API of the e-ink display component (items 41-44,
 * 55-65). display_task is the sole owner of the panel; all screens are
 * requested via the disp_queue (display_req_t). Full bitmaps are rendered
 * through a caller-supplied band-reader (G12), so the driver knows nothing
 * about partitions — a streaming source could be swapped in without touching
 * the driver.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "events.h" /* display_req_t, disp_queue, sys_events, BIT_DISP_* */

#ifdef __cplusplus
extern "C" {
#endif

/* G12: band source is caller-supplied — the driver is panel/source-agnostic.
 * read() fills `len` bytes of `layer` (0=black, 1=red) starting at `offset`. */
typedef struct {
    esp_err_t (*read)(void *ctx, uint32_t layer /*0=black,1=red*/,
                      uint32_t offset, uint8_t *buf, size_t len);
    void *ctx;
} band_source_t;

/* Initialize panel pins/SPI and start display_task. Must run before any
 * display_request(). Idempotent guard inside. */
esp_err_t display_init(void);

/* Post a display request to the queue (value-copied). Returns pdTRUE on
 * enqueue. display_task sets exactly one of BIT_DISP_DONE / BIT_DISP_ABORTED
 * for every DISP_RENDER_BITMAP it services (G3); other kinds signal nothing. */
BaseType_t display_request(const display_req_t *req, TickType_t ticks);

/* display_task internals call this to render a full bitmap by band copy from
 * the supplied source. w/h may be smaller than the panel (row-stride copy).
 * BWR: black layer verbatim, red layer bitwise-NOT; mono: bitwise-NOT into the
 * black pass, red pass left white (R7 bit semantics). */
esp_err_t display_render_bands(const band_source_t *src, uint16_t w, uint16_t h,
                               uint32_t black_len, uint32_t red_len, bool bwr);

#ifdef __cplusplus
}
#endif
