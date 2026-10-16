// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "modbus_mapping_cache.h"

#include <cstdint>
#include <unordered_map>

struct cache_entry_t {
    float value;
    uint32_t validity_duration_ms;
    uint32_t timestamp_ms;
};

static std::unordered_map<int, cache_entry_t> cache_entries;



void modbus_mapping_cache_record(ems_bridge_sensor_t sensor_id, float value, uint32_t validity_duration_ms, uint32_t now_ms) {
    cache_entry_t entry;
    entry.value = value;
    entry.validity_duration_ms = validity_duration_ms;
    entry.timestamp_ms = now_ms;
    cache_entries[static_cast<int>(sensor_id)] = entry;
}



bool modbus_mapping_cache_get(ems_bridge_sensor_t sensor_id, float *out_value, uint32_t now_ms) {
    if (out_value == nullptr) {
        return false;
    }

    auto iterator = cache_entries.find(static_cast<int>(sensor_id));
    if (iterator == cache_entries.end()) {
        return false;
    }

    const cache_entry_t &entry = iterator->second;

    // A validity duration of 0 means the value is never considered valid.
    // Callers should then fall back to a configured default.
    if (entry.validity_duration_ms == 0) {
        return false;
    }

    // Monotonic timestamps: subtraction in unsigned arithmetic wraps cleanly,
    // so a 32-bit ms counter rollover after ~49 days does not produce a false
    // "still valid" result as long as the validity window itself fits.
    uint32_t elapsed_ms = now_ms - entry.timestamp_ms;
    if (elapsed_ms > entry.validity_duration_ms) {
        return false;
    }

    *out_value = entry.value;
    return true;
}

void modbus_mapping_cache_clear() {
    cache_entries.clear();
}