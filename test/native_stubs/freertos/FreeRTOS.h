// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <cstddef>

typedef int BaseType_t;
typedef unsigned int UBaseType_t;
typedef uint32_t TickType_t;
typedef uint32_t EventBits_t;

#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS  pdTRUE
#define pdFAIL  pdFALSE

#define portMAX_DELAY ((TickType_t)0xFFFFFFFF)
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#define BIT0 ((EventBits_t)0x01)
#define BIT1 ((EventBits_t)0x02)
#define BIT2 ((EventBits_t)0x04)
#define BIT3 ((EventBits_t)0x08)
#define BIT4 ((EventBits_t)0x10)
