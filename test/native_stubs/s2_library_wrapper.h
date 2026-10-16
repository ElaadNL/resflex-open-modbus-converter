// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <net/if.h>

#include "esp_err.h"
#include "ems_bridge.h"
#include "definitions.h"

#define NVS_KEY_S2_ACCESS_TOKEN "s2_access_tkn"
#define S2_ACCESS_TOKEN_MAX_LEN 64

struct s2_hems_values {
    char base_url[128];
    char interface[IF_NAMESIZE];
    char pairing_input_token[S2_ACCESS_TOKEN_MAX_LEN];
    bool tls_enabled;
    char cert_pem[CAPTIVE_TLS_PEM_BUF];
};

esp_err_t s2_init(
    s2_hems_values* hems_values,
    ems_bridge_setup_values* appliance_config,
    ems_bridge_request_sensor_cb_t request_sensor_cb,
    ems_bridge_set_actuator_cb_t set_actuator_cb,
    ems_bridge_on_init_ready_cb_t on_init_ready_cb
);

void s2_on_new_sensor_value(ems_bridge_sensor_result);
void s2_on_actuator_set(ems_bridge_set_actuator_result);
