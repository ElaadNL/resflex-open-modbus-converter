// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <message_formats.h>
#include <cstdint>
#include <string>

/**
 * @brief Converts a typed Modbus value to text for logging/debugging.
 * @param value Typed value to format.
 * @return Human-readable value string; unsupported/empty types return a marker such as "<unknown>".
 */
std::string modbus_value_as_string(const modbus_value_t &value);

/**
 * @brief Converts numeric Modbus value types to a double.
 * @param value Typed value to convert. BOOL and numeric types are supported; strings are not.
 * @param output Destination pointer for the converted number.
 * @return true when conversion succeeded; false for unsupported types or a null output pointer.
 */
bool modbus_value_as_number(const modbus_value_t &value, double *output);

/**
 * @brief Converts a numeric Modbus value to a scaled 16-bit register value.
 * @param value Typed value to convert. Must be supported by modbus_value_as_number().
 * @param multiplier Scaling factor from the device template.
 * @param output Destination pointer for the uint16_t register value.
 * @return true when conversion succeeded; false for unsupported types or a null output pointer.
 */
bool modbus_value_as_scaled_uint16(const modbus_value_t &value, float multiplier, uint16_t *output);

/**
 * @brief Converts a numeric Modbus value to a scaled 32-bit register value.
 * @param value Typed value to convert. Must be supported by modbus_value_as_number().
 * @param multiplier Scaling factor from the device template.
 * @param output Destination pointer for the uint32_t register value.
 * @return true when conversion succeeded; false for unsupported types or a null output pointer.
 */
bool modbus_value_as_scaled_uint32(const modbus_value_t &value, float multiplier, uint32_t *output);

/**
 * @brief Creates a typed Modbus value from a float input.
 * @param type Desired target value type.
 * @param value Source float value, cast into the union field that matches type.
 * @return modbus_value_t with the requested type, or UNKNOWN when the type cannot be created from float.
 */
modbus_value_t modbus_value_from_float(modbus_value_type_t type, float value);
