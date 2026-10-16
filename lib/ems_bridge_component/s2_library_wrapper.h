// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <net/if.h>
#include "definitions.h"
#include "ems_bridge.h"

#define NVS_KEY_S2_ACCESS_TOKEN "s2_access_tkn"
#define S2_ACCESS_TOKEN_MAX_LEN 64


/**
 * @brief Runtime HEMS/CEM connection values for the S2 client.
 */
struct s2_hems_values {
    char base_url[128];                                /*!< HEMS/CEM S2 base URL. */
    char interface[IF_NAMESIZE];                       /*!< Network interface name used to bind S2 traffic; empty means default routing. */
    char pairing_input_token[S2_ACCESS_TOKEN_MAX_LEN]; /*!< Pairing token from HEMS UI; leave empty when already paired. */
    bool tls_enabled;                                  /*!< When true, use TLS (wss://) and validate the websocket cert against cert_pem. */
    char cert_pem[CAPTIVE_TLS_PEM_BUF];                /*!< Trusted CA certificate in PEM form; ignored when tls_enabled is false. */
};


/**
 * @brief Initialize the S2 client wrapper and start the S2 session.
 *
 * Builds the S2 client configuration from the appliance and HEMS values,
 * performs pairing when no access token is stored, starts the websocket
 * connection, and registers callbacks that translate S2 requests to EMS
 * bridge requests.
 *
 * @param hems_values HEMS/CEM URL, interface binding and optional pair token.
 * @param appliance_config EMS bridge appliance setup used to construct the S2 resource manager config.
 * @param request_sensor_cb Callback used when S2 requests a measurement or PEBC parameter.
 * @param set_actuator_cb Callback used when S2 issues PEBC actuator limits.
 * @param on_init_ready_cb Callback invoked when the S2 session is ready.
 * @return ESP_OK on successful session startup, otherwise an ESP-IDF error code.
 */
esp_err_t s2_init(
    s2_hems_values *hems_values,
    ems_bridge_setup_values *appliance_config,
    ems_bridge_request_sensor_cb_t request_sensor_cb,
    ems_bridge_set_actuator_cb_t set_actuator_cb,
    ems_bridge_on_init_ready_cb_t on_init_ready_cb);

/**
 * @brief Forward a completed EMS bridge sensor read to the S2 client.
 *
 * Successful values are mapped to S2 measurement or PEBC provide calls.
 * Results with error status are logged and ignored.
 *
 * @param result Sensor result produced by the EMS bridge implementation.
 */
void s2_on_new_sensor_value(ems_bridge_sensor_result result);

/**
 * @brief Forward a completed EMS bridge actuator write to the S2 client.
 *
 * The current implementation is a placeholder; actuator write results are
 * accepted but not reported further into S2 yet.
 *
 * @param result Actuator result produced by the EMS bridge implementation.
 */
void s2_on_actuator_set(ems_bridge_set_actuator_result result);



// extern void task_S2_library(void *pvParameter);
