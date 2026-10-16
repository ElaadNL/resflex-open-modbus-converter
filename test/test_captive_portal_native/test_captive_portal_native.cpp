// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <unity.h>

#include <cstring>
#include <cstdint>

#include "captive_portal_http_routes.h"
#include "captive_portal_nvs.h"
#include "captive_portal_parsing.h"
#include "captive_portal_dns_message.h"
#include "definitions.h"  // NVS_STORAGE_KEY_* — the keys the source persists under
#include "storage_stub.h"

void setUp(void) {
    captive_test_storage::reset();
}
void tearDown(void) {}

// ── captive_url_decode ──────────────────────────────────────────────────────

void test_url_decode_passthrough_for_plain_string(void) {
    char s[] = "hello";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING("hello", s);
}

void test_url_decode_plus_becomes_space(void) {
    char s[] = "a+b+c";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING("a b c", s);
}

void test_url_decode_percent_hex_pairs(void) {
    char s[] = "%48%65%6C%6C%6F";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING("Hello", s);
}

void test_url_decode_lowercase_hex(void) {
    char s[] = "%c3%a9"; // UTF-8 'é'
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_UINT8(0xC3, (uint8_t)s[0]);
    TEST_ASSERT_EQUAL_UINT8(0xA9, (uint8_t)s[1]);
    TEST_ASSERT_EQUAL_UINT8(0x00, (uint8_t)s[2]);
}

void test_url_decode_invalid_percent_left_as_is(void) {
    char s[] = "%ZZ";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING("%ZZ", s);
}

void test_url_decode_truncated_percent_at_end_left_as_is(void) {
    char s[] = "ab%";
    captive_url_decode(s);
    // Behavior: a trailing '%' with no following bytes is left as a '%'.
    TEST_ASSERT_EQUAL_STRING("ab%", s);
}

void test_url_decode_mixed_plus_and_percent(void) {
    char s[] = "S2+Modbus%21";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING("S2 Modbus!", s);
}

void test_url_decode_empty_string(void) {
    char s[] = "";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING("", s);
}

void test_url_decode_only_pluses(void) {
    char s[] = "+++";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING("   ", s);
}

void test_url_decode_only_percent_pairs(void) {
    char s[] = "%41%42%43";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING("ABC", s);
}

void test_url_decode_percent_at_start_with_one_byte(void) {
    // '%' followed by only one char isn't a valid escape (need two hex digits)
    // — leave the '%' in place and continue from the next char.
    char s[] = "%A";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING("%A", s);
}

void test_url_decode_null_byte_in_middle(void) {
    // %00 decodes to a NUL byte, which terminates the string early.
    char s[] = "ab%00cd";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING("ab", s);
}

void test_url_decode_long_string(void) {
    // A typical SSID + websocket URL combined, all url-encoded — make sure
    // we don't blow up on something this size.
    char s[] = "wifi_ssid=Living+Room+WiFi%21&websocket_uri=wss%3A%2F%2Fems.example.com%2Fconnect";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_STRING(
        "wifi_ssid=Living Room WiFi!&websocket_uri=wss://ems.example.com/connect", s);
}

void test_url_decode_consecutive_percent_pairs(void) {
    // Real-world payload: UTF-8 encoded "✓" is "%E2%9C%93".
    char s[] = "%E2%9C%93";
    captive_url_decode(s);
    TEST_ASSERT_EQUAL_UINT8(0xE2, (uint8_t)s[0]);
    TEST_ASSERT_EQUAL_UINT8(0x9C, (uint8_t)s[1]);
    TEST_ASSERT_EQUAL_UINT8(0x93, (uint8_t)s[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, (uint8_t)s[3]);
}

// ── captive_form_field ──────────────────────────────────────────────────────

void test_form_field_finds_value(void) {
    char body[] = "wifi_ssid=MyNet&wifi_password=secret";
    char out[32] = {};
    bool ok = captive_form_field(body, "wifi_ssid", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("MyNet", out);
}

void test_form_field_finds_value_at_end(void) {
    char body[] = "wifi_ssid=MyNet&wifi_password=secret";
    char out[32] = {};
    bool ok = captive_form_field(body, "wifi_password", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("secret", out);
}

void test_form_field_missing_returns_false(void) {
    char body[] = "wifi_ssid=MyNet";
    char out[32] = {};
    bool ok = captive_form_field(body, "websocket_uri", out, sizeof(out));
    TEST_ASSERT_FALSE(ok);
}

void test_form_field_empty_value_still_present(void) {
    char body[] = "wifi_password=&websocket_uri=ws://x";
    char out[32] = {};
    bool ok = captive_form_field(body, "wifi_password", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_form_field_decodes_value(void) {
    char body[] = "wifi_ssid=My+Net%21&x=y";
    char out[32] = {};
    bool ok = captive_form_field(body, "wifi_ssid", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("My Net!", out);
}

void test_form_field_lookup_does_not_match_prefix(void) {
    // A naive substring search would match "wifi_ssid" inside the value of
    // an earlier field — make sure that doesn't happen.
    char body[] = "other=wifi_ssid_decoy&wifi_ssid=Real";
    char out[32] = {};
    bool ok = captive_form_field(body, "wifi_ssid", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("Real", out);
}

void test_form_field_two_lookups_on_same_body(void) {
    // The implementation mutates and restores the body so multiple lookups
    // still work. Verify that.
    char body[] = "a=1&b=2&c=3";
    char out[16] = {};

    TEST_ASSERT_TRUE(captive_form_field(body, "a", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("1", out);
    TEST_ASSERT_TRUE(captive_form_field(body, "b", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("2", out);
    TEST_ASSERT_TRUE(captive_form_field(body, "c", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("3", out);
}

void test_form_field_truncates_when_value_overflows_out(void) {
    char body[] = "wifi_ssid=abcdefghij";
    char out[5] = {};
    bool truncated = false;
    bool ok = captive_form_field(body, "wifi_ssid", out, sizeof(out), &truncated);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("abcd", out); // 4 chars + NUL
    TEST_ASSERT_TRUE(truncated);
}

void test_form_field_not_truncated_when_value_fits(void) {
    char body[] = "wifi_ssid=abc";
    char out[16] = {};
    bool truncated = true; // start true to prove it gets cleared
    bool ok = captive_form_field(body, "wifi_ssid", out, sizeof(out), &truncated);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("abc", out);
    TEST_ASSERT_FALSE(truncated);
}

void test_form_field_empty_body(void) {
    char body[] = "";
    char out[16] = {};
    TEST_ASSERT_FALSE(captive_form_field(body, "wifi_ssid", out, sizeof(out)));
}

void test_form_field_body_without_equals(void) {
    // A body that has no '=' at all is malformed — we report missing.
    char body[] = "wifi_ssid";
    char out[16] = {};
    TEST_ASSERT_FALSE(captive_form_field(body, "wifi_ssid", out, sizeof(out)));
}

void test_form_field_value_with_encoded_ampersand(void) {
    // %26 = '&' inside the value should survive decoding and not split the
    // value at a logical '&'.
    char body[] = "wifi_ssid=A%26B&other=1";
    char out[16] = {};
    bool ok = captive_form_field(body, "wifi_ssid", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("A&B", out);

    // The body must still be functional for the next lookup.
    TEST_ASSERT_TRUE(captive_form_field(body, "other", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("1", out);
}

void test_form_field_value_with_encoded_equals(void) {
    char body[] = "expr=a%3Db";
    char out[16] = {};
    TEST_ASSERT_TRUE(captive_form_field(body, "expr", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a=b", out);
}

void test_form_field_out_size_one_writes_only_nul(void) {
    char body[] = "x=hello";
    char out[1] = {'X'};
    TEST_ASSERT_TRUE(captive_form_field(body, "x", out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8('\0', (uint8_t)out[0]);
}

void test_form_field_finds_value_when_field_appears_twice(void) {
    // If the same key appears twice we get the first occurrence — that's
    // standard form-urlencoded behavior.
    char body[] = "k=first&k=second";
    char out[16] = {};
    TEST_ASSERT_TRUE(captive_form_field(body, "k", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("first", out);
}

// ── captive_html_escape ─────────────────────────────────────────────────────

void test_html_escape_passes_safe_chars(void) {
    char out[64] = {};
    captive_html_escape("hello world", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("hello world", out);
}

void test_html_escape_all_five_entities(void) {
    char out[128] = {};
    captive_html_escape("<b>\"It's & dangerous\"</b>", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING(
        "&lt;b&gt;&quot;It&#39;s &amp; dangerous&quot;&lt;/b&gt;",
        out);
}

void test_html_escape_truncates_on_overflow(void) {
    char out[6] = {};
    captive_html_escape("<<<<<<<<", out, sizeof(out));
    // "&lt;" = 4 chars, second would need 4 more but only 1 byte left for
    // NUL — so output is "&lt;\0" (5 chars including NUL).
    TEST_ASSERT_EQUAL_STRING("&lt;", out);
}

void test_html_escape_empty_input(void) {
    char out[8] = "garbage";
    captive_html_escape("", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_html_escape_zero_out_size(void) {
    // out_size 0 means: don't write anything at all (not even a NUL).
    // Initialize so we can verify nothing was touched.
    char out[2] = {'X', 'Y'};
    captive_html_escape("hello", out, 0);
    TEST_ASSERT_EQUAL_UINT8('X', (uint8_t)out[0]);
    TEST_ASSERT_EQUAL_UINT8('Y', (uint8_t)out[1]);
}

void test_html_escape_utf8_passes_through(void) {
    // Bytes >= 0x80 are not in the escape set — they should be copied
    // verbatim so multibyte UTF-8 survives.
    char out[32] = {};
    captive_html_escape("héllo→", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("héllo→", out);
}

void test_html_escape_exactly_fits_buffer(void) {
    // "&amp;" is 5 chars; out_size 6 leaves room for entity + NUL exactly.
    char out[6] = {};
    captive_html_escape("&", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("&amp;", out);
}

void test_html_escape_truncates_inside_entity(void) {
    // "&" wants to become "&amp;" but with only 4 bytes for data + 1 NUL
    // (out_size=5) the entity doesn't fit, so escape() should bail before
    // writing a partial entity.
    char out[5] = {};
    captive_html_escape("&x", out, sizeof(out));
    // We don't write a partial "&amp" — bail at the entity boundary, then
    // the loop ends. Result is empty string (NUL-terminated).
    TEST_ASSERT_EQUAL_STRING("", out);
}

// ── captive_build_dns_response ──────────────────────────────────────────────

// A minimal DNS query for "example.com" type A class IN.
// header (12) + qname (13 = 7+5+1) + qtype (2) + qclass (2) = 29 bytes.
static int make_query_example_com_A(uint8_t* buf, size_t buf_size) {
    static const uint8_t query[] = {
        // header
        0xAB, 0xCD,             // ID
        0x01, 0x00,             // flags: standard query, RD=1
        0x00, 0x01,             // QDCOUNT=1
        0x00, 0x00,             // ANCOUNT=0
        0x00, 0x00,             // NSCOUNT=0
        0x00, 0x00,             // ARCOUNT=0
        // qname: example.com
        7, 'e','x','a','m','p','l','e',
        3, 'c','o','m',
        0,
        // qtype = A
        0x00, 0x01,
        // qclass = IN
        0x00, 0x01,
    };
    TEST_ASSERT(sizeof(query) <= buf_size);
    std::memcpy(buf, query, sizeof(query));
    return (int)sizeof(query);
}

void test_dns_response_builds_valid_a_record(void) {
    uint8_t buf[512] = {};
    int qlen = make_query_example_com_A(buf, sizeof(buf));

    const uint8_t ip[4] = {192, 168, 4, 1};
    int rlen = captive_build_dns_response(buf, qlen, sizeof(buf), ip);

    TEST_ASSERT_EQUAL_INT(qlen + 16, rlen);

    // Header flags: QR=1, AA=1, RCODE=0  → byte[2] = 0x84, byte[3] = 0x00.
    TEST_ASSERT_EQUAL_UINT8(0x84, buf[2]);
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[3]);

    // ANCOUNT == 1
    TEST_ASSERT_EQUAL_UINT8(0x00, buf[6]);
    TEST_ASSERT_EQUAL_UINT8(0x01, buf[7]);

    // Answer record starts at offset qlen.
    uint8_t* a = buf + qlen;
    TEST_ASSERT_EQUAL_UINT8(0xC0, a[0]); // name pointer hi
    TEST_ASSERT_EQUAL_UINT8(0x0C, a[1]); // pointer to offset 12
    TEST_ASSERT_EQUAL_UINT8(0x00, a[2]); // TYPE A
    TEST_ASSERT_EQUAL_UINT8(0x01, a[3]);
    TEST_ASSERT_EQUAL_UINT8(0x00, a[4]); // CLASS IN
    TEST_ASSERT_EQUAL_UINT8(0x01, a[5]);
    TEST_ASSERT_EQUAL_UINT8(0x00, a[10]); // RDLENGTH=4
    TEST_ASSERT_EQUAL_UINT8(0x04, a[11]);
    TEST_ASSERT_EQUAL_UINT8(192, a[12]);
    TEST_ASSERT_EQUAL_UINT8(168, a[13]);
    TEST_ASSERT_EQUAL_UINT8(4,   a[14]);
    TEST_ASSERT_EQUAL_UINT8(1,   a[15]);
}

void test_dns_response_preserves_id(void) {
    uint8_t buf[512] = {};
    int qlen = make_query_example_com_A(buf, sizeof(buf));

    const uint8_t ip[4] = {192, 168, 4, 1};
    captive_build_dns_response(buf, qlen, sizeof(buf), ip);

    TEST_ASSERT_EQUAL_UINT8(0xAB, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0xCD, buf[1]);
}

void test_dns_response_rejects_too_short_query(void) {
    uint8_t buf[16] = {0};
    const uint8_t ip[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(-1, captive_build_dns_response(buf, 5, sizeof(buf), ip));
}

void test_dns_response_rejects_pointer_in_query(void) {
    // Header (12) + a label byte with 0xC0 bits set (illegal in a query).
    uint8_t buf[64] = {
        0,0, 0,0, 0,1, 0,0, 0,0, 0,0,
        0xC0, 0x0C,
        0x00, 0x01,
        0x00, 0x01,
    };
    const uint8_t ip[4] = {1,2,3,4};
    TEST_ASSERT_EQUAL_INT(-1, captive_build_dns_response(buf, 18, sizeof(buf), ip));
}

void test_dns_response_rejects_qname_overrun(void) {
    // QNAME label claims 50 bytes but the query is much shorter.
    uint8_t buf[64] = {
        0,0, 0,0, 0,1, 0,0, 0,0, 0,0,
        50, 'a', 'b', 'c',  // length byte says 50, only 3 bytes follow
    };
    const uint8_t ip[4] = {1,2,3,4};
    TEST_ASSERT_EQUAL_INT(-1, captive_build_dns_response(buf, 16, sizeof(buf), ip));
}

void test_dns_response_rejects_buffer_too_small_for_answer(void) {
    // Provide a buffer that fits the query exactly but has no room for the
    // 16-byte answer record.
    uint8_t buf[29] = {};
    int qlen = make_query_example_com_A(buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(29, qlen);

    const uint8_t ip[4] = {1,2,3,4};
    TEST_ASSERT_EQUAL_INT(-1, captive_build_dns_response(buf, qlen, sizeof(buf), ip));
}

void test_dns_response_handles_aaaa_query(void) {
    // We always answer with an A-record regardless of QTYPE. Verify that an
    // AAAA query (QTYPE=28) still produces a valid A response that clients
    // will interpret as "no IPv6".
    uint8_t buf[512] = {};
    int qlen = make_query_example_com_A(buf, sizeof(buf));
    // Patch QTYPE to AAAA (0x001C).
    buf[qlen - 4] = 0x00;
    buf[qlen - 3] = 0x1C;

    const uint8_t ip[4] = {10, 0, 0, 1};
    int rlen = captive_build_dns_response(buf, qlen, sizeof(buf), ip);
    TEST_ASSERT_EQUAL_INT(qlen + 16, rlen);

    // Answer TYPE is still A (1), regardless of what was asked.
    uint8_t* a = buf + qlen;
    TEST_ASSERT_EQUAL_UINT8(0x00, a[2]);
    TEST_ASSERT_EQUAL_UINT8(0x01, a[3]);
}

void test_dns_response_handles_multilabel_qname(void) {
    // QNAME with 3 labels: "captive.example.com"
    uint8_t buf[512] = {};
    uint8_t query[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        7, 'c','a','p','t','i','v','e',
        7, 'e','x','a','m','p','l','e',
        3, 'c','o','m',
        0,
        0x00, 0x01, // QTYPE A
        0x00, 0x01, // QCLASS IN
    };
    std::memcpy(buf, query, sizeof(query));
    int qlen = (int)sizeof(query);

    const uint8_t ip[4] = {192, 168, 4, 1};
    int rlen = captive_build_dns_response(buf, qlen, sizeof(buf), ip);
    TEST_ASSERT_EQUAL_INT(qlen + 16, rlen);

    // Answer name still points back to offset 12.
    uint8_t* a = buf + qlen;
    TEST_ASSERT_EQUAL_UINT8(0xC0, a[0]);
    TEST_ASSERT_EQUAL_UINT8(0x0C, a[1]);
}

void test_dns_response_handles_max_label_length(void) {
    // 63 is the RFC-1035 maximum label length. A single label of length 63
    // should parse fine.
    uint8_t buf[512] = {};
    int o = 0;
    buf[o++] = 0xAA; buf[o++] = 0xBB;          // ID
    buf[o++] = 0x01; buf[o++] = 0x00;          // flags
    buf[o++] = 0x00; buf[o++] = 0x01;          // QDCOUNT
    buf[o++] = 0x00; buf[o++] = 0x00;
    buf[o++] = 0x00; buf[o++] = 0x00;
    buf[o++] = 0x00; buf[o++] = 0x00;
    buf[o++] = 63;
    for (int i = 0; i < 63; i++) buf[o++] = 'a';
    buf[o++] = 0;
    buf[o++] = 0x00; buf[o++] = 0x01;          // QTYPE
    buf[o++] = 0x00; buf[o++] = 0x01;          // QCLASS
    int qlen = o;

    const uint8_t ip[4] = {1, 2, 3, 4};
    int rlen = captive_build_dns_response(buf, qlen, sizeof(buf), ip);
    TEST_ASSERT_EQUAL_INT(qlen + 16, rlen);
}

void test_dns_response_qname_terminated_exactly_at_buffer_end(void) {
    // A query whose QNAME's terminator is the very last byte (no room for
    // QTYPE/QCLASS) should be rejected.
    uint8_t buf[16] = {
        0,0, 0,0, 0,1, 0,0, 0,0, 0,0,
        1, 'a', 0,           // 1-byte label "a" then NUL terminator
        0x00,                // partial QTYPE — only 1 byte present
    };
    const uint8_t ip[4] = {1,2,3,4};
    TEST_ASSERT_EQUAL_INT(-1, captive_build_dns_response(buf, 16, sizeof(buf), ip));
}

void test_dns_response_writes_supplied_ip(void) {
    // The IP we pass in should appear verbatim in the RDATA.
    uint8_t buf[512] = {};
    int qlen = make_query_example_com_A(buf, sizeof(buf));

    const uint8_t ip[4] = {10, 20, 30, 40};
    int rlen = captive_build_dns_response(buf, qlen, sizeof(buf), ip);
    TEST_ASSERT_EQUAL_INT(qlen + 16, rlen);

    uint8_t* a = buf + qlen;
    TEST_ASSERT_EQUAL_UINT8(10, a[12]);
    TEST_ASSERT_EQUAL_UINT8(20, a[13]);
    TEST_ASSERT_EQUAL_UINT8(30, a[14]);
    TEST_ASSERT_EQUAL_UINT8(40, a[15]);
}

// ── captive_apply_form_to_nvs ───────────────────────────────────────────────

void test_apply_form_writes_all_string_fields(void) {
    // The left-hand keys in the body are the HTML <input name="..."> values
    // the browser POSTs; the queried keys are the NVS storage keys the source
    // persists them under (see definitions.h NVS_STORAGE_KEY_* macros).
    char body[] = "wifi_ssid=MyNet&wifi_password=secret123"
                  "&websocket_uri=wss%3A%2F%2Fems.example.com%2F&pair_key=abc123"
                  "&ip_static=1&ip_addr=10.0.0.5&ip_netmask=255.255.255.0&ip_gateway=10.0.0.1";
    captive_apply_form_to_nvs(body);

    TEST_ASSERT_TRUE(captive_test_storage::has_string(NVS_STORAGE_KEY_WIFI_SSID));
    TEST_ASSERT_EQUAL_STRING("MyNet", captive_test_storage::get_string(NVS_STORAGE_KEY_WIFI_SSID).c_str());

    TEST_ASSERT_TRUE(captive_test_storage::has_string(NVS_STORAGE_KEY_WIFI_PSK));
    TEST_ASSERT_EQUAL_STRING("secret123", captive_test_storage::get_string(NVS_STORAGE_KEY_WIFI_PSK).c_str());

    TEST_ASSERT_TRUE(captive_test_storage::has_string(NVS_STORAGE_KEY_HEMS_URL));
    TEST_ASSERT_EQUAL_STRING("wss://ems.example.com/",
                             captive_test_storage::get_string(NVS_STORAGE_KEY_HEMS_URL).c_str());

    TEST_ASSERT_TRUE(captive_test_storage::has_string(NVS_STORAGE_KEY_HEMS_PAIR_KEY));
    TEST_ASSERT_EQUAL_STRING("abc123", captive_test_storage::get_string(NVS_STORAGE_KEY_HEMS_PAIR_KEY).c_str());

    TEST_ASSERT_TRUE(captive_test_storage::has_string(NVS_STORAGE_KEY_HEMS_NETWORK_IP));
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", captive_test_storage::get_string(NVS_STORAGE_KEY_HEMS_NETWORK_IP).c_str());

    TEST_ASSERT_TRUE(captive_test_storage::has_u16(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP));
    TEST_ASSERT_EQUAL_UINT16(1, captive_test_storage::get_u16(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP));
}

void test_apply_form_empty_password_keeps_stored_value(void) {
    captive_test_storage::seed_string(NVS_STORAGE_KEY_WIFI_PSK, "EXISTING_PASS");

    char body[] = "wifi_ssid=NewNet&wifi_password=";
    captive_apply_form_to_nvs(body);

    // SSID got updated, password did NOT — stub still holds the seeded value.
    TEST_ASSERT_EQUAL_STRING("NewNet", captive_test_storage::get_string(NVS_STORAGE_KEY_WIFI_SSID).c_str());
    TEST_ASSERT_EQUAL_STRING("EXISTING_PASS", captive_test_storage::get_string(NVS_STORAGE_KEY_WIFI_PSK).c_str());
}

void test_apply_form_empty_pair_key_keeps_stored_value(void) {
    captive_test_storage::seed_string(NVS_STORAGE_KEY_HEMS_PAIR_KEY, "EXISTING_KEY");

    char body[] = "pair_key=";
    captive_apply_form_to_nvs(body);

    TEST_ASSERT_EQUAL_STRING("EXISTING_KEY", captive_test_storage::get_string(NVS_STORAGE_KEY_HEMS_PAIR_KEY).c_str());
}

void test_apply_form_non_empty_password_overwrites(void) {
    captive_test_storage::seed_string(NVS_STORAGE_KEY_WIFI_PSK, "OLD");
    char body[] = "wifi_password=NEW";
    captive_apply_form_to_nvs(body);
    TEST_ASSERT_EQUAL_STRING("NEW", captive_test_storage::get_string(NVS_STORAGE_KEY_WIFI_PSK).c_str());
}

void test_apply_form_unchecked_static_ip_is_zero(void) {
    // `ip_static` not present in body == unchecked. We still write a 0 so
    // the user can switch back to DHCP after enabling static once.
    char body[] = "wifi_ssid=x";
    captive_apply_form_to_nvs(body);
    TEST_ASSERT_TRUE(captive_test_storage::has_u16(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP));
    TEST_ASSERT_EQUAL_UINT16(0, captive_test_storage::get_u16(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP));
}

void test_apply_form_checked_static_ip_is_one(void) {
    char body[] = "ip_static=1";
    captive_apply_form_to_nvs(body);
    TEST_ASSERT_EQUAL_UINT16(1, captive_test_storage::get_u16(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP));
}

void test_apply_form_static_ip_other_value_treated_as_off(void) {
    // Anything other than literal "1" should be treated as off — only the
    // literal HTML checkbox value of "1" counts.
    char body[] = "ip_static=true";
    captive_apply_form_to_nvs(body);
    TEST_ASSERT_EQUAL_UINT16(0, captive_test_storage::get_u16(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP));
}

void test_apply_form_blank_ssid_overwrites(void) {
    // Empty string IS written (only password / pair_key have the "keep"
    // semantics). Allows the user to blank out the SSID if they want.
    captive_test_storage::seed_string(NVS_STORAGE_KEY_WIFI_SSID, "OldSSID");
    char body[] = "wifi_ssid=";
    captive_apply_form_to_nvs(body);
    TEST_ASSERT_EQUAL_STRING("", captive_test_storage::get_string(NVS_STORAGE_KEY_WIFI_SSID).c_str());
}

void test_apply_form_missing_fields_left_untouched(void) {
    // A body that only contains ip_static must not touch wifi_ssid etc.
    captive_test_storage::seed_string(NVS_STORAGE_KEY_WIFI_SSID, "Preserved");
    captive_test_storage::seed_string(NVS_STORAGE_KEY_HEMS_PAIR_KEY, "PreservedKey");
    char body[] = "ip_static=1";
    captive_apply_form_to_nvs(body);

    TEST_ASSERT_EQUAL_STRING("Preserved", captive_test_storage::get_string(NVS_STORAGE_KEY_WIFI_SSID).c_str());
    TEST_ASSERT_EQUAL_STRING("PreservedKey", captive_test_storage::get_string(NVS_STORAGE_KEY_HEMS_PAIR_KEY).c_str());
}

void test_apply_form_url_encoded_values_decoded_before_storage(void) {
    char body[] = "wifi_ssid=Living+Room+WiFi%21";
    captive_apply_form_to_nvs(body);
    TEST_ASSERT_EQUAL_STRING("Living Room WiFi!",
                             captive_test_storage::get_string(NVS_STORAGE_KEY_WIFI_SSID).c_str());
}

void test_apply_form_static_ip_disabled_after_being_enabled(void) {
    // User had static IP on, now submits the form with the checkbox off.
    captive_test_storage::reset();
    char enable_body[] = "ip_static=1";
    captive_apply_form_to_nvs(enable_body);
    TEST_ASSERT_EQUAL_UINT16(1, captive_test_storage::get_u16(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP));

    char disable_body[] = "wifi_ssid=x"; // checkbox simply not in body
    captive_apply_form_to_nvs(disable_body);
    TEST_ASSERT_EQUAL_UINT16(0, captive_test_storage::get_u16(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP));
}

// ── captive_apply_appliance_params_to_nvs ───────────────────────────────────
//
// Persists the appliance-form fields (name/model/serial/firmware, the five
// new modbus_* fields, modbus_channel and num_phases) to NVS. Empty strings
// are written unchanged (so the user can clear a value); num_phases is
// clamped to 1/2/3, anything else falls back to 1.

void test_apply_appliance_writes_all_string_fields(void) {
    char body[] = "appliance_name=MyAppliance"
                  "&appliance_model=ModelX"
                  "&appliance_serial=SN12345"
                  "&appliance_firmware=fw-1.2.3"
                  "&modbus_channel=2"
                  "&modbus_ip=10.0.0.50"
                  "&modbus_port=502"
                  "&modbus_gateway=10.0.0.1"
                  "&modbus_netmask=255.255.255.0"
                  "&modbus_unit_id=7"
                  "&num_phases=3";
    captive_apply_appliance_params_to_nvs(body);

    TEST_ASSERT_EQUAL_STRING("MyAppliance", captive_test_storage::get_string(NVS_STORAGE_KEY_APPLIANCE_NAME).c_str());
    TEST_ASSERT_EQUAL_STRING("ModelX",      captive_test_storage::get_string(NVS_STORAGE_KEY_APPLIANCE_MODEL).c_str());
    TEST_ASSERT_EQUAL_STRING("SN12345",     captive_test_storage::get_string(NVS_STORAGE_KEY_APPLIANCE_SERIAL).c_str());
    TEST_ASSERT_EQUAL_STRING("fw-1.2.3",    captive_test_storage::get_string(NVS_STORAGE_KEY_APPLIANCE_FIRMWARE).c_str());
    TEST_ASSERT_EQUAL_STRING("2",           captive_test_storage::get_string(NVS_STORAGE_KEY_MODBUS_CHANNEL).c_str());
    TEST_ASSERT_EQUAL_STRING("10.0.0.50",   captive_test_storage::get_string(NVS_STORAGE_KEY_MODBUS_ETH_IP).c_str());
    TEST_ASSERT_EQUAL_STRING("502",         captive_test_storage::get_string(NVS_STORAGE_KEY_MODBUS_ETH_PORT).c_str());
    TEST_ASSERT_EQUAL_STRING("10.0.0.1",    captive_test_storage::get_string(NVS_STORAGE_KEY_MODBUS_ETH_GATEWAY).c_str());
    TEST_ASSERT_EQUAL_STRING("255.255.255.0", captive_test_storage::get_string(NVS_STORAGE_KEY_MODBUS_ETH_NETMASK).c_str());
    TEST_ASSERT_EQUAL_STRING("7",           captive_test_storage::get_string(NVS_STORAGE_KEY_MODBUS_UNIT_ID).c_str());
    TEST_ASSERT_EQUAL_UINT16(3,             captive_test_storage::get_u16(NVS_STORAGE_KEY_NUM_PHASES));
}

void test_apply_appliance_missing_fields_left_untouched(void) {
    // Only the fields actually present in the POST body must be written.
    // Pre-seeded values for the other fields must survive untouched, so a
    // partial form submission doesn't wipe state the user didn't touch.
    captive_test_storage::seed_string(NVS_STORAGE_KEY_APPLIANCE_NAME, "PreservedName");
    captive_test_storage::seed_string(NVS_STORAGE_KEY_MODBUS_ETH_IP,  "10.10.10.10");
    captive_test_storage::seed_string(NVS_STORAGE_KEY_MODBUS_UNIT_ID, "99");

    char body[] = "appliance_model=NewModel";
    captive_apply_appliance_params_to_nvs(body);

    TEST_ASSERT_EQUAL_STRING("NewModel",      captive_test_storage::get_string(NVS_STORAGE_KEY_APPLIANCE_MODEL).c_str());
    TEST_ASSERT_EQUAL_STRING("PreservedName", captive_test_storage::get_string(NVS_STORAGE_KEY_APPLIANCE_NAME).c_str());
    TEST_ASSERT_EQUAL_STRING("10.10.10.10",   captive_test_storage::get_string(NVS_STORAGE_KEY_MODBUS_ETH_IP).c_str());
    TEST_ASSERT_EQUAL_STRING("99",            captive_test_storage::get_string(NVS_STORAGE_KEY_MODBUS_UNIT_ID).c_str());
}

void test_apply_appliance_empty_string_overwrites(void) {
    // captive_apply_appliance_params_to_nvs writes empty strings — unlike
    // the wifi password / pair_key path. Lets the user clear a value.
    captive_test_storage::seed_string(NVS_STORAGE_KEY_APPLIANCE_NAME, "OldName");
    captive_test_storage::seed_string(NVS_STORAGE_KEY_MODBUS_ETH_IP,  "10.0.0.1");

    char body[] = "appliance_name=&modbus_ip=";
    captive_apply_appliance_params_to_nvs(body);

    TEST_ASSERT_EQUAL_STRING("", captive_test_storage::get_string(NVS_STORAGE_KEY_APPLIANCE_NAME).c_str());
    TEST_ASSERT_EQUAL_STRING("", captive_test_storage::get_string(NVS_STORAGE_KEY_MODBUS_ETH_IP).c_str());
}

void test_apply_appliance_url_encoded_values_decoded_before_storage(void) {
    // The form is application/x-www-form-urlencoded, so the IP's dots, the
    // name's spaces, and a regex-like firmware string must be decoded before
    // being persisted.
    char body[] = "appliance_name=My+Device%21"
                  "&modbus_ip=10.0.0.5"
                  "&appliance_firmware=v1%2E0";
    captive_apply_appliance_params_to_nvs(body);

    TEST_ASSERT_EQUAL_STRING("My Device!", captive_test_storage::get_string(NVS_STORAGE_KEY_APPLIANCE_NAME).c_str());
    TEST_ASSERT_EQUAL_STRING("10.0.0.5",   captive_test_storage::get_string(NVS_STORAGE_KEY_MODBUS_ETH_IP).c_str());
    TEST_ASSERT_EQUAL_STRING("v1.0",       captive_test_storage::get_string(NVS_STORAGE_KEY_APPLIANCE_FIRMWARE).c_str());
}

void test_apply_appliance_num_phases_valid_values_pass_through(void) {
    for (int v = 1; v <= 3; v++) {
        captive_test_storage::reset();
        char body[16];
        std::snprintf(body, sizeof(body), "num_phases=%d", v);
        captive_apply_appliance_params_to_nvs(body);
        TEST_ASSERT_EQUAL_UINT16((uint16_t)v,
                                 captive_test_storage::get_u16(NVS_STORAGE_KEY_NUM_PHASES));
    }
}

void test_apply_appliance_num_phases_out_of_range_clamps_to_one(void) {
    // Out-of-range value: clamped to 1 (matches GET-handler default).
    char body[] = "num_phases=5";
    captive_apply_appliance_params_to_nvs(body);
    TEST_ASSERT_EQUAL_UINT16(1, captive_test_storage::get_u16(NVS_STORAGE_KEY_NUM_PHASES));
}

void test_apply_appliance_num_phases_unparseable_clamps_to_one(void) {
    // atoi("abc") == 0 → falls into the "not 1/2/3" branch → clamped to 1.
    char body[] = "num_phases=abc";
    captive_apply_appliance_params_to_nvs(body);
    TEST_ASSERT_EQUAL_UINT16(1, captive_test_storage::get_u16(NVS_STORAGE_KEY_NUM_PHASES));
}

void test_apply_appliance_num_phases_absent_defaults_to_one(void) {
    // No num_phases field at all in the body → handler still writes 1.
    char body[] = "appliance_name=x";
    captive_apply_appliance_params_to_nvs(body);
    TEST_ASSERT_EQUAL_UINT16(1, captive_test_storage::get_u16(NVS_STORAGE_KEY_NUM_PHASES));
}

// ── captive_load_escaped (NVS-backed) ───────────────────────────────────────

void test_load_escaped_uses_stored_value(void) {
    captive_test_storage::seed_string("wifi_ssid", "MyHomeNet");
    char out[64] = {};
    captive_load_escaped("wifi_ssid", "", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("MyHomeNet", out);
}

void test_load_escaped_falls_back_to_default_when_absent(void) {
    char out[64] = {};
    captive_load_escaped("nonexistent_key", "default-val", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("default-val", out);
}

void test_load_escaped_escapes_html_sensitive_chars(void) {
    captive_test_storage::seed_string("wifi_ssid", "<bad>&\"name'");
    char out[128] = {};
    captive_load_escaped("wifi_ssid", "", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("&lt;bad&gt;&amp;&quot;name&#39;", out);
}

void test_load_escaped_handles_null_default(void) {
    char out[16] = {};
    captive_load_escaped("missing", NULL, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_load_escaped_truncates_on_small_buffer(void) {
    captive_test_storage::seed_string("wifi_ssid", "verylongssidvalue");
    char out[6] = {};  // 5 chars + NUL
    captive_load_escaped("wifi_ssid", "", out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("veryl", out);
}

// ── captive_multipart_boundary ──────────────────────────────────────────────

void test_multipart_boundary_simple(void) {
    char out[64] = {};
    bool ok = captive_multipart_boundary(
        "multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW",
        out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("----WebKitFormBoundary7MA4YWxkTrZu0gW", out);
}

void test_multipart_boundary_quoted(void) {
    char out[64] = {};
    bool ok = captive_multipart_boundary(
        "multipart/form-data; boundary=\"my-boundary\"", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("my-boundary", out);
}

void test_multipart_boundary_missing(void) {
    char out[64] = "garbage";
    bool ok = captive_multipart_boundary(
        "multipart/form-data", out, sizeof(out));
    TEST_ASSERT_FALSE(ok);
    // Must still leave a NUL-terminated buffer.
    TEST_ASSERT_EQUAL_STRING("", out);
}

void test_multipart_boundary_trailing_params(void) {
    char out[64] = {};
    bool ok = captive_multipart_boundary(
        "multipart/form-data; boundary=abc123; charset=utf-8", out, sizeof(out));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_STRING("abc123", out);
}

void test_multipart_boundary_truncates_silently(void) {
    char out[8] = {};
    bool ok = captive_multipart_boundary(
        "multipart/form-data; boundary=abcdefghijklmnop", out, sizeof(out));
    TEST_ASSERT_TRUE(ok); // truncation is best-effort, still returns true
    TEST_ASSERT_EQUAL_STRING("abcdefg", out);
}

// ── captive_multipart_file_part ─────────────────────────────────────────────

// Helper: build a minimal multipart body for tests. Returns total length.
// Layout:
//   --B\r\n
//   Content-Disposition: form-data; name="appliance_config"; filename="x.json"\r\n
//   Content-Type: application/json\r\n
//   \r\n
//   <payload>\r\n
//   --B--\r\n
static size_t build_multipart(char* buf, size_t buf_size,
                              const char* boundary, const char* payload) {
    return (size_t)std::snprintf(buf, buf_size,
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"appliance_config\"; filename=\"x.json\"\r\n"
        "Content-Type: application/json\r\n"
        "\r\n"
        "%s\r\n"
        "--%s--\r\n",
        boundary, payload, boundary);
}

void test_multipart_file_part_extracts_payload(void) {
    char body[512];
    size_t blen = build_multipart(body, sizeof(body), "B", "{\"hello\":\"world\"}");

    const char* data = NULL;
    size_t dlen = 0;
    bool ok = captive_multipart_file_part(body, blen, "B", &data, &dlen);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(strlen("{\"hello\":\"world\"}"), dlen);
    TEST_ASSERT_EQUAL_MEMORY("{\"hello\":\"world\"}", data, dlen);
}

void test_multipart_file_part_with_binary_payload(void) {
    // Payload contains NUL byte — make sure we return a length-bounded slice
    // and don't rely on NUL-termination.
    char body[512];
    size_t off = (size_t)std::snprintf(body, sizeof(body),
        "--B\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"raw.bin\"\r\n"
        "\r\n");
    body[off++] = 'A';
    body[off++] = '\0';
    body[off++] = 'B';
    off += (size_t)std::snprintf(body + off, sizeof(body) - off, "\r\n--B--\r\n");

    const char* data = NULL;
    size_t dlen = 0;
    bool ok = captive_multipart_file_part(body, off, "B", &data, &dlen);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(3u, dlen);
    TEST_ASSERT_EQUAL_UINT8('A', (uint8_t)data[0]);
    TEST_ASSERT_EQUAL_UINT8(0,   (uint8_t)data[1]);
    TEST_ASSERT_EQUAL_UINT8('B', (uint8_t)data[2]);
}

void test_multipart_file_part_missing_boundary_in_body(void) {
    const char body[] = "no boundary here at all";
    const char* data = NULL;
    size_t dlen = 0;
    bool ok = captive_multipart_file_part(body, sizeof(body) - 1, "B", &data, &dlen);
    TEST_ASSERT_FALSE(ok);
}

void test_multipart_file_part_no_filename_skipped(void) {
    // A field part (no filename=) should be skipped. We then have no file
    // part remaining, so the lookup must fail.
    const char body[] =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"other\"\r\n"
        "\r\n"
        "just a string field\r\n"
        "--B--\r\n";
    const char* data = NULL;
    size_t dlen = 0;
    bool ok = captive_multipart_file_part(body, sizeof(body) - 1, "B", &data, &dlen);
    TEST_ASSERT_FALSE(ok);
}

void test_multipart_file_part_picks_file_after_text_field(void) {
    // Mixed body: text field first, then the file. We should skip the text
    // field and return the file payload.
    const char body[] =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"hint\"\r\n"
        "\r\n"
        "ignore me\r\n"
        "--B\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"c.json\"\r\n"
        "\r\n"
        "{\"k\":1}\r\n"
        "--B--\r\n";
    const char* data = NULL;
    size_t dlen = 0;
    bool ok = captive_multipart_file_part(body, sizeof(body) - 1, "B", &data, &dlen);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(strlen("{\"k\":1}"), dlen);
    TEST_ASSERT_EQUAL_MEMORY("{\"k\":1}", data, dlen);
}

void test_multipart_file_part_empty_payload(void) {
    // Zero-byte file is parseable; handler is the one that rejects it.
    const char body[] =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"empty.json\"\r\n"
        "\r\n"
        "\r\n"
        "--B--\r\n";
    const char* data = NULL;
    size_t dlen = 0;
    bool ok = captive_multipart_file_part(body, sizeof(body) - 1, "B", &data, &dlen);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(0u, dlen);
}

void test_multipart_file_part_truncated_body(void) {
    // Body claims a part but is cut off before the next boundary — must fail.
    const char body[] =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"x.json\"\r\n"
        "\r\n"
        "{\"hello\":";
    const char* data = NULL;
    size_t dlen = 0;
    bool ok = captive_multipart_file_part(body, sizeof(body) - 1, "B", &data, &dlen);
    TEST_ASSERT_FALSE(ok);
}

void test_multipart_file_part_payload_with_crlf_inside(void) {
    // JSON pretty-printed with embedded CRLFs must survive verbatim — we only
    // strip the trailing CRLF that precedes the next boundary.
    const char body[] =
        "--B\r\n"
        "Content-Disposition: form-data; name=\"f\"; filename=\"x.json\"\r\n"
        "\r\n"
        "{\r\n  \"k\": 1\r\n}\r\n"
        "--B--\r\n";
    const char* data = NULL;
    size_t dlen = 0;
    bool ok = captive_multipart_file_part(body, sizeof(body) - 1, "B", &data, &dlen);
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_EQUAL_size_t(strlen("{\r\n  \"k\": 1\r\n}"), dlen);
    TEST_ASSERT_EQUAL_MEMORY("{\r\n  \"k\": 1\r\n}", data, dlen);
}

// ── captive routing table ───────────────────────────────────────────────────

// Helper: linear search through CAPTIVE_ROUTES for a given URI.
static const captive_route_t* find_route(const char* uri) {
    for (size_t i = 0; i < CAPTIVE_ROUTES_COUNT; i++) {
        if (std::strcmp(CAPTIVE_ROUTES[i].uri, uri) == 0) {
            return &CAPTIVE_ROUTES[i];
        }
    }
    return nullptr;
}

void test_routes_table_is_non_empty(void) {
    TEST_ASSERT_GREATER_THAN(0u, CAPTIVE_ROUTES_COUNT);
}

void test_routes_android_probes_present_and_redirect(void) {
    const captive_route_t* r = find_route("/generate_204");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(CAPTIVE_ROUTE_REDIRECT, r->behavior);

    r = find_route("/gen_204");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(CAPTIVE_ROUTE_REDIRECT, r->behavior);
}

void test_routes_apple_probes_present_and_redirect(void) {
    const captive_route_t* r = find_route("/hotspot-detect.html");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(CAPTIVE_ROUTE_REDIRECT, r->behavior);

    r = find_route("/library/test/success.html");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(CAPTIVE_ROUTE_REDIRECT, r->behavior);
}

void test_routes_windows_probes_present_and_redirect(void) {
    const captive_route_t* r = find_route("/ncsi.txt");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(CAPTIVE_ROUTE_REDIRECT, r->behavior);

    r = find_route("/connecttest.txt");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(CAPTIVE_ROUTE_REDIRECT, r->behavior);

    r = find_route("/redirect");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(CAPTIVE_ROUTE_REDIRECT, r->behavior);
}

void test_routes_firefox_probe_present_and_redirect(void) {
    const captive_route_t* r = find_route("/canonical.html");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(CAPTIVE_ROUTE_REDIRECT, r->behavior);
}

void test_routes_noisy_probes_present_and_404(void) {
    const captive_route_t* r = find_route("/wpad.dat");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(CAPTIVE_ROUTE_404, r->behavior);

    r = find_route("/favicon.ico");
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL_INT(CAPTIVE_ROUTE_404, r->behavior);
}

void test_routes_all_uris_start_with_slash(void) {
    for (size_t i = 0; i < CAPTIVE_ROUTES_COUNT; i++) {
        TEST_ASSERT_NOT_NULL(CAPTIVE_ROUTES[i].uri);
        TEST_ASSERT_EQUAL_UINT8('/', (uint8_t)CAPTIVE_ROUTES[i].uri[0]);
    }
}

void test_routes_no_duplicate_uris(void) {
    for (size_t i = 0; i < CAPTIVE_ROUTES_COUNT; i++) {
        for (size_t j = i + 1; j < CAPTIVE_ROUTES_COUNT; j++) {
            if (std::strcmp(CAPTIVE_ROUTES[i].uri, CAPTIVE_ROUTES[j].uri) == 0) {
                char msg[128];
                std::snprintf(msg, sizeof(msg), "Duplicate route: %s", CAPTIVE_ROUTES[i].uri);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

void test_routes_behavior_is_within_enum_range(void) {
    for (size_t i = 0; i < CAPTIVE_ROUTES_COUNT; i++) {
        const int b = (int)CAPTIVE_ROUTES[i].behavior;
        TEST_ASSERT_TRUE_MESSAGE(
            b == CAPTIVE_ROUTE_REDIRECT || b == CAPTIVE_ROUTE_404 ||
            b == CAPTIVE_ROUTE_200 || b == CAPTIVE_ROUTE_204,
            "behavior is not a known captive_route_behavior_t value");
    }
}

// ── runner ──────────────────────────────────────────────────────────────────

int main(int, char**) {
    UNITY_BEGIN();

    RUN_TEST(test_url_decode_passthrough_for_plain_string);
    RUN_TEST(test_url_decode_plus_becomes_space);
    RUN_TEST(test_url_decode_percent_hex_pairs);
    RUN_TEST(test_url_decode_lowercase_hex);
    RUN_TEST(test_url_decode_invalid_percent_left_as_is);
    RUN_TEST(test_url_decode_truncated_percent_at_end_left_as_is);
    RUN_TEST(test_url_decode_mixed_plus_and_percent);
    RUN_TEST(test_url_decode_empty_string);
    RUN_TEST(test_url_decode_only_pluses);
    RUN_TEST(test_url_decode_only_percent_pairs);
    RUN_TEST(test_url_decode_percent_at_start_with_one_byte);
    RUN_TEST(test_url_decode_null_byte_in_middle);
    RUN_TEST(test_url_decode_long_string);
    RUN_TEST(test_url_decode_consecutive_percent_pairs);

    RUN_TEST(test_form_field_finds_value);
    RUN_TEST(test_form_field_finds_value_at_end);
    RUN_TEST(test_form_field_missing_returns_false);
    RUN_TEST(test_form_field_empty_value_still_present);
    RUN_TEST(test_form_field_decodes_value);
    RUN_TEST(test_form_field_lookup_does_not_match_prefix);
    RUN_TEST(test_form_field_two_lookups_on_same_body);
    RUN_TEST(test_form_field_truncates_when_value_overflows_out);
    RUN_TEST(test_form_field_not_truncated_when_value_fits);
    RUN_TEST(test_form_field_empty_body);
    RUN_TEST(test_form_field_body_without_equals);
    RUN_TEST(test_form_field_value_with_encoded_ampersand);
    RUN_TEST(test_form_field_value_with_encoded_equals);
    RUN_TEST(test_form_field_out_size_one_writes_only_nul);
    RUN_TEST(test_form_field_finds_value_when_field_appears_twice);

    RUN_TEST(test_html_escape_passes_safe_chars);
    RUN_TEST(test_html_escape_all_five_entities);
    RUN_TEST(test_html_escape_truncates_on_overflow);
    RUN_TEST(test_html_escape_empty_input);
    RUN_TEST(test_html_escape_zero_out_size);
    RUN_TEST(test_html_escape_utf8_passes_through);
    RUN_TEST(test_html_escape_exactly_fits_buffer);
    RUN_TEST(test_html_escape_truncates_inside_entity);

    RUN_TEST(test_dns_response_builds_valid_a_record);
    RUN_TEST(test_dns_response_preserves_id);
    RUN_TEST(test_dns_response_rejects_too_short_query);
    RUN_TEST(test_dns_response_rejects_pointer_in_query);
    RUN_TEST(test_dns_response_rejects_qname_overrun);
    RUN_TEST(test_dns_response_rejects_buffer_too_small_for_answer);
    RUN_TEST(test_dns_response_handles_aaaa_query);
    RUN_TEST(test_dns_response_handles_multilabel_qname);
    RUN_TEST(test_dns_response_handles_max_label_length);
    RUN_TEST(test_dns_response_qname_terminated_exactly_at_buffer_end);
    RUN_TEST(test_dns_response_writes_supplied_ip);

    RUN_TEST(test_apply_form_writes_all_string_fields);
    RUN_TEST(test_apply_form_empty_password_keeps_stored_value);
    RUN_TEST(test_apply_form_empty_pair_key_keeps_stored_value);
    RUN_TEST(test_apply_form_non_empty_password_overwrites);
    RUN_TEST(test_apply_form_unchecked_static_ip_is_zero);
    RUN_TEST(test_apply_form_checked_static_ip_is_one);
    RUN_TEST(test_apply_form_static_ip_other_value_treated_as_off);
    RUN_TEST(test_apply_form_blank_ssid_overwrites);
    RUN_TEST(test_apply_form_missing_fields_left_untouched);
    RUN_TEST(test_apply_form_url_encoded_values_decoded_before_storage);
    RUN_TEST(test_apply_form_static_ip_disabled_after_being_enabled);

    RUN_TEST(test_apply_appliance_writes_all_string_fields);
    RUN_TEST(test_apply_appliance_missing_fields_left_untouched);
    RUN_TEST(test_apply_appliance_empty_string_overwrites);
    RUN_TEST(test_apply_appliance_url_encoded_values_decoded_before_storage);
    RUN_TEST(test_apply_appliance_num_phases_valid_values_pass_through);
    RUN_TEST(test_apply_appliance_num_phases_out_of_range_clamps_to_one);
    RUN_TEST(test_apply_appliance_num_phases_unparseable_clamps_to_one);
    RUN_TEST(test_apply_appliance_num_phases_absent_defaults_to_one);

    RUN_TEST(test_load_escaped_uses_stored_value);
    RUN_TEST(test_load_escaped_falls_back_to_default_when_absent);
    RUN_TEST(test_load_escaped_escapes_html_sensitive_chars);
    RUN_TEST(test_load_escaped_handles_null_default);
    RUN_TEST(test_load_escaped_truncates_on_small_buffer);

    RUN_TEST(test_multipart_boundary_simple);
    RUN_TEST(test_multipart_boundary_quoted);
    RUN_TEST(test_multipart_boundary_missing);
    RUN_TEST(test_multipart_boundary_trailing_params);
    RUN_TEST(test_multipart_boundary_truncates_silently);

    RUN_TEST(test_multipart_file_part_extracts_payload);
    RUN_TEST(test_multipart_file_part_with_binary_payload);
    RUN_TEST(test_multipart_file_part_missing_boundary_in_body);
    RUN_TEST(test_multipart_file_part_no_filename_skipped);
    RUN_TEST(test_multipart_file_part_picks_file_after_text_field);
    RUN_TEST(test_multipart_file_part_empty_payload);
    RUN_TEST(test_multipart_file_part_truncated_body);
    RUN_TEST(test_multipart_file_part_payload_with_crlf_inside);

    RUN_TEST(test_routes_table_is_non_empty);
    RUN_TEST(test_routes_android_probes_present_and_redirect);
    RUN_TEST(test_routes_apple_probes_present_and_redirect);
    RUN_TEST(test_routes_windows_probes_present_and_redirect);
    RUN_TEST(test_routes_firefox_probe_present_and_redirect);
    RUN_TEST(test_routes_noisy_probes_present_and_404);
    RUN_TEST(test_routes_all_uris_start_with_slash);
    RUN_TEST(test_routes_no_duplicate_uris);
    RUN_TEST(test_routes_behavior_is_within_enum_range);

    return UNITY_END();
}
