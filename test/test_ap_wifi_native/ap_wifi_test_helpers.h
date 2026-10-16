// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"

namespace ap_wifi_test_helpers {

// Reset every transport capture used by the ap_wifi tests.
void reset_all() {
    ap_wifi_test::reset();
}

}

namespace ap_wifi_storage_stub {
void reset();
}
