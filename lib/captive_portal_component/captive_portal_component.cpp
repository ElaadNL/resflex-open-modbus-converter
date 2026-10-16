// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "captive_portal_component.h"

#include "esp_log.h"

#include "../eth_wifi_component/ap_wifi.h"
#include "captive_portal_dns.h"
#include "captive_portal_http_server.h"

static const char *TAG = "CAPTIVE";

bool captive_portal_start(void) {
    // Pre-condition: eth_wifi_component has set WIFI_MODE_APSTA, created
    // both netifs and called esp_wifi_start(). Now we layer the captive-
    // portal-specific config on top.

    if (!captive_portal_dns_start()) {
        ESP_LOGE(TAG, "DNS responder failed to start");
        return false;
    }
    if (!captive_portal_http_server_start()) {
        captive_portal_dns_stop();
        return false;
    }
    ESP_LOGI(TAG, "Captive portal up on http://%d.%d.%d.%d/config",
             AP_WIFI_IP_A, AP_WIFI_IP_B,
             AP_WIFI_IP_C, AP_WIFI_IP_D);
    return true;
}

void captive_portal_stop(void) {
    captive_portal_http_server_stop();
    captive_portal_dns_stop();
}
