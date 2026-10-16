<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Developer setup

This guide explains how to set up, build, flash, and configure the S2 Modbus Converter firmware for the LilyGO ESP32 Ethernet board. The converter bridges an S2-capable energy management system to a Modbus TCP or Modbus RTU appliance.

## Prerequisites

- Git
- Visual Studio Code
- PlatformIO IDE extension for Visual Studio Code
- USB driver for the board's serial adapter, if your operating system does not detect it automatically
- A LilyGO ESP32 Ethernet board with 16 MB flash
- A USB cable that supports data, not only charging

## Clone the repository

Clone the repository and open the repository root in Visual Studio Code:

```sh
git clone https://github.com/ElaadNL/resflex-open-modbus-converter.git
cd resflex-open-modbus-converter
code .
```

Visual Studio Code should detect `platformio.ini` and activate the PlatformIO project automatically. The main embedded target is:

```ini
[env:esp-wrover-kit]
```

This target is currently used for the LilyGO ESP32 Ethernet hardware because it matches the ESP32 WROVER and 16 MB flash setup used by the board.

## S2 device firmware library

The converter uses the `s2-device-firmware` library for the S2 protocol implementation. The converter-specific code lives in this repository, while the generic S2 client functionality is consumed as a PlatformIO library dependency.

In `platformio.ini`, this is configured under `lib_deps`:

```ini
lib_deps =
    bblanchon/ArduinoJson@^7.2.2
    https://github.com/ElaadNL/resflex-open-s2-device-connector.git#v1.0.0
```

For active development on both repositories, you can also use a local checkout instead of the remote dependency:

```ini
lib_deps =
    bblanchon/ArduinoJson@^7.2.2
    ../resflex-open-s2-device-connector
```

Use the local option only when both repositories are checked out next to each other. Before committing, make sure `platformio.ini` points back to the intended shared GitHub dependency unless the project explicitly wants local development paths in source control.

## Install dependencies

PlatformIO downloads the ESP32 platform, Arduino/ESP-IDF framework packages, and library dependencies during the first build. In Visual Studio Code, open the PlatformIO panel and run `Build` for `esp-wrover-kit`.

From a terminal, the equivalent command is:

```sh
pio run -e esp-wrover-kit
```

The first build can take several minutes because PlatformIO has to download the toolchain and framework packages.

## Build and flash

Connect the LilyGO ESP32 Ethernet board over USB and check that PlatformIO can see it:

```sh
pio device list
```

Build the firmware:

```sh
pio run -e esp-wrover-kit
```

Flash it to the board:

```sh
pio run -e esp-wrover-kit -t upload
```

If PlatformIO cannot select the correct serial port automatically, pass it explicitly:

```sh
pio run -e esp-wrover-kit -t upload --upload-port /dev/cu.usbserial-XXXX
```

After flashing, open the serial monitor:

```sh
pio device monitor -e esp-wrover-kit
```

The monitor speed is configured as `115200` in `platformio.ini`.

## First start

On boot, the firmware initializes persistent storage, Ethernet, WiFi in AP/STA mode, the captive portal, the S2 wrapper, and the Modbus bridge.

The converter uses two network sides:

- WiFi client mode is used for the S2 connection to the HEMS or CEM.
- Ethernet is used for the Modbus TCP connection to the appliance.

If no valid WiFi credentials are configured yet, the firmware still starts the captive portal access point so the device can be configured.

## Captive portal configuration

Join the converter access point from a laptop or phone. The SSID starts with:

```text
S2MB_
```

Most clients should open the captive portal automatically. If that does not happen, open this URL manually:

```text
http://4.3.2.1/config
```

On `/config`, configure:

- WiFi SSID and password for the network that can reach the S2 HEMS or CEM.
- S2 websocket endpoint URI.
- S2 pair key, if the HEMS/CEM pairing flow provides one.
- Optional static network settings. Leave static IP disabled to use DHCP.

Saving the form stores the values in NVS and reboots the ESP32.

## Appliance and Modbus configuration

Open the appliance page from the portal navigation, or browse directly to:

```text
http://4.3.2.1/appliance
```

On `/appliance`, paste or edit the appliance JSON template. A generic example template is stored in `device-templates/example-home-battery.json`.

The page also exposes the user-input fields required by the selected template, such as:

- appliance name, model, serial number, and firmware version
- Modbus TCP IP address and port
- Modbus gateway and netmask, where required by the template
- Modbus unit ID
- Modbus RTU channel, when using an RTU template
- number of phases

The JSON is minified before it is stored. The current storage limit is 4096 bytes after minification. Saving the form stores the template and parameters in NVS and reboots the ESP32.

## Ready to use

After configuration and reboot:

1. The ESP32 connects to the configured WiFi network for S2 communication.
2. The Ethernet interface is brought up for the Modbus appliance network.
3. The S2 wrapper starts using the `s2-device-firmware` library.
4. The Modbus mapping component translates S2 instructions and measurements to the configured Modbus device template.

At this point the converter is ready to pair with the S2 HEMS/CEM and exchange data with the Modbus appliance.

## Useful development commands

Run native unit tests:

```sh
pio test -e native
pio test -e native_captive
pio test -e native_s2
```

Run the Modbus TCP simulator for the example template for local testing:

```sh
python3 python/modbus_example_sim.py --host 127.0.0.1 --port 1502
```

Use the simulator when testing Modbus TCP behavior without a physical appliance. Point the converter or test client at the simulator host and port.

## Troubleshooting

- If the build cannot find `s2-device-firmware`, check that the dependency URL in `platformio.ini` is reachable and that your Git credentials have access.
- If flashing fails, confirm the USB cable supports data and specify the upload port manually.
- If the captive portal does not pop up automatically, connect to the `S2MB_` access point and open `http://4.3.2.1/config`.
- If the device cannot reach the S2 endpoint, verify the WiFi credentials and websocket URI on `/config`.
- If Modbus TCP does not connect, verify the Ethernet cable, appliance IP, port, and unit ID on `/appliance`.
