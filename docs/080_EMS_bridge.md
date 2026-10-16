<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# EMS Bridge

## Status

Current implementation

---

## Overview

The EMS Bridge is the protocol-neutral layer between device control logic and an energy-management protocol implementation.

At the moment, the only supported EMS interface is:

```cpp
EMS_INTERFACE_S2_PEBC
```

The bridge exposes generic EMS sensor and actuator IDs to the Modbus Mapping Component and delegates protocol-specific behavior to the S2 Wrapper.

---

## Responsibilities

- Define EMS-facing setup, sensor, actuator and result types.
- Store the selected EMS interface type.
- Create queues for sensor values and actuator results.
- Start tasks that forward queued results to the selected protocol wrapper.
- Initialize the selected protocol wrapper.
- Load HEMS/CEM URL and pair key from NVS for S2.
- Pass request callbacks from the protocol wrapper to the Modbus Mapping Component.

---

## Non-Responsibilities

The EMS Bridge does not:

- Read or write Modbus registers.
- Parse the appliance JSON template.
- Implement S2 protocol details directly.
- Render or parse captive portal pages.
- Manage WiFi connection setup.
- Interpret Modbus-specific scaling or function codes.

---

## Public API

The main entry point is:

```cpp
bool ems_bridge_init(
    ems_bridge_interface_t ems_type,
    ems_bridge_setup_values* appliance_config,
    ems_bridge_request_sensor_cb_t request_sensor_cb,
    ems_bridge_set_actuator_cb_t set_actuator_cb,
    ems_bridge_on_init_ready_cb_t on_init_ready_cb
);
```

Result submission functions:

```cpp
bool ems_bridge_send_new_sensor_value(ems_bridge_sensor_result* result);
bool ems_bridge_send_set_actuator_result(ems_bridge_set_actuator_result* result);
```

The Modbus Mapping Component calls these result functions after it has handled a sensor read or actuator write.

---

## Setup Model

`ems_bridge_setup_values` describes the appliance identity and control model used by the protocol wrapper.

Identity fields:

- brand
- logo URI
- type
- model
- user-defined name

Control fields:

- roles
- instruction processing delay
- phase count
- power constraint behavior
- default power and energy limits per phase

The current maximums are:

- roles: `3`
- phases: `3`

---

## Sensor and Actuator Model

The bridge defines EMS-level sensor IDs such as:

- actual current
- actual voltage
- actual power
- power limit range
- energy limit range
- status

It also defines actuator IDs for setpoints such as:

- maximum current
- maximum power
- minimum current
- minimum power
- heartbeat

These IDs are intentionally not Modbus register addresses. The Modbus Mapping Component maps them to device-specific register definitions from the appliance template.

---

## Initialization Flow

1. `main.cpp` waits until the Modbus Mapping Component is ready.
2. `main.cpp` builds `ems_bridge_setup_values` from the resolved appliance config.
3. `main.cpp` calls `ems_bridge_init()` with:
   - interface type
   - appliance setup values
   - sensor request callback
   - actuator set callback
   - init-ready callback
4. The bridge stores the selected EMS type.
5. The bridge creates `newSensorValueQueue`.
6. The bridge creates `setActuatorResultQueue`.
7. The bridge starts `newSensorValueTask`.
8. The bridge starts `setActuatorResultTask`.
9. For `EMS_INTERFACE_S2_PEBC`, the bridge loads HEMS URL and pair key from NVS.
10. The bridge asks the Ethernet/WiFi component for the active network interface name.
11. The bridge calls `s2_init()`.

If queue creation or S2 initialization fails, the bridge sets `LED_STATE_FATAL_ERROR` and returns `false`.

---

## S2 Configuration Loading

For S2 PEBC, the bridge fills `s2_hems_values` from NVS:

- `hems_url`
- `pair_key`

The bridge also tries to bind S2 traffic to the active network interface by calling:

```cpp
eth_wifi_get_active_interface_name(...)
```

If no interface name is available, S2 traffic uses default routing.

---

## Runtime Data Flow

### Sensor Result Flow

1. Modbus Mapping reads a sensor.
2. Modbus Mapping calls `ems_bridge_send_new_sensor_value()`.
3. The bridge queues the `ems_bridge_sensor_result`.
4. `newSensorValueTask` receives the result.
5. For S2 PEBC, the task calls `s2_on_new_sensor_value()`.

The queue send timeout is `100 ms`.

### Actuator Result Flow

1. Modbus Mapping writes an actuator.
2. Modbus Mapping calls `ems_bridge_send_set_actuator_result()`.
3. The bridge queues the `ems_bridge_set_actuator_result`.
4. `setActuatorResultTask` receives the result.
5. For S2 PEBC, the task calls `s2_on_actuator_set()`.

The queue send timeout is `100 ms`.

### Request Callback Flow

The request direction is callback-based:

1. S2 Wrapper receives a sensor or actuator request from the S2 client library.
2. S2 Wrapper calls the callbacks passed into `ems_bridge_init()`.
3. Those callbacks are implemented by the Modbus Mapping Component.
4. The Modbus Mapping Component maps the EMS ID to a device-specific Modbus register action.

---

## FreeRTOS Tasks

The bridge starts two tasks:

- `newSensorValueTask`
- `setActuatorResultTask`

Both tasks block on their queue with `portMAX_DELAY`. After handling a message, each task delays for `20 ms`.

---

## Error Handling

`ems_bridge_send_new_sensor_value()` returns `false` when `newSensorValueQueue` is full.

`ems_bridge_send_set_actuator_result()` returns `false` when `setActuatorResultQueue` is full.

Initialization returns `false` when:

- sensor result queue creation fails
- actuator result queue creation fails
- S2 initialization fails

The bridge logs these failures and sets the fatal LED state.

---

## Current Limitations

- Only `EMS_INTERFACE_S2_PEBC` is supported.
- There is no deinit function for stopping tasks or deleting queues.
- Actuator result forwarding reaches `s2_on_actuator_set()`, but the S2 wrapper implementation is currently empty.
- HEMS URL and pair key defaults are hardcoded in the bridge: `http://homeassistant.local:8123/api/s2_pebc` and `RECEIVE_FROM_HEMS` respectively. These are written to NVS on first read when the keys are empty.

---

## Summary

The EMS Bridge keeps the rest of the firmware independent from S2-specific implementation details.

It provides generic EMS sensor and actuator types, initializes the selected protocol wrapper, and forwards Modbus Mapping results to that wrapper through small FreeRTOS queues.
