// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "modbus_value_utils.h"

#include <cstdint>
#include <stdio.h>

std::string modbus_value_as_string(const modbus_value_t &value) {
    char buffer[32] = {};

    switch (value.type) {
        case modbus_value_type_t::NONE:
            return "<none>";
        case modbus_value_type_t::BOOL:
            return value.data.bool_value ? "true" : "false";
        case modbus_value_type_t::INT16:
            snprintf(buffer, sizeof(buffer), "%d", value.data.int16_value);
            return buffer;
        case modbus_value_type_t::UINT16:
            snprintf(buffer, sizeof(buffer), "%u", value.data.uint16_value);
            return buffer;
        case modbus_value_type_t::INT32:
            snprintf(buffer, sizeof(buffer), "%ld", static_cast<long>(value.data.int32_value));
            return buffer;
        case modbus_value_type_t::UINT32:
            snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(value.data.uint32_value));
            return buffer;
        case modbus_value_type_t::FLOAT32:
            snprintf(buffer, sizeof(buffer), "%f", value.data.float32_value);
            return buffer;
        case modbus_value_type_t::IP_ADDR:
        case modbus_value_type_t::STRING:
        case modbus_value_type_t::REGEX:
            return value.data.string_value;
        case modbus_value_type_t::UNKNOWN:
        default:
            return "<unknown>";
    }
}

bool modbus_value_as_number(const modbus_value_t &value, double *output) {
    if (output == nullptr) {
        return false;
    }

    switch (value.type) {
        case modbus_value_type_t::BOOL:
            *output = value.data.bool_value ? 1.0 : 0.0;
            return true;
        case modbus_value_type_t::INT16:
            *output = value.data.int16_value;
            return true;
        case modbus_value_type_t::UINT16:
            *output = value.data.uint16_value;
            return true;
        case modbus_value_type_t::INT32:
            *output = value.data.int32_value;
            return true;
        case modbus_value_type_t::UINT32:
            *output = value.data.uint32_value;
            return true;
        case modbus_value_type_t::FLOAT32:
            *output = value.data.float32_value;
            return true;
        default:
            return false;
    }
}

bool modbus_value_as_scaled_uint16(const modbus_value_t &value, float multiplier, uint16_t *output) {
    if (output == nullptr) {
        return false;
    }

    double numeric_value = 0.0;
    if (!modbus_value_as_number(value, &numeric_value)) {
        return false;
    }

    *output = static_cast<uint16_t>(numeric_value * multiplier);
    return true;
}

bool modbus_value_as_scaled_uint32(const modbus_value_t &value, float multiplier, uint32_t *output) {
    if (output == nullptr) {
        return false;
    }

    double numeric_value = 0.0;
    if (!modbus_value_as_number(value, &numeric_value)) {
        return false;
    }

    *output = static_cast<uint32_t>(numeric_value * multiplier);
    return true;
}

modbus_value_t modbus_value_from_float(modbus_value_type_t type, float value) {
    modbus_value_t result = {};
    result.type = type;

    switch (type) {
        case modbus_value_type_t::BOOL:
            result.data.bool_value = value != 0.0f;
            break;
        case modbus_value_type_t::INT16:
            result.data.int16_value = static_cast<int16_t>(value);
            break;
        case modbus_value_type_t::UINT16:
            result.data.uint16_value = static_cast<uint16_t>(value);
            break;
        case modbus_value_type_t::INT32:
            result.data.int32_value = static_cast<int32_t>(value);
            break;
        case modbus_value_type_t::UINT32:
            result.data.uint32_value = static_cast<uint32_t>(value);
            break;
        case modbus_value_type_t::FLOAT32:
            result.data.float32_value = value;
            break;
        default:
            result.type = modbus_value_type_t::UNKNOWN;
            break;
    }

    return result;
}
