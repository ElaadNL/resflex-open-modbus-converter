// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Strict dotted-decimal IPv4 parser.
 *
 * Accepts exactly three '.' separators, each octet in range 0..255. Rejects
 * leading zeros (except the literal "0"), embedded spaces, empty octets, and
 * any trailing characters. On failure @p out is left unchanged.
 *
 * Kept independent of ESP-IDF so it is reachable from the native test
 * environment. The ESP-IDF path uses esp_netif_str_to_ip4 directly; this
 * parser exists so the captive-portal-fed strings can be validated before
 * the netif calls.
 *
 * @param str  Null-terminated source string (e.g. "192.168.1.10").
 * @param out  Destination for the four octets in network order (out[0] is
 *             the most significant octet, e.g. 192 for "192.168.1.10").
 * @return true when @p str is a valid IPv4 address, false otherwise.
 */
bool eth_wifi_parse_ipv4(const char *str, uint8_t out[4]);

#ifdef __cplusplus
}
#endif