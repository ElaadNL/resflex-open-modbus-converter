// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "modbus_appliance_definition.h"

#include <esp_log.h>
#include <cstdlib>
#include <cstring>
#include <driver/uart.h>
#include <definitions.h>
#include <modbus_component.h>
#include <storage_component.h>

static const char *TAG = "S2 Modbus Mapping";

/**
 * @brief Check whether a JSON config field must be read from captive-portal NVS.
 */
static bool config_field_is_user_input(JsonObject field) {
    return strcmp(field["source"] | "", "USER_INPUT") == 0;
}

/**
 * @brief Return a string field's value/default fallback chain.
 */
static const char *config_field_default_string(JsonObject field, const char *fallback = "") {
    return field["value"] | (field["defaultValue"] | (field["default"] | fallback));
}

/**
 * @brief Return a uint16 field's value/default fallback chain.
 */
static uint16_t config_field_default_u16(JsonObject field, uint16_t fallback) {
    return field["value"] | (field["defaultValue"] | (field["default"] | fallback));
}

/**
 * @brief Parse a decimal uint16 string, falling back on invalid input.
 */
static uint16_t parse_u16_or_default(const char *value, uint16_t fallback) {
    if (value == nullptr || value[0] == '\0') {
        return fallback;
    }

    char *end = nullptr;
    unsigned long parsed = strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed > UINT16_MAX) {
        return fallback;
    }

    return static_cast<uint16_t>(parsed);
}

static std::optional<ems_bridge_roles_t> role_from_string(const std::string &role) {
    static const std::unordered_map<std::string, ems_bridge_roles_t> role_map = {
        {"CONSUMER", EMS_BRIDGE_ROLE_CONSUMER},
        {"PRODUCER", EMS_BRIDGE_ROLE_PRODUCER},
        { "STORAGE",  EMS_BRIDGE_ROLE_STORAGE}
    };

    auto it = role_map.find(role);
    if (it != role_map.end()) {
        return it->second;
    }

    return std::nullopt;
}

static const char *role_to_string(ems_bridge_roles_t role) {
    switch (role) {
        case EMS_BRIDGE_ROLE_CONSUMER:
            return "CONSUMER";
        case EMS_BRIDGE_ROLE_PRODUCER:
            return "PRODUCER";
        case EMS_BRIDGE_ROLE_STORAGE:
            return "STORAGE";
        default:
            return nullptr;
    }
}

/**
 * @brief Materialize a USER_INPUT string field from NVS into JSON value.
 */
static void apply_user_input_string(JsonObject field, const char *nvs_key, const char *fallback = "") {
    if (field.isNull() || !config_field_is_user_input(field)) {
        return;
    }

    char value[128] = {};
    storage_get_configuration(nvs_key, value, sizeof(value), config_field_default_string(field, fallback));
    field["value"] = value;
}

/**
 * @brief Materialize a USER_INPUT uint16 stored as string in NVS into JSON value.
 */
static void apply_user_input_u16_string(JsonObject field, const char *nvs_key, uint16_t fallback) {
    if (field.isNull() || !config_field_is_user_input(field)) {
        return;
    }

    char value[16] = {};
    uint16_t default_value = config_field_default_u16(field, fallback);
    storage_get_configuration(nvs_key, value, sizeof(value), "");
    field["value"] = parse_u16_or_default(value, default_value);
}

/**
 * @brief Materialize a USER_INPUT uint16 stored as uint16 in NVS into JSON value.
 */
static void apply_user_input_u16(JsonObject field, const char *nvs_key, uint16_t fallback) {
    if (field.isNull() || !config_field_is_user_input(field)) {
        return;
    }

    uint16_t value = config_field_default_u16(field, fallback);
    storage_get_configuration(nvs_key, value, value);
    field["value"] = value;
}

/**
 * @brief Fill all known USER_INPUT appliance config fields from NVS before parsing.
 */
static void apply_user_input_config_values(JsonDocument &doc) {
    JsonObject spec = doc["appliance"]["spec"];
    JsonObject conn = doc["appliance"]["connection"];
    JsonObject eth = conn["eth"];
    JsonObject rtu = conn["rtu"];
    JsonObject ctrl = doc["appliance"]["control"];

    apply_user_input_string(spec["name"].as<JsonObject>(), NVS_STORAGE_KEY_APPLIANCE_NAME, "Unknown");
    apply_user_input_string(spec["model"].as<JsonObject>(), NVS_STORAGE_KEY_APPLIANCE_MODEL, "Unknown");
    apply_user_input_string(spec["serialNumber"].as<JsonObject>(), NVS_STORAGE_KEY_APPLIANCE_SERIAL, "Unknown");
    apply_user_input_string(spec["firmwareVersion"].as<JsonObject>(), NVS_STORAGE_KEY_APPLIANCE_FIRMWARE, "Unknown");
    apply_user_input_string(eth["ipAddress"].as<JsonObject>(), NVS_STORAGE_KEY_MODBUS_ETH_IP, MODBUS_DEVICE_IP);

    apply_user_input_u16_string(conn["unitId"].as<JsonObject>(), NVS_STORAGE_KEY_MODBUS_UNIT_ID, MODBUS_DEVICE_UID);
    apply_user_input_u16_string(eth["port"].as<JsonObject>(), NVS_STORAGE_KEY_MODBUS_ETH_PORT, MODBUS_DEVICE_PORT);
    apply_user_input_u16_string(rtu["modbusChannel"].as<JsonObject>(), NVS_STORAGE_KEY_MODBUS_CHANNEL, static_cast<uint16_t>(UART_NUM_2));
    apply_user_input_u16(ctrl["numberOfPhases"].as<JsonObject>(), NVS_STORAGE_KEY_NUM_PHASES, DEVICE_DEFAULT_PHASES);
}

modbus_appliance_definition::modbus_appliance_definition() {
}

bool modbus_appliance_definition::init() {
    std::vector<char> json_buffer(MAX_JSON_SIZE, 0);
    storage_get_configuration(NVS_STORAGE_KEY_JSON_TEMPLATE, json_buffer.data(), json_buffer.size(), "");

    if (strnlen(json_buffer.data(), json_buffer.size()) == 0) {
        ESP_LOGE(TAG, "device_template is an empty string! Is there anything in the NVS yet?");
        return false;
    }

    return load_from_json(json_buffer.data());
}



bool modbus_appliance_definition::load_config_from_storage(appliance_config_t *config) {
    if (config == nullptr) {
        return false;
    }

    std::vector<char> json_buffer(MAX_JSON_SIZE, 0);
    storage_get_configuration(NVS_STORAGE_KEY_JSON_TEMPLATE, json_buffer.data(), json_buffer.size(), "");
    ESP_LOGW(TAG, "loaded %u bytes from NVS", (unsigned)strnlen(json_buffer.data(), json_buffer.size()));
    printf("RAW NVS JSON >>>%s<<<\n", json_buffer.data());

    if (strnlen(json_buffer.data(), json_buffer.size()) == 0) {
        ESP_LOGE(TAG, "device_template is an empty string! Is there anything in the NVS yet?");
        return false;
    }

    return parse_config_from_json(json_buffer.data(), config);
}



bool modbus_appliance_definition::load_from_json(const std::string &json) {
    sensors.clear();
    actuators.clear();
    initialisation_sequence.clear();
    config_modbus_sources.clear();
    config = appliance_config_t{};
    source_json.clear();

    if (!parse_json(json)) {
        return false;
    }

    source_json = json;
    return true;
}

bool modbus_appliance_definition::parse_config_from_json(const std::string &json, appliance_config_t *config) {
    if (config == nullptr) {
        return false;
    }

    JsonDocument filter;
    filter["appliance"] = true;

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, json, DeserializationOption::Filter(filter));
    if (error) {
        ESP_LOGE(TAG, "appliance JSON parse failed: %s", error.c_str());
        return false;
    }

    parse_config(doc, config);
    return true;
}

std::string modbus_appliance_definition::save_to_json() const {
    JsonDocument doc;

    if (!source_json.empty()) {
        DeserializationError error = deserializeJson(doc, source_json);
        if (error) {
            return "";
        }
    }

    write_config_to_json(doc);

    std::string json;
    serializeJson(doc, json);
    return json;
}

bool modbus_appliance_definition::find_actuator_index(const ems_bridge_actuator_t actuator_id, uint16_t *actuator_index) const {
    if (actuator_index == nullptr) {
        return false;
    }

    for (uint16_t index = 0; index < actuators.size(); index++) {
        if (actuators[index].emd_actuator_type == actuator_id) {
            *actuator_index = index;
            return true;
        }
    }

#ifdef MODBUS_DONT_FAIL_WHEN_NOT_EXISTENT
    ESP_LOGW(TAG, "Unable to find actuator %d in current list of actuators!", actuator_id);
#else
    ESP_LOGE(TAG, "Unable to find actuator %d in current list of actuators!", actuator_id);
#endif

    return false;
}

bool modbus_appliance_definition::find_sensor_index(const ems_bridge_sensor_t sensor_id, uint16_t *sensor_index) const {
    if (sensor_index == nullptr) {
        return false;
    }

    for (uint16_t index = 0; index < sensors.size(); index++) {
        if (sensors[index].emd_sensor_type == sensor_id) {
            *sensor_index = index;
            return true;
        }
    }

    ESP_LOGE(TAG, "Unable to find sensor %d in current list of sensors!", sensor_id);

    return false;
}

void modbus_appliance_definition::parse_sequence(JsonArray arr, std::vector<sequence_step_t> &target) {
    if (arr.isNull())
        return;
    for (JsonObject s : arr) {
        sequence_step_t step;
        step.fc = string_to_fc(s["functionCode"] | "");
        step.start_address = s["startAddress"];
        step.type = string_to_type(s["type"] | "UINT16");
        step.value = s["value"] | 0.0f;
        step.wait_ms = s["waitMs"] | 0;
        step.bit_mask = s["bitMask"] | 0;
        target.push_back(step);
    }
}

bool modbus_appliance_definition::parse_json(const std::string &json) {
    JsonDocument doc;
    if (deserializeJson(doc, json))
        return false;

    // 1. Appliance & Control Specs
    parse_config(doc, &config);
    parse_config_modbus_sources(doc);

    // 2. Initialisation Sequence
    parse_sequence(doc["initialisationSequence"].as<JsonArray>(), initialisation_sequence);

    // 3. Sensors
    for (JsonObject s : doc["sensors"].as<JsonArray>()) {
        sensor_t r = {};
        r.mapping_id = s["mappingId"].as<std::string>();

        std::optional<ems_bridge_sensor_t> sensor_type = get_sensor_type(r.mapping_id);
        if (!sensor_type) {
            ESP_LOGE(TAG, "Sensor %s is not a valid S2 sensor!", s["mappingId"].as<std::string>().c_str());
            // Skip this sensor
            continue;
        }
        else {
            r.emd_sensor_type = *sensor_type;
            ESP_LOGI(TAG, "Sensor %s is a valid S2 sensor (%d)!", s["mappingId"].as<std::string>().c_str(), r.emd_sensor_type);
        }

        r.start_address = s["startAddress"];
        r.fc = string_to_fc(s["functionCode"] | "FC04");
        r.type = string_to_type(s["type"] | "UINT16");
        r.multiplier = s["multiplier"] | 1.0f;
        r.big_endian = s["bigEndian"] | true;

        // Sensor specific fields
        r.source = string_to_source(s["source"] | "MODBUS");
        if (r.source == register_source_t::TEMPLATE_DEFINED) {
            if (!s["value"].is<float>()) {
                ESP_LOGE(TAG, "TEMPLATE_DEFINED sensor %s has no numeric value!", r.mapping_id.c_str());
                continue;
            }

            r.template_value = s["value"] | 0.0f;
            r.has_template_value = true;
        }
        r.source_size = s["sourceSize"] | 0;
        r.register_count = calculate_register_count(r.source_size, r.type);
        r.update_interval_ms = s["updateIntervalMs"] | 1000;
        r.validity_duration_ms = s["validityDurationMs"] | 5000;
        r.bit_mask = s["bitMask"] | 0;

        if (s["outputMapping"].is<JsonArray>()) {
            for (JsonObject m : s["outputMapping"].as<JsonArray>()) {
                r.output_mapping.push_back({m["value"], m["label"].as<std::string>()});
            }
        }
        sensors.push_back(r);
    }

    // 4. Actuators
    for (JsonObject a : doc["actuators"].as<JsonArray>()) {
        actuator_t act;
        act.mapping_id = a["mappingId"].as<std::string>();
        std::optional<ems_bridge_actuator_t> actuator_type = get_actuator_type(act.mapping_id);
        if (!actuator_type) {
            ESP_LOGE(TAG, "Actuator %s is not a valid S2 actuator!", a["mappingId"].as<std::string>().c_str());
            // Skip this actuator
            continue;
        }
        else {
            act.emd_actuator_type = *actuator_type;
            ESP_LOGI(TAG, "Actuator %s is a valid S2 actuator (%d)!", a["mappingId"].as<std::string>().c_str(), act.emd_actuator_type);
        }
        act.start_address = a["startAddress"];
        act.fc = string_to_fc(a["functionCode"] | "FC06");
        act.type = string_to_type(a["type"] | "UINT16");
        act.multiplier = a["multiplier"] | 1.0f;
        act.big_endian = a["bigEndian"] | true;

        // Actuator specific fields
        act.destination_size = a["destinationSize"] | 0;
        act.register_count = calculate_register_count(act.destination_size, act.type);
        act.bit_mask = a["bitMask"] | 0;
        act.evaluation_interval_s = a["evaluationIntervalS"] | 1;
        act.max_stepsize_up = a["maxStepsizeUp"] | 0.0f;
        act.max_stepsize_down = a["maxStepsizeDown"] | 0.0f;
        act.min_step_time_ms = a["minStepTimeMs"] | 0;

        if (a["requiredSensorsBeforeEvaluation"].is<JsonArray>()) {
            for (JsonVariant v : a["requiredSensorsBeforeEvaluation"].as<JsonArray>()) {
                act.required_sensors_before_evaluation.push_back(v.as<std::string>());
            }
        }

        parse_sequence(a["requiredPreSequence"].as<JsonArray>(), act.pre_sequence);
        parse_sequence(a["requiredPostSequence"].as<JsonArray>(), act.post_sequence);

        actuators.push_back(act);
    }

    return true;
}

void modbus_appliance_definition::parse_config(JsonDocument &doc, appliance_config_t *config) {
    //
    // If the template contains USER_INPUT fields for the config, we read them first from NVS
    // if available or use the default value in 'value' or 'Unknown' if nothing is present
    //
    // The function adds the right values in the memory representation of the JSON objects so
    // the remainder of the function sees no difference of the original source of the information
    //
    apply_user_input_config_values(doc);

    JsonObject spec = doc["appliance"]["spec"];
    config->name = spec["name"]["value"] | "Unknown";
    config->model = spec["model"]["value"] | "Unknown";
    config->serial_number = spec["serialNumber"]["value"] | "Unknown";
    config->firmware_version = spec["firmwareVersion"]["value"] | "Unknown";
    config->expected_firmware = spec["expectedFirmware"]["value"] | "Unknown";

    JsonObject conn = doc["appliance"]["connection"];
    config->interface_type = MODBUS_CONN_TYPE_TCP;

    config->unit_id = conn["unitId"]["value"] | (conn["unitId"]["defaultValue"] | MODBUS_DEVICE_UID); // Note: Moved up from eth
    if (conn["iface"] == "RTU") {
        config->interface_type = MODBUS_CONN_TYPE_RTU;
    }

    JsonObject eth = conn["eth"];
    config->ip_address = eth["ipAddress"]["value"] | (eth["ipAddress"]["defaultValue"] | MODBUS_DEVICE_IP);
    config->port = eth["port"]["value"] | (eth["port"]["defaultValue"] | MODBUS_DEVICE_PORT);

    JsonObject rtu = conn["rtu"];
    config->baudrate = rtu["baudrate"]["value"] | (rtu["baudrate"]["defaultValue"] | (uint32_t)MODBUS_DEVICE_BAUDRATE);
    config->modbus_channel = rtu["modbusChannel"]["value"] | (rtu["modbusChannel"]["defaultValue"] | (uint16_t)UART_NUM_2);
    config->parity = rtu["parity"]["value"] | (rtu["parity"]["defaultValue"] | (uint16_t)UART_PARITY_DISABLE);
    config->data_bits = rtu["dataBits"]["value"] | (rtu["dataBits"]["defaultValue"] | (uint16_t)UART_DATA_8_BITS);
    config->stop_bits = rtu["stopBits"]["value"] | (rtu["stopBits"]["defaultValue"] | (uint16_t)UART_STOP_BITS_1);

    JsonObject ctrl = doc["appliance"]["control"];
    config->roles.clear();
    JsonArray roles = ctrl["roles"].as<JsonArray>();
    for (JsonVariant role_value : roles) {
        if (config->roles.size() >= EMS_BRIDGE_ROLES_MAX_AMOUNT) {
            ESP_LOGW(TAG, "Ignoring extra appliance role; max role count is %d", EMS_BRIDGE_ROLES_MAX_AMOUNT);
            break;
        }

        std::string role_name = role_value.as<std::string>();
        if (role_name == "SENTINEL") {
            ESP_LOGW(TAG, "Ignoring SENTINEL appliance role; it is added internally");
            continue;
        }

        std::optional<ems_bridge_roles_t> role = role_from_string(role_name);
        if (!role.has_value()) {
            ESP_LOGW(TAG, "Ignoring unknown appliance role: %s", role_name.c_str());
            continue;
        }

        config->roles.push_back(role.value());
    }

    if (config->roles.empty()) {
        config->roles.push_back(EMS_BRIDGE_ROLE_CONSUMER);
    }

    config->num_phases = ctrl["numberOfPhases"]["value"] | DEVICE_DEFAULT_PHASES;
    config->instruction_delay_s = ctrl["instructionDelayS"]["value"] | DEVICE_DEFAULT_INSTRUCTION_DELAY_S;
    config->defer_consequence = ctrl["consequenceTypeDefer"]["value"] | true;
    // Default false = per-phase control. Set true in the template for appliances
    // whose only control register applies symmetrically to all phases.
    config->phase_symmetric = ctrl["phaseSymmetric"]["value"] | false;

    // Default 0.0 V: safe value when no fallbackVoltage is configured. Used only
    // when an S2 power instruction must be translated to a current setpoint and
    // no recent valid ACTUAL_VOLTAGE_Lx measurement is available — a 0 V fallback
    // results in a 0 A setpoint instead of an unsafe extrapolation.
    JsonVariant fallback_voltage_variant = ctrl["fallbackVoltage"]["value"];
    if (fallback_voltage_variant.isNull()) {
        ESP_LOGI(TAG, "fallbackVoltage not set in template; defaulting to 0.0 V");
        config->fallback_voltage = 0.0f;
    }
    else {
        config->fallback_voltage = fallback_voltage_variant.as<float>();
    }

    ESP_LOGI(TAG, "Resolved appliance config after JSON/NVS parsing:");
    ESP_LOGI(TAG, "- name:              %s", config->name.c_str());
    ESP_LOGI(TAG, "- model:             %s", config->model.c_str());
    ESP_LOGI(TAG, "- serial number:     %s", config->serial_number.c_str());
    ESP_LOGI(TAG, "- firmware version:  %s", config->firmware_version.c_str());
    ESP_LOGI(TAG, "- expected firmware: %s", config->expected_firmware.c_str());
    ESP_LOGI(TAG, "- interface:         %s", config->interface_type == MODBUS_CONN_TYPE_RTU ? "RTU" : "ETH");
    ESP_LOGI(TAG, "- unit id:           %u", static_cast<unsigned>(config->unit_id));
    ESP_LOGI(TAG, "- eth ip address:    %s", config->ip_address.c_str());
    ESP_LOGI(TAG, "- eth port:          %u", static_cast<unsigned>(config->port));
    ESP_LOGI(TAG, "- rtu baudrate:      %lu", static_cast<unsigned long>(config->baudrate));
    ESP_LOGI(TAG, "- rtu channel:       %u", static_cast<unsigned>(config->modbus_channel));
    ESP_LOGI(TAG, "- rtu parity:        %u", static_cast<unsigned>(config->parity));
    ESP_LOGI(TAG, "- rtu data bits:     %u", static_cast<unsigned>(config->data_bits));
    ESP_LOGI(TAG, "- rtu stop bits:     %u", static_cast<unsigned>(config->stop_bits));
    ESP_LOGI(TAG, "- number of phases:  %u", static_cast<unsigned>(config->num_phases));
    ESP_LOGI(TAG, "- instruction delay: %u s", static_cast<unsigned>(config->instruction_delay_s));
    ESP_LOGI(TAG, "- defer consequence: %s", config->defer_consequence ? "true" : "false");
    ESP_LOGI(TAG, "- phase symmetric:   %s", config->phase_symmetric ? "true" : "false");
    ESP_LOGI(TAG, "- fallback voltage:  %.2f V", config->fallback_voltage);
    for (size_t i = 0; i < config->roles.size(); i++) {
        const char *role_name = role_to_string(config->roles[i]);
        ESP_LOGI(TAG, "- role[%u]:           %s",
                 static_cast<unsigned>(i),
                 role_name != nullptr ? role_name : "UNKNOWN");
    }
}

void modbus_appliance_definition::parse_config_modbus_sources(JsonDocument &doc) {
    JsonObject spec = doc["appliance"]["spec"];
    parse_config_modbus_source(spec["model"].as<JsonObject>(), appliance_config_field_t::MODEL);
    parse_config_modbus_source(spec["serialNumber"].as<JsonObject>(), appliance_config_field_t::SERIAL_NUMBER);
    parse_config_modbus_source(spec["firmwareVersion"].as<JsonObject>(), appliance_config_field_t::FIRMWARE_VERSION);
}

/**
 *  @brief parse_config_modbus_source takes care of the special fields where source == MODBUS in the JSON.
 *  Reading these values can be done, once the Modbus connection is available and initialized. Only then
 *  these fields can be read and stored internally.
 *
 *  This function is called during the initial parse and pushes the fields with MODBUS on a vector to be
 *  handled at a later stage.
 */
void modbus_appliance_definition::parse_config_modbus_source(JsonObject field_object, appliance_config_field_t field) {
    if (field_object.isNull() || string_to_source(field_object["source"] | "") != register_source_t::MODBUS) {
        return;
    }

    appliance_config_modbus_source_t source;
    source.field = field;
    source.fc = string_to_fc(field_object["functionCode"] | "FC03");
    source.start_address = field_object["startAddress"] | 0;
    source.type = string_to_type(field_object["type"] | "STRING");
    source.big_endian = field_object["bigEndian"] | true;

    uint16_t register_count = field_object["registerCount"] | 0;
    if (register_count == 0) {
        uint16_t source_size = field_object["sourceSize"] | 0;
        register_count = calculate_register_count(source_size, source.type);
    }
    source.register_count = register_count;

    if (source.fc != function_code_t::READ_HR && source.fc != function_code_t::READ_IR) {
        ESP_LOGE(TAG, "MODBUS appliance config field uses unsupported function code %d", static_cast<int>(source.fc));
        return;
    }

    config_modbus_sources.push_back(source);
}

/**
 *  @brief resolve_modbus_config_fields takes care of the special fields where source == MODBUS in the JSON.
 *  It goes through the list with these special fields and reads the values from the Modbus device. The results are
 *  stored in the configuration using write_resolved_config_field()
 */
bool modbus_appliance_definition::resolve_modbus_config_fields() {
    bool all_resolved = true;

    for (const appliance_config_modbus_source_t &source : config_modbus_sources) {
        std::string value;

        if (source.type == modbus_type_t::STRING || source.type == modbus_type_t::REGEX || source.type == modbus_type_t::IP_ADDR) {
            char buffer[MODBUS_VALUE_STRING_MAX_LEN] = {};
            bool success = false;

            if (source.fc == function_code_t::READ_HR) {
                success = modbus_component_read_holding_register_string(source.start_address, source.register_count, buffer, sizeof(buffer), source.big_endian);
            }
            else if (source.fc == function_code_t::READ_IR) {
                success = modbus_component_read_input_register_string(source.start_address, source.register_count, buffer, sizeof(buffer), source.big_endian);
            }

            if (!success) {
                all_resolved = false;
                continue;
            }

            value = buffer;
        }
        else if (source.type == modbus_type_t::UINT16 || source.type == modbus_type_t::INT16) {
            uint16_t raw_value = 0;
            bool success = false;

            if (source.fc == function_code_t::READ_HR) {
                success = modbus_component_read_holding_register(source.start_address, &raw_value);
            }
            else if (source.fc == function_code_t::READ_IR) {
                success = modbus_component_read_input_register(source.start_address, &raw_value);
            }

            if (!success) {
                all_resolved = false;
                continue;
            }

            value = (source.type == modbus_type_t::INT16)
                        ? std::to_string(static_cast<int16_t>(raw_value))
                        : std::to_string(raw_value);
        }
        else if (source.type == modbus_type_t::UINT32 || source.type == modbus_type_t::INT32) {
            uint32_t raw_value = 0;
            bool success = false;

            if (source.fc == function_code_t::READ_HR) {
                success = modbus_component_read_holding_register(source.start_address, &raw_value);
            }
            else if (source.fc == function_code_t::READ_IR) {
                success = modbus_component_read_input_register(source.start_address, &raw_value);
            }

            if (!success) {
                all_resolved = false;
                continue;
            }

            value = (source.type == modbus_type_t::INT32)
                        ? std::to_string(static_cast<int32_t>(raw_value))
                        : std::to_string(raw_value);
        }
        else {
            ESP_LOGE(TAG, "Unsupported Modbus appliance config type %d", static_cast<int>(source.type));
            all_resolved = false;
            continue;
        }

        if (!write_resolved_config_field(source.field, value)) {
            all_resolved = false;
        }
    }

    return all_resolved;
}

/**
 * @brief write_resolved_config_field does the final step of the special fields with MODBUS as source. Once the
 * fields are known during the initial parse and resolved just after initialization of the Modbus device, this
 * method is called to put the values into the right config properties.
 */
bool modbus_appliance_definition::write_resolved_config_field(appliance_config_field_t field, const std::string &value) {
    switch (field) {
        case appliance_config_field_t::MODEL:
            config.model = value;
            return true;
        case appliance_config_field_t::SERIAL_NUMBER:
            config.serial_number = value;
            return true;
        case appliance_config_field_t::FIRMWARE_VERSION:
            config.firmware_version = value;
            return true;
        default:
            return false;
    }
}

void modbus_appliance_definition::write_config_to_json(JsonDocument &doc) const {
    JsonObject appliance = doc["appliance"].as<JsonObject>();
    if (appliance.isNull()) {
        appliance = doc["appliance"].to<JsonObject>();
    }

    JsonObject spec = appliance["spec"].as<JsonObject>();
    if (spec.isNull()) {
        spec = appliance["spec"].to<JsonObject>();
    }
    spec["name"]["value"] = config.name;
    spec["model"]["value"] = config.model;
    spec["serialNumber"]["value"] = config.serial_number;
    spec["firmwareVersion"]["value"] = config.firmware_version;
    spec["expectedFirmware"]["value"] = config.expected_firmware;

    JsonObject conn = appliance["connection"].as<JsonObject>();
    if (conn.isNull()) {
        conn = appliance["connection"].to<JsonObject>();
    }
    conn["iface"] = (config.interface_type == MODBUS_CONN_TYPE_RTU) ? "RTU" : "ETH";
    conn["unitId"]["value"] = config.unit_id;

    JsonObject eth = conn["eth"].as<JsonObject>();
    if (eth.isNull()) {
        eth = conn["eth"].to<JsonObject>();
    }
    eth["ipAddress"]["value"] = config.ip_address;
    eth["port"]["value"] = config.port;

    JsonObject rtu = conn["rtu"].as<JsonObject>();
    if (rtu.isNull()) {
        rtu = conn["rtu"].to<JsonObject>();
    }
    rtu["baudrate"]["value"] = config.baudrate;
    rtu["modbusChannel"]["value"] = config.modbus_channel;
    rtu["parity"]["value"] = config.parity;
    rtu["dataBits"]["value"] = config.data_bits;
    rtu["stopBits"]["value"] = config.stop_bits;

    JsonObject ctrl = appliance["control"].as<JsonObject>();
    if (ctrl.isNull()) {
        ctrl = appliance["control"].to<JsonObject>();
    }
    ctrl["numberOfPhases"]["value"] = config.num_phases;
    ctrl["instructionDelayS"]["value"] = config.instruction_delay_s;
    ctrl["consequenceTypeDefer"]["value"] = config.defer_consequence;
    ctrl["phaseSymmetric"]["value"] = config.phase_symmetric;
    ctrl["fallbackVoltage"]["value"] = config.fallback_voltage;

    JsonArray roles = ctrl["roles"].to<JsonArray>();
    for (ems_bridge_roles_t role : config.roles) {
        const char *role_name = role_to_string(role);
        if (role_name != nullptr) {
            roles.add(role_name);
        }
    }
}

modbus_type_t modbus_appliance_definition::string_to_type(const std::string &s) {
    static const std::unordered_map<std::string, modbus_type_t> type_map = {
        { "UINT16",  modbus_type_t::UINT16},
        {  "INT16",   modbus_type_t::INT16},
        { "UINT32",  modbus_type_t::UINT32},
        {  "INT32",   modbus_type_t::INT32},
        {"FLOAT32", modbus_type_t::FLOAT32},
        {   "BOOL",    modbus_type_t::BOOL},
        {"IP_ADDR", modbus_type_t::IP_ADDR},
        {  "REGEX",   modbus_type_t::REGEX},
        { "STRING",  modbus_type_t::STRING}
    };
    auto it = type_map.find(s);
    return (it != type_map.end()) ? it->second : modbus_type_t::UNKNOWN;
}

function_code_t modbus_appliance_definition::string_to_fc(const std::string &s) {
    static const std::unordered_map<std::string, function_code_t> fc_map = {
        {                    "FC01",           function_code_t::READ_COILS},
        {                    "FC02", function_code_t::READ_DISCRETE_INPUTS},
        {                    "FC03",              function_code_t::READ_HR},
        {                    "FC04",              function_code_t::READ_IR},
        {                    "FC05",         function_code_t::WRITE_SINGLE},
        {                    "FC06",         function_code_t::WRITE_SINGLE},
        {                    "FC16",          function_code_t::WRITE_MULTI},
        {  "READ_HOLDING_REGISTERS",              function_code_t::READ_HR},
        {"WRITE_MULTIPLE_REGISTERS",          function_code_t::WRITE_MULTI}
    };
    auto it = fc_map.find(s);
    return (it != fc_map.end()) ? it->second : function_code_t::UNKNOWN;
}

register_source_t modbus_appliance_definition::string_to_source(const std::string &s) {
    static const std::unordered_map<std::string, register_source_t> src_map = {
        {          "MODBUS",           register_source_t::MODBUS},
        {      "USER_INPUT",       register_source_t::USER_INPUT},
        {"TEMPLATE_DEFINED", register_source_t::TEMPLATE_DEFINED}
    };
    auto it = src_map.find(s);
    return (it != src_map.end()) ? it->second : register_source_t::UNKNOWN;
}

uint16_t modbus_appliance_definition::calculate_register_count(uint16_t bit_size, modbus_type_t type) {
    // If the JSON defines the size explicitly (e.g. 16 or 32 bits), use it.
    if (bit_size > 0) {
        return (bit_size + 15) / 16; // Ceiling division by 16 bits per register
    }
    // Fallback: estimate based on type if size is missing
    if (type == modbus_type_t::UINT32 || type == modbus_type_t::INT32 || type == modbus_type_t::FLOAT32)
        return 2;
    return 1;
}

std::optional<ems_bridge_actuator_t> modbus_appliance_definition::get_actuator_type(const std::string &actuator_name) const {
    auto it = s2_actuator_map.find(actuator_name);
    if (it != s2_actuator_map.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<ems_bridge_sensor_t> modbus_appliance_definition::get_sensor_type(const std::string &sensor_name) const {
    auto it = s2_sensor_map.find(sensor_name);
    if (it != s2_sensor_map.end()) {
        return it->second;
    }
    return std::nullopt;
}
