// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Native-test stub for lwip/ip4_addr.h.
//
// ap_wifi.cpp configures the AP IP with IP4_ADDR() and reads it back through
// two different shapes: esp_netif_ip_info_t carries esp_ip4_addr_t (a union
// exposing a top-level .addr and .bytes[4]), while esp_netif_dns_info_t
// carries lwIP's ip_addr_t (exposing u_addr.ip4.addr / u_addr.addr). Both
// are reproduced here to match the component's access paths exactly.

#include <stdint.h>

// ESP-IDF's esp_netif_ip_info_t address type.
typedef union {
    uint32_t addr;
    uint8_t bytes[4];
} esp_ip4_addr_t;

// lwIP's ip_addr_t, used by esp_netif_dns_info_t.
typedef struct {
    uint32_t addr;
} ip4_addr_t;

typedef union {
    uint32_t addr;
    ip4_addr_t ip4;
    uint8_t bytes[4];
} ip_addr_u_t;

typedef struct {
    ip_addr_u_t u_addr;
} ip_addr_t;

#define IP4_ADDR(ipaddr, a, b, c, d) do { \
    (ipaddr)->addr = ((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d); \
} while(0)

#define IPSTR "%d.%d.%d.%d"
#define IP2STR(ipaddr) ((ipaddr)->bytes[0]), ((ipaddr)->bytes[1]), ((ipaddr)->bytes[2]), ((ipaddr)->bytes[3])
