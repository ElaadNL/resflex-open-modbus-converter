// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "ap_wifi.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "lwip/ip4_addr.h"
#include "dhcpserver/dhcpserver.h"

#include "storage_component.h"

static const char *TAG = "CAPTIVE_AP";

/**
 * @brief Build a 6-character hex suffix from the device MAC.
 *
 * Uses bytes 3..5 of the base MAC (the OUI-free, device-unique portion) so
 * every unit advertises a unique AP SSID. Output is NUL-terminated.
 *
 * @param out      Destination buffer.
 * @param out_size Size of @p out in bytes (must be ≥ 7 for the full suffix).
 */
static void build_device_unique_suffix(char *out, size_t out_size) {
    uint8_t mac[6] = {};
    esp_efuse_mac_get_default(mac);
    snprintf(out, out_size, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

/**
 * @brief Load the AP password from NVS, or generate and persist one.
 *
 * On first boot the NVS key `ap_password` is empty; we generate an 8-character
 * random password (A-Z0-9) and store it. Stored in NVS so it survives
 * reboots. The /config form can overwrite the stored value later
 *
 * @param pwd      Destination buffer for the password (NUL-terminated).
 * @param pwd_size Size of @p pwd in bytes (must be ≥ 9 for the default).
 */
static void load_or_init_ap_password(char *pwd, size_t pwd_size) {
    storage_get_configuration("ap_password", pwd, pwd_size, "");
    if (pwd[0] != '\0') {
        return;
    }

    const char availableChars[] = "123456789ABCDEFGHJKLMNPQRSTUVWXY";
    uint8_t randomBytes[8];
    esp_fill_random(randomBytes, sizeof(randomBytes));
    for (int i = 0; i < 8; i++) {
        pwd[i] = availableChars[randomBytes[i] % (sizeof(availableChars) - 1)];
    }
    pwd[8] = '\0';
    storage_set_configuration("ap_password", pwd);
    ESP_LOGI(TAG, "AP password not set yet; generated random alphanumeric (capitalized) password and stored.");
}

/**
 * @brief Push the AP radio configuration onto WIFI_IF_AP.
 *
 * Builds an SSID of the form `<AP_WIFI_SSID_PREFIX><MAC suffix>`,
 * uses the password from @ref load_or_init_ap_password, and selects WPA2-PSK
 * if the password is long enough (≥ 8 chars) — otherwise falls back to an
 * open AP so the radio still comes up. Channel and max-clients are taken
 * from the @ref captive_portal_component.h defines.
 *
 * Pre-condition: WiFi must be in APSTA mode and the stack must be
 * initialised (typically done by @c init_eth_wifi).
 */
void ap_wifi_configure_radio(void) {
    char unique_suffix[8] = {};
    build_device_unique_suffix(unique_suffix, sizeof(unique_suffix));

    char ap_pwd[64] = {};
    load_or_init_ap_password(ap_pwd, sizeof(ap_pwd));

    wifi_config_t ap_config = {};
    int ssid_len = snprintf((char *)ap_config.ap.ssid, sizeof(ap_config.ap.ssid), "%s%s", AP_WIFI_SSID_PREFIX, unique_suffix);
    ap_config.ap.ssid_len = (ssid_len > 0) ? ssid_len : 0;
    strlcpy((char *)ap_config.ap.password, ap_pwd, sizeof(ap_config.ap.password));

    ap_config.ap.channel = AP_WIFI_CHANNEL;
    ap_config.ap.max_connection = AP_WIFI_MAX_CLIENTS;
    // WPA2-PSK requires an 8+ char password. If the stored password is too
    // short, pad it with "_" + a MAC-derived suffix (bytes 2..5, taken from
    // the back) so the total length is at least 8.
    size_t pwd_len = strnlen(ap_pwd, sizeof(ap_pwd));
    if (pwd_len < 8) {
        uint8_t mac[6] = {};
        esp_efuse_mac_get_default(mac);
        char mac_hex[9] = {};
        snprintf(mac_hex, sizeof(mac_hex), "%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
        size_t suffix_len = (pwd_len <= 6) ? (7 - pwd_len) : 1;
        strlcat((char *)ap_config.ap.password, "_", sizeof(ap_config.ap.password));
        strlcat((char *)ap_config.ap.password, mac_hex + (8 - suffix_len), sizeof(ap_config.ap.password));
    }
    ap_config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;

    ESP_LOGI(TAG, "AP password is %s", (char *)ap_config.ap.password);
    ap_config.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_LOGI(TAG, "AP up: SSID='%s' channel=%d authmode=WPA2/WPA3-PSK",
             ap_config.ap.ssid,
             ap_config.ap.channel);
}

/**
 * @brief Force the AP netif to 4.3.2.1/24 and restart its DHCP server.
 *
 * Stops the DHCP server, applies the captive-portal IP/gateway/netmask via
 * @c esp_netif_set_ip_info, then restarts DHCP so leases hand out on the
 * same subnet. This is what makes the portal reachable at the well-known
 * AP IP from every connected client.
 *
 * Pre-condition: @c esp_wifi_start has been called — before that the netif
 * is not fully wired up and these calls don't stick. Logs an error and
 * returns silently if the AP netif cannot be located.
 */
void ap_wifi_configure_netif(void) {
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ap_netif == nullptr) {
        ESP_LOGE(TAG, "AP netif not found — was esp_netif_create_default_wifi_ap() called?");
        return;
    }

    // DHCP server must be stopped before we touch the IP info.
    esp_netif_dhcps_stop(ap_netif);

    esp_netif_ip_info_t ip_info = {};
    IP4_ADDR(&ip_info.ip, AP_WIFI_IP_A, AP_WIFI_IP_B, AP_WIFI_IP_C, AP_WIFI_IP_D);
    IP4_ADDR(&ip_info.gw, AP_WIFI_IP_A, AP_WIFI_IP_B, AP_WIFI_IP_C, AP_WIFI_IP_D);
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0);
    esp_netif_set_ip_info(ap_netif, &ip_info);

    // Advertise ourselves as the DNS server via DHCP option 6. Without this,
    // clients fall back to whatever DNS they had before (mobile data, cached
    // upstream) and our wildcard DNS responder never sees a query — which is
    // why the captive-portal pop-up never triggers even though HTTP works.
    {
        dhcps_offer_t offer_dns = OFFER_DNS;
        esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
                               ESP_NETIF_DOMAIN_NAME_SERVER,
                               &offer_dns, sizeof(offer_dns));
        esp_netif_dns_info_t dns = {};
        dns.ip.u_addr.ip4.addr = ip_info.ip.addr;
        dns.ip.type = ESP_IPADDR_TYPE_V4;
        esp_netif_set_dns_info(ap_netif, ESP_NETIF_DNS_MAIN, &dns);
    }

    esp_netif_dhcps_start(ap_netif);
    ESP_LOGI(TAG, "AP netif configured at " IPSTR " (DNS=self)", IP2STR(&ip_info.ip));
}
