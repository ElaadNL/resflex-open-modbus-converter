// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <cstring>

#include "s2_library_wrapper.h"
#include "s2_client.h"
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
    std::strncpy(cfg.type, "ev_charger", sizeof(cfg.type) - 1);
    std::strncpy(cfg.model, "rev1", sizeof(cfg.model) - 1);
    std::strncpy(cfg.user_defined_name, "test", sizeof(cfg.user_defined_name) - 1);
    cfg.control.roles[0] = EMS_BRIDGE_ROLE_CONSUMER;
    cfg.control.roles[1] = EMS_BRIDGE_ROLE_PRODUCER;
    cfg.control.roles[2] = EMS_BRIDGE_ROLE_SENTINEL;
    cfg.control.instruction_processing_delay_ms = 1000;
    cfg.control.phase_count = 3;
    cfg.control.power_constraint_defer = false;
    for (int i = 0; i < 3; i++) {
        cfg.control.default_limits[i].power_upper_limit = 1000.0f * (i + 1);
        cfg.control.default_limits[i].power_lower_limit = 0.0f;
        cfg.control.default_limits[i].energy_upper_limit = 5000.0f;
        cfg.control.default_limits[i].energy_lower_limit = 0.0f;
    }
    return cfg;
}

static s2_hems_values make_hems_values(const char* pairing_token) {
    s2_hems_values v = {};
    std::strncpy(v.base_url, "http://test.local/api/s2_pebc", sizeof(v.base_url) - 1);
    std::strncpy(v.interface, "wlan0", sizeof(v.interface) - 1);
    if (pairing_token != nullptr) {
        std::strncpy(v.pairing_input_token, pairing_token, sizeof(v.pairing_input_token) - 1);
    }
    return v;
}

void setUp(void) {
    native_stubs::reset();
    sensor_cb_calls = 0;
    actuator_cb_calls = 0;
    init_ready_cb_calls = 0;
}

void tearDown(void) {}

void test_s2_init_succeeds_with_existing_access_token(void) {
    native_stubs::nvs_s2_access_token = "existing-token";
    native_stubs::nvs_s2_access_token_present = true;
    native_stubs::s2_client_state.initiate_connection_response_access_token = "rotated-token";
    native_stubs::s2_client_state.initiate_connection_response_communication_token = "comm";
    native_stubs::s2_client_state.initiate_connection_response_url = "ws://x";

    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values(nullptr);

    esp_err_t ret = s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_EQUAL_INT(ESP_OK, ret);
    TEST_ASSERT_EQUAL_INT(1, native_stubs::s2_client_state.init_call_count);
    TEST_ASSERT_EQUAL_INT(0, native_stubs::s2_client_state.prepare_pairing_call_count);
    TEST_ASSERT_EQUAL_INT(0, native_stubs::s2_client_state.request_pairing_call_count);
    TEST_ASSERT_EQUAL_INT(1, native_stubs::s2_client_state.initiate_connection_call_count);
    TEST_ASSERT_EQUAL_INT(1, native_stubs::s2_client_state.ws_connect_call_count);
    TEST_ASSERT_EQUAL_STRING("rotated-token", native_stubs::nvs_s2_access_token.c_str());
}

void test_s2_init_fails_when_no_token_and_no_pairing_token(void) {
    native_stubs::nvs_s2_access_token_present = false;

    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values(nullptr);

    esp_err_t ret = s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_INT(0, native_stubs::s2_client_state.prepare_pairing_call_count);
    TEST_ASSERT_EQUAL_INT(0, native_stubs::s2_client_state.initiate_connection_call_count);
}

void test_s2_init_does_pairing_when_no_token(void) {
    native_stubs::nvs_s2_access_token_present = false;
    native_stubs::s2_client_state.request_pairing_response_token = "new-paired-token";
    native_stubs::s2_client_state.initiate_connection_response_access_token = "rotated";
    native_stubs::s2_client_state.initiate_connection_response_communication_token = "comm";
    native_stubs::s2_client_state.initiate_connection_response_url = "ws://x";

    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values("pairing-input-token");

    esp_err_t ret = s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_EQUAL_INT(ESP_OK, ret);
    TEST_ASSERT_EQUAL_INT(1, native_stubs::s2_client_state.prepare_pairing_call_count);
    TEST_ASSERT_EQUAL_INT(1, native_stubs::s2_client_state.request_pairing_call_count);
    TEST_ASSERT_EQUAL_INT(1, native_stubs::s2_client_state.initiate_connection_call_count);
    TEST_ASSERT_EQUAL_INT(1, native_stubs::s2_client_state.ws_connect_call_count);
}

void test_s2_init_fails_when_s2_client_init_returns_null(void) {
    native_stubs::s2_client_state.init_returns_null = true;

    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values("pairing-input");

    esp_err_t ret = s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    TEST_ASSERT_EQUAL_INT(0, native_stubs::s2_client_state.prepare_pairing_call_count);
}

void test_s2_init_fails_when_prepare_pairing_fails(void) {
    native_stubs::nvs_s2_access_token_present = false;
    native_stubs::s2_client_state.prepare_pairing_return = ESP_FAIL;

    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values("pairing-input");

    esp_err_t ret = s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
}

void test_s2_init_fails_when_request_pairing_fails(void) {
    native_stubs::nvs_s2_access_token_present = false;
    native_stubs::s2_client_state.request_pairing_return = ESP_FAIL;

    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values("pairing-input");

    esp_err_t ret = s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
}

void test_s2_init_fails_when_initiate_connection_fails(void) {
    native_stubs::nvs_s2_access_token = "stored-token";
    native_stubs::nvs_s2_access_token_present = true;
    native_stubs::s2_client_state.initiate_connection_return = ESP_FAIL;

    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values(nullptr);

    esp_err_t ret = s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
    // After failed initiateConnection the token should have been wiped.
    TEST_ASSERT_FALSE(native_stubs::nvs_s2_access_token_present);
}

void test_s2_init_fails_when_ws_connect_fails(void) {
    native_stubs::nvs_s2_access_token = "stored";
    native_stubs::nvs_s2_access_token_present = true;
    native_stubs::s2_client_state.initiate_connection_response_access_token = "rotated";
    native_stubs::s2_client_state.initiate_connection_response_communication_token = "comm";
    native_stubs::s2_client_state.initiate_connection_response_url = "ws://x";
    native_stubs::s2_client_state.ws_connect_return = ESP_FAIL;

    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values(nullptr);

    esp_err_t ret = s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    TEST_ASSERT_NOT_EQUAL(ESP_OK, ret);
}

void test_s2_on_new_sensor_value_ignored_when_status_not_ok(void) {
    ems_bridge_sensor_result r = {};
    r.sensorId = EMS_BRIDGE_SENSOR_ACTUAL_POWER_TOTAL_W;
    r.status = EMS_BRIDGE_STATUS_ERR;
    r.value = 100.0f;

    s2_on_new_sensor_value(r);

    TEST_ASSERT_EQUAL_INT(0, native_stubs::s2_client_state.provide_measurement_call_count);
}

void test_s2_on_new_sensor_value_routes_power_upper_end(void) {
    native_stubs::nvs_s2_access_token = "x";
    native_stubs::nvs_s2_access_token_present = true;
    native_stubs::s2_client_state.initiate_connection_response_access_token = "x";
    native_stubs::s2_client_state.initiate_connection_response_communication_token = "x";
    native_stubs::s2_client_state.initiate_connection_response_url = "ws://x";
    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values(nullptr);
    s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    ems_bridge_sensor_result r = {};
    r.sensorId = EMS_BRIDGE_SENSOR_POWER_LIMIT_UPPER_END_W;
    r.status = EMS_BRIDGE_STATUS_OK;
    r.value = 1500.0f;
    s2_on_new_sensor_value(r);

    TEST_ASSERT_EQUAL_INT(3, native_stubs::s2_client_state.provide_power_upper_limit_max_call_count);
    TEST_ASSERT_FLOAT_WITHIN(0.01, 1500.0f, native_stubs::s2_client_state.last_provided_value);
}

void test_s2_on_new_sensor_value_routes_power_upper_start(void) {
    native_stubs::nvs_s2_access_token = "x";
    native_stubs::nvs_s2_access_token_present = true;
    native_stubs::s2_client_state.initiate_connection_response_access_token = "x";
    native_stubs::s2_client_state.initiate_connection_response_communication_token = "x";
    native_stubs::s2_client_state.initiate_connection_response_url = "ws://x";
    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values(nullptr);
    s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    ems_bridge_sensor_result r = {};
    r.sensorId = EMS_BRIDGE_SENSOR_POWER_LIMIT_UPPER_START_W;
    r.status = EMS_BRIDGE_STATUS_OK;
    s2_on_new_sensor_value(r);

    TEST_ASSERT_EQUAL_INT(3, native_stubs::s2_client_state.provide_power_upper_limit_min_call_count);
}

void test_s2_on_new_sensor_value_routes_power_lower_end_and_start(void) {
    native_stubs::nvs_s2_access_token = "x";
    native_stubs::nvs_s2_access_token_present = true;
    native_stubs::s2_client_state.initiate_connection_response_access_token = "x";
    native_stubs::s2_client_state.initiate_connection_response_communication_token = "x";
    native_stubs::s2_client_state.initiate_connection_response_url = "ws://x";
    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values(nullptr);
    s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    ems_bridge_sensor_result r1 = {};
    r1.sensorId = EMS_BRIDGE_SENSOR_POWER_LIMIT_LOWER_END_W;
    r1.status = EMS_BRIDGE_STATUS_OK;
    s2_on_new_sensor_value(r1);
    TEST_ASSERT_EQUAL_INT(3, native_stubs::s2_client_state.provide_power_lower_limit_max_call_count);

    ems_bridge_sensor_result r2 = {};
    r2.sensorId = EMS_BRIDGE_SENSOR_POWER_LIMIT_LOWER_START_W;
    r2.status = EMS_BRIDGE_STATUS_OK;
    s2_on_new_sensor_value(r2);
    TEST_ASSERT_EQUAL_INT(3, native_stubs::s2_client_state.provide_power_lower_limit_min_call_count);
}

void test_s2_on_new_sensor_value_routes_energy_limits(void) {
    native_stubs::nvs_s2_access_token = "x";
    native_stubs::nvs_s2_access_token_present = true;
    native_stubs::s2_client_state.initiate_connection_response_access_token = "x";
    native_stubs::s2_client_state.initiate_connection_response_communication_token = "x";
    native_stubs::s2_client_state.initiate_connection_response_url = "ws://x";
    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values(nullptr);
    s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    ems_bridge_sensor_result r = {};
    r.status = EMS_BRIDGE_STATUS_OK;

    r.sensorId = EMS_BRIDGE_SENSOR_ENERGY_LIMIT_UPPER_END_WH;
    s2_on_new_sensor_value(r);
    r.sensorId = EMS_BRIDGE_SENSOR_ENERGY_LIMIT_UPPER_START_WH;
    s2_on_new_sensor_value(r);
    r.sensorId = EMS_BRIDGE_SENSOR_ENERGY_LIMIT_LOWER_END_WH;
    s2_on_new_sensor_value(r);
    r.sensorId = EMS_BRIDGE_SENSOR_ENERGY_LIMIT_LOWER_START_WH;
    s2_on_new_sensor_value(r);

    TEST_ASSERT_EQUAL_INT(3, native_stubs::s2_client_state.provide_energy_upper_limit_max_call_count);
    TEST_ASSERT_EQUAL_INT(3, native_stubs::s2_client_state.provide_energy_upper_limit_min_call_count);
    TEST_ASSERT_EQUAL_INT(3, native_stubs::s2_client_state.provide_energy_lower_limit_max_call_count);
    TEST_ASSERT_EQUAL_INT(3, native_stubs::s2_client_state.provide_energy_lower_limit_min_call_count);
}

void test_s2_on_new_sensor_value_routes_measurements(void) {
    native_stubs::nvs_s2_access_token = "x";
    native_stubs::nvs_s2_access_token_present = true;
    native_stubs::s2_client_state.initiate_connection_response_access_token = "x";
    native_stubs::s2_client_state.initiate_connection_response_communication_token = "x";
    native_stubs::s2_client_state.initiate_connection_response_url = "ws://x";
    ems_bridge_setup_values cfg = make_appliance_config();
    s2_hems_values hems = make_hems_values(nullptr);
    s2_init(&hems, &cfg, test_sensor_cb, test_actuator_cb, test_init_ready_cb);

    ems_bridge_sensor_result r = {};
    r.status = EMS_BRIDGE_STATUS_OK;
    r.value = 250.0f;

    r.sensorId = EMS_BRIDGE_SENSOR_ACTUAL_POWER_L1_W;
    s2_on_new_sensor_value(r);
    TEST_ASSERT_EQUAL_INT(S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, native_stubs::s2_client_state.last_measurement_quantity);

    r.sensorId = EMS_BRIDGE_SENSOR_ACTUAL_POWER_L2_W;
    s2_on_new_sensor_value(r);
    TEST_ASSERT_EQUAL_INT(S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2, native_stubs::s2_client_state.last_measurement_quantity);

    r.sensorId = EMS_BRIDGE_SENSOR_ACTUAL_POWER_L3_W;
    s2_on_new_sensor_value(r);
    TEST_ASSERT_EQUAL_INT(S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3, native_stubs::s2_client_state.last_measurement_quantity);

    r.sensorId = EMS_BRIDGE_SENSOR_ACTUAL_POWER_TOTAL_W;
    s2_on_new_sensor_value(r);
    TEST_ASSERT_EQUAL_INT(S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC, native_stubs::s2_client_state.last_measurement_quantity);

    TEST_ASSERT_EQUAL_INT(4, native_stubs::s2_client_state.provide_measurement_call_count);
}

void test_s2_on_new_sensor_value_unknown_sensor_id_is_noop(void) {
    ems_bridge_sensor_result r = {};
    r.sensorId = EMS_BRIDGE_SENSOR_STATUS;
    r.status = EMS_BRIDGE_STATUS_OK;

    s2_on_new_sensor_value(r);

    TEST_ASSERT_EQUAL_INT(0, native_stubs::s2_client_state.provide_measurement_call_count);
}

void test_s2_on_actuator_set_does_not_crash(void) {
    ems_bridge_set_actuator_result r = {};
    s2_on_actuator_set(r);
    TEST_PASS();
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_s2_init_succeeds_with_existing_access_token);
    RUN_TEST(test_s2_init_fails_when_no_token_and_no_pairing_token);
    RUN_TEST(test_s2_init_does_pairing_when_no_token);
    RUN_TEST(test_s2_init_fails_when_s2_client_init_returns_null);
    RUN_TEST(test_s2_init_fails_when_prepare_pairing_fails);
    RUN_TEST(test_s2_init_fails_when_request_pairing_fails);
    RUN_TEST(test_s2_init_fails_when_initiate_connection_fails);
    RUN_TEST(test_s2_init_fails_when_ws_connect_fails);
    RUN_TEST(test_s2_on_new_sensor_value_ignored_when_status_not_ok);
    RUN_TEST(test_s2_on_new_sensor_value_routes_power_upper_end);
    RUN_TEST(test_s2_on_new_sensor_value_routes_power_upper_start);
    RUN_TEST(test_s2_on_new_sensor_value_routes_power_lower_end_and_start);
    RUN_TEST(test_s2_on_new_sensor_value_routes_energy_limits);
    RUN_TEST(test_s2_on_new_sensor_value_routes_measurements);
    RUN_TEST(test_s2_on_new_sensor_value_unknown_sensor_id_is_noop);
    RUN_TEST(test_s2_on_actuator_set_does_not_crash);
    return UNITY_END();
}
