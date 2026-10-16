// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "eth_wifi_ip_parse.h"

#include <cstdint>
#include <stddef.h>

bool eth_wifi_parse_ipv4(const char *str, uint8_t out[4]) {
    if (str == nullptr || out == nullptr) {
        return false;
    }

    uint8_t parsed_octets[4] = {0, 0, 0, 0};
    size_t octet_index = 0;
    bool any_digit_in_current_octet = false;
    bool current_octet_is_just_zero = false;
    uint16_t current_octet_value = 0;

    for (const char *cursor = str;; ++cursor) {
        const char character = *cursor;

        if (character >= '0' && character <= '9') {
            // Reject leading zeros: a digit after a "just 0" means "0X" — ambiguous.
            if (current_octet_is_just_zero) {
                return false;
            }
            const uint8_t digit_value = (uint8_t)(character - '0');
            if (!any_digit_in_current_octet && digit_value == 0) {
                current_octet_is_just_zero = true;
            }
            current_octet_value = (uint16_t)(current_octet_value * 10u + digit_value);
            if (current_octet_value > 255u) {
                return false;
            }
            any_digit_in_current_octet = true;
            continue;
        }

        if (character == '.' || character == '\0') {
            if (!any_digit_in_current_octet) {
                return false;
            }
            if (octet_index >= 4) {
                return false;
            }
            parsed_octets[octet_index] = (uint8_t)current_octet_value;
            octet_index += 1;

            if (character == '\0') {
                break;
            }

            // Prepare next octet.
            any_digit_in_current_octet = false;
            current_octet_is_just_zero = false;
            current_octet_value = 0;
            continue;
        }

        // Any other character — including spaces — is invalid.
        return false;
    }

    if (octet_index != 4) {
        return false;
    }

    out[0] = parsed_octets[0];
    out[1] = parsed_octets[1];
    out[2] = parsed_octets[2];
    out[3] = parsed_octets[3];
    return true;
}