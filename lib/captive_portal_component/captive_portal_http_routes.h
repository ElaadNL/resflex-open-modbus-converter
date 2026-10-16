// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>

/**
 * @file captive_portal_http_routes.h
 * @brief OS phone-home route table for the captive portal.
 *
 * Types-and-data only — no esp_http_server dependency — so the native test
 * env can walk the table without pulling in the httpd stack. The actual
 * array definition lives in captive_portal_http_handlers.cpp; the per-URI
 * handler functions and registration entry point live in
 * captive_portal_http_handlers.h.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief How the captive portal should respond to a probe URI. */
typedef enum {
    /** Send an HTTP 302 redirect to the `/config` page so the OS captive sheet pops up. */
    CAPTIVE_ROUTE_REDIRECT,
    /** Send a 404 to silence noisy probes that we don't want to redirect. */
    CAPTIVE_ROUTE_404,
    /** Send a 200 OK with empty body — Firefox needs this for /success.txt to flag "captive portal" instead of "no network". */
    CAPTIVE_ROUTE_200,
    /** Send a 204 No Content — Android (Samsung especially) treats anything else on /generate_204 as "no internet". */
    CAPTIVE_ROUTE_204,
} captive_route_behavior_t;

/** @brief One row in the routing table. */
typedef struct {
    const char *uri;
    captive_route_behavior_t behavior;
} captive_route_t;

/**
 * @brief Static route table for OS phone-home URIs.
 *
 * The portal's own pages (`/config`, `/appliance`) and the catch-all `/` are
 * registered separately by @ref captive_portal_http_handlers_register —
 * this table only covers the per-OS probe URIs.
 *
 * Order is stable so the native test env can rely on it for table-driven
 * assertions.
 */
extern const captive_route_t CAPTIVE_ROUTES[];

/** @brief Number of entries in @ref CAPTIVE_ROUTES. */
extern const size_t CAPTIVE_ROUTES_COUNT;

#ifdef __cplusplus
}
#endif
