// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "captive_portal_dns.h"

#include <stdint.h>
#include <errno.h>
#include <string.h>

#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "../eth_wifi_component/ap_wifi.h"
#include "captive_portal_dns_message.h"

static const char *TAG = "CAPTIVE_DNS";

static TaskHandle_t s_dns_task = nullptr;
static volatile bool s_dns_task_run = false;

#define DNS_PORT 53
#define DNS_RX_BUF_SIZE 512 // RFC-1035 UDP message size limit

// ── DNS responder ────────────────────────────────────────────────────────────
//
// Wildcard DNS: every query is answered with a single A-record pointing at the
// AP IP, so phones/PCs route their captive-portal probe to our HTTP server.
//
// We don't parse the query — we copy the request, flip the header to "response",
// and append one answer that uses a name-pointer (0xC00C) back to the query name
// at offset 12. This is RFC-1035 message-compression and avoids re-emitting the
// QNAME. Works for QTYPE A and AAAA (clients reading an A-answer for an AAAA
// query simply fall back, which is fine).
//
// DNS header (12 bytes):
//   [0..1] ID
//   [2]    flags hi: QR(1)|Opcode(4)|AA(1)|TC(1)|RD(1)
//   [3]    flags lo: RA(1)|Z(3)|RCODE(4)
//   [4..5] QDCOUNT
//   [6..7] ANCOUNT
//   [8..9] NSCOUNT
//   [10..11] ARCOUNT



/**
 * @brief FreeRTOS task that runs the wildcard DNS responder.
 *
 * Opens a UDP socket on port 53, accepts queries from any AP client, and
 * answers each one with a single A-record pointing at the AP IP. Loops
 * until @ref s_dns_task_run is cleared; the short recv timeout makes that
 * observable within a second.
 *
 * Self-deletes on exit. Errors from socket()/bind() bail out early and also
 * self-delete.
 *
 * @param arg Unused — required by xTaskCreate signature.
 */
static void dns_responder_task(void *arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns: socket() failed: errno %d", errno);
        s_dns_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // Short recv timeout so we can observe s_dns_task_run for clean shutdown.
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // Bind the socket to the AP interface itself, so the responder only ever
    // receives datagrams that arrived on the AP netif and never on the STA
    // interface. The captive portal must be reachable from AP clients only.
    //
    // This restricts by interface (SO_BINDTODEVICE), not by destination IP:
    // it survives a future AP-IP change and expresses the actual intent. The
    // lwIP interface name (e.g. "ap1") comes from the AP netif handle, not
    // from the "WIFI_AP_DEF" ifkey string.
    // Look up the AP netif by its ifkey. Without this handle we cannot resolve
    // the lwIP interface name, so there is nothing to bind the socket to.
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif == nullptr) {
        ESP_LOGE(TAG, "dns: AP netif not found — cannot bind to AP interface");
        close(sock);
        s_dns_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // Resolve the lwIP interface name (e.g. "ap1") that SO_BINDTODEVICE expects.
    // The ifkey string ("WIFI_AP_DEF") is not the same as the lwIP name.
    struct ifreq ap_interface = {};
    if (esp_netif_get_netif_impl_name(ap_netif, ap_interface.ifr_name) != ESP_OK) {
        ESP_LOGE(TAG, "dns: could not resolve AP interface name");
        close(sock);
        s_dns_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    // Bind the socket to the AP interface. After this the socket only ever
    // receives datagrams that arrived on the AP netif, never on the STA side.
    if (setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &ap_interface, sizeof(ap_interface)) < 0) {
        ESP_LOGE(TAG, "dns: SO_BINDTODEVICE failed: errno %d", errno);
        close(sock);
        s_dns_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    struct sockaddr_in bind_addr = {};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(DNS_PORT);
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "dns: bind() failed: errno %d", errno);
        close(sock);
        s_dns_task = nullptr;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(TAG, "dns: responder listening on UDP/%d", DNS_PORT);

    uint8_t buf[DNS_RX_BUF_SIZE];
    while (s_dns_task_run) {
        struct sockaddr_in client = {};
        socklen_t client_len = sizeof(client);

        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&client, &client_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; // recv timeout, loop and re-check shutdown flag
            }
            ESP_LOGW(TAG, "dns: recvfrom() errno %d", errno);
            continue;
        }

        static const uint8_t ap_ip[4] = {
            AP_WIFI_IP_A,
            AP_WIFI_IP_B,
            AP_WIFI_IP_C,
            AP_WIFI_IP_D,
        };

        int resp_len = captive_build_dns_response(buf, n, sizeof(buf), ap_ip);
        if (resp_len < 0) {
            ESP_LOGD(TAG, "dns: malformed query (%d bytes) — dropping", n);
            continue;
        }

        int sent = sendto(sock, buf, resp_len, 0, (struct sockaddr *)&client, client_len);
        if (sent < 0) {
            ESP_LOGW(TAG, "dns: sendto() errno %d", errno);
        }
    }

    close(sock);
    ESP_LOGI(TAG, "dns: responder stopped");
    s_dns_task = nullptr;
    vTaskDelete(nullptr);
}



bool captive_portal_dns_start(void) {
    s_dns_task_run = true;
    BaseType_t ok = xTaskCreatePinnedToCore(
        dns_responder_task, "captive_dns", 4096, nullptr, 5, &s_dns_task, 0);
    return ok == pdPASS;
}



void captive_portal_dns_stop(void) {
    s_dns_task_run = false;
    // dns_responder_task self-deletes after observing the flag on its next
    // recv timeout (≤ 1s).
}
