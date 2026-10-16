<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Storage Component

## Overview

The Storage component is responsible for the persistent storage of configuration parameters and S2 templates. It provides a simple and reliable interface for reading and writing structured data to non-volatile memory.

The module abstracts the underlying storage and ensures that configuration data is consistently stored and retrieved across system restarts.

---

## Design Philosophy

- Simple and predictable behavior
- No business logic or data interpretation
- Structured storage for large files using JSON
- Key-value pairs for parameters
- Separation of configuration domains
- Synchronous access (blocking, but controlled)

---

## Responsibilities**

- Initialize and manage the filesystem (SPIFFS / NVS)
- Store and retrieve configuration data
- Provide structured access to configuration domains
- Ensure data persistence across reboots
- Handle basic file integrity

---

## Non-Responsibilities

The module explicitly does NOT handle:

- Validation of configuration content
- Interpretation of configuration data
- Network configuration logic
- S2 or Modbus logic
- Runtime decision making

These responsibilities belong to higher-level modules such as the Resource Manager or Network module.

---

## Storage Structure

The active code path stores everything in NVS:

- System configuration (key-value pairs)
- The appliance JSON template, under NVS key `device_template`

SPIFFS is still mounted by `init_storage_component()` and the `storage_load_s2_template` / `storage_save_s2_template` helpers exist, but no component calls them. They are kept available for future file-based template storage.

---

## Data Format

- All S2 template data is stored as **JSON**
- **All system configuration is stored as key-value pairs compliant with ESP-IDF's API**
- Human-readable and editable
- Compatible with S2 message structures

---

## Public API

### Initialization

`bool init_storage_component();`

Mounts SPIFFS and initializes the default NVS partition. Sets `LED_STATE_FATAL_ERROR` on either failure.

Return values:

- `true`: storage component has initialized successfully
- `false`: otherwise

---

### Load S2 Template

**Loads a specific S2 template from the file system and stores the data in the `output` variable with max length `max_len`.**

`bool storage_load_s2_template(const char* path, char * output, size_t max_len);`

Return values:

- `true`: loading a template was successful
- `false`: otherwise

---

### Save S2 Template

**Saves a specific S2 `template_data` into the file system with the given `path`.**

`bool storage_save_s2_template(const char* path, const char * template_data);`

Return values:

- `true`: saving a template was successful
- `false`: otherwise

---

### Configuration set

Sets a specific key-value in the NVS environment of the ESP32 with given `key` and `value`. NVS keys are limited to 15 characters by ESP-IDF.

`bool storage_set_configuration(const char * key, uint16_t value);`

`bool storage_set_configuration(const char * key, uint32_t value);`

`bool storage_set_configuration(const char * key, const char * value);`

The string variant skips the write when the stored value already equals `value`, to reduce flash wear.

Return values:

- `true`: storage was successful
- `false`: otherwise

---

### Configuration get

Gets a specific key-value in the NVS environment of the ESP32 with given `key` and `value`. If the key is not present, the default is written back to NVS so subsequent reads return it directly.

`bool storage_get_configuration(const char * key, uint16_t &value, uint16_t default_value);`

`bool storage_get_configuration(const char * key, uint32_t &value, uint32_t default_value);`

`bool storage_get_configuration(const char * key, char * value, size_t max_len, const char * default_value);`

For the string variant, passing `nullptr` as `default_value` returns an empty string without persisting a default.

Return values:

- `true`: storage was successful
- `false`: otherwise

---

## Access Pattern

- Modules request configuration data when needed
- Data is loaded into memory and used locally
- Updates are explicitly written back to storage

---

## Error Handling

- Return status for all operations (true/false)
- Log failures (e.g. mount failure, read/write errors)
- No automatic retries or recovery logic

---

## Logging

No logging.

---

## Limitations

- Blocking operations (no async handling)
- No built-in data validation
- No versioning or migration support

---

## Best Practices

- Keep configuration files small
- Avoid frequent writes (flash wear)
- Validate data in higher-level modules
- Use consistent JSON structure per domain

---

## Summary

The Storage module provides:

- Persistent configuration storage
- Simple and structured data access
- Clear separation from application logic

It serves as the foundation for all system configuration and S2 template storage.
