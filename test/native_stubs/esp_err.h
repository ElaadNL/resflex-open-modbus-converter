// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

typedef int esp_err_t;

#define ESP_OK   0
#define ESP_FAIL -1

#define ESP_ERR_NOT_FOUND        (-2001)
#define ESP_ERR_INVALID_STATE    (-2030)

#define ESP_ERROR_CHECK(x) do { \
        esp_err_t err_rc_ = (x); \
        if (err_rc_ != ESP_OK) { \
            abort(); \
        } \
    } while(0)

inline const char* esp_err_to_name(esp_err_t) { return "ESP_NATIVE_STUB"; }
