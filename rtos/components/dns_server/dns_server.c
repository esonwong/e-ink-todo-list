/* dns_server.c — captive-portal UDP DNS catcher (item 20).
 *
 * Single UDP socket on port 53; for every inbound query it parses the
 * question, appends a synthesised A answer pointing at the softAP IP, and
 * echoes the packet back. Any A lookup the client makes (captive probes,
 * arbitrary hostnames) therefore resolves to the device — the standard
 * captive-portal trick from the IDF captive_portal example, ported to the
 * v3.4 lwIP BSD socket layer.
 */
#include "dns_server.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "lwip/sockets.h"

static const char *TAG = "dns_server";

#define DNS_PORT          53
#define DNS_MAX_LEN       256       /* DNS over UDP without EDNS is <=512; portal queries are tiny */
#define DNS_QR_FLAG       (1 << 7)
#define DNS_OPCODE_MASK   0x7800
#define DNS_QTYPE_A       1
#define DNS_QCLASS_IN     1
#define DNS_ANSWER_TTL    300

/* 12-byte DNS message header (network byte order). */
typedef struct __attribute__((__packed__)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

/* The fixed trailer appended after the (echoed) question to form one A answer:
 * a name pointer to offset 0x0C (the question), then type/class/ttl/rdlen/rdata. */
typedef struct __attribute__((__packed__)) {
    uint16_t ptr_offset;   /* 0xC00C — compression pointer to the question name */
    uint16_t type;         /* A */
    uint16_t class;        /* IN */
    uint32_t ttl;
    uint16_t addr_len;     /* 4 */
    uint32_t addr;         /* the softAP IP, network order */
} dns_answer_t;

static TaskHandle_t s_task;
static volatile int s_sock = -1;
static ip4_addr_t   s_resolve_to;

/* Parse one query and, if it is a single A question we can echo, build the
 * response into resp (capacity resp_cap). Returns the response length, or 0 to
 * drop the packet. */
static size_t build_response(const uint8_t *req, size_t req_len,
                             uint8_t *resp, size_t resp_cap)
{
    if (req_len < sizeof(dns_header_t)) {
        return 0;
    }

    dns_header_t hdr;
    memcpy(&hdr, req, sizeof(hdr));

    /* Only answer standard queries (QR=0, opcode 0) with at least one question. */
    uint16_t flags = ntohs(hdr.flags);
    if ((flags & DNS_QR_FLAG) || (flags & DNS_OPCODE_MASK)) {
        return 0;
    }
    if (ntohs(hdr.qd_count) != 1) {
        return 0;
    }

    /* Walk the QNAME labels to find the 4-byte QTYPE/QCLASS that follow it. */
    size_t qname_start = sizeof(dns_header_t);
    size_t p = qname_start;
    while (p < req_len && req[p] != 0) {
        p += req[p] + 1;          /* length byte + label */
        if (p > req_len) {
            return 0;
        }
    }
    if (p >= req_len) {
        return 0;                 /* unterminated name */
    }
    p += 1;                       /* skip the zero length byte */
    if (p + 4 > req_len) {
        return 0;                 /* no room for QTYPE/QCLASS */
    }

    size_t question_len = p + 4 - qname_start;
    size_t resp_len = sizeof(dns_header_t) + question_len + sizeof(dns_answer_t);
    if (resp_len > resp_cap) {
        return 0;
    }

    /* Header: copy the request id, set response flags, one answer. */
    dns_header_t rhdr = hdr;
    rhdr.flags    = htons(0x8180);    /* QR=1, RD=1, RA=1, rcode=0 */
    rhdr.an_count = htons(1);
    rhdr.ns_count = 0;
    rhdr.ar_count = 0;
    memcpy(resp, &rhdr, sizeof(rhdr));

    /* Echo the question verbatim. */
    memcpy(resp + sizeof(dns_header_t), req + qname_start, question_len);

    /* Append the synthesised A answer. */
    dns_answer_t ans;
    ans.ptr_offset = htons(0xC000 | qname_start);
    ans.type       = htons(DNS_QTYPE_A);
    ans.class      = htons(DNS_QCLASS_IN);
    ans.ttl        = htonl(DNS_ANSWER_TTL);
    ans.addr_len   = htons(4);
    ans.addr       = s_resolve_to.addr;   /* already network byte order */
    memcpy(resp + sizeof(dns_header_t) + question_len, &ans, sizeof(ans));

    return resp_len;
}

static void dns_task(void *arg)
{
    uint8_t rx[DNS_MAX_LEN];
    uint8_t tx[DNS_MAX_LEN];

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket() failed: errno %d", errno);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = { 0 };
    bind_addr.sin_family      = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port        = htons(DNS_PORT);
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind() failed: errno %d", errno);
        close(sock);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    s_sock = sock;
    ESP_LOGI(TAG, "captive DNS up, resolving all A queries to %s",
             ip4addr_ntoa(&s_resolve_to));

    while (1) {
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        int n = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&src, &src_len);
        if (n < 0) {
            /* dns_server_stop() closed the socket — exit cleanly. */
            ESP_LOGI(TAG, "socket closed, DNS task exiting");
            break;
        }
        size_t reply_len = build_response(rx, (size_t)n, tx, sizeof(tx));
        if (reply_len > 0) {
            sendto(sock, tx, reply_len, 0, (struct sockaddr *)&src, src_len);
        }
    }

    s_sock = -1;
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t dns_server_start(ip4_addr_t resolve_to)
{
    if (s_task != NULL) {
        ESP_LOGW(TAG, "DNS server already running");
        return ESP_OK;
    }
    s_resolve_to = resolve_to;

    /* prio 4 per architecture §1 task table; self-contained socket loop. */
    if (xTaskCreate(dns_task, "dns_server", 2048, NULL, 4, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "failed to create DNS task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

void dns_server_stop(void)
{
    int sock = s_sock;
    if (sock >= 0) {
        s_sock = -1;
        /* Closing the socket unblocks recvfrom(); the task self-deletes. */
        close(sock);
    }
}
