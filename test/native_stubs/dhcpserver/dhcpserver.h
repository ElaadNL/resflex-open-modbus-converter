// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Native-test stub for dhcpserver/dhcpserver.h.
//
// ap_wifi_configure_netif() declares `dhcps_offer_t offer_dns = OFFER_DNS;`
// so OFFER_DNS must be a value assignable to dhcps_offer_t. In ESP-IDF
// dhcps_offer_t is an enum of bit flags; we reproduce the one flag the
// component uses.

typedef enum {
    OFFER_TIME = 0x0001,
    OFFER_DNS = 0x0004,
    OFFER_NTP = 0x0008
} dhcps_offer_t;
