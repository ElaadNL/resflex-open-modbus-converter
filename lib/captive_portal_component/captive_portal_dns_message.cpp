// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "captive_portal_dns_message.h"

#include <stdint.h>

#define DNS_ANSWER_TTL 60

int captive_build_dns_response(uint8_t *buf, int query_len, size_t buf_size, const uint8_t ip[4]) {
    if (query_len < 12) {
        return -1; // smaller than a DNS header
    }
    if ((size_t)query_len > buf_size) {
        return -1;
    }

    // Find end of QNAME (sequence of labels terminated by 0x00). Cap the
    // scan at the buffer to defend against malformed packets.
    int qname_end = 12;
    while (qname_end < query_len && buf[qname_end] != 0x00) {
        uint8_t label_len = buf[qname_end];
        if ((label_len & 0xC0) != 0) {
            // Pointer in a query is illegal.
            return -1;
        }
        qname_end += 1 + label_len;
    }
    if (qname_end >= query_len) {
        return -1;
    }
    qname_end += 1; // include the terminating 0x00

    int qtype_qclass_end = qname_end + 4; // QTYPE(2) + QCLASS(2)
    if (qtype_qclass_end > query_len) {
        return -1;
    }

    // Answer record we append: pointer(2) + type(2) + class(2) + ttl(4)
    // + rdlength(2) + rdata(4) = 16 bytes.
    int response_len = qtype_qclass_end + 16;
    if ((size_t)response_len > buf_size) {
        return -1;
    }

    // Header → response, authoritative answer, no error, ANCOUNT = 1.
    buf[2] = 0x84;
    buf[3] = 0x00;
    buf[6] = 0x00;
    buf[7] = 0x01;
    buf[8] = 0x00;
    buf[9] = 0x00;
    buf[10] = 0x00;
    buf[11] = 0x00;

    uint8_t *a = buf + qtype_qclass_end;
    a[0] = 0xC0;
    a[1] = 0x0C; // name = pointer to QNAME at offset 12
    a[2] = 0x00;
    a[3] = 0x01; // TYPE  = A
    a[4] = 0x00;
    a[5] = 0x01; // CLASS = IN
    a[6] = (DNS_ANSWER_TTL >> 24) & 0xFF;
    a[7] = (DNS_ANSWER_TTL >> 16) & 0xFF;
    a[8] = (DNS_ANSWER_TTL >> 8) & 0xFF;
    a[9] = (DNS_ANSWER_TTL) & 0xFF;
    a[10] = 0x00;
    a[11] = 0x04; // RDLENGTH = 4
    a[12] = ip[0];
    a[13] = ip[1];
    a[14] = ip[2];
    a[15] = ip[3];

    return response_len;
}
