// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "captive_portal_nvs.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "captive_portal_parsing.h"
#include "definitions.h"
#include "storage_component.h"

void captive_load_escaped(const char *key, const char *default_value, char *escaped_out, size_t escaped_out_size) {
    char raw[128] = {};
    storage_get_configuration(key, raw, sizeof(raw), default_value ? default_value : "");
    captive_html_escape(raw, escaped_out, escaped_out_size);
}

void captive_apply_form_to_nvs(char *body) {
    char ssid[33], pwd[65], ws_uri[129], pair_key[129];
    char ip_addr[16], ip_netmask[16], ip_gateway[16], ip_static_val[8];
    char tls_enabled_val[8];
    char prefer_wifi_val[8];

    // PEM lives on the heap because CAPTIVE_TLS_PEM_BUF on the httpd-task
    // stack is too generous. Sized to match the textarea's maxlength="4096".
    char *tls_pem = (char *)malloc(CAPTIVE_TLS_PEM_BUF);
    if (tls_pem == nullptr) {
        return;
    }

    bool has_ssid = captive_form_field(body, "wifi_ssid", ssid, sizeof(ssid));
    bool has_pwd = captive_form_field(body, "wifi_password", pwd, sizeof(pwd));
    bool has_ws = captive_form_field(body, "websocket_uri", ws_uri, sizeof(ws_uri));
    bool has_pair_key = captive_form_field(body, "pair_key", pair_key, sizeof(pair_key));
    bool has_ip_addr = captive_form_field(body, "ip_addr", ip_addr, sizeof(ip_addr));
    bool has_ip_mask = captive_form_field(body, "ip_netmask", ip_netmask, sizeof(ip_netmask));
    bool has_ip_gw = captive_form_field(body, "ip_gateway", ip_gateway, sizeof(ip_gateway));
    bool has_ip_static = captive_form_field(body, "ip_static", ip_static_val, sizeof(ip_static_val));
    bool has_tls_enabled = captive_form_field(body, "tls_enabled", tls_enabled_val, sizeof(tls_enabled_val));
    bool has_tls_pem = captive_form_field(body, "tls_pem", tls_pem, CAPTIVE_TLS_PEM_BUF);
    bool has_prefer_wifi = captive_form_field(body, "prefer_wifi", prefer_wifi_val, sizeof(prefer_wifi_val));

    // Persist. Empty password / pair_key means "keep existing" — that's
    // the contract advertised by the GET form's hint text.
    if (has_ssid)
        storage_set_configuration(NVS_STORAGE_KEY_WIFI_SSID, ssid);
    if (has_pwd && pwd[0] != '\0')
        storage_set_configuration(NVS_STORAGE_KEY_WIFI_PSK, pwd);
    if (has_ws)
        storage_set_configuration(NVS_STORAGE_KEY_HEMS_URL, ws_uri);
    if (has_pair_key && pair_key[0] != '\0')
        storage_set_configuration(NVS_STORAGE_KEY_HEMS_PAIR_KEY, pair_key);
    if (has_ip_addr)
        storage_set_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_IP, ip_addr);
    if (has_ip_mask)
        storage_set_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_NETMASK, ip_netmask);
    if (has_ip_gw)
        storage_set_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_GATEWAY, ip_gateway);
    // PEM is also written when empty so a wrong cert can be cleared by
    // submitting the textarea blank.
    if (has_tls_pem)
        storage_set_configuration(NVS_STORAGE_KEY_HEMS_TLS_PEM, tls_pem);

    // Checkbox semantics: absent in body == unchecked == 0. We always write
    // so the user can also turn it off again. Stored as uint16_t for
    // consistency with the storage_component integer overload (same shape
    // as ip_static_flag).
    uint16_t ip_static_flag = (has_ip_static && strcmp(ip_static_val, "1") == 0) ? 1 : 0;
    storage_set_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP, ip_static_flag);

    uint16_t tls_enabled_flag = (has_tls_enabled && strcmp(tls_enabled_val, "1") == 0) ? 1 : 0;
    storage_set_configuration(NVS_STORAGE_KEY_HEMS_TLS_ENABLED, tls_enabled_flag);

    // Same checkbox semantics as ip_static / tls_enabled: absent == unchecked == 0.
    uint16_t prefer_wifi_flag = (has_prefer_wifi && strcmp(prefer_wifi_val, "1") == 0) ? 1 : 0;
    storage_set_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_PREFER_WIFI, prefer_wifi_flag);

    free(tls_pem);
}

void captive_apply_appliance_params_to_nvs(char *body) {
    char name[65], model[65], serial[65], firmware[33], modbus_channel[33],
        modbus_ip[16], modbus_gateway[16], modbus_netmask[16], modbus_unit_id[8],
        modbus_port[8], num_phases_val[8];

    bool has_name = captive_form_field(body, "appliance_name", name, sizeof(name));
    bool has_model = captive_form_field(body, "appliance_model", model, sizeof(model));
    bool has_serial = captive_form_field(body, "appliance_serial", serial, sizeof(serial));
    bool has_firmware = captive_form_field(body, "appliance_firmware", firmware, sizeof(firmware));
    bool has_modbus_channel = captive_form_field(body, "modbus_channel", modbus_channel, sizeof(modbus_channel));
    bool has_modbus_ip = captive_form_field(body, "modbus_ip", modbus_ip, sizeof(modbus_ip));
    bool has_modbus_port = captive_form_field(body, "modbus_port", modbus_port, sizeof(modbus_port));
    bool has_modbus_gateway = captive_form_field(body, "modbus_gateway", modbus_gateway, sizeof(modbus_gateway));
    bool has_modbus_netmask = captive_form_field(body, "modbus_netmask", modbus_netmask, sizeof(modbus_netmask));
    bool has_modbus_unit_id = captive_form_field(body, "modbus_unit_id", modbus_unit_id, sizeof(modbus_unit_id));
    bool has_num_phases = captive_form_field(body, "num_phases", num_phases_val, sizeof(num_phases_val));

    // String fields: write whenever present, including empty — so a user
    // can clear a value by submitting it blank.
    if (has_name)
        storage_set_configuration(NVS_STORAGE_KEY_APPLIANCE_NAME, name);
    if (has_model)
        storage_set_configuration(NVS_STORAGE_KEY_APPLIANCE_MODEL, model);
    if (has_serial)
        storage_set_configuration(NVS_STORAGE_KEY_APPLIANCE_SERIAL, serial);
    if (has_firmware)
        storage_set_configuration(NVS_STORAGE_KEY_APPLIANCE_FIRMWARE, firmware);
    if (has_modbus_ip)
        storage_set_configuration(NVS_STORAGE_KEY_MODBUS_ETH_IP, modbus_ip);
    if (has_modbus_port)
        storage_set_configuration(NVS_STORAGE_KEY_MODBUS_ETH_PORT, modbus_port);
    if (has_modbus_gateway)
        storage_set_configuration(NVS_STORAGE_KEY_MODBUS_ETH_GATEWAY, modbus_gateway);
    if (has_modbus_netmask)
        storage_set_configuration(NVS_STORAGE_KEY_MODBUS_ETH_NETMASK, modbus_netmask);
    if (has_modbus_unit_id)
        storage_set_configuration(NVS_STORAGE_KEY_MODBUS_UNIT_ID, modbus_unit_id);
    if (has_modbus_channel)
        storage_set_configuration(NVS_STORAGE_KEY_MODBUS_CHANNEL, modbus_channel);

    // Dropdown 1/2/3: clamp to the rendered options; anything unparseable
    // falls back to 1 (matches the default the GET handler shows).
    int parsed = has_num_phases ? atoi(num_phases_val) : 1;
    if (parsed != 1 && parsed != 2 && parsed != 3)
        parsed = 1;
    storage_set_configuration(NVS_STORAGE_KEY_NUM_PHASES, (uint16_t)parsed);
}
