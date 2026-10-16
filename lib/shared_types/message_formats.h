// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "led_component.h"
#include <cstdint>
#include <string>

#define MODBUS_VALUE_STRING_MAX_LEN 64

/**
 * Shared value types for template values and messages between tasks.
 *
 * Keep this enum as the single source of truth for values moving between the
 * S2 wrapper, modbus mapping, and Modbus components.
 */
enum class modbus_value_type_t {
    NONE = 0,
    BOOL,
    INT16,
    UINT16,
    INT32,
    UINT32,
    FLOAT32,
    IP_ADDR,
    STRING,
    REGEX,
    UNKNOWN
};

typedef struct {
    modbus_value_type_t type; /*!< Discriminator that indicates which union field is valid. */
    union {
        bool bool_value;
        int16_t int16_value;
        uint16_t uint16_value;
        int32_t int32_value;
        uint32_t uint32_value;
        float float32_value;
        char string_value[MODBUS_VALUE_STRING_MAX_LEN];
    } data;
} modbus_value_t;

/**
 * @brief Queue payload for an S2-originated Modbus actuator request.
 */
struct s2_modbus_actuator_t {
    uint16_t message_id;     /*!< Monotonic request id reserved for correlating future responses. */
    modbus_value_t value;    /*!< Typed actuator value to write. */
    uint16_t actuator_index; /*!< Index into modbus_appliance_definition::actuators. */
};

/**
 * @brief Queue payload for an S2-originated Modbus sensor request.
 */
struct s2_modbus_sensor_t {
    uint16_t message_id;   /*!< Monotonic request id reserved for correlating future responses. */
    uint16_t sensor_index; /*!< Index into modbus_appliance_definition::sensors. */
};

/**
 * @brief Generic Modbus response payload reserved for queue-based responses.
 *
 * The current EMS bridge result path sends sensor and actuator results through
 * EMS bridge queues instead of using this queue payload.
 */
struct s2_modbus_response_t {
    uint16_t message_id;  /*!< Request id this response belongs to. */
    modbus_value_t value; /*!< Returned typed value. */
    bool success;         /*!< true when the Modbus operation succeeded. */
};

/**
 * @brief Queue payload for LED state changes.
 */
struct led_set_state_t {
    led_state led_state_value; /*!< Desired LED state. */
};

/**
 * @brief Discriminator for the active payload in message_definition_t.
 */
typedef enum {
    MESSAGE_TYPE_MODBUS_ACTUATOR,
    MESSAGE_TYPE_MODBUS_SENSOR,
    MESSAGE_TYPE_MODBUS_RESPONSE,
    MESSAGE_TYPE_LED_STATUS
} message_type;

/**
 * @brief Shared queue message envelope used by component queues.
 */
typedef struct {
    message_type type; /*!< Discriminator for the active union member. */
    union {
        s2_modbus_actuator_t modbus_actuator;
        s2_modbus_sensor_t modbus_sensor;
        s2_modbus_response_t modbus_response;
        led_set_state_t led_message;
    } data;
} message_definition_t;
