<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/).

---

## [1.0.0] – 2026-10-19

Initial public release, developed under the ElaadNL
[Residential Flexibility](https://elaad.nl/en/projects/residential-flexibility/)
initiative.

### Added

- ESP32 firmware that exposes Modbus devices as S2 Resource Managers (PEBC)
- Modbus TCP and Modbus RTU (RS485) transports
- JSON device templates mapping S2 sensors and actuators to Modbus registers
- Generic example device template with a matching Modbus TCP simulator
- Captive portal for local configuration over WiFi
- Native unit tests and architecture documentation
