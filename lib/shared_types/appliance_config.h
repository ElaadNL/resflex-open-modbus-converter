// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdint.h>
#include <ems_bridge.h>
#include <string>
#include <vector>

/**
 * @brief Supported Modbus transport types for the configured appliance.
 */
typedef enum {
    MODBUS_CONN_TYPE_TCP = 0,
    MODBUS_CONN_TYPE_RTU = 1
} modbus_conn_type_t;

/**
 * @brief Resolved appliance configuration used during startup.
 *
 * Values are parsed from the appliance JSON template and may be completed
 * from Modbus-backed template fields before the EMS bridge is started.
 */
struct appliance_config_t {
    std::string name;                  /*!< Human-readable appliance name. */
    std::string model;                 /*!< Device model identifier. */
    std::string serial_number;         /*!< Device serial number. */
    std::string firmware_version;      /*!< Current firmware version. */
    std::string expected_firmware;     /*!< Expected firmware version or matching expression from the template. */
    modbus_conn_type_t interface_type; /*!< Modbus transport type: TCP or RTU. */
    std::string ip_address;            /*!< Modbus TCP target IP address. */
    uint16_t port;                     /*!< Modbus TCP target port. */
    uint8_t unit_id;                   /*!< Modbus slave/unit id. */

    uint32_t baudrate;       /*!< Modbus RTU baud rate. */
    uint16_t modbus_channel; /*!< UART channel used for Modbus RTU. */
    uint16_t parity;         /*!< UART parity setting for Modbus RTU. */
    uint16_t data_bits;      /*!< UART data-bit setting for Modbus RTU. */
    uint16_t stop_bits;      /*!< UART stop-bit setting for Modbus RTU. */

    uint16_t num_phases;                   /*!< Number of controllable electrical phases. */
    uint16_t instruction_delay_s;          /*!< S2 instruction processing delay in seconds. */
    bool defer_consequence;                /*!< true for DEFER behavior, false for VANISH behavior. */
    bool phase_symmetric;                  /*!< true when a single setpoint applies to all phases (one register controls all phases); false for independent per-phase setpoints. */
    float fallback_voltage;                /*!< Voltage (V) used by mapping_request_preprocessing() to convert a power instruction into a current setpoint when no recent valid ACTUAL_VOLTAGE_Lx measurement is available for that phase. */
    std::vector<ems_bridge_roles_t> roles; /*!< EMS roles parsed from the appliance template. */
};
