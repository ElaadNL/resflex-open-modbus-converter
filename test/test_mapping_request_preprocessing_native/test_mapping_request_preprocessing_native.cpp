// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <mapping_request_preprocessing.h>
#include <modbus_appliance_definition.h>
#include <modbus_mapping_cache.h>

#include "native_test_state.h"

// Exercises mapping_request_preprocessing(): the power -> current rewrite that
// runs when a template exposes only current setpoints. The rewrite divides the
// requested power by the latest valid phase voltage (from the cache) or by the
// configured fallbackVoltage when no fresh voltage is available.

// Template that exposes only current setpoints (no SETPOINT_*_POWER_* actuators),
// so any incoming power setpoint must be converted to current. fallbackVoltage
// is 230 V.
static const char* kCurrentOnlyTemplate = R"json(
{
  "appliance": {
    "spec": { "name": { "value": "Current Only" } },
    "connection": { "iface": "RTU", "unitId": { "value": 1 } },
    "control": { "fallbackVoltage": { "value": 230.0 } }
  },
  "sensors": [
    { "mappingId": "ACTUAL_VOLTAGE_L1", "source": "MODBUS", "type": "UINT16", "startAddress": 100, "sourceSize": 16 }
  ],
  "actuators": [
    { "mappingId": "SETPOINT_MAX_CURRENT_L1", "type": "UINT16", "multiplier": 1, "startAddress": 200, "destinationSize": 16 },
    { "mappingId": "SETPOINT_MAX_CURRENT_PHASE_SYMMETRIC", "type": "UINT16", "multiplier": 1, "startAddress": 202, "destinationSize": 16 }
  ]
}
)json";

// Template that exposes the power setpoint directly -> no rewrite expected.
static const char* kPowerCapableTemplate = R"json(
{
  "appliance": {
    "spec": { "name": { "value": "Power Capable" } },
    "connection": { "iface": "RTU", "unitId": { "value": 1 } },
    "control": { "fallbackVoltage": { "value": 230.0 } }
  },
  "sensors": [],
  "actuators": [
    { "mappingId": "SETPOINT_MAX_POWER_L1", "type": "UINT16", "multiplier": 1, "startAddress": 200, "destinationSize": 16 }
  ]
}
)json";

void setUp(void) {
    native_stubs::reset();
    modbus_mapping_cache_clear();
}

void tearDown(void) {
}

void test_non_power_request_passes_through_untouched(void) {
    modbus_appliance_definition definition;
    TEST_ASSERT_TRUE(definition.load_from_json(kCurrentOnlyTemplate));

    ems_bridge_set_actuator_request request;
    request.actuatorId = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L1_A;
    request.value = 16.0f;

    ems_bridge_set_actuator_request result = mapping_request_preprocessing(request, definition, definition.config, 1000);

    TEST_ASSERT_EQUAL_INT(EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L1_A, result.actuatorId);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 16.0f, result.value);
}

void test_power_request_passes_through_when_template_has_power_actuator(void) {
    modbus_appliance_definition definition;
    TEST_ASSERT_TRUE(definition.load_from_json(kPowerCapableTemplate));

    ems_bridge_set_actuator_request request;
    request.actuatorId = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W;
    request.value = 3680.0f;

    ems_bridge_set_actuator_request result = mapping_request_preprocessing(request, definition, definition.config, 1000);

    // Power actuator is present -> forwarded as-is, no conversion.
    TEST_ASSERT_EQUAL_INT(EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W, result.actuatorId);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 3680.0f, result.value);
}

void test_power_request_converts_to_current_using_cached_voltage(void) {
    modbus_appliance_definition definition;
    TEST_ASSERT_TRUE(definition.load_from_json(kCurrentOnlyTemplate));

    // Seed a fresh L1 voltage measurement (240 V, valid for 5 s from t=1000).
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 240.0f, 5000, 1000);

    ems_bridge_set_actuator_request request;
    request.actuatorId = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W;
    request.value = 4800.0f;

    ems_bridge_set_actuator_request result = mapping_request_preprocessing(request, definition, definition.config, 2000);

    // 4800 W / 240 V = 20 A, mapped onto the L1 current setpoint.
    TEST_ASSERT_EQUAL_INT(EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L1_A, result.actuatorId);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, result.value);
}

void test_power_request_uses_fallback_voltage_when_cache_stale(void) {
    modbus_appliance_definition definition;
    TEST_ASSERT_TRUE(definition.load_from_json(kCurrentOnlyTemplate));

    // Voltage recorded at t=1000 with a 1 s window -> stale by t=3000.
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 240.0f, 1000, 1000);

    ems_bridge_set_actuator_request request;
    request.actuatorId = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W;
    request.value = 4600.0f;

    ems_bridge_set_actuator_request result = mapping_request_preprocessing(request, definition, definition.config, 3000);

    // Cache stale -> fallbackVoltage 230 V used: 4600 / 230 = 20 A.
    TEST_ASSERT_EQUAL_INT(EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L1_A, result.actuatorId);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 20.0f, result.value);
}

void test_phase_symmetric_power_converts_using_l1_voltage(void) {
    modbus_appliance_definition definition;
    TEST_ASSERT_TRUE(definition.load_from_json(kCurrentOnlyTemplate));

    // Phase-symmetric conversion uses L1 voltage by convention.
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 230.0f, 5000, 1000);

    ems_bridge_set_actuator_request request;
    request.actuatorId = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_PHASE_SYMMETRIC_W;
    request.value = 2300.0f;

    ems_bridge_set_actuator_request result = mapping_request_preprocessing(request, definition, definition.config, 2000);

    // 2300 W / 230 V = 10 A onto the phase-symmetric current setpoint.
    TEST_ASSERT_EQUAL_INT(EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_PHASE_SYMMETRIC_A, result.actuatorId);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 10.0f, result.value);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_non_power_request_passes_through_untouched);
    RUN_TEST(test_power_request_passes_through_when_template_has_power_actuator);
    RUN_TEST(test_power_request_converts_to_current_using_cached_voltage);
    RUN_TEST(test_power_request_uses_fallback_voltage_when_cache_stale);
    RUN_TEST(test_phase_symmetric_power_converts_using_l1_voltage);
    return UNITY_END();
}
