// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

/**
 * @file captive_portal_html_fragments.h
 * @brief Raw HTML / CSS / JS string literals for the captive-portal pages.
 *
 * Pure data. The render helpers in captive_portal_http_handlers.cpp pull
 * these in either via httpd_resp_send_chunk(.., HTTPD_RESP_USE_STRLEN) for
 * the literals without placeholders, or via snprintf for the literals with
 * `%s` / `%u` placeholders.
 *
 * IMPORTANT: do not put bare `%` characters in the literals that go through
 * snprintf — any stray `%` would be interpreted as a conversion. The CSS
 * `100%` in CAPTIVE_HTML_COMMON_STYLE is only ever streamed directly, so it
 * is safe there.
 */

#ifdef __cplusplus
extern "C" {
#endif

extern const char CAPTIVE_HTML_COMMON_STYLE[];
extern const char CAPTIVE_HTML_HEAD_OPEN[];
extern const char CAPTIVE_HTML_HEAD_CLOSE_CONFIG[];
extern const char CAPTIVE_HTML_FIELDSET_WIFI[];
extern const char CAPTIVE_HTML_FIELDSET_S2[];
extern const char CAPTIVE_HTML_FIELDSET_NETWORK[];
extern const char CAPTIVE_HTML_TAIL[];

extern const char CAPTIVE_HTML_HEAD_CLOSE_APPLIANCE[];
extern const char CAPTIVE_HTML_APPLIANCE_COUNTER[];
extern const char CAPTIVE_HTML_APPLIANCE_TEXTAREA_OPEN[];
extern const char CAPTIVE_HTML_APPLIANCE_TEXTAREA_CLOSE[];
extern const char CAPTIVE_HTML_APPLIANCE_PARAMETERS[];
extern const char CAPTIVE_HTML_APPLIANCE_TAIL[];
extern const char CAPTIVE_HTML_APPLIANCE_SAVED[];

#ifdef __cplusplus
}
#endif
