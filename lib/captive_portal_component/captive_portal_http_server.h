// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

/**
 * @file captive_portal_http_server.h
 * @brief esp_http_server lifecycle for the captive portal.
 *
 * Brings up the httpd instance, hands it to captive_portal_http_handlers to
 * register all URIs, and tears it down again on stop. This file knows
 * nothing about which URIs exist — that's the handler file's job.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the captive-portal HTTP server.
 *
 * Calls @c httpd_start with our tuned config (raised stack, max URI count,
 * LRU purge), then hands the server handle to
 * @ref captive_portal_http_handlers_register so it can wire all routes.
 *
 * @return true on success, false if httpd_start failed.
 */
bool captive_portal_http_server_start(void);

/**
 * @brief Stop the captive-portal HTTP server, if running.
 *
 * Safe to call multiple times; no-op if the server was never started or has
 * already been stopped.
 */
void captive_portal_http_server_stop(void);

#ifdef __cplusplus
}
#endif
