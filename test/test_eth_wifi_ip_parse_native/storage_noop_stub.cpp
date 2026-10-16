// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

// Linker-only stub: the IP-parse test does not exercise storage at all, but
// the shared native_captive build pulls in captive_portal_nvs.cpp which
// references storage_set_configuration / storage_get_configuration. We
// provide empty implementations so the link succeeds; behavior is
// irrelevant because the IP-parse test never invokes the captive-portal code.

#include "storage_component.h"

bool init_storage_component() { return true; }
bool storage_load_s2_template(const char*, char*, size_t) { return false; }
bool storage_save_s2_template(const char*, const char*) { return false; }

bool storage_set_configuration(const char*, uint16_t) { return true; }
bool storage_set_configuration(const char*, uint32_t) { return true; }
bool storage_set_configuration(const char*, const char*) { return true; }

bool storage_get_configuration(const char*, uint16_t& value, uint16_t default_value) {
    value = default_value;
    return true;
}
bool storage_get_configuration(const char*, uint32_t& value, uint32_t default_value) {
    value = default_value;
    return true;
}
bool storage_get_configuration(const char*, char* value, size_t max_len, const char* default_value) {
    if (value == nullptr || max_len == 0) {
        return false;
    }
    if (default_value == nullptr) {
        value[0] = '\0';
    } else {
        size_t i = 0;
        for (; i + 1 < max_len && default_value[i] != '\0'; ++i) {
            value[i] = default_value[i];
        }
        value[i] = '\0';
    }
    return true;
}