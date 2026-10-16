// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdint.h>

/**
 * @file ems_bridge_component.h
 * @brief Protocol-neutral bridge between device mapping logic and EMS protocols.
 *
 * The EMS bridge exposes generic sensor/actuator identifiers and result
 * queues. Protocol-specific behavior is delegated to wrappers such as the S2
 * wrapper. The current implementation supports S2 PEBC.
 */

/**
 * @brief Maximum number of roles a bridge can have.
 */
#define EMS_BRIDGE_ROLES_MAX_AMOUNT 3
/**
 * @brief Maximum number of phases a bridge can have.
 */
#define EMS_BRIDGE_PHASES_MAX_AMOUNT 3

/**
 * @brief Supported EMS bridge communication interfaces.
 */
typedef enum {
    EMS_INTERFACE_S2_PEBC /*!< S2 Protocol - Power Envelope Based Control */
} ems_bridge_interface_t;


/**
 * @brief Roles an appliance can fulfill in the energy management system.
 */
typedef enum {
    EMS_BRIDGE_ROLE_CONSUMER, /*!< Device consumes energy */
    EMS_BRIDGE_ROLE_PRODUCER, /*!< Device produces energy */
    EMS_BRIDGE_ROLE_STORAGE,  /*!< Device stores energy */
    EMS_BRIDGE_ROLE_SENTINEL, /*!< Sentinel value, do not use */
} ems_bridge_roles_t;


/**
 * @brief Full setup configuration for an EMS bridge device.
 *
 * This structure describes the identity and control configuration.
 * It is provided once during initialization and does not change at runtime.
 */
struct ems_bridge_setup_values {
    char brand[64];             /*!< Brand name of the device */
    char logo_uri[128];         /*!< URI pointing to the brand logo */
    char type[64];              /*!< Device type identifier */
    char model[64];             /*!< Device model identifier */
    char user_defined_name[64]; /*!< Human-readable name assigned by the user */

    struct {
        /**
         * @brief Roles this bridge fulfills. List is terminated by EMS_BRIDGE_ROLE_SENTINEL or by reaching EMS_BRIDGE_ROLES_MAX_AMOUNT entries.
         */
        ems_bridge_roles_t roles[EMS_BRIDGE_ROLES_MAX_AMOUNT];

        /**
         * @brief Delay in milliseconds between receiving and processing an instruction.
         */
        uint32_t instruction_processing_delay_ms;

        /**
         * @brief Number of active phases. Must be <= EMS_BRIDGE_PHASES_MAX_AMOUNT.
         */
        uint8_t phase_count;

        /**
         * @brief Behavior when a power constraint cannot be met.
         *
         * false = VANISH: the device disappears from the available resource pool (safe default).
         * true  = DEFER:  the device defers execution until the constraint can be met.
         */
        bool power_constraint_defer;

        /**
         * @brief Whether PEBC control is symmetric across phases.
         *
         * true  = SYMMETRIC: a single setpoint applies to all phases. The S2 wrapper
         *         declares ELECTRIC.POWER.3_PHASE_SYMMETRIC, so the CEM sends one
         *         instruction per cycle that maps to SETPOINT_MAX_POWER_PHASE_SYMMETRIC /
         *         SETPOINT_MIN_POWER_PHASE_SYMMETRIC. Suitable for appliances with a single
         *         control register (e.g. many EV chargers).
         * false = PER_PHASE: independent setpoints per phase. The wrapper declares
         *         ELECTRIC.POWER.L1/L2/L3 separately and maps instructions onto
         *         SETPOINT_MAX_POWER_L1/L2/L3 / SETPOINT_MIN_POWER_L1/L2/L3.
         *
         * Independent of measurement declaration: a device may measure per phase
         * and still be controlled symmetrically.
         */
        bool phase_symmetric;

        /**
         * @brief Default power and energy limits per phase.
         *
         * Ordered list; index 0 corresponds to phase 1, index 1 to phase 2, etc.
         */
        struct {
            float power_upper_limit;  /*!< Default upper power limit in watts */
            float power_lower_limit;  /*!< Default lower power limit in watts */
            float energy_upper_limit; /*!< Default upper energy limit in watt-hours */
            float energy_lower_limit; /*!< Default lower energy limit in watt-hours */
        } default_limits[EMS_BRIDGE_PHASES_MAX_AMOUNT];
    } control;
};

/**
 * @brief Sensor identifiers exposed by an EMS bridge.
 *
 * Used to request a specific measurement from the bridge implementation.
 */
typedef enum {
    EMS_BRIDGE_SENSOR_ACTUAL_CURRENT_L1_A,
    EMS_BRIDGE_SENSOR_ACTUAL_CURRENT_L2_A,
    EMS_BRIDGE_SENSOR_ACTUAL_CURRENT_L3_A,
    EMS_BRIDGE_SENSOR_ACTUAL_CURRENT_TOTAL_A,
    EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V,
    EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L2_V,
    EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L3_V,
    EMS_BRIDGE_SENSOR_ACTUAL_POWER_L1_W,
    EMS_BRIDGE_SENSOR_ACTUAL_POWER_L2_W,
    EMS_BRIDGE_SENSOR_ACTUAL_POWER_L3_W,
    EMS_BRIDGE_SENSOR_ACTUAL_POWER_TOTAL_W,
    EMS_BRIDGE_SENSOR_POWER_LIMIT_UPPER_START_W,
    EMS_BRIDGE_SENSOR_POWER_LIMIT_UPPER_END_W,
    EMS_BRIDGE_SENSOR_POWER_LIMIT_LOWER_START_W,
    EMS_BRIDGE_SENSOR_POWER_LIMIT_LOWER_END_W,
    EMS_BRIDGE_SENSOR_ENERGY_LIMIT_UPPER_START_WH,
    EMS_BRIDGE_SENSOR_ENERGY_LIMIT_UPPER_END_WH,
    EMS_BRIDGE_SENSOR_ENERGY_LIMIT_LOWER_START_WH,
    EMS_BRIDGE_SENSOR_ENERGY_LIMIT_LOWER_END_WH,
    EMS_BRIDGE_SENSOR_STATUS, // outputmapping -> ENUM_STATUS_STATUS_OK/ENUM_STATUS_STATUS_ERR
} ems_bridge_sensor_t;


/**
 * @brief Actuator identifiers exposed by an EMS bridge.
 *
 * Used to set a specific control value on the modbus.
 */
typedef enum {
    EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_PHASE_SYMMETRIC_A,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L1_A,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L2_A,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L3_A,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_PHASE_SYMMETRIC_W,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L2_W,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L3_W,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_PHASE_SYMMETRIC_A,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_L1_A,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_L2_A,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_L3_A,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_PHASE_SYMMETRIC_W,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L1_W,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L2_W,
    EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L3_W,
    HEARTBEAT
} ems_bridge_actuator_t;


/**
 * @brief Operational status of an EMS bridge or sensor read.
 */
typedef enum {
    EMS_BRIDGE_STATUS_OK,
    EMS_BRIDGE_STATUS_ERR
} ems_bridge_status_t;

/**
 * @brief Result of a sensor read request.
 */
struct ems_bridge_sensor_result {
    ems_bridge_sensor_t sensorId; /*!< Sensor this result belongs to. */
    ems_bridge_status_t status;   /*!< Result status. */
    float value;                  /*!< Sensor value; only valid when status is OK. */
};

/**
 * @brief Request to set an actuator to a specific value.
 */
struct ems_bridge_set_actuator_request {
    ems_bridge_actuator_t actuatorId; /*!< Actuator to write. */
    float value;                      /*!< Requested actuator value. */
};

/**
 * @brief Result of an actuator set request.
 */
struct ems_bridge_set_actuator_result {
    ems_bridge_actuator_t actuatorId; /*!< Actuator that was written. */
    ems_bridge_status_t status;       /*!< Write result status. */
};

/**
 * @brief Callback invoked by the EMS system to request a sensor reading from modbus.
 *
 * The modbus must read the specified sensor and report the result back to the EMS system through the appropriate reporting mechanism.
 *
 * @param sensor  The sensor whose value is being requested.
 */
typedef void (*ems_bridge_request_sensor_cb_t)(ems_bridge_sensor_t);

/**
 * @brief Callback invoked by the EMS system to let modbus apply an actuator setpoint.
 *
 * The modbus must apply the value in the request to the specified actuator.
 *
 * @param request  The actuator and the value to apply.
 */
typedef void (*ems_bridge_set_actuator_cb_t)(ems_bridge_set_actuator_request);

/**
 * @brief Callback invoked when the bridge has completed initialization and is ready for operation.
 */
typedef void (*ems_bridge_on_init_ready_cb_t)(void);

/**
 * @brief Sends a sensor result to the newSensorValueQueue.
 *
 * @param result Pointer to the sensor result to send.
 * @return true if the result was successfully queued, false if the queue was full.
 */
bool ems_bridge_send_new_sensor_value(ems_bridge_sensor_result *result);

/**
 * @brief Sends an actuator result to the setActuatorResultQueue.
 *
 * @param result Pointer to the actuator result to send.
 * @return true if the result was successfully queued, false if the queue was full.
 */
bool ems_bridge_send_set_actuator_result(ems_bridge_set_actuator_result *result);


/**
 * @brief Initialize the EMS bridge and selected protocol wrapper.
 *
 * Creates the sensor-result and actuator-result queues, starts the processing
 * tasks for both queues, stores the EMS interface type, and initializes the
 * selected protocol wrapper. For `EMS_INTERFACE_S2_PEBC`, this loads S2 HEMS
 * settings from NVS and calls `s2_init()`.
 *
 * @example
 * @code
 * ems_bridge_setup_values appliance_config = {
 *     .brand = "example",
 *     .logo_uri = "https://example.com/logo.png",
 *     .type = "ev_charger",
 *     .model = "rev1",
 *     .user_defined_name = "driveway charger",
 *     .control = {
 *         .roles = {EMS_BRIDGE_ROLE_CONSUMER, EMS_BRIDGE_ROLE_PRODUCER},
 *         .instruction_processing_delay_ms = 1000,
 *         .phase_count = 2,
 *         .power_constraint_defer = false,
 *         .default_limits = {
 *             { // phase1
 *                 .power_upper_limit = 1000,
 *                 .power_lower_limit = 0,
 *                 .energy_upper_limit = 333,
 *                 .energy_lower_limit = 0,
 *             },
 *             { // phase2
 *                 .power_upper_limit = 1000,
 *                 .power_lower_limit = 0,
 *                 .energy_upper_limit = 333,
 *                 .energy_lower_limit = 0,
 *             }
 *         }
 *     }
 * };
 *
 * // init:
 * bool bridge_ready = ems_bridge_init(EMS_INTERFACE_S2_PEBC, &appliance_config, request_sensor_cb, set_actuator_cb, on_init_ready_cb);
 *
 * @endcode
 *
 * @param ems_type EMS protocol interface to initialize.
 * @param appliance_config Appliance identity/control setup passed to the protocol wrapper.
 * @param request_sensor_cb Callback used by the protocol wrapper to request sensor values.
 * @param set_actuator_cb Callback used by the protocol wrapper to set actuator values.
 * @param on_init_ready_cb Callback invoked when the protocol session is ready.
 * @return true if queues and the selected protocol wrapper started successfully, false otherwise.
 */
bool ems_bridge_init(
    ems_bridge_interface_t ems_type,
    ems_bridge_setup_values *appliance_config,
    ems_bridge_request_sensor_cb_t request_sensor_cb,
    ems_bridge_set_actuator_cb_t set_actuator_cb,
    ems_bridge_on_init_ready_cb_t on_init_ready_cb);
