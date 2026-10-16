// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

/**
 * @file captive_portal_component.h
 * @brief Public entry points for the captive portal.
 *
 * The captive portal runs alongside the STA / Ethernet connection in APSTA
 * mode so a configuration UI is always reachable, even when STA creds are
 * missing or wrong.
 *
 * Responsibilities of this component:
 *  - Run a tiny native DNS responder on port 53 that points every host to
 *    the AP IP. See captive_portal_dns.h.
 *  - Start an esp_http_server and register the OS phone-home URIs + the
 *    `/config` and `/appliance` pages. See captive_portal_http_server.h
 *    and captive_portal_http_handlers.h.
 *
 * Internal split (alphabetical):
 *  - captive_portal_component       — public start/stop entry points
 *  - captive_portal_dns             — UDP/53 wildcard responder task
 *  - captive_portal_dns_message     — pure DNS A-response builder
 *  - captive_portal_html_fragments  — raw HTML/CSS/JS string literals
 *  - captive_portal_http_handlers   — route table + handlers + page renderers
 *  - captive_portal_http_server     — httpd lifecycle
 *  - captive_portal_nvs             — NVS-write policies for form submissions
 *  - captive_portal_parsing         — ESP-independent parser helpers
 *
 * NVS keys read/written via storage_component:
 *   - `wifi_ssid`, `wifi_psk`                      — STA credentials
 *   - `hems_url`                                   — S2 / HEMS endpoint
 *   - `pair_key`                                   — S2 pairing key
 *   - `net_use_static_ip`                          — 0/1 flag for static IP vs DHCP
 *   - `net_ip_addr`, `net_netmask`, `net_gateway`  — static IP settings
 *   - `ap_password`                                — AP password (see ap_wifi.h)
 *
 * Pre-conditions before @ref captive_portal_start :
 *   - init_storage_component() has been called.
 *   - esp_netif_init() and esp_event_loop_create_default() have been called
 *     (eth_wifi_component takes care of this today).
 *   - WiFi is initialised in APSTA mode with both default netifs created
 *     and esp_wifi_start() has run — i.e. init_eth_wifi() has returned.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring the captive portal online.
 *
 * Starts the wildcard DNS responder and HTTP server. Safe to call once after
 * `init_eth_wifi()` has configured and started the APSTA network stack.
 *
 * @return true if both DNS and HTTP came up; false otherwise (DNS is rolled
 *         back if HTTP fails, so callers never end up with half-started
 *         state).
 */
bool captive_portal_start(void);

/**
 * @brief Stop the captive portal.
 *
 * Tears down the DNS task and the HTTP server. The AP netif stays up until
 * WiFi itself is torn down — leave that to eth_wifi_component. Safe to call
 * even if @ref captive_portal_start was never called or already failed.
 */
void captive_portal_stop(void);

#ifdef __cplusplus
}
#endif
