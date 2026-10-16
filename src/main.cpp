// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <cstdint>
#include <cstring>
#include <stdio.h>
#include <time.h>

#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "ems_bridge.h"
#include "led_component.h"
#include "message_formats.h"
#include "shared_queue.hpp"
#include "time_component.h"
#include <captive_portal_component.h>
#include <definitions.h>
#include <eth_wifi_component.h>
#include <modbus_component.h>
#include <modbus_mapping_component.h>
#include <storage_component.h>

static const char *TAG = "MAIN";

// define queues
QueueHandle_t led_queue = NULL;
QueueHandle_t modbus_mapping_queue = NULL;

// define taskhandles
TaskHandle_t LEDTaskHandle = NULL;
TaskHandle_t S2TaskHandle = NULL;

// some helper functions

/**
 * fill the ems_applicance_config
 * @param appliance_config
 * @param ems_appliance_config
 */
static void fill_ems_appliance_config(const appliance_config_t &appliance_config, ems_bridge_setup_values *ems_appliance_config) {
    memset(ems_appliance_config, 0, sizeof(*ems_appliance_config));
    strlcpy(ems_appliance_config->brand, "Unknown", sizeof(ems_appliance_config->brand));
    strlcpy(ems_appliance_config->logo_uri, "", sizeof(ems_appliance_config->logo_uri));
    strlcpy(ems_appliance_config->type, "modbus-appliance", sizeof(ems_appliance_config->type));
    strlcpy(ems_appliance_config->model, appliance_config.model.empty() ? "Unknown" : appliance_config.model.c_str(), sizeof(ems_appliance_config->model));
    strlcpy(ems_appliance_config->user_defined_name, appliance_config.name.empty() ? "S2 Modbus device" : appliance_config.name.c_str(), sizeof(ems_appliance_config->user_defined_name));
    uint8_t phase_count = appliance_config.num_phases;

    if (phase_count == 0) {
        phase_count = DEVICE_DEFAULT_PHASES;
    }
    if (phase_count > EMS_BRIDGE_PHASES_MAX_AMOUNT) {
        phase_count = EMS_BRIDGE_PHASES_MAX_AMOUNT;
    }

    ems_appliance_config->control.phase_count = phase_count;
    ems_appliance_config->control.instruction_processing_delay_ms = appliance_config.instruction_delay_s * 1000;
    ems_appliance_config->control.power_constraint_defer = appliance_config.defer_consequence;
    ems_appliance_config->control.phase_symmetric = appliance_config.phase_symmetric;
    size_t role_count = appliance_config.roles.size();

    if (role_count > EMS_BRIDGE_ROLES_MAX_AMOUNT) {
        role_count = EMS_BRIDGE_ROLES_MAX_AMOUNT;
    }
    for (size_t i = 0; i < role_count; i++) {
        ems_appliance_config->control.roles[i] = appliance_config.roles[i];
    }
    for (size_t i = role_count; i < EMS_BRIDGE_ROLES_MAX_AMOUNT; i++) {
        ems_appliance_config->control.roles[i] = EMS_BRIDGE_ROLE_SENTINEL;
    }
    for (uint8_t i = 0; i < phase_count; i++) {
        ems_appliance_config->control.default_limits[i].power_upper_limit = 0.0f;
        ems_appliance_config->control.default_limits[i].power_lower_limit = 0.0f;
        ems_appliance_config->control.default_limits[i].energy_upper_limit = 0.0f;
        ems_appliance_config->control.default_limits[i].energy_lower_limit = 0.0f;
    }
}

/**
 * init the share queueus
 */
void init_shared_queues() {

    led_queue = xQueueCreate(QUEUE_SIZE, sizeof(message_definition_t));
    if (led_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create LED queue!");
    }

    modbus_mapping_queue = xQueueCreate(QUEUE_SIZE, sizeof(message_definition_t));
    if (modbus_mapping_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create Modbus Mapping queue!");
    }
}

// Entry point for ESP-IDF
extern "C" void app_main(void) {

    BaseType_t taskCreated; // define one variable for tracking taskcreation

    vTaskDelay(pdMS_TO_TICKS(500)); // Give the console UART some time to settle
    ESP_LOGI(TAG, "--- System Started (ESP-IDF) ---");

    // Initialize the shared queue
    init_shared_queues();

    // Set the LED to the BOOT state
    set_LED_state(LED_STATE_BOOT);



    // LED Component will be on CORE 1 (Core 0 in ESP-IDF is reserved for WiFi/BLE)
    taskCreated = xTaskCreatePinnedToCore(task_LED_component, "LED_Task", 4096, NULL, 1, &LEDTaskHandle, 1);
    if (taskCreated == pdPASS) {
        ESP_LOGI(TAG, "Task created on Core 1");
    }
    else {
        ESP_LOGE(TAG, "Failed to create LED task");
        return;
    }

    // Initialize the storage
    if (!init_storage_component()) {
        ESP_LOGE(TAG, "System startup stopped because storage initialization failed");
        set_LED_state(LED_STATE_FATAL_ERROR);
        return;
    }

    // Init networking
    init_eth_wifi();

    // Apply the user-chosen default route: Ethernet when present, or WiFi STA when the captive portal "Use WiFi instead of Ethernet" flag is on.
    eth_wifi_apply_default_route();

    // init_eth_wifi() configures WiFi in APSTA mode and brings up the AP netif at 4.3.2.1/24.
    // The captive portal (DNS + HTTP) hooks onto that AP so the user can reach /config without an STA connection.
    if (!captive_portal_start()) {
        ESP_LOGE(TAG, "Captive portal failed to start");
    }

    // Update the clock after the network is available. TLS certificate validation depends on this.
    if (!time_component_obtain_time()) {
        ESP_LOGW(TAG, "Time synchronization failed; TLS/S2 communication may fail");
    }

    ESP_LOGI(TAG, "Wait till TCP socket is available... waiting 3 seconds");
    vTaskDelay(pdMS_TO_TICKS(3000)); // 3s

    // Load the appliance config from the JSON template (stored in NVS), if available
    appliance_config_t appliance_config = {};
    bool appliance_config_loaded = modbus_appliance_definition::load_config_from_storage(&appliance_config);
    if (!appliance_config_loaded) {
        ESP_LOGE(TAG, "Failed to load appliance configuration");
        set_LED_state(LED_STATE_FATAL_ERROR);
        return;
    }

    // Init the Modbus Component
    bool modbus_component_ready = false;
    if (appliance_config_loaded) {
        // only relevant if appliance config is loaded
        modbus_component_ready = modbus_component_init(appliance_config);
        if (!modbus_component_ready) {
            ESP_LOGE(TAG, "Failed to initialize Modbus component");
            set_LED_state(LED_STATE_FATAL_ERROR);
        }
    }


    if (modbus_component_ready && modbus_mapping_init_events()) {
        // Modbus Mapping Component will be on CORE 1 (Core 0 in ESP-IDF is reserved for WiFi/BLE)
        taskCreated = xTaskCreatePinnedToCore(task_modbus_mapping_component, "modbus_mapping_Task", 2 * MAX_JSON_SIZE + 4096, NULL, 1, &S2TaskHandle, 1);

        if (taskCreated == pdPASS) {
            ESP_LOGI(TAG, "Task created on Core 1");

            if (modbus_mapping_wait_until_modbus_ready()) {

                // The Modbus device is ready now, so EMS can use the resolved appliance config.
                ems_bridge_setup_values ems_appliance_config = {};
                fill_ems_appliance_config(modbus_mapping_get_definition().config, &ems_appliance_config);

                bool ems_bridge_ready = ems_bridge_init(
                    EMS_INTERFACE_S2_PEBC,
                    &ems_appliance_config,
                    ems_bridge_request_sensor_implementation,
                    ems_bridge_set_actuator_implementation,
                    modbus_mapping_on_ems_ready);

                if (!ems_bridge_ready) {
                    ESP_LOGE(TAG, "S2 library not started because EMS bridge initialization failed");
                    set_LED_state(LED_STATE_FATAL_ERROR);
                }
            }
            else {
                ESP_LOGE(TAG, "S2 library not started because Modbus Mapping is not ready");
                set_LED_state(LED_STATE_FATAL_ERROR);
            }
        }
        else {
            ESP_LOGE(TAG, "Failed to create Modbus Mapping task");
            set_LED_state(LED_STATE_FATAL_ERROR);
        }
    }
    else {
        ESP_LOGE(TAG, "S2 library not started because Modbus or Modbus Mapping events are not ready");
        set_LED_state(LED_STATE_FATAL_ERROR);
    }

    ESP_LOGI(TAG, "Reached of main initialization phase!");
}
