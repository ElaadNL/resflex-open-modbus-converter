// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// we currently only support this lilygo board
#define BOARD_LILYGO

#ifdef BOARD_LILYGO

// --- UART SETUP
#define RS485_TX 33
#define RS485_RX 32
#define RS485_RTS 14

// --- Ethernet LAN8720 Configuration (LilyGO T-PoE-Pro) ---
#define ETH_PHY_ADDR 0
#define ETH_MDC_GPIO 23
#define ETH_MDIO_GPIO 18
#define ETH_PHY_RST_GPIO 5 // Sometimes -1 when hardware reset isn't connected

// --- LED extension for status information
#define LED_GPIO 4

#else // fallback default, which is same as BOARD_LILYGO

// --- UART SETUP
#define RS485_TX 33
#define RS485_RX 32
#define RS485_RTS 14

// --- Ethernet LAN8720 Configuration (LilyGO T-PoE-Pro) ---
#define ETH_PHY_ADDR 0
#define ETH_MDC_GPIO 23
#define ETH_MDIO_GPIO 18
#define ETH_PHY_RST_GPIO 5 // Sometimes -1 when hardware reset isn't connected

// --- LED extension for status information
#define LED_GPIO 4

#endif


#define DEFAULT_WIFI_SSID "Nothing"
#define DEFAULT_WIFI_PASS "example123"

#define LOCAL_FIXED_IP "" //  // When not set (e.g. nullptr or "")  DHCP will be used
#define LOCAL_FIXED_NETMASK "255.255.255.0"
#define LOCAL_FIXED_GATEWAY ""


// these will be used as defaults
#define MODBUS_DEVICE_IP "172.1.1.1"
#define MODBUS_DEVICE_PORT 502
#define MODBUS_DEVICE_UID 1
#define MODBUS_DEVICE_BAUDRATE 115200

#define DEVICE_DEFAULT_PHASES 1
#define DEVICE_DEFAULT_INSTRUCTION_DELAY_S 0

// 4096 due to nvs limit
#define MAX_JSON_SIZE 4096


// Max PEM buffer used by the captive-portal TLS field, including the
// terminating NUL. Matches the textarea's maxlength="4096" in the form,
// the raw load buffer in the GET handler, the heap allocation in the
// POST parser, and the s2_hems_values cert_pem field.
#define CAPTIVE_TLS_PEM_BUF 4097


// nvs key definitions
#define NVS_STORAGE_KEY_JSON_TEMPLATE "device_template"
#define NVS_STORAGE_KEY_HEMS_URL "hems_url"
#define NVS_STORAGE_KEY_HEMS_PAIR_KEY "pair_key"
#define NVS_STORAGE_KEY_HEMS_TLS_ENABLED "hems_tls_en"
#define NVS_STORAGE_KEY_HEMS_TLS_PEM "hems_tls_pem"
#define NVS_STORAGE_KEY_WIFI_SSID "wifi_ssid"
#define NVS_STORAGE_KEY_WIFI_PSK "wifi_psk"
#define NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP "net_use_stat_ip"
#define NVS_STORAGE_KEY_HEMS_NETWORK_IP "net_ip_addr"
#define NVS_STORAGE_KEY_HEMS_NETWORK_NETMASK "net_netmask"
#define NVS_STORAGE_KEY_HEMS_NETWORK_GATEWAY "net_gateway"
#define NVS_STORAGE_KEY_HEMS_NETWORK_PREFER_WIFI "net_prefer_wifi"
#define NVS_STORAGE_KEY_APPLIANCE_NAME "appliance_name"
#define NVS_STORAGE_KEY_APPLIANCE_MODEL "appl_model"
#define NVS_STORAGE_KEY_APPLIANCE_SERIAL "appl_serial"
#define NVS_STORAGE_KEY_APPLIANCE_FIRMWARE "appl_firmware"
#define NVS_STORAGE_KEY_MODBUS_ETH_IP "appl_ip"
#define NVS_STORAGE_KEY_MODBUS_ETH_PORT "appl_port"
#define NVS_STORAGE_KEY_MODBUS_ETH_GATEWAY "appl_gw"
#define NVS_STORAGE_KEY_MODBUS_ETH_NETMASK "appl_nm"
#define NVS_STORAGE_KEY_MODBUS_UNIT_ID "appl_uid"
#define NVS_STORAGE_KEY_MODBUS_CHANNEL "modbus_channel"
#define NVS_STORAGE_KEY_NUM_PHASES "num_phases"
#define NVS_STORAGE_KEY_S2_NODE_ID "s2_node_id"


/*
 * With MODBUS_DONT_FAIL_WHEN_NOT_EXISTENT defined, missing actuators are silently accepted
 * (single-setpoint appliances, such as many EV chargers, have no lower-bound register).
 */
#define MODBUS_DONT_FAIL_WHEN_NOT_EXISTENT