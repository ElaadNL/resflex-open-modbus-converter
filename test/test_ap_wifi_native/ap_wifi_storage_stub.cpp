// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

// Self-contained storage stub for the ap_wifi native env.
//
// ap_wifi.cpp only touches the string form of the configuration API for the
// `ap_password` key. We model that key with a per-test seedable value:
//   - storage_set_configuration("ap_password", ...) records the generated
//     password.
//   - storage_get_configuration("ap_password", ...) returns the seeded value
//     (empty by default, which simulates first boot).
// Other keys fall back to the caller-supplied default, matching real NVS
// behavior when a key is absent.

#include <cstring>

#include "ap_wifi_storage_stub.h"
#include "storage_component.h"

namespace ap_wifi_storage_stub {

std::string seeded_password;
std::string generated_password;

void reset() {
    seeded_password.clear();
    generated_password.clear();
}

}

bool storage_set_configuration(const char* key, const char* value) {
    if (key != nullptr && std::strcmp(key, "ap_password") == 0 && value != nullptr) {
        ap_wifi_storage_stub::generated_password = value;
    }
    return true;
}

bool storage_get_configuration(const char* key, char* value, size_t max_len, const char* default_value) {
    if (value == nullptr || max_len == 0) {
        return false;
    }
    std::string src;
    if (key != nullptr && std::strcmp(key, "ap_password") == 0) {
        // A value written via storage_set_configuration wins; otherwise fall
        // back to the pre-existing (seeded) NVS entry.
        if (!ap_wifi_storage_stub::generated_password.empty()) {
            src = ap_wifi_storage_stub::generated_password;
        } else {
            src = ap_wifi_storage_stub::seeded_password;
        }
    } else {
        src = default_value != nullptr ? default_value : std::string();
    }
    const size_t copy_len = src.size() < max_len - 1 ? src.size() : max_len - 1;
    std::memcpy(value, src.data(), copy_len);
    value[copy_len] = '\0';
    return true;
}
