// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <ArduinoJson.h>
#include <message_formats.h>
#include <ems_bridge.h>
#include <appliance_config.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

using modbus_type_t = modbus_value_type_t;

/**
 * Supported modbus function codes
 */
enum class function_code_t {
    READ_COILS = 1,
    READ_DISCRETE_INPUTS = 2,
    READ_HR = 3,      // Read holding registers
    READ_IR = 4,      // Read input registers
    WRITE_SINGLE = 6, // Write single register
    WRITE_MULTI = 16, // Write multiple registers
    UNKNOWN = 0
};

/**
 * Data sources
 */
enum class register_source_t {
    MODBUS,
    USER_INPUT,
    TEMPLATE_DEFINED,
    UNKNOWN
};

/**
 * Model for sequence steps (init, pre-write, post-write)
 */
struct sequence_step_t {
    function_code_t fc;
    uint16_t start_address;
    modbus_type_t type;
    float value;
    uint32_t wait_ms;
    uint16_t bit_mask; // 0 = whole register; otherwise only these bits are modified (read-modify-write)
};

/**
 * Label mapping for sensor enums
 */
struct output_mapping_t {
    float value;
    std::string label;
};

/**
 * Base class for any modbus register action
 */
struct base_register_t {
    std::string mapping_id;
    uint16_t start_address;
    function_code_t fc;
    modbus_type_t type;
    uint16_t register_count; // Calculated (16 bits = 1 register)
    float multiplier;
    bool big_endian;
};

/**
 * Sensor definition (Inherits from base_register_t)
 */
struct sensor_t : public base_register_t {
    ems_bridge_sensor_t emd_sensor_type;
    register_source_t source;
    uint16_t source_size; // Size in bits from JSON
    float template_value;
    bool has_template_value;
    uint32_t update_interval_ms;
    uint32_t validity_duration_ms;
    uint16_t bit_mask; // 0 = no mask; otherwise the read register value is AND-ed with this before outputMapping comparison
    std::vector<output_mapping_t> output_mapping;
};

/**
 * Actuator definition (Inherits from base_register_t)
 */
struct actuator_t : public base_register_t {
    ems_bridge_actuator_t emd_actuator_type;
    uint16_t destination_size; // Size in bits from JSON
    uint16_t bit_mask; // 0 = whole register; otherwise only these bits are modified (read-modify-write)
    uint32_t evaluation_interval_s;
    std::vector<std::string> required_sensors_before_evaluation; // this is not implemented yet, since there was no usecase found during our research
    float max_stepsize_up;
    float max_stepsize_down;
    uint32_t min_step_time_ms;
    std::vector<sequence_step_t> pre_sequence;
    std::vector<sequence_step_t> post_sequence;
};

enum class appliance_config_field_t {
    MODEL,
    SERIAL_NUMBER,
    FIRMWARE_VERSION
};

/**
 *  @brief appliance_config_modbus_source_t connects fields with source == MODBUS with the field in the
 *  configuration. These fields are parsed during startup, but filled once the Modbus connection is
 *  available and initialezed. This struct therefore ensures the connection for that stage.
 */
struct appliance_config_modbus_source_t {
    appliance_config_field_t field;
    function_code_t fc;
    uint16_t start_address;
    modbus_type_t type;
    uint16_t register_count;
    bool big_endian;
};

class modbus_appliance_definition {
  public:
    /**
     * @brief Creates an empty Modbus mapping instance.
     */
    modbus_appliance_definition();

    /**
     * @brief Loads the device template from storage and parses it into the mapping.
     * @return true if the stored template exists and was parsed successfully, false otherwise.
     */
    bool init();

    /**
     * @brief Loads only the appliance config from storage without parsing the full mapping.
     * @param config Output config object to populate.
     * @return true if the stored template exists and the config was parsed successfully, false otherwise.
     */
    static bool load_config_from_storage(appliance_config_t *config);

    /**
     * @brief Clears the current mapping and loads sensors, actuators, sequences and config from JSON.
     * @param json Device template JSON string to parse.
     * @return true if the JSON was parsed successfully, false otherwise.
     */
    bool load_from_json(const std::string &json);

    /**
     * @brief Parses only the appliance config from a device template JSON string.
     * @param json Device template JSON string to parse.
     * @param config Output config object to populate.
     * @return true if the JSON was parsed successfully, false otherwise.
     */
    static bool parse_config_from_json(const std::string &json, appliance_config_t *config);

    /**
     * @brief Serializes the loaded JSON template with the current appliance config values applied.
     * @return Full device template JSON string, including unchanged fields from the original JSON document.
     */
    std::string save_to_json() const;

    /**
     * @brief Reads appliance config fields that are defined with source MODBUS and writes them into config.
     * @return true if all MODBUS sourced config fields were resolved, false if one or more reads failed.
     */
    bool resolve_modbus_config_fields();

    /**
     * @brief Finds the index of an actuator in the loaded actuator list.
     * @param actuator_id EMS bridge actuator identifier to look up.
     * @param actuator_index Output pointer that receives the matching actuator index.
     * @return true if the actuator was found and actuator_index was written, false otherwise.
     */
    bool find_actuator_index(const ems_bridge_actuator_t actuator_id, uint16_t *actuator_index) const;

    /**
     * @brief Finds the index of a sensor in the loaded sensor list.
     * @param sensor_id EMS bridge sensor identifier to look up.
     * @param sensor_index Output pointer that receives the matching sensor index.
     * @return true if the sensor was found and sensor_index was written, false otherwise.
     */
    bool find_sensor_index(const ems_bridge_sensor_t sensor_id, uint16_t *sensor_index) const;

    /**
     * @brief Converts a template actuator mapping name to an EMS bridge actuator identifier.
     * @param name Actuator mapping name from the device template.
     * @return Matching actuator identifier, or std::nullopt when the name is unknown.
     */
    std::optional<ems_bridge_actuator_t> get_actuator_type(const std::string &name) const;

    /**
     * @brief Converts a template sensor mapping name to an EMS bridge sensor identifier.
     * @param name Sensor mapping name from the device template.
     * @return Matching sensor identifier, or std::nullopt when the name is unknown.
     */
    std::optional<ems_bridge_sensor_t> get_sensor_type(const std::string &name) const;

    /**
     * @brief this contains the available sensors
     */
    std::vector<sensor_t> sensors;
    /**
     * @brief this contains the available actuators
     */
    std::vector<actuator_t> actuators;
    /**
     * @brief this contains the available initialisation_sequence
     */
    std::vector<sequence_step_t> initialisation_sequence;

    inline static const std::unordered_map<std::string, ems_bridge_actuator_t> s2_actuator_map = {
        {"SETPOINT_MAX_CURRENT_PHASE_SYMMETRIC", EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_PHASE_SYMMETRIC_A},
        {             "SETPOINT_MAX_CURRENT_L1",                 EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L1_A},
        {             "SETPOINT_MAX_CURRENT_L2",                 EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L2_A},
        {             "SETPOINT_MAX_CURRENT_L3",                 EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_CURRENT_L3_A},

        {  "SETPOINT_MAX_POWER_PHASE_SYMMETRIC",   EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_PHASE_SYMMETRIC_W},
        {               "SETPOINT_MAX_POWER_L1",                   EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W},
        {               "SETPOINT_MAX_POWER_L2",                   EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L2_W},
        {               "SETPOINT_MAX_POWER_L3",                   EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L3_W},

        {"SETPOINT_MIN_CURRENT_PHASE_SYMMETRIC", EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_PHASE_SYMMETRIC_A},
        {             "SETPOINT_MIN_CURRENT_L1",                 EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_L1_A},
        {             "SETPOINT_MIN_CURRENT_L2",                 EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_L2_A},
        {             "SETPOINT_MIN_CURRENT_L3",                 EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_CURRENT_L3_A},

        {  "SETPOINT_MIN_POWER_PHASE_SYMMETRIC",   EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_PHASE_SYMMETRIC_W},
        {               "SETPOINT_MIN_POWER_L1",                   EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L1_W},
        {               "SETPOINT_MIN_POWER_L2",                   EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L2_W},
        {               "SETPOINT_MIN_POWER_L3",                   EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L3_W},

        {                 "HEARTBEAT",                                        HEARTBEAT}
    };

    inline static const std::unordered_map<std::string, ems_bridge_sensor_t> s2_sensor_map = {
        {       "ACTUAL_CURRENT_L1",         EMS_BRIDGE_SENSOR_ACTUAL_CURRENT_L1_A},
        {       "ACTUAL_CURRENT_L2",         EMS_BRIDGE_SENSOR_ACTUAL_CURRENT_L2_A},
        {       "ACTUAL_CURRENT_L3",         EMS_BRIDGE_SENSOR_ACTUAL_CURRENT_L3_A},
        {    "ACTUAL_CURRENT_TOTAL",      EMS_BRIDGE_SENSOR_ACTUAL_CURRENT_TOTAL_A},

        {       "ACTUAL_VOLTAGE_L1",         EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L1_V},
        {       "ACTUAL_VOLTAGE_L2",         EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L2_V},
        {       "ACTUAL_VOLTAGE_L3",         EMS_BRIDGE_SENSOR_ACTUAL_VOLTAGE_L3_V},

        {         "ACTUAL_POWER_L1",           EMS_BRIDGE_SENSOR_ACTUAL_POWER_L1_W},
        {         "ACTUAL_POWER_L2",           EMS_BRIDGE_SENSOR_ACTUAL_POWER_L2_W},
        {         "ACTUAL_POWER_L3",           EMS_BRIDGE_SENSOR_ACTUAL_POWER_L3_W},
        {      "ACTUAL_POWER_TOTAL",        EMS_BRIDGE_SENSOR_ACTUAL_POWER_TOTAL_W},

        { "POWER_LIMIT_UPPER_START",   EMS_BRIDGE_SENSOR_POWER_LIMIT_UPPER_START_W},
        {   "POWER_LIMIT_UPPER_END",     EMS_BRIDGE_SENSOR_POWER_LIMIT_UPPER_END_W},
        { "POWER_LIMIT_LOWER_START",   EMS_BRIDGE_SENSOR_POWER_LIMIT_LOWER_START_W},
        {   "POWER_LIMIT_LOWER_END",     EMS_BRIDGE_SENSOR_POWER_LIMIT_LOWER_END_W},

        {"ENERGY_LIMIT_UPPER_START", EMS_BRIDGE_SENSOR_ENERGY_LIMIT_UPPER_START_WH},
        {  "ENERGY_LIMIT_UPPER_END",   EMS_BRIDGE_SENSOR_ENERGY_LIMIT_UPPER_END_WH},
        {"ENERGY_LIMIT_LOWER_START", EMS_BRIDGE_SENSOR_ENERGY_LIMIT_LOWER_START_WH},
        {  "ENERGY_LIMIT_LOWER_END",   EMS_BRIDGE_SENSOR_ENERGY_LIMIT_LOWER_END_WH},

        {                  "STATUS",                      EMS_BRIDGE_SENSOR_STATUS}
    };

    /**
     * @brief config contains a memory representation of the configuration as read from the JSON template
     *        and is the only changable element. Saving this again to json, please use the save_to_json method.
     */
    appliance_config_t config;

  private:
    /**
     * @brief Parses the full device template into config, sequences, sensors and actuators.
     */
    bool parse_json(const std::string &json);

    /**
     * @brief Parses the appliance config section into the internal config object.
     */
    static void parse_config(JsonDocument &doc, appliance_config_t *config);

    /**
     * @brief Collects appliance config fields whose value must be read from Modbus later.
     */
    void parse_config_modbus_sources(JsonDocument &doc);

    /**
     * @brief Adds one Modbus-backed appliance config field to the deferred source list.
     */
    void parse_config_modbus_source(JsonObject field_object, appliance_config_field_t field);

    /**
     * @brief Writes a resolved Modbus-backed config value into the internal config object.
     */
    bool write_resolved_config_field(appliance_config_field_t field, const std::string &value);

    /**
     * @brief Writes the current appliance config values back into a JSON document.
     */
    void write_config_to_json(JsonDocument &doc) const;

    /**
     * @brief Converts a template type string to a Modbus value type.
     */
    modbus_type_t string_to_type(const std::string &s);

    /**
     * @brief Converts a template function-code string to an internal function code.
     */
    function_code_t string_to_fc(const std::string &s);

    /**
     * @brief Converts a template source string to an internal register source.
     */
    register_source_t string_to_source(const std::string &s);

    /**
     * @brief Calculates how many 16-bit Modbus registers a value occupies.
     */
    uint16_t calculate_register_count(uint16_t bit_size, modbus_type_t type);

    /**
     * @brief Parses a JSON sequence array into sequence steps.
     */
    void parse_sequence(JsonArray arr, std::vector<sequence_step_t> &target);

    std::string source_json;
    std::vector<appliance_config_modbus_source_t> config_modbus_sources;
};
