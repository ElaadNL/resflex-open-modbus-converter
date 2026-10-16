// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "esp_err.h"
#include "ems_bridge.h"

namespace native_stubs {

struct register_read_value {
    bool success = false;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    std::string str;
};

struct s2_init_capture {
    int call_count = 0;
    esp_err_t return_value = ESP_OK;
    ems_bridge_setup_values last_appliance_config = {};
    ems_bridge_interface_t last_interface = EMS_INTERFACE_S2_PEBC;
    std::string last_base_url;
    std::string last_pairing_token;
    std::string last_interface_name;
};

struct s2_callbacks_capture {
    int new_sensor_value_call_count = 0;
    int actuator_set_call_count = 0;
    ems_bridge_sensor_result last_sensor_value = {};
    ems_bridge_set_actuator_result last_actuator_result = {};
};

struct eth_wifi_capture {
    bool interface_name_available = false;
    std::string interface_name;
};

struct s2_client_capture {
    int init_call_count = 0;
    bool init_returns_null = false;
    int prepare_pairing_call_count = 0;
    int request_pairing_call_count = 0;
    int initiate_connection_call_count = 0;
    int ws_connect_call_count = 0;
    int provide_measurement_call_count = 0;
    int provide_power_upper_limit_max_call_count = 0;
    int provide_power_upper_limit_min_call_count = 0;
    int provide_power_lower_limit_max_call_count = 0;
    int provide_power_lower_limit_min_call_count = 0;
    int provide_energy_upper_limit_max_call_count = 0;
    int provide_energy_upper_limit_min_call_count = 0;
    int provide_energy_lower_limit_max_call_count = 0;
    int provide_energy_lower_limit_min_call_count = 0;
    int last_measurement_quantity = -1;
    float last_measurement_value = 0.0f;
    float last_provided_value = 0.0f;
    esp_err_t prepare_pairing_return = ESP_OK;
    esp_err_t request_pairing_return = ESP_OK;
    esp_err_t initiate_connection_return = ESP_OK;
    esp_err_t ws_connect_return = ESP_OK;
    std::string request_pairing_response_token;
    std::string initiate_connection_response_access_token;
    std::string initiate_connection_response_communication_token;
    std::string initiate_connection_response_url;
};

extern std::string storage_template_json;
extern register_read_value holding_register_value;
extern register_read_value input_register_value;
extern s2_init_capture s2_init_state;
extern s2_callbacks_capture s2_callbacks_state;
extern eth_wifi_capture eth_wifi_state;
extern s2_client_capture s2_client_state;

extern std::string nvs_s2_access_token;
extern bool nvs_s2_access_token_present;

// Per-key NVS overrides for the native stubs. Empty by default — only the
// keys a test explicitly seeds are returned from storage_get_configuration().
// Anything not seeded falls back to the caller-supplied default_value, which
// matches real NVS behavior when the key is absent.
extern std::map<std::string, std::string> nvs_string_values;
extern std::map<std::string, uint16_t> nvs_u16_values;

void reset();

}
