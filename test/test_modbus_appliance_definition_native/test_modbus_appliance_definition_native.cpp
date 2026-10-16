// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <cstring>

#include <modbus_appliance_definition.h>
#include <modbus_value_utils.h>
#include <definitions.h>

#include "native_test_state.h"

static const char* kTemplateJson = R"json(
{
  "appliance": {
    "spec": {
      "name": { "value": "Unit Test Device" },
      "model": { "value": "UT-1" },
      "firmwareVersion": { "value": "1.2.3" },
      "expectedFirmware": { "value": "1\\..*" }
    },
    "connection": {
      "iface": "RTU",
      "unitId": { "value": 7 },
      "eth": {
        "ipAddress": { "value": "192.168.4.20" },
        "port": { "value": 1502 }
      }
    },
    "control": {
      "numberOfPhases": { "value": 3 },
      "instructionDelayS": { "value": 2 },
      "consequenceTypeDefer": { "value": false }
    }
  },
  "initialisationSequence": [
    { "functionCode": "FC06", "startAddress": 100, "type": "UINT16", "value": 42, "waitMs": 250 }
  ],
  "sensors": [
    {
      "mappingId": "ACTUAL_POWER_TOTAL",
      "source": "MODBUS",
      "functionCode": "FC04",
      "type": "INT16",
      "startAddress": 30006,
      "sourceSize": 16,
      "multiplier": 0.5,
      "bigEndian": true,
      "updateIntervalMs": 1000,
      "validityDurationMs": 3000,
      "bitMask": 12,
      "outputMapping": [
        { "value": 0, "label": "OFF" },
        { "value": 1, "label": "ON" }
      ]
    },
    {
      "mappingId": "ACTUAL_POWER_L1",
      "source": "MODBUS",
      "functionCode": "FC03",
      "type": "UINT32",
      "startAddress": 40010,
      "bigEndian": false
    }
  ],
  "actuators": [
    {
      "mappingId": "SETPOINT_MAX_POWER_L1",
      "functionCode": "FC06",
      "type": "UINT16",
      "bigEndian": true,
      "multiplier": 1,
      "startAddress": 42020,
      "destinationSize": 16,
      "bitMask": 255,
      "evaluationIntervalS": 1,
      "maxStepsizeUp": 200,
      "maxStepsizeDown": 100,
      "minStepTimeMs": 1000,
      "requiredPreSequence": [
        { "functionCode": "FC06", "startAddress": 42000, "type": "UINT16", "value": 21930, "waitMs": 500, "bitMask": 3840 }
      ],
      "requiredPostSequence": [
        { "functionCode": "FC06", "startAddress": 42000, "type": "UINT16", "value": 21947, "waitMs": 200 }
      ]
    },
    {
      "mappingId": "SETPOINT_MIN_POWER_L1",
      "functionCode": "FC16",
      "type": "UINT32",
      "bigEndian": true,
      "multiplier": 0.1,
      "startAddress": 42022,
      "destinationSize": 32
    }
  ]
}
)json";

void setUp(void) {
    native_stubs::reset();
}

void tearDown(void) {
}

void test_load_from_json_populates_basic_config(void) {
    modbus_appliance_definition mapping;

    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));

    TEST_ASSERT_EQUAL_STRING("Unit Test Device", mapping.config.name.c_str());
    TEST_ASSERT_EQUAL_STRING("UT-1", mapping.config.model.c_str());
    TEST_ASSERT_EQUAL_INT(MODBUS_CONN_TYPE_RTU, mapping.config.interface_type);
    TEST_ASSERT_EQUAL_UINT8(7, mapping.config.unit_id);
    TEST_ASSERT_EQUAL_UINT16(1502, mapping.config.port);
    TEST_ASSERT_EQUAL_UINT16(3, mapping.config.num_phases);
    TEST_ASSERT_FALSE(mapping.config.defer_consequence);
}

void test_load_from_json_populates_initialisation_sequence(void) {
    modbus_appliance_definition mapping;

    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));

    TEST_ASSERT_EQUAL_UINT32(1, mapping.initialisation_sequence.size());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(function_code_t::WRITE_SINGLE),
                          static_cast<int>(mapping.initialisation_sequence[0].fc));
    TEST_ASSERT_EQUAL_UINT16(100, mapping.initialisation_sequence[0].start_address);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 42.0f, mapping.initialisation_sequence[0].value);
    TEST_ASSERT_EQUAL_UINT32(250, mapping.initialisation_sequence[0].wait_ms);
}

void test_load_from_json_populates_sensors(void) {
    modbus_appliance_definition mapping;

    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));

    TEST_ASSERT_EQUAL_UINT32(2, mapping.sensors.size());
    TEST_ASSERT_EQUAL_STRING("ACTUAL_POWER_TOTAL", mapping.sensors[0].mapping_id.c_str());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(function_code_t::READ_IR),
                          static_cast<int>(mapping.sensors[0].fc));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(modbus_type_t::INT16),
                          static_cast<int>(mapping.sensors[0].type));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(register_source_t::MODBUS),
                          static_cast<int>(mapping.sensors[0].source));
    TEST_ASSERT_EQUAL_UINT16(1, mapping.sensors[0].register_count);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, mapping.sensors[0].multiplier);
    TEST_ASSERT_EQUAL_UINT16(12, mapping.sensors[0].bit_mask);
    TEST_ASSERT_EQUAL_UINT32(2, mapping.sensors[0].output_mapping.size());

    TEST_ASSERT_EQUAL_INT(static_cast<int>(function_code_t::READ_HR),
                          static_cast<int>(mapping.sensors[1].fc));
    TEST_ASSERT_EQUAL_UINT16(2, mapping.sensors[1].register_count);
    TEST_ASSERT_FALSE(mapping.sensors[1].big_endian);
}

void test_load_from_json_populates_actuators(void) {
    modbus_appliance_definition mapping;

    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));

    TEST_ASSERT_EQUAL_UINT32(2, mapping.actuators.size());
    TEST_ASSERT_EQUAL_STRING("SETPOINT_MAX_POWER_L1", mapping.actuators[0].mapping_id.c_str());
    TEST_ASSERT_EQUAL_INT(static_cast<int>(function_code_t::WRITE_SINGLE),
                          static_cast<int>(mapping.actuators[0].fc));
    TEST_ASSERT_EQUAL_UINT16(42020, mapping.actuators[0].start_address);
    TEST_ASSERT_EQUAL_UINT16(1, mapping.actuators[0].register_count);
    TEST_ASSERT_EQUAL_UINT16(255, mapping.actuators[0].bit_mask);
    TEST_ASSERT_EQUAL_UINT32(1, mapping.actuators[0].pre_sequence.size());
    TEST_ASSERT_EQUAL_UINT32(1, mapping.actuators[0].post_sequence.size());

    TEST_ASSERT_EQUAL_INT(static_cast<int>(function_code_t::WRITE_MULTI),
                          static_cast<int>(mapping.actuators[1].fc));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(modbus_type_t::UINT32),
                          static_cast<int>(mapping.actuators[1].type));
    TEST_ASSERT_EQUAL_UINT16(2, mapping.actuators[1].register_count);
}

void test_load_from_json_parses_bitmask_fields(void) {
    modbus_appliance_definition mapping;

    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));

    // Sensor bitMask is parsed; a sensor without bitMask defaults to 0.
    TEST_ASSERT_EQUAL_UINT16(12, mapping.sensors[0].bit_mask);
    TEST_ASSERT_EQUAL_UINT16(0, mapping.sensors[1].bit_mask);

    // Actuator bitMask is parsed; a second actuator without bitMask defaults to 0.
    TEST_ASSERT_EQUAL_UINT16(255, mapping.actuators[0].bit_mask);
    TEST_ASSERT_EQUAL_UINT16(0, mapping.actuators[1].bit_mask);

    // Sequence-step bitMask is parsed on the requiredPreSequence step.
    TEST_ASSERT_EQUAL_UINT16(3840, mapping.actuators[0].pre_sequence[0].bit_mask);

    // The requiredPostSequence step and the initialisation-sequence step have no
    // bitMask, so both default to 0.
    TEST_ASSERT_EQUAL_UINT16(0, mapping.actuators[0].post_sequence[0].bit_mask);
    TEST_ASSERT_EQUAL_UINT16(0, mapping.initialisation_sequence[0].bit_mask);
}

void test_load_from_json_clears_previous_state(void) {
    modbus_appliance_definition mapping;

    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));
    TEST_ASSERT_EQUAL_UINT32(2, mapping.sensors.size());

    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));
    TEST_ASSERT_EQUAL_UINT32(2, mapping.sensors.size());
    TEST_ASSERT_EQUAL_UINT32(2, mapping.actuators.size());
    TEST_ASSERT_EQUAL_UINT32(1, mapping.initialisation_sequence.size());
}

void test_load_from_json_rejects_invalid_json(void) {
    modbus_appliance_definition mapping;

    TEST_ASSERT_FALSE(mapping.load_from_json("{ not-json"));
    TEST_ASSERT_EQUAL_UINT32(0, mapping.sensors.size());
    TEST_ASSERT_EQUAL_UINT32(0, mapping.actuators.size());
}

void test_init_returns_false_when_storage_empty(void) {
    modbus_appliance_definition mapping;
    native_stubs::storage_template_json.clear();

    TEST_ASSERT_FALSE(mapping.init());
}

void test_init_parses_template_from_storage(void) {
    modbus_appliance_definition mapping;
    native_stubs::storage_template_json = kTemplateJson;

    TEST_ASSERT_TRUE(mapping.init());
    TEST_ASSERT_EQUAL_UINT32(2, mapping.sensors.size());
    TEST_ASSERT_EQUAL_UINT32(2, mapping.actuators.size());
}

void test_load_config_from_storage_returns_false_on_null(void) {
    TEST_ASSERT_FALSE(modbus_appliance_definition::load_config_from_storage(nullptr));
}

void test_load_config_from_storage_returns_false_when_empty(void) {
    appliance_config_t config;
    native_stubs::storage_template_json.clear();

    TEST_ASSERT_FALSE(modbus_appliance_definition::load_config_from_storage(&config));
}

void test_load_config_from_storage_parses_config(void) {
    appliance_config_t config;
    native_stubs::storage_template_json = kTemplateJson;

    TEST_ASSERT_TRUE(modbus_appliance_definition::load_config_from_storage(&config));
    TEST_ASSERT_EQUAL_STRING("Unit Test Device", config.name.c_str());
    TEST_ASSERT_EQUAL_INT(MODBUS_CONN_TYPE_RTU, config.interface_type);
}

void test_parse_config_from_json_returns_false_on_null(void) {
    TEST_ASSERT_FALSE(modbus_appliance_definition::parse_config_from_json(kTemplateJson, nullptr));
}

void test_parse_config_from_json_rejects_invalid(void) {
    appliance_config_t config;
    TEST_ASSERT_FALSE(modbus_appliance_definition::parse_config_from_json("{ broken", &config));
}

void test_get_actuator_type_known_and_unknown(void) {
    modbus_appliance_definition mapping;

    auto known = mapping.get_actuator_type("SETPOINT_MAX_POWER_L1");
    TEST_ASSERT_TRUE(known.has_value());
    TEST_ASSERT_EQUAL_INT(EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W, known.value());

    auto unknown = mapping.get_actuator_type("DOES_NOT_EXIST");
    TEST_ASSERT_FALSE(unknown.has_value());
}

void test_find_actuator_index_returns_indices(void) {
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));

    uint16_t index = 99;
    TEST_ASSERT_TRUE(mapping.find_actuator_index(EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W, &index));
    TEST_ASSERT_EQUAL_UINT16(0, index);

    TEST_ASSERT_TRUE(mapping.find_actuator_index(EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L1_W, &index));
    TEST_ASSERT_EQUAL_UINT16(1, index);

    TEST_ASSERT_FALSE(mapping.find_actuator_index(EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L2_W, &index));
    TEST_ASSERT_FALSE(mapping.find_actuator_index(EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W, nullptr));
}

void test_find_sensor_index_returns_indices(void) {
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));

    uint16_t index = 99;
    TEST_ASSERT_TRUE(mapping.find_sensor_index(EMS_BRIDGE_SENSOR_ACTUAL_POWER_TOTAL_W, &index));
    TEST_ASSERT_EQUAL_UINT16(0, index);

    TEST_ASSERT_TRUE(mapping.find_sensor_index(EMS_BRIDGE_SENSOR_ACTUAL_POWER_L1_W, &index));
    TEST_ASSERT_EQUAL_UINT16(1, index);

    TEST_ASSERT_FALSE(mapping.find_sensor_index(EMS_BRIDGE_SENSOR_ACTUAL_POWER_L2_W, &index));
    TEST_ASSERT_FALSE(mapping.find_sensor_index(EMS_BRIDGE_SENSOR_ACTUAL_POWER_L1_W, nullptr));
}

void test_save_to_json_returns_serialized_template(void) {
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));

    mapping.config.name = "Renamed Device";
    mapping.config.model = "M-9000";

    std::string out = mapping.save_to_json();
    TEST_ASSERT_FALSE(out.empty());
    TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "Renamed Device"));
    TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "M-9000"));
}

void test_save_to_json_on_empty_returns_serialized_config(void) {
    modbus_appliance_definition mapping;
    mapping.config.name = "Fresh";

    std::string out = mapping.save_to_json();
    TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "Fresh"));
}

static const char* kTemplateJsonModbusSourced = R"json(
{
  "appliance": {
    "spec": {
      "name": { "value": "Modbus Sourced Device" },
      "model": { "source": "MODBUS", "functionCode": "FC03", "startAddress": 100, "type": "STRING", "sourceSize": 64, "bigEndian": true },
      "serialNumber": { "source": "MODBUS", "functionCode": "FC04", "startAddress": 200, "type": "UINT16", "sourceSize": 16 },
      "firmwareVersion": { "source": "MODBUS", "functionCode": "FC03", "startAddress": 300, "type": "UINT32", "sourceSize": 32 }
    },
    "connection": {
      "iface": "TCP",
      "unitId": { "value": 3 },
      "eth": {
        "ipAddress": { "value": "10.0.0.1" },
        "port": { "value": 502 }
      }
    },
    "control": {
      "numberOfPhases": { "value": 1 },
      "instructionDelayS": { "value": 1 },
      "consequenceTypeDefer": { "value": true }
    }
  },
  "sensors": [],
  "actuators": []
}
)json";

void test_resolve_modbus_config_fields_success(void) {
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonModbusSourced));

    native_stubs::holding_register_value.success = true;
    native_stubs::holding_register_value.str = "MODEL-XYZ";
    native_stubs::holding_register_value.u32 = 12345;
    native_stubs::input_register_value.success = true;
    native_stubs::input_register_value.u16 = 999;

    TEST_ASSERT_TRUE(mapping.resolve_modbus_config_fields());
    TEST_ASSERT_EQUAL_STRING("MODEL-XYZ", mapping.config.model.c_str());
    TEST_ASSERT_EQUAL_STRING("999", mapping.config.serial_number.c_str());
    TEST_ASSERT_EQUAL_STRING("12345", mapping.config.firmware_version.c_str());
}

void test_resolve_modbus_config_fields_fails_when_read_fails(void) {
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonModbusSourced));

    native_stubs::holding_register_value.success = false;
    native_stubs::input_register_value.success = false;

    TEST_ASSERT_FALSE(mapping.resolve_modbus_config_fields());
}

void test_get_sensor_type_known_and_unknown(void) {
    modbus_appliance_definition mapping;

    auto known = mapping.get_sensor_type("ACTUAL_POWER_TOTAL");
    TEST_ASSERT_TRUE(known.has_value());
    TEST_ASSERT_EQUAL_INT(EMS_BRIDGE_SENSOR_ACTUAL_POWER_TOTAL_W, known.value());

    auto unknown = mapping.get_sensor_type("DOES_NOT_EXIST");
    TEST_ASSERT_FALSE(unknown.has_value());
}

void test_modbus_value_as_string_basic_types(void) {
    modbus_value_t v_bool = {modbus_value_type_t::BOOL, {}};
    v_bool.data.bool_value = true;
    TEST_ASSERT_EQUAL_STRING("true", modbus_value_as_string(v_bool).c_str());

    modbus_value_t v_bool_false = {modbus_value_type_t::BOOL, {}};
    v_bool_false.data.bool_value = false;
    TEST_ASSERT_EQUAL_STRING("false", modbus_value_as_string(v_bool_false).c_str());

    modbus_value_t v_i16 = {modbus_value_type_t::INT16, {}};
    v_i16.data.int16_value = -42;
    TEST_ASSERT_EQUAL_STRING("-42", modbus_value_as_string(v_i16).c_str());

    modbus_value_t v_u16 = {modbus_value_type_t::UINT16, {}};
    v_u16.data.uint16_value = 7;
    TEST_ASSERT_EQUAL_STRING("7", modbus_value_as_string(v_u16).c_str());

    modbus_value_t v_i32 = {modbus_value_type_t::INT32, {}};
    v_i32.data.int32_value = -100000;
    TEST_ASSERT_EQUAL_STRING("-100000", modbus_value_as_string(v_i32).c_str());

    modbus_value_t v_u32 = {modbus_value_type_t::UINT32, {}};
    v_u32.data.uint32_value = 12345;
    TEST_ASSERT_EQUAL_STRING("12345", modbus_value_as_string(v_u32).c_str());

    modbus_value_t v_f32 = {modbus_value_type_t::FLOAT32, {}};
    v_f32.data.float32_value = 3.5f;
    std::string f32_result = modbus_value_as_string(v_f32);
    TEST_ASSERT_NOT_NULL(strstr(f32_result.c_str(), "3.5"));

    modbus_value_t v_str = {modbus_value_type_t::STRING, {}};
    std::strcpy(v_str.data.string_value, "hello");
    TEST_ASSERT_EQUAL_STRING("hello", modbus_value_as_string(v_str).c_str());

    modbus_value_t v_ip = {modbus_value_type_t::IP_ADDR, {}};
    std::strcpy(v_ip.data.string_value, "10.0.0.1");
    TEST_ASSERT_EQUAL_STRING("10.0.0.1", modbus_value_as_string(v_ip).c_str());

    modbus_value_t v_regex = {modbus_value_type_t::REGEX, {}};
    std::strcpy(v_regex.data.string_value, "^[0-9]+$");
    TEST_ASSERT_EQUAL_STRING("^[0-9]+$", modbus_value_as_string(v_regex).c_str());

    modbus_value_t v_unknown = {modbus_value_type_t::UNKNOWN, {}};
    TEST_ASSERT_EQUAL_STRING("<unknown>", modbus_value_as_string(v_unknown).c_str());

    modbus_value_t v_none = {modbus_value_type_t::NONE, {}};
    TEST_ASSERT_EQUAL_STRING("<none>", modbus_value_as_string(v_none).c_str());
}

void test_modbus_value_as_number(void) {
    double output = 0;

    modbus_value_t v_i16 = {modbus_value_type_t::INT16, {}};
    v_i16.data.int16_value = -10;
    TEST_ASSERT_TRUE(modbus_value_as_number(v_i16, &output));
    TEST_ASSERT_FLOAT_WITHIN(0.001, -10.0, static_cast<float>(output));

    modbus_value_t v_u16 = {modbus_value_type_t::UINT16, {}};
    v_u16.data.uint16_value = 25;
    TEST_ASSERT_TRUE(modbus_value_as_number(v_u16, &output));
    TEST_ASSERT_FLOAT_WITHIN(0.001, 25.0, static_cast<float>(output));

    modbus_value_t v_i32 = {modbus_value_type_t::INT32, {}};
    v_i32.data.int32_value = -100000;
    TEST_ASSERT_TRUE(modbus_value_as_number(v_i32, &output));
    TEST_ASSERT_FLOAT_WITHIN(0.1, -100000.0, static_cast<float>(output));

    modbus_value_t v_u32 = {modbus_value_type_t::UINT32, {}};
    v_u32.data.uint32_value = 100000;
    TEST_ASSERT_TRUE(modbus_value_as_number(v_u32, &output));
    TEST_ASSERT_FLOAT_WITHIN(0.1, 100000.0, static_cast<float>(output));

    modbus_value_t v_f32 = {modbus_value_type_t::FLOAT32, {}};
    v_f32.data.float32_value = 1.5f;
    TEST_ASSERT_TRUE(modbus_value_as_number(v_f32, &output));
    TEST_ASSERT_FLOAT_WITHIN(0.001, 1.5, static_cast<float>(output));

    modbus_value_t v_bool = {modbus_value_type_t::BOOL, {}};
    v_bool.data.bool_value = true;
    TEST_ASSERT_TRUE(modbus_value_as_number(v_bool, &output));
    TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, static_cast<float>(output));

    modbus_value_t v_str = {modbus_value_type_t::STRING, {}};
    TEST_ASSERT_FALSE(modbus_value_as_number(v_str, &output));

    TEST_ASSERT_FALSE(modbus_value_as_number(v_i16, nullptr));
}

void test_modbus_value_as_scaled_uint16(void) {
    uint16_t output = 0;

    modbus_value_t v = {modbus_value_type_t::UINT16, {}};
    v.data.uint16_value = 100;
    TEST_ASSERT_TRUE(modbus_value_as_scaled_uint16(v, 2.0f, &output));
    TEST_ASSERT_EQUAL_UINT16(200, output);

    modbus_value_t v_str = {modbus_value_type_t::STRING, {}};
    TEST_ASSERT_FALSE(modbus_value_as_scaled_uint16(v_str, 1.0f, &output));

    TEST_ASSERT_FALSE(modbus_value_as_scaled_uint16(v, 1.0f, nullptr));
}

void test_modbus_value_as_scaled_uint32(void) {
    uint32_t output = 0;

    modbus_value_t v = {modbus_value_type_t::UINT32, {}};
    v.data.uint32_value = 1000;
    TEST_ASSERT_TRUE(modbus_value_as_scaled_uint32(v, 3.0f, &output));
    TEST_ASSERT_EQUAL_UINT32(3000, output);

    modbus_value_t v_str = {modbus_value_type_t::STRING, {}};
    TEST_ASSERT_FALSE(modbus_value_as_scaled_uint32(v_str, 1.0f, &output));

    TEST_ASSERT_FALSE(modbus_value_as_scaled_uint32(v, 1.0f, nullptr));
}

void test_modbus_value_from_float(void) {
    modbus_value_t u16 = modbus_value_from_float(modbus_value_type_t::UINT16, 42.7f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(modbus_value_type_t::UINT16), static_cast<int>(u16.type));
    TEST_ASSERT_EQUAL_UINT16(42, u16.data.uint16_value);

    modbus_value_t i16 = modbus_value_from_float(modbus_value_type_t::INT16, -42.7f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(modbus_value_type_t::INT16), static_cast<int>(i16.type));
    TEST_ASSERT_EQUAL_INT16(-42, i16.data.int16_value);

    modbus_value_t i32 = modbus_value_from_float(modbus_value_type_t::INT32, -100000.5f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(modbus_value_type_t::INT32), static_cast<int>(i32.type));
    TEST_ASSERT_EQUAL_INT32(-100000, i32.data.int32_value);

    modbus_value_t u32 = modbus_value_from_float(modbus_value_type_t::UINT32, 100000.9f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(modbus_value_type_t::UINT32), static_cast<int>(u32.type));
    TEST_ASSERT_EQUAL_UINT32(100000, u32.data.uint32_value);

    modbus_value_t f32 = modbus_value_from_float(modbus_value_type_t::FLOAT32, 1.5f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(modbus_value_type_t::FLOAT32), static_cast<int>(f32.type));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.5f, f32.data.float32_value);

    modbus_value_t bool_true = modbus_value_from_float(modbus_value_type_t::BOOL, 1.0f);
    TEST_ASSERT_TRUE(bool_true.data.bool_value);

    modbus_value_t bool_false = modbus_value_from_float(modbus_value_type_t::BOOL, 0.0f);
    TEST_ASSERT_FALSE(bool_false.data.bool_value);

    modbus_value_t unknown = modbus_value_from_float(modbus_value_type_t::STRING, 1.0f);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(modbus_value_type_t::UNKNOWN), static_cast<int>(unknown.type));
}

void test_load_from_json_skips_invalid_sensor_and_actuator(void) {
    modbus_appliance_definition mapping;
    const char* json = R"json(
{
  "appliance": { "spec": { "name": { "value": "X" } }, "connection": { "iface": "TCP" }, "control": {} },
  "sensors": [
    { "mappingId": "ACTUAL_POWER_TOTAL", "functionCode": "FC04", "type": "INT16", "startAddress": 1 },
    { "mappingId": "DOES_NOT_EXIST", "functionCode": "FC04", "type": "INT16", "startAddress": 2 }
  ],
  "actuators": [
    { "mappingId": "SETPOINT_MAX_POWER_L1", "functionCode": "FC06", "type": "UINT16", "startAddress": 3 },
    { "mappingId": "DOES_NOT_EXIST_EITHER", "functionCode": "FC06", "type": "UINT16", "startAddress": 4 }
  ]
}
)json";

    TEST_ASSERT_TRUE(mapping.load_from_json(json));
    TEST_ASSERT_EQUAL_UINT32(1, mapping.sensors.size());
    TEST_ASSERT_EQUAL_UINT32(1, mapping.actuators.size());
}

void test_load_from_json_parses_required_sensors_before_evaluation(void) {
    modbus_appliance_definition mapping;
    const char* json = R"json(
{
  "appliance": { "spec": {}, "connection": { "iface": "TCP" }, "control": {} },
  "sensors": [],
  "actuators": [
    {
      "mappingId": "SETPOINT_MAX_POWER_L1",
      "functionCode": "FC06",
      "type": "UINT16",
      "startAddress": 10,
      "requiredSensorsBeforeEvaluation": ["ACTUAL_POWER_TOTAL", "ACTUAL_POWER_L1"]
    }
  ]
}
)json";

    TEST_ASSERT_TRUE(mapping.load_from_json(json));
    TEST_ASSERT_EQUAL_UINT32(1, mapping.actuators.size());
    TEST_ASSERT_EQUAL_UINT32(2, mapping.actuators[0].required_sensors_before_evaluation.size());
    TEST_ASSERT_EQUAL_STRING("ACTUAL_POWER_TOTAL", mapping.actuators[0].required_sensors_before_evaluation[0].c_str());
}

void test_resolve_modbus_config_fields_uses_input_register_path(void) {
    modbus_appliance_definition mapping;
    const char* json = R"json(
{
  "appliance": {
    "spec": {
      "model": { "source": "MODBUS", "functionCode": "FC04", "startAddress": 100, "type": "STRING", "sourceSize": 64, "bigEndian": true },
      "serialNumber": { "source": "MODBUS", "functionCode": "FC04", "startAddress": 200, "type": "UINT32", "sourceSize": 32 },
      "firmwareVersion": { "source": "MODBUS", "functionCode": "FC04", "startAddress": 300, "type": "INT16", "sourceSize": 16 }
    },
    "connection": { "iface": "TCP" },
    "control": {}
  },
  "sensors": [],
  "actuators": []
}
)json";
    TEST_ASSERT_TRUE(mapping.load_from_json(json));

    native_stubs::input_register_value.success = true;
    native_stubs::input_register_value.str = "INPUT-STRING";
    native_stubs::input_register_value.u16 = 42;
    native_stubs::input_register_value.u32 = 99999;

    TEST_ASSERT_TRUE(mapping.resolve_modbus_config_fields());
    TEST_ASSERT_EQUAL_STRING("INPUT-STRING", mapping.config.model.c_str());
    TEST_ASSERT_EQUAL_STRING("99999", mapping.config.serial_number.c_str());
    TEST_ASSERT_EQUAL_STRING("42", mapping.config.firmware_version.c_str());
}

void test_resolve_modbus_config_fields_uses_int_paths(void) {
    modbus_appliance_definition mapping;
    const char* json = R"json(
{
  "appliance": {
    "spec": {
      "serialNumber": { "source": "MODBUS", "functionCode": "FC03", "startAddress": 100, "type": "INT16", "sourceSize": 16 },
      "firmwareVersion": { "source": "MODBUS", "functionCode": "FC03", "startAddress": 200, "type": "INT32", "sourceSize": 32 }
    },
    "connection": { "iface": "TCP" },
    "control": {}
  },
  "sensors": [],
  "actuators": []
}
)json";
    TEST_ASSERT_TRUE(mapping.load_from_json(json));

    native_stubs::holding_register_value.success = true;
    native_stubs::holding_register_value.u16 = 0xFFFE;
    native_stubs::holding_register_value.u32 = 0xFFFFFFFE;

    TEST_ASSERT_TRUE(mapping.resolve_modbus_config_fields());
    TEST_ASSERT_EQUAL_STRING("-2", mapping.config.serial_number.c_str());
    TEST_ASSERT_EQUAL_STRING("-2", mapping.config.firmware_version.c_str());
}

void test_parse_config_modbus_source_rejects_write_fc(void) {
    modbus_appliance_definition mapping;
    const char* json = R"json(
{
  "appliance": {
    "spec": {
      "model": { "source": "MODBUS", "functionCode": "FC06", "startAddress": 1, "type": "UINT16", "sourceSize": 16 }
    },
    "connection": { "iface": "TCP" },
    "control": {}
  },
  "sensors": [],
  "actuators": []
}
)json";
    TEST_ASSERT_TRUE(mapping.load_from_json(json));

    TEST_ASSERT_TRUE(mapping.resolve_modbus_config_fields());
    TEST_ASSERT_EQUAL_STRING("Unknown", mapping.config.model.c_str());
}

void test_save_to_json_handles_invalid_source_json(void) {
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJson));

    std::string out = mapping.save_to_json();
    TEST_ASSERT_FALSE(out.empty());
}

void test_load_from_json_preserves_register_count_fallback(void) {
    modbus_appliance_definition mapping;
    const char* json = R"json(
{
  "appliance": { "spec": {}, "connection": { "iface": "TCP" }, "control": {} },
  "sensors": [
    { "mappingId": "ACTUAL_POWER_TOTAL", "functionCode": "FC04", "type": "UINT32", "startAddress": 1 }
  ],
  "actuators": []
}
)json";
    TEST_ASSERT_TRUE(mapping.load_from_json(json));
    TEST_ASSERT_EQUAL_UINT16(2, mapping.sensors[0].register_count);
}

// ── apply_user_input_config_values (USER_INPUT NVS injection) ───────────────
//
// USER_INPUT fields in the template are filled from NVS during parse_config().
// These tests drive that path via load_from_json() and verify the resolved
// config picks up the seeded NVS value, the JSON fallback chain, or the
// hardcoded fallback in the expected order.

static const char* kTemplateJsonUserInput = R"json(
{
  "appliance": {
    "spec": {
      "name":             { "source": "USER_INPUT", "value": "TmplName" },
      "model":            { "source": "USER_INPUT", "defaultValue": "TmplModel" },
      "serialNumber":     { "source": "USER_INPUT" },
      "firmwareVersion":  { "source": "USER_INPUT", "default": "TmplFw" }
    },
    "connection": {
      "iface": "TCP",
      "unitId": { "source": "USER_INPUT", "defaultValue": "9" },
      "eth": {
        "ipAddress": { "source": "USER_INPUT", "value": "10.0.0.99" },
        "port":      { "source": "USER_INPUT", "defaultValue": "1234" }
      }
    },
    "control": {
      "numberOfPhases":    { "source": "USER_INPUT", "defaultValue": 2 },
      "instructionDelayS": { "value": 0 },
      "consequenceTypeDefer": { "value": true }
    }
  },
  "sensors": [],
  "actuators": []
}
)json";

// Template where each USER_INPUT-able field is a plain "value" (no source),
// so apply_user_input_config_values must leave them alone even when NVS keys
// are seeded.
static const char* kTemplateJsonNoUserInput = R"json(
{
  "appliance": {
    "spec": {
      "name":  { "value": "Static Name" },
      "model": { "value": "Static Model" }
    },
    "connection": {
      "iface": "TCP",
      "unitId": { "value": 7 },
      "eth": {
        "ipAddress": { "value": "192.168.1.1" },
        "port":      { "value": 502 }
      }
    },
    "control": {
      "numberOfPhases": { "value": 3 }
    }
  },
  "sensors": [],
  "actuators": []
}
)json";

void test_user_input_string_uses_nvs_when_present(void) {
    native_stubs::nvs_string_values[NVS_STORAGE_KEY_APPLIANCE_NAME] = "NvsName";
    native_stubs::nvs_string_values[NVS_STORAGE_KEY_APPLIANCE_MODEL] = "NvsModel";
    native_stubs::nvs_string_values[NVS_STORAGE_KEY_MODBUS_ETH_IP] = "172.16.0.5";

    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonUserInput));

    // NVS-seeded values overwrite the template "value" / "defaultValue".
    TEST_ASSERT_EQUAL_STRING("NvsName",   mapping.config.name.c_str());
    TEST_ASSERT_EQUAL_STRING("NvsModel",  mapping.config.model.c_str());
    TEST_ASSERT_EQUAL_STRING("172.16.0.5", mapping.config.ip_address.c_str());
}

void test_user_input_string_falls_back_to_template_value_when_nvs_empty(void) {
    // No NVS values seeded — stub returns the caller-supplied default (which
    // apply_user_input_string takes from the template's value/defaultValue/
    // default chain).
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonUserInput));

    // "value" present in template.
    TEST_ASSERT_EQUAL_STRING("TmplName", mapping.config.name.c_str());
    // "defaultValue" wins when no "value".
    TEST_ASSERT_EQUAL_STRING("TmplModel", mapping.config.model.c_str());
    // "default" (legacy spelling) wins when neither value nor defaultValue.
    TEST_ASSERT_EQUAL_STRING("TmplFw", mapping.config.firmware_version.c_str());
}

void test_user_input_string_falls_back_to_hardcoded_unknown(void) {
    // serialNumber has only `source: USER_INPUT`, no value/defaultValue/default.
    // The hardcoded fallback in apply_user_input_string is "Unknown".
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonUserInput));

    TEST_ASSERT_EQUAL_STRING("Unknown", mapping.config.serial_number.c_str());
}

void test_user_input_ip_falls_back_to_hardcoded_default(void) {
    // Template that has USER_INPUT on ipAddress with no value/defaultValue.
    // Hardcoded fallback is MODBUS_DEVICE_IP (see definitions.h).
    const char* json = R"json(
{
  "appliance": {
    "spec": {},
    "connection": {
      "iface": "TCP",
      "eth": { "ipAddress": { "source": "USER_INPUT" } }
    },
    "control": {}
  },
  "sensors": [],
  "actuators": []
}
)json";
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(json));

    TEST_ASSERT_EQUAL_STRING(MODBUS_DEVICE_IP, mapping.config.ip_address.c_str());
}

void test_non_user_input_field_ignores_seeded_nvs(void) {
    // The seeded NVS value MUST NOT overwrite a non-USER_INPUT template value.
    native_stubs::nvs_string_values[NVS_STORAGE_KEY_APPLIANCE_NAME] = "ShouldBeIgnored";
    native_stubs::nvs_string_values[NVS_STORAGE_KEY_MODBUS_ETH_IP] = "9.9.9.9";

    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonNoUserInput));

    TEST_ASSERT_EQUAL_STRING("Static Name",  mapping.config.name.c_str());
    TEST_ASSERT_EQUAL_STRING("192.168.1.1",  mapping.config.ip_address.c_str());
}

void test_user_input_u16_string_uses_nvs_value(void) {
    // unitId / eth.port are stored as strings in NVS (set via the captive
    // portal text input) and parsed to uint16 by apply_user_input_u16_string.
    native_stubs::nvs_string_values[NVS_STORAGE_KEY_MODBUS_UNIT_ID] = "42";
    native_stubs::nvs_string_values[NVS_STORAGE_KEY_MODBUS_ETH_PORT] = "8502";

    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonUserInput));

    TEST_ASSERT_EQUAL_UINT16(42,   mapping.config.unit_id);
    TEST_ASSERT_EQUAL_UINT16(8502, mapping.config.port);
}

void test_user_input_u16_string_falls_back_when_nvs_empty(void) {
    // NVS empty → parse_u16_or_default("", ...) returns the fallback derived
    // from the template's defaultValue chain. The template here uses string
    // "9"/"1234" defaultValues, which ArduinoJson's `| uint16_t` does not
    // coerce — so the fallback chain drops to the hardcoded MODBUS_DEVICE_*.
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonUserInput));

    TEST_ASSERT_EQUAL_UINT16(MODBUS_DEVICE_UID, mapping.config.unit_id);
    TEST_ASSERT_EQUAL_UINT16(MODBUS_DEVICE_PORT,  mapping.config.port);
}

void test_user_input_u16_string_uses_numeric_default_value(void) {
    // When defaultValue is a JSON number (not a string), config_field_default_u16
    // does pick it up — confirming the JSON-fallback path actually works for
    // u16 fields when the template author used a number literal.
    const char* json = R"json(
{
  "appliance": {
    "spec": {},
    "connection": {
      "iface": "TCP",
      "unitId": { "source": "USER_INPUT", "defaultValue": 11 },
      "eth": {
        "port": { "source": "USER_INPUT", "defaultValue": 8888 }
      }
    },
    "control": {}
  },
  "sensors": [],
  "actuators": []
}
)json";
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(json));

    TEST_ASSERT_EQUAL_UINT16(11,   mapping.config.unit_id);
    TEST_ASSERT_EQUAL_UINT16(8888, mapping.config.port);
}

void test_user_input_u16_string_falls_back_on_invalid_nvs(void) {
    // Non-numeric and out-of-range NVS values must be rejected by
    // parse_u16_or_default and fall through to the same fallback chain
    // as an empty NVS string.
    native_stubs::nvs_string_values[NVS_STORAGE_KEY_MODBUS_UNIT_ID] = "not-a-number";
    native_stubs::nvs_string_values[NVS_STORAGE_KEY_MODBUS_ETH_PORT] = "70000";

    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonUserInput));

    TEST_ASSERT_EQUAL_UINT16(MODBUS_DEVICE_UID, mapping.config.unit_id);
    TEST_ASSERT_EQUAL_UINT16(MODBUS_DEVICE_PORT,  mapping.config.port);
}

void test_user_input_u16_numeric_uses_nvs_value(void) {
    // numberOfPhases is read from NVS as a real u16 (not a string) by
    // apply_user_input_u16, so it needs a seeded u16 NVS value.
    native_stubs::nvs_u16_values[NVS_STORAGE_KEY_NUM_PHASES] = 3;

    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonUserInput));

    TEST_ASSERT_EQUAL_UINT16(3, mapping.config.num_phases);
}

void test_user_input_u16_numeric_falls_back_to_template_default(void) {
    // No u16 seed → the stub returns default_value passed by the caller,
    // which apply_user_input_u16 derives from the template's defaultValue (2).
    modbus_appliance_definition mapping;
    TEST_ASSERT_TRUE(mapping.load_from_json(kTemplateJsonUserInput));

    TEST_ASSERT_EQUAL_UINT16(2, mapping.config.num_phases);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_load_from_json_populates_basic_config);
    RUN_TEST(test_load_from_json_populates_initialisation_sequence);
    RUN_TEST(test_load_from_json_populates_sensors);
    RUN_TEST(test_load_from_json_populates_actuators);
    RUN_TEST(test_load_from_json_parses_bitmask_fields);
    RUN_TEST(test_load_from_json_clears_previous_state);
    RUN_TEST(test_load_from_json_rejects_invalid_json);
    RUN_TEST(test_init_returns_false_when_storage_empty);
    RUN_TEST(test_init_parses_template_from_storage);
    RUN_TEST(test_load_config_from_storage_returns_false_on_null);
    RUN_TEST(test_load_config_from_storage_returns_false_when_empty);
    RUN_TEST(test_load_config_from_storage_parses_config);
    RUN_TEST(test_parse_config_from_json_returns_false_on_null);
    RUN_TEST(test_parse_config_from_json_rejects_invalid);
    RUN_TEST(test_get_actuator_type_known_and_unknown);
    RUN_TEST(test_find_actuator_index_returns_indices);
    RUN_TEST(test_find_sensor_index_returns_indices);
    RUN_TEST(test_save_to_json_returns_serialized_template);
    RUN_TEST(test_save_to_json_on_empty_returns_serialized_config);
    RUN_TEST(test_resolve_modbus_config_fields_success);
    RUN_TEST(test_resolve_modbus_config_fields_fails_when_read_fails);
    RUN_TEST(test_get_sensor_type_known_and_unknown);
    RUN_TEST(test_modbus_value_as_string_basic_types);
    RUN_TEST(test_modbus_value_as_number);
    RUN_TEST(test_modbus_value_as_scaled_uint16);
    RUN_TEST(test_modbus_value_as_scaled_uint32);
    RUN_TEST(test_modbus_value_from_float);
    RUN_TEST(test_load_from_json_skips_invalid_sensor_and_actuator);
    RUN_TEST(test_load_from_json_parses_required_sensors_before_evaluation);
    RUN_TEST(test_resolve_modbus_config_fields_uses_input_register_path);
    RUN_TEST(test_resolve_modbus_config_fields_uses_int_paths);
    RUN_TEST(test_parse_config_modbus_source_rejects_write_fc);
    RUN_TEST(test_save_to_json_handles_invalid_source_json);
    RUN_TEST(test_load_from_json_preserves_register_count_fallback);
    RUN_TEST(test_user_input_string_uses_nvs_when_present);
    RUN_TEST(test_user_input_string_falls_back_to_template_value_when_nvs_empty);
    RUN_TEST(test_user_input_string_falls_back_to_hardcoded_unknown);
    RUN_TEST(test_user_input_ip_falls_back_to_hardcoded_default);
    RUN_TEST(test_non_user_input_field_ignores_seeded_nvs);
    RUN_TEST(test_user_input_u16_string_uses_nvs_value);
    RUN_TEST(test_user_input_u16_string_falls_back_when_nvs_empty);
    RUN_TEST(test_user_input_u16_string_uses_numeric_default_value);
    RUN_TEST(test_user_input_u16_string_falls_back_on_invalid_nvs);
    RUN_TEST(test_user_input_u16_numeric_uses_nvs_value);
    RUN_TEST(test_user_input_u16_numeric_falls_back_to_template_default);
    return UNITY_END();
}
