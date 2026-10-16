// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>


/**
 * @brief Initialize persistent storage backends.
 *
 * Initializes the default NVS partition and SPIFFS template storage used by
 * the firmware.
 *
 * @return true if all required storage backends initialized successfully,
 *         false otherwise.
 */
bool init_storage_component();

/**
 * @brief Load an S2 template file from SPIFFS.
 *
 * @param path File path inside the mounted SPIFFS partition.
 * @param output Destination buffer for the loaded, NUL-terminated template.
 * @param max_len Size of @p output in bytes.
 * @return true if the file was read successfully, false otherwise.
 */
bool storage_load_s2_template(const char *path, char *output, size_t max_len);

/**
 * @brief Save an S2 template file to SPIFFS.
 *
 * @param path File path inside the mounted SPIFFS partition.
 * @param template_data Null-terminated template data to write.
 * @return true if the file was written successfully, false otherwise.
 */
bool storage_save_s2_template(const char *path, const char *template_data);

/**
 * @brief Set a uint16_t configuration value in NVS.
 * @param key   The key to store the value under (max 15 characters).
 * @param value The uint16_t value to store.
 * @return true if successful, false otherwise.
 */
bool storage_set_configuration(const char *key, uint16_t value);

/**
 * @brief Set a uint32_t configuration value in NVS.
 * @param key   The key to store the value under (max 15 characters).
 * @param value The uint32_t value to store.
 * @return true if successful, false otherwise.
 */
bool storage_set_configuration(const char *key, uint32_t value);

/**
 * @brief Set a string configuration value in NVS.
 * @param key   The key to store the value under (max 15 characters).
 * @param value Null-terminated string to store.
 * @return true if successful, false otherwise.
 */
bool storage_set_configuration(const char *key, const char *value);

/**
 * @brief Get a uint16_t configuration value. If it doesn't exist, use and save the default.
 * @param key           The key to search for.
 * @param value         Reference to store the retrieved (or default) uint16_t value.
 * @param default_value The value to use and save if the key is not found.
 * @return true if successful (either read or default applied), false on hardware/read errors.
 */
bool storage_get_configuration(const char *key, uint16_t &value, uint16_t default_value);

/**
 * @brief Get a uint32_t configuration value. If it doesn't exist, use and save the default.
 * @param key           The key to search for.
 * @param value         Reference to store the retrieved (or default) uint32_t value.
 * @param default_value The value to use and save if the key is not found.
 * @return true if successful (either read or default applied), false on hardware/read errors.
 */
bool storage_get_configuration(const char *key, uint32_t &value, uint32_t default_value);

/**
 * @brief Get a string configuration value. If it doesn't exist, use and save the default.
 * @param key           The key to search for.
 * @param value         Pointer to the buffer where the string will be stored.
 * @param max_len       The maximum length of the buffer.
 * @param default_value The string to use and save if the key is not found. If null, missing keys return an empty string without storing a default.
 * @return true if successful, false on hardware/read errors.
 */
bool storage_get_configuration(const char *key, char *value, size_t max_len, const char *default_value);
