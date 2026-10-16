// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

// Native tests for lib/eth_wifi_component/ap_wifi.cpp.
//
// Covers the random AP password generation:
//   - On first boot an 8-character password from the A-Z0-9 alphabet is
//     generated and persisted to NVS.
//   - On subsequent boots the stored password is reused (no regeneration).
//   - The radio SSID is built from the prefix + MAC suffix.
// The storage backend is the shared native stub (test/native_stubs/stubs.cpp);
// the WiFi/netif layer is replaced by test/native_stubs/transport_stubs.cpp.

#include <unity.h>

#include <cctype>
#include <cstring>

#include "ap_wifi.h"
#include "esp_err.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "ap_wifi_test_helpers.h"
#include "ap_wifi_storage_stub.h"
#include "storage_component.h"

void setUp(void) {
    ap_wifi_test_helpers::reset_all();
    ap_wifi_storage_stub::reset();
}

void tearDown(void) {}

// ── AP radio: password ──────────────────────────────────────────────────────

void test_radio_generates_random_password_on_first_boot(void) {
    // No password seeded in NVS: first boot.
    ap_wifi_configure_radio();

    TEST_ASSERT_EQUAL_INT(1, ap_wifi_test::set_config_call_count);
    const char* password = reinterpret_cast<const char*>(ap_wifi_test::last_ap_config.ap.password);
    TEST_ASSERT_EQUAL_UINT8(8, static_cast<uint8_t>(std::strlen(password)));

    // Every character must come from the A-Z0-9 alphabet.
    for (const char* ch = password; *ch != '\0'; ++ch) {
        TEST_ASSERT_TRUE(std::isalnum(static_cast<unsigned char>(*ch)));
    }

    // The generated password must have been persisted to NVS.
    TEST_ASSERT_EQUAL_STRING(password, ap_wifi_storage_stub::generated_password.c_str());
    // And a subsequent get returns the generated value (now "stored").
    char stored[32] = {};
    TEST_ASSERT_TRUE(storage_get_configuration("ap_password", stored, sizeof(stored), ""));
    TEST_ASSERT_EQUAL_STRING(password, stored);
}

void test_radio_reuses_stored_password(void) {
    ap_wifi_storage_stub::seeded_password = "PRESET12";
    ap_wifi_configure_radio();

    TEST_ASSERT_EQUAL_INT(1, ap_wifi_test::set_config_call_count);
    TEST_ASSERT_EQUAL_STRING("PRESET12",
                             reinterpret_cast<const char*>(ap_wifi_test::last_ap_config.ap.password));
}

void test_radio_pads_short_stored_password_with_mac_suffix(void) {
    ap_wifi_storage_stub::seeded_password = "A1";
    // MAC default: A4 C1 38 12 34 56 -> hex of bytes 2..5 = "38123456".
    // pwd_len=2 -> suffix_len=5 -> last 5 hex chars "23456" are appended.
    ap_wifi_configure_radio();

    const char* password = reinterpret_cast<const char*>(ap_wifi_test::last_ap_config.ap.password);
    TEST_ASSERT_EQUAL_UINT8(8, static_cast<uint8_t>(std::strlen(password)));
    TEST_ASSERT_EQUAL_STRING("A1_23456", password);
}

void test_radio_uses_wpa2_wpa3_authmode(void) {
    ap_wifi_configure_radio();
    TEST_ASSERT_EQUAL_INT(WIFI_AUTH_WPA2_WPA3_PSK, ap_wifi_test::last_ap_config.ap.authmode);
    TEST_ASSERT_TRUE(ap_wifi_test::last_ap_config.ap.pmf_cfg.required == false);
}

// ── AP radio: SSID ──────────────────────────────────────────────────────────

void test_radio_builds_ssid_from_mac_suffix(void) {
    // MAC default: A4 C1 38 12 34 56 -> suffix from bytes 3..5 = "123456".
    ap_wifi_configure_radio();

    const char* ssid = reinterpret_cast<const char*>(ap_wifi_test::last_ap_config.ap.ssid);
    TEST_ASSERT_EQUAL_STRING("S2MB_123456", ssid);
    TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(std::strlen(ssid)), ap_wifi_test::last_ap_config.ap.ssid_len);
    TEST_ASSERT_EQUAL_UINT8(AP_WIFI_CHANNEL, ap_wifi_test::last_ap_config.ap.channel);
    TEST_ASSERT_EQUAL_UINT8(AP_WIFI_MAX_CLIENTS, ap_wifi_test::last_ap_config.ap.max_connection);
}

// ── AP netif ────────────────────────────────────────────────────────────────

void test_netif_applies_captive_portal_ip(void) {
    ap_wifi_configure_netif();

    TEST_ASSERT_EQUAL_INT(1, ap_wifi_test::set_ip_info_call_count);
    TEST_ASSERT_EQUAL_UINT32((4u << 24) | (3u << 16) | (2u << 8) | 1u, ap_wifi_test::last_ip_info.ip.addr);
    TEST_ASSERT_EQUAL_UINT32(0xFFFFFF00, ap_wifi_test::last_ip_info.netmask.addr);
    // The DNS server advertised to clients is the AP itself.
    TEST_ASSERT_EQUAL_INT(1, ap_wifi_test::set_dns_call_count);
    TEST_ASSERT_EQUAL_UINT32(ap_wifi_test::last_ip_info.ip.addr, ap_wifi_test::last_dns_info.ip.u_addr.addr);
    TEST_ASSERT_EQUAL_INT(1, ap_wifi_test::dhcps_stop_call_count);
    TEST_ASSERT_EQUAL_INT(1, ap_wifi_test::dhcps_start_call_count);
}

void test_netif_noop_when_ap_netif_missing(void) {
    ap_wifi_test::netif_available = false;
    ap_wifi_configure_netif();

    TEST_ASSERT_EQUAL_INT(0, ap_wifi_test::set_ip_info_call_count);
    TEST_ASSERT_EQUAL_INT(0, ap_wifi_test::dhcps_stop_call_count);
    TEST_ASSERT_EQUAL_INT(0, ap_wifi_test::dhcps_start_call_count);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_radio_generates_random_password_on_first_boot);
    RUN_TEST(test_radio_reuses_stored_password);
    RUN_TEST(test_radio_pads_short_stored_password_with_mac_suffix);
    RUN_TEST(test_radio_uses_wpa2_wpa3_authmode);
    RUN_TEST(test_radio_builds_ssid_from_mac_suffix);
    RUN_TEST(test_netif_applies_captive_portal_ip);
    RUN_TEST(test_netif_noop_when_ap_netif_missing);
    return UNITY_END();
}
