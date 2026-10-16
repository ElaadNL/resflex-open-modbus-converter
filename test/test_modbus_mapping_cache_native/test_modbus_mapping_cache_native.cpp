// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <modbus_mapping_cache.h>

// Exercises the sensor value cache used by mapping_request_preprocessing(). The
// cache is time-source agnostic: every record()/get() call takes an explicit
// monotonic millisecond timestamp, so these tests drive time by hand and never
// touch esp_timer.

void setUp(void) {
    modbus_mapping_cache_clear();
}

void tearDown(void) {
}

void test_cache_get_returns_false_when_empty(void) {
    float value = -1.0f;
    TEST_ASSERT_FALSE(modbus_mapping_cache_get(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, &value, 1000));
}

void test_cache_returns_value_within_validity_window(void) {
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 230.0f, 5000, 1000);

    float value = 0.0f;
    TEST_ASSERT_TRUE(modbus_mapping_cache_get(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, &value, 4000));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 230.0f, value);
}

void test_cache_value_is_valid_exactly_at_window_edge(void) {
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 230.0f, 5000, 1000);

    // now - timestamp == validity_duration is still within the window.
    float value = 0.0f;
    TEST_ASSERT_TRUE(modbus_mapping_cache_get(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, &value, 6000));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 230.0f, value);
}

void test_cache_expires_after_validity_window(void) {
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 230.0f, 5000, 1000);

    float value = 0.0f;
    TEST_ASSERT_FALSE(modbus_mapping_cache_get(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, &value, 6001));
}

void test_cache_validity_zero_is_never_valid(void) {
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 230.0f, 0, 1000);

    float value = 0.0f;
    TEST_ASSERT_FALSE(modbus_mapping_cache_get(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, &value, 1000));
}

void test_cache_record_overwrites_previous_entry(void) {
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 230.0f, 5000, 1000);
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 231.5f, 5000, 2000);

    float value = 0.0f;
    TEST_ASSERT_TRUE(modbus_mapping_cache_get(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, &value, 3000));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 231.5f, value);
}

void test_cache_keeps_entries_per_sensor(void) {
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 230.0f, 5000, 1000);
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L2_V, 400.0f, 5000, 1000);

    float value_l1 = 0.0f;
    float value_l2 = 0.0f;
    TEST_ASSERT_TRUE(modbus_mapping_cache_get(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, &value_l1, 2000));
    TEST_ASSERT_TRUE(modbus_mapping_cache_get(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L2_V, &value_l2, 2000));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 230.0f, value_l1);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 400.0f, value_l2);
}

void test_cache_clear_removes_all_entries(void) {
    modbus_mapping_cache_record(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, 230.0f, 5000, 1000);
    modbus_mapping_cache_clear();

    float value = 0.0f;
    TEST_ASSERT_FALSE(modbus_mapping_cache_get(EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V, &value, 2000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cache_get_returns_false_when_empty);
    RUN_TEST(test_cache_returns_value_within_validity_window);
    RUN_TEST(test_cache_value_is_valid_exactly_at_window_edge);
    RUN_TEST(test_cache_expires_after_validity_window);
    RUN_TEST(test_cache_validity_zero_is_never_valid);
    RUN_TEST(test_cache_record_overwrites_previous_entry);
    RUN_TEST(test_cache_keeps_entries_per_sensor);
    RUN_TEST(test_cache_clear_removes_all_entries);
    return UNITY_END();
}
