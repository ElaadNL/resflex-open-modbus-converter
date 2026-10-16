// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @file captive_portal_dns_message.h
 * @brief Pure (ESP-independent) DNS A-response builder.
 *
 * Kept free of ESP-IDF / lwIP dependencies so it can be linked into the
 * native unit-test environment (see `env:native_captive` in platformio.ini).
 * The runtime DNS responder task in captive_portal_dns.cpp calls into this
 * to turn each incoming query into a response on the wire.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build a wildcard DNS A-response in place inside @p buf.
 *
 * Takes a DNS query that already occupies the first @p query_len bytes of
 * @p buf and rewrites the header to a response (QR=1, AA=1, RCODE=0,
 * ANCOUNT=1), then appends a single A-record using an RFC-1035 name
 * pointer (`0xC00C`) back to the query name at offset 12. Works for any
 * QTYPE: AAAA queries also receive an A-record, which clients interpret as
 * "no IPv6", which is fine for the captive-portal flow.
 *
 * @param buf       Buffer holding the query bytes; will be overwritten with
 *                  the response in place.
 * @param query_len Length of the incoming query in bytes.
 * @param buf_size  Total size of @p buf in bytes (must fit query + 16
 *                  bytes for the appended A-record).
 * @param ip        Four bytes of the answer IP address, big-endian
 *                  (e.g. `{192, 168, 4, 1}`).
 * @return Response length on success, or -1 if the query is malformed
 *         (header too short, pointers in query, QNAME runs off the buffer,
 *         or the appended answer would overflow @p buf_size).
 */
int captive_build_dns_response(uint8_t *buf, int query_len, size_t buf_size, const uint8_t ip[4]);

#ifdef __cplusplus
}
#endif
