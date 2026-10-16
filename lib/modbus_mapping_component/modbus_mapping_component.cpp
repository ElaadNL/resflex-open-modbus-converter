// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "modbus_mapping_component.h"
#include <cmath>   // isfinite, for NaN/Inf substitution in FLOAT32 read path
#include <cstring> // memcpy, for FLOAT32 bit-reinterpret in read_modbus_sensor_value
#include <freertos/FreeRTOS.h>
#include <freertos/projdefs.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/task.h> // explicit for vTaskDelay; otherwise native test-stubs won't pull it in transitively
#include <esp_log.h>
#include <esp_timer.h>
#include <definitions.h>
#include <shared_queue.hpp>
#include <modbus_value_utils.h>
#include <modbus_component.h>
#include "modbus_mapping_cache.h"
#include "mapping_request_preprocessing.h"

static const char *TAG = "Modbus Mapping Component";
static const int TASK_INTERVAL_MS = 20;
static constexpr EventBits_t MODBUS_FAILED_BIT = BIT2;
static constexpr EventBits_t MODBUS_READY_BIT = BIT1;
static constexpr EventBits_t S2_READY_BIT = BIT0;

uint16_t message_counter = 0;
static EventGroupHandle_t modbus_mapping_events = nullptr;
static modbus_appliance_definition appliance_definition;

/**
 * @brief Monotonic millisecond counter for cache freshness checks.
 *
 * esp_timer_get_time() returns microseconds since boot; dividing by 1000 keeps
 * us in the millisecond domain the cache expects. Wraps at ~49 days; matches
 * the unsigned arithmetic the cache uses to evaluate elapsed time.
 */
static uint32_t monotonic_now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

modbus_appliance_definition &modbus_mapping_get_definition() {
    return appliance_definition;
}

bool modbus_mapping_init_events() {
    if (modbus_mapping_events != nullptr) {
        return true;
    }

    modbus_mapping_events = xEventGroupCreate();
    if (modbus_mapping_events == nullptr) {
        ESP_LOGE(TAG, "Failed to create Modbus Mapping event group");
        return false;
    }

    return true;
}

//
// Modbus functions to the Modbus component
//
static bool write_modbus_value(function_code_t fc, uint16_t start_address, modbus_type_t type, bool big_endian, float multiplier, uint16_t bit_mask, const modbus_value_t &value) {
    if (fc != function_code_t::WRITE_SINGLE && fc != function_code_t::WRITE_MULTI) {
        ESP_LOGE(TAG, "Function code %d is not supported for actuator writes", static_cast<int>(fc));
        return false;
    }

    if (type == modbus_type_t::UINT16 || type == modbus_type_t::INT16) {
        uint16_t register_value = 0;
        if (!modbus_value_as_scaled_uint16(value, multiplier, &register_value)) {
            ESP_LOGE(TAG, "Cannot write 16-bit value: unsupported value type %d", static_cast<int>(value.type));
            return false;
        }

        // bitMask (spec: only for destinationSize == 16): modify only the masked bits.
        // Read the current holding register, replace the bits inside the mask with the
        // masked new value, and write the merged register back (read-modify-write).
        if (bit_mask != 0) {
            uint16_t current_value = 0;
            if (!modbus_component_read_holding_register(start_address, &current_value)) {
                ESP_LOGE(TAG, "bitMask write: failed to read current value of register %d", start_address);
                return false;
            }

            uint16_t merged_value = (current_value & ~bit_mask) | (register_value & bit_mask);
            ESP_LOGI(TAG, "Applied bitMask 0x%04X to register %d: current 0x%04X + value 0x%04X -> 0x%04X",
                     bit_mask, start_address, current_value, register_value, merged_value);
            register_value = merged_value;
        }

        return modbus_component_write_register_16(start_address, register_value);
    }

    if (type == modbus_type_t::UINT32 || type == modbus_type_t::INT32) {
        uint32_t register_value = 0;
        if (!modbus_value_as_scaled_uint32(value, multiplier, &register_value)) {
            ESP_LOGE(TAG, "Cannot write 32-bit value: unsupported value type %d", static_cast<int>(value.type));
            return false;
        }

        return modbus_component_write_register_32(
            start_address,
            register_value,
            big_endian);
    }

    if (type == modbus_type_t::FLOAT32) {
        // Pack the IEEE-754 bits into a uint32 and send as two 16-bit registers.
        // Mirrors the FLOAT32 read path; multiplier applied before the bit-pun.
        float float_value = 0.0f;
        if (value.type == modbus_value_type_t::FLOAT32) {
            float_value = value.data.float32_value;
        }
        else {
            ESP_LOGE(TAG, "Cannot write FLOAT32: incoming value type %d not a float", static_cast<int>(value.type));
            return false;
        }
        float_value *= multiplier;
        uint32_t register_bits = 0;
        memcpy(&register_bits, &float_value, sizeof(register_bits));
        ESP_LOGI(TAG, "Writing float value %f to register %d", float_value, start_address);
        return modbus_component_write_register_32(start_address, register_bits, big_endian);
    }

    ESP_LOGE(TAG, "Message type not supported yet!");
    return false;
}

static bool write_modbus_actuator(const actuator_t &actuator, const modbus_value_t &value) {
    return write_modbus_value(
        actuator.fc,
        actuator.start_address,
        actuator.type,
        actuator.big_endian,
        actuator.multiplier,
        actuator.bit_mask,
        value);
}

static bool write_modbus_sequence_step(const sequence_step_t &step) {
    modbus_value_t value = modbus_value_from_float(step.type, step.value);
    return write_modbus_value(
        step.fc,
        step.start_address,
        step.type,
        true,
        1.0f,
        step.bit_mask,
        value);
}

static bool run_modbus_sequence(const std::vector<sequence_step_t> &sequence, const char *sequence_name) {
    for (const sequence_step_t &step : sequence) {
        ESP_LOGI(TAG, "Running %s step: FC %d, register %d, value %f",
                 sequence_name,
                 static_cast<int>(step.fc),
                 step.start_address,
                 step.value);

        if (!write_modbus_sequence_step(step)) {
            ESP_LOGE(TAG, "%s step failed", sequence_name);
            return false;
        }

        if (step.wait_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(step.wait_ms));
        }
    }

    return true;
}

/**
 * @brief send_actuator_result is small helper that takes the Modbus' command's result and sends it to the EMS/S2
 */
static void send_actuator_result(ems_bridge_actuator_t actuator_id, ems_bridge_status_t status) {
    ems_bridge_set_actuator_result result = {
        .actuatorId = actuator_id,
        .status = status};
    ems_bridge_send_set_actuator_result(&result);
}

/**
 * @brief send_sensor_result is small helper that takes the Modbus' sensor's value as float and sends it to the EMS/S2
 */
static void send_sensor_result(ems_bridge_sensor_t sensor_id, ems_bridge_status_t status, float value) {
    ems_bridge_sensor_result result = {
        .sensorId = sensor_id,
        .status = status,
        .value = value};
    ems_bridge_send_new_sensor_value(&result);
}

/**
 * @brief Applies the sensor bitMask and outputMapping to a freshly read 16-bit register value.
 *
 * Per the mapping template spec, bitMask and outputMapping are only meaningful for single-register
 * (sourceSize == 16) reads. When a bitMask is set the raw register value is AND-ed with it first, so
 * only the relevant bits remain. When an outputMapping is present the masked value is compared against
 * each mapping entry's value; on a match the mapping value is reported as the discrete (enum) result
 * and the multiplier is not applied (an enum is not a scaled measurement). When no outputMapping entry
 * matches, the masked value falls through to the normal scaled numeric result.
 *
 * @param sensor Sensor definition holding the bit_mask, output_mapping and multiplier.
 * @param raw_value Raw 16-bit register value as read from Modbus.
 * @param result_value Output that receives the resolved float value.
 */
static void apply_sensor_bitmask_and_mapping(const sensor_t &sensor, uint16_t raw_value, float *result_value) {
    uint16_t masked_value = raw_value;
    if (sensor.bit_mask != 0) {
        masked_value = raw_value & sensor.bit_mask;
        ESP_LOGI(TAG, "Applied bitMask 0x%04X to register %d: raw 0x%04X -> masked 0x%04X",
                 sensor.bit_mask, sensor.start_address, raw_value, masked_value);
    }

    if (!sensor.output_mapping.empty()) {
        for (const output_mapping_t &mapping : sensor.output_mapping) {
            if (static_cast<uint16_t>(mapping.value) == masked_value) {
                *result_value = mapping.value;
                ESP_LOGI(TAG, "Register %d masked value 0x%04X matched outputMapping label %s (value %f)",
                         sensor.start_address, masked_value, mapping.label.c_str(), mapping.value);
                return;
            }
        }
        ESP_LOGW(TAG, "Register %d masked value 0x%04X matched no outputMapping entry", sensor.start_address, masked_value);
    }

    if (sensor.type == modbus_type_t::INT16) {
        *result_value = sensor.multiplier * static_cast<float>(static_cast<int16_t>(masked_value));
    }
    else {
        *result_value = sensor.multiplier * static_cast<float>(masked_value);
    }
}

static bool read_modbus_sensor_value(const sensor_t &sensor, float *result_value) {
    if (result_value == nullptr) {
        return false;
    }

    if (sensor.type == modbus_type_t::UINT16 || sensor.type == modbus_type_t::INT16) {
        uint16_t value = 0;
        bool read_success = sensor.fc == function_code_t::READ_HR
                                ? modbus_component_read_holding_register(sensor.start_address, &value)
                                : modbus_component_read_input_register(sensor.start_address, &value);

        // A failed read leaves value at 0. Do not run the bitmask/outputMapping or
        // scaling on it: a 0 would otherwise be reported as a real measurement (and
        // with an outputMapping even resolve to a discrete enum, e.g. status 0 ->
        // SLEEP), masking the read error. Return 0 and false instead.
        if (!read_success) {
            *result_value = 0.0f;
            ESP_LOGW(TAG, "Read failed for register %d; not applying value", sensor.start_address);
            return false;
        }

        apply_sensor_bitmask_and_mapping(sensor, value, result_value);

        if (sensor.type == modbus_type_t::INT16) {
            ESP_LOGI(TAG, "Read value %d from register %d", static_cast<int16_t>(value), sensor.start_address);
        }
        else {
            ESP_LOGI(TAG, "Read value %d from register %d", value, sensor.start_address);
        }

        return read_success;
    }

    if (sensor.type == modbus_type_t::UINT32 || sensor.type == modbus_type_t::INT32) {
        uint32_t value = 0;
        bool read_success = sensor.fc == function_code_t::READ_HR
                                ? modbus_component_read_holding_register(sensor.start_address, &value)
                                : modbus_component_read_input_register(sensor.start_address, &value);

        // A failed read leaves value at 0; do not report it as a real measurement.
        if (!read_success) {
            *result_value = 0.0f;
            ESP_LOGW(TAG, "Read failed for register %d; not applying value", sensor.start_address);
            return false;
        }

        if (sensor.type == modbus_type_t::INT32) {
            *result_value = sensor.multiplier * static_cast<float>(static_cast<int32_t>(value));
            ESP_LOGI(TAG, "Read value %d from register %d", static_cast<int32_t>(value), sensor.start_address);
        }
        else {
            *result_value = sensor.multiplier * static_cast<float>(value);
            ESP_LOGI(TAG, "Read value %d from register %d", value, sensor.start_address);
        }

        return read_success;
    }

    if (sensor.type == modbus_type_t::FLOAT32) {
        // Modbus delivers FLOAT32 as the IEEE-754 bit pattern packed into two
        // consecutive 16-bit registers. Read them as one uint32_t and reinterpret
        // the bits as a native float via memcpy (strict-aliasing safe; no UB).
        uint32_t register_bits = 0;
        bool read_success = sensor.fc == function_code_t::READ_HR
                                ? modbus_component_read_holding_register(sensor.start_address, &register_bits)
                                : modbus_component_read_input_register(sensor.start_address, &register_bits);

        // A failed read leaves register_bits at 0; do not report it as a real measurement.
        if (!read_success) {
            *result_value = 0.0f;
            ESP_LOGW(TAG, "Read failed for register %d; not applying value", sensor.start_address);
            return false;
        }

        float float_value = 0.0f;
        memcpy(&float_value, &register_bits, sizeof(float_value));

        // Some vendors signal "no measurement available" by leaving
        // the register at 0xFFFFFFFF, which decodes to NaN. The S2 CEM rejects
        // PowerMeasurement messages with null/NaN as INVALID_MESSAGE. Treat NaN
        // and infinities as 0 so the message stays well-formed.
        if (!std::isfinite(float_value)) {
            ESP_LOGW(TAG, "FLOAT32 register %d delivered NaN/Inf — substituting 0", sensor.start_address);
            float_value = 0.0f;
        }

        *result_value = sensor.multiplier * float_value;
        ESP_LOGI(TAG, "Read float value %f from register %d", float_value, sensor.start_address);

        return read_success;
    }

    ESP_LOGE(TAG, "Message type not supported yet!");
    return false;
}




static void handle_modbus_actuator_request(const s2_modbus_actuator_t &request) {
    std::string actuator_value = modbus_value_as_string(request.value);

    ESP_LOGI(TAG, "Received new S2 request with actuator %d request and value %s",
             request.actuator_index,
             actuator_value.c_str());

    if (request.actuator_index >= appliance_definition.actuators.size()) {
        ESP_LOGE(TAG, "Actuator index %d is out of range", request.actuator_index);
        return;
    }

    actuator_t actuator = appliance_definition.actuators[request.actuator_index];

    //
    // Run the requirePreSequence, if present
    //
    ESP_LOGI(TAG, "Running requiredPreSequence...");
    if (!run_modbus_sequence(actuator.pre_sequence, "requiredPreSequence")) {
        send_actuator_result(actuator.emd_actuator_type, EMS_BRIDGE_STATUS_ERR);
        return;
    }

    //
    // Write the actual actuator
    //
    if (!write_modbus_actuator(actuator, request.value)) {
        ESP_LOGE(TAG, "Failed to write actuator %s", actuator.mapping_id.c_str());
        send_actuator_result(actuator.emd_actuator_type, EMS_BRIDGE_STATUS_ERR);
        return;
    }

    //
    // Run the requirePostSequence, if present
    //
    ESP_LOGI(TAG, "Running requiredPostSequence...");
    if (!run_modbus_sequence(actuator.post_sequence, "requiredPostSequence")) {
        send_actuator_result(actuator.emd_actuator_type, EMS_BRIDGE_STATUS_ERR);
        return;
    }

    send_actuator_result(actuator.emd_actuator_type, EMS_BRIDGE_STATUS_OK);
}



/**
 *  Reads a sensor value and records it in the cache, without sending any result back to the EMS. Shared by the
 *  S2 sensor-request handler (which sends the result afterwards) and the periodic refresh task (which only fills the cache).
 *
 * @param sensor
 * @param result_value
 * @return true and writes result_value when a value was resolved (from the template or from Modbus); returns false otherwise.
 */
static bool refresh_modbus_sensor(const sensor_t &sensor, float *result_value) {
    if (result_value == nullptr) {
        return false;
    }
    *result_value = 0.0f;

    if (sensor.source == register_source_t::TEMPLATE_DEFINED) {
        if (!sensor.has_template_value) {
            ESP_LOGE(TAG, "TEMPLATE_DEFINED sensor %s has no value", sensor.mapping_id.c_str());
            return false;
        }

        ESP_LOGI(TAG, "Using template-defined value %f for sensor %s", sensor.template_value, sensor.mapping_id.c_str());
        modbus_mapping_cache_record(sensor.emd_sensor_type, sensor.template_value, sensor.validity_duration_ms, monotonic_now_ms());
        *result_value = sensor.template_value;
        return true;
    }

    if (sensor.source != register_source_t::MODBUS) {
        ESP_LOGE(TAG, "Sensor source %d is not supported for sensor %s", static_cast<int>(sensor.source), sensor.mapping_id.c_str());
        return false;
    }

    if (sensor.fc == function_code_t::READ_HR || sensor.fc == function_code_t::READ_IR) {
        bool read_success = read_modbus_sensor_value(sensor, result_value);

        // Cache every successful sensor read so mapping_request_preprocessing()
        // (and any future logic that needs recent sensor values) can look up
        // the latest value without re-reading Modbus. The cache itself ignores
        // entries whose validity_duration_ms is 0.
        if (read_success) {
            modbus_mapping_cache_record(sensor.emd_sensor_type, *result_value, sensor.validity_duration_ms, monotonic_now_ms());
        }

        return read_success;
    }

    ESP_LOGE(TAG, "Function code not supported yet!");
    return false;
}

static void handle_modbus_sensor_request(const s2_modbus_sensor_t &request) {
    ESP_LOGI(TAG, "Received new S2 request with sensor %d request", request.sensor_index);

    if (request.sensor_index >= appliance_definition.sensors.size()) {
        ESP_LOGE(TAG, "Sensor index %d is out of range", request.sensor_index);
        return;
    }

    sensor_t sensor = appliance_definition.sensors[request.sensor_index];
    float result_value = 0.0f;

    bool read_success = refresh_modbus_sensor(sensor, &result_value);

    send_sensor_result(
        sensor.emd_sensor_type,
        read_success ? EMS_BRIDGE_STATUS_OK : EMS_BRIDGE_STATUS_ERR,
        result_value);
}

/**
 * @brief Refreshes at most one MODBUS sensor that is due for an update.
 *
 * Called once per mapping-loop tick, after any S2 message has been handled, so
 * the sensor cache stays fresh for cache-only consumers (such as the power ->
 * current voltage lookup in mapping_request_preprocessing) without needing a
 * separate task or bus mutex: all Modbus access stays in the mapping task.
 *
 * Each MODBUS sensor is refreshed on its own updateIntervalMs. To keep every
 * tick short and the bus load even, only one due sensor is refreshed per call;
 * a rotating start index makes sure no sensor starves when several are due at
 * once. A sensor is due when at least update_interval_ms has elapsed since its
 * last refresh, or when it has never been refreshed (last_refresh_ms == 0).
 */
static void refresh_due_sensor() {
    // Per-sensor timestamp of the last refresh; 0 means "never", so every sensor
    // is due on the first pass and the cache is populated shortly after startup.
    static std::vector<uint32_t> last_refresh_ms(appliance_definition.sensors.size(), 0);
    // Rotating start offset so a single refresh-per-tick visits sensors fairly.
    static uint16_t scan_start = 0;

    uint16_t sensor_count = appliance_definition.sensors.size();
    if (sensor_count == 0) {
        return;
    }

    uint32_t now_ms = monotonic_now_ms();

    for (uint16_t offset = 0; offset < sensor_count; offset++) {
        uint16_t sensor_index = (scan_start + offset) % sensor_count;
        const sensor_t &sensor = appliance_definition.sensors[sensor_index];

        // Only MODBUS sensors are read from the bus; TEMPLATE_DEFINED and any
        // other source do not need periodic Modbus reads.
        if (sensor.source != register_source_t::MODBUS) {
            continue;
        }

        bool never_refreshed = last_refresh_ms[sensor_index] == 0;
        uint32_t elapsed_ms = now_ms - last_refresh_ms[sensor_index];
        if (!never_refreshed && elapsed_ms < sensor.update_interval_ms) {
            continue;
        }

        float result_value = 0.0f;
        refresh_modbus_sensor(sensor, &result_value);
        last_refresh_ms[sensor_index] = now_ms;

        // Resume the next scan just past the sensor we just refreshed, so the
        // remaining due sensors get their turn on the following ticks.
        scan_start = (sensor_index + 1) % sensor_count;
        return;
    }
}

//   ____        _     _ _         __                  _   _
//  |  _ \ _   _| |__ | (_) ___   / _|_   _ _ __   ___| |_(_) ___  _ __
//  | |_) | | | | '_ \| | |/ __| | |_| | | | '_ \ / __| __| |/ _ \| '_ \
//  |  __/| |_| | |_) | | | (__  |  _| |_| | | | | (__| |_| | (_) | | | |
//  |_|    \__,_|_.__/|_|_|\___| |_|  \__,_|_| |_|\___|\__|_|\___/|_| |_|
//
//
// Here start the actual public functions
//
void modbus_mapping_on_ems_ready() {
    if (modbus_mapping_events == nullptr) {
        ESP_LOGE(TAG, "Cannot set S2 ready because event group is unavailable");
        return;
    }

    xEventGroupSetBits(modbus_mapping_events, S2_READY_BIT);
}

bool modbus_mapping_wait_until_modbus_ready() {
    if (modbus_mapping_events == nullptr) {
        ESP_LOGE(TAG, "Cannot wait for Modbus ready because event group is unavailable");
        return false;
    }

    ESP_LOGI(TAG, "Waiting for Modbus Mapping ready...");
    EventBits_t bits = xEventGroupWaitBits(
        modbus_mapping_events,
        MODBUS_READY_BIT | MODBUS_FAILED_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY);

    if ((bits & MODBUS_FAILED_BIT) != 0) {
        ESP_LOGE(TAG, "Modbus Mapping failed to become ready");
        return false;
    }

    ESP_LOGI(TAG, "Modbus Mapping ready");
    return true;
}

void task_modbus_mapping_component(void *pvParameters) {

    ESP_LOGI(TAG, "Modbus Mapping Component started");

    bool modbus_mapping_ready = false;

    if (appliance_definition.init()) {
        bool config_fields_resolved = appliance_definition.resolve_modbus_config_fields();
        if (!config_fields_resolved) {
            ESP_LOGE(TAG, "Failed to resolve one or more Modbus appliance config fields");
        }

        ESP_LOGI(TAG, "Modbus Mapping read JSON succesfully!");
        ESP_LOGI(TAG, "- Device name:     %s", appliance_definition.config.name.c_str());
        ESP_LOGI(TAG, "- Device model:    %s", appliance_definition.config.model.c_str());
        ESP_LOGI(TAG, "- Device serial:   %s", appliance_definition.config.serial_number.c_str());
        ESP_LOGI(TAG, "- Device firmware: %s", appliance_definition.config.firmware_version.c_str());
        ESP_LOGI(TAG, "- #sensors:        %d", appliance_definition.sensors.size());
        ESP_LOGI(TAG, "- #actuators:      %d", appliance_definition.actuators.size());
        ESP_LOGI(TAG, "- #init steps:     %d", appliance_definition.initialisation_sequence.size());


        if (!appliance_definition.initialisation_sequence.empty()) {

            ESP_LOGI(TAG, "Running initialisation sequence...");
            bool initialisation_sequence_ok = run_modbus_sequence(appliance_definition.initialisation_sequence, "initialisationSequence");
            if (!initialisation_sequence_ok) {
                ESP_LOGE(TAG, "Initialisation Sequence failed");
            }
            modbus_mapping_ready = config_fields_resolved && initialisation_sequence_ok;
        }
        else {
            ESP_LOGI(TAG, "No initialisation sequence defined.");
            modbus_mapping_ready = config_fields_resolved;
        }
    }
    else {
        ESP_LOGE(TAG, "Error in initialization of Modbus mapping component, please check JSON file!");
    }

    if (modbus_mapping_events != nullptr) {
        xEventGroupSetBits(modbus_mapping_events, modbus_mapping_ready ? MODBUS_READY_BIT : MODBUS_FAILED_BIT);
    }

    //
    // Here starts the component's main loop taking care of all requests coming in from S2
    //
    if (modbus_mapping_events != nullptr) {
        ESP_LOGI(TAG, "Waiting for S2 session ready...");
        xEventGroupWaitBits(
            modbus_mapping_events,
            S2_READY_BIT,
            pdFALSE,
            pdTRUE,
            portMAX_DELAY);
        ESP_LOGI(TAG, "S2 session ready, processing Modbus Mapping requests");
    }
    else {
        ESP_LOGE(TAG, "S2 ready wait skipped because event group is unavailable");
    }

    message_definition_t received_message;
    for (;;) {
        // Check for new messages in the queue
        if (xQueueReceive(modbus_mapping_queue, &received_message, 0) == pdPASS) {
            if (received_message.type == MESSAGE_TYPE_MODBUS_ACTUATOR) {
                handle_modbus_actuator_request(received_message.data.modbus_actuator);
            }
            else if (received_message.type == MESSAGE_TYPE_MODBUS_SENSOR) {
                handle_modbus_sensor_request(received_message.data.modbus_sensor);
            }
        }

        // Keep the sensor cache fresh for cache-only consumers (e.g. the power ->
        // current voltage lookup). Refreshes at most one due sensor per tick, so
        // an incoming S2 request handled above is not delayed by a burst of reads.
        refresh_due_sensor();

        // Wait 20ms for the CPU to handle other tasks.
        vTaskDelay(pdMS_TO_TICKS(TASK_INTERVAL_MS));
    }
}

//
// CALLBACK functions to be used by the EMS Bridge component actuator and sensor requests
//
void ems_bridge_set_actuator_implementation(ems_bridge_set_actuator_request request) {
    ESP_LOGI(TAG, "ems_bridge_set_actuator_implementation called with %d and value %f", request.actuatorId, request.value);

    //
    // Power -> current rewrite for templates that only expose current setpoints.
    // The preprocessing returns the original request unchanged when no rewrite
    // is applicable (already-current actuators, heartbeat, template that does
    // expose the power actuator, etc.).
    //
    request = mapping_request_preprocessing(request, appliance_definition, appliance_definition.config, monotonic_now_ms());

    //
    // Basic check, is the cmd a known actuator in the current device. If not, we can stop here.
    // With MODBUS_DONT_FAIL_WHEN_NOT_EXISTENT defined, missing actuators are silently accepted
    // (single-setpoint appliances, such as many EV chargers, have no lower-bound register).
    //
    uint16_t actuator_index = 0;
    if (!appliance_definition.find_actuator_index(request.actuatorId, &actuator_index)) {
#ifdef MODBUS_DONT_FAIL_WHEN_NOT_EXISTENT
        ESP_LOGI(TAG, "Command %d not in template, silently accepted", request.actuatorId);
#else
        ESP_LOGE(TAG, "Command %d not found in list of actuators!", request.actuatorId);
#endif
        return;
    }
    else {
        ESP_LOGI(TAG, "Command %d found in list of actuators and mapped on index %d.", request.actuatorId, actuator_index);
    }

    message_definition_t message;
    message.type = MESSAGE_TYPE_MODBUS_ACTUATOR;
    message.data.modbus_actuator.actuator_index = actuator_index;
    const actuator_t &actuator = appliance_definition.actuators[actuator_index];

    //
    // Store the request.value (i.e., float) in the actuator's type according to the JSON template
    //
    message.data.modbus_actuator.value = modbus_value_from_float(actuator.type, request.value);
    if (message.data.modbus_actuator.value.type == modbus_value_type_t::UNKNOWN) {
        ESP_LOGE(TAG, "Cannot map actuator %d value to Modbus type %d", request.actuatorId, static_cast<int>(actuator.type));
        return;
    }
    message.data.modbus_actuator.message_id = message_counter++;
    xQueueSend(modbus_mapping_queue, &message, portMAX_DELAY);
}



void ems_bridge_request_sensor_implementation(ems_bridge_sensor_t sensor) {
    ESP_LOGI(TAG, "ems_bridge_request_sensor_implementation called with %d", sensor);

    //
    // Basic check, is the sensor a known sensor in the current device. If not, we can stop here.
    //
    uint16_t sensor_index = 0;
    if (!appliance_definition.find_sensor_index(sensor, &sensor_index)) {
        ESP_LOGE(TAG, "Sensor %d not found in list of sensors!", sensor);
        return;
    }
    else {
        ESP_LOGI(TAG, "Command %d found in list of sensors and mapped on index %d.", sensor, sensor_index);
    }

    message_definition_t message;
    message.type = MESSAGE_TYPE_MODBUS_SENSOR;
    message.data.modbus_sensor.sensor_index = sensor_index;
    message.data.modbus_sensor.message_id = message_counter++;

    xQueueSend(modbus_mapping_queue, &message, portMAX_DELAY);
}
