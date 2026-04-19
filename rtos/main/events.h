/* events.h — shared queue payloads, queue/event-group handles, sys_events bits.
 *
 * G7 invariant (binding): every queue payload below is a fixed-size struct
 * copied by value by the queue — NO POINTERS INTO TASK-LOCAL BUFFERS, EVER.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

/* sys_events bits */
#define BIT_WIFI_CONNECTED (1 << 0)
#define BIT_PORTAL_ACTIVE  (1 << 1)
#define BIT_TIME_SYNCED    (1 << 2)
#define BIT_DISP_DONE      (1 << 3)
#define BIT_DISP_ABORTED   (1 << 4)

/* VALUE-COPIED: no pointers into task-local buffers */
typedef struct {                 /* cmd_queue, depth 4, button/wifi/httpd → cycle_task */
    enum { CMD_FORCE_REFRESH, CMD_DEVICE_EVENT,
           CMD_ERASE_PVALUE, CMD_API_KEY_UPDATED } type;
    char arg[42];                /* event name ("double-click") or api key, NUL-terminated */
} app_cmd_t;

/* VALUE-COPIED: no pointers into task-local buffers */
typedef struct {                 /* wifi_cmd_queue, depth 4, button/httpd → wifi_ctrl_task */
    enum { WIFI_CMD_PORTAL_START, WIFI_CMD_PORTAL_STOP, WIFI_CMD_TOGGLE,
           WIFI_CMD_RESET_AND_PORTAL, WIFI_CMD_SAVE } type;
    struct { char ssid[33]; char pass[65]; char api_key[42]; } save;  /* WIFI_CMD_SAVE only */
} wifi_cmd_t;

/* VALUE-COPIED: no pointers into task-local buffers */
typedef struct {                 /* disp_queue, depth 4, any task → display_task */
    enum { DISP_LAUNCH, DISP_TEXT_CENTER, DISP_AP_INFO, DISP_WIFI_CONNECTED,
           DISP_AUTH_401, DISP_NO_CONTENT, DISP_PORTAL_TIMEOUT,
           DISP_RENDER_BITMAP, DISP_TEST, DISP_PCB_INFO } kind;
    char text[64];               /* center-text / AP info lines */
    struct { uint16_t w, h; uint32_t black_len, red_len; bool bwr; } bitmap; /* RENDER only */
} display_req_t;                 /* ~120 B */

/* Queue / event-group handles — created in app_main before any task starts
 * (definitions live in main/app_main.c). */
extern QueueHandle_t cmd_queue;       /* app_cmd_t, depth 4 */
extern QueueHandle_t wifi_cmd_queue;  /* wifi_cmd_t, depth 4 */
extern QueueHandle_t disp_queue;      /* display_req_t, depth 4 */
extern EventGroupHandle_t sys_events;

#ifdef __cplusplus
}
#endif
