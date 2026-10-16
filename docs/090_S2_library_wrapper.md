<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# S2 Wrapper

## Status

Current implementation

---

## Overview

The S2 Wrapper connects the internal EMS bridge interface to the external S2 client library.

It constructs the S2 resource-manager configuration, handles pairing and websocket startup, converts S2 requests into EMS bridge callbacks, and forwards EMS bridge results back into the S2 client.

The wrapper currently supports S2 PEBC only.

---

## Responsibilities

- Build `s2_client_config_t` from `ems_bridge_setup_values` and `s2_hems_values`.
- Initialize the S2 client.
- Pair with the HEMS/CEM when no access token is stored.
- Store and rotate the S2 access token in NVS.
- Start the S2 websocket connection.
- Map S2 PEBC instruction callbacks to EMS bridge actuator requests.
- Map S2 measurement and parameter requests to EMS bridge sensor requests.
- Map EMS bridge sensor results back to S2 provide calls.
- Signal readiness through the EMS bridge init-ready callback.

---

## Non-Responsibilities

The wrapper does not:

- Read Modbus registers.
- Write Modbus actuators.
- Store WiFi or HEMS settings from the captive portal.
- Decide appliance mappings.
- Validate the appliance JSON template.
- Implement a standalone FreeRTOS task.

---

## Connected Components

### EMS Bridge

`ems_bridge_init()` calls `s2_init()` for `EMS_INTERFACE_S2_PEBC`.

The wrapper receives three callbacks from the EMS bridge:

- request sensor callback
- set actuator callback
- init-ready callback

These callbacks are stored in static wrapper variables and used by S2 event handlers.

### S2 Client Library

The wrapper calls the S2 client API:

- `s2_client_init`
- `s2_client_prepare_pairing`
- `s2_client_request_pairing`
- `s2_client_initiate_connection`
- `s2_client_ws_connect`
- `s2_client_provide_measurement`
- PEBC constraint provide functions

### Storage Component

The wrapper stores the S2 access token under:

```cpp
s2_access_tkn
```

The HEMS URL and pair key are loaded by the EMS bridge before calling `s2_init()`.

---

## Initialization Flow

1. EMS bridge calls `s2_init()`.
2. The wrapper stores the EMS bridge callbacks.
3. `s2_construct_config()` builds an `s2_client_config_t`.
4. `s2_client_init()` creates the S2 client handle.
5. `s2_check_pairing()` checks NVS for `s2_access_tkn`.
6. If no token exists, `s2_do_pairing()` uses the configured pair key.
7. `s2_start_pairing()` calls `s2_client_initiate_connection()`.
8. The returned rotated access token is stored in NVS.
9. `s2_client_ws_connect()` connects to the returned websocket URL with the communication token.
10. When the S2 session is ready, `on_session_ready()` sets the LED to `LED_STATE_ALL_OK` and calls the EMS bridge init-ready callback.

If initialization fails, the wrapper clears its static handle and callback state.

---

## S2 Configuration

`s2_construct_config()` fills these main sections:

- node identity
- server config
- resource-manager config
- PEBC defaults
- roles
- commodity quantities

Node fields are copied from `ems_bridge_setup_values`:

- brand
- logo URI
- type
- model
- user-defined name

---

## Pairing

If `s2_access_tkn` exists in NVS, the wrapper attempts to initiate a connection with that token.

If no token exists:

1. `s2_client_prepare_pairing()` is called.
2. `s2_client_request_pairing()` is called with the configured pair key.
3. The returned access token is saved to NVS.

During `s2_client_initiate_connection()`, the HEMS/CEM can return a rotated access token. The wrapper stores that rotated token immediately because the old token may be invalid after connection startup.

If the stored token is rejected during initiate-connection, the wrapper clears `s2_access_tkn` in NVS.

---

## S2 to EMS Bridge Flow

### Instructions

`on_instruction()` receives S2 PEBC upper and lower limits for a commodity quantity.

The wrapper maps the quantity to an EMS bridge lower and upper actuator:

- L1 power -> min/max power L1
- L2 power -> min/max power L2
- L3 power -> min/max power L3
- three-phase symmetric power -> min/max power total

It then calls the EMS bridge set-actuator callback twice:

- once for the lower limit
- once for the upper limit

Unsupported quantities are ignored.

### Measurement Requests

`on_request_measurement()` maps S2 quantity requests to EMS bridge sensors:

- L1 power -> actual power L1
- L2 power -> actual power L2
- L3 power -> actual power L3
- three-phase symmetric power -> actual power total

Unsupported quantities are ignored.

### Parameter Requests

`on_request_param()` maps S2 PEBC params to EMS bridge sensors for power and energy limit ranges.

The `quantity` and `is_abnormal` arguments are currently not used when choosing the EMS sensor. Requested limit values are later provided to all registered quantities.

### Forecast Requests

`on_request_forecast()` is present but not implemented.

---

## EMS Bridge to S2 Flow

The EMS bridge calls:

```cpp
s2_on_new_sensor_value(ems_bridge_sensor_result result)
```

If the result status is not `EMS_BRIDGE_STATUS_OK`, the wrapper logs and ignores it.

For successful values:

- power limit range sensors call the matching PEBC provide functions
- energy limit range sensors call the matching PEBC provide functions
- actual power sensors call `s2_client_provide_measurement()`

The current limit-range implementation applies the returned value to all configured commodity quantities.

The EMS bridge also calls:

```cpp
s2_on_actuator_set(ems_bridge_set_actuator_result result)
```

This function is currently a stub.

---

## NVS Keys

The wrapper uses:

- `s2_access_tkn`: S2 access token
- `s2_node_id`: S2 node id (UUIDv4, generated and persisted on first boot if empty)

The EMS bridge loads these before calling the wrapper:

- `hems_url`: HEMS/CEM base URL
- `pair_key`: pairing input token

---

## Error Handling

Initialization can fail when:

- S2 client initialization fails
- no access token exists and no pair key is configured
- prepare-pairing fails
- request-pairing fails
- initiate-connection fails
- websocket connection fails

On failures, the wrapper sets `LED_STATE_FATAL_ERROR` and returns an error to the EMS bridge.

Sensor results with error status are ignored instead of forwarded to S2.

---

## Current Limitations

- Only PEBC is implemented.
- Forecasts are not implemented.
- `s2_on_actuator_set()` is currently empty.
- `is_abnormal` is not used in parameter handling.
- Parameter values are applied to all configured quantities instead of only the requested quantity.
- Measurement mapping only handles power quantities.

---

## Summary

The S2 Wrapper is the adapter between the generic EMS bridge API and the S2 client library.

It owns S2 pairing, session startup, callback translation and value forwarding, while Modbus-specific work stays behind the EMS bridge callbacks.
