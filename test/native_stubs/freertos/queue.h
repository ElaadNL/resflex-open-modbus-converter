// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "FreeRTOS.h"

#include <cstddef>
#include <cstring>
#include <deque>
#include <vector>

struct native_queue {
    std::deque<std::vector<unsigned char>> items;
    UBaseType_t max_items = 0;
    UBaseType_t item_size = 0;
};

typedef native_queue* QueueHandle_t;

inline QueueHandle_t xQueueCreate(UBaseType_t max_items, UBaseType_t item_size) {
    auto* q = new native_queue();
    q->max_items = max_items;
    q->item_size = item_size;
    return q;
}

inline void vQueueDelete(QueueHandle_t q) {
    delete q;
}

inline BaseType_t xQueueSend(QueueHandle_t q, const void* item, TickType_t) {
    if (q == nullptr) return pdFAIL;
    if (q->items.size() >= q->max_items) return pdFAIL;
    std::vector<unsigned char> entry(q->item_size);
    std::memcpy(entry.data(), item, q->item_size);
    q->items.push_back(std::move(entry));
    return pdPASS;
}

inline BaseType_t xQueueReceive(QueueHandle_t q, void* buffer, TickType_t) {
    if (q == nullptr || q->items.empty()) return pdFAIL;
    std::memcpy(buffer, q->items.front().data(), q->item_size);
    q->items.pop_front();
    return pdPASS;
}

inline UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) {
    return q ? static_cast<UBaseType_t>(q->items.size()) : 0;
}
