// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <ems_bridge.h>
#include <message_formats.h>
#include <modbus_mapping_component.h>
#include <shared_queue.hpp>

#include "freertos/queue.h"
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
      "iface": "TCP",
      "unitId": { "value": 7 },
      "eth": {
        "ipAddress": { "value": "192.168.4.20" },
        "port": { "value": 1502 }
      }
    },
    "control": {
      "numberOfPhases": { "value": 1 },
      "instructionDelayS": { "value": 2 },
      "consequenceTypeDefer": { "value": false }
    }
  },
  "initialisationSequence": [],
  "sensors": [
    {
      "mappingId": "ACTUAL_POWER_TOTAL",
      "source": "MODBUS",
      "functionCode": "FC04",
      "type": "INT16",
      "startAddress": 30006,
      "sourceSize": 16,
      "multiplier": 0.5,
      "bigEndian": true
    },
    {
      "mappingId": "ACTUAL_POWER_L1",
      "source": "MODBUS",
      "functionCode": "FC03",
      "type": "UINT32",
      "startAddress": 40010,
      "sourceSize": 32,
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
      "destinationSize": 16
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

static void reset_mapping_queue() {
    if (modbus_mapping_queue != nullptr) {
        vQueueDelete(modbus_mapping_queue);
    }

    modbus_mapping_queue = xQueueCreate(QUEUE_SIZE, sizeof(message_definition_t));
    TEST_ASSERT_NOT_NULL(modbus_mapping_queue);
}

void setUp(void) {
    native_stubs::reset();
    reset_mapping_queue();
    TEST_ASSERT_TRUE(modbus_mapping_get_definition().load_from_json(kTemplateJson));
}

void tearDown(void) {
    if (modbus_mapping_queue != nullptr) {
        vQueueDelete(modbus_mapping_queue);
        modbus_mapping_queue = nullptr;
    }
}

void test_actuator_callback_queues_known_actuator_request(void) {
    message_definition_t received_message = {};
    ems_bridge_set_actuator_request request = {};
    request.actuatorId = EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L1_W;
    request.value = 123.0f;

    ems_bridge_set_actuator_implementation(request);

    TEST_ASSERT_EQUAL(pdPASS, xQueueReceive(modbus_mapping_queue, &received_message, 0));
    TEST_ASSERT_EQUAL(MESSAGE_TYPE_MODBUS_ACTUATOR, received_message.type);
    TEST_ASSERT_EQUAL_UINT16(1, received_message.data.modbus_actuator.actuator_index);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(modbus_value_type_t::UINT32),
                          static_cast<int>(received_message.data.modbus_actuator.value.type));
    TEST_ASSERT_EQUAL_UINT32(123, received_message.data.modbus_actuator.value.data.uint32_value);
}

void test_actuator_callback_ignores_unknown_actuator(void) {
    message_definition_t received_message = {};
    ems_bridge_set_actuator_request request = {};
    request.actuatorId = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L2_W;
    request.value = 10.0f;

    ems_bridge_set_actuator_implementation(request);

    TEST_ASSERT_NOT_EQUAL(pdPASS, xQueueReceive(modbus_mapping_queue, &received_message, 0));
}

void test_sensor_callback_queues_known_sensor_request(void) {
    message_definition_t received_message = {};

    ems_bridge_request_sensor_implementation(EMS_BRIDGE_SENSOR_ACTUAL_POWER_L1_W);

    TEST_ASSERT_EQUAL(pdPASS, xQueueReceive(modbus_mapping_queue, &received_message, 0));
    TEST_ASSERT_EQUAL(MESSAGE_TYPE_MODBUS_SENSOR, received_message.type);
    TEST_ASSERT_EQUAL_UINT16(1, received_message.data.modbus_sensor.sensor_index);
}

void test_sensor_callback_ignores_unknown_sensor(void) {
    message_definition_t received_message = {};

    ems_bridge_request_sensor_implementation(EMS_BRIDGE_SENSOR_ACTUAL_POWER_L2_W);

    TEST_ASSERT_NOT_EQUAL(pdPASS, xQueueReceive(modbus_mapping_queue, &received_message, 0));
}

void test_template_still_covers_modbus_mapping_indexes(void) {
    uint16_t index = 99;

    TEST_ASSERT_TRUE(modbus_mapping_get_definition().find_actuator_index(
        EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W,
        &index
    ));
    TEST_ASSERT_EQUAL_UINT16(0, index);

    TEST_ASSERT_TRUE(modbus_mapping_get_definition().find_sensor_index(
        EMS_BRIDGE_SENSOR_ACTUAL_POWER_TOTAL_W,
        &index
    ));
    TEST_ASSERT_EQUAL_UINT16(0, index);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_actuator_callback_queues_known_actuator_request);
    RUN_TEST(test_actuator_callback_ignores_unknown_actuator);
    RUN_TEST(test_sensor_callback_queues_known_sensor_request);
    RUN_TEST(test_sensor_callback_ignores_unknown_sensor);
    RUN_TEST(test_template_still_covers_modbus_mapping_indexes);
    return UNITY_END();
}
