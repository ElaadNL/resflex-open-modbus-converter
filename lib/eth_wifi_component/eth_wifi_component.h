// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include "esp_netif.h"

/**
 * @brief Initialize Ethernet, WiFi STA and WiFi AP networking.
 *
 * Creates the network event group, initializes optional Ethernet, loads STA
 * credentials from NVS/defaults, starts WiFi in APSTA mode, and applies the
 * captive-portal AP radio/netif configuration.
 */
void init_eth_wifi();

/**
 * @brief Resolve which interface should carry outbound IP traffic right now.
 *
 * Honours NVS_STORAGE_KEY_HEMS_NETWORK_PREFER_WIFI. When the flag is set the
 * WiFi STA netif is returned; otherwise the Ethernet netif is preferred
 * when present, falling back to WiFi STA. Returns @c nullptr when neither
 * netif exists yet (very early in boot).
 */
esp_netif_t *eth_wifi_get_active_netif();

/**
 * @brief Copy the active outbound interface name into @p interface_name.
 *
 * The S2 wrapper uses this to bind outbound S2 traffic to the same interface
 * that @ref eth_wifi_get_active_netif returns. Picks Ethernet by default and
 * WiFi STA when the user-chosen prefer-WiFi flag is set.
 *
 * @param interface_name Destination buffer for the interface name.
 * @param interface_name_size Size of @p interface_name in bytes.
 * @return true when a non-empty interface name was copied, false otherwise.
 */
bool eth_wifi_get_active_interface_name(char *interface_name, size_t interface_name_size);

/**
 * @brief Apply the user-chosen default route for outbound IP traffic.
 *
 * Reads @c NVS_STORAGE_KEY_HEMS_NETWORK_PREFER_WIFI and points the lwIP
 * default netif at WiFi STA when the flag is set, otherwise at Ethernet
 * (with WiFi STA as fallback if no ETH netif exists). Safe to call again
 * after the user toggles the flag in the captive portal — but in practice
 * the captive portal reboots after save so a single call from boot is
 * sufficient.
 *
 * @return true when the route preference was applied, false on error.
 */
bool eth_wifi_apply_default_route();
