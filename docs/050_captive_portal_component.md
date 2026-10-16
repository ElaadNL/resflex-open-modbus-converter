<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Captive Portal Component

## Status

Current implementation

---

## Overview

The Captive Portal Component provides a local configuration UI that remains reachable through the device's WiFi access point, even when the normal station connection is missing or misconfigured.

The implementation is split over two areas:

- `eth_wifi_component/ap_wifi.*` configures the AP radio and AP network interface.
- `captive_portal_component/*` starts the wildcard DNS responder, HTTP server, configuration pages and form parsing.

`main.cpp` calls `init_eth_wifi()` first and then starts the portal with `captive_portal_start()`.

---

## Responsibilities

- Keep a WiFi AP reachable while the device also tries to connect as STA.
- Generate or load the AP password from NVS.
- Serve DHCP and DNS for AP clients.
- Redirect common OS captive-portal probes to the local config page.
- Render `/config` for WiFi, S2 endpoint, pair key and static IP settings.
- Render `/appliance` for editing the stored appliance JSON template.
- Persist submitted configuration values to NVS.
- Reboot the device after saving configuration that must be reloaded during startup.

---

## Non-Responsibilities

The component does not:

- Connect to the configured external WiFi network. That is handled by the Ethernet/WiFi component.
- Validate appliance JSON before storing it.
- Start Modbus, EMS bridge or S2 directly.
- Authenticate HTTP access to the portal pages.
- Manage the S2 access token produced after successful pairing.

---

## Startup Flow

1. `main.cpp` initializes storage.
2. `main.cpp` calls `init_eth_wifi()`.
3. The Ethernet/WiFi component creates default STA and AP netifs.
4. WiFi is configured in `WIFI_MODE_APSTA`.
5. `ap_wifi_configure_radio()` applies AP SSID, password, channel and auth mode.
6. `ap_wifi_configure_netif()` configures the AP IP, gateway, netmask, DHCP and DNS option.
7. `main.cpp` calls `captive_portal_start()`.
8. `captive_portal_start()` starts the wildcard DNS responder.
9. `captive_portal_start()` starts the HTTP server and registers all portal routes.

The current code configures AP radio and netif from the Ethernet/WiFi component, not from `captive_portal_start()`.

---

## AP Configuration

The AP SSID is generated as:

```cpp
S2MB_ + MAC bytes 3..5 as uppercase hex
```

The AP password is read from NVS key:

```cpp
ap_password
```

If the key is empty, the code generates a random 8-character alphanumeric (A-Z0-9) password. It is stored in NVS so it remains stable after reboot.

If the stored AP password is shorter than 8 characters, the AP falls back to `WIFI_AUTH_OPEN`, because WPA2-PSK requires at least 8 characters.

Default AP constants:

- SSID prefix: `S2MB_`
- channel: `6`
- max clients: `4`
- AP IP: `4.3.2.1`

---

## DNS Responder

The DNS responder runs in a FreeRTOS task named `captive_dns` on UDP port `53`.

It answers every valid DNS query with one A-record pointing to the AP IP. This makes phones and laptops route captive-portal probe URLs to the local HTTP server.

The DNS task:

- binds to `INADDR_ANY`
- uses a 1 second receive timeout
- loops while `s_dns_task_run` is true
- self-deletes when stopped

Malformed DNS queries are dropped.

---

## HTTP Server

The HTTP server is an `esp_http_server` instance with:

- `max_uri_handlers = 20`
- `lru_purge_enable = true`
- `max_req_hdr_len = 1024`
- `stack_size = 8192`

Registered local pages:

- `GET /config`
- `POST /config`
- `GET /appliance`
- `POST /appliance`

The root route `/` redirects to `/config`.

---

## Captive Portal Probe Routes

OS and browser probe routes are declared in `captive_portal_routes.cpp`.

Most probes redirect to:

```text
http://<AP IP>/config
```

Examples:

- `/generate_204`
- `/hotspot-detect.html`
- `/ncsi.txt`
- `/connecttest.txt`
- `/canonical.html`

Firefox `/success.txt` returns an empty `200 OK` response so Firefox treats the network as captive.

Noisy probes such as `/wpad.dat` and `/favicon.ico` return `404`.

---

## `/config` Page

The `/config` page stores regular device settings.

Rendered fields:

- WiFi STA SSID
- WiFi STA password
- S2 / websocket endpoint URI
- S2 pair key
- TLS-enabled checkbox
- TLS root CA certificate (PEM)
- static IP checkbox
- static IP address
- netmask
- gateway
- prefer-WiFi checkbox (use WiFi instead of Ethernet as the default outbound route)

Passwords, pair keys and the TLS PEM are not echoed back in the HTML form unless already stored.

On `POST /config`, the body is parsed as `application/x-www-form-urlencoded`. The body is rejected if it is empty or larger than `1024` bytes.

Persisted NVS keys:

- `wifi_ssid`
- `wifi_psk`
- `hems_url`
- `pair_key`
- `hems_tls_en` (TLS enable flag)
- `hems_tls_pem` (TLS root CA PEM)
- `net_use_stat_ip` (NVS keys are truncated to 15 chars by ESP-IDF)
- `net_ip_addr`
- `net_netmask`
- `net_gateway`
- `net_prefer_wifi` (prefer WiFi over Ethernet as the default outbound route)

Empty WiFi password and pair key fields are skipped so the existing stored value is kept.

The static-IP values are applied to the WiFi STA netif at boot by `eth_wifi_component`. They are validated before being passed to the netif; any invalid or empty value falls back to DHCP with a warning. See the [Network component](040_network_component) for the boot-time application path.

After a successful save, a short-lived task waits 1500 ms and calls `esp_restart()`.

---

## `/appliance` Page

The `/appliance` page edits the device template JSON stored under:

```cpp
NVS_STORAGE_KEY_JSON_TEMPLATE
```

The page shows the currently stored JSON in a textarea.

Limits:

- maximum encoded POST body: `13312` bytes
- intended maximum JSON size: `4096` bytes

On `POST /appliance`, the decoded `appliance_config` field is written to NVS. The current handler checks that the field exists and is not empty, but it does not parse or validate the JSON before storing it.

After save, the device reboots through the same delayed reboot task used by `/config`.

---

## Internal File Layout

The component is split across small files, each with a single responsibility:

| File | Responsibility |
| --- | --- |
| `captive_portal_component.{cpp,h}` | Public `captive_portal_start` / `captive_portal_stop` orchestrator. |
| `captive_portal_dns.{cpp,h}` | UDP/53 wildcard DNS responder task and start/stop lifecycle. ESP-only. |
| `captive_portal_dns_message.{cpp,h}` | Pure DNS A-response builder. ESP-independent — used by native tests. |
| `captive_portal_html_fragments.{cpp,h}` | Raw HTML / CSS / JS string literals (pure data). |
| `captive_portal_http_server.{cpp,h}` | `esp_http_server` lifecycle (start/stop, `register_uri` helper). |
| `captive_portal_http_handlers.{cpp,h}` | Per-URI handler functions and HTML page renderers; registration entry point. |
| `captive_portal_http_routes.{cpp,h}` | OS phone-home route table + types. ESP-independent. |
| `captive_portal_nvs.{cpp,h}` | NVS-write policies for the submitted form fields. |
| `captive_portal_parsing.{cpp,h}` | Pure ESP-independent parser helpers (URL decode, form-field extraction, HTML escape, multipart). |

Helpers reachable from the native test environment:

- URL decoding, form-field extraction, HTML escaping, multipart boundary and file-part parsing — in `captive_portal_parsing.cpp`.
- DNS A-response construction — in `captive_portal_dns_message.cpp`.
- NVS write policies (`captive_apply_form_to_nvs`, `captive_apply_appliance_params_to_nvs`, `captive_load_escaped`) — in `captive_portal_nvs.cpp`.
- Route table walk — via `captive_portal_http_routes.h`.

The active `/config` and `/appliance` handlers use URL-encoded form parsing.

---

## Error Handling

Startup errors:

- DNS task creation failure
- HTTP server start failure

HTTP request errors:

- empty or oversized body
- failed request receive
- missing appliance JSON field
- empty appliance JSON
- failed NVS write
- out-of-memory during request buffering

If HTTP server startup fails after DNS has already started, the DNS responder is stopped again.

---

## Current Limitations

- The portal has no HTTP login or access control.
- The AP password is not editable through the current `/config` form.
- `/appliance` stores JSON with only basic js validation in the frontend.
- Some comments say `captive_portal_start()` applies AP radio/netif configuration, but the current code applies that from `eth_wifi_component`.

---

## Summary

The Captive Portal Component gives installers a local recovery and configuration path through the device AP.

The AP side provides stable access, DHCP and DNS. The portal side serves `/config` and `/appliance`, stores submitted values in NVS, and reboots so startup-time components can reload the new configuration.
