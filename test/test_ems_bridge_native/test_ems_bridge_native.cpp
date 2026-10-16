// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <cstring>

#include "ems_bridge.h"
#include "esp_err.h"
#include "native_test_state.h"

static int sensor_cb_calls = 0;
static int actuator_cb_calls = 0;
static int init_ready_cb_calls = 0;

static void test_sensor_cb(ems_bridge_sensor_t) { sensor_cb_calls++; }
static void test_actuator_cb(ems_bridge_set_actuator_request) { actuator_cb_calls++; }
static void test_init_ready_cb() { init_ready_cb_calls++; }

static ems_bridge_setup_values make_appliance_config() {
    ems_bridge_setup_values cfg = {};
    std::strncpy(cfg.brand, "example", sizeof(cfg.brand) - 1);
    std::strncpy(cfg.model, "rev1", sizeof(cfg.model) - 1);
    std::strncpy(cfg.user_defined_name, "driveway charger", sizeof(cfg.user_defined_name) - 1);
    cfg.control.roles[0] = EMS_BRIDGE_ROLE_CONSUMER;
    cfg.control.roles[1] = EMS_BRIDGE_ROLE_SENTINEL;
    cfg.control.instruction_processing_delay_ms = 1000;
    cfg.control.phase_count = 1;
    cfg.control.power_constraint_defer = false;
    return cfg;
}

void setUp(void) {
    native_stubs::reset();
    sensor_cb_calls = 0;
    actuator_cb_calls = 0;
    init_ready_cb_calls = 0;
}

void tearDown(void) {}

void test_ems_bridge_init_calls_s2_init_for_pebc(void) {
    ems_bridge_setup_values cfg = make_appliance_config();

    bool ok = ems_bridge_init(EMS_INTERFACE_S2_PEBC, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_INT(1, native_stubs::s2_init_state.call_count);
    TEST_ASSERT_EQUAL_STRING("example", native_stubs::s2_init_state.last_appliance_config.brand);
    TEST_ASSERT_EQUAL_UINT8(1, native_stubs::s2_init_state.last_appliance_config.control.phase_count);
}

void test_ems_bridge_init_passes_base_url_and_token(void) {
    ems_bridge_setup_values cfg = make_appliance_config();

    ems_bridge_init(EMS_INTERFACE_S2_PEBC, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_NOT_NULL(strstr(native_stubs::s2_init_state.last_base_url.c_str(), "s2_pebc"));
    TEST_ASSERT_FALSE(native_stubs::s2_init_state.last_pairing_token.empty());
}

void test_ems_bridge_init_uses_wifi_interface_when_available(void) {
    native_stubs::eth_wifi_state.interface_name_available = true;
    native_stubs::eth_wifi_state.interface_name = "wlan0";

    ems_bridge_setup_values cfg = make_appliance_config();
    ems_bridge_init(EMS_INTERFACE_S2_PEBC, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_EQUAL_STRING("wlan0", native_stubs::s2_init_state.last_interface_name.c_str());
}

void test_ems_bridge_init_without_wifi_interface_still_calls_s2(void) {
    native_stubs::eth_wifi_state.interface_name_available = false;

    ems_bridge_setup_values cfg = make_appliance_config();
    ems_bridge_init(EMS_INTERFACE_S2_PEBC, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_EQUAL_INT(1, native_stubs::s2_init_state.call_count);
    TEST_ASSERT_TRUE(native_stubs::s2_init_state.last_interface_name.empty());
}

void test_ems_bridge_init_propagates_s2_init_failure(void) {
    native_stubs::s2_init_state.return_value = ESP_FAIL;

    ems_bridge_setup_values cfg = make_appliance_config();
    bool ok = ems_bridge_init(EMS_INTERFACE_S2_PEBC, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    // s2_init failing must make ems_bridge_init report failure to the caller.
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_EQUAL_INT(1, native_stubs::s2_init_state.call_count);
}

void test_ems_bridge_send_new_sensor_value_succeeds(void) {
    ems_bridge_setup_values cfg = make_appliance_config();
    ems_bridge_init(EMS_INTERFACE_S2_PEBC, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    ems_bridge_sensor_result result = {};
    result.sensorId = EMS_BRIDGE_SENSOR_ACTUAL_POWER_TOTAL_W;
    result.status = EMS_BRIDGE_STATUS_OK;
    result.value = 1234.5f;

    TEST_ASSERT_TRUE(ems_bridge_send_new_sensor_value(&result));
}

void test_ems_bridge_send_new_sensor_value_fails_when_queue_full(void) {
    ems_bridge_setup_values cfg = make_appliance_config();
    ems_bridge_init(EMS_INTERFACE_S2_PEBC, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    ems_bridge_sensor_result result = {};
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(ems_bridge_send_new_sensor_value(&result));
    }

    TEST_ASSERT_FALSE(ems_bridge_send_new_sensor_value(&result));
}

void test_ems_bridge_send_set_actuator_result_succeeds(void) {
    ems_bridge_setup_values cfg = make_appliance_config();
    ems_bridge_init(EMS_INTERFACE_S2_PEBC, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    ems_bridge_set_actuator_result result = {};
    result.actuatorId = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W;
    result.status = EMS_BRIDGE_STATUS_OK;

    TEST_ASSERT_TRUE(ems_bridge_send_set_actuator_result(&result));
}

void test_ems_bridge_send_set_actuator_result_fails_when_queue_full(void) {
    ems_bridge_setup_values cfg = make_appliance_config();
    ems_bridge_init(EMS_INTERFACE_S2_PEBC, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    ems_bridge_set_actuator_result result = {};
    for (int i = 0; i < 10; i++) {
        TEST_ASSERT_TRUE(ems_bridge_send_set_actuator_result(&result));
    }

    TEST_ASSERT_FALSE(ems_bridge_send_set_actuator_result(&result));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ems_bridge_init_calls_s2_init_for_pebc);
    RUN_TEST(test_ems_bridge_init_passes_base_url_and_token);
    RUN_TEST(test_ems_bridge_init_uses_wifi_interface_when_available);
    RUN_TEST(test_ems_bridge_init_without_wifi_interface_still_calls_s2);
    RUN_TEST(test_ems_bridge_init_propagates_s2_init_failure);
    RUN_TEST(test_ems_bridge_send_new_sensor_value_succeeds);
    RUN_TEST(test_ems_bridge_send_new_sensor_value_fails_when_queue_full);
    RUN_TEST(test_ems_bridge_send_set_actuator_result_succeeds);
    RUN_TEST(test_ems_bridge_send_set_actuator_result_fails_when_queue_full);
    return UNITY_END();
}
