// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "captive_portal_http_server.h"

#include <string.h>

#include "esp_log.h"
#include "esp_http_server.h"
#include "lwip/sockets.h"

#include "../eth_wifi_component/ap_wifi.h"
#include "captive_portal_http_handlers.h"

static const char *TAG = "CAPTIVE_HTTP";

static httpd_handle_t s_httpd = nullptr;

/**
 * @brief Reject any HTTP session that did not arrive on the AP IP (4.3.2.1).
 *
 * esp_http_server always listens on INADDR_ANY, so the listen socket also
 * accepts connections routed in over the STA interface. This open callback
 * runs right after accept() and before any data is read; it reads the local
 * (destination) address of the freshly accepted socket and returns an error —
 * which makes esp_http_server close the socket immediately — for anything that
 * did not come in on the AP IP. The captive portal is AP-only.
 *
 * @param httpd Server handle (unused).
 * @param sockfd The freshly accepted client socket.
 * @return ESP_OK to keep the session, ESP_FAIL to close it.
 */
static esp_err_t reject_non_ap_session(httpd_handle_t httpd, int sockfd) {
    // Read the local (destination) address the client connected to. If this
    // fails we cannot tell which interface the session arrived on, so reject
    // it rather than risk serving a session that came in over STA. Use
    // sockaddr_storage: with CONFIG_LWIP_IPV6 the listen socket is PF_INET6,
    // so getsockname() returns a sockaddr_in6 here, not a sockaddr_in.
    struct sockaddr_storage local_addr = {};
    socklen_t local_len = sizeof(local_addr);
    if (getsockname(sockfd, (struct sockaddr *)&local_addr, &local_len) < 0) {
        ESP_LOGW(TAG, "getsockname() failed on sockfd %d — rejecting session", sockfd);
        return ESP_FAIL;
    }

    // Extract the local IPv4 address in network byte order. IPv4 clients on an
    // IPv6 listen socket appear as IPv4-mapped IPv6 (::ffff:a.b.c.d), so pull
    // the last four bytes of the mapped address; a plain AF_INET socket gives
    // the address directly.
    uint32_t local_ipv4 = 0;
    if (local_addr.ss_family == AF_INET6) {
        const struct sockaddr_in6 *addr6 = (const struct sockaddr_in6 *)&local_addr;
        memcpy(&local_ipv4, &addr6->sin6_addr.s6_addr[12], sizeof(local_ipv4));
    }
    else if (local_addr.ss_family == AF_INET) {
        const struct sockaddr_in *addr4 = (const struct sockaddr_in *)&local_addr;
        local_ipv4 = addr4->sin_addr.s_addr;
    }
    else {
        ESP_LOGW(TAG, "Unexpected address family %d on sockfd %d — rejecting session", local_addr.ss_family, sockfd);
        return ESP_FAIL;
    }

    // Keep only sessions that arrived on the AP IP (4.3.2.1). Anything with a
    // different local address came in over the STA interface — reject it, so
    // the captive portal stays reachable from AP clients only.
    uint32_t ap_ip = htonl(
        (AP_WIFI_IP_A << 24) | (AP_WIFI_IP_B << 16) | (AP_WIFI_IP_C << 8) | AP_WIFI_IP_D);
    if (local_ipv4 != ap_ip) {
        ESP_LOGD(TAG, "Rejecting non-AP session on sockfd %d", sockfd);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Thin wrapper around httpd_register_uri_handler that fills in the
 *        boilerplate fields. Passed to the handler file so it can register
 *        its own URIs without knowing the httpd_uri_t layout.
 */
static void register_uri(httpd_handle_t httpd, const char *uri, httpd_method_t method, esp_err_t (*fn)(httpd_req_t *)) {
    httpd_uri_t u = {};
    u.uri = uri;
    u.method = method;
    u.handler = fn;
    u.user_ctx = nullptr;
    httpd_register_uri_handler(httpd, &u);
}

bool captive_portal_http_server_start(void) {
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 20;
    cfg.lru_purge_enable = true;
    cfg.max_req_hdr_len = 1024;
    cfg.stack_size = 8192;
    cfg.max_open_sockets = 4;
    cfg.recv_wait_timeout = 5;
    cfg.send_wait_timeout = 5;
    cfg.keep_alive_enable = true;
    cfg.open_fn = reject_non_ap_session;

    if (httpd_start(&s_httpd, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        s_httpd = nullptr;
        return false;
    }

    captive_portal_http_handlers_register(s_httpd, register_uri);
    return true;
}

void captive_portal_http_server_stop(void) {
    if (s_httpd != nullptr) {
        httpd_stop(s_httpd);
        s_httpd = nullptr;
    }
}
