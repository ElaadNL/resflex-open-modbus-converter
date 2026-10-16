<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Control And Startup Flow

## Overview

The firmware starts Modbus first and only starts S2 after the Modbus mapping layer is ready. This prevents S2 from requesting measurements or sending actuator instructions before the Modbus device, mapping JSON, and resolved appliance configuration are available.

The synchronization between `main`, the Modbus mapping task, the EMS bridge, and the S2 session callback is handled with a FreeRTOS EventGroup.

## Event Bits

The Modbus mapping component owns one EventGroup with these bits:

| Bit | Name | Meaning |
| --- | --- | --- |
| `BIT0` | `S2_READY_BIT` | The S2 session is established and S2 may now send requests to the mapping task. |
| `BIT1` | `MODBUS_READY_BIT` | The Modbus mapping component is ready and S2 may be initialized. |
| `BIT2` | `MODBUS_FAILED_BIT` | The Modbus mapping component failed to become ready. S2 must not be started. |

The EventGroup is created explicitly by `main` before the Modbus mapping task is started. This avoids a race where multiple tasks could create different EventGroups.

## Startup Flow

1. `app_main()` initializes the shared queues, storage, LED component, network, and the low-level Modbus component.

2. `app_main()` calls:

   ```cpp
   modbus_mapping_init_events();
   ```

   This creates the EventGroup used by the Modbus mapping component.

3. `app_main()` starts the Modbus mapping task:

   ```cpp
   xTaskCreatePinnedToCore(task_modbus_mapping_component, ...);
   ```

4. The Modbus mapping task loads and parses the JSON template:

   ```cpp
   appliance_definition.init();
   ```

5. The Modbus mapping task resolves appliance configuration fields that must be read from Modbus, such as `model` when its source is `MODBUS`:

   ```cpp
   appliance_definition.resolve_modbus_config_fields();
   ```

6. The Modbus mapping task runs the optional Modbus initialization sequence from the template.

7. If all required Modbus mapping initialization steps succeed, the task sets:

   ```cpp
   MODBUS_READY_BIT
   ```

   If initialization fails, it sets:

   ```cpp
   MODBUS_FAILED_BIT
   ```

8. Meanwhile, `app_main()` waits for either outcome:

   ```cpp
   modbus_mapping_wait_until_modbus_ready();
   ```

9. If `MODBUS_READY_BIT` is received, `app_main()` builds the EMS/S2 appliance config from the resolved Modbus mapping config:

   ```cpp
   fill_ems_appliance_config(modbus_mapping_get_definition().config, &ems_appliance_config);
   ```

   At this point `model` can contain the value read from Modbus.

10. `app_main()` starts the EMS bridge for S2:

   ```cpp
   ems_bridge_init(
       EMS_INTERFACE_S2_PEBC,
       &ems_appliance_config,
       ems_bridge_request_sensor_implementation,
       ems_bridge_set_actuator_implementation,
       modbus_mapping_on_s2_ready
   );
   ```

1. The EMS bridge creates its result queues and result-processing tasks, asks the network component for the active network interface name, and then starts the S2 library.

2. The S2 library binds its HTTP and WebSocket clients to the active network interface when an interface name is available, then establishes the session with the server.

3. When the S2 session is ready, the wrapper calls the registered callback:

   ```cpp
   modbus_mapping_on_s2_ready();
   ```

4. The callback sets:

   ```cpp
   S2_READY_BIT
   ```

5. The Modbus mapping task is waiting for `S2_READY_BIT` before entering its main queue-processing loop. Once the bit is set, the task starts processing S2-originated Modbus sensor and actuator requests.

## Runtime Control Flow

After startup, S2 can request sensor readings or actuator writes through the EMS bridge callbacks.

### Sensor Request

1. S2 requests a measurement.
2. `s2_library_wrapper` maps the S2 quantity to an EMS bridge sensor ID.
3. It calls:

   ```cpp
   ems_bridge_request_sensor_implementation(sensor);
   ```

4. The Modbus mapping component maps the EMS sensor ID to a configured Modbus sensor.
5. A `MESSAGE_TYPE_MODBUS_SENSOR` message is sent to `modbus_mapping_queue`.
6. The Modbus mapping task receives the message and reads the configured Modbus register.
7. The Modbus mapping task reports the result to the EMS bridge:

   ```cpp
   ems_bridge_send_new_sensor_value(&result);
   ```

8. The EMS bridge dispatches successful results to S2:

   ```cpp
   s2_on_new_sensor_value(sensor_value);
   ```

### Actuator Request

1. S2 receives an instruction from the server.
2. `s2_library_wrapper` maps the instruction limits to EMS bridge actuator IDs.
3. It calls:

   ```cpp
   ems_bridge_set_actuator_implementation(request);
   ```

4. The Modbus mapping component preprocesses the request: an S2 power setpoint is rewritten to a current setpoint (using the latest cached `ACTUAL_VOLTAGE`, or `control.fallbackVoltage`) when the template exposes only current actuators. It then maps the resulting actuator ID to a configured Modbus actuator.
5. A `MESSAGE_TYPE_MODBUS_ACTUATOR` message is sent to `modbus_mapping_queue`.
6. The Modbus mapping task receives the message.
7. It runs the optional pre-sequence.
8. It writes the actuator value to the configured Modbus register.
9. It runs the optional post-sequence.
10. The Modbus mapping task reports the actuator result to the EMS bridge:

   ```cpp
   ems_bridge_send_set_actuator_result(&result);
   ```

1. The EMS bridge dispatches the result to S2:

   ```cpp
   s2_on_actuator_set(actuator_result);
   ```

## Why This Flow Is Safe

- EMS/S2 is not initialized until Modbus mapping is ready.
- The S2 appliance config is built after Modbus-sourced fields, such as `model`, have been resolved.
- The Modbus mapping task does not process S2 requests until the S2 session is ready.
- Modbus mapping reports results to the EMS bridge, not directly to S2, keeping the HEMS protocol integration isolated.
- The S2 client receives the active network interface name from the network component through the EMS bridge, keeping interface-specific knowledge out of the S2 wrapper.
- Event bits are sticky, so a bit can be set before another task starts waiting for it without losing the signal.
- The EventGroup is created once before the mapping task starts, avoiding a lazy-create race condition.
