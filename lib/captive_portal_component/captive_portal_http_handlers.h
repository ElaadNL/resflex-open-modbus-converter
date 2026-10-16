// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "esp_http_server.h"

#include "captive_portal_http_routes.h"

/**
 * @file captive_portal_http_handlers.h
 * @brief HTTP controller for the captive portal.
 *
 * Owns:
 *   - the per-URI handler functions
 *   - the page-rendering helpers (HTML composition)
 *   - the registration entry point used by captive_portal_http_server
 *
 * The route-table types and the CAPTIVE_ROUTES array are declared in
 * captive_portal_http_routes.h so the native test env can include them
 * without pulling in esp_http_server. The array itself is defined at the
 * top of captive_portal_http_handlers.cpp.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Signature of the URI-registration helper that the http_server
 *        component supplies. Lets the handler file register its own URIs
 *        without depending on httpd_uri_t internals.
 */
typedef void (*captive_portal_register_uri_fn)(httpd_handle_t httpd,
                                               const char *uri,
                                               httpd_method_t method,
                                               esp_err_t (*handler)(httpd_req_t *));

/**
 * @brief Register every captive-portal URI with @p httpd.
 *
 * Called by captive_portal_http_server_start() right after @c httpd_start.
 * Walks the internal route table (OS phone-home probes + the portal's own
 * `/config` and `/appliance` pages + catch-all `/`) and registers each
 * entry via the supplied @p register_uri helper.
 *
 * @param httpd        Active esp_http_server handle.
 * @param register_uri Helper supplied by the http_server component that
 *                     fills in the httpd_uri_t boilerplate. Must not be
 *                     nullptr.
 */
void captive_portal_http_handlers_register(httpd_handle_t httpd,
                                           captive_portal_register_uri_fn register_uri);

#ifdef __cplusplus
}
#endif
