// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "mapping_request_preprocessing.h"

#include <cstdint>
#include <esp_log.h>
#include "modbus_mapping_cache.h"

static const char *TAG = "Modbus Mapping Preprocessing";

/**
 * @brief Returns true when the active template exposes the given actuator.
 *
 * Inline silent check; deliberately does not call
 * modbus_appliance_definition::find_actuator_index() because that helper logs
 * an error when the actuator is missing, which would flood the log every time
 * the preprocessing probes for a not-present power actuator.
 */
static bool template_has_actuator(const modbus_appliance_definition &definition, ems_bridge_actuator_t actuator_id) {
    for (const actuator_t &actuator : definition.actuators) {
        if (actuator.emd_actuator_type == actuator_id) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Maps an S2 power-setpoint actuator onto the corresponding current
 *        setpoint and the voltage sensor needed for the conversion.
 *
 * Returns true when the requested actuator is a SETPOINT_*_POWER_* identifier
 * (so the request is eligible for power -> current conversion). Returns false
 * for any other actuator (current setpoints, heartbeat, etc.); in that case
 * the request must be forwarded as-is.
 *
 * @param power_actuator       Incoming actuator identifier from S2.
 * @param current_actuator_out Equivalent current-setpoint actuator for the same
 *                             MIN/MAX role and phase.
 * @param voltage_sensor_out   Voltage sensor used for the conversion. For the
 *                             phase-symmetric setpoint this is set to L1 by
 *                             convention, as all phases carry the same value.
 */
static bool map_power_to_current(
    ems_bridge_actuator_t power_actuator,
    ems_bridge_actuator_t *current_actuator_out,
    ems_bridge_sensor_t *voltage_sensor_out) {
    switch (power_actuator) {
        case EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W:
            *current_actuator_out = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L1_A;
            *voltage_sensor_out = EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V;
            return true;
        case EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L2_W:
            *current_actuator_out = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L2_A;
            *voltage_sensor_out = EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L2_V;
            return true;
        case EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L3_W:
            *current_actuator_out = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L3_A;
            *voltage_sensor_out = EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L3_V;
            return true;
        case EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L1_W:
            *current_actuator_out = EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_L1_A;
            *voltage_sensor_out = EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V;
            return true;
        case EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L2_W:
            *current_actuator_out = EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_L2_A;
            *voltage_sensor_out = EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L2_V;
            return true;
        case EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L3_W:
            *current_actuator_out = EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_L3_A;
            *voltage_sensor_out = EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L3_V;
            return true;
        case EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_PHASE_SYMMETRIC_W:
            *current_actuator_out = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_PHASE_SYMMETRIC_A;
            *voltage_sensor_out = EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V;
            return true;
        case EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_PHASE_SYMMETRIC_W:
            *current_actuator_out = EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_PHASE_SYMMETRIC_A;
            *voltage_sensor_out = EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V;
            return true;
        default:
            return false;
    }
}

/**
 * @brief Picks the most recent valid voltage for a phase, falling back to
 *        the configured fallback voltage when the cache is empty or stale.
 */
static float voltage_for_phase(ems_bridge_sensor_t voltage_sensor, float fallback_voltage, uint32_t now_ms) {
    float voltage = 0.0f;
    if (modbus_mapping_cache_get(voltage_sensor, &voltage, now_ms)) {
        return voltage;
    }
    return fallback_voltage;
}


// the actual preprocessing function
ems_bridge_set_actuator_request mapping_request_preprocessing(
    ems_bridge_set_actuator_request request,
    const modbus_appliance_definition &definition,
    const appliance_config_t &config,
    uint32_t now_ms) {
    ems_bridge_actuator_t current_actuator;
    ems_bridge_sensor_t voltage_sensor;

    // Not a power setpoint -> pass through untouched.
    if (!map_power_to_current(request.actuatorId, &current_actuator, &voltage_sensor)) {
        return request;
    }

    // Power actuator is present in the template -> the modbus side can handle
    // it directly, no conversion needed.
    if (template_has_actuator(definition, request.actuatorId)) {
        return request;
    }

    // Power actuator absent but current actuator also absent -> nothing we can
    // do here; pass through and let the regular not-found path handle it.
    if (!template_has_actuator(definition, current_actuator)) {
        return request;
    }

    float power_value = request.value;
    float current_value = 0.0f;

    float phase_voltage = voltage_for_phase(voltage_sensor, config.fallback_voltage, now_ms);

    if (phase_voltage > 0.0f) {
        current_value = power_value / phase_voltage;
    }
    ESP_LOGI(TAG, "Converted power %.2f W -> current %.3f A (phase V = %.2f, fallback = %.2f)", power_value, current_value, phase_voltage, config.fallback_voltage);

    ems_bridge_set_actuator_request rewritten_request;
    rewritten_request.actuatorId = current_actuator;
    rewritten_request.value = current_value;
    return rewritten_request;
}
