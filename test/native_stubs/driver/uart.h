// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

enum {
    UART_NUM_0 = 0,
    UART_NUM_1 = 1,
    UART_NUM_2 = 2,
    UART_NUM_MAX = 3,
};

enum {
    UART_PARITY_DISABLE = 0,
    UART_PARITY_EVEN = 2,
    UART_PARITY_ODD = 3,
};

enum {
    UART_DATA_5_BITS = 0,
    UART_DATA_6_BITS = 1,
    UART_DATA_7_BITS = 2,
    UART_DATA_8_BITS = 3,
};

enum {
    UART_STOP_BITS_1 = 1,
    UART_STOP_BITS_1_5 = 2,
    UART_STOP_BITS_2 = 3,
};

typedef int uart_port_t;
typedef int uart_parity_t;
typedef int uart_word_length_t;
typedef int uart_stop_bits_t;

enum {
    UART_PIN_NO_CHANGE = -1,
    UART_MODE_RS485_HALF_DUPLEX = 2,
    UART_HW_FLOWCTRL_DISABLE = 0,
    UART_SCLK_DEFAULT = 0,
};

typedef struct {
    int baud_rate;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    int flow_ctrl;
    int rx_flow_ctrl_thresh;
    int source_clk;
} uart_config_t;

// Native-test transport state for the UART stub.
//
// Every uart_write_bytes() call records the request bytes. Every
// uart_read_bytes() call serves the next response from the FIFO unless a
// read override is set (used to simulate timeouts).
namespace modbus_test_uart {
extern std::vector<std::vector<unsigned char>> requests;
extern std::deque<std::vector<unsigned char>> responses;
extern int uart_write_bytes_override;
extern bool uart_write_bytes_override_set;
extern int uart_read_bytes_override;
extern bool uart_read_bytes_override_set;

void reset();
}

esp_err_t uart_param_config(uart_port_t port, const uart_config_t* config);
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts);
esp_err_t uart_driver_install(uart_port_t port, int rx_buffer_size, int tx_buffer_size, int queue_size, void* queue, int flags);
esp_err_t uart_set_mode(uart_port_t port, int mode);
void uart_flush_input(uart_port_t port);
int uart_write_bytes(uart_port_t port, const char* data, unsigned int len);
int uart_read_bytes(uart_port_t port, void* data, unsigned int len, TickType_t wait_ms);
