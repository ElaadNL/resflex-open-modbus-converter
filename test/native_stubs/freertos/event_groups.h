// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FreeRTOS.h"

typedef void* EventGroupHandle_t;

inline EventGroupHandle_t xEventGroupCreate() { return nullptr; }
inline EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t bits) { return bits; }
inline EventBits_t xEventGroupWaitBits(EventGroupHandle_t, EventBits_t bits, BaseType_t, BaseType_t, TickType_t) { return bits; }
inline EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t bits) { return bits; }
