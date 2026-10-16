// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "captive_portal_parsing.h"

#include <string.h>

/**
 * @brief Convert a single hex digit to its 0..15 value.
 * @param c ASCII digit (`0-9`, `a-f`, or `A-F`).
 * @return Numeric value, or -1 if @p c is not a valid hex digit.
 */
static int hex_nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return 10 + c - 'a';
    if (c >= 'A' && c <= 'F')
        return 10 + c - 'A';
    return -1;
}

void captive_url_decode(char *s) {
    char *r = s;
    char *w = s;
    while (*r) {
        if (*r == '+') {
            *w++ = ' ';
            r++;
        }
        else if (*r == '%' && r[1] && r[2]) {
            int hi = hex_nibble(r[1]);
            int lo = hex_nibble(r[2]);
            if (hi >= 0 && lo >= 0) {
                *w++ = (char)((hi << 4) | lo);
                r += 3;
            }
            else {
                *w++ = *r++;
            }
        }
        else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

bool captive_form_field(char *body, const char *name, char *out, size_t out_size, bool *out_truncated) {
    if (out_truncated)
        *out_truncated = false;
    size_t name_len = strlen(name);
    char *p = body;
    while (*p) {
        // p must point at the start of a key. Look for "name="
        if (strncmp(p, name, name_len) == 0 && p[name_len] == '=') {
            const char *val = p + name_len + 1;
            // Find end of this field (next '&' or end of string). We work
            // with byte ranges instead of mutating the body so a decoded
            // value that contains '&' (e.g. from %26) doesn't corrupt the
            // body for subsequent lookups.
            const char *end = strchr(val, '&');
            size_t val_len = end ? (size_t)(end - val) : strlen(val);

            // Copy the encoded value to out (truncating), then url-decode
            // there in place. captive_url_decode only ever shrinks the
            // string, so it's safe to do inside out_size.
            if (out_size > 0) {
                size_t copy_len = val_len < out_size - 1 ? val_len : out_size - 1;
                if (out_truncated && val_len > out_size - 1) {
                    *out_truncated = true;
                }

                memcpy(out, val, copy_len);
                out[copy_len] = '\0';
                captive_url_decode(out);
            }
            return true;
        }
        // Advance to the next key (after the next '&').
        char *next = strchr(p, '&');
        if (!next)
            return false;
        p = next + 1;
    }
    return false;
}

void captive_html_escape(const char *in, char *out, size_t out_size) {
    if (out_size == 0)
        return;
    size_t o = 0;
    for (size_t i = 0; in[i] != '\0'; i++) {
        const char *repl = nullptr;
        switch (in[i]) {
            case '&':
                repl = "&amp;";
                break;
            case '<':
                repl = "&lt;";
                break;
            case '>':
                repl = "&gt;";
                break;
            case '"':
                repl = "&quot;";
                break;
            case '\'':
                repl = "&#39;";
                break;
            default:
                break;
        }
        if (repl) {
            size_t rl = strlen(repl);
            if (o + rl >= out_size)
                break;
            memcpy(out + o, repl, rl);
            o += rl;
        }
        else {
            if (o + 1 >= out_size)
                break;
            out[o++] = in[i];
        }
    }
    out[o] = '\0';
}

bool captive_multipart_boundary(const char *content_type, char *out, size_t out_size) {
    if (out_size == 0 || content_type == nullptr)
        return false;
    out[0] = '\0';

    const char *p = strstr(content_type, "boundary=");
    if (!p)
        return false;
    p += 9; // strlen("boundary=")

    // Boundary may be quoted per RFC 2046.
    bool quoted = (*p == '"');
    if (quoted)
        p++;

    size_t i = 0;
    while (*p && i + 1 < out_size) {
        if (quoted && *p == '"')
            break;
        if (!quoted && (*p == ';' || *p == ' ' || *p == '\r' || *p == '\n'))
            break;
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

/**
 * @brief Memory-safe substring search (memmem-equivalent).
 *
 * Exists because @c memmem is a GNU extension not available on every host
 * the native test env runs on. Linear scan; fine for the small bodies we
 * accept (4KB-ish).
 */
static const char *find_bytes(const char *haystack, size_t haystack_len, const char *needle, size_t needle_len) {
    if (needle_len == 0 || haystack_len < needle_len)
        return nullptr;
    for (size_t i = 0; i + needle_len <= haystack_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return haystack + i;
        }
    }
    return nullptr;
}

bool captive_multipart_file_part(const char *body, size_t body_len, const char *boundary, const char **out_data, size_t *out_len) {
    if (!body || !boundary || !out_data || !out_len)
        return false;

    // Delimiter as it appears on the wire: "--" + boundary.
    char delim[128];
    // Read one byte more than we accept so an oversized boundary is
    // detectable instead of silently truncated to the buffer limit.
    size_t blen = strnlen(boundary, sizeof(delim) - 2);
    // Reject empty or oversized boundary (leaves room for "--" prefix and NUL).
    if (blen == 0 || blen > sizeof(delim) - 3)
        return false;
    delim[0] = '-';
    delim[1] = '-';
    memcpy(delim + 2, boundary, blen);
    delim[blen + 2] = '\0';
    size_t delim_len = blen + 2;

    const char *cursor = body;
    size_t remaining = body_len;

    while (remaining > 0) {
        const char *d = find_bytes(cursor, remaining, delim, delim_len);
        if (!d)
            return false;

        // Skip past the delimiter and the CRLF (or "--" terminator) that follows.
        const char *after = d + delim_len;
        size_t after_off = (size_t)(after - body);
        if (after_off + 2 > body_len)
            return false;
        // Final boundary is "--boundary--"; nothing more to parse.
        if (after[0] == '-' && after[1] == '-')
            return false;
        // Otherwise expect CRLF then headers.
        if (after[0] != '\r' || after[1] != '\n') {
            cursor = after;
            remaining = body_len - after_off;
            continue;
        }
        const char *headers = after + 2;
        size_t headers_off = (size_t)(headers - body);
        if (headers_off >= body_len)
            return false;

        // Headers end at "\r\n\r\n".
        const char *hdr_end = find_bytes(headers, body_len - headers_off, "\r\n\r\n", 4);
        if (!hdr_end)
            return false;

        // Only accept parts that carry a filename — that's the JSON upload.
        size_t hdr_len = (size_t)(hdr_end - headers);
        bool is_file = false;
        for (size_t i = 0; i + 8 < hdr_len; i++) {
            if (memcmp(headers + i, "filename", 8) == 0) {
                is_file = true;
                break;
            }
        }

        const char *payload = hdr_end + 4;
        size_t payload_off = (size_t)(payload - body);
        // Next boundary marks the end of this payload (preceded by CRLF).
        const char *next = find_bytes(payload, body_len - payload_off, delim, delim_len);
        if (!next)
            return false;
        // Strip the trailing CRLF that belongs to the boundary, not the payload.
        size_t plen = (size_t)(next - payload);
        if (plen >= 2 && payload[plen - 2] == '\r' && payload[plen - 1] == '\n') {
            plen -= 2;
        }

        if (is_file) {
            *out_data = payload;
            *out_len = plen;
            return true;
        }

        // Not a file part — keep scanning from the next delimiter.
        cursor = next;
        remaining = body_len - (size_t)(next - body);
    }
    return false;
}
