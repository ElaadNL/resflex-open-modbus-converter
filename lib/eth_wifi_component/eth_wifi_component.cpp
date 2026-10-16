// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include <modbus_mapping_component.h> // Has to be on top due to a conflict between ESP-IDF and Arduino

#include "ap_wifi.h"
#include "eth_wifi_ip_parse.h"

#include <stdio.h>
#include <cstring>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/timers.h"
#include "esp_wifi.h"
#include "esp_eap_client.h"
#include "esp_eth.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

#include <led_component.h>
#include <storage_component.h>

#include <definitions.h>
#include <modbus_value_utils.h>
#include <lwip/if_api.h>

// This component only brings the WiFi stack up in APSTA mode and creates
// both netifs. All AP-specific configuration (SSID, password, channel, IP,
// DHCP) lives in lib/eth_wifi_component/ap_wifi.cpp and is
// applied from captive_portal_start() after init_eth_wifi() returns.


// --- Configuration ---
#define MAX_STRING_SIZE 50

static char wifi_ssid[MAX_STRING_SIZE];
static char wifi_password[MAX_STRING_SIZE];

static const char *TAG = "ETH_WFI";
static const TickType_t ETH_WAIT_TIMEOUT = pdMS_TO_TICKS(3000);

// FreeRTOS event group wait for network status
static EventGroupHandle_t network_event_group;
const int WIFI_CONNECTED_BIT = BIT0;
const int ETH_LINK_BIT = BIT1;
const int ETH_IP_BIT = BIT2;

// STA reconnect backoff: APSTA shares one radio, so a tight reconnect loop
// scans channels and tears down the captive-portal AP association. We back
// off exponentially (2s, 4s, 8s, ... capped at 60s) and reset on success.
static const uint32_t STA_BACKOFF_MIN_MS = 2000;
static const uint32_t STA_BACKOFF_MAX_MS = 60000;
static TimerHandle_t sta_reconnect_timer = nullptr;
static uint32_t sta_backoff_ms = STA_BACKOFF_MIN_MS;

/**
 * @brief FreeRTOS timer callback that triggers a single STA reconnect attempt.
 *
 * Fires once when the backoff timer elapses. Runs in the FreeRTOS timer
 * service task — must stay short, no blocking calls. esp_wifi_connect() is
 * non-blocking: it just kicks off association and returns immediately.
 * If it fails, WIFI_EVENT_STA_DISCONNECTED will re-arm us via
 * schedule_sta_reconnect() with a larger backoff.
 */
static void sta_reconnect_timer_cb(TimerHandle_t /*xTimer*/) {
    ESP_LOGI(TAG, "STA reconnect attempt (next backoff if it fails: %u ms)",
             (unsigned)sta_backoff_ms);
    esp_wifi_connect();
}

/**
 * @brief Schedule the next STA reconnect attempt with exponential backoff.
 *
 * Schedules the next STA reconnect attempt after `sta_backoff_ms`, then
 * doubles the backoff (capped at STA_BACKOFF_MAX_MS) so each failed attempt
 * waits longer than the last. We do NOT call esp_wifi_connect() directly
 * from the disconnect event handler, because each call triggers a scan that
 * hops channels on the shared APSTA radio — that's what was knocking the
 * captive-portal AP clients off. The backoff is reset to STA_BACKOFF_MIN_MS
 * on STA_START and on STA_GOT_IP, so a successful connect always returns
 * us to a fast-retry baseline.
 *
 * Lazily creates the FreeRTOS timer on first use. If timer creation fails
 * (out of memory), we fall back to an immediate esp_wifi_connect() — worse
 * than backoff, but better than never reconnecting at all.
 */
static void schedule_sta_reconnect(void) {
    if (sta_reconnect_timer == nullptr) {
        sta_reconnect_timer = xTimerCreate("sta_reconn",
                                           pdMS_TO_TICKS(sta_backoff_ms),
                                           pdFALSE, nullptr,
                                           sta_reconnect_timer_cb);
        if (sta_reconnect_timer == nullptr) {
            ESP_LOGE(TAG, "Failed to create STA reconnect timer; falling back to immediate connect");
            esp_wifi_connect();
            return;
        }
    }
    // ChangePeriod also starts the timer, but we call Start explicitly in
    // case the timer was previously stopped (e.g. after a successful connect
    // that was then lost again).
    xTimerChangePeriod(sta_reconnect_timer, pdMS_TO_TICKS(sta_backoff_ms), 0);
    xTimerStart(sta_reconnect_timer, 0);

    // Double for next time, saturating at the max. Reset happens in the
    // event handler on STA_START / STA_GOT_IP.
    uint32_t next = sta_backoff_ms * 2;
    sta_backoff_ms = (next > STA_BACKOFF_MAX_MS) ? STA_BACKOFF_MAX_MS : next;
}

/**
 * @brief Unified event handler for WIFI_EVENT, ETH_EVENT and IP_EVENT.
 *
 * Dispatches on @p event_base + @p event_id. STA disconnects schedule a
 * reconnect via @ref schedule_sta_reconnect; STA_START and STA_GOT_IP reset
 * the backoff. Ethernet link/disconnect updates the LED state machine. AP
 * client connect/disconnect events are logged. STA and ETH "got IP" events
 * raise the corresponding bit in @c network_event_group so waiters in
 * @ref ethernet_init and @ref wifi_init unblock.
 */
// --- WiFi Event Handler ---
static void network_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        sta_backoff_ms = STA_BACKOFF_MIN_MS;
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "Connection with AP lost, reason=%d, reconnect scheduled in %u ms", d->reason, (unsigned)sta_backoff_ms);
        xEventGroupClearBits(network_event_group, WIFI_CONNECTED_BIT);
        schedule_sta_reconnect();
    }
    else if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet interface started");
                break;
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet connected!");
                xEventGroupSetBits(network_event_group, ETH_LINK_BIT);
                set_LED_state(LED_STATE_WAITING_FOR_DEVICE);
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGI(TAG, "Ethernet disconnected!");
                xEventGroupClearBits(network_event_group, ETH_LINK_BIT | ETH_IP_BIT);
                set_LED_state(LED_STATE_WAITING_FOR_ETHERNET);
                break;
        }
        // ap events
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
        ESP_LOGI(TAG, "AP client connected: " MACSTR " (aid=%d)", MAC2STR(event->mac), event->aid);
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
        ESP_LOGI(TAG, "AP client disconnected: " MACSTR " (aid=%d)", MAC2STR(event->mac), event->aid);
        // sta events
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP Address received from WiFi: " IPSTR, IP2STR(&event->ip_info.ip));
        sta_backoff_ms = STA_BACKOFF_MIN_MS;
        xEventGroupSetBits(network_event_group, WIFI_CONNECTED_BIT);
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP Address received from Ethernet: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(network_event_group, ETH_IP_BIT);
    }
}

/**
 * @brief Bring up the on-board LAN8720 PHY and the ESP-IDF Ethernet driver.
 *
 * Powers the PHY via @c ETH_PHY_RST_GPIO, installs the EMAC + PHY drivers,
 * attaches them to a new ETH netif, and applies either DHCP or a static IP
 * config based on @c LOCAL_FIXED_IP. Registers @ref network_event_handler
 * for ETH_EVENT and IP_EVENT_ETH_GOT_IP, then waits up to
 * @c ETH_WAIT_TIMEOUT for link or got-ip (whichever applies). If no Ethernet
 * is detected within that window the function returns and the system
 * continues on WiFi only.
 *
 * Pre-condition: @c esp_netif_init and @c esp_event_loop_create_default have
 * already been called by @ref init_eth_wifi.
 */
// --- Ethernet Initialization ---
static void ethernet_init(void) {
    set_LED_state(LED_STATE_WAITING_FOR_ETHERNET);

    // Start the ETH oscillator
    const gpio_num_t PHY_POWER_PIN = (gpio_num_t)ETH_PHY_RST_GPIO;

    gpio_reset_pin(PHY_POWER_PIN);
    gpio_set_direction(PHY_POWER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(PHY_POWER_PIN, 1); // Enable
    vTaskDelay(pdMS_TO_TICKS(100));   // Give the chip some time to start

    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&cfg);

    // Initialize MAC and PHY structs
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

    esp32_emac_config.smi_gpio.mdc_num = ETH_MDC_GPIO;
    esp32_emac_config.smi_gpio.mdio_num = ETH_MDIO_GPIO;

    // Configure RMII clock
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_OUT;

    // Ensure the clock uses GPIO 0
    esp32_emac_config.clock_config.rmii.clock_gpio = EMAC_APPL_CLK_OUT_GPIO;

    // Configure LAN8720 parameters
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = -1; // Reset is done via PHY_POWER_PIN

    // ESP-IDF v5 takes esp_eth_mac_new_esp32 as two configurations as argument!
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = nullptr;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

    // --- START IP CONFIGURATION (DHCP vs STATIC) ---
    const char *local_fixed_ip = LOCAL_FIXED_IP;
    const char *local_fixed_netmask = LOCAL_FIXED_NETMASK;
    const char *local_fixed_gateway = LOCAL_FIXED_GATEWAY;

    const bool ethernet_static_ip = local_fixed_ip != nullptr && strlen(local_fixed_ip) > 0;

    if (ethernet_static_ip) {
        ESP_LOGI(TAG, "Static IP configured. Stop DHCP...");

        // Stop DHCP client
        ESP_ERROR_CHECK(esp_netif_dhcpc_stop(eth_netif));

        // Add fixed IP information
        esp_netif_ip_info_t ip_info;

        // Use ESP-IDF wrappers and not ip4addr_aton!
        esp_netif_str_to_ip4(local_fixed_ip, &ip_info.ip);
        esp_netif_str_to_ip4(local_fixed_netmask, &ip_info.netmask);
        esp_netif_str_to_ip4(local_fixed_gateway, &ip_info.gw);

        // Apply config to interface
        ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netif, &ip_info));
        ESP_LOGI(TAG, "Static IP set: %s", local_fixed_ip);
    }
    else {
        ESP_LOGI(TAG, "No static IP. DHCP remains active.");
        // Nothing to be done, ESP-IDF starts using DHCP.
    }

    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &network_event_handler, nullptr));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    const EventBits_t eth_ready_bit = ethernet_static_ip ? ETH_LINK_BIT : ETH_IP_BIT;

    ESP_LOGI(TAG, "Ethernet initialization done. Waiting briefly for optional Ethernet...");
    EventBits_t bits = xEventGroupWaitBits(
        network_event_group,
        eth_ready_bit,
        pdFALSE,
        pdFALSE,
        ETH_WAIT_TIMEOUT);

    if ((bits & eth_ready_bit) == 0) {
        ESP_LOGW(TAG, "No optional Ethernet %s detected, continuing with WiFi only.",
                 ethernet_static_ip ? "link" : "IP");
    }
}

// --- Wifi Initialization ---
static const TickType_t STA_CONNECT_TIMEOUT = pdMS_TO_TICKS(15000);

/**
l * @brief Apply static IP / netmask / gateway to the WiFi STA netif from NVS.
 *
 * Reads the captive-portal-managed keys NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP
 * (uint16_t flag), NVS_STORAGE_KEY_HEMS_NETWORK_IP, _NETMASK and _GATEWAY.
 * When the flag is set and all three address fields parse to valid IPv4
 * addresses, the DHCP client is stopped and the static configuration is
 * applied. On any problem — flag off, missing field, parse failure, or
 * esp_netif call failure — we log a warning and leave DHCP active so the
 * user can still recover via the captive portal.
 *
 * Must be called after esp_netif_create_default_wifi_sta() and before
 * esp_wifi_start(), so the DHCP client has not yet started and the netif
 * still accepts static config without races.
 */
static void apply_sta_static_ip_from_nvs(void) {
    uint16_t use_static = 0;
    storage_get_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_STATIC_IP, use_static, (uint16_t)0);
    if (use_static == 0) {
        ESP_LOGI(TAG, "STA static IP disabled in NVS — using DHCP.");
        return;
    }

    char ip_str[16] = {};
    char netmask_str[16] = {};
    char gateway_str[16] = {};
    storage_get_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_IP, ip_str, sizeof(ip_str), "");
    storage_get_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_NETMASK, netmask_str, sizeof(netmask_str), "");
    storage_get_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_GATEWAY, gateway_str, sizeof(gateway_str), "");

    if (ip_str[0] == '\0' || netmask_str[0] == '\0' || gateway_str[0] == '\0') {
        ESP_LOGW(TAG, "STA static IP enabled but one or more address fields are empty (ip='%s' netmask='%s' gateway='%s') — falling back to DHCP.",
                 ip_str, netmask_str, gateway_str);
        return;
    }

    // Strict pre-validation so a typo in the captive portal never reaches the
    // netif layer. esp_netif_str_to_ip4 is more permissive than what we want
    // for user-typed input (e.g. it accepts "0xC0.0xA8.0.1").
    uint8_t ip_octets[4];
    uint8_t netmask_octets[4];
    uint8_t gateway_octets[4];
    if (!eth_wifi_parse_ipv4(ip_str, ip_octets) || !eth_wifi_parse_ipv4(netmask_str, netmask_octets) || !eth_wifi_parse_ipv4(gateway_str, gateway_octets)) {
        ESP_LOGW(TAG, "STA static IP enabled but one or more address fields failed to parse (ip='%s' netmask='%s' gateway='%s') — falling back to DHCP.",
                 ip_str, netmask_str, gateway_str);
        return;
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif == nullptr) {
        ESP_LOGW(TAG, "STA static IP enabled but WIFI_STA_DEF netif not found — falling back to DHCP.");
        return;
    }

    esp_netif_ip_info_t ip_info = {};
    if (esp_netif_str_to_ip4(ip_str, &ip_info.ip) != ESP_OK || esp_netif_str_to_ip4(netmask_str, &ip_info.netmask) != ESP_OK || esp_netif_str_to_ip4(gateway_str, &ip_info.gw) != ESP_OK) {
        ESP_LOGW(TAG, "STA static IP enabled but address fields passed pre-validation but esp_netif_str_to_ip4 rejected them (ip='%s' netmask='%s' gateway='%s') — falling back to DHCP.",
                 ip_str, netmask_str, gateway_str);
        return;
    }

    // Stopping a DHCP client that was never started returns
    // ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED — not an error for us.
    esp_err_t dhcp_err = esp_netif_dhcpc_stop(sta_netif);
    if (dhcp_err != ESP_OK && dhcp_err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) {
        ESP_LOGW(TAG, "Could not stop STA DHCP client (%s) — falling back to DHCP.", esp_err_to_name(dhcp_err));
        return;
    }

    esp_err_t set_err = esp_netif_set_ip_info(sta_netif, &ip_info);
    if (set_err != ESP_OK) {
        ESP_LOGW(TAG, "Could not apply STA static IP info (%s) — falling back to DHCP.", esp_err_to_name(set_err));
        // Best-effort: try to re-arm DHCP so the user can still get an address.
        esp_netif_dhcpc_start(sta_netif);
        return;
    }

    ESP_LOGI(TAG, "STA static IP applied: %s netmask %s gateway %s", ip_str, netmask_str, gateway_str);
}

/**
 * @brief Bring up WiFi in APSTA mode and start associating with the configured STA.
 *
 * Runs WiFi in APSTA mode so the captive portal AP is always reachable next
 * to the regular STA connection. Both netifs are created here and STA creds
 * are set from NVS; the AP side (SSID/pwd/IP/DHCP) is configured from
 * captive_portal_start() — see ap_wifi.cpp.
 *
 * STA threshold authmode is set to @c WIFI_AUTH_WEP so open APs are refused
 * but legacy and modern APs remain accepted. PMF is capable but not required.
 * AMPDU RX is disabled — a known workaround for Android captive-portal probes
 * getting silently dropped/reordered when aggregation is on.
 *
 * Blocks up to @c STA_CONNECT_TIMEOUT waiting for STA to associate. Times out
 * silently so the captive portal can still come up when credentials are
 * missing or wrong.
 */
static void wifi_init(void) {
    set_LED_state(LED_STATE_WAITING_FOR_WIFI);

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // Disable AMPDU RX on the ESP32 WiFi. Well-known workaround for a bug
    // where Android phones associate fine but captive-portal HTTP probes
    // never come through — AMPDU RX aggregation drops/reorders them
    // silently. Matches CDFER/Captive-Portal-ESP32 reference.
    cfg.ampdu_rx_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, nullptr, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &network_event_handler, nullptr, &instance_got_ip));

    // Initialize the config structure to zero
    wifi_config_t wifi_config = {};

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WEP; // Minimum WEP — refuse open networks, accept legacy and modern APs
    // wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN; // if you realy want this, you can change it to WIFI_AUTH_OPEN (NOT SECURE!)
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    // Safely copy SSID and Password
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", wifi_ssid);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", wifi_password);

    // // Optional: If password is empty, ensure authmode is definitely OPEN -> not secure,
    // if (strnlen(wifi_password, sizeof(wifi_password)) == 0) {
    //     wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    // }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // ESP-IDF requires the DHCP client to be stopped before
    // esp_netif_set_ip_info; doing this before esp_wifi_start keeps the
    // sequence simple — the DHCP client has not been started yet.
    apply_sta_static_ip_from_nvs();

    ESP_ERROR_CHECK(esp_wifi_start());

    ap_wifi_configure_radio(); // SSID, password, channel
    ap_wifi_configure_netif();

    // esp_wifi_deinit resets power save to default; force it off again so
    // the AP doesn't miss probes from phones with aggressive sleep timing.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    vTaskDelay(pdMS_TO_TICKS(100)); // matches the reference's settle delay
    // AP-side config (SSID/pwd/IP/DHCP) is applied by captive_portal_start()
    // — it owns everything captive-portal-specific including the AP itself.

    ESP_LOGI(TAG, "WiFi initialization done. Connecting... to %s", wifi_ssid);
    // Timebox the wait: with no/wrong credentials the captive portal still has
    // to come up, so we can't block forever here.
    EventBits_t bits = xEventGroupWaitBits(network_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, STA_CONNECT_TIMEOUT);
    if ((bits & WIFI_CONNECTED_BIT) == 0) {
        ESP_LOGW(TAG, "STA not connected within timeout — continuing so captive portal can start.");
    }
}


// Read the captive-portal "Use WiFi instead of Ethernet" override. Default
// 0 = use Ethernet when available, fall back to WiFi otherwise.
static bool eth_wifi_user_prefers_wifi() {
    uint16_t prefer_wifi = 0;
    storage_get_configuration(NVS_STORAGE_KEY_HEMS_NETWORK_PREFER_WIFI, prefer_wifi, (uint16_t)0);
    return prefer_wifi != 0;
}

esp_netif_t *eth_wifi_get_active_netif() {
    esp_netif_t *eth_netif = esp_netif_get_handle_from_ifkey("ETH_DEF");
    esp_netif_t *wifi_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    // Explicit user override wins. If the user asked for WiFi but the WiFi
    // netif somehow isn't there yet, fall back to ETH so we don't return
    // nullptr in the common case.
    if (eth_wifi_user_prefers_wifi()) {
        if (wifi_netif != nullptr) {
            return wifi_netif;
        }
        return eth_netif;
    }

    // Default: prefer Ethernet when present, otherwise WiFi.
    if (eth_netif != nullptr) {
        return eth_netif;
    }
    return wifi_netif;
}

bool eth_wifi_get_active_interface_name(char *interface_name, size_t interface_name_size) {
    if (interface_name == nullptr || interface_name_size == 0) {
        return false;
    }

    esp_netif_t *active_netif = eth_wifi_get_active_netif();
    if (active_netif == nullptr) {
        return false;
    }

    char resolved_name[IF_NAMESIZE] = {};
    if (esp_netif_get_netif_impl_name(active_netif, resolved_name) != ESP_OK) {
        return false;
    }

    strlcpy(interface_name, resolved_name, interface_name_size);
    return true;
}

bool eth_wifi_apply_default_route() {
    esp_netif_t *active_netif = eth_wifi_get_active_netif();
    if (active_netif == nullptr) {
        ESP_LOGW(TAG, "No active netif could be found, default route unchanged.");
        return false;
    }

    esp_err_t err = esp_netif_set_default_netif(active_netif);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply default route: %s", esp_err_to_name(err));
        return false;
    }

    const bool prefer_wifi = eth_wifi_user_prefers_wifi();
    ESP_LOGI(TAG, "Default route set to %s (user prefers WiFi: %s).",
             (active_netif == esp_netif_get_handle_from_ifkey("ETH_DEF")) ? "Ethernet" : "WiFi STA",
             prefer_wifi ? "yes" : "no");
    return true;
}



void init_eth_wifi() {
    ESP_LOGW(TAG, "Starting module...");

    /* Read or initialize the configuration parameters from the configuration
    */
    storage_get_configuration(NVS_STORAGE_KEY_WIFI_SSID, wifi_ssid, MAX_STRING_SIZE, DEFAULT_WIFI_SSID);
    storage_get_configuration(NVS_STORAGE_KEY_WIFI_PSK, wifi_password, MAX_STRING_SIZE, DEFAULT_WIFI_PASS);

    // The following two function calls are generic for the ESP32 and can only be called once!
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    network_event_group = xEventGroupCreate();

    // Start Wifi and Ethernet.
    ESP_LOGI(TAG, "Starting network via WiFi...");
    wifi_init();


    ESP_LOGI(TAG, "Starting network via Ethernet...");
    ethernet_init();
    eth_wifi_apply_default_route();
}
