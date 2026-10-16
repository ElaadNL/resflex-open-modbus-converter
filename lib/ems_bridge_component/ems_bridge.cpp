// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "ems_bridge.h"
#include "eth_wifi_component.h"
#include "led_component.h"
#include "s2_library_wrapper.h"
#include <definitions.h>

#include "storage_component.h"

static QueueHandle_t newSensorValueQueue;
static QueueHandle_t setActuatorResultQueue;
static const char *TAG = "ems_bridge_component";
static ems_bridge_interface_t hems_type;

/**
 * @brief Task that processes incoming sensor readings from the newSensorValueQueue.
 *
 * Blocks indefinitely until a new sensor result is available, then act on it
 *
 * @param parameters Unused task parameter.
 */
static void ems_bridge_new_sensor_value_task(void *parameters) {
    ems_bridge_sensor_result sensor_value;

    while (1) {
        if (xQueueReceive(newSensorValueQueue, &sensor_value, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Received sensorid=%d value=%.2f status=%i", sensor_value.sensorId, sensor_value.value, sensor_value.status);

            switch (hems_type) {
                case EMS_INTERFACE_S2_PEBC:
                    s2_on_new_sensor_value(sensor_value);
                    break;
            }
        }
        // Wait 20ms. If it's busy, other tasks can be handled as well
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}


/**
 * @brief Task that processes incoming actuator results from the setActuatorResultQueue.
 *
 * Blocks indefinitely until a new actuator result is available, then act on it
 *
 * @param parameters Unused task parameter.
 */
static void ems_bridge_set_actuator_result_task(void *parameters) {
    ems_bridge_set_actuator_result actuator_result;

    while (1) {
        if (xQueueReceive(setActuatorResultQueue, &actuator_result, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Received actuatorId=%d status=%i", actuator_result.actuatorId, actuator_result.status);

            switch (hems_type) {
                case EMS_INTERFACE_S2_PEBC:
                    s2_on_actuator_set(actuator_result);
                    break;
            }
        }
        // Wait 20ms. If it's busy, other tasks can be handled as well
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}



bool ems_bridge_send_new_sensor_value(ems_bridge_sensor_result *result) {
    if (xQueueSend(newSensorValueQueue, result, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "newSensorValueQueue full");
        return false;
    }
    return true;
}



bool ems_bridge_send_set_actuator_result(ems_bridge_set_actuator_result *result) {
    if (xQueueSend(setActuatorResultQueue, result, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "setActuatorResultQueue full");
        return false;
    }
    return true;
}


bool ems_bridge_init(
    ems_bridge_interface_t ems_type,
    ems_bridge_setup_values *appliance_config,
    ems_bridge_request_sensor_cb_t request_sensor_cb,
    ems_bridge_set_actuator_cb_t set_actuator_cb,
    ems_bridge_on_init_ready_cb_t on_init_ready_cb) {

    // set the hems type
    hems_type = ems_type;

    newSensorValueQueue = xQueueCreate(10, sizeof(ems_bridge_sensor_result));
    setActuatorResultQueue = xQueueCreate(10, sizeof(ems_bridge_set_actuator_result));

    if (newSensorValueQueue == nullptr) {
        ESP_LOGE(TAG, "Failed to create newSensorValueQueue");
        set_LED_state(LED_STATE_FATAL_ERROR);
        return false;
    }
    if (setActuatorResultQueue == nullptr) {
        ESP_LOGE(TAG, "Failed to create setActuatorResultQueue");
        set_LED_state(LED_STATE_FATAL_ERROR);
        return false;
    }

    xTaskCreatePinnedToCore(ems_bridge_new_sensor_value_task, "newSensorValueTask", 4096, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(ems_bridge_set_actuator_result_task, "setActuatorResultTask", 4096, nullptr, 4, nullptr, 1);

    switch (hems_type) {
        case EMS_INTERFACE_S2_PEBC:

            // hems config values
            s2_hems_values hems_values = {};
            storage_get_configuration(NVS_STORAGE_KEY_HEMS_URL, hems_values.base_url, sizeof(hems_values.base_url), "http://homeassistant.local:8123/api/s2_pebc");
            storage_get_configuration(NVS_STORAGE_KEY_HEMS_PAIR_KEY, hems_values.pairing_input_token, sizeof(hems_values.pairing_input_token), "RECEIVE_FROM_HEMS");

            // TLS: stored as uint16_t in NVS (same shape as ip_static); narrow
            // to the bool field on the struct. Only load cert_pem when TLS is
            // on; otherwise leave the field empty so downstream code doesn't
            // act on a stale certificate.
            uint16_t tls_enabled_flag = 0;
            storage_get_configuration(NVS_STORAGE_KEY_HEMS_TLS_ENABLED, tls_enabled_flag, (uint16_t)0);
            hems_values.tls_enabled = (tls_enabled_flag != 0);
            if (hems_values.tls_enabled) {
                storage_get_configuration(NVS_STORAGE_KEY_HEMS_TLS_PEM, hems_values.cert_pem, sizeof(hems_values.cert_pem), "");
            }
            else {
                hems_values.cert_pem[0] = '\0';
            }

            ESP_LOGI(TAG, "Start pairing with url: %s (tls=%d)", hems_values.base_url, (int)hems_values.tls_enabled);
            ESP_LOGD(TAG, "Pairing key: %s", hems_values.pairing_input_token);

            if (!eth_wifi_get_active_interface_name(hems_values.interface, sizeof(hems_values.interface))) {
                ESP_LOGW(TAG, "No active interface name available. S2 traffic will use default routing.");
            }
            else {
                ESP_LOGI(TAG, "S2 traffic bound to interface %s", hems_values.interface);
            }

            // init s2
            esp_err_t err = s2_init(&hems_values, appliance_config, request_sensor_cb, set_actuator_cb, on_init_ready_cb);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "S2 init failed: %s", esp_err_to_name(err));
                set_LED_state(LED_STATE_FATAL_ERROR);
                return false;
            }

            break;
    }

    return true;
}
