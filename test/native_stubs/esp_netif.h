// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Native-test stub for esp_netif.h.
//
// eth_wifi_component.h includes esp_netif.h and is pulled into the native
// build transitively. No natively-compiled code calls any esp_netif
// function except ap_wifi.cpp, which needs the handle lookup plus the
// IP/DNS setters so the captive-portal netif configuration can be
// asserted in tests.

#include <string.h>

#include "esp_err.h"
#include "lwip/ip4_addr.h"

typedef struct esp_netif_obj {
    int _unused;
} esp_netif_t;

typedef struct {
    esp_ip4_addr_t ip;
    esp_ip4_addr_t gw;
    esp_ip4_addr_t netmask;
} esp_netif_ip_info_t;

typedef enum {
    ESP_NETIF_DNS_MAIN = 0,
    ESP_NETIF_DNS_SECOND = 1
} esp_netif_dns_index_t;

typedef enum {
    ESP_NETIF_DOMAIN_NAME_SERVER = 2
} esp_netif_dhcps_key_t;

typedef enum {
    ESP_NETIF_OP_SET = 0,
    ESP_NETIF_OP_REMOVE = 1
} esp_netif_op_t;

typedef enum {
    ESP_IPADDR_TYPE_V4 = 0,
    ESP_IPADDR_TYPE_V6 = 1
} esp_ipaddr_type_t;

// The DNS info carries an address the component reads through both
// .ip.u_addr.ip4.addr / .ip.u_addr.addr and a family selector via .ip.type,
// so the address is a struct that wraps the u_addr union with a type field.
typedef union {
    uint32_t addr;
    struct {
        uint32_t addr;
    } ip4;
    uint8_t bytes[4];
} esp_dns_ip_u_addr_t;

typedef struct {
    esp_dns_ip_u_addr_t u_addr;
    esp_ipaddr_type_t type;
} esp_dns_ip_t;

typedef struct {
    esp_dns_ip_t ip;
} esp_netif_dns_info_t;

// Native-test capture for the AP netif configuration.
namespace ap_wifi_test {
// nullptr = no netif found, otherwise a handle is returned.
extern bool netif_available;
// Last ip info passed to esp_netif_set_ip_info (valid when netif_available).
extern esp_netif_ip_info_t last_ip_info;
extern int set_ip_info_call_count;
// Last dns info passed to esp_netif_set_dns_info.
extern esp_netif_dns_info_t last_dns_info;
extern int set_dns_call_count;
// DHCP server lifecycle counters.
extern int dhcps_stop_call_count;
extern int dhcps_start_call_count;
extern int dhcps_option_call_count;

void reset();
}

esp_netif_t* esp_netif_get_handle_from_ifkey(const char* ifkey);
esp_err_t esp_netif_get_netif_impl_name(esp_netif_t* netif, char* name);
void esp_netif_set_ip_info(esp_netif_t* netif, const esp_netif_ip_info_t* info);
esp_err_t esp_netif_dhcps_stop(esp_netif_t* netif);
esp_err_t esp_netif_dhcps_start(esp_netif_t* netif);
esp_err_t esp_netif_dhcps_option(esp_netif_t* netif, esp_netif_op_t op, esp_netif_dhcps_key_t key, void* option, size_t size);
esp_err_t esp_netif_set_dns_info(esp_netif_t* netif, esp_netif_dns_index_t dns_index, const esp_netif_dns_info_t* dns);
