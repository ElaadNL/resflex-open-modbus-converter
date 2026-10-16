// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <stdbool.h>

/**
 * @file captive_portal_dns.h
 * @brief Wildcard DNS responder lifecycle for the captive portal.
 *
 * Runs a tiny UDP/53 task that answers every query with an A-record
 * pointing at the AP IP, so client OS captive-portal probes are routed
 * to our HTTP server. The actual DNS message construction lives in
 * captive_portal_dns_message.h so it can be unit-tested without lwIP.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the wildcard DNS responder task on core 0.
 *
 * Spawns a FreeRTOS task that opens a UDP/53 socket and answers each query
 * with a wildcard A-record. Idempotent in practice — if the task is already
 * running this returns true after a no-op spawn attempt; the task creation
 * call itself will fail safely in that case.
 *
 * @return true if xTaskCreatePinnedToCore succeeded, false otherwise.
 */
bool captive_portal_dns_start(void);

/**
 * @brief Request shutdown of the DNS responder task.
 *
 * Sets the run flag to false; the task self-deletes on its next recv
 * timeout (≤ 1s). Returns immediately — does not block.
 */
void captive_portal_dns_stop(void);

#ifdef __cplusplus
}
#endif
