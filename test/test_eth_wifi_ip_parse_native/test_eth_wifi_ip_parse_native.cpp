// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <cstdint>

#include "eth_wifi_ip_parse.h"

void setUp(void) {}
void tearDown(void) {}

// ── happy path ──────────────────────────────────────────────────────────────

void test_parse_typical_ipv4_address(void) {
    uint8_t out[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(eth_wifi_parse_ipv4("192.168.1.10", out));
    TEST_ASSERT_EQUAL_UINT8(192, out[0]);
    TEST_ASSERT_EQUAL_UINT8(168, out[1]);
    TEST_ASSERT_EQUAL_UINT8(1,   out[2]);
    TEST_ASSERT_EQUAL_UINT8(10,  out[3]);
}

void test_parse_all_zeros(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_TRUE(eth_wifi_parse_ipv4("0.0.0.0", out));
    TEST_ASSERT_EQUAL_UINT8(0, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0, out[1]);
    TEST_ASSERT_EQUAL_UINT8(0, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0, out[3]);
}

void test_parse_all_max(void) {
    uint8_t out[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(eth_wifi_parse_ipv4("255.255.255.255", out));
    TEST_ASSERT_EQUAL_UINT8(255, out[0]);
    TEST_ASSERT_EQUAL_UINT8(255, out[1]);
    TEST_ASSERT_EQUAL_UINT8(255, out[2]);
    TEST_ASSERT_EQUAL_UINT8(255, out[3]);
}

void test_parse_typical_netmask(void) {
    uint8_t out[4] = {0, 0, 0, 0};
    TEST_ASSERT_TRUE(eth_wifi_parse_ipv4("255.255.255.0", out));
    TEST_ASSERT_EQUAL_UINT8(255, out[0]);
    TEST_ASSERT_EQUAL_UINT8(255, out[1]);
    TEST_ASSERT_EQUAL_UINT8(255, out[2]);
    TEST_ASSERT_EQUAL_UINT8(0,   out[3]);
}

// ── reject: octet out of range ──────────────────────────────────────────────

void test_reject_octet_256(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("256.0.0.1", out));
}

void test_reject_octet_999(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192.168.1.999", out));
}

// ── reject: wrong number of octets ──────────────────────────────────────────

void test_reject_only_three_octets(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192.168.1", out));
}

void test_reject_five_octets(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192.168.1.10.5", out));
}

void test_reject_empty_string(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("", out));
}

// ── reject: empty octets ────────────────────────────────────────────────────

void test_reject_double_dot(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192..1.10", out));
}

void test_reject_leading_dot(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4(".192.168.1.10", out));
}

void test_reject_trailing_dot(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192.168.1.10.", out));
}

// ── reject: leading zeros ───────────────────────────────────────────────────

void test_reject_leading_zero_in_octet(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192.168.01.10", out));
}

void test_reject_leading_zero_in_first_octet(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("010.0.0.0", out));
}

void test_accept_single_zero_octet(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_TRUE(eth_wifi_parse_ipv4("10.0.0.1", out));
    TEST_ASSERT_EQUAL_UINT8(10, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0,  out[1]);
    TEST_ASSERT_EQUAL_UINT8(0,  out[2]);
    TEST_ASSERT_EQUAL_UINT8(1,  out[3]);
}

// ── reject: non-digit characters ────────────────────────────────────────────

void test_reject_letters(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192.168.a.10", out));
}

void test_reject_internal_space(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192.168.1.10 ", out));
}

void test_reject_leading_space(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4(" 192.168.1.10", out));
}

void test_reject_hex_prefix(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("0xC0.0xA8.0.1", out));
}

void test_reject_negative_octet(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192.168.-1.10", out));
}

// ── reject: null inputs ─────────────────────────────────────────────────────

void test_reject_null_string(void) {
    uint8_t out[4] = {1, 1, 1, 1};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4(nullptr, out));
}

void test_reject_null_output_buffer(void) {
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192.168.1.10", nullptr));
}

// ── failure leaves output untouched ─────────────────────────────────────────

void test_failure_does_not_partially_write_output(void) {
    uint8_t out[4] = {7, 7, 7, 7};
    TEST_ASSERT_FALSE(eth_wifi_parse_ipv4("192.168.999.1", out));
    TEST_ASSERT_EQUAL_UINT8(7, out[0]);
    TEST_ASSERT_EQUAL_UINT8(7, out[1]);
    TEST_ASSERT_EQUAL_UINT8(7, out[2]);
    TEST_ASSERT_EQUAL_UINT8(7, out[3]);
}

int main(int /*argc*/, char** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_typical_ipv4_address);
    RUN_TEST(test_parse_all_zeros);
    RUN_TEST(test_parse_all_max);
    RUN_TEST(test_parse_typical_netmask);
    RUN_TEST(test_reject_octet_256);
    RUN_TEST(test_reject_octet_999);
    RUN_TEST(test_reject_only_three_octets);
    RUN_TEST(test_reject_five_octets);
    RUN_TEST(test_reject_empty_string);
    RUN_TEST(test_reject_double_dot);
    RUN_TEST(test_reject_leading_dot);
    RUN_TEST(test_reject_trailing_dot);
    RUN_TEST(test_reject_leading_zero_in_octet);
    RUN_TEST(test_reject_leading_zero_in_first_octet);
    RUN_TEST(test_accept_single_zero_octet);
    RUN_TEST(test_reject_letters);
    RUN_TEST(test_reject_internal_space);
    RUN_TEST(test_reject_leading_space);
    RUN_TEST(test_reject_hex_prefix);
    RUN_TEST(test_reject_negative_octet);
    RUN_TEST(test_reject_null_string);
    RUN_TEST(test_reject_null_output_buffer);
    RUN_TEST(test_failure_does_not_partially_write_output);
    return UNITY_END();
}
