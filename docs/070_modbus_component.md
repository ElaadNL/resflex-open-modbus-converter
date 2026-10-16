<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Modbus Component

## Status

Draft

---

## Overview

The Modbus Component is responsible for establishing, maintaining, and executing Modbus communication with a single configured device.

It provides a transport-independent interface for Modbus communication, supporting both RS485 (RTU) and TCP/IP connections.

The component executes requests received from the Modbus Mapping Component and returns responses in a structured and reliable manner.

---

## Design Philosophy

- Clear separation between communication and mapping logic
- Transport abstraction (RS485 and TCP/IP)
- No own task for monitoring / pulling --\> should be done by component's user
- Robust handling of communication errors

---

## Responsibilities

- Initialize and manage Modbus communication
- Support RS485 (RTU) and TCP/IP transport modes
- Execute Modbus read/write requests
- Manage timeouts and retries
- Provide responses to requesting components

---

## Non-Responsibilities

The component explicitly does NOT handle:

- Mapping between S2 and Modbus (handled by Modbus Mapping Component)
- S2 protocol logic (handled by the S2 component)
- Network management (handled by Network Component)
- Device-specific logic (handled by Modbus Mapping Component)
- Configuration UI logic (handled by HTTP webpage component)

---

## Architecture

### Connected Components

#### Modbus Mapping Component

- Sends Modbus requests
- Receives responses
- Interprets data

#### Storage Component

- Provides configuration for:
  - Transport type (RTU/TCP)
  - Serial settings (baudrate, parity, etc.)
  - IP address, gateway, mask, and port (for TCP/IP)
  - Modbus slave address

---

## Transport Modes

### RS485 (Modbus RTU)

- Serial communication over RS485
- Configurable parameters:
  - Baud rate
  - Parity
  - Stop bits
- Requires proper timing and bus control

---

### Modbus TCP/IP

- Communication over Ethernet
- Uses IP address and port (default: 502)
- Relies on Network Component for connectivity

---

## Configuration

Configuration is loaded from the selected S2 Template and Storage Component.

---

## Functions

```cpp
bool modbus_component_init(const appliance_config_t& config);
```

- Initializes the Modbus connection from the appliance configuration loaded from the device template JSON.
  - For TCP: Attempts to open a socket to the target IP address and binds it to the specified network interface.
  - For RTU: Configures the UART hardware and pins for RS485 half-duplex mode.
- Ensure the target device configuration is fully populated before calling.
- Return value:
  - true if the connection was established (TCP) or hardware initialized (RTU).
  - false if the initialization failed.

```cpp
bool modbus_component_read_holding_register(uint16_t address, uint16_t *value);
bool modbus_component_read_holding_register(uint16_t address, uint32_t *value);
```

- Reads a single 16-bit or 32-bit Holding Register (Function Code 0x03). Typically used to read configuration parameters or writable settings from the device. The 32-bit overload reads two consecutive registers starting at the given address.
- Parameters:
  - address The 0-based memory address of the register to read.
  - value Pointer to the variable where the retrieved 16-bit or 32-bit value will be stored.
- Return value:
  - true if the transaction was successful and the value is valid.
  - false if a timeout, network error, or Modbus Exception occurred.

```cpp
bool modbus_component_read_input_register(uint16_t address, uint16_t *value);
bool modbus_component_read_input_register(uint16_t address, uint32_t *value);
```

- Reads a single 16-bit or 32-bit Input Register (Function Code 0x04). Typically used to read live sensor data, metrics, or read-only statuses. The 32-bit overload reads two consecutive registers starting at the given address.
- Parameters:
  - address The 0-based memory address of the register to read.
  - value Pointer to the variable where the retrieved 16-bit or 32-bit value will be stored.
- Return value:
  - true if the transaction was successful and the value is valid.
  - false if a timeout, network error, or Modbus Exception occurred.

```cpp
bool modbus_component_read_holding_register_string(uint16_t address, uint16_t register_count, char* value, size_t max_len, bool big_endian);
bool modbus_component_read_input_register_string(uint16_t address, uint16_t register_count, char* value, size_t max_len, bool big_endian);
```

- Reads multiple Holding Registers (Function Code 0x03) or Input Registers (Function Code 0x04) and decodes them as an ASCII string. Used for appliance identity fields such as model, serial number, or firmware version sourced from Modbus.
- Parameters:
  - address The 0-based memory address of the first register.
  - register_count Number of 16-bit registers to read.
  - value Destination buffer for the null-terminated string.
  - max_len Size of the destination buffer.
  - big_endian true for high-byte/low-byte character order per register, false for swapped order.
- Return value:
  - true if the transaction was successful and the string was decoded.
  - false if a timeout, network error, Modbus Exception, or invalid argument occurred.

```cpp
bool modbus_component_write_register_16(uint16_t address, uint16_t value);
```

- Writes a value to a single 16-bit Holding Register (Function Code 0x06).
- Parameters:
  - address The 0-based memory address of the register to write to.
  - value The 16-bit value to write to the device.
- Return value
  - true if the device successfully acknowledged the write command.
  - false if a timeout, network error, or Modbus Exception occurred.

```cpp
bool modbus_component_write_register_32(uint16_t address, uint32_t value, bool big_endian);
```

- Writes a 32-bit value to two consecutive Holding Registers (Function Code 0x10).
- Parameters:
  - address The 0-based memory address of the first register to write to.
  - value The 32-bit value to write to the device.
  - big_endian true to write the high word first, false to write the low word first.
- Return value
  - true if the device successfully acknowledged the write command.
  - false if a timeout, network error, or Modbus Exception occurred.

```cpp
bool modbus_component_read_coil(uint16_t address, bool *value);
```

- Reads the status of a single Coil (Function Code 0x01). Coils are discrete, 1-bit boolean values (ON/OFF).
- Parameters:
  - address The 0-based memory address of the coil.
  - value Pointer to the boolean variable where the status (true = ON, false = OFF) will be stored.
- Return value
  - true if the transaction was successful.
  - false if a timeout, network error, or Modbus Exception occurred.

```cpp
bool modbus_component_write_coil(uint16_t address, bool value);
```

- Writes a boolean status to a single Coil (Function Code 0x05).
- Parameters:
  - address The 0-based memory address of the coil.
  - value The boolean state to write (true = ON/0xFF00, false = OFF/0x0000).
- Return value
  - true if the device successfully acknowledged the write command.
  - false if a timeout, network error, or Modbus Exception occurred.

---

## Connection Management

### RS485

- No persistent “connection”
- Bus is used on-demand
- Timing must be respected

---

### TCP/IP

- Maintain connection to device
- Detect disconnects
- Attempt reconnection when needed

---

## Error Handling

- Detect communication errors
- Detect timeouts
- Return structured error responses

---

## Limitations

- Supports a single device connection
- Dependent on network stability (for TCP)
- Requires correct configuration

---
