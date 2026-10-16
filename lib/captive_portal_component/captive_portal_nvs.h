// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>

/**
 * @file captive_portal_nvs.h
 * @brief NVS-write policies for the captive-portal form submissions.
 *
 * Separated from captive_portal_parsing.h so that the parsing helpers can
 * stay ESP-independent. This file talks to storage_component and applies
 * the captive portal's policy decisions about which fields are persisted
 * unconditionally, which only when non-empty, and how dropdowns/checkboxes
 * map to NVS values.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Apply a urlencoded form body to NVS via storage_component.
 *
 * Walks the known config fields, extracts their values via
 * @ref captive_form_field, and persists them with
 * @c storage_set_configuration. Encodes the captive portal's policy
 * decisions about which fields should be saved:
 *
 *  - Form fields `wifi_ssid`, `websocket_uri`, `ip_addr`, `ip_netmask` and
 *    `ip_gateway` are written whenever present, including when empty.
 *  - Form fields `wifi_password` and `pair_key` are written only when
 *    present and non-empty. An empty submit means "keep stored value".
 *  - Form field `ip_static` is written as the `net_use_static_ip` NVS flag.
 *    Absent in the body == unchecked == 0, so the user can turn it off.
 *
 * @param body NUL-terminated urlencoded body. The buffer is not modified.
 */
void captive_apply_form_to_nvs(char *body);

/**
 * @brief Apply the appliance-parameter form fields to NVS.
 *
 * Companion to @ref captive_apply_form_to_nvs, but for the parameter inputs
 * rendered below the JSON textarea on the `/appliance` page. Writes:
 *
 *  - `appliance_name`, `appliance_model`, `appliance_serial`,
 *    `appliance_firmware`, `modbus_channel`, `modbus_ip`, `modbus_port`,
 *    `modbus_gateway`, `modbus_netmask`, `modbus_unit_id`: written whenever
 *    present, including when empty (so the user can blank them out).
 *  - `num_phases`: parsed as a uint16, clamped to {1, 2, 3}; defaults to 1
 *    if absent or unparseable. Always written.
 *
 * @param body Mutable, NUL-terminated urlencoded body (not modified).
 */
void captive_apply_appliance_params_to_nvs(char *body);

/**
 * @brief Read a string from NVS and HTML-escape it into a destination buffer.
 *
 * Convenience helper used by the page renderers to pre-fill form fields
 * safely. Reads the raw value via @c storage_get_configuration (falling
 * back to @p default_value if the key is missing or empty), then runs it
 * through @ref captive_html_escape so it can be interpolated into HTML
 * without breaking the form or enabling XSS.
 *
 * @param key               NVS key to read.
 * @param default_value     Value used if the key is absent (may be nullptr —
 *                          treated as empty string).
 * @param escaped_out       Destination buffer for the escaped string
 *                          (NUL-terminated when @p escaped_out_size > 0).
 * @param escaped_out_size  Size of @p escaped_out (truncates on overflow).
 */
void captive_load_escaped(const char *key, const char *default_value,
                          char *escaped_out, size_t escaped_out_size);

#ifdef __cplusplus
}
#endif
