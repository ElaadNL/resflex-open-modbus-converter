// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdint.h>

// Native-test MAC override: ap_wifi_test::mac_bytes seeds the value returned
// by esp_efuse_mac_get_default(). Defaults to a fixed OUI + device suffix.
namespace ap_wifi_test {
extern uint8_t mac_bytes[6];
}

inline void esp_efuse_mac_get_default(uint8_t* mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = ap_wifi_test::mac_bytes[i];
    }
}
