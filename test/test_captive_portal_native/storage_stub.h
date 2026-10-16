// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>

// Test-side helpers for inspecting the in-memory storage stub used by the
// captive-portal POST tests.

namespace captive_test_storage {

void reset();

bool has_string(const std::string& key);
bool has_u16(const std::string& key);

const std::string& get_string(const std::string& key);
uint16_t get_u16(const std::string& key);

void seed_string(const std::string& key, const std::string& value);

}
