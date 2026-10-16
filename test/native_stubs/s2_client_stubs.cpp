// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <cstring>

#include "s2_client.h"
#include "native_test_state.h"

static int dummy_s2_client_handle_object = 0;

s2_client_handle_t s2_client_init(s2_client_config_t*) {
    native_stubs::s2_client_state.init_call_count++;
    if (native_stubs::s2_client_state.init_returns_null) {
        return nullptr;
    }
    return &dummy_s2_client_handle_object;
}

esp_err_t s2_client_prepare_pairing(s2_client_handle_t) {
    native_stubs::s2_client_state.prepare_pairing_call_count++;
    return native_stubs::s2_client_state.prepare_pairing_return;
}

esp_err_t s2_client_cancel_prepare_pairing(s2_client_handle_t) {
    return ESP_OK;
}

esp_err_t s2_client_request_pairing(s2_client_handle_t, const char*, s2_client_request_pairing_response_t* response) {
    native_stubs::s2_client_state.request_pairing_call_count++;
    if (response != nullptr) {
        const std::string& token = native_stubs::s2_client_state.request_pairing_response_token;
        std::strncpy(response->access_token, token.c_str(), sizeof(response->access_token) - 1);
        response->access_token[sizeof(response->access_token) - 1] = '\0';
    }
    return native_stubs::s2_client_state.request_pairing_return;
}

esp_err_t s2_client_initiate_connection(s2_client_handle_t, const char*, s2_client_initiate_connection_response_t* response) {
    native_stubs::s2_client_state.initiate_connection_call_count++;
    if (response != nullptr) {
        const auto& state = native_stubs::s2_client_state;
        std::strncpy(response->token_pair.access_token, state.initiate_connection_response_access_token.c_str(), sizeof(response->token_pair.access_token) - 1);
        response->token_pair.access_token[sizeof(response->token_pair.access_token) - 1] = '\0';
        std::strncpy(response->token_pair.communication_token, state.initiate_connection_response_communication_token.c_str(), sizeof(response->token_pair.communication_token) - 1);
        response->token_pair.communication_token[sizeof(response->token_pair.communication_token) - 1] = '\0';
        std::strncpy(response->connection_url, state.initiate_connection_response_url.c_str(), sizeof(response->connection_url) - 1);
        response->connection_url[sizeof(response->connection_url) - 1] = '\0';
    }
    return native_stubs::s2_client_state.initiate_connection_return;
}

esp_err_t s2_client_ws_connect(s2_client_handle_t, const char*, const char*, const char*) {
    native_stubs::s2_client_state.ws_connect_call_count++;
    return native_stubs::s2_client_state.ws_connect_return;
}

esp_err_t s2_client_provide_measurement(s2_client_handle_t, s2_commodity_quantity_t quantity, float value) {
    native_stubs::s2_client_state.provide_measurement_call_count++;
    native_stubs::s2_client_state.last_measurement_quantity = static_cast<int>(quantity);
    native_stubs::s2_client_state.last_measurement_value = value;
    return ESP_OK;
}

esp_err_t s2_client_pebc_abort_instruction(s2_client_handle_t) { return ESP_OK; }
esp_err_t s2_client_pebc_revoke_constraints(s2_client_handle_t) { return ESP_OK; }
esp_err_t s2_client_pebc_provide_energy_valid_until(s2_client_handle_t, s2_commodity_quantity_t, int64_t) { return ESP_OK; }

esp_err_t s2_client_pebc_provide_power_upper_limit_max(s2_client_handle_t, s2_commodity_quantity_t, float value) {
    native_stubs::s2_client_state.provide_power_upper_limit_max_call_count++;
    native_stubs::s2_client_state.last_provided_value = value;
    return ESP_OK;
}
esp_err_t s2_client_pebc_provide_power_upper_limit_min(s2_client_handle_t, s2_commodity_quantity_t, float value) {
    native_stubs::s2_client_state.provide_power_upper_limit_min_call_count++;
    native_stubs::s2_client_state.last_provided_value = value;
    return ESP_OK;
}
esp_err_t s2_client_pebc_provide_power_lower_limit_max(s2_client_handle_t, s2_commodity_quantity_t, float value) {
    native_stubs::s2_client_state.provide_power_lower_limit_max_call_count++;
    native_stubs::s2_client_state.last_provided_value = value;
    return ESP_OK;
}
esp_err_t s2_client_pebc_provide_power_lower_limit_min(s2_client_handle_t, s2_commodity_quantity_t, float value) {
    native_stubs::s2_client_state.provide_power_lower_limit_min_call_count++;
    native_stubs::s2_client_state.last_provided_value = value;
    return ESP_OK;
}
esp_err_t s2_client_pebc_provide_energy_upper_limit_max(s2_client_handle_t, s2_commodity_quantity_t, float value) {
    native_stubs::s2_client_state.provide_energy_upper_limit_max_call_count++;
    native_stubs::s2_client_state.last_provided_value = value;
    return ESP_OK;
}
esp_err_t s2_client_pebc_provide_energy_upper_limit_min(s2_client_handle_t, s2_commodity_quantity_t, float value) {
    native_stubs::s2_client_state.provide_energy_upper_limit_min_call_count++;
    native_stubs::s2_client_state.last_provided_value = value;
    return ESP_OK;
}
esp_err_t s2_client_pebc_provide_energy_lower_limit_max(s2_client_handle_t, s2_commodity_quantity_t, float value) {
    native_stubs::s2_client_state.provide_energy_lower_limit_max_call_count++;
    native_stubs::s2_client_state.last_provided_value = value;
    return ESP_OK;
}
esp_err_t s2_client_pebc_provide_energy_lower_limit_min(s2_client_handle_t, s2_commodity_quantity_t, float value) {
    native_stubs::s2_client_state.provide_energy_lower_limit_min_call_count++;
    native_stubs::s2_client_state.last_provided_value = value;
    return ESP_OK;
}
