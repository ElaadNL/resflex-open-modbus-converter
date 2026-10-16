<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# LED Component

## Description: functionality of the led_component

This component manages all LED states and reflects xQueue commands through LED color output.

## Functionality

A message on the xQueue will change this converter LED's color.

## Interfacing

Input: A message of type led_state_state_t on the LED's xQueue bus:

`struct led_set_state_t { led_state led_state_value; };`

The led_state defines what the LED should show:

```cpp
enum led_state {
    LED_STATE_BOOT,
    LED_STATE_WAITING_FOR_WIFI,
    LED_STATE_WAITING_FOR_ETHERNET,
    LED_STATE_WAITING_FOR_DEVICE,
    LED_STATE_ALL_OK,
    LED_STATE_WARNING,
    LED_STATE_FATAL_ERROR
};
```

Which corresponds to the following LED states:

- `rgb(200,200,200)`\[White\] Converter is booting
- `rgb(0,0,200)`\[Blue\] Converter is waiting for the WiFi interface
- `rgb(200,0,200)`\[Magenta\] Converter is waiting for the Ethernet interface
- `rgb(0,200,200)`\[Cyan\] Converter is waiting for Modbus device
- `rgb(0,200,0)`\[Green\] Converter is all OK
- `rgb(200,100,0)`\[Orange\] Converter has a warning
- `rgb(200,0,0)`\[Red\] Converter has a fatal error

## External dependencies

Adafruit NeoPixel Library (Arduino), from <https://github.com/adafruit/Adafruit_NeoPixel>. Pinned in `platformio.ini` as `adafruit/Adafruit NeoPixel@^1.15.4`.

## Architecture

Queue-driven, a message on the xQueue will activate and/or change the LED component

## Testing

### Testcases

## References

Datasheet WS2812 LED: <a href="https://cdn-shop.adafruit.com/datasheets/WS2812.pdf">https://cdn-shop.adafruit.com/datasheets/WS2812.pdf</a>. The LED is connected at PIN-IO4 (DATA) and operates at 3.3V
