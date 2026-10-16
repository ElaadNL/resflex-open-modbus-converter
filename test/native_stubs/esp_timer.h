// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Native-test stub for esp_timer.h.
//
// modbus_mapping_component.cpp uses esp_timer_get_time() to build a monotonic
// millisecond clock (monotonic_now_ms). On the host we back it with the standard
// steady_clock so the elapsed-time logic behaves like it does on the ESP32, where
// esp_timer_get_time() returns microseconds since boot.

#include <cstdint>
#include <chrono>

inline int64_t esp_timer_get_time(void) {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}
