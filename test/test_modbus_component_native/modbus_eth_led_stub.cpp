// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

// Linker-only stubs for the modbus native env.
//
// modbus_component.cpp references eth_wifi_get_active_netif() and
// set_LED_state(), but the real definitions live in eth_wifi_component.cpp
// and led_component.cpp which are not part of the native build. We provide
// minimal implementations: the netif resolves to nullptr (so the TCP path
// uses OS default routing) and the LED state change is a no-op.

#include "eth_wifi_component.h"
#include "led_component.h"

esp_netif_t *eth_wifi_get_active_netif() {
    return nullptr;
}

void set_LED_state(led_state) {}
