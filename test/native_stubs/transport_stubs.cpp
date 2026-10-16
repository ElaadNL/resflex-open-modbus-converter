// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdlib>
#include <cstring>

#include "driver/uart.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"

// ---------------------------------------------------------------------------
// Modbus TCP socket transport
// ---------------------------------------------------------------------------

namespace modbus_test_tcp {

std::vector<std::vector<unsigned char>> requests;
std::vector<std::vector<unsigned char>> responses;
int recv_return_override = 0;
bool recv_return_override_set = false;
int connect_result = 0;
int close_call_count = 0;

void reset() {
    requests.clear();
    responses.clear();
    recv_return_override = 0;
    recv_return_override_set = false;
    connect_result = 0;
    close_call_count = 0;
}

}

extern "C" {

int socket(int, int, int) { return 3; }

int connect(int, const struct sockaddr*, socklen_t) { return modbus_test_tcp::connect_result; }

ssize_t send(int, const void* buf, size_t len, int) {
    if (buf == nullptr) {
        return -1;
    }
    const unsigned char* bytes = static_cast<const unsigned char*>(buf);
    modbus_test_tcp::requests.emplace_back(bytes, bytes + len);
    return static_cast<ssize_t>(len);
}

ssize_t recv(int, void* buf, size_t len, int) {
    if (modbus_test_tcp::recv_return_override_set) {
        return static_cast<ssize_t>(modbus_test_tcp::recv_return_override);
    }
    if (modbus_test_tcp::responses.empty()) {
        return -1;
    }
    const std::vector<unsigned char>& payload = modbus_test_tcp::responses.front();
    // Copy the payload out BEFORE erasing it: the reference to front() is
    // invalidated by erase(), so payload.data() would be dangling afterwards.
    const size_t copy_len = payload.size() < len ? payload.size() : len;
    std::memcpy(buf, payload.data(), copy_len);
    modbus_test_tcp::responses.erase(modbus_test_tcp::responses.begin());
    return static_cast<ssize_t>(copy_len);
}

int close(int) {
    modbus_test_tcp::close_call_count++;
    return 0;
}

int setsockopt(int, int, int, const void*, socklen_t) { return 0; }

} // extern "C"

// ---------------------------------------------------------------------------
// Modbus RTU UART transport
// ---------------------------------------------------------------------------

namespace modbus_test_uart {

std::vector<std::vector<unsigned char>> requests;
std::deque<std::vector<unsigned char>> responses;
int uart_write_bytes_override = -1;
bool uart_write_bytes_override_set = false;
int uart_read_bytes_override = -1;
bool uart_read_bytes_override_set = false;

void reset() {
    requests.clear();
    responses.clear();
    uart_write_bytes_override = -1;
    uart_write_bytes_override_set = false;
    uart_read_bytes_override = -1;
    uart_read_bytes_override_set = false;
}

}

esp_err_t uart_param_config(uart_port_t, const uart_config_t*) { return ESP_OK; }
esp_err_t uart_set_pin(uart_port_t, int, int, int, int) { return ESP_OK; }
esp_err_t uart_driver_install(uart_port_t, int, int, int, void*, int) { return ESP_OK; }
esp_err_t uart_set_mode(uart_port_t, int) { return ESP_OK; }
void uart_flush_input(uart_port_t) {}

int uart_write_bytes(uart_port_t, const char* data, unsigned int len) {
    if (modbus_test_uart::uart_write_bytes_override_set) {
        return modbus_test_uart::uart_write_bytes_override;
    }
    if (data != nullptr) {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
        modbus_test_uart::requests.emplace_back(bytes, bytes + len);
    }
    return static_cast<int>(len);
}

int uart_read_bytes(uart_port_t, void* data, unsigned int len, TickType_t) {
    if (modbus_test_uart::uart_read_bytes_override_set) {
        return modbus_test_uart::uart_read_bytes_override;
    }
    if (modbus_test_uart::responses.empty()) {
        return 0;
    }
    const std::vector<unsigned char>& payload = modbus_test_uart::responses.front();
    // Copy the payload out BEFORE popping it: the reference to front() is
    // invalidated by pop_front(), so payload.data() would be dangling afterwards.
    const size_t copy_len = payload.size() < len ? payload.size() : len;
    std::memcpy(data, payload.data(), copy_len);
    modbus_test_uart::responses.pop_front();
    return static_cast<int>(copy_len);
}

// ---------------------------------------------------------------------------
// AP WiFi transport
// ---------------------------------------------------------------------------

namespace ap_wifi_test {

wifi_config_t last_ap_config = {};
int set_config_call_count = 0;
uint8_t mac_bytes[6] = {0xA4, 0xC1, 0x38, 0x12, 0x34, 0x56};
bool netif_available = true;
esp_netif_ip_info_t last_ip_info = {};
int set_ip_info_call_count = 0;
esp_netif_dns_info_t last_dns_info = {};
int set_dns_call_count = 0;
int dhcps_stop_call_count = 0;
int dhcps_start_call_count = 0;
int dhcps_option_call_count = 0;

void reset() {
    last_ap_config = {};
    set_config_call_count = 0;
    mac_bytes[0] = 0xA4;
    mac_bytes[1] = 0xC1;
    mac_bytes[2] = 0x38;
    mac_bytes[3] = 0x12;
    mac_bytes[4] = 0x34;
    mac_bytes[5] = 0x56;
    netif_available = true;
    last_ip_info = {};
    set_ip_info_call_count = 0;
    last_dns_info = {};
    set_dns_call_count = 0;
    dhcps_stop_call_count = 0;
    dhcps_start_call_count = 0;
    dhcps_option_call_count = 0;
}

}

esp_err_t esp_wifi_set_config(wifi_interface_t if_index, const wifi_config_t* config) {
    if (if_index == WIFI_IF_AP && config != nullptr) {
        ap_wifi_test::last_ap_config = *config;
        ap_wifi_test::set_config_call_count++;
    }
    return ESP_OK;
}

static esp_netif_t dummy_netif_instance;

esp_netif_t* esp_netif_get_handle_from_ifkey(const char*) {
    return ap_wifi_test::netif_available ? &dummy_netif_instance : nullptr;
}

esp_err_t esp_netif_get_netif_impl_name(esp_netif_t*, char* name) {
    if (name != nullptr) {
        std::strcpy(name, "eth0");
    }
    return ESP_OK;
}

void esp_netif_set_ip_info(esp_netif_t*, const esp_netif_ip_info_t* info) {
    if (info != nullptr) {
        ap_wifi_test::last_ip_info = *info;
        ap_wifi_test::set_ip_info_call_count++;
    }
}

esp_err_t esp_netif_dhcps_stop(esp_netif_t*) {
    ap_wifi_test::dhcps_stop_call_count++;
    return ESP_OK;
}

esp_err_t esp_netif_dhcps_start(esp_netif_t*) {
    ap_wifi_test::dhcps_start_call_count++;
    return ESP_OK;
}

esp_err_t esp_netif_dhcps_option(esp_netif_t*, esp_netif_op_t, esp_netif_dhcps_key_t, void*, size_t) {
    ap_wifi_test::dhcps_option_call_count++;
    return ESP_OK;
}

esp_err_t esp_netif_set_dns_info(esp_netif_t*, esp_netif_dns_index_t, const esp_netif_dns_info_t* dns) {
    if (dns != nullptr) {
        ap_wifi_test::last_dns_info = *dns;
        ap_wifi_test::set_dns_call_count++;
    }
    return ESP_OK;
}
