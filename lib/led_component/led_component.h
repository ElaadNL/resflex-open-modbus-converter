// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

enum led_state {
    LED_STATE_BOOT,
    LED_STATE_WAITING_FOR_WIFI,
    LED_STATE_WAITING_FOR_ETHERNET,
    LED_STATE_WAITING_FOR_DEVICE,
    LED_STATE_ALL_OK,
    LED_STATE_WARNING,
    LED_STATE_FATAL_ERROR
};

/**
 * @brief FreeRTOS task entry point for LED status handling.
 *
 * Initializes the LED hardware and processes LED state messages from
 * `led_queue`. Start this with `xTaskCreatePinnedToCore()`.
 *
 * @param pvParameters Unused FreeRTOS task parameter.
 */
extern void task_LED_component(void *pvParameters);

/**
 * @brief Request a visible LED status change.
 *
 * Sends the desired state to the LED task via `led_queue`; the caller does
 * not drive the LED hardware directly.
 *
 * @param value Desired LED state.
 */
extern void set_LED_state(led_state value);
