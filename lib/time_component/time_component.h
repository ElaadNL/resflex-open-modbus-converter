// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

/**
 * @brief Synchronizes the system clock using SNTP.
 *
 * Starts the SNTP client, waits for the system time to be updated by an NTP
 * server, and configures the local timezone for the Netherlands (CET/CEST).
 * This should be called after the network interface is connected, because NTP
 * requires network access. A valid system clock is important for TLS
 * certificate validation and time-based protocol logic.
 *
 * @return true if the system time was synchronized successfully, false if the
 *         timeout expired before SNTP completed.
 */
extern bool time_component_obtain_time(void);
