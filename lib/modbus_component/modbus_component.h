// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <appliance_config.h>

/**
 * @brief Initializes the Modbus connection.
 *
 * For TCP, opens a socket to the configured target IP/port. For RTU,
 * configures the UART hardware and pins for RS485 half-duplex mode.
 *
 * @param config Appliance configuration loaded from the device template JSON.
 * @return true if the TCP connection or RTU hardware initialization succeeded,
 *         false otherwise.
 */
bool modbus_component_init(const appliance_config_t &config);

/**
 * @brief Reads a single 16-bit Holding Register (Function Code 0x03).
 *
 * Typically used to read configuration parameters or writable settings.
 *
 * @param address The 0-based memory address of the register to read.
 * @param value Pointer to the variable where the retrieved 16-bit value will be stored.
 * @return true if the transaction was successful and the value is valid.
 * @return false if a timeout, network error, or Modbus Exception occurred.
 */
bool modbus_component_read_holding_register(uint16_t address, uint16_t *value);

/**
 * @brief Reads a 32-bit Holding Register (Function Code 0x03).
 *
 * Reads two consecutive 16-bit holding registers starting at the given address
 * and combines them into a single 32-bit unsigned integer.
 *
 * @param address The 0-based memory address of the first register.
 * @param value Pointer to the variable where the retrieved 32-bit value will be stored.
 * @return true if the transaction was successful and the value is valid.
 * @return false if a timeout, network error, or Modbus Exception occurred.
 */
bool modbus_component_read_holding_register(uint16_t address, uint32_t *value);

/**
 * @brief Reads a single 16-bit Input Register (Function Code 0x04).
 *
 * Typically used to read live sensor data, metrics, or read-only statuses.
 *
 * @param address The 0-based memory address of the register to read.
 * @param value Pointer to the variable where the retrieved 16-bit value will be stored.
 * @return true if the transaction was successful and the value is valid.
 * @return false if a timeout, network error, or Modbus Exception occurred.
 */
bool modbus_component_read_input_register(uint16_t address, uint16_t *value);

/**
 * @brief Reads a 32-bit Input Register (Function Code 0x04).
 *
 * Reads two consecutive 16-bit input registers starting at the given address
 * and combines them into a single 32-bit unsigned integer.
 *
 * @param address The 0-based memory address of the first register.
 * @param value Pointer to the variable where the retrieved 32-bit value will be stored.
 * @return true if the transaction was successful and the value is valid.
 * @return false if a timeout, network error, or Modbus Exception occurred.
 */
bool modbus_component_read_input_register(uint16_t address, uint32_t *value);

/**
 * @brief Reads multiple Holding Registers (Function Code 0x03) and decodes them as an ASCII string.
 * @param address The 0-based memory address of the first register.
 * @param register_count Number of 16-bit registers to read.
 * @param value Destination buffer for the null-terminated string.
 * @param max_len Size of the destination buffer.
 * @param big_endian true for high-byte/low-byte character order per register, false for swapped order.
 * @return true if the transaction was successful and the string was decoded.
 * @return false if a timeout, network error, Modbus Exception, or invalid argument occurred.
 */
bool modbus_component_read_holding_register_string(uint16_t address, uint16_t register_count, char *value, size_t max_len, bool big_endian);

/**
 * @brief Reads multiple Input Registers (Function Code 0x04) and decodes them as an ASCII string.
 * @param address The 0-based memory address of the first register.
 * @param register_count Number of 16-bit registers to read.
 * @param value Destination buffer for the null-terminated string.
 * @param max_len Size of the destination buffer.
 * @param big_endian true for high-byte/low-byte character order per register, false for swapped order.
 * @return true if the transaction was successful and the string was decoded.
 * @return false if a timeout, network error, Modbus Exception, or invalid argument occurred.
 */
bool modbus_component_read_input_register_string(uint16_t address, uint16_t register_count, char *value, size_t max_len, bool big_endian);

/**
 * @brief Writes a value to a single 16-bit Holding Register (Function Code 0x06).
 * @param address The 0-based memory address of the register to write to.
 * @param value The 16-bit value to write to the device.
 * @return true if the device successfully acknowledged the write command.
 * @return false if a timeout, network error, or Modbus Exception occurred.
 */
bool modbus_component_write_register_16(uint16_t address, uint16_t value);

/**
 * @brief Writes a value to a set of two 16-bit Holding Register (Function Code 0x16).
 * @param address The 0-based memory address of the register to write to.
 * @param value The 32-bit value to write to the device.
 * @param big_endian true to write the high word first, false to write the low word first.
 * @return true if the device successfully acknowledged the write command.
 * @return false if a timeout, network error, or Modbus Exception occurred.
 */
bool modbus_component_write_register_32(uint16_t address, uint32_t value, bool big_endian);

/**
 * @brief Reads the status of a single Coil (Function Code 0x01).
 *
 * Coils are discrete, 1-bit boolean values.
 *
 * @param address The 0-based memory address of the coil.
 * @param value Pointer to the boolean variable where the status (true = ON, false = OFF) will be stored.
 * @return true if the transaction was successful.
 * @return false if a timeout, network error, or Modbus Exception occurred.
 */
bool modbus_component_read_coil(uint16_t address, bool *value);

/**
 * @brief Writes a boolean status to a single Coil (Function Code 0x05).
 * @param address The 0-based memory address of the coil.
 * @param value The boolean state to write (true = ON/0xFF00, false = OFF/0x0000).
 * @return true if the device successfully acknowledged the write command.
 * @return false if a timeout, network error, or Modbus Exception occurred.
 */
bool modbus_component_write_coil(uint16_t address, bool value);
