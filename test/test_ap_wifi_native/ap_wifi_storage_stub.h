// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace ap_wifi_storage_stub {
// Value a test seeds before calling (simulates a pre-existing NVS entry).
extern std::string seeded_password;
// Value recorded by storage_set_configuration (the generated password).
extern std::string generated_password;

void reset();
}
