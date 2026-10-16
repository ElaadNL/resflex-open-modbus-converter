// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "captive_portal_http_routes.h"

const captive_route_t CAPTIVE_ROUTES[] = {
    // Android captive portal probes
    {             "/generate_204", CAPTIVE_ROUTE_REDIRECT},
    {              "/generate204", CAPTIVE_ROUTE_REDIRECT},
    {                  "/gen_204", CAPTIVE_ROUTE_REDIRECT},

    // Apple (iOS/macOS) captive portal probes
    {      "/hotspot-detect.html", CAPTIVE_ROUTE_REDIRECT},
    {           "/hotspot-detect", CAPTIVE_ROUTE_REDIRECT},
    {"/library/test/success.html", CAPTIVE_ROUTE_REDIRECT},

    // Microsoft Windows network connectivity probes
    {                 "/ncsi.txt", CAPTIVE_ROUTE_REDIRECT},
    {          "/connecttest.txt", CAPTIVE_ROUTE_REDIRECT},
    {                 "/redirect", CAPTIVE_ROUTE_REDIRECT},

    // Firefox captive portal probe. /success.txt MUST be 200 OK — Firefox
    // checks the *body* matches "success\n" to decide "no captive portal".
    // Anything else (including a 302 to /config) flags the network as
    // captive and triggers the doorhanger / notification.
    {           "/canonical.html", CAPTIVE_ROUTE_REDIRECT},
    {              "/success.txt",      CAPTIVE_ROUTE_200},

    // Noisy probes we silence with a 404 to stop clients from hammering us.
    {                 "/wpad.dat",      CAPTIVE_ROUTE_404},
    {              "/favicon.ico",      CAPTIVE_ROUTE_404},
};
const size_t CAPTIVE_ROUTES_COUNT = sizeof(CAPTIVE_ROUTES) / sizeof(CAPTIVE_ROUTES[0]);
