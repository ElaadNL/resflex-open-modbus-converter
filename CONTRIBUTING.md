<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Contributing Guide

Thank you for your interest in contributing to the S2 Modbus Converter.

This repository contains firmware, configuration templates, tests, and
documentation for a converter that bridges S2-capable energy management systems
to appliances that expose Modbus TCP or Modbus RTU interfaces.

The project is still evolving. This guide is a first edition and will be updated
as the contribution process matures.

## Ways to contribute

Useful contributions include:

- Improving the device template format and the generic example template.
- Extending S2 support beyond the current PEBC-oriented functionality.
- Improving the Modbus mapping logic, value conversion, validation, or error
  handling.
- Improving captive portal configuration, storage, WiFi/Ethernet behavior, or
  developer tooling.
- Adding tests for native mapping logic, captive portal parsing, S2 wrapper
  behavior, and regression cases.
- Improving documentation for setup, device integration, architecture, and
  troubleshooting.
- Reporting bugs with clear reproduction steps and logs.

## Device templates

Device templates describe how a specific appliance is represented to the
converter and how S2 measurements or commands map to Modbus registers. This
repository contains one generic example,
`device-templates/example-home-battery.json`, with a fictional register map.
Templates for specific products are not included in this repository.

When creating a template for a specific device, document as much of the
following as possible:

- Manufacturer, model, firmware version, and source of the Modbus register
  information.
- Whether the appliance uses Modbus TCP, Modbus RTU, or both.
- Required connection settings such as IP address, port, unit ID, baud rate, or
  RTU channel.
- Sensor mappings with function code, register address, data type, byte order,
  multiplier, validity duration, and output mapping where relevant.
- Actuator mappings with function code, register address, data type, multiplier,
  step limits, minimum step time, and required pre/post sequences.
- Any initialization sequence required before normal operation.
- Notes about tested hardware and known limitations.

Prefer `USER_INPUT` fields for values that are installation-specific, such as
IP address, unit ID, serial number, or firmware version. Prefer
`TEMPLATE_DEFINED` fields only when the value is truly fixed for the device
model.

Test a template against real hardware before use. The example template can be
tested with the simulator in `python/modbus_example_sim.py`.

## S2 behavior contributions

The converter currently focuses on PEBC-style behavior. Contributions that add
or improve other S2 capabilities are welcome, but they should be introduced with
care because they may affect interoperability with HEMS/CEM implementations.

For S2-related changes, please describe:

- Which S2 feature, command, role, or data element is being added or changed.
- Whether the change belongs in this converter repository or in the shared
  `s2-device-firmware` library.
- How the behavior maps to Modbus sensors or actuators.
- What happens when a configured appliance cannot support the requested S2
  behavior.
- Which tests or manual validation were performed.

Changes to the generic S2 protocol implementation should normally be made in
`s2-device-firmware`. This repository should contain converter-specific logic:
wrapping the S2 library, translating S2 behavior to Modbus, and handling device
configuration.

## Firmware contributions

The main embedded target is defined in `platformio.ini` as:

```ini
[env:esp-wrover-kit]
```

Before changing firmware behavior, read the relevant component boundaries:

- `lib/s2_library_wrapper/` wraps the shared S2 library for this converter.
- `lib/modbus_mapping_component/` translates S2 concepts to configured Modbus
  sensors and actuators.
- `lib/modbus_component/` handles Modbus communication.
- `lib/ems_bridge/` connects the S2 side to the mapping layer.
- `lib/eth_wifi_component/` initializes WiFi, Ethernet, and routing behavior.
- `lib/captive_portal_component/` provides first-boot and device configuration.
- `lib/storage_component/` handles persistent NVS/SPIFFS storage.

Keep changes scoped to the component that owns the behavior. If a change crosses
components, explain the interaction in the pull request.

## Coding conventions

- Use `snake_case` for C, C++, header, and test filenames.
- Prefer ESP-IDF APIs and ESP logging for embedded code.
- Keep Arduino dependencies optional and limited to places where ESP-IDF does
  not provide a practical alternative.
- Keep Modbus communication code simple; S2-specific decisions should stay in
  the mapping and bridge layers.
- Avoid hard-coded installation-specific credentials, IP addresses, serial
  numbers, or device secrets.
- Keep comments useful and explanatory. Do not restate the code line by line.

## Tests and validation

Run the tests that match the area you changed:

```sh
pio test -e native
pio test -e native_captive
pio test -e native_s2
```

For embedded changes, also build the ESP32 target:

```sh
pio run -e esp-wrover-kit
```

For upload or hardware validation:

```sh
pio run -e esp-wrover-kit -t upload
pio device monitor -e esp-wrover-kit
```

For Modbus TCP development without a physical appliance, use the simulator in
`python/` where appropriate:

```sh
python3 python/modbus_example_sim.py --host 127.0.0.1 --port 1502
```

If you cannot run a relevant test or hardware validation step, mention that in
the pull request.

## Pull request process

1. Fork or branch from the current main development branch.
2. Create a focused branch:

   ```sh
   git checkout -b feature/add-example-device-template
   ```

3. Make a small, reviewable change.
4. Add or update tests and documentation where appropriate.
5. Run the relevant PlatformIO tests or builds.
6. Open a pull request with a clear description of the change.

In the pull request description, include:

- What changed and why.
- Which hardware, simulator, or templates were used for validation.
- Which commands were run.
- Any known limitations or follow-up work.

## Branch naming

Suggested branch prefixes:

- `template/<topic>` for work on the template format or the example template.
- `feature/<description>` for new firmware or S2 behavior.
- `fix/<description>` for bug fixes.
- `test/<description>` for test-only changes.
- `docs/<topic>` for documentation.

## Review expectations

Pull requests are reviewed for:

- Correctness of S2 and Modbus behavior.
- Safety of actuator writes and initialization sequences.
- Compatibility with the LilyGO ESP32 Ethernet target.
- Clear component ownership and maintainable code structure.
- Adequate tests or manual validation.
- Documentation that helps the next developer understand the change.

Maintainers may ask for smaller pull requests if a change mixes unrelated
template, firmware, and documentation work.

## Reporting issues

When reporting a bug, include:

- Firmware commit or release version.
- Board type and relevant wiring or network setup.
- Appliance model and device template used.
- Captive portal settings that matter for the issue, excluding secrets.
- Serial monitor logs, preferably from boot through the failure.
- Steps to reproduce and expected behavior.

Do not include WiFi passwords, pair keys, private endpoints, serial numbers, or
other sensitive deployment data in public issues.

## Security

Please do not report security issues in public issues. Follow the process in
`SECURITY.md`.

## Questions

If you are unsure whether a contribution belongs in this repository or in the
shared `s2-device-firmware` library, open an issue or draft pull request and
describe the intended behavior. In general, generic S2 protocol behavior belongs
in `s2-device-firmware`; converter-specific Modbus mapping and device
configuration belongs here.
