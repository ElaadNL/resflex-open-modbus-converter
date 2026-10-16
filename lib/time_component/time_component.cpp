// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "time_component.h"

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "esp_sntp.h" // Contains the correct esp_sntp_ functions

static const char *TAG = "TimeComponent";
static const time_t MIN_VALID_TIME = 1700000000; // 2023-11-14, safely beyond the Unix epoch default.

void time_component_initialize_sntp(void);

static bool system_time_is_valid(void) {
    time_t now = 0;
    time(&now);
    return now >= MIN_VALID_TIME;
}

void time_component_time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void time_component_initialize_sntp(void) {
    ESP_LOGI(TAG, "Initializing SNTP");

    // In ESP-IDF v5.x, the APIs are prefixed with 'esp_'
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_component_time_sync_notification_cb);

#ifdef CONFIG_SNTP_TIME_SYNC_METHOD_SMOOTH
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_SMOOTH);
#endif

    esp_sntp_init();
}

bool time_component_obtain_time(void) {
    time_component_initialize_sntp();

    // Wait until the time has been set via NTP
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 15; // Slightly increased (30 seconds total) for slower networks

    while (!system_time_is_valid() && retry < retry_count) {
        retry++;
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    if (!system_time_is_valid()) {
        ESP_LOGW(TAG, "System time was not synchronized after %d attempts", retry_count);
        return false;
    }

    // Once time is available, set the timezone for the Netherlands (CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    // Get the updated time
    time(&now);
    localtime_r(&now, &timeinfo);

    // Print the time to the log for testing
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Local time: %s", strftime_buf);

    return true;
}
