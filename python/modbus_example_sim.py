#!/usr/bin/env python3
# SPDX-FileCopyrightText: ElaadNL
#
# SPDX-License-Identifier: Apache-2.0

"""Small dependency-free Modbus TCP simulator for the example home battery template.

Usage:
    python3 python/modbus_example_sim.py
    python3 python/modbus_example_sim.py --host 127.0.0.1 --port 1502 --unit-id 1
    python3 python/modbus_example_sim.py --power -250

Run this from the repository root, then point a Modbus TCP client (or the
converter) at the printed host/port. Port 502 is the default Modbus TCP port and
may require elevated privileges on some systems; use --port 1502 for local
testing.

The simulator implements the fictional register map of
device-templates/example-home-battery.json:

    100  ACTUAL_POWER_L1 / ACTUAL_POWER_TOTAL  INT16, W, positive = charging
    101  ACTUAL_VOLTAGE_L1                     UINT16, 0.1 V
    102  STATUS                                UINT16, 0 = OK, 1 = error
    200  SETPOINT_MAX_POWER_L1                 UINT16, W, maximum charge power
    201  SETPOINT_MIN_POWER_L1                 UINT16, W, maximum discharge power

The measured power follows a slow wave around a base value and is clamped to
the limits written to registers 200 and 201. Every read and write is printed to
the console.
"""

import argparse
import asyncio
import contextlib
import math
import struct
import time
from typing import Iterable, List


DEFAULT_UNIT_ID = 1
DEFAULT_HOST = "0.0.0.0"
DEFAULT_PORT = 502


class SensorModel:
    DEFAULT_BASE_POWER_W = 500
    DEFAULT_WAVE_POWER_W = 35
    DEFAULT_VOLTAGE_V = 230.4

    def __init__(self, base_power_w=DEFAULT_BASE_POWER_W, wave_power_w=DEFAULT_WAVE_POWER_W, voltage_v=DEFAULT_VOLTAGE_V):
        self.base_power_w = base_power_w
        self.wave_power_w = wave_power_w
        self.voltage_v = voltage_v


class ExampleBatteryRegisterMap:
    """Register map of device-templates/example-home-battery.json."""

    POWER_ADDRESS = 100
    VOLTAGE_ADDRESS = 101
    STATUS_ADDRESS = 102
    MAX_POWER_ADDRESS = 200
    MIN_POWER_ADDRESS = 201
    ACTUATORS = {
        MAX_POWER_ADDRESS: "SETPOINT_MAX_POWER_L1",
        MIN_POWER_ADDRESS: "SETPOINT_MIN_POWER_L1",
    }
    DEFAULT_LIMIT_W = 2000

    def __init__(self, model: SensorModel) -> None:
        self.model = model
        self.started_at = time.monotonic()
        self.holding_registers = {
            self.MAX_POWER_ADDRESS: self.DEFAULT_LIMIT_W,
            self.MIN_POWER_ADDRESS: self.DEFAULT_LIMIT_W,
        }
        self.measurements = {}

    def tick(self) -> None:
        elapsed = time.monotonic() - self.started_at
        power = self.model.base_power_w + round(math.sin(elapsed / 8.0) * self.model.wave_power_w)
        max_charge = self.holding_registers[self.MAX_POWER_ADDRESS]
        max_discharge = self.holding_registers[self.MIN_POWER_ADDRESS]
        power = max(-max_discharge, min(max_charge, power))

        self.measurements = {
            self.POWER_ADDRESS: self._to_u16(power),
            self.VOLTAGE_ADDRESS: round(self.model.voltage_v * 10),
            self.STATUS_ADDRESS: 0,
        }

    def read_registers(self, function_code: int, address: int, count: int) -> List[int]:
        if function_code not in (0x03, 0x04):
            raise ValueError(f"Unsupported read function 0x{function_code:02X}")
        self.tick()
        # Measurements and setpoints are readable as both holding (FC03) and input (FC04) registers.
        source = {**self.measurements, **self.holding_registers}
        return [source.get(address + offset, 0) & 0xFFFF for offset in range(count)]

    def write_single_register(self, address: int, value: int) -> None:
        self.holding_registers[address] = value & 0xFFFF

    def write_multiple_registers(self, address: int, values: Iterable[int]) -> None:
        for offset, value in enumerate(values):
            self.write_single_register(address + offset, value)

    def describe_write(self, address: int, value: int) -> str:
        if address == self.MAX_POWER_ADDRESS:
            return f"{self.ACTUATORS[address]} set maximum charge power to {value} W"
        if address == self.MIN_POWER_ADDRESS:
            return f"{self.ACTUATORS[address]} set maximum discharge power to {value} W"
        return "write holding register"

    def actuator_state(self) -> str:
        max_power = self.holding_registers.get(self.MAX_POWER_ADDRESS, 0)
        min_power = self.holding_registers.get(self.MIN_POWER_ADDRESS, 0)
        return f"actuators: max charge={max_power} W, max discharge={min_power} W"

    @staticmethod
    def _to_u16(value: int) -> int:
        return value & 0xFFFF


class ModbusTcpServer:
    def __init__(self, host: str, port: int, unit_id: int, register_map: ExampleBatteryRegisterMap) -> None:
        self.host = host
        self.port = port
        self.unit_id = unit_id
        self.register_map = register_map

    async def start(self) -> None:
        server = await asyncio.start_server(self.handle_client, self.host, self.port)
        sockets = ", ".join(str(sock.getsockname()) for sock in server.sockets or [])
        print(f"Example home battery Modbus TCP simulator listening on {sockets}")
        print("Registers: power=100, voltage=101, status=102")
        print("Writes: SETPOINT_MAX_POWER_L1=200, SETPOINT_MIN_POWER_L1=201")

        try:
            while True:
                await asyncio.sleep(3600)
        finally:
            server.close()
            await server.wait_closed()

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        peer = writer.get_extra_info("peername")
        print(f"[+] Client connected: {peer}")
        try:
            while True:
                header = await reader.readexactly(7)
                transaction_id, protocol_id, length, unit_id = struct.unpack(">HHHB", header)
                pdu = await reader.readexactly(length - 1)
                response_pdu = self.handle_pdu(unit_id, pdu)
                response = struct.pack(">HHHB", transaction_id, protocol_id, len(response_pdu) + 1, unit_id)
                writer.write(response + response_pdu)
                await writer.drain()
        except asyncio.IncompleteReadError:
            pass
        except Exception as exc:  # Keep the simulator alive while surfacing malformed requests.
            print(f"[!] Client {peer} error: {exc}")
        finally:
            writer.close()
            with contextlib.suppress(Exception):
                await writer.wait_closed()
            print(f"[-] Client disconnected: {peer}")

    def handle_pdu(self, unit_id: int, pdu: bytes) -> bytes:
        if not pdu:
            return self.exception_response(0, 0x03)

        function_code = pdu[0]
        if unit_id not in (self.unit_id, 0xFF):
            return self.exception_response(function_code, 0x0B)

        if function_code in (0x03, 0x04):
            return self.handle_read(function_code, pdu)
        if function_code == 0x06:
            return self.handle_write_single(function_code, pdu)
        if function_code == 0x10:
            return self.handle_write_multiple(function_code, pdu)

        return self.exception_response(function_code, 0x01)

    def handle_read(self, function_code: int, pdu: bytes) -> bytes:
        if len(pdu) != 5:
            return self.exception_response(function_code, 0x03)
        address, count = struct.unpack(">HH", pdu[1:5])
        if count < 1 or count > 125:
            return self.exception_response(function_code, 0x03)

        values = self.register_map.read_registers(function_code, address, count)
        payload = b"".join(struct.pack(">H", value) for value in values)
        print(f"READ FC{function_code:02d} address={address} count={count} values={values}")
        return bytes([function_code, len(payload)]) + payload

    def handle_write_single(self, function_code: int, pdu: bytes) -> bytes:
        if len(pdu) != 5:
            return self.exception_response(function_code, 0x03)
        address, value = struct.unpack(">HH", pdu[1:5])
        self.register_map.write_single_register(address, value)
        print(f"WRITE FC06 address={address} value={value} ({self.register_map.describe_write(address, value)})")
        if address in self.register_map.ACTUATORS:
            print(f"      {self.register_map.actuator_state()}")
        return pdu

    def handle_write_multiple(self, function_code: int, pdu: bytes) -> bytes:
        if len(pdu) < 6:
            return self.exception_response(function_code, 0x03)
        address, count, byte_count = struct.unpack(">HHB", pdu[1:6])
        if count < 1 or byte_count != count * 2 or len(pdu) != 6 + byte_count:
            return self.exception_response(function_code, 0x03)

        values = [struct.unpack(">H", pdu[6 + offset : 8 + offset])[0] for offset in range(0, byte_count, 2)]
        self.register_map.write_multiple_registers(address, values)
        print(f"WRITE FC16 address={address} count={count} values={values}")
        if any(reg in self.register_map.ACTUATORS for reg in range(address, address + count)):
            print(f"      {self.register_map.actuator_state()}")
        return struct.pack(">BHH", function_code, address, count)

    @staticmethod
    def exception_response(function_code: int, exception_code: int) -> bytes:
        return bytes([function_code | 0x80, exception_code])


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Dependency-free Modbus TCP simulator for the example home battery template")
    parser.add_argument("--host", default=DEFAULT_HOST, help=f"Bind address, default {DEFAULT_HOST}")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help=f"TCP port, default {DEFAULT_PORT}")
    parser.add_argument("--unit-id", type=int, default=DEFAULT_UNIT_ID, help=f"Modbus unit id, default {DEFAULT_UNIT_ID}")
    parser.add_argument("--power", type=int, default=SensorModel.DEFAULT_BASE_POWER_W, help="Initial power in W (positive = charging)")
    return parser.parse_args()


async def main(args: argparse.Namespace) -> None:
    register_map = ExampleBatteryRegisterMap(SensorModel(base_power_w=args.power))
    server = ModbusTcpServer(args.host, args.port, args.unit_id, register_map)
    await server.start()


if __name__ == "__main__":
    try:
        asyncio.run(main(parse_args()))
    except KeyboardInterrupt:
        print("\nSimulator stopped.")
