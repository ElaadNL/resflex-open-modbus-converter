// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * @file captive_portal_parsing.h
 * @brief Pure (ESP-independent) parsing helpers used by the captive portal.
 *
 * Kept free of ESP-IDF / lwIP / NVS dependencies on purpose so they can be
 * linked into the native unit-test environment (see `env:native_captive` in
 * platformio.ini). The functions are reused by the runtime captive-portal
 * code on-device.
 *
 * NVS-write policies (apply_form_to_nvs, apply_appliance_params_to_nvs,
 * load_escaped) live in captive_portal_nvs.h.
 *
 * The DNS A-response builder lives in captive_portal_dns_message.h.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief In-place URL-decode of a NUL-terminated string.
 *
 * Replacement rules:
 *   - `'+'`   → `' '`
 *   - `'%XX'` → byte from two hex digits (case-insensitive)
 *
 * Malformed `'%XX'` sequences (non-hex digits, or truncated at the end of
 * the string) are left as-is. The resulting string is shorter than or
 * equal to the input, so decoding in place is always safe.
 *
 * @param s NUL-terminated string to decode in place.
 */
void captive_url_decode(char *s);

/**
 * @brief Look up a field in an application/x-www-form-urlencoded body.
 *
 * Searches @p body for a `name=value` pair, URL-decodes the value, and
 * copies it (truncating if needed) into @p out. Matching is whole-key:
 * the substring before `=` must equal @p name exactly, so a field named
 * "ssid" won't accidentally match against the value of another field.
 *
 * @note The body is *not* mutated — the value is copied to @p out and
 *       decoded there in place. This means multiple lookups on the same
 *       body are safe even if a previous value contained an encoded `&`.
 *
 * @param body     The form body (mutated; must be writable, NUL-terminated).
 * @param name     The field name to find.
 * @param out      Destination buffer for the decoded value.
 * @param out_size Size of @p out in bytes (output is always NUL-terminated
 *                 if @p out_size > 0; value is silently truncated to fit).
 * @param out_truncated Optional; if non-null, set to true when the value did
 *                 not fit in @p out and was truncated (the encoded value was
 *                 longer than @p out_size - 1). Lets callers reject a
 *                 truncated payload instead of persisting a corrupt copy.
 * @return true if the field was present (even if its value is empty),
 *         false otherwise.
 */
bool captive_form_field(char *body, const char *name, char *out, size_t out_size, bool *out_truncated = nullptr);

/**
 * @brief HTML-escape a NUL-terminated string into @p out.
 *
 * Replaces the five HTML-sensitive characters with their entities:
 *   `&` → `&amp;`, `<` → `&lt;`, `>` → `&gt;`, `"` → `&quot;`, `'` → `&#39;`.
 *
 * Truncates silently if the escaped result would overflow @p out_size —
 * acceptable for UI pre-fill, which is best-effort.
 *
 * @param in       Source NUL-terminated string.
 * @param out      Destination buffer (always NUL-terminated if @p out_size > 0).
 * @param out_size Size of @p out in bytes.
 */
void captive_html_escape(const char *in, char *out, size_t out_size);

/**
 * @brief Extract the first file part payload from a multipart/form-data body.
 *
 * Scans @p body for the boundary delimiter @p boundary (without the leading
 * `--`), locates the first part that carries a `Content-Disposition` header
 * with a `filename=` parameter, and returns a pointer to the start of that
 * part's payload along with its length. The payload is the raw bytes
 * between the blank line that ends the part headers and the trailing CRLF
 * that precedes the next boundary delimiter.
 *
 * No copying is done — @p out_data points into @p body. The byte after the
 * payload is *not* NUL-terminated by this function; callers that need a
 * C string must copy and terminate themselves.
 *
 * @param body         The full multipart body (read-only).
 * @param body_len     Length of @p body in bytes.
 * @param boundary     Boundary string from the Content-Type header (without
 *                     the leading `--`).
 * @param out_data     On success, receives a pointer to the payload start
 *                     inside @p body.
 * @param out_len      On success, receives the payload length in bytes.
 * @return true if a file part was found and parsed, false otherwise.
 */
bool captive_multipart_file_part(const char *body, size_t body_len,
                                 const char *boundary,
                                 const char **out_data, size_t *out_len);

/**
 * @brief Extract the boundary parameter from a Content-Type header value.
 *
 * Parses @p content_type looking for a `boundary=` parameter and copies the
 * boundary string (without surrounding quotes if present) to @p out.
 *
 * @param content_type Full Content-Type header value, e.g.
 *                     `multipart/form-data; boundary=----WebKitFormBoundary…`.
 * @param out          Destination buffer for the boundary (NUL-terminated).
 * @param out_size     Size of @p out in bytes.
 * @return true if a boundary was found and fit in @p out, false otherwise.
 */
bool captive_multipart_boundary(const char *content_type,
                                char *out, size_t out_size);

#ifdef __cplusplus
}
#endif
