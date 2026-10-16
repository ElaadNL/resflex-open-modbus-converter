// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

// Native tests for lib/modbus_component/modbus_component.cpp.
//
// Covers the response-parsing guards:
//   - TCP/RTU read responses where the slave-reported byte count runs past
//     the received frame are rejected instead of overflowing the caller buffer.
//   - TCP write/FC16 responses shorter than the 12-byte minimum are rejected.
//   - RTU write/FC16 responses shorter than the 8-byte minimum are rejected.
//   - FC16 request construction and Modbus exception handling.
// The socket and UART layers are replaced by scripted stubs
// (test/native_stubs/transport_stubs.cpp).

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "driver/uart.h"
#include "lwip/sockets.h"
#include "modbus_component.h"
#include "appliance_config.h"

// ── helpers ─────────────────────────────────────────────────────────────────

static uint16_t test_crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t pos = 0; pos < length; pos++) {
        crc ^= static_cast<uint16_t>(data[pos]);
        for (int bit = 0; bit < 8; bit++) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// Modbus TCP read response: MBAP header (7) + unit id + fc + byte count + data.
static std::vector<uint8_t> make_tcp_read_response(uint8_t function_code, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> frame = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01};
    frame.push_back(function_code);
    frame.push_back(static_cast<uint8_t>(data.size()));
    frame.insert(frame.end(), data.begin(), data.end());
    frame[5] = static_cast<uint8_t>(3 + data.size());
    return frame;
}

// Modbus TCP write (FC05/FC06) and FC16 response: MBAP header (7) + unit id + 4 echoed bytes.
static std::vector<uint8_t> make_tcp_write_response(uint8_t function_code, uint16_t address, uint16_t value_or_count) {
    return {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01,
            function_code,
            static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address & 0xFF),
            static_cast<uint8_t>(value_or_count >> 8), static_cast<uint8_t>(value_or_count & 0xFF)};
}

// Modbus RTU read response: slave + fc + byte count + data + CRC (low first).
static std::vector<uint8_t> make_rtu_read_response(uint8_t function_code, const std::vector<uint8_t>& data, bool corrupt_crc = false) {
    std::vector<uint8_t> frame = {0x01, function_code, static_cast<uint8_t>(data.size())};
    frame.insert(frame.end(), data.begin(), data.end());
    uint16_t crc = test_crc16(frame.data(), frame.size());
    if (corrupt_crc) {
        crc ^= 0xFFFF;
    }
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

// Modbus RTU write/FC16 response: slave + fc + 4 echoed bytes + CRC.
static std::vector<uint8_t> make_rtu_write_response(uint8_t function_code, uint16_t address, uint16_t value_or_count) {
    std::vector<uint8_t> frame = {0x01, function_code,
                                  static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address & 0xFF),
                                  static_cast<uint8_t>(value_or_count >> 8), static_cast<uint8_t>(value_or_count & 0xFF)};
    uint16_t crc = test_crc16(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    return frame;
}

static bool init_tcp() {
    appliance_config_t config = {};
    config.interface_type = MODBUS_CONN_TYPE_TCP;
    config.ip_address = "127.0.0.1";
    config.port = 502;
    config.unit_id = 1;
    return modbus_component_init(config);
}

static bool init_rtu() {
    appliance_config_t config = {};
    config.interface_type = MODBUS_CONN_TYPE_RTU;
    config.unit_id = 1;
    config.baudrate = 115200;
    config.modbus_channel = 2;
    config.parity = UART_PARITY_DISABLE;
    config.data_bits = UART_DATA_8_BITS;
    config.stop_bits = UART_STOP_BITS_1;
    return modbus_component_init(config);
}

void setUp(void) {
    modbus_test_tcp::reset();
    modbus_test_uart::reset();
}

void tearDown(void) {}

// ── Modbus TCP: read responses ──────────────────────────────────────────────

void test_tcp_read_holding_16_success(void) {
    TEST_ASSERT_TRUE(init_tcp());
    modbus_test_tcp::responses.push_back(make_tcp_read_response(0x03, {0x12, 0x34}));

    uint16_t value = 0;
    TEST_ASSERT_TRUE(modbus_component_read_holding_register(0x0064, &value));
    TEST_ASSERT_EQUAL_HEX16(0x1234, value);

    // Verify the request frame that was sent.
    TEST_ASSERT_EQUAL(1, modbus_test_tcp::requests.size());
    const std::vector<uint8_t>& request = modbus_test_tcp::requests.front();
    TEST_ASSERT_EQUAL(12, request.size());
    TEST_ASSERT_EQUAL_HEX8(1, request[1]);        // transaction id started at 1
    TEST_ASSERT_EQUAL_HEX8(0x06, request[5]);     // MBAP length
    TEST_ASSERT_EQUAL_HEX8(1, request[6]);        // unit id
    TEST_ASSERT_EQUAL_HEX8(0x03, request[7]);     // function code
    TEST_ASSERT_EQUAL_HEX8(0x00, request[8]);     // address high
    TEST_ASSERT_EQUAL_HEX8(0x64, request[9]);     // address low
    TEST_ASSERT_EQUAL_HEX8(0x00, request[10]);    // quantity high
    TEST_ASSERT_EQUAL_HEX8(0x01, request[11]);    // quantity low
}

void test_tcp_read_input_32_success(void) {
    TEST_ASSERT_TRUE(init_tcp());
    modbus_test_tcp::responses.push_back(make_tcp_read_response(0x04, {0x11, 0x22, 0x33, 0x44}));

    uint32_t value = 0;
    TEST_ASSERT_TRUE(modbus_component_read_input_register(0x0000, &value));
    TEST_ASSERT_EQUAL_HEX32(0x11223344, value);
}

void test_tcp_read_string_success(void) {
    TEST_ASSERT_TRUE(init_tcp());
    // Registers hold "HI" and " \0" big-endian; the decoder stops at the null
    // and trims the trailing space, yielding "HI".
    modbus_test_tcp::responses.push_back(make_tcp_read_response(0x03, {0x48, 0x49, 0x20, 0x00}));

    char buffer[16] = {};
    TEST_ASSERT_TRUE(modbus_component_read_holding_register_string(0x0000, 2, buffer, sizeof(buffer), true));
    TEST_ASSERT_EQUAL_STRING("HI", buffer);
}

void test_tcp_read_byte_count_exceeds_frame_is_rejected(void) {
    TEST_ASSERT_TRUE(init_tcp());
    // The slave claims 100 bytes of payload but the frame only carries 11 bytes.
    std::vector<uint8_t> frame = {0x00, 0x01, 0x00, 0x00, 0x00, 0x66, 0x01, 0x03, 100};
    frame.resize(11);
    modbus_test_tcp::responses.push_back(frame);

    uint16_t value = 0;
    TEST_ASSERT_FALSE(modbus_component_read_holding_register(0x0000, &value));
}

void test_tcp_read_response_too_short_is_rejected(void) {
    TEST_ASSERT_TRUE(init_tcp());
    // 8 bytes is below the 9-byte minimum for any read response.
    std::vector<uint8_t> frame = {0x00, 0x01, 0x00, 0x00, 0x00, 0x05, 0x01, 0x03};
    modbus_test_tcp::responses.push_back(frame);

    uint16_t value = 0;
    TEST_ASSERT_FALSE(modbus_component_read_holding_register(0x0000, &value));
}

void test_tcp_read_exception_response_is_rejected(void) {
    TEST_ASSERT_TRUE(init_tcp());
    std::vector<uint8_t> frame = {0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x01, 0x83, 0x02};
    modbus_test_tcp::responses.push_back(frame);

    uint16_t value = 0;
    TEST_ASSERT_FALSE(modbus_component_read_holding_register(0x0000, &value));
}

void test_tcp_read_recv_error_is_rejected(void) {
    TEST_ASSERT_TRUE(init_tcp());
    modbus_test_tcp::recv_return_override = -1;
    modbus_test_tcp::recv_return_override_set = true;

    uint16_t value = 0;
    TEST_ASSERT_FALSE(modbus_component_read_holding_register(0x0000, &value));
}

void test_tcp_read_coil_true(void) {
    TEST_ASSERT_TRUE(init_tcp());
    modbus_test_tcp::responses.push_back(make_tcp_read_response(0x01, {0x01}));

    bool value = false;
    TEST_ASSERT_TRUE(modbus_component_read_coil(0x0000, &value));
    TEST_ASSERT_TRUE(value);
}

void test_tcp_read_coil_false(void) {
    TEST_ASSERT_TRUE(init_tcp());
    modbus_test_tcp::responses.push_back(make_tcp_read_response(0x01, {0x00}));

    bool value = true;
    TEST_ASSERT_TRUE(modbus_component_read_coil(0x0000, &value));
    TEST_ASSERT_FALSE(value);
}

// ── Modbus TCP: write responses ─────────────────────────────────────────────

void test_tcp_write_16_success(void) {
    TEST_ASSERT_TRUE(init_tcp());
    modbus_test_tcp::responses.push_back(make_tcp_write_response(0x06, 0x0010, 0x0102));

    TEST_ASSERT_TRUE(modbus_component_write_register_16(0x0010, 0x0102));
    TEST_ASSERT_EQUAL(1, modbus_test_tcp::requests.size());
    TEST_ASSERT_EQUAL_HEX8(0x06, modbus_test_tcp::requests.front()[7]);
}

void test_tcp_write_16_response_too_short_is_rejected(void) {
    TEST_ASSERT_TRUE(init_tcp());
    // 10 bytes passes the generic 9-byte minimum but is below the 12-byte
    // minimum for a write response (the new guard).
    std::vector<uint8_t> frame = make_tcp_write_response(0x06, 0x0010, 0x0102);
    frame.resize(10);
    modbus_test_tcp::responses.push_back(frame);

    TEST_ASSERT_FALSE(modbus_component_write_register_16(0x0010, 0x0102));
}

void test_tcp_write_32_fc16_success(void) {
    TEST_ASSERT_TRUE(init_tcp());
    modbus_test_tcp::responses.push_back(make_tcp_write_response(0x10, 0x0000, 0x0002));

    TEST_ASSERT_TRUE(modbus_component_write_register_32(0x0000, 0x11223344, true));

    // Verify the FC16 request frame: 13 fixed bytes + 4 data bytes.
    const std::vector<uint8_t>& request = modbus_test_tcp::requests.front();
    TEST_ASSERT_EQUAL(17, request.size());
    TEST_ASSERT_EQUAL_HEX8(0x10, request[7]);      // function code
    TEST_ASSERT_EQUAL_HEX8(0x00, request[8]);      // address high
    TEST_ASSERT_EQUAL_HEX8(0x00, request[9]);      // address low
    TEST_ASSERT_EQUAL_HEX8(0x00, request[10]);     // quantity high
    TEST_ASSERT_EQUAL_HEX8(0x02, request[11]);     // quantity low
    TEST_ASSERT_EQUAL_HEX8(0x04, request[12]);     // byte count
    TEST_ASSERT_EQUAL_HEX8(0x11, request[13]);     // value word 0 high (big endian)
    TEST_ASSERT_EQUAL_HEX8(0x22, request[14]);
    TEST_ASSERT_EQUAL_HEX8(0x33, request[15]);
    TEST_ASSERT_EQUAL_HEX8(0x44, request[16]);
}

void test_tcp_write_32_fc16_response_too_short_is_rejected(void) {
    TEST_ASSERT_TRUE(init_tcp());
    // 10 bytes passes the generic 9-byte minimum but is below the 12-byte
    // minimum for an FC16 response (the new guard).
    std::vector<uint8_t> frame = make_tcp_write_response(0x10, 0x0000, 0x0002);
    frame.resize(10);
    modbus_test_tcp::responses.push_back(frame);

    TEST_ASSERT_FALSE(modbus_component_write_register_32(0x0000, 0x11223344, true));
}

void test_tcp_write_coil_success(void) {
    TEST_ASSERT_TRUE(init_tcp());
    modbus_test_tcp::responses.push_back(make_tcp_write_response(0x05, 0x0020, 0xFF00));

    TEST_ASSERT_TRUE(modbus_component_write_coil(0x0020, true));
    TEST_ASSERT_EQUAL_HEX8(0x05, modbus_test_tcp::requests.front()[7]);
}

// ── Modbus TCP: connection state ────────────────────────────────────────────

void test_tcp_init_fails_when_connect_fails(void) {
    modbus_test_tcp::connect_result = -1;

    TEST_ASSERT_FALSE(init_tcp());
    // The failed socket must be closed.
    TEST_ASSERT_EQUAL_INT(1, modbus_test_tcp::close_call_count);

    // No socket, so a transaction must fail cleanly.
    uint16_t value = 0;
    TEST_ASSERT_FALSE(modbus_component_read_holding_register(0x0000, &value));
}

// ── Modbus RTU: read responses ──────────────────────────────────────────────

void test_rtu_read_16_success(void) {
    TEST_ASSERT_TRUE(init_rtu());
    modbus_test_uart::responses.push_back(make_rtu_read_response(0x03, {0xAB, 0xCD}));

    uint16_t value = 0;
    TEST_ASSERT_TRUE(modbus_component_read_holding_register(0x0100, &value));
    TEST_ASSERT_EQUAL_HEX16(0xABCD, value);

    // Verify the RTU request frame: slave + fc + address + count + CRC.
    TEST_ASSERT_EQUAL(1, modbus_test_uart::requests.size());
    const std::vector<uint8_t>& request = modbus_test_uart::requests.front();
    TEST_ASSERT_EQUAL(8, request.size());
    TEST_ASSERT_EQUAL_HEX8(1, request[0]);          // slave id
    TEST_ASSERT_EQUAL_HEX8(0x03, request[1]);       // function code
    TEST_ASSERT_EQUAL_HEX8(0x01, request[2]);       // address high
    TEST_ASSERT_EQUAL_HEX8(0x00, request[3]);       // address low
    TEST_ASSERT_EQUAL_HEX8(0x00, request[4]);       // quantity high
    TEST_ASSERT_EQUAL_HEX8(0x01, request[5]);       // quantity low
    const uint16_t expected_crc = test_crc16(request.data(), 6);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(expected_crc & 0xFF), request[6]);
    TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(expected_crc >> 8), request[7]);
}

void test_rtu_read_crc_mismatch_is_rejected(void) {
    TEST_ASSERT_TRUE(init_rtu());
    modbus_test_uart::responses.push_back(make_rtu_read_response(0x03, {0xAB, 0xCD}, true));

    uint16_t value = 0;
    TEST_ASSERT_FALSE(modbus_component_read_holding_register(0x0100, &value));
}

void test_rtu_read_timeout_is_rejected(void) {
    TEST_ASSERT_TRUE(init_rtu());
    // No response queued: uart_read_bytes returns 0.

    uint16_t value = 0;
    TEST_ASSERT_FALSE(modbus_component_read_holding_register(0x0100, &value));
}

void test_rtu_read_byte_count_exceeds_frame_is_rejected(void) {
    TEST_ASSERT_TRUE(init_rtu());
    // The slave claims 100 bytes of payload in a 5-byte frame with a valid CRC.
    std::vector<uint8_t> frame = {0x01, 0x03, 100, 0x00, 0x00};
    uint16_t crc = test_crc16(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    modbus_test_uart::responses.push_back(frame);

    uint16_t value = 0;
    TEST_ASSERT_FALSE(modbus_component_read_holding_register(0x0100, &value));
}

void test_rtu_read_exception_response_is_rejected(void) {
    TEST_ASSERT_TRUE(init_rtu());
    modbus_test_uart::responses.push_back(make_rtu_read_response(0x83, {0x02}));

    uint16_t value = 0;
    TEST_ASSERT_FALSE(modbus_component_read_holding_register(0x0100, &value));
}

void test_rtu_read_string_success(void) {
    TEST_ASSERT_TRUE(init_rtu());
    // Registers hold "HI" and " \0" big-endian; the decoder stops at the null
    // and trims the trailing space, yielding "HI".
    modbus_test_uart::responses.push_back(make_rtu_read_response(0x04, {0x48, 0x49, 0x20, 0x00}));

    char buffer[16] = {};
    TEST_ASSERT_TRUE(modbus_component_read_input_register_string(0x0000, 2, buffer, sizeof(buffer), true));
    TEST_ASSERT_EQUAL_STRING("HI", buffer);
}

// ── Modbus RTU: write responses ─────────────────────────────────────────────

void test_rtu_write_16_success(void) {
    TEST_ASSERT_TRUE(init_rtu());
    modbus_test_uart::responses.push_back(make_rtu_write_response(0x06, 0x0010, 0x0102));

    TEST_ASSERT_TRUE(modbus_component_write_register_16(0x0010, 0x0102));
}

void test_rtu_write_16_response_too_short_is_rejected(void) {
    TEST_ASSERT_TRUE(init_rtu());
    // A 6-byte frame with a valid CRC: passes the generic 5-byte minimum but
    // is below the 8-byte minimum for a write response (the new guard).
    std::vector<uint8_t> frame = {0x01, 0x06, 0x00, 0x10};
    uint16_t crc = test_crc16(frame.data(), frame.size());
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    modbus_test_uart::responses.push_back(frame);

    TEST_ASSERT_FALSE(modbus_component_write_register_16(0x0010, 0x0102));
}

void test_rtu_write_32_fc16_success(void) {
    TEST_ASSERT_TRUE(init_rtu());
    modbus_test_uart::responses.push_back(make_rtu_write_response(0x10, 0x0000, 0x0002));

    TEST_ASSERT_TRUE(modbus_component_write_register_32(0x0000, 0x11223344, true));

    const std::vector<uint8_t>& request = modbus_test_uart::requests.front();
    // slave + fc + address + count + byte count + 4 data bytes + 2 CRC.
    TEST_ASSERT_EQUAL(13, request.size());
    TEST_ASSERT_EQUAL_HEX8(0x10, request[1]);
    TEST_ASSERT_EQUAL_HEX8(0x04, request[6]);
    TEST_ASSERT_EQUAL_HEX8(0x11, request[7]);
    TEST_ASSERT_EQUAL_HEX8(0x22, request[8]);
    TEST_ASSERT_EQUAL_HEX8(0x33, request[9]);
    TEST_ASSERT_EQUAL_HEX8(0x44, request[10]);
}

void test_rtu_write_32_fc16_crc_mismatch_is_rejected(void) {
    TEST_ASSERT_TRUE(init_rtu());
    // A valid 8-byte FC16 response with a corrupted CRC must be rejected.
    std::vector<uint8_t> frame = {0x01, 0x10, 0x00, 0x00, 0x00, 0x02};
    uint16_t crc = test_crc16(frame.data(), frame.size());
    crc ^= 0xFFFF;
    frame.push_back(static_cast<uint8_t>(crc & 0xFF));
    frame.push_back(static_cast<uint8_t>(crc >> 8));
    modbus_test_uart::responses.push_back(frame);

    TEST_ASSERT_FALSE(modbus_component_write_register_32(0x0000, 0x11223344, true));
}

void test_rtu_write_timeout_is_rejected(void) {
    TEST_ASSERT_TRUE(init_rtu());
    // No response queued: uart_read_bytes returns 0.

    TEST_ASSERT_FALSE(modbus_component_write_register_16(0x0010, 0x0102));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_tcp_read_holding_16_success);
    RUN_TEST(test_tcp_read_input_32_success);
    RUN_TEST(test_tcp_read_string_success);
    RUN_TEST(test_tcp_read_byte_count_exceeds_frame_is_rejected);
    RUN_TEST(test_tcp_read_response_too_short_is_rejected);
    RUN_TEST(test_tcp_read_exception_response_is_rejected);
    RUN_TEST(test_tcp_read_recv_error_is_rejected);
    RUN_TEST(test_tcp_read_coil_true);
    RUN_TEST(test_tcp_read_coil_false);
    RUN_TEST(test_tcp_write_16_success);
    RUN_TEST(test_tcp_write_16_response_too_short_is_rejected);
    RUN_TEST(test_tcp_write_32_fc16_success);
    RUN_TEST(test_tcp_write_32_fc16_response_too_short_is_rejected);
    RUN_TEST(test_tcp_write_coil_success);
    RUN_TEST(test_tcp_init_fails_when_connect_fails);
    RUN_TEST(test_rtu_read_16_success);
    RUN_TEST(test_rtu_read_crc_mismatch_is_rejected);
    RUN_TEST(test_rtu_read_timeout_is_rejected);
    RUN_TEST(test_rtu_read_byte_count_exceeds_frame_is_rejected);
    RUN_TEST(test_rtu_read_exception_response_is_rejected);
    RUN_TEST(test_rtu_read_string_success);
    RUN_TEST(test_rtu_write_16_success);
    RUN_TEST(test_rtu_write_16_response_too_short_is_rejected);
    RUN_TEST(test_rtu_write_32_fc16_success);
    RUN_TEST(test_rtu_write_32_fc16_crc_mismatch_is_rejected);
    RUN_TEST(test_rtu_write_timeout_is_rejected);
    return UNITY_END();
}
