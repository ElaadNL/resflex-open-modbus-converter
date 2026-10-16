// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#include "s2_library_wrapper.h"
#include <cstdint>
#include <cstring>
#include "s2_client.h"
#include "storage_component.h"

/**
 * the position here is important! It fixes the error 
 * lwip/ip4_addr.h:63:37: error: expected ')' before numeric constant
 63 | #define IPADDR_NONE         ((u32_t)0xffffffffUL)
 */
#undef INADDR_NONE 

#include "definitions.h"
#include "ems_bridge.h"
#include "esp_log.h"
#include "esp_random.h"
#include "led_component.h"
#include "message_formats.h"
#include "freertos/task.h"

static const char* TAG = "S2-Wrapper";

/* Global s2 lib handle */
static s2_client_handle_t s2_handle = nullptr;
static s2_client_config_t s2_client_config ={};

static ems_bridge_request_sensor_cb_t _request_sensor_cb;
static ems_bridge_set_actuator_cb_t _set_actuator_cb;
static ems_bridge_on_init_ready_cb_t _on_init_ready_cb;

// called when an instruction becomes active (CEM limits) or expires (back to config defaults).
// real impl: write limits to hardware here. if that fails, call s2_client_pebc_abort_instruction().
//
// This callback fires in two situations:
//   1. Instruction STARTED  -> upper_limit/lower_limit are the CEM's instructed values.
//   2. Instruction REVOKED -> upper_limit/lower_limit are your config default_limits.
//      Use this to reset hardware back to its normal operating range.
//
// NOTES:   called once PER QUANTITY present in the instruction. Always use `quantity` value to apply
//          limits to the correct channel.
//          abnormal_condition is handled inside the library. If the CEM sends an instruction with
//          abnormal_condition = true, this callback fires using the constraints that have abnormal_condition_only = true
//          (the library validates them against abnormal_condition_only constraint limits in config).
//          You do not need to distinguish abnormal vs normal here just apply whatever limits arrive.
//          Library handles all validation of abnormal/normal and if instruction is within constraints.
static void on_instruction(s2_commodity_quantity_t quantity, float upper_limit, float lower_limit){


    ESP_LOGI(TAG, "on_instruction: quantity=%d upper=%.1fW lower=%.1fW", (int)quantity, upper_limit, lower_limit);

    ems_bridge_actuator_t actuatorIdLower;
    ems_bridge_actuator_t actuatorIdUpper;

    switch(quantity){
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1:
            actuatorIdLower = EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L1_W;
            actuatorIdUpper = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L1_W;
            break;
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2:
            actuatorIdLower = EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L2_W;
            actuatorIdUpper = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L2_W;
            break;
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3:
            actuatorIdLower = EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_L3_W;
            actuatorIdUpper = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_L3_W;
            break;
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC:
            actuatorIdLower = EMS_BRIDGE_ACTUATOR_SETPOINT_MIN_POWER_PHASE_SYMMETRIC_W;
            actuatorIdUpper = EMS_BRIDGE_ACTUATOR_SETPOINT_MAX_POWER_PHASE_SYMMETRIC_W;
            break;
        default:
            // Handle error or unknown param
            return;
    }

    ems_bridge_set_actuator_request lowerRequest = {
        .actuatorId = actuatorIdLower,
        .value = lower_limit
    };
    ems_bridge_set_actuator_request upperRequest = {
        .actuatorId = actuatorIdUpper,
        .value = upper_limit
    };
   _set_actuator_cb(lowerRequest);
   _set_actuator_cb(upperRequest);

}

static void on_request_measurement(s2_commodity_quantity_t quantity){
    
    ems_bridge_sensor_t sensorId;
    switch(quantity) {
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1:
            sensorId = EMS_BRIDGE_SENSOR_ACTUAL_POWER_L1_W;
            break;
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2:
            sensorId = EMS_BRIDGE_SENSOR_ACTUAL_POWER_L2_W;
            break;
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3:
            sensorId = EMS_BRIDGE_SENSOR_ACTUAL_POWER_L3_W;
            break;
        case S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC:
            sensorId = EMS_BRIDGE_SENSOR_ACTUAL_POWER_TOTAL_W;
            break;
        default:
            // Handle error or unknown param
            return;
    }
    _request_sensor_cb(sensorId);
}

static void on_request_forecast(s2_commodity_quantity_t quantity){
    // not implemented yet, as it is not applicable to the modbus devices we researched
}

static void on_request_param(s2_pebc_param_t param, s2_commodity_quantity_t quantity, bool is_abnormal){
    
    ems_bridge_sensor_t sensorId;
    switch(param) {
        case S2_PEBC_PARAM_POWER_UPPER_LIMIT_MAX:
            sensorId = EMS_BRIDGE_SENSOR_POWER_LIMIT_UPPER_END_W;
            break;
        case S2_PEBC_PARAM_POWER_UPPER_LIMIT_MIN:
            sensorId = EMS_BRIDGE_SENSOR_POWER_LIMIT_UPPER_START_W;
            break;
        case S2_PEBC_PARAM_POWER_LOWER_LIMIT_MAX:
            sensorId = EMS_BRIDGE_SENSOR_POWER_LIMIT_LOWER_END_W;
            break;
        case S2_PEBC_PARAM_POWER_LOWER_LIMIT_MIN:
            sensorId = EMS_BRIDGE_SENSOR_POWER_LIMIT_LOWER_START_W;
            break;
        case S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MAX:
            sensorId = EMS_BRIDGE_SENSOR_ENERGY_LIMIT_UPPER_END_WH;
            break;
        case S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MIN:
            sensorId = EMS_BRIDGE_SENSOR_ENERGY_LIMIT_UPPER_START_WH;
            break;
        case S2_PEBC_PARAM_ENERGY_LOWER_LIMIT_MAX:
            sensorId = EMS_BRIDGE_SENSOR_ENERGY_LIMIT_LOWER_END_WH;
            break;
        case S2_PEBC_PARAM_ENERGY_LOWER_LIMIT_MIN:
            sensorId = EMS_BRIDGE_SENSOR_ENERGY_LIMIT_LOWER_START_WH;
            break;
        default:
            // Handle error or unknown param
            return;
    }
    _request_sensor_cb(sensorId);
}

/**
 * @brief Helpers function for setting all quantities to the same value
 * 
 * @param setter Function pointer to the esp_err_t function that handles the assignment per quantity.
 * @param value  The float value to be applied to each commodity quantity.
 */
static void set_same_sensor_for_all_quantities_float(esp_err_t (*setter)(s2_client_handle_t, s2_commodity_quantity_t, float), float value){
    for(int i = 0; i< s2_client_config.rm_config.commodity_quantities_count; i++){
        setter(s2_handle, s2_client_config.rm_config.commodity_quantities[i], value);
    }
}

void s2_on_new_sensor_value(ems_bridge_sensor_result sensor_result){
    if (sensor_result.status != EMS_BRIDGE_STATUS_OK) {
        ESP_LOGW(TAG, "Ignoring sensor result with error status for sensorId=%d", sensor_result.sensorId);
        return;
    }

    bool is_abnormal = false; // this is not applicable to modbus devices
    // in case of abornmal, the values should be set different? don't know -> need to check
    // this must be applied to all registered quantities -> is the same (for now??), hence the set_same_sensor_for_all_quantities_float mechanism
    // call the relevant s2 function, based on the sensorId
    switch(sensor_result.sensorId){

        // THE PARAMS
        case EMS_BRIDGE_SENSOR_POWER_LIMIT_UPPER_END_W:
            set_same_sensor_for_all_quantities_float(s2_client_pebc_provide_power_upper_limit_max, sensor_result.value);
            break;
        case EMS_BRIDGE_SENSOR_POWER_LIMIT_UPPER_START_W:
            set_same_sensor_for_all_quantities_float(s2_client_pebc_provide_power_upper_limit_min, sensor_result.value);
            break;
        case EMS_BRIDGE_SENSOR_POWER_LIMIT_LOWER_END_W:
            set_same_sensor_for_all_quantities_float(s2_client_pebc_provide_power_lower_limit_max, sensor_result.value);
            break;
        case EMS_BRIDGE_SENSOR_POWER_LIMIT_LOWER_START_W:
            set_same_sensor_for_all_quantities_float(s2_client_pebc_provide_power_lower_limit_min, sensor_result.value);
            break;
        // energy constraint range: average power bounds over the energy window (for smart scheduling)
        case EMS_BRIDGE_SENSOR_ENERGY_LIMIT_UPPER_END_WH:
            set_same_sensor_for_all_quantities_float(s2_client_pebc_provide_energy_upper_limit_max, sensor_result.value);
            // Optional: override the default valid_until for this energy constraint (default: now + 24h).
            // Call this in the same on_request_param as the energy limit provides.
            // time_t deadline = time(nullptr) + 8 * 3600; // valid for 8 hours from now

            // s2_client_pebc_provide_energy_valid_until(s2_handle, quantity, deadline);
            break;
        case EMS_BRIDGE_SENSOR_ENERGY_LIMIT_UPPER_START_WH:
            set_same_sensor_for_all_quantities_float(s2_client_pebc_provide_energy_upper_limit_min, sensor_result.value);
            break;
        case EMS_BRIDGE_SENSOR_ENERGY_LIMIT_LOWER_END_WH:
            set_same_sensor_for_all_quantities_float(s2_client_pebc_provide_energy_lower_limit_max, sensor_result.value);
            break;
        case EMS_BRIDGE_SENSOR_ENERGY_LIMIT_LOWER_START_WH:
            set_same_sensor_for_all_quantities_float(s2_client_pebc_provide_energy_lower_limit_min, sensor_result.value);
            break;

        // THE MEASUREMENTS
        case EMS_BRIDGE_SENSOR_ACTUAL_POWER_L1_W:
            // note: example has a cap mechanism -> i am not sure that is ok, because we want the raw data from the appliance(?)
            s2_client_provide_measurement(s2_handle, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1, sensor_result.value);
            break;  
        case EMS_BRIDGE_SENSOR_ACTUAL_POWER_L2_W:
            // note: example has a cap mechanism -> i am not sure that is ok, because we want the raw data from the appliance(?)
            s2_client_provide_measurement(s2_handle, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2, sensor_result.value);
            break;  
        case EMS_BRIDGE_SENSOR_ACTUAL_POWER_L3_W:
            // note: example has a cap mechanism -> i am not sure that is ok, because we want the raw data from the appliance(?)
            s2_client_provide_measurement(s2_handle, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3, sensor_result.value);
            break;  
        case EMS_BRIDGE_SENSOR_ACTUAL_POWER_TOTAL_W:
            // note: example has a cap mechanism -> i am not sure that is ok, because we want the raw data from the appliance(?)
            s2_client_provide_measurement(s2_handle, S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC, sensor_result.value);
            break;  
        default:
            return;
    }
}

static void on_session_ready(void){
      ESP_LOGI(TAG, "PEBC session ready - timers started, on_request_param will be called periodically for measurements, constraints and forecasts");
      set_LED_state(LED_STATE_ALL_OK);

     if (_on_init_ready_cb != nullptr) {
        _on_init_ready_cb();
     }
}

/**
 * @brief Load the S2 node id from NVS, or generate and persist a UUIDv4.
 *
 * On first boot the NVS key `s2_node_id` is empty; we generate a random
 * RFC 4122 v4 UUID, store it, and reuse it across reboots so the S2 server
 * keeps recognising this device as the same node. NVS-wipe = new identity.
 *
 * @param node_id      Destination buffer (NUL-terminated UUID string).
 * @param node_id_size Size of @p node_id in bytes (must be >= 37 for a full UUID).
 */
static void load_or_init_node_id(char* node_id, size_t node_id_size){
    storage_get_configuration(NVS_STORAGE_KEY_S2_NODE_ID, node_id, node_id_size, "");
    if (node_id[0] != '\0') {
        return;
    }

    uint8_t random_bytes[16] = {};
    esp_fill_random(random_bytes, sizeof(random_bytes));

    // RFC 4122 v4: version 4 in byte 6 high-nibble, variant 10xx in byte 8 high-bits.
    random_bytes[6] = (random_bytes[6] & 0x0F) | 0x40;
    random_bytes[8] = (random_bytes[8] & 0x3F) | 0x80;

    snprintf(node_id, node_id_size,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             random_bytes[0],  random_bytes[1],  random_bytes[2],  random_bytes[3],
             random_bytes[4],  random_bytes[5],
             random_bytes[6],  random_bytes[7],
             random_bytes[8],  random_bytes[9],
             random_bytes[10], random_bytes[11], random_bytes[12], random_bytes[13], random_bytes[14], random_bytes[15]);

    storage_set_configuration(NVS_STORAGE_KEY_S2_NODE_ID, node_id);
    ESP_LOGI(TAG, "S2 node id not set yet; generated UUIDv4 and stored: %s", node_id);
}

static s2_client_config_t s2_construct_config(ems_bridge_setup_values* appliance_config, s2_hems_values* s2_hems_config){

    s2_client_config_t s2_config = {};

    load_or_init_node_id(s2_config.node.id, sizeof(s2_config.node.id));
    strlcpy(s2_config.node.brand,                 appliance_config->brand, sizeof(s2_config.node.brand));
    strlcpy(s2_config.node.logo_uri,              appliance_config->logo_uri, sizeof(s2_config.node.logo_uri));
    strlcpy(s2_config.node.type,                  appliance_config->type, sizeof(s2_config.node.type));
    strlcpy(s2_config.node.model,                 appliance_config->model, sizeof(s2_config.node.model));
    strlcpy(s2_config.node.user_defined_name,     appliance_config->user_defined_name, sizeof(s2_config.node.user_defined_name));
    strlcpy(s2_config.rm_config.name,             appliance_config->user_defined_name, sizeof(s2_config.rm_config.name));
    strlcpy(s2_config.node.supported_versions[0], "0.0.1-beta", sizeof(s2_config.node.supported_versions[0])); // should be hardcoded - this is the s2-version, so it only concerns this wrapper-component
    s2_config.node.role = S2_ROLE_RM; // we are are a Resource Manager
    s2_config.node.deployment = S2_DEPLOYMENT_LAN; // for now hardcoded

    strlcpy(const_cast<char*>(s2_config.server_config.base_url), s2_hems_config->base_url, sizeof(s2_config.server_config.base_url));
    strlcpy(const_cast<char*>(s2_config.server_config.interface), s2_hems_config->interface, sizeof(s2_config.server_config.interface));

    // TLS: hand the captive-portal value to the S2 client. The library expects
    // cert_pem as a NUL-terminated PEM string with cert_pem_len = strlen + 1
    // (per s2_client.h). When TLS is off, leave the pointer NULL so the library
    // takes its plain-TCP path.
    s2_config.server_config.enable_tls = s2_hems_config->tls_enabled;
    // Bound the length scan to the buffer size; a result equal to the size means
    // no terminator was found, so treat the buffer as holding no certificate.
    size_t cert_pem_len = strnlen(s2_hems_config->cert_pem, sizeof(s2_hems_config->cert_pem));
    if (s2_hems_config->tls_enabled && cert_pem_len > 0 && cert_pem_len < sizeof(s2_hems_config->cert_pem)) {
        s2_config.server_config.cert_pem = s2_hems_config->cert_pem;
        s2_config.server_config.cert_pem_len = cert_pem_len + 1;
    }
    else {
        s2_config.server_config.cert_pem = nullptr;
        s2_config.server_config.cert_pem_len = 0;
    }

    // s2_config.server_config.skip_cert_common_name_check = true;

    s2_config.rm_config.control_type = S2_CONTROL_TYPE_PEBC; // we only use pebc
    s2_config.rm_config.provides_forecast = false; // no forecasts for now
    s2_config.rm_config.instruction_processing_delay_ms = appliance_config->control.instruction_processing_delay_ms;
    s2_config.rm_config.measurement_interval_ms = 5000; // optional, 0 = default (60s)
    s2_config.rm_config.on_request_measurement = on_request_measurement;
    s2_config.rm_config.on_request_forecast = on_request_forecast;
    s2_config.rm_config.on_session_ready = on_session_ready;
    s2_config.rm_config.pebc.power_constraint_defer = appliance_config->control.power_constraint_defer; // optional, false = VANISH (safe default), true = DEFER
    s2_config.rm_config.pebc.on_instruction = on_instruction;
    s2_config.rm_config.pebc.on_request_param = on_request_param;

    // fill the roles dynamically
    for(int i=0; i<EMS_BRIDGE_ROLES_MAX_AMOUNT; i++){
        if(appliance_config->control.roles[i]==EMS_BRIDGE_ROLE_SENTINEL){
            break;
        }
        s2_config.rm_config.roles[i] = { .commodity = S2_COMMODITY_ELECTRICITY, .role_type = (s2_role_type_t)appliance_config->control.roles[i] };
        s2_config.rm_config.roles_count = i + 1;
    }

    // Control-side declaration. Two paths, selected by phase_symmetric:
    //
    //   true  -> SYMMETRIC. The appliance has one setpoint register that applies
    //            to all phases. Declare a single ELECTRIC.POWER.3_PHASE_SYMMETRIC
    //            commodity quantity. The CEM will then send one PowerConstraint
    //            and one PEBC.Instruction per cycle, mapped by on_instruction
    //            onto SETPOINT_MAX_POWER_PHASE_SYMMETRIC / SETPOINT_MIN_POWER_PHASE_SYMMETRIC.
    //   false -> PER_PHASE. Each phase has its own setpoint. Declare
    //            ELECTRIC.POWER.L1/L2/L3 (one entry per phase_count) and map
    //            per-phase instructions onto SETPOINT_MAX_POWER_L1/L2/L3 /
    //            SETPOINT_MIN_POWER_L1/L2/L3.
    //
    // Independent from measurement declaration: a device may measure per phase
    // and still be controlled symmetrically (e.g. many EV chargers).
    if (appliance_config->control.phase_symmetric) {
        s2_config.rm_config.commodity_quantities_count = 1;
        s2_config.rm_config.commodity_quantities[0] = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC;

        s2_config.rm_config.pebc.default_limits_count = 1;
        s2_config.rm_config.pebc.default_limits[0] = {
            .commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC,
            .power_upper_limit  = appliance_config->control.default_limits[0].power_upper_limit,
            .power_lower_limit  = appliance_config->control.default_limits[0].power_lower_limit,
            .energy_upper_limit = appliance_config->control.default_limits[0].energy_upper_limit,
            .energy_lower_limit = appliance_config->control.default_limits[0].energy_lower_limit,
        };
    }
    else {
        s2_config.rm_config.commodity_quantities_count = appliance_config->control.phase_count;
        s2_config.rm_config.pebc.default_limits_count = appliance_config->control.phase_count;

        // apply default limits and fill the commodoties (phases); This is only electric.
        for(int i = 0; i<appliance_config->control.phase_count; i++){

            // set commodity
            s2_commodity_quantity_t commodity_quantity;
            switch(i){
                case 0:
                    commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1;
                break;
                case 1:
                    commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2;
                break;
                case 2:
                    commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3;
                break;
                default:
                    commodity_quantity = S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1;
            }
            s2_config.rm_config.commodity_quantities[i] = commodity_quantity;
            s2_config.rm_config.pebc.default_limits[i] = {
                .commodity_quantity = commodity_quantity,
                .power_upper_limit = appliance_config->control.default_limits[i].power_upper_limit,
                .power_lower_limit = appliance_config->control.default_limits[i].power_lower_limit,
                .energy_upper_limit = appliance_config->control.default_limits[i].energy_upper_limit,
                .energy_lower_limit = appliance_config->control.default_limits[i].energy_lower_limit,
            };
        }
    }

    return s2_config;

}

static bool s2_check_pairing(s2_client_handle_t s2_client_handle) {
    char access_token[S2_ACCESS_TOKEN_MAX_LEN] = {};
    bool has_token = storage_get_configuration(NVS_KEY_S2_ACCESS_TOKEN, access_token, sizeof(access_token), nullptr)
                     && access_token[0] != '\0';
    if (has_token) {
        ESP_LOGI(TAG, "Access token found in NVS");
    } else {
        ESP_LOGW(TAG, "No access token in NVS — call s2_do_pairing() with input token first");
    }
    return has_token;
}

static bool s2_do_pairing(s2_client_handle_t s2_client_handle, const char* pairing_input_token) {
    char access_token[S2_ACCESS_TOKEN_MAX_LEN] = {};
    esp_err_t err;

    err = s2_client_prepare_pairing(s2_client_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed request to /preparePairing!");
        return false;
    }
    ESP_LOGI(TAG, "Successful request to /preparePairing!");

    s2_client_request_pairing_response_t* req_pairing_res =
        (s2_client_request_pairing_response_t*)malloc(sizeof(s2_client_request_pairing_response_t));
    if (!req_pairing_res) {
        ESP_LOGE(TAG, "Failed to allocate memory for requestPairing response");
        return false;
    }

    err = s2_client_request_pairing(s2_client_handle, pairing_input_token, req_pairing_res);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed request to /requestPairing!");
        free(req_pairing_res);
        return false;
    }

    ESP_LOGI(TAG, "Successful request to /requestPairing!");
    ESP_LOGD(TAG, "Access token = %s", req_pairing_res->access_token);
    strlcpy(access_token, req_pairing_res->access_token, sizeof(access_token));
    free(req_pairing_res);

    if (!storage_set_configuration(NVS_KEY_S2_ACCESS_TOKEN, access_token)) {
        ESP_LOGW(TAG, "Failed to save access token to NVS");
    }

    return true;
}

static bool s2_start_pairing(s2_client_handle_t s2_client_handle) {
    char access_token[S2_ACCESS_TOKEN_MAX_LEN] = {};
    esp_err_t err;

    if (!storage_get_configuration(NVS_KEY_S2_ACCESS_TOKEN, access_token, sizeof(access_token), nullptr)
        || access_token[0] == '\0') {
        ESP_LOGE(TAG, "No access token in NVS — call s2_check_pairing() / s2_do_pairing() first");
        return false;
    }

    s2_client_initiate_connection_response_t* init_connection_res = (s2_client_initiate_connection_response_t*)malloc(sizeof(s2_client_initiate_connection_response_t));
    if (!init_connection_res) {
        ESP_LOGE(TAG, "Failed to allocate memory for initiateConnection response");
        return false;
    }

    err = s2_client_initiate_connection(s2_client_handle, access_token, init_connection_res);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Stored access token rejected by HEMS, clearing token");
        storage_set_configuration(NVS_KEY_S2_ACCESS_TOKEN, "");
        free(init_connection_res);
        return false;
    }

    ESP_LOGI(TAG, "Successful request to /initiateConnection! URL = %s", init_connection_res->connection_url);
    ESP_LOGD(TAG, "Access token = %s, Communication token = %s",
                init_connection_res->token_pair.access_token,
                init_connection_res->token_pair.communication_token);

    // Save rotated access token so the next initiateConnection uses the new one.
    // HA invalidates the old token immediately on initiateConnection, so without this the device unpairs on every restart.
    if (!storage_set_configuration(NVS_KEY_S2_ACCESS_TOKEN, init_connection_res->token_pair.access_token)) {
        ESP_LOGW(TAG, "Failed to save rotated access token to NVS");
    }

    err = s2_client_ws_connect(s2_client_handle, init_connection_res->connection_url, init_connection_res->token_pair.communication_token, init_connection_res->token_pair.access_token);
    free(init_connection_res);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to websocket server");
        return false;
    }

    return true;
}


esp_err_t s2_init(
    s2_hems_values* hems_values,
    ems_bridge_setup_values* appliance_config,
    ems_bridge_request_sensor_cb_t request_sensor_cb,
    ems_bridge_set_actuator_cb_t set_actuator_cb,
    ems_bridge_on_init_ready_cb_t on_init_ready_cb
){
    esp_err_t ret = ESP_FAIL;

    _request_sensor_cb = request_sensor_cb;
    _set_actuator_cb = set_actuator_cb;
    _on_init_ready_cb = on_init_ready_cb;

    s2_client_config_t temp = s2_construct_config(appliance_config, hems_values);
    memcpy(&s2_client_config, &temp, sizeof(s2_client_config_t));

    s2_handle = s2_client_init(&s2_client_config);
    if (s2_handle == nullptr) {
        ESP_LOGE(TAG, "Initialization failed!");
        set_LED_state(LED_STATE_FATAL_ERROR);
        goto final;
    }
    ESP_LOGI(TAG, "Initialization success!");

    if (!s2_check_pairing(s2_handle)) {
        if (hems_values->pairing_input_token[0] == '\0') {
            ESP_LOGE(TAG, "No access token in NVS and no pairing_input_token provided");
            set_LED_state(LED_STATE_FATAL_ERROR);
            ret = ESP_ERR_NOT_FOUND;
            goto final;
        }
        if (!s2_do_pairing(s2_handle, hems_values->pairing_input_token)) {
            ESP_LOGE(TAG, "Pairing failed!");
            set_LED_state(LED_STATE_FATAL_ERROR);
            goto final;
        }
    }

    if (!s2_start_pairing(s2_handle)) {
        ESP_LOGE(TAG, "S2 start pairing failed!");
        set_LED_state(LED_STATE_FATAL_ERROR);
        goto final;
    }

    ret = ESP_OK;

final:
    if (ret != ESP_OK) {
        // s2_client has no deinit — clear our own state so the wrapper is in a clean uninitialised state.
        s2_handle = nullptr;
        _request_sensor_cb = nullptr;
        _set_actuator_cb = nullptr;
        _on_init_ready_cb = nullptr;
    }
    return ret;
}

void s2_on_actuator_set(ems_bridge_set_actuator_result){
    // no usecase found, so not implemented
}

