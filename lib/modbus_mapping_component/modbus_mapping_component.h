// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <modbus_appliance_definition.h>

/**
 * @brief Access the in-memory appliance template owned by the Modbus
 * Mapping component. Loaded from the active device template at task start
 * and shared with `main.cpp` (for the appliance config) and tests.
 */
modbus_appliance_definition &modbus_mapping_get_definition();

/**
 * @brief Create the Modbus Mapping event group if needed.
 *
 * The event group is used to signal Modbus Mapping readiness/failure and S2
 * session readiness between `main.cpp`, the mapping task and the EMS bridge.
 *
 * @return true if the event group exists, false if it could not be created.
 */
extern bool modbus_mapping_init_events();

/**
 * @brief FreeRTOS task entry point for the Modbus Mapping component.
 *
 * Loads the JSON template, resolves Modbus-backed appliance config fields,
 * runs the optional initialisation sequence, signals readiness, waits for S2
 * readiness, and then processes Modbus sensor/actuator messages from
 * `modbus_mapping_queue`.
 *
 * @param pvParameters Unused FreeRTOS task parameter.
 */
extern void task_modbus_mapping_component(void *pvParameters);

/**
 * @brief Signal that the EMS/S2 session is ready.
 *
 * Registered as the EMS bridge init-ready callback. Sets the S2-ready event
 * bit so the Modbus Mapping task can start processing queued S2 requests.
 */
extern void modbus_mapping_on_ems_ready();

/**
 * @brief Block until Modbus Mapping startup succeeds or fails.
 *
 * Called by `main.cpp` before starting the EMS bridge so S2 receives the
 * resolved appliance configuration.
 *
 * @return true if Modbus Mapping became ready, false if startup failed.
 */
extern bool modbus_mapping_wait_until_modbus_ready();

/**
 * EMS bridge callback for actuator write requests from S2.
 *
 * Looks up the requested EMS bridge actuator in the active Modbus mapping,
 * converts the requested float value to the configured Modbus register type,
 * and queues a MESSAGE_TYPE_MODBUS_ACTUATOR message for the Modbus mapping
 * task. Unknown actuators or values that cannot be converted are logged and
 * ignored.
 */
extern void ems_bridge_set_actuator_implementation(ems_bridge_set_actuator_request request);

/**
 * EMS bridge callback for sensor read requests from S2.
 *
 * Looks up the requested EMS bridge sensor in the active Modbus mapping and
 * queues a MESSAGE_TYPE_MODBUS_SENSOR message for the Modbus mapping task.
 * Unknown sensors are logged and ignored.
 */
extern void ems_bridge_request_sensor_implementation(ems_bridge_sensor_t sensor);
