// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "led_strip.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "led_component.h"
#include "message_formats.h"
#include "shared_queue.hpp"
#include <definitions.h>

// NeoPixel/WS2812 configuratie
#define NUM_PIXELS 1

static const char *TAG = "LEDComponent";

static led_strip_handle_t strip = NULL;

void task_LED_component(void *pvParameters) {

    ESP_LOGI(TAG, "LED Component started");

    int brightness = 0;
    int fadeAmount = 5;
    const int interval = 20;

    led_state current_state = LED_STATE_BOOT;
    message_definition_t received_message;

    led_strip_config_t strip_config = {};
    strip_config.strip_gpio_num = LED_GPIO;
    strip_config.max_leds = NUM_PIXELS;

    led_strip_rmt_config_t rmt_config = {};
    rmt_config.resolution_hz = 10 * 1000 * 1000; // 10 MHz RMT resolution

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
    led_strip_clear(strip);

    for (;;) {
        // Check for new messages in the queue
        if (xQueueReceive(led_queue, &received_message, 0) == pdPASS) {
            if (received_message.type == MESSAGE_TYPE_LED_STATUS) {
                led_state requested_state = received_message.data.led_message.led_state_value;
                if (current_state == LED_STATE_FATAL_ERROR && requested_state != LED_STATE_FATAL_ERROR) {
                    ESP_LOGW(TAG, "Ignoring LED state %d because fatal error is active", requested_state);
                }
                else {
                    current_state = requested_state;
                    ESP_LOGI(TAG, "Received new LED state: %d", current_state);
                }
            }
        }

        brightness += fadeAmount;

        if (brightness <= 20 || brightness >= 245) {
            fadeAmount = -fadeAmount;
            if (brightness < 20)
                brightness = 20;
            if (brightness > 245)
                brightness = 245;
        }

        // Apply color based on current state
        switch (current_state) {
            case LED_STATE_BOOT:
                led_strip_set_pixel(strip, 0, brightness, brightness, brightness); // White
                break;
            case LED_STATE_WAITING_FOR_WIFI:
                led_strip_set_pixel(strip, 0, 0, 0, brightness); // Blue
                break;
            case LED_STATE_WAITING_FOR_ETHERNET:
                led_strip_set_pixel(strip, 0, brightness, 0, brightness); // Magenta
                break;
            case LED_STATE_WAITING_FOR_DEVICE:
                led_strip_set_pixel(strip, 0, 0, brightness, brightness); // Cyan
                break;
            case LED_STATE_ALL_OK:
                led_strip_set_pixel(strip, 0, 0, brightness, 0); // Green
                break;
            case LED_STATE_WARNING:
                led_strip_set_pixel(strip, 0, brightness, brightness / 2, 0); // Orange/Yellow
                break;
            case LED_STATE_FATAL_ERROR:
                led_strip_set_pixel(strip, 0, brightness, 0, 0); // Red
                break;
            default:
                led_strip_set_pixel(strip, 0, 0, 0, 0); // Off
                break;
        }

        led_strip_refresh(strip);

        // Wait 20ms. If it's busy, other tasks can be handled as well
        vTaskDelay(pdMS_TO_TICKS(interval));
    }
}

void set_LED_state(led_state value) {
    message_definition_t message;
    message.type = MESSAGE_TYPE_LED_STATUS;
    message.data.led_message.led_state_value = value;
    xQueueSend(led_queue, &message, portMAX_DELAY);
}
