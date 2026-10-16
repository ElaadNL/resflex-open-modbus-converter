// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string.h>

#include "esp_err.h"

typedef enum {
    WIFI_IF_STA = 0,
    WIFI_IF_AP = 1
} wifi_interface_t;

typedef enum {
    WIFI_AUTH_OPEN = 0,
    WIFI_AUTH_WPA2_PSK = 3,
    WIFI_AUTH_WPA2_WPA3_PSK = 10
} wifi_auth_mode_t;

typedef struct {
    struct {
        uint8_t ssid[32];
        uint8_t ssid_len;
        uint8_t password[64];
        uint8_t channel;
        uint8_t max_connection;
        wifi_auth_mode_t authmode;
        struct {
            bool required;
        } pmf_cfg;
    } ap;
} wifi_config_t;

// Native-test capture of the last AP radio configuration pushed onto WIFI_IF_AP.
namespace ap_wifi_test {
extern wifi_config_t last_ap_config;
extern int set_config_call_count;

void reset();
}

esp_err_t esp_wifi_set_config(wifi_interface_t if_index, const wifi_config_t* config);
