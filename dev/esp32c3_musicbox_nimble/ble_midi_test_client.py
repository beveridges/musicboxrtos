#!/usr/bin/env python3
"""
BLE test client for SB1 ESP32-C3 firmware.

Features:
- Scan/connect to SB1
- Verify NUS + BLE MIDI services/chars
- Send NUS RX payload and print NUS TX notifications
- Send BLE MIDI note on/off packets and print BLE MIDI notifications
- Optional reliability loop (connect/send/disconnect cycles)
"""

from __future__ import annotations

import argparse
import asyncio
import binascii
from dataclasses import dataclass
from typing import Optional

from bleak import BleakClient, BleakScanner

NUS_SVC = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

BLE_MIDI_SVC = "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
BLE_MIDI_CHR = "7772e5db-3868-4112-a1a9-f2669d106be3"


@dataclass
class NotifyState:
    nus_tx_seen: int = 0
    midi_seen: int = 0


def parse_hex(s: str) -> bytes:
    compact = s.replace(" ", "").replace(":", "")
    if len(compact) % 2 != 0:
        raise ValueError(f"Hex string must be even length: {s}")
    return binascii.unhexlify(compact)


def midi_packet(status: int, d1: int, d2: int) -> bytes:
    # BLE MIDI header + timestamp (0) + MIDI payload
    return bytes([0x80, 0x80, status & 0xFF, d1 & 0x7F, d2 & 0x7F])


async def find_device(name_filter: str, timeout: float) -> str:
    devices = await BleakScanner.discover(timeout=timeout)
    for d in devices:
        name = d.name or ""
        if name_filter.lower() in name.lower():
            return d.address
    raise RuntimeError(f"No BLE device found matching name filter: {name_filter}")


async def verify_services(client: BleakClient) -> None:
    services = client.services
    uuids = {svc.uuid.lower() for svc in services}
    if NUS_SVC not in uuids:
        raise RuntimeError("NUS service missing")
    if BLE_MIDI_SVC not in uuids:
        raise RuntimeError("BLE MIDI service missing")

    chars = {c.uuid.lower() for svc in services for c in svc.characteristics}
    for required in (NUS_RX, NUS_TX, BLE_MIDI_CHR):
        if required not in chars:
            raise RuntimeError(f"Required characteristic missing: {required}")


async def run_once(args: argparse.Namespace) -> None:
    address = args.address
    if not address:
        address = await find_device(args.name_filter, args.scan_timeout)
        print(f"[scan] found {address} for filter '{args.name_filter}'")

    state = NotifyState()

    async with BleakClient(address, timeout=args.connect_timeout) as client:
        print("[conn] connected")
        await verify_services(client)
        print("[check] services/chars present")

        def on_nus_tx(_: int, data: bytearray) -> None:
            state.nus_tx_seen += 1
            print(f"[notify] NUS TX: {bytes(data).hex(' ')}")

        def on_midi(_: int, data: bytearray) -> None:
            state.midi_seen += 1
            print(f"[notify] BLE MIDI: {bytes(data).hex(' ')}")

        await client.start_notify(NUS_TX, on_nus_tx)
        await client.start_notify(BLE_MIDI_CHR, on_midi)
        print("[check] notifications subscribed")

        # NUS inbound test
        nus_payload = parse_hex(args.nus_hex)
        await client.write_gatt_char(NUS_RX, nus_payload, response=False)
        print(f"[write] NUS RX: {nus_payload.hex(' ')}")

        # BLE MIDI inbound test (Note On then Note Off)
        note_on = midi_packet(0x90, args.note, args.velocity)
        note_off = midi_packet(0x80, args.note, 0x00)
        await client.write_gatt_char(BLE_MIDI_CHR, note_on, response=False)
        print(f"[write] BLE MIDI Note On: {note_on.hex(' ')}")
        await asyncio.sleep(args.inter_message_delay)
        await client.write_gatt_char(BLE_MIDI_CHR, note_off, response=False)
        print(f"[write] BLE MIDI Note Off: {note_off.hex(' ')}")

        await asyncio.sleep(args.notify_wait)

        await client.stop_notify(NUS_TX)
        await client.stop_notify(BLE_MIDI_CHR)
        print(
            f"[result] notifications seen: NUS_TX={state.nus_tx_seen} BLE_MIDI={state.midi_seen}"
        )


async def main() -> None:
    p = argparse.ArgumentParser(description="SB1 BLE NUS + BLE MIDI test client")
    p.add_argument("--address", help="BLE MAC/address to connect")
    p.add_argument("--name-filter", default="SB1 MIDI INTERFACE", help="Name filter for scan")
    p.add_argument("--scan-timeout", type=float, default=5.0)
    p.add_argument("--connect-timeout", type=float, default=10.0)
    p.add_argument("--notify-wait", type=float, default=3.0)
    p.add_argument("--inter-message-delay", type=float, default=0.1)
    p.add_argument("--nus-hex", default="4D 42 01", help="Hex payload for NUS RX write")
    p.add_argument("--note", type=int, default=60)
    p.add_argument("--velocity", type=int, default=100)
    p.add_argument("--cycles", type=int, default=1, help="Connect/send/disconnect cycles")
    args = p.parse_args()

    for i in range(args.cycles):
        print(f"\n=== cycle {i + 1}/{args.cycles} ===")
        await run_once(args)
        await asyncio.sleep(0.5)

    print("\nAll cycles complete.")


if __name__ == "__main__":
    asyncio.run(main())
