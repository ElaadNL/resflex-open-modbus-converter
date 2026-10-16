// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

// Minimal in-memory implementation of storage_component used by the
// captive-portal POST flow tests. Lives in this test directory (not in
// test/native_stubs) so it can't affect the other native envs.

#include "storage_component.h"

#include <cstring>
#include <map>
#include <string>

namespace captive_test_storage {

// Two maps: strings and uint16s. uint32 isn't used by the captive portal.
static std::map<std::string, std::string> string_values;
static std::map<std::string, uint16_t> u16_values;

void reset() {
    string_values.clear();
    u16_values.clear();
}

bool has_string(const std::string& key) {
    return string_values.find(key) != string_values.end();
}

bool has_u16(const std::string& key) {
    return u16_values.find(key) != u16_values.end();
}

const std::string& get_string(const std::string& key) {
    return string_values[key];
}

uint16_t get_u16(const std::string& key) {
    return u16_values[key];
}

void seed_string(const std::string& key, const std::string& value) {
    string_values[key] = value;
}

}  // namespace captive_test_storage

// ── storage_component interface implementation ──────────────────────────────

bool init_storage_component() { return true; }
bool storage_load_s2_template(const char*, char*, size_t) { return false; }
bool storage_save_s2_template(const char*, const char*) { return true; }

bool storage_set_configuration(const char* key, uint16_t value) {
    if (key) captive_test_storage::u16_values[key] = value;
    return true;
}

bool storage_set_configuration(const char*, uint32_t) {
    // Not used by the captive portal — kept so the linker is happy.
    return true;
}

bool storage_set_configuration(const char* key, const char* value) {
    if (key && value) captive_test_storage::string_values[key] = value;
    return true;
}

bool storage_get_configuration(const char* key, uint16_t& value, uint16_t default_value) {
    auto it = captive_test_storage::u16_values.find(key ? key : "");
    if (it == captive_test_storage::u16_values.end()) {
        value = default_value;
        return true;
    }
    value = it->second;
    return true;
}

bool storage_get_configuration(const char*, uint32_t& value, uint32_t default_value) {
    value = default_value;
    return true;
}

bool storage_get_configuration(const char* key, char* out, size_t max_len, const char* default_value) {
    if (out == nullptr || max_len == 0) return false;
    auto it = captive_test_storage::string_values.find(key ? key : "");
    const std::string& src = (it != captive_test_storage::string_values.end())
        ? it->second
        : std::string(default_value ? default_value : "");
    size_t copy_len = src.size() < max_len - 1 ? src.size() : max_len - 1;
    std::memcpy(out, src.data(), copy_len);
    out[copy_len] = '\0';
    return true;
}
