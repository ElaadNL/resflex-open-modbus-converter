// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

/**
 * @file ap_wifi.h
 * @brief AP-side WiFi configuration for the captive portal.
 *
 * eth_wifi_component is responsible for bringing the WiFi stack up in APSTA
 * mode and creating both netifs. The functions declared here then push the
 * AP-specific settings (SSID/password/channel) onto WIFI_IF_AP and force
 * the AP netif to the configured captive-portal IP.
 *
 * NVS keys:
 *   - `ap_password` — string. On first boot a random 8-character alphanumeric
 *     password (A-Z0-9) is generated and persisted. The current `/config` form
 *     does not expose this value.
 */

#ifdef __cplusplus
extern "C" {
#endif


/** @brief AP SSID prefix; the device's MAC-derived suffix is appended at runtime. */
#ifndef AP_WIFI_SSID_PREFIX
#define AP_WIFI_SSID_PREFIX "S2MB_"
#endif

/** @brief 2.4 GHz channel the AP broadcasts on (1–13). Default 6 is least busy in most regions. */
#ifndef AP_WIFI_CHANNEL
#define AP_WIFI_CHANNEL 6
#endif

/** @brief Maximum number of stations that may associate with the AP at once. */
#ifndef AP_WIFI_MAX_CLIENTS
#define AP_WIFI_MAX_CLIENTS 4
#endif



/**
 * @brief AP gateway / portal IP, split into four octets.
 *
 * Set deliberately to a public-space IP (4.3.2.1) — Samsung Android refuses
 * to show the captive-portal popup when the gateway sits in RFC-1918
 * private space, because it treats the network as "local-only, no internet"
 * and skips the captive-detection flow. Public-space IPs trick the OS into
 * running the full probe + redirect path. The CDFER/Captive-Portal-ESP32
 * reference uses the same trick and notes it as the "Samsung requires the
 * IP to be in public space" workaround.
 * @{
 */
#define AP_WIFI_IP_A 4
#define AP_WIFI_IP_B 3
#define AP_WIFI_IP_C 2
#define AP_WIFI_IP_D 1
/** @} */


/**
 * @brief Apply the AP radio configuration (SSID, password, channel, authmode).
 *
 * Pushes a wifi_config_t onto WIFI_IF_AP. The SSID is built as
 * `AP_WIFI_SSID_PREFIX` + 6 hex chars derived from the MAC; the
 * password is read from NVS (auto-generated on first boot). Falls back to
 * an open AP if the stored password is shorter than 8 characters (WPA2-PSK
 * requires ≥ 8).
 *
 * @note Pre-condition: WiFi mode is APSTA. May be called before or after
 *       esp_wifi_start(); for new APs we recommend before so the radio
 *       comes up with the right params on the first beacon.
 */
void ap_wifi_configure_radio(void);

/**
 * @brief Apply the AP netif IP configuration and (re)start its DHCP server.
 *
 * Stops the DHCP server, sets ip/gateway to the configured captive-portal
 * AP IP with a /24 netmask, then restarts DHCP so leases hand out on the
 * same subnet. This is what makes the portal reachable at the well-known
 * AP IP from any connected client.
 *
 * @note Pre-condition: esp_wifi_start() must have run. Before that the
 *       netif isn't fully wired up and these calls don't stick. Logs an
 *       error and returns silently if the AP netif cannot be located.
 */
void ap_wifi_configure_netif(void);

#ifdef __cplusplus
}
#endif
