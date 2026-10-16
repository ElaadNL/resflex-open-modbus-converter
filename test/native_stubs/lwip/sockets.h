// SPDX-FileCopyrightText: ElaadNL
//
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Native-test stub for lwip/sockets.h.
//
// modbus_component.cpp builds real Modbus TCP request frames and validates
// response parsing against a socket. On the host we pull in the real POSIX
// headers (struct sockaddr_in, struct timeval, struct ifreq, and the
// AF_/SO_/SOL_ constants, plus inet_addr()/htons() all come from there) and
// only override the six transport functions the component actually calls —
// socket, connect, send, recv, close, setsockopt — using symbol
// interposition so the test controls the request/response bytes.
// The overrides are defined in transport_stubs.cpp under extern "C" so the
// C symbols match the ones modbus_component.cpp calls through sys/socket.h.

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <vector>

// --- Native-test socket transport state ---

namespace modbus_test_tcp {
extern std::vector<std::vector<unsigned char>> requests;   // recorded send() payloads
extern std::vector<std::vector<unsigned char>> responses;  // FIFO of scripted recv() payloads
extern int recv_return_override;                           // returned by recv() instead of the next payload
extern bool recv_return_override_set;                      // enables recv_return_override
extern int connect_result;                                 // return value of connect()
extern int close_call_count;

void reset();
}
