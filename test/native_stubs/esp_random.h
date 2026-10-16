// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

inline void esp_fill_random(void* buf, size_t len) {
    uint8_t* out = (uint8_t*) buf;
    for (size_t i = 0; i < len; ++i) {
        out[i] = (uint8_t)(rand() & 0xFF);
    }
}