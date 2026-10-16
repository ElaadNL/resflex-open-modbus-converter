// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

#include "storage_component.h"
#include "modbus_component.h"
#include "led_component.h"
#include "shared_queue.hpp"
#include "s2_library_wrapper.h"
#include "eth_wifi_component.h"
#include "native_test_state.h"

QueueHandle_t led_queue = nullptr;
QueueHandle_t modbus_mapping_queue = nullptr;

namespace native_stubs {

std::string storage_template_json;
register_read_value holding_register_value;
register_read_value input_register_value;
s2_init_capture s2_init_state;
s2_callbacks_capture s2_callbacks_state;
eth_wifi_capture eth_wifi_state;
s2_client_capture s2_client_state;

std::string nvs_s2_access_token;
bool nvs_s2_access_token_present = false;

std::map<std::string, std::string> nvs_string_values;
std::map<std::string, uint16_t> nvs_u16_values;

void reset() {
    storage_template_json.clear();
    holding_register_value = {};
    input_register_value = {};
    s2_init_state = {};
    s2_callbacks_state = {};
    eth_wifi_state = {};
    s2_client_state = {};
    nvs_s2_access_token.clear();
    nvs_s2_access_token_present = false;
    nvs_string_values.clear();
    nvs_u16_values.clear();
}

}

bool init_storage_component() { return true; }

bool storage_load_s2_template(const char*, char*, size_t) { return false; }
bool storage_save_s2_template(const char*, const char*) { return true; }

bool storage_set_configuration(const char* key, uint16_t value) {
    if (key != nullptr) native_stubs::nvs_u16_values[key] = value;
    return true;
}
bool storage_set_configuration(const char*, uint32_t) { return true; }
bool storage_set_configuration(const char* key, const char* value) {
    if (key != nullptr && std::strcmp(key, "s2_access_tkn") == 0) {
        if (value == nullptr || value[0] == '\0') {
            native_stubs::nvs_s2_access_token.clear();
            native_stubs::nvs_s2_access_token_present = false;
        } else {
            native_stubs::nvs_s2_access_token = value;
            native_stubs::nvs_s2_access_token_present = true;
        }
    }
    return true;
}

bool storage_get_configuration(const char* key, uint16_t& value, uint16_t default_value) {
    if (key != nullptr) {
        auto it = native_stubs::nvs_u16_values.find(key);
        if (it != native_stubs::nvs_u16_values.end()) {
            value = it->second;
            return true;
        }
    }
    value = default_value;
    return true;
}

bool storage_get_configuration(const char*, uint32_t& value, uint32_t default_value) {
    value = default_value;
    return true;
}

bool storage_get_configuration(const char* key, char* value, size_t max_len, const char* default_value) {
    if (value == nullptr || max_len == 0) {
        return false;
    }
    // Default for any key the stub doesn't explicitly model: mimic real NVS,
    // which hands back the caller-supplied default_value when the key is absent.
    std::string default_str = default_value != nullptr ? default_value : "";
    const std::string* src = &default_str;
    // Per-key seeded values win over the special-case keys below, so a test
    // can override `device_template` etc. through nvs_string_values too.
    if (key != nullptr) {
        auto it = native_stubs::nvs_string_values.find(key);
        if (it != native_stubs::nvs_string_values.end()) {
            src = &it->second;
        } else if (std::strcmp(key, "device_template") == 0) {
            src = &native_stubs::storage_template_json;
        } else if (std::strcmp(key, "s2_access_tkn") == 0) {
            if (!native_stubs::nvs_s2_access_token_present) {
                value[0] = '\0';
                return false;
            }
            src = &native_stubs::nvs_s2_access_token;
        }
    }
    if (src->empty()) {
        value[0] = '\0';
        return true;
    }
    const size_t copy_len = src->size() < max_len - 1 ? src->size() : max_len - 1;
    std::memcpy(value, src->data(), copy_len);
    value[copy_len] = '\0';
    return true;
}

bool modbus_component_init(const appliance_config_t&) { return true; }

bool modbus_component_read_holding_register(uint16_t, uint16_t* value) {
    if (value == nullptr) return false;
    *value = native_stubs::holding_register_value.u16;
    return native_stubs::holding_register_value.success;
}

bool modbus_component_read_holding_register(uint16_t, uint32_t* value) {
    if (value == nullptr) return false;
    *value = native_stubs::holding_register_value.u32;
    return native_stubs::holding_register_value.success;
}

bool modbus_component_read_input_register(uint16_t, uint16_t* value) {
    if (value == nullptr) return false;
    *value = native_stubs::input_register_value.u16;
    return native_stubs::input_register_value.success;
}

bool modbus_component_read_input_register(uint16_t, uint32_t* value) {
    if (value == nullptr) return false;
    *value = native_stubs::input_register_value.u32;
    return native_stubs::input_register_value.success;
}

bool modbus_component_read_holding_register_string(uint16_t, uint16_t, char* value, size_t max_len, bool) {
    if (value == nullptr || max_len == 0) return false;
    const std::string& src = native_stubs::holding_register_value.str;
    const size_t copy_len = src.size() < max_len - 1 ? src.size() : max_len - 1;
    std::memcpy(value, src.data(), copy_len);
    value[copy_len] = '\0';
    return native_stubs::holding_register_value.success;
}

bool modbus_component_read_input_register_string(uint16_t, uint16_t, char* value, size_t max_len, bool) {
    if (value == nullptr || max_len == 0) return false;
    const std::string& src = native_stubs::input_register_value.str;
    const size_t copy_len = src.size() < max_len - 1 ? src.size() : max_len - 1;
    std::memcpy(value, src.data(), copy_len);
    value[copy_len] = '\0';
    return native_stubs::input_register_value.success;
}

bool modbus_component_write_register_16(uint16_t, uint16_t) { return true; }
bool modbus_component_write_register_32(uint16_t, uint32_t, bool) { return true; }
bool modbus_component_read_coil(uint16_t, bool*) { return false; }
bool modbus_component_write_coil(uint16_t, bool) { return false; }

void set_LED_state(led_state) {}

// s2_init/s2_on_new_sensor_value/s2_on_actuator_set live in s2_wrapper_stubs.cpp
// so envs that link the real s2_library_wrapper.cpp can omit those stubs.

void init_eth_wifi() {}

bool eth_wifi_get_active_interface_name(char* interface_name, size_t interface_name_size) {
    if (!native_stubs::eth_wifi_state.interface_name_available || interface_name == nullptr || interface_name_size == 0) {
        return false;
    }
    const std::string& src = native_stubs::eth_wifi_state.interface_name;
    const size_t copy_len = src.size() < interface_name_size - 1 ? src.size() : interface_name_size - 1;
    std::memcpy(interface_name, src.data(), copy_len);
    interface_name[copy_len] = '\0';
    return true;
}
