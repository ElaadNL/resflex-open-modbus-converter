<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Modbus Mapping Component

## Overview

The Modbus Mapping Component translates EMS/S2 sensor and actuator requests to Modbus register reads and writes for one configured device.

The active mapping is loaded from the JSON device template stored in NVS under `NVS_STORAGE_KEY_JSON_TEMPLATE`. The parsed mapping is owned by the Modbus Mapping Component as a private `modbus_appliance_definition` instance, accessible through `modbus_mapping_get_definition()`. It contains:

- appliance configuration
- sensor mappings
- actuator mappings
- optional initialisation sequence
- optional actuator pre- and post-sequences
- appliance config fields that must be resolved from Modbus registers

The component itself runs as a FreeRTOS task and processes S2-originated requests from `modbus_mapping_queue`.

---

## Responsibilities

- Load and parse the JSON device template from storage.
- Build an in-memory mapping of configured sensors and actuators.
- Resolve appliance config fields whose source is `MODBUS`.
- Run the optional `initialisationSequence` during startup.
- Wait until the EMS/S2 session is ready before processing S2 requests.
- Translate EMS bridge sensor requests to Modbus reads.
- Periodically refresh MODBUS sensors on their configured `updateIntervalMs` and cache the latest value.
- Translate EMS bridge actuator requests to Modbus writes.
- Rewrite S2 power setpoints to current setpoints when the template exposes only current actuators.
- Run optional actuator `requiredPreSequence` and `requiredPostSequence` steps.
- Send sensor and actuator results back to the EMS bridge.

---

## Non-Responsibilities

The component does not implement:

- EMS/S2 protocol handling. This is handled by the EMS bridge and S2 library.
- Network setup. This is handled by the Ethernet/WiFi component.
- Low-level Modbus transport, retries and timeouts. This is handled by the Modbus component.
- Persistent storage management. It only reads the stored JSON template through the Storage component.
- Web interface logic.
- Device or firmware validation beyond reading configured appliance fields.

---

## Connected Components

### Storage Component

`modbus_appliance_definition::init()` reads the JSON template from NVS using:

```cpp
storage_get_configuration(NVS_STORAGE_KEY_JSON_TEMPLATE, ...)
```

If the stored template is empty or cannot be parsed, the mapping task reports startup failure.

### Modbus Component

The mapping component calls the Modbus component for all register access:

- `modbus_component_read_holding_register`
- `modbus_component_read_input_register`
- `modbus_component_read_holding_register_string`
- `modbus_component_read_input_register_string`
- `modbus_component_write_register_16`
- `modbus_component_write_register_32`

### EMS Bridge

The EMS bridge is initialized with the mapping callbacks:

```cpp
ems_bridge_init(
  EMS_INTERFACE_S2_PEBC,
  &ems_appliance_config,
  ems_bridge_request_sensor_implementation,
  ems_bridge_set_actuator_implementation,
  modbus_mapping_on_s2_ready
);
```

The EMS bridge calls these callbacks when S2 requests a sensor value or actuator write.

---

## Startup Flow

See [Control and Startup Flow](020_control_flow) for the full cross-component sequence.

1. `main.cpp` creates `modbus_mapping_queue`.
2. `main.cpp` loads a lightweight appliance config from the stored template with `modbus_appliance_definition::load_config_from_storage()`.
3. The Modbus component is initialized using that appliance config.
4. `modbus_mapping_init_events()` creates the component event group.
5. `task_modbus_mapping_component()` is started.
6. The task calls `appliance_definition.init()` on the component's private `modbus_appliance_definition` instance to parse the full JSON template.
7. The task calls `resolve_modbus_config_fields()` to read appliance fields with `source: "MODBUS"`.
8. The optional `initialisationSequence` is executed.
9. The task sets either `MODBUS_READY_BIT` or `MODBUS_FAILED_BIT`.
10. `main.cpp` waits in `modbus_mapping_wait_until_modbus_ready()`.
11. If Modbus Mapping is ready, `main.cpp` builds the EMS/S2 appliance config from `modbus_mapping_get_definition().config`.
12. `main.cpp` starts the EMS bridge.
13. When the S2 session is ready, the EMS bridge calls `modbus_mapping_on_s2_ready()`.
14. The mapping task starts processing queued sensor and actuator requests.

The mapping task waits for `S2_READY_BIT` before it processes S2-originated Modbus requests. This prevents S2 callbacks from being handled before the S2 session is fully initialized.

---

## Event Bits

The component uses a FreeRTOS event group with these bits:

- `S2_READY_BIT`: set by `modbus_mapping_on_s2_ready()`.
- `MODBUS_READY_BIT`: set when the template was parsed, Modbus config fields were resolved and the optional initialisation sequence succeeded.
- `MODBUS_FAILED_BIT`: set when startup cannot complete successfully.

---

## JSON Mapping

### Appliance Config

The parser reads:

- `appliance.spec.name`
- `appliance.spec.model`
- `appliance.spec.serialNumber`
- `appliance.spec.firmwareVersion`
- `appliance.spec.expectedFirmware`
- `appliance.connection`
- `appliance.control`

Connection config supports both Ethernet/TCP and RTU fields. If `appliance.connection.iface` is `RTU`, the config uses `MODBUS_CONN_TYPE_RTU`; otherwise it defaults to `MODBUS_CONN_TYPE_TCP`.

### Modbus-Sourced Appliance Fields

`model`, `serialNumber` and `firmwareVersion` can be declared with `source: "MODBUS"`.

During the first JSON parse these fields are not read immediately. Instead, their register definitions are stored in `config_modbus_sources`. After the Modbus component is initialized, `resolve_modbus_config_fields()` reads the values and writes them into:

- `config.model`
- `config.serial_number`
- `config.firmware_version`

### Sensors

Each entry in `sensors` is parsed into a `sensor_t`.

The sensor's `mappingId` must exist in `modbus_appliance_definition::s2_sensor_map`. Unknown sensor mappings are skipped.

Sensor sources currently handled at runtime:

- `MODBUS`: read from a Modbus register.
- `TEMPLATE_DEFINED`: return the numeric template `value` directly.

`USER_INPUT` is parsed as a source type but is not handled by the runtime sensor request path.

### Actuators

Each entry in `actuators` is parsed into an `actuator_t`.

The actuator's `mappingId` must exist in `modbus_appliance_definition::s2_actuator_map`. Unknown actuator mappings are skipped.

`requiredSensorsBeforeEvaluation`, step sizes and timing fields are parsed and stored, but the current request handler does not enforce them.

---

## Runtime Data Flow

### Sensor Request

1. S2 requests a sensor value through the EMS bridge.
2. `ems_bridge_request_sensor_implementation(sensor)` looks up the EMS bridge sensor ID in the active mapping.
3. If the sensor is found, a `MESSAGE_TYPE_MODBUS_SENSOR` message is queued on `modbus_mapping_queue`.
4. The mapping task receives the message and calls `handle_modbus_sensor_request()`.
5. If the sensor is `TEMPLATE_DEFINED`, the configured numeric value is returned directly.
6. If the sensor is `MODBUS`, the configured register is read through the Modbus component.
7. The raw register value is converted to `float` using the configured type and `multiplier`. For a single-register (`sourceSize == 16`) sensor with a `bitMask`, the register is AND-ed with the mask first; when an `outputMapping` is present the masked value is matched against the mapping entries and the matched entry value is returned as a discrete (enum) result without applying the multiplier.
8. The result is sent back with `ems_bridge_send_new_sensor_value()`.
9. Every successful read (and every `TEMPLATE_DEFINED` value) is also recorded in the sensor cache with its `validityDurationMs`.

Besides request-driven reads, the mapping task refreshes MODBUS sensors on their own `updateIntervalMs` (`refresh_due_sensor()`), at most one due sensor per tick. This keeps the cache fresh for cache-only consumers such as the power -> current voltage lookup, without a separate task or bus mutex, because all Modbus access stays in the mapping task.

### Actuator Request

1. S2 sends an actuator write through the EMS bridge.
2. `ems_bridge_set_actuator_implementation(request)` runs `mapping_request_preprocessing()`. When the request is a power setpoint but the template exposes only the matching current setpoint, the power value is converted to current (`current = power / voltage`) using the latest valid `ACTUAL_VOLTAGE` from the cache, falling back to `control.fallbackVoltage`, and the request is rewritten onto the current actuator. Any other request passes through unchanged.
3. The (possibly rewritten) EMS bridge actuator ID is looked up in the active mapping.
4. The incoming float request value is converted to the actuator's configured Modbus type.
5. A `MESSAGE_TYPE_MODBUS_ACTUATOR` message is queued on `modbus_mapping_queue`.
6. The mapping task receives the message and calls `handle_modbus_actuator_request()`.
7. The optional `requiredPreSequence` is executed.
8. The actuator value is written to the configured Modbus register. When the actuator has a `bitMask` (single register), the current register value is read first and only the masked bits are replaced (read-modify-write).
9. The optional `requiredPostSequence` is executed.
10. The result is sent back with `ems_bridge_send_set_actuator_result()`.

Supported runtime actuator writes:

- `FC06` / write single register
- `FC16` / write multiple registers
- `UINT16`
- `INT16`
- `UINT32`
- `INT32`
- `FLOAT32`

Other function codes and types result in an EMS bridge error response.

---

## Sequences

Sequences are arrays of `sequence_step_t` and can be used in:

- `initialisationSequence`
- actuator `requiredPreSequence`
- actuator `requiredPostSequence`

Each sequence step contains:

- `functionCode`
- `startAddress`
- `type`
- `value`
- `waitMs`
- `bitMask` (optional; single register only — replaces just the masked bits via read-modify-write)

The current sequence writer uses big-endian register order and multiplier `1.0`.

If a sequence step fails, the sequence stops. For actuator sequences this sends an actuator error result to the EMS bridge. During startup, a failed initialisation sequence prevents the mapping component from becoming ready.

---

## Queue Messages

`modbus_mapping_queue` carries `message_definition_t` values.

The mapping task currently handles:

- `MESSAGE_TYPE_MODBUS_ACTUATOR`
- `MESSAGE_TYPE_MODBUS_SENSOR`

For actuator messages, the queue payload contains:

- `message_id`
- `actuator_index`
- `value`

For sensor messages, the queue payload contains:

- `message_id`
- `sensor_index`

`message_id` is incremented when the EMS bridge callbacks enqueue requests. The current result path sends results back by EMS bridge sensor or actuator ID, not by queue response messages.

---

## Error Handling

Startup errors:

- missing or empty JSON template
- JSON parse failure
- failure to resolve one or more Modbus-sourced appliance config fields
- failed initialisation sequence
- missing event group

Runtime errors:

- unknown EMS bridge sensor or actuator ID
- queue request index out of range
- unsupported sensor source
- unsupported function code
- unsupported Modbus value type
- failed Modbus read or write
- failed sequence step

Sensor and actuator processing reports runtime failures through the EMS bridge when the request has already been mapped to a configured sensor or actuator. Unknown EMS bridge IDs are logged and ignored before a queue message is created.

---

## Logging

The component logs:

- startup and readiness transitions
- parsed appliance details
- number of sensors, actuators and init steps
- mapped EMS bridge sensor and actuator IDs
- sequence execution
- Modbus register reads and writes
- unsupported types or function codes
- runtime read/write failures

The log tag used by the task implementation is:

```cpp
Modbus Mapping Component
```

The mapping parser uses:

```cpp
S2 Modbus Mapping
```

---

## Current Limitations

- Only one `modbus_appliance_definition` instance is used (owned by the component, accessed through `modbus_mapping_get_definition()`).
- `USER_INPUT` sensor sources are parsed but not handled at runtime.
- `BOOL`, `STRING`, `REGEX` and `IP_ADDR` are parsed as value types, but regular runtime sensor/actuator Modbus access currently handles only 16-bit and 32-bit integer values and `FLOAT32`.
- Appliance config Modbus reads support string-like and integer values, but not `FLOAT32` or `BOOL`.
- `requiredSensorsBeforeEvaluation`, step size limits and minimum step timing are parsed but not enforced.
- Device validation and firmware comparison are not implemented as separate validation logic.

---

## Summary

The Modbus Mapping Component is the runtime bridge between EMS/S2 callbacks and the Modbus component.

It loads a device template, resolves Modbus-backed appliance identity fields, runs configured startup and actuator sequences, and processes S2 sensor and actuator requests through a FreeRTOS queue.
