// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "captive_portal_http_handlers.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage_component.h"

#include "../eth_wifi_component/ap_wifi.h"
#include "captive_portal_html_fragments.h"
#include "captive_portal_http_routes.h"
#include "captive_portal_nvs.h"
#include "captive_portal_parsing.h"
#include "definitions.h"

static const char *TAG = "CAPTIVE_HTTP";

// Route table (CAPTIVE_ROUTES / CAPTIVE_ROUTES_COUNT) lives in
// captive_portal_http_routes.cpp so the native test env can link against
// the table without pulling in esp_http_server.

// ── Body-size limits ────────────────────────────────────────────────────────

#define CAPTIVE_POST_MAX_BODY 16384 // /config form body cap (PEM textarea pushes this up from the old 1 KB)
// urlencoded body: "appliance_config=<json>". The textarea value is
// url-encoded by the browser, so the worst case is 3× expansion (every byte
// becomes %XX). 4096 bytes of JSON → up to 12288 encoded + field name + '='.
#define CAPTIVE_APPLIANCE_MAX_BODY 13312
#define CAPTIVE_APPLIANCE_MAX_JSON 4096

// ── Page renderers ──────────────────────────────────────────────────────────
//
// Compose the HTML response from the fragments in
// captive_portal_html_fragments.cpp, NVS-loaded values, and runtime state.
// Static — only the handlers below call them.

static esp_err_t render_config_page(httpd_req_t *req) {
    // Pre-fill from NVS. Buffers are sized for typical config values plus
    // worst-case escape inflation (×6 for &quot; etc.). 256 covers a 40-char
    // value with all-escape characters, which is well above expected input.
    char ssid[256], ws_uri[256], ip_addr[64], ip_mask[64], ip_gw[64];

    captive_load_escaped(NVS_STORAGE_KEY_WIFI_SSID, "", ssid, sizeof(ssid));
    captive_load_escaped(NVS_STORAGE_KEY_HEMS_URL, "", ws_uri, sizeof(ws_uri));
    captive_load_escaped(NVS_STORAGE_KEY_HEMS_NETWORK_IP, "", ip_addr, sizeof(ip_addr));
    captive_load_escaped(NVS_STORAGE_KEY_HEMS_NETWORK_NETMASK, "", ip_mask, sizeof(ip_mask));
    captive_load_escaped(NVS_STORAGE_KEY_HEMS_NETWORK_GATEWAY, "", ip_gw, sizeof(ip_gw));

    // PEM can be up to 4 KB raw — too large for captive_load_escaped's
    // internal 128-byte buffer. Allocate the raw + escaped buffers on the
    // heap so they don't take ~12 KB of permanent BSS for a page that
    // renders only on captive-portal visits.
    char *tls_pem_raw = (char *)malloc(CAPTIVE_TLS_PEM_BUF);
    char *tls_pem_escaped = (char *)malloc(CAPTIVE_TLS_PEM_BUF * 2);
    if (tls_pem_raw == nullptr || tls_pem_escaped == nullptr) {
        free(tls_pem_raw);
        free(tls_pem_escaped);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_OK;
    }
    storage_get_configuration(NVS_STORAGE_KEY_HEMS_TLS_PEM, tls_pem_raw, CAPTIVE_TLS_PEM_BUF, "");
    captive_html_escape(tls_pem_raw, tls_pem_escaped, CAPTIVE_TLS_PEM_BUF * 2);

    uint16_t tls_enabled = 0;
    storage_get_configuration(NVS_STORAGE_KEY_HEMS_TLS_ENABLED, tls_enabled, (uint16_t)0);
    const char *tls_checked = (tls_enabled != 0) ? "checked" : "";
    const char *tls_pem_disabled = (tls_enabled != 0) ? "" : "disabled";

    uint16_t ip_static = 0;
    storage_get_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP, ip_static, (uint16_t)0);
    const char *ip_static_checked = (ip_static != 0) ? "checked" : "";
    const char *ip_fields_disabled = (ip_static != 0) ? "" : "disabled";

    uint16_t prefer_wifi = 0;
    storage_get_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_PREFER_WIFI, prefer_wifi, (uint16_t)0);
    const char *prefer_wifi_checked = (prefer_wifi != 0) ? "checked" : "";

    // Chunked send lets us stream a body bigger than any single resp buffer
    // without needing httpd_resp_send's length-known path.
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send_chunk(req, CAPTIVE_HTML_HEAD_OPEN, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, CAPTIVE_HTML_COMMON_STYLE, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, CAPTIVE_HTML_HEAD_CLOSE_CONFIG, HTTPD_RESP_USE_STRLEN);

    // One buffer reused for each fieldset. 1024 bytes is plenty for any of
    // the snprintf'd sections below.
    char chunk[1024];
    int n;

    n = snprintf(chunk, sizeof(chunk), CAPTIVE_HTML_FIELDSET_WIFI, ssid, prefer_wifi_checked);
    httpd_resp_send_chunk(req, chunk, n);

    // Separate, larger buffer because the PEM textarea easily exceeds the
    // 1 KB `chunk` once a real cert is present. Heap-allocated so this
    // 10 KB doesn't sit in BSS forever for a page that only renders on
    // captive-portal visits.
    const size_t s2_chunk_size = 10240;
    char *s2_chunk = (char *)malloc(s2_chunk_size);
    if (s2_chunk == nullptr) {
        free(tls_pem_raw);
        free(tls_pem_escaped);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_OK;
    }
    int s2_n = snprintf(s2_chunk, s2_chunk_size, CAPTIVE_HTML_FIELDSET_S2,
                        ws_uri, tls_checked, tls_pem_disabled, tls_pem_escaped);
    httpd_resp_send_chunk(req, s2_chunk, s2_n);
    free(s2_chunk);
    free(tls_pem_raw);
    free(tls_pem_escaped);

    n = snprintf(chunk, sizeof(chunk), CAPTIVE_HTML_FIELDSET_NETWORK,
                 ip_static_checked,
                 ip_addr, ip_fields_disabled,
                 ip_mask, ip_fields_disabled,
                 ip_gw, ip_fields_disabled);
    httpd_resp_send_chunk(req, chunk, n);

    httpd_resp_send_chunk(req, CAPTIVE_HTML_TAIL, HTTPD_RESP_USE_STRLEN);

    // Terminate chunked response.
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

static esp_err_t render_appliance_page(httpd_req_t *req) {
    // Read the stored config just to report its size. 4KB + 1 NUL.
    static char stored[4097];
    stored[0] = '\0';
    unsigned stored_len = 0;
    if (storage_get_configuration(NVS_STORAGE_KEY_JSON_TEMPLATE, stored, sizeof(stored), "")) {
        stored_len = (unsigned)strlen(stored);
    }
    else {
        ESP_LOGW(TAG, "Could not read stored JSON template; rendering as empty");
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send_chunk(req, CAPTIVE_HTML_HEAD_OPEN, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, CAPTIVE_HTML_COMMON_STYLE, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, CAPTIVE_HTML_HEAD_CLOSE_APPLIANCE, HTTPD_RESP_USE_STRLEN);

    char chunk[128];
    int n = snprintf(chunk, sizeof(chunk), CAPTIVE_HTML_APPLIANCE_COUNTER, stored_len);
    httpd_resp_send_chunk(req, chunk, n);

    httpd_resp_send_chunk(req, CAPTIVE_HTML_APPLIANCE_TEXTAREA_OPEN, HTTPD_RESP_USE_STRLEN);

    // Pre-fill the textarea with the currently-stored JSON. Worst-case escape
    // inflation is ×6 (e.g. `"` → `&quot;`), so size for 4096 raw bytes.
    // `static` to keep it off the task stack alongside `stored` above.
    static char stored_escaped[4097 * 6];
    captive_html_escape(stored, stored_escaped, sizeof(stored_escaped));
    if (stored_escaped[0] != '\0') {
        httpd_resp_send_chunk(req, stored_escaped, HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_send_chunk(req, CAPTIVE_HTML_APPLIANCE_TEXTAREA_CLOSE, HTTPD_RESP_USE_STRLEN);

    // Pre-fill the appliance parameter fields from NVS. Same sizing rationale
    // as render_config_page above: 256 bytes covers any expected value plus
    // worst-case ×6 HTML-escape inflation.
    // `static` to keep these off the esp_http_server task stack — same
    // reasoning as `stored` / `stored_escaped` above. Five 256-byte locals
    // here plus the 1KB `param_chunk` below overflowed the default httpd
    // task stack on-device (observed: stack overflow after captive portal
    // came up). Living in BSS instead avoids that.
    static char name[256], model[256], serial[256], firmware[256],
        modbus_ip[256], modbus_channel[256],
        modbus_gateway[256], modbus_netmask[256], modbus_unit_id[256],
        modbus_port[256];
    captive_load_escaped(NVS_STORAGE_KEY_APPLIANCE_NAME, "", name, sizeof(name));
    captive_load_escaped(NVS_STORAGE_KEY_APPLIANCE_MODEL, "", model, sizeof(model));
    captive_load_escaped(NVS_STORAGE_KEY_APPLIANCE_SERIAL, "", serial, sizeof(serial));
    captive_load_escaped(NVS_STORAGE_KEY_APPLIANCE_FIRMWARE, "", firmware, sizeof(firmware));
    captive_load_escaped(NVS_STORAGE_KEY_MODBUS_CHANNEL, "", modbus_channel, sizeof(modbus_channel));
    captive_load_escaped(NVS_STORAGE_KEY_MODBUS_ETH_IP, "", modbus_ip, sizeof(modbus_ip));
    captive_load_escaped(NVS_STORAGE_KEY_MODBUS_ETH_PORT, "", modbus_port, sizeof(modbus_port));
    captive_load_escaped(NVS_STORAGE_KEY_MODBUS_ETH_GATEWAY, "", modbus_gateway, sizeof(modbus_gateway));
    captive_load_escaped(NVS_STORAGE_KEY_MODBUS_ETH_NETMASK, "", modbus_netmask, sizeof(modbus_netmask));
    captive_load_escaped(NVS_STORAGE_KEY_MODBUS_UNIT_ID, "", modbus_unit_id, sizeof(modbus_unit_id));

    uint16_t num_phases = 1;
    storage_get_configuration(NVS_STORAGE_KEY_NUM_PHASES, num_phases, (uint16_t)1);
    const char *sel1 = (num_phases == 1) ? " selected" : "";
    const char *sel2 = (num_phases == 2) ? " selected" : "";
    const char *sel3 = (num_phases == 3) ? " selected" : "";

    static char param_chunk[2048];

    // note the order is important!!!
    int pn = snprintf(param_chunk, sizeof(param_chunk), CAPTIVE_HTML_APPLIANCE_PARAMETERS,
                      name, model, serial, firmware, modbus_channel, modbus_ip, modbus_port,
                      modbus_gateway, modbus_netmask, modbus_unit_id, sel1, sel2, sel3);
    httpd_resp_send_chunk(req, param_chunk, pn);

    httpd_resp_send_chunk(req, CAPTIVE_HTML_APPLIANCE_TAIL, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

static esp_err_t render_appliance_saved_page(httpd_req_t *req, size_t saved_len) {
    // The saved-page literal lives in another translation unit, so the
    // previous sizeof() math no longer compiles. The literal is ~800 bytes
    // today; 1024 leaves head-room for the %u expansion and modest growth.
    char chunk[1024];
    int n = snprintf(chunk, sizeof(chunk), CAPTIVE_HTML_APPLIANCE_SAVED, (unsigned)saved_len);

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, chunk, n);
    return ESP_OK;
}

// ── Reboot helper ───────────────────────────────────────────────────────────

/**
 * @brief Short-lived task that reboots the device after a config save.
 *
 * Sleeps long enough for the HTTP "Saved" response to flush, then calls
 * @c esp_restart(). Rebooting directly from the POST handler would cut the
 * response short. Does not return.
 *
 * @param arg Unused.
 */
static void reboot_task(void *arg) {
    vTaskDelay(pdMS_TO_TICKS(1500));
    ESP_LOGI(TAG, "Restarting after config save…");
    esp_restart();
}

// ── HTTP handlers ───────────────────────────────────────────────────────────

/**
 * @brief HTTP handler that 302-redirects any GET to the config page.
 *
 * Used both for the OS phone-home URIs (so the captive-portal sheet pops
 * up on the client) and as the catch-all root handler.
 */
static esp_err_t http_redirect_handler(httpd_req_t *req) {
    // Build the Location URL from the AP IP defines so changing the AP IP
    // (e.g. for the Samsung public-space workaround) is a one-line edit.
    char location[48];
    snprintf(location, sizeof(location), "http://%d.%d.%d.%d/config",
             AP_WIFI_IP_A, AP_WIFI_IP_B,
             AP_WIFI_IP_C, AP_WIFI_IP_D);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

static esp_err_t http_config_get_handler(httpd_req_t *req) {
    return render_config_page(req);
}

/**
 * @brief HTTP handler for `POST /config` — persists submitted form values.
 *
 * Reads the urlencoded body, looks up each known field, writes non-empty
 * values to NVS via storage_component, and schedules a reboot so the new
 * config takes effect. Empty password / access-key fields are skipped on
 * purpose (the GET form promises "leave empty to keep the stored one").
 *
 * Refuses bodies larger than @ref CAPTIVE_POST_MAX_BODY to bound RAM use.
 */
static esp_err_t http_config_post_handler(httpd_req_t *req) {
    if (req->content_len == 0 || req->content_len > CAPTIVE_POST_MAX_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body missing or too large");
        return ESP_OK;
    }

    // +1 for the terminating NUL we add after recv.
    char *body = (char *)malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_OK;
    }

    int received = 0;
    while (received < (int)req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            free(body);
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, nullptr);
            }
            else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            }
            return ESP_OK;
        }
        received += r;
    }
    body[received] = '\0';

    captive_apply_form_to_nvs(body);
    free(body);

    // Confirmation page; reboot from a side task so the response can flush.
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
                       "<!doctype html><html><body style=\"font-family:system-ui,sans-serif;max-width:480px;margin:1em auto;padding:0 1em\">"
                       "<h1>Saved</h1><p>Device is rebooting…</p>"
                       "</body></html>");

    xTaskCreate(reboot_task, "captive_reboot", 2048, nullptr, 5, nullptr);
    return ESP_OK;
}

static esp_err_t http_appliance_get_handler(httpd_req_t *req) {
    return render_appliance_page(req);
}

static esp_err_t http_appliance_post_handler(httpd_req_t *req) {
    if (req->content_len == 0 || req->content_len > CAPTIVE_APPLIANCE_MAX_BODY) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body missing or too large");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "appliance POST content_len=%d", (int)req->content_len);

    char *body = (char *)malloc(req->content_len + 1);
    if (!body) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "out of memory");
        return ESP_OK;
    }

    int received = 0;
    while (received < (int)req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            free(body);
            if (r == HTTPD_SOCK_ERR_TIMEOUT) {
                httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, nullptr);
            }
            else {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "recv failed");
            }
            return ESP_OK;
        }
        received += r;
    }
    body[received] = '\0';
    ESP_LOGI(TAG, "received=%d body_strlen=%u", received, (unsigned)strlen(body));

    // Sized for the *encoded* value: a field full of %XX escapes is up to 3×
    // the decoded JSON, so the decoded form always fits and url-decode only
    // shrinks it. The real size cap is checked on the decoded length below.
    static char json[CAPTIVE_APPLIANCE_MAX_BODY + 1];
    if (!captive_form_field(body, "appliance_config", json, sizeof(json))) {
        free(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing field");
        return ESP_OK;
    }
    // Reject an over-length template instead of persisting one that won't fit
    // the NVS read buffer on boot (the IncompleteInput crash). The limit is on
    // the decoded JSON, which is what actually gets stored and re-parsed.
    if (strnlen(json, sizeof(json)) > CAPTIVE_APPLIANCE_MAX_JSON) {
        free(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "JSON too large (max 4096 bytes after minify)");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "decoded json strlen=%u", (unsigned)strlen(json));

    if (json[0] == '\0') {
        free(body);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "empty json");
        return ESP_OK;
    }

    if (!storage_set_configuration(NVS_STORAGE_KEY_JSON_TEMPLATE, json)) {
        free(body);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "nvs write failed");
        return ESP_OK;
    }

    // Parameter inputs rendered below the JSON textarea (name, model, serial,
    // firmware, modbus_channel, num_phases). Must run before free(body).
    captive_apply_appliance_params_to_nvs(body);
    free(body);

    if (render_appliance_saved_page(req, strnlen(json, sizeof(json))) != ESP_OK) {
        return ESP_OK;
    }
    xTaskCreate(reboot_task, "captive_reboot", 2048, nullptr, 5, nullptr);
    return ESP_OK;
}

/**
 * @brief HTTP handler that returns "204 No Content".
 *
 * Used for phone-home URIs whose clients expect a 204 to mean "internet is
 * reachable" — we *don't* want that for the captive-portal flow, so this
 * handler is currently unused. Kept for cases where we want to silence a
 * specific probe without redirecting it.
 */
static esp_err_t http_204_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

/**
 * @brief 200 OK with an empty body. Used for Firefox' /success.txt probe.
 *
 * Firefox treats an empty body as "not the expected success page" and flags
 * the network as captive, which is what we want so the doorhanger pops up.
 */
static esp_err_t http_200_success_handler(httpd_req_t *req) {
    httpd_resp_send(req, "", 0);
    return ESP_OK;
}

/**
 * @brief HTTP handler that returns "404 Not Found".
 *
 * Wired up for noisy probes like `/wpad.dat` and `/favicon.ico` to stop
 * clients from repeatedly hammering them.
 */
static esp_err_t http_404_handler(httpd_req_t *req) {
    httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, nullptr);
    return ESP_OK;
}

// ── Registration entry point ────────────────────────────────────────────────

void captive_portal_http_handlers_register(httpd_handle_t httpd, captive_portal_register_uri_fn register_uri) {
    // Own pages
    register_uri(httpd, "/config", HTTP_GET, http_config_get_handler);
    register_uri(httpd, "/config", HTTP_POST, http_config_post_handler);
    register_uri(httpd, "/appliance", HTTP_GET, http_appliance_get_handler);
    register_uri(httpd, "/appliance", HTTP_POST, http_appliance_post_handler);

    // Phone-home URIs
    for (size_t i = 0; i < CAPTIVE_ROUTES_COUNT; i++) {
        esp_err_t (*fn)(httpd_req_t *) = nullptr;
        switch (CAPTIVE_ROUTES[i].behavior) {
            case CAPTIVE_ROUTE_REDIRECT:
                fn = http_redirect_handler;
                break;
            case CAPTIVE_ROUTE_200:
                fn = http_200_success_handler;
                break;
            case CAPTIVE_ROUTE_204:
                fn = http_204_handler;
                break;
            case CAPTIVE_ROUTE_404:
                fn = http_404_handler;
                break;
        }
        if (fn != nullptr) {
            register_uri(httpd, CAPTIVE_ROUTES[i].uri, HTTP_GET, fn);
        }
    }

    // Catch-all root → config
    register_uri(httpd, "/", HTTP_GET, http_redirect_handler);
}
