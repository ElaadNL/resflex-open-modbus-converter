// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdint.h>
#include <ems_bridge.h>

/**
 * @brief Stores the most recent value of a sensor along with its validity window.
 *
 * Used by mapping_request_preprocessing() (and potentially by
 * requiredSensorsBeforeEvaluation handling) to look up cached sensor values
 * without re-reading Modbus. Each entry carries the validity duration that was
 * configured for that sensor in the device template; the entry is considered
 * stale once (now_ms - timestamp_ms) exceeds that duration.
 *
 * A validity duration of 0 is treated as "never valid" by design: callers fall
 * back to a configured default instead of relying on a possibly stale value.
 *
 * The cache is time-source agnostic: callers pass a monotonic millisecond
 * timestamp on every record() and get() call. This keeps the cache trivially
 * unit-testable and avoids pulling esp_timer into native test builds.
 */

/**
 * @brief Records a sensor value in the cache with its validity window.
 *
 * Overwrites any previous entry for the same sensor.
 *
 * @param sensor_id            Sensor identifier the value belongs to.
 * @param value                Latest measured value (in the sensor's native unit).
 * @param validity_duration_ms Validity window for the value, in milliseconds.
 *                             0 means the value is never considered valid by get().
 * @param now_ms               Current monotonic time in milliseconds.
 */
void modbus_mapping_cache_record(ems_bridge_sensor_t sensor_id, float value, uint32_t validity_duration_ms, uint32_t now_ms);

/**
 * @brief Reads a sensor value from the cache if it is still valid.
 *
 * @param sensor_id Sensor identifier to look up.
 * @param out_value Output pointer that receives the cached value when the entry
 *                  is present and still within its validity window.
 * @param now_ms    Current monotonic time in milliseconds.
 * @return true if a fresh value was returned; false if the sensor has no entry,
 *         its validity duration is 0, or the entry has expired.
 */
bool modbus_mapping_cache_get(ems_bridge_sensor_t sensor_id, float *out_value, uint32_t now_ms);

/**
 * @brief Clears all cached sensor values. Intended for test setup/teardown.
 */
void modbus_mapping_cache_clear();