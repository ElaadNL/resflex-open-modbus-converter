// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#ifndef SHARED_QUEUE_H
#define SHARED_QUEUE_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/** @brief Default queue depth used by shared component queues. */
#define QUEUE_SIZE 10

/**
 * @brief Queue carrying LED status messages to the LED component task.
 */
extern QueueHandle_t led_queue;

/**
 * @brief Queue carrying S2-originated sensor/actuator requests to Modbus Mapping.
 */
extern QueueHandle_t modbus_mapping_queue;

#endif
