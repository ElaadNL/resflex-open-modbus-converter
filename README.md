<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# S2 Modbus Converter

ESP32 firmware that makes existing Modbus devices controllable through the
[S2 protocol](https://docs.s2standard.org/). The converter sits between an S2
Customer Energy Manager (CEM), such as a Home Energy Management System (HEMS),
and a device without an S2 interface, and talks to that device over Modbus TCP
or Modbus RTU (RS485). It also implements the timing behaviour S2 expects on top
of devices that were never designed for it.

The converter uses the
[`resflex-open-s2-device-connector`](https://github.com/ElaadNL/resflex-open-s2-device-connector)
library for S2 and supports the Power Envelope Based Control (PEBC) control type.

## Disclaimer

This software is an open reference implementation, developed under the
coordination of ElaadNL as part of the
[Residential Flexibility](https://elaad.nl/en/projects/residential-flexibility/) initiative. It is not a finished,
certified, or end-user product. Before publication, the code was reviewed for
security by the European Network for Cyber Security (ENCS); that review is a
snapshot in time, not a guarantee. Anyone who integrates this software into a
product does so at their own risk and is responsible for its secure integration,
ongoing security testing, regulatory compliance, and any certification of that
product. The software is provided "as is", without warranty, under the Apache
License 2.0.

Read the full [disclaimer](DISCLAIMER.md) before using this software.

## Features

- S2 Resource Manager over WebSocket, including S2 Connect pairing
- Modbus TCP (on Ethernet) and Modbus RTU over RS485
- Template-based device mapping: a JSON template per device type describes which
  registers to read and write, without changing the firmware
- Captive portal for local configuration over the device's WiFi access point
- A generic example template in [`device-templates/`](device-templates/) and a
  matching Modbus TCP simulator in [`python/`](python/)

## Hardware

The reference target is a LilyGO ETH board with an ESP32-WROVER and 16 MB flash
(`esp-wrover-kit` environment in [`platformio.ini`](platformio.ini)). The S2
interface is bound to WiFi; Modbus TCP is bound to Ethernet by default.

## Architecture

The firmware runs separate FreeRTOS tasks that exchange work through queues. The
main modules are:

- **S2 library wrapper**: wraps the S2 device library and exposes functions and
  callbacks to the rest of the converter.
- **EMS bridge**: a protocol-neutral layer between device control logic and the
  energy-management protocol (currently S2 PEBC).
- **Modbus mapping**: translates requests from the S2 side into the desired
  device behaviour, sends Modbus commands, and processes responses. All S2-related
  device logic lives here.
- **Modbus**: the communication layer with the actual device, kept as simple as
  possible.

The C4 architecture diagram is available as
[`S2 Modbus Converter C4 Architecture.drawio`](S2%20Modbus%20Converter%20C4%20Architecture.drawio).
Each component is described in [`docs/`](docs/000_home.md).

## Getting started

See [`DEVELOPER_SETUP.md`](DEVELOPER_SETUP.md) for the toolchain, build, flash,
and test instructions. In short, with [PlatformIO](https://platformio.org/)
installed:

```sh
git clone https://github.com/ElaadNL/resflex-open-modbus-converter.git
cd resflex-open-modbus-converter
pio run -e esp-wrover-kit
```

## Device templates

A device template describes how the converter maps S2 sensors and actuators onto
the Modbus registers of a specific device. The format is documented in
[`docs/010_modbus_mapping_template.md`](docs/010_modbus_mapping_template.md).

The repository contains one generic example,
[`example-home-battery.json`](device-templates/example-home-battery.json), with
a fictional register map that does not belong to any real product. Use it as a
starting point for a template for your own device, based on that device's
Modbus documentation. The simulator in
[`python/modbus_example_sim.py`](python/modbus_example_sim.py) implements the
same register map, so the converter can be tested without hardware.

## Contributing

Contributions are welcome. See [`CONTRIBUTING.md`](CONTRIBUTING.md) for the
workflow and coding conventions, [`SUPPORT.md`](SUPPORT.md) for questions, and
[`SECURITY.md`](SECURITY.md) for reporting vulnerabilities.

## License

This project is licensed under the [Apache License 2.0](LICENSE).
