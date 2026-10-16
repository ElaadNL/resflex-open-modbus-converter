<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# **Network Component (Ethernet / WiFi)**

## **Overview**

The Network component is responsible for handling all Ethernet and WiFi connectivity. It provides a stable and unified network interface for the system, without containing any application-level logic such as WebSockets or protocol handling.

The component provides network availability to higher-level modules. It does not guarantee the reachability of external systems.

Protocol-level communication, message exchange, HTTP, and WebSocket handling are implemented outside this component.

The implementation lives in `lib/eth_wifi_component/`. AP-specific radio and netif settings (SSID, password, channel, AP IP, DHCP) are applied from `ap_wifi.cpp` immediately after `esp_wifi_start()`. The captive portal component hooks onto the AP that this component sets up, but does not configure it.

---

## **Design Philosophy**

- Strict separation between **transport layer** and **application logic**
- No protocol-specific handling (e.g. WebSockets)
- Lightweight and robust
- APSTA always on, so the captive portal AP stays reachable while STA tries to associate

---

## **Responsibilities**

- Initialize and manage Ethernet connection (LAN8720)
- Initialize and manage WiFi connection (STA and AP mode, always together in APSTA)
- Handle STA reconnect with exponential backoff
- Apply captive-portal AP radio and netif config via `ap_wifi_configure_radio()` / `ap_wifi_configure_netif()`
- Provide the active outbound interface name to the EMS bridge / S2 wrapper
- Apply the user-chosen default route for outbound IP traffic (Ethernet by default, WiFi STA when the prefer-WiFi flag is set)

---

## **Non-Responsibilities**

The component explicitly does NOT handle:

- WebSockets
- HTTP application logic
- EMS protocol handling
- Modbus communication
- Captive portal UI, DNS responder, or form parsing
- Any business logic

These responsibilities belong to higher-level components such as the EMS Bridge, S2 Wrapper, Modbus Mapping component, or the Captive Portal component.

---

## **Threading Model**

Network handling is event-driven:

- The global ESP-IDF event loop (created in `init_eth_wifi()`) dispatches `WIFI_EVENT`, `ETH_EVENT` and `IP_EVENT` notifications to `network_event_handler`.
- STA reconnects are scheduled via a one-shot FreeRTOS timer (`sta_reconn`); the timer callback runs in the FreeRTOS timer service task and only calls the non-blocking `esp_wifi_connect()`.

`init_eth_wifi()` itself blocks during startup waiting for STA association and Ethernet link/IP, then returns.

## **Architecture**

### **Supported Interfaces**

#### **WiFi (EMS / configuration)**

- Used for outbound TLS / WebSocket traffic to the HEMS or CEM
- Runs in `WIFI_MODE_APSTA`:
  - Station mode (STA) — connects to the configured network
  - Access Point mode (AP) — always on, hosts the captive portal for configuration and recovery

#### **Ethernet (Modbus appliance)**

- Used for the Modbus TCP connection to the appliance
- Runs in parallel with WiFi; not a fallback for it
- Brought up via the on-board LAN8720 PHY

---

## **Connection Strategy**

1. Load STA credentials from NVS (`wifi_ssid`, `wifi_psk`), falling back to compile-time defaults
2. Bring WiFi up in APSTA mode, register event handlers
3. Before starting WiFi, read the captive-portal-managed static IP keys (`net_use_stat_ip`, `net_ip_addr`, `net_netmask`, `net_gateway`); when the flag is set and all three addresses parse, stop the DHCP client and apply the static config to the STA netif. Any validation failure falls back to DHCP with a warning.
4. Start WiFi; the STA association attempt begins
5. Immediately apply AP radio and AP netif config (`ap_wifi_configure_radio()`, `ap_wifi_configure_netif()`)
6. Block up to 15 s waiting for STA to associate; continue regardless so the captive portal can still come up
7. Bring Ethernet up (DHCP or compile-time static IP), block up to 3 s for link/IP, continue on timeout
8. Apply the user-chosen default route (`eth_wifi_apply_default_route()`): Ethernet by default, WiFi STA when the prefer-WiFi flag is set
9. On STA disconnect, schedule a single reconnect with exponential backoff (2 s → 60 s, reset on success)

---

## **State Model**

Internal state is held in a FreeRTOS event group:

| Bit | Name | Set when |
| ----- | ------ | ---------- |
| `BIT0` | `WIFI_CONNECTED_BIT` | `IP_EVENT_STA_GOT_IP` |
| `BIT1` | `ETH_LINK_BIT` | `ETHERNET_EVENT_CONNECTED` |
| `BIT2` | `ETH_IP_BIT` | `IP_EVENT_ETH_GOT_IP` |

The bits are cleared on the matching disconnect events. The event group is used internally by `wifi_init()` and `ethernet_init()` to time-box their waits; it is not exposed to other components.

---

## **Public API**

### **Initialization**

`void init_eth_wifi();`

Loads STA credentials from NVS, initializes the global netif and event loop, starts WiFi in APSTA mode, applies AP radio + netif config from `ap_wifi.cpp`, brings up Ethernet, and applies the user-chosen default route (`eth_wifi_apply_default_route()`: Ethernet by default, WiFi STA when the prefer-WiFi flag is set).

---

### **Get Active Interface Name**

`bool eth_wifi_get_active_interface_name(char* interface_name, size_t interface_name_size);`

Returns the underlying netif name of the active outbound interface (the one `eth_wifi_get_active_netif()` returns: Ethernet by default, WiFi STA when the prefer-WiFi flag is set). The EMS bridge passes this into the S2 library so HTTP/WebSocket clients bind to that interface explicitly. Returns `false` if the netif cannot be resolved.

---

### **Get Active Netif**

`esp_netif_t *eth_wifi_get_active_netif();`

Resolves which netif should carry outbound IP traffic right now. Honours `NVS_STORAGE_KEY_HEMS_NETWORK_PREFER_WIFI`: WiFi STA when the flag is set, otherwise Ethernet when present, falling back to WiFi STA. Returns `nullptr` when neither netif exists yet (very early in boot).

---

### **Apply Default Route**

`bool eth_wifi_apply_default_route();`

Reads `NVS_STORAGE_KEY_HEMS_NETWORK_PREFER_WIFI` and points the lwIP default netif at WiFi STA when the flag is set, otherwise at Ethernet (with WiFi STA as fallback). Safe to call again after the user toggles the flag; in practice the captive portal reboots after save, so a single call from boot suffices. Returns `false` on error.

---

## **Internal Behavior**

### **STA Reconnect Backoff**

A tight `esp_wifi_connect()` loop on disconnect scans channels and tears down associated AP clients, because APSTA shares a single radio. To prevent this:

- STA disconnects schedule a single reconnect via a one-shot FreeRTOS timer
- Backoff starts at 2 s, doubles on each failed attempt, caps at 60 s
- Backoff resets to 2 s on `WIFI_EVENT_STA_START` and `IP_EVENT_STA_GOT_IP`
- If the timer cannot be created the code falls back to an immediate `esp_wifi_connect()`

### **WiFi Workarounds**

Two well-known quirks are accommodated explicitly:

- `cfg.ampdu_rx_enable = false` — AMPDU RX aggregation silently drops/reorders Android captive-portal probes. Matches the CDFER captive-portal-ESP32 reference.
- `esp_wifi_set_ps(WIFI_PS_NONE)` — forces WiFi power save off so the AP does not miss probes from phones with aggressive sleep timing.

### **STA Authmode Threshold**

`wifi_config.sta.threshold.authmode = WIFI_AUTH_WEP` — refuses fully open APs but accepts everything from WEP upwards. PMF is `capable=true, required=false`.

### **LED Integration**

The Ethernet event handler drives the LED state machine:

- `ETHERNET_EVENT_CONNECTED` → `LED_STATE_WAITING_FOR_DEVICE`
- `ETHERNET_EVENT_DISCONNECTED` → `LED_STATE_WAITING_FOR_ETHERNET`

`wifi_init()` sets `LED_STATE_WAITING_FOR_WIFI` at start.

---

## **Configuration**

STA credentials, the optional STA static-IP config, and the AP password live in NVS:

| NVS Key | Description | Default |
| --------- | ------------- | --------- |
| `wifi_ssid` | STA SSID | `DEFAULT_WIFI_SSID` (compile-time) |
| `wifi_psk` | STA password | `DEFAULT_WIFI_PASS` (compile-time) |
| `net_use_stat_ip` | STA static IP enable flag (`uint16_t`, 0/1) | 0 → DHCP |
| `net_ip_addr` | STA static IPv4 address | empty |
| `net_netmask` | STA static IPv4 netmask | empty |
| `net_gateway` | STA static IPv4 gateway | empty |
| `ap_password` | AP password | random 8-char alphanumeric (A-Z0-9) generated on first boot, persisted |

The STA static-IP values are validated with the strict dotted-decimal parser in `eth_wifi_ip_parse.{cpp,h}` before being passed to the netif. Anything that does not parse — empty fields, leading zeros, hex prefixes, out-of-range octets — is treated as "fall back to DHCP" and logged at `WARN`.

The Ethernet IP is taken from compile-time constants `LOCAL_FIXED_IP`, `LOCAL_FIXED_NETMASK`, `LOCAL_FIXED_GATEWAY`. If `LOCAL_FIXED_IP` is empty the DHCP client is used; otherwise the static config is applied and DHCP stops.

AP radio/netif defaults (from `ap_wifi.h`):

| Parameter | Default |
| ----------- | --------- |
| AP SSID prefix | `S2MB_` (MAC-derived suffix appended at runtime) |
| AP channel | 6 |
| AP max clients | 4 |
| AP IP / gateway | 4.3.2.1 |
| AP netmask | /24 |
| Authmode | WPA2-PSK (falls back to OPEN if stored password < 8 chars) |

The AP IP is deliberately in public address space — Samsung Android refuses to show the captive-portal popup when the gateway sits in RFC-1918 space.

---

## **Error Handling**

- STA disconnects: schedule reconnect with backoff; never fatal
- STA does not associate within timeout: log warning, continue so captive portal can come up
- Ethernet link/IP not seen within timeout: log warning, continue on WiFi only
- `esp_netif_set_default_netif()` fails: logged, function returns `false`

---

## **Logging**

Logs under tag `ETH_WFI`:

- STA disconnect reason and next backoff value
- STA reconnect attempts
- AP client connect/disconnect with MAC and AID
- STA / Ethernet "got IP" with address
- Ethernet driver lifecycle
- Static-IP vs DHCP decision
- Non-fatal timeouts when STA or Ethernet does not come up in time

Example:

`[ETH_WFI] IP Address received from WiFi: 192.168.1.50 [ETH_WFI] Ethernet connected! [ETH_WFI] STA reconnect attempt (next backoff if it fails: 4000 ms)`

---

## **Limitations**

- No public state-machine API or event-emit interface to higher layers; consumers learn about link state implicitly via the EMS bridge / S2 wrapper and via LED state
- The default route is applied once at startup from the prefer-WiFi flag via `eth_wifi_apply_default_route()`; changing it takes effect after the captive-portal save reboots the device
- No de-init or stop API
- Dependent on external modules or components for higher-level communication

---

## **Best Practices**

- Keep all operations non-blocking
- Always use timeouts for network calls
- Do not embed application logic in this component
- Treat link state as an implicit signal; do not poll for it

---

## **Summary**

The Network component provides:

- Reliable, concurrent Ethernet and APSTA WiFi connectivity
- Clean separation from application logic
- A stable AP for the captive portal regardless of STA state
- A way for the S2 wrapper to bind outbound traffic to WiFi specifically

It acts purely as a **transport layer**, enabling other modules or components to operate independently of network complexity.
