// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FreeRTOS.h"

typedef void (*TaskFunction_t)(void*);
typedef void* TaskHandle_t;

inline BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*) {
    return pdPASS;
}

inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, UBaseType_t, TaskHandle_t*, BaseType_t) {
    return pdPASS;
}

inline void vTaskDelay(TickType_t) {}
