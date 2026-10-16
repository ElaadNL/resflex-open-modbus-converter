// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "modbus_component.h"
#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "driver/uart.h"
#include <string.h>
#include <led_component.h>
#include <definitions.h>
#include <appliance_config.h>
#include <eth_wifi_component.h>

#define IP_ADDRESS_LENGTH 16

typedef struct {
    modbus_conn_type_t conn_type;
    uint16_t slave_id;

    // TCP specific fields
    char ip_address[IP_ADDRESS_LENGTH];
    uint16_t ip_port;
    esp_netif_t *netif; // <-- the netif to bind the TCP socket to via SO_BINDTODEVICE; picked by eth_wifi_get_active_netif() so it follows the user's prefer-WiFi flag

    // RTU specific fields
    uart_port_t uart_port;
    uint32_t baudrate;
    uint16_t modbus_channel;
    uart_parity_t parity;
    uart_word_length_t data_bits;
    uart_stop_bits_t stop_bits;

    // The fields below are fixed for the device Lilygo and will not be read from configuration!
    uint16_t rx_pin;
    uint16_t tx_pin;
    uint16_t rts_pin;
} modbus_config_t;

// TAG for the ESP_LOG functions
static const char *TAG = "MODBUS_COMPONENT";

// --- Internal State ---
static modbus_config_t target_device_config;
static int tcp_socket_fd = -1;
static uint16_t transaction_id = 0;
static bool is_connected = false;
static bool rtu_uart_driver_installed = false;

// --- State Management Helpers (from your original code) ---
/**
 * @brief Mark the connection as down and light the waiting LED.
 * @param reason Short description of the failure, used in the log message only.
 */
static void modbus_component_trigger_error_state(const char *reason) {
    if (is_connected) {
        is_connected = false;
        ESP_LOGE(TAG, "Error triggered: %s", reason);
        set_LED_state(LED_STATE_WAITING_FOR_DEVICE);
    }
}

/**
 * @brief Mark the connection as healthy again after a successful transaction.
 */
static void modbus_component_recover_normal_state() {
    if (!is_connected) {
        is_connected = true;
        ESP_LOGI(TAG, "Reconnected successfully!");
    }
}

// --- CRC16 Calculation for Modbus RTU ---
/**
 * @brief Compute the Modbus RTU CRC-16 (polynomial 0xA001, initial value 0xFFFF) over a buffer.
 * @param buffer Data to checksum.
 * @param length Number of bytes to process.
 * @return The 16-bit CRC, with the low byte transmitted first on the wire.
 */
static uint16_t calculate_crc16(uint8_t *buffer, uint16_t length) {
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < length; pos++) {
        crc ^= (uint16_t)buffer[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            }
            else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

// --- Core Transaction Handler ---
/**
 * @brief Execute a single Modbus request and parse the response.
 *
 * Handles the raw bytes for both TCP (MBAP) and RTU (CRC). It prevents code
 * duplication in all the public read/write functions.
 *
 * @param function_code Modbus function code (0x01, 0x02, 0x03, 0x04, 0x05, 0x06).
 * @param address Start address of the registers or coils.
 * @param value_or_count Quantity of registers/coils for read functions, value for write functions.
 * @param rx_payload Caller buffer receiving the decoded payload.
 * @param rx_payload_capacity Size in bytes of rx_payload.
 * @param rx_payload_len Receives the number of payload bytes copied.
 * @return true if a valid, non-exception response was decoded.
 * @return false on network error, timeout, CRC failure, Modbus exception, or invalid response.
 */
static bool modbus_execute_transaction(uint8_t function_code, uint16_t address, uint16_t value_or_count, uint8_t *rx_payload, size_t rx_payload_capacity, uint8_t *rx_payload_len) {

    //  _____ ____ ____                     _
    // |_   _/ ___|  _ \   _ __   __ _ _ __| |_
    //   | || |   | |_) | | '_ \ / _` | '__| __|
    //   | || |___|  __/  | |_) | (_| | |  | |_
    //   |_| \____|_|     | .__/ \__,_|_|   \__|
    //                    |_|
    if (target_device_config.conn_type == MODBUS_CONN_TYPE_TCP) {
        if (tcp_socket_fd < 0) {
            ESP_LOGE(TAG, "TCP Socket is not connected!");
            return false;
        }

        transaction_id++;

        // Build the Modbus TCP Request (12 bytes for standard read/write requests)
        uint8_t request[12];

        // 1. MBAP Header (7 bytes)
        request[0] = transaction_id >> 8;           // Transaction ID (High Byte)
        request[1] = transaction_id & 0xFF;         // Transaction ID (Low Byte)
        request[2] = 0x00;                          // Protocol ID (0 = Modbus TCP)
        request[3] = 0x00;                          // Protocol ID
        request[4] = 0x00;                          // Length (High Byte)
        request[5] = 0x06;                          // Length (Low Byte): 6 bytes follow
        request[6] = target_device_config.slave_id; // Unit ID

        // 2. PDU (Protocol Data Unit) (5 bytes)
        request[7] = function_code;          // Function Code (e.g., 0x03, 0x04, 0x06)
        request[8] = address >> 8;           // Start Address (High Byte)
        request[9] = address & 0xFF;         // Start Address (Low Byte)
        request[10] = value_or_count >> 8;   // Quantity or Value (High Byte)
        request[11] = value_or_count & 0xFF; // Quantity or Value (Low Byte)

        // Send the request
        int sent_bytes = send(tcp_socket_fd, request, sizeof(request), 0);
        if (sent_bytes < 0) {
            ESP_LOGE(TAG, "Failed to send data: errno %d", errno);
            return false;
        }

        // Receive the response
        uint8_t rx_buffer[256];
        int received_bytes = static_cast<int>(recv(tcp_socket_fd, rx_buffer, sizeof(rx_buffer), 0));

        if (received_bytes < 0) {
            ESP_LOGE(TAG, "Timeout or error receiving data: errno %d", errno);
            return false;
        }
        else if (received_bytes < 9) {
            ESP_LOGE(TAG, "Response too short (%d bytes)", received_bytes);
            return false;
        }

        // Check for Modbus Exception (Function Code + 0x80)
        if (rx_buffer[7] == (function_code | 0x80)) {
            ESP_LOGE(TAG, "Modbus Exception received! Exception Code: 0x%02X", rx_buffer[8]);
            return false;
        }

        // Extract payload based on function code
        if (function_code == 0x01 || function_code == 0x02 || function_code == 0x03 || function_code == 0x04) {
            // Read functions return a byte count at index 8, followed by data
            uint8_t payload_len = rx_buffer[8];
            // The byte count comes from the slave; reject it if it runs past the received frame before copying
            if (static_cast<size_t>(9 + payload_len) > static_cast<size_t>(received_bytes)) {
                ESP_LOGE(TAG, "TCP Response byte count exceeds buffer (%d bytes)", payload_len);
                return false;
            }
            // Also reject if the caller's payload buffer cannot hold the data
            if (payload_len > rx_payload_capacity) {
                ESP_LOGE(TAG, "TCP Payload buffer too small (%lu < %u bytes)", (unsigned long)rx_payload_capacity, payload_len);
                return false;
            }
            *rx_payload_len = payload_len;
            memcpy(rx_payload, &rx_buffer[9], payload_len);
        }
        else {
            // Write functions (0x05, 0x06) echo the address and value back (4 bytes)
            // MBAP header (7) + Unit ID (1) + 4 echoed bytes = 12 bytes minimum
            if (received_bytes < 12) {
                ESP_LOGE(TAG, "TCP Response too short for write response (%d bytes)", received_bytes);
                return false;
            }
            if (rx_payload_capacity < 4) {
                ESP_LOGE(TAG, "TCP Payload buffer too small for write echo");
                return false;
            }
            *rx_payload_len = 4;
            memcpy(rx_payload, &rx_buffer[8], 4);
        }

        return true;
    }

    //  ____ _____ _   _                    _
    // |  _ \_   _| | | |  _ __   __ _ _ __| |_
    // | |_) || | | | | | | '_ \ / _` | '__| __|
    // |  _ < | | | |_| | | |_) | (_| | |  | |_
    // |_| \_\|_|  \___/  | .__/ \__,_|_|   \__|
    //                    |_|
    else if (target_device_config.conn_type == MODBUS_CONN_TYPE_RTU) {
        // --- RTU IMPLEMENTATION ---

        // Build the RTU Request (8 bytes for standard read/write requests)
        uint8_t request[8];

        request[0] = target_device_config.slave_id; // Slave Address
        request[1] = function_code;                 // Function Code
        request[2] = address >> 8;                  // Start Address (High)
        request[3] = address & 0xFF;                // Start Address (Low)
        request[4] = value_or_count >> 8;           // Quantity/Value (High)
        request[5] = value_or_count & 0xFF;         // Quantity/Value (Low)

        // Calculate and append CRC16
        uint16_t crc = calculate_crc16(request, 6);
        request[6] = crc & 0xFF; // CRC Low Byte
        request[7] = crc >> 8;   // CRC High Byte

        // Flush any lingering noise from the UART buffer before asking a question
        uart_flush_input(target_device_config.uart_port);

        // Send the request over RS485
        int sent_bytes = uart_write_bytes(target_device_config.uart_port, (const char *)request, 8);
        if (sent_bytes != 8) {
            ESP_LOGE(TAG, "Failed to send RTU data over UART");
            return false;
        }

        // Wait for and read the response
        uint8_t rx_buffer[256];
        // 1000ms is a safe timeout for most Modbus slaves to respond
        int received_bytes = uart_read_bytes(target_device_config.uart_port, rx_buffer, sizeof(rx_buffer), pdMS_TO_TICKS(1000));

        if (received_bytes <= 0) {
            ESP_LOGE(TAG, "RTU Read Timeout (No response from Slave %d)", target_device_config.slave_id);
            return false;
        }
        else if (received_bytes < 5) {
            // Min valid response is 5 bytes: SlaveID(1) + Func(1) + ErrCode(1) + CRC(2)
            ESP_LOGE(TAG, "RTU Response too short (%d bytes)", received_bytes);
            return false;
        }

        // Validate the CRC of the received message
        uint16_t received_crc = (rx_buffer[received_bytes - 1] << 8) | rx_buffer[received_bytes - 2];
        uint16_t calculated_crc = calculate_crc16(rx_buffer, received_bytes - 2);

        if (received_crc != calculated_crc) {
            ESP_LOGE(TAG, "RTU CRC Check Failed! Received: 0x%04X, Calculated: 0x%04X", received_crc, calculated_crc);
            return false;
        }

        // Check for Modbus Exception
        if (rx_buffer[1] == (function_code | 0x80)) {
            ESP_LOGE(TAG, "Modbus Exception received! Code: 0x%02X", rx_buffer[2]);
            return false;
        }

        // Extract payload
        if (function_code == 0x01 || function_code == 0x02 || function_code == 0x03 || function_code == 0x04) {
            // Byte 2 is the length of the data payload
            uint8_t payload_len = rx_buffer[2];
            // Frame layout: SlaveID(1) + Func(1) + ByteCount(1) + Data(payload_len) + CRC(2)
            // if the byte count (+overhead) exceeds the actual received bytes count, then abort
            if (static_cast<size_t>(3 + payload_len + 2) > static_cast<size_t>(received_bytes)) {
                ESP_LOGE(TAG, "RTU Response byte count exceeds frame (%d bytes)", payload_len);
                return false;
            }
            // Also reject if the caller's payload buffer cannot hold the data
            if (payload_len > rx_payload_capacity) {
                ESP_LOGE(TAG, "RTU Payload buffer too small (%lu < %u bytes)", (unsigned long)rx_payload_capacity, payload_len);
                return false;
            }
            *rx_payload_len = payload_len;
            memcpy(rx_payload, &rx_buffer[3], payload_len);
        }
        else {
            // Write operations just echo the 4-byte address/value combo (plus 2 CRC bytes)
            // SlaveID(1) + Func(1) + 4 echoed bytes + CRC(2) = 8 bytes minimum
            if (received_bytes < 8) {
                ESP_LOGE(TAG, "RTU Response too short for write response (%d bytes)", received_bytes);
                return false;
            }
            if (rx_payload_capacity < 4) {
                ESP_LOGE(TAG, "RTU Payload buffer too small for write echo");
                return false;
            }
            *rx_payload_len = 4;
            memcpy(rx_payload, &rx_buffer[2], 4);
        }

        return true;
    }
    return false;
}

// --- Core Transaction Handler for FC16 (Write Multiple Registers) ---
/**
 * @brief Write multiple holding registers using Function Code 0x10.
 *
 * @param address Start address of the registers to write.
 * @param num_registers Number of 16-bit registers to write.
 * @param values Array of register values to write.
 * @param rx_payload Caller buffer receiving the echoed address and quantity.
 * @param rx_payload_capacity Size in bytes of rx_payload.
 * @param rx_payload_len Receives the number of payload bytes copied.
 * @return true if the device acknowledged the write.
 * @return false on network error, timeout, CRC failure, Modbus exception, or invalid argument.
 */
static bool modbus_execute_transaction_write_multiple(uint16_t address, uint16_t num_registers, uint16_t *values, uint8_t *rx_payload, size_t rx_payload_capacity, uint8_t *rx_payload_len) {
    uint8_t function_code = 0x10;           // FC16
    uint8_t byte_count = num_registers * 2; // Every register is 2 bytes

    // Guard against oversized register counts before any buffer is touched:
    // byte_count must not wrap around (uint8_t) and must fit the 256-byte TCP
    // request buffer (13 fixed bytes + byte_count <= 256 -> byte_count <= 243)
    if (byte_count == 0 || static_cast<size_t>(num_registers) * 2 != byte_count || byte_count > 243) {
        ESP_LOGE(TAG, "FC16 register count out of range: %u", num_registers);
        return false;
    }

    // The FC16 response always echoes 4 bytes back; the caller's buffer must hold them
    if (rx_payload_capacity < 4) {
        ESP_LOGE(TAG, "FC16 Payload buffer too small (%lu bytes)", (unsigned long)rx_payload_capacity);
        return false;
    }

    //  _____ ____ ____                     _
    // |_   _/ ___|  _ \   _ __   __ _ _ __| |_
    //   | || |   | |_) | | '_ \ / _` | '__| __|
    //   | || |___|  __/  | |_) | (_| | |  | |_
    //   |_| \____|_|     | .__/ \__,_|_|   \__|
    //                    |_|
    if (target_device_config.conn_type == MODBUS_CONN_TYPE_TCP) {
        if (tcp_socket_fd < 0) {
            ESP_LOGE(TAG, "TCP Socket is not connected!");
            return false;
        }

        transaction_id++;

        // Calculate PDU length (Unit ID + FC + Addr + Qty + ByteCount + Data)
        uint16_t length_field = 7 + byte_count;

        uint8_t request[256];
        // 1. MBAP Header
        request[0] = transaction_id >> 8;
        request[1] = transaction_id & 0xFF;
        request[2] = 0x00;
        request[3] = 0x00;
        request[4] = length_field >> 8;
        request[5] = length_field & 0xFF;
        request[6] = target_device_config.slave_id;

        // 2. PDU
        request[7] = function_code;
        request[8] = address >> 8;
        request[9] = address & 0xFF;
        request[10] = num_registers >> 8;
        request[11] = num_registers & 0xFF;
        request[12] = byte_count;

        // 3. Add payload (Big-Endian)
        for (int i = 0; i < num_registers; i++) {
            request[13 + (i * 2)] = values[i] >> 8;   // High Byte
            request[14 + (i * 2)] = values[i] & 0xFF; // Low Byte
        }

        uint16_t total_req_len = 13 + byte_count;

        // Send
        int sent_bytes = send(tcp_socket_fd, request, total_req_len, 0);
        if (sent_bytes < 0) {
            ESP_LOGE(TAG, "Failed to send FC16 data: errno %d", errno);
            return false;
        }

        // Receive
        uint8_t rx_buffer[256];
        int received_bytes = recv(tcp_socket_fd, rx_buffer, sizeof(rx_buffer), 0);

        if (received_bytes < 0) {
            ESP_LOGE(TAG, "Timeout or error receiving FC16 data: errno %d", errno);
            return false;
        }
        else if (received_bytes < 9) {
            ESP_LOGE(TAG, "FC16 Response too short");
            return false;
        }

        // Exception check
        if (rx_buffer[7] == (function_code | 0x80)) {
            ESP_LOGE(TAG, "Modbus Exception! Code: 0x%02X", rx_buffer[8]);
            return false;
        }

        // FC16 returns address and quantity (4 bytes) at offset 8
        // MBAP header (7) + Unit ID (1) + 4 echoed bytes = 12 bytes minimum
        if (received_bytes < 12) {
            ESP_LOGE(TAG, "FC16 TCP Response too short (%d bytes)", received_bytes);
            return false;
        }
        *rx_payload_len = 4;
        memcpy(rx_payload, &rx_buffer[8], 4);
        return true;
    }

    //  ____ _____ _   _                    _
    // |  _ \_   _| | | |  _ __   __ _ _ __| |_
    // | |_) || | | | | | | '_ \ / _` | '__| __|
    // |  _ < | | | |_| | | |_) | (_| | |  | |_
    // |_| \_\|_|  \___/  | .__/ \__,_|_|   \__|
    //                    |_|
    else if (target_device_config.conn_type == MODBUS_CONN_TYPE_RTU) {
        uint8_t request[256];

        request[0] = target_device_config.slave_id;
        request[1] = function_code;
        request[2] = address >> 8;
        request[3] = address & 0xFF;
        request[4] = num_registers >> 8;
        request[5] = num_registers & 0xFF;
        request[6] = byte_count;

        // Append payload
        for (int i = 0; i < num_registers; i++) {
            request[7 + (i * 2)] = values[i] >> 8;
            request[8 + (i * 2)] = values[i] & 0xFF;
        }

        uint16_t data_len = 7 + byte_count;

        // Calculate and append CRC
        uint16_t crc = calculate_crc16(request, data_len);
        request[data_len] = crc & 0xFF;   // Modbus RTU CRC is sent low byte first!
        request[data_len + 1] = crc >> 8; // High Byte

        uint16_t total_req_len = data_len + 2;

        uart_flush_input(target_device_config.uart_port);

        int sent_bytes = uart_write_bytes(target_device_config.uart_port, (const char *)request, total_req_len);
        if (sent_bytes != total_req_len) {
            ESP_LOGE(TAG, "Failed to send FC16 RTU data");
            return false;
        }

        uint8_t rx_buffer[256];
        int received_bytes = uart_read_bytes(target_device_config.uart_port, rx_buffer, sizeof(rx_buffer), pdMS_TO_TICKS(1000));

        if (received_bytes < 8) {
            ESP_LOGE(TAG, "FC16 RTU Read Timeout or short response (%d bytes)", received_bytes);
            return false;
        }

        uint16_t received_crc = (rx_buffer[received_bytes - 1] << 8) | rx_buffer[received_bytes - 2];
        uint16_t calculated_crc = calculate_crc16(rx_buffer, received_bytes - 2);

        if (received_crc != calculated_crc) {
            ESP_LOGE(TAG, "FC16 RTU CRC Check Failed!");
            return false;
        }

        if (rx_buffer[1] == (function_code | 0x80)) {
            ESP_LOGE(TAG, "FC16 Modbus Exception! Code: 0x%02X", rx_buffer[2]);
            return false;
        }

        *rx_payload_len = 4;
        memcpy(rx_payload, &rx_buffer[2], 4);
        return true;
    }
    return false;
}

//
// Here start the actual public functions
//
bool modbus_component_init(const appliance_config_t &config) {
    // Pick the same netif the rest of the system routes outbound traffic
    // through: Ethernet by default, WiFi STA when the captive-portal
    // "Use WiFi instead of Ethernet" flag is set.
    esp_netif_t *active_netif = eth_wifi_get_active_netif();

    if (config.interface_type == MODBUS_CONN_TYPE_TCP) {
        target_device_config.conn_type = MODBUS_CONN_TYPE_TCP;
        target_device_config.netif = active_netif;
        snprintf(target_device_config.ip_address, IP_ADDRESS_LENGTH, "%s", config.ip_address.c_str());
        target_device_config.ip_port = config.port;
        target_device_config.slave_id = config.unit_id;
    }
    else {
        target_device_config.conn_type = MODBUS_CONN_TYPE_RTU;
        target_device_config.slave_id = config.unit_id;
        target_device_config.baudrate = config.baudrate;
        target_device_config.modbus_channel = config.modbus_channel;
        target_device_config.uart_port = static_cast<uart_port_t>(target_device_config.modbus_channel);
        if (target_device_config.uart_port >= UART_NUM_MAX) {
            ESP_LOGW(TAG, "Configured UART %u is invalid; falling back to UART2", target_device_config.modbus_channel);
            target_device_config.uart_port = UART_NUM_2;
            target_device_config.modbus_channel = UART_NUM_2;
        }

        target_device_config.parity = static_cast<uart_parity_t>(config.parity);
        target_device_config.data_bits = static_cast<uart_word_length_t>(config.data_bits);
        target_device_config.stop_bits = static_cast<uart_stop_bits_t>(config.stop_bits);

        target_device_config.rx_pin = RS485_RX;
        target_device_config.tx_pin = RS485_TX;
        target_device_config.rts_pin = RS485_RTS;
    }

    //  _____ ____ ____                     _
    // |_   _/ ___|  _ \   _ __   __ _ _ __| |_
    //   | || |   | |_) | | '_ \ / _` | '__| __|
    //   | || |___|  __/  | |_) | (_| | |  | |_
    //   |_| \____|_|     | .__/ \__,_|_|   \__|
    //                    |_|
    if (target_device_config.conn_type == MODBUS_CONN_TYPE_TCP) {
        ESP_LOGI(TAG, "Initializing Modbus TCP connection to %s:%d", target_device_config.ip_address, target_device_config.ip_port);

        struct sockaddr_in dest_addr;
        dest_addr.sin_addr.s_addr = inet_addr(target_device_config.ip_address);
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(target_device_config.ip_port);

        tcp_socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (tcp_socket_fd < 0) {
            ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
            return false;
        }

        // This code forces the socket to use the specific interface: ETHERNET
        if (target_device_config.netif != nullptr) {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));

            // Create a buffer for ESP-IDF to write the name into
            char if_name[32] = {0};
            esp_err_t net_err = esp_netif_get_netif_impl_name(target_device_config.netif, if_name);

            if (net_err == ESP_OK) {
                snprintf(ifr.ifr_name, IFNAMSIZ, "%s", if_name);
                int bind_err = setsockopt(tcp_socket_fd, SOL_SOCKET, SO_BINDTODEVICE, (void *)&ifr, sizeof(ifr));

                if (bind_err < 0) {
                    ESP_LOGW(TAG, "Warning: Failed to bind to interface %s (errno %d)", if_name, errno);
                }
                else {
                    ESP_LOGI(TAG, "TCP Socket strictly bound to network interface: %s", if_name);
                }
            }
            else {
                ESP_LOGW(TAG, "Warning: Could not get interface name for binding (err: %s)", esp_err_to_name(net_err));
            }
        }
        else {
            ESP_LOGW(TAG, "Notice: No netif provided, allowing OS to route automatically");
        }

        // Set 3-second receive timeout to prevent infinite blocking
        struct timeval timeout;
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;
        int timeout_err = setsockopt(tcp_socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (timeout_err < 0) {
            ESP_LOGE(TAG, "Failed to set socket receive timeout: errno %d", errno);
            close(tcp_socket_fd);
            tcp_socket_fd = -1;
            return false;
        }

        int err = connect(tcp_socket_fd, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
        if (err != 0) {
            ESP_LOGE(TAG, "Socket connect failed: errno %d", errno);
            close(tcp_socket_fd);
            tcp_socket_fd = -1;
            return false;
        }

        ESP_LOGI(TAG, "TCP Socket successfully connected!");
        is_connected = true;
        return true;

        //  ____ _____ _   _                    _
        // |  _ \_   _| | | |  _ __   __ _ _ __| |_
        // | |_) || | | | | | | '_ \ / _` | '__| __|
        // |  _ < | | | |_| | | |_) | (_| | |  | |_
        // |_| \_\|_|  \___/  | .__/ \__,_|_|   \__|
        //                    |_|
    }
    else if (target_device_config.conn_type == MODBUS_CONN_TYPE_RTU) {
        ESP_LOGI(TAG, "Initializing Modbus RTU (RS485) on UART %d at %lu baud",
                 target_device_config.uart_port, (unsigned long)target_device_config.baudrate);

        uart_config_t uart_config = {
            .baud_rate = static_cast<int>(target_device_config.baudrate),
            .data_bits = target_device_config.data_bits,
            .parity = target_device_config.parity,
            .stop_bits = target_device_config.stop_bits,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 122,
            .source_clk = UART_SCLK_DEFAULT,
        };

        ESP_ERROR_CHECK(uart_param_config(target_device_config.uart_port, &uart_config));

        // Set the pins (TX, RX, RTS). CTS is not used in half-duplex RS485.
        ESP_ERROR_CHECK(uart_set_pin(target_device_config.uart_port,
                                     target_device_config.tx_pin,
                                     target_device_config.rx_pin,
                                     target_device_config.rts_pin,
                                     UART_PIN_NO_CHANGE));

        // Install the UART driver with a 256-byte RX buffer and no TX buffer/queues
        if (!rtu_uart_driver_installed) {
            esp_err_t install_err = uart_driver_install(target_device_config.uart_port, 256, 0, 0, nullptr, 0);
            if (install_err == ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "UART driver already installed for UART %d, reusing it", target_device_config.uart_port);
            }
            else if (install_err != ESP_OK) {
                ESP_LOGE(TAG, "UART driver install failed for UART %d: %s",
                         target_device_config.uart_port, esp_err_to_name(install_err));
                return false;
            }
            rtu_uart_driver_installed = true;
        }

        // Tell the ESP32 to manage the RTS pin automatically for RS485 transceivers!
        ESP_ERROR_CHECK(uart_set_mode(target_device_config.uart_port, UART_MODE_RS485_HALF_DUPLEX));

        is_connected = true;
        ESP_LOGI(TAG, "UART successfully initialized for RS485!");
        return true;
    }

    return false;
}

//
// --- Generic Internal Read Functions ---
//     user by input | holding reads
//
/**
 * @brief Read a single 16-bit register using the given function code.
 * @param function_code 0x03 (holding) or 0x04 (input).
 * @param address Start address of the register.
 * @param value Receives the decoded 16-bit value.
 * @param error_msg Description logged and stored on failure.
 * @return true if the register was read and decoded.
 */
static bool modbus_generic_read_16(uint8_t function_code, uint16_t address, uint16_t *value, const char *error_msg) {
    uint8_t rx_data[256];
    uint8_t rx_len = 0;

    // Request exactly 1 register (which is 2 bytes)
    if (modbus_execute_transaction(function_code, address, 1, rx_data, sizeof(rx_data), &rx_len)) {
        if (rx_len == 2) {
            *value = (rx_data[0] << 8) | rx_data[1];
            modbus_component_recover_normal_state();
            return true;
        }
    }
    modbus_component_trigger_error_state(error_msg);
    ESP_LOGE(TAG, "ERROR READING %i with function %i", address, function_code);
    return false;
}

/**
 * @brief Read two consecutive 16-bit registers and combine them into a 32-bit value.
 * @param function_code 0x03 (holding) or 0x04 (input).
 * @param address Start address of the first register.
 * @param value Receives the decoded 32-bit value.
 * @param error_msg Description logged and stored on failure.
 * @return true if both registers were read and decoded.
 */
static bool modbus_generic_read_32(uint8_t function_code, uint16_t address, uint32_t *value, const char *error_msg) {
    uint8_t rx_data[256];
    uint8_t rx_len = 0;

    // Request exactly 2 registers (which is 4 bytes)
    if (modbus_execute_transaction(function_code, address, 2, rx_data, sizeof(rx_data), &rx_len)) {
        if (rx_len == 4) {
            *value = (rx_data[0] << 24) | (rx_data[1] << 16) | (rx_data[2] << 8) | rx_data[3];
            modbus_component_recover_normal_state();
            return true;
        }
    }
    modbus_component_trigger_error_state(error_msg);
    return false;
}

/**
 * @brief Decode a register payload as an ASCII string.
 *
 * Each 16-bit register holds two characters. Stops at the first null character
 * and trims trailing spaces.
 *
 * @param rx_data Raw register payload.
 * @param rx_len Length of rx_data in bytes.
 * @param value Destination buffer for the null-terminated string.
 * @param max_len Size in bytes of value.
 * @param big_endian true for high-byte/low-byte character order, false for swapped order.
 */
static void modbus_payload_to_string(const uint8_t *rx_data, uint8_t rx_len, char *value, size_t max_len, bool big_endian) {
    size_t output_index = 0;

    for (uint8_t input_index = 0; input_index + 1 < rx_len && output_index + 1 < max_len; input_index += 2) {
        uint8_t first_char = big_endian ? rx_data[input_index] : rx_data[input_index + 1];
        uint8_t second_char = big_endian ? rx_data[input_index + 1] : rx_data[input_index];

        if (first_char == '\0') {
            break;
        }
        value[output_index++] = static_cast<char>(first_char);

        if (output_index + 1 >= max_len || second_char == '\0') {
            break;
        }
        value[output_index++] = static_cast<char>(second_char);
    }

    while (output_index > 0 && value[output_index - 1] == ' ') {
        output_index--;
    }
    value[output_index] = '\0';
}

/**
 * @brief Read a block of registers and decode it as an ASCII string.
 * @param function_code 0x03 (holding) or 0x04 (input).
 * @param address Start address of the first register.
 * @param register_count Number of 16-bit registers to read.
 * @param value Destination buffer for the null-terminated string.
 * @param max_len Size in bytes of value.
 * @param big_endian true for high-byte/low-byte character order, false for swapped order.
 * @param error_msg Description logged and stored on failure.
 * @return true if the registers were read and decoded.
 */
static bool modbus_generic_read_string(uint8_t function_code, uint16_t address, uint16_t register_count, char *value, size_t max_len, bool big_endian, const char *error_msg) {
    if (value == nullptr || max_len == 0 || register_count == 0) {
        return false;
    }

    value[0] = '\0';

    uint8_t rx_data[256];
    uint8_t rx_len = 0;

    if (modbus_execute_transaction(function_code, address, register_count, rx_data, sizeof(rx_data), &rx_len)) {
        modbus_payload_to_string(rx_data, rx_len, value, max_len, big_endian);
        modbus_component_recover_normal_state();
        return true;
    }

    modbus_component_trigger_error_state(error_msg);
    return false;
}

// ============================================================================
// HOLDING REGISTERS (0x03)
// ============================================================================

bool modbus_component_read_holding_register(uint16_t address, uint16_t *value) {
    return modbus_generic_read_16(0x03, address, value, "Failed to read 16-bit Holding Register");
}

bool modbus_component_read_holding_register(uint16_t address, uint32_t *value) {
    return modbus_generic_read_32(0x03, address, value, "Failed to read 32-bit Holding Register");
}

// ============================================================================
// INPUT REGISTERS (0x04)
// ============================================================================

bool modbus_component_read_input_register(uint16_t address, uint16_t *value) {
    return modbus_generic_read_16(0x04, address, value, "Failed to read 16-bit Input Register");
}

bool modbus_component_read_input_register(uint16_t address, uint32_t *value) {
    return modbus_generic_read_32(0x04, address, value, "Failed to read 32-bit Input Register");
}

bool modbus_component_read_holding_register_string(uint16_t address, uint16_t register_count, char *value, size_t max_len, bool big_endian) {
    return modbus_generic_read_string(0x03, address, register_count, value, max_len, big_endian, "Failed to read string Holding Registers");
}

bool modbus_component_read_input_register_string(uint16_t address, uint16_t register_count, char *value, size_t max_len, bool big_endian) {
    return modbus_generic_read_string(0x04, address, register_count, value, max_len, big_endian, "Failed to read string Input Registers");
}

// ============================================================================
// WRITE REGISTERS
// ============================================================================

bool modbus_component_write_register_16(uint16_t address, uint16_t value) {
    uint8_t rx_data[256];
    uint8_t rx_len = 0;

    // 0x06 = Write Single Register
    if (modbus_execute_transaction(0x06, address, value, rx_data, sizeof(rx_data), &rx_len)) {
        modbus_component_recover_normal_state();
        return true;
    }
    modbus_component_trigger_error_state("Failed to write register");
    return false;
}

bool modbus_component_write_register_32(uint16_t address, uint32_t value, bool big_endian) {
    uint8_t rx_data[256];
    uint8_t rx_len = 0;

    uint16_t tx_payload[2];

    // Determine the register order from the JSON template (big_endian setting)
    if (big_endian) {
        // Most significant word first (A B C D)
        tx_payload[0] = (uint16_t)(value >> 16);
        tx_payload[1] = (uint16_t)(value & 0xFFFF);
    }
    else {
        // Little Endian Word Swap (C D A B)
        tx_payload[0] = (uint16_t)(value & 0xFFFF);
        tx_payload[1] = (uint16_t)(value >> 16);
    }

    // Perform the FC16 transaction with 2 registers
    if (modbus_execute_transaction_write_multiple(address, 2, tx_payload, rx_data, sizeof(rx_data), &rx_len)) {
        modbus_component_recover_normal_state();
        return true;
    }

    modbus_component_trigger_error_state("Failed to write 32-bit register");
    return false;
}

// ============================================================================
// COIL INTERFACES
// ============================================================================

bool modbus_component_read_coil(uint16_t address, bool *value) {
    uint8_t rx_data[256];
    uint8_t rx_len = 0;

    // 0x01 = Read Coils, count = 1
    if (modbus_execute_transaction(0x01, address, 1, rx_data, sizeof(rx_data), &rx_len)) {
        if (rx_len >= 1) {
            // The status is packed into the bits of the first data byte
            *value = (rx_data[0] & 0x01) == 0x01;
            modbus_component_recover_normal_state();
            return true;
        }
    }
    modbus_component_trigger_error_state("Failed to read coil");
    return false;
}

bool modbus_component_write_coil(uint16_t address, bool value) {
    uint8_t rx_data[256];
    uint8_t rx_len = 0;

    // Modbus protocol: 0xFF00 turns coil ON, 0x0000 turns coil OFF
    uint16_t coil_data = value ? 0xFF00 : 0x0000;

    // 0x05 = Write Single Coil
    if (modbus_execute_transaction(0x05, address, coil_data, rx_data, sizeof(rx_data), &rx_len)) {
        modbus_component_recover_normal_state();
        return true;
    }
    modbus_component_trigger_error_state("Failed to write coil");
    return false;
}
