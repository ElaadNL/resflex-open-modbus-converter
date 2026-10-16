// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

// s2_init/s2_on_new_sensor_value/s2_on_actuator_set are stubbed here for tests that
// don't compile the real s2_library_wrapper.cpp. Tests that DO compile the real
// implementation must NOT link this file.

#include <cstring>

#include "s2_library_wrapper.h"
#include "native_test_state.h"

esp_err_t s2_init(
    s2_hems_values* hems_values,
    ems_bridge_setup_values* appliance_config,
    ems_bridge_request_sensor_cb_t,
    ems_bridge_set_actuator_cb_t,
    ems_bridge_on_init_ready_cb_t
) {
    native_stubs::s2_init_state.call_count++;
    if (appliance_config != nullptr) {
        native_stubs::s2_init_state.last_appliance_config = *appliance_config;
    }
    if (hems_values != nullptr) {
        native_stubs::s2_init_state.last_base_url = hems_values->base_url;
        native_stubs::s2_init_state.last_pairing_token = hems_values->pairing_input_token;
        native_stubs::s2_init_state.last_interface_name = hems_values->interface;
    }
    return native_stubs::s2_init_state.return_value;
}

void s2_on_new_sensor_value(ems_bridge_sensor_result result) {
    native_stubs::s2_callbacks_state.new_sensor_value_call_count++;
    native_stubs::s2_callbacks_state.last_sensor_value = result;
}

void s2_on_actuator_set(ems_bridge_set_actuator_result result) {
    native_stubs::s2_callbacks_state.actuator_set_call_count++;
    native_stubs::s2_callbacks_state.last_actuator_result = result;
}
