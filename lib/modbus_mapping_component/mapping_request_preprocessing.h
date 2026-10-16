// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdint.h>
#include <ems_bridge.h>
#include <appliance_config.h>
#include <modbus_appliance_definition.h>

/**
 * @brief Translates an EMS power setpoint into a current setpoint when the
 *        active device template only exposes current actuators for that phase.
 *
 * Rationale: EMS issues power instructions (watt), but many appliances only
 * accept current setpoints (ampere). When the template lacks the matching
 * SETPOINT_*_POWER_Lx/PHASE_SYMMETRIC actuator and does provide the equivalent
 * SETPOINT_*_CURRENT_Lx/PHASE_SYMMETRIC actuator, the request is converted using
 * the most recent valid ACTUAL_VOLTAGE_Lx measurement from the sensor cache,
 * falling back to appliance.control.fallbackVoltage when no fresh voltage is
 * available.
 *
 * Conversion formulae (per-phase semantics; matches EMS 3_PHASE_SYMMETRIC):
 *   SETPOINT_*_POWER_L1              -> current_L1  = power_L1  / voltage_L1
 *   SETPOINT_*_POWER_L2              -> current_L2  = power_L2  / voltage_L2
 *   SETPOINT_*_POWER_L3              -> current_L3  = power_L3  / voltage_L3
 *   SETPOINT_*_POWER_PHASE_SYMMETRIC -> current     = power     / voltage_L1
 *
 * The phase-symmetric setpoint carries a per-phase value that applies equally to
 * all phases, so it converts like a single phase using voltage_L1 by convention.
 * If the voltage is 0 V (no measurement and fallbackVoltage = 0), the derived
 * current setpoint is also 0 A — the safe default.
 *
 * When the template already exposes the requested power actuator, or when the
 * request is not a SETPOINT_*_POWER_* actuator, the request is returned
 * unchanged.
 *
 * @param request Original actuator request as produced by the EMS bridge.
 * @param definition Active appliance definition (used to query which actuators the template exposes).
 * @param config Resolved appliance configuration (provides fallback_voltage).
 * @param now_ms Current monotonic time in milliseconds; used to evaluate the validity of cached voltage measurements.
 * @return Possibly-rewritten actuator request, ready to be queued for Modbus.
 */
ems_bridge_set_actuator_request mapping_request_preprocessing(
    ems_bridge_set_actuator_request request,
    const modbus_appliance_definition &definition,
    const appliance_config_t &config,
    uint32_t now_ms);