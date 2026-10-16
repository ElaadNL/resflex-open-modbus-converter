// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "esp_err.h"

#define ESP_ERR_NOT_FOUND 0x105

typedef void* s2_client_handle_t;

typedef enum {
    S2_ROLE_RM = 0,
    S2_ROLE_CEM = 1,
} s2_role_t;

typedef enum {
    S2_DEPLOYMENT_LAN = 0,
    S2_DEPLOYMENT_WAN = 1,
} s2_deployment_t;

typedef enum {
    S2_COMMODITY_ELECTRICITY = 0,
} s2_commodity_t;

typedef enum {
    S2_ROLE_TYPE_CONSUMER = 0,
    S2_ROLE_TYPE_PRODUCER = 1,
    S2_ROLE_TYPE_STORAGE = 2,
} s2_role_type_t;

typedef enum {
    S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L1 = 0,
    S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L2 = 1,
    S2_COMMODITY_QUANTITY_ELECTRIC_POWER_L3 = 2,
    S2_COMMODITY_QUANTITY_ELECTRIC_POWER_3_PHASE_SYMMETRIC = 3,
} s2_commodity_quantity_t;

typedef enum {
    S2_PEBC_PARAM_POWER_UPPER_LIMIT_MAX = 0,
    S2_PEBC_PARAM_POWER_UPPER_LIMIT_MIN,
    S2_PEBC_PARAM_POWER_LOWER_LIMIT_MAX,
    S2_PEBC_PARAM_POWER_LOWER_LIMIT_MIN,
    S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MAX,
    S2_PEBC_PARAM_ENERGY_UPPER_LIMIT_MIN,
    S2_PEBC_PARAM_ENERGY_LOWER_LIMIT_MAX,
    S2_PEBC_PARAM_ENERGY_LOWER_LIMIT_MIN,
} s2_pebc_param_t;

typedef enum {
    S2_CONTROL_TYPE_PEBC = 0,
} s2_control_type_t;

#define S2_NODE_VERSIONS_MAX 4
#define S2_NODE_VERSION_LEN  32
#define S2_RM_ROLES_MAX      4
#define S2_RM_QUANTITIES_MAX 4
#define S2_PEBC_LIMITS_MAX   4

typedef void (*s2_pebc_on_instruction_cb_t)(s2_commodity_quantity_t, float, float);
typedef void (*s2_pebc_on_request_param_cb_t)(s2_pebc_param_t, s2_commodity_quantity_t, bool);
typedef void (*s2_on_request_measurement_cb_t)(s2_commodity_quantity_t);
typedef void (*s2_on_request_forecast_cb_t)(s2_commodity_quantity_t);
typedef void (*s2_on_session_ready_cb_t)(void);

typedef struct {
    char id[64];
    char brand[64];
    char logo_uri[128];
    char type[64];
    char model[64];
    char user_defined_name[64];
    char supported_versions[S2_NODE_VERSIONS_MAX][S2_NODE_VERSION_LEN];
    s2_role_t role;
    s2_deployment_t deployment;
} s2_node_t;

typedef struct {
    s2_commodity_t commodity;
    s2_role_type_t role_type;
} s2_rm_role_t;

typedef struct {
    s2_commodity_quantity_t commodity_quantity;
    float power_upper_limit;
    float power_lower_limit;
    float energy_upper_limit;
    float energy_lower_limit;
} s2_pebc_default_limit_t;

typedef struct {
    int default_limits_count;
    bool power_constraint_defer;
    s2_pebc_on_instruction_cb_t on_instruction;
    s2_pebc_on_request_param_cb_t on_request_param;
    s2_pebc_default_limit_t default_limits[S2_PEBC_LIMITS_MAX];
} s2_pebc_config_t;

typedef struct {
    char name[64];
    s2_control_type_t control_type;
    bool provides_forecast;
    uint32_t instruction_processing_delay_ms;
    uint32_t measurement_interval_ms;
    s2_on_request_measurement_cb_t on_request_measurement;
    s2_on_request_forecast_cb_t on_request_forecast;
    s2_on_session_ready_cb_t on_session_ready;
    s2_rm_role_t roles[S2_RM_ROLES_MAX];
    int roles_count;
    s2_commodity_quantity_t commodity_quantities[S2_RM_QUANTITIES_MAX];
    int commodity_quantities_count;
    s2_pebc_config_t pebc;
} s2_rm_config_t;

typedef struct {
    char base_url[128];
    bool enable_tls;
    const char* cert_pem;
    size_t cert_pem_len;
    char interface[16];
} s2_server_config_t;

typedef struct {
    s2_node_t node;
    s2_rm_config_t rm_config;
    s2_server_config_t server_config;
} s2_client_config_t;

typedef struct {
    char access_token[64];
} s2_client_request_pairing_response_t;

typedef struct {
    struct {
        char access_token[64];
        char communication_token[64];
    } token_pair;
    char connection_url[128];
} s2_client_initiate_connection_response_t;

s2_client_handle_t s2_client_init(s2_client_config_t* config);

esp_err_t s2_client_prepare_pairing(s2_client_handle_t handle);
esp_err_t s2_client_cancel_prepare_pairing(s2_client_handle_t handle);
esp_err_t s2_client_request_pairing(s2_client_handle_t handle, const char* pairing_input_token, s2_client_request_pairing_response_t* response);
esp_err_t s2_client_initiate_connection(s2_client_handle_t handle, const char* access_token, s2_client_initiate_connection_response_t* response);
esp_err_t s2_client_ws_connect(s2_client_handle_t handle, const char* url, const char* communication_token, const char* access_token);

esp_err_t s2_client_provide_measurement(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_abort_instruction(s2_client_handle_t handle);
esp_err_t s2_client_pebc_revoke_constraints(s2_client_handle_t handle);
esp_err_t s2_client_pebc_provide_energy_valid_until(s2_client_handle_t handle, s2_commodity_quantity_t quantity, int64_t deadline);

esp_err_t s2_client_pebc_provide_power_upper_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_power_upper_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_power_lower_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_power_lower_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_energy_upper_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_energy_upper_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_energy_lower_limit_max(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
esp_err_t s2_client_pebc_provide_energy_lower_limit_min(s2_client_handle_t handle, s2_commodity_quantity_t quantity, float value);
