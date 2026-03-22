#!/usr/bin/env python3
"""
Pinclaw BLE Connection + Command Test
Connects to the Pinclaw hardware via BLE and tests commands
to diagnose the disconnect-on-button-press issue.
"""

import asyncio
import sys
import time
from bleak import BleakClient, BleakScanner

SERVICE_UUID       = "12345678-1234-1234-1234-123456789abc"
TEXT_CHAR_UUID     = "12345678-1234-1234-1234-123456789abd"
AUDIO_CHAR_UUID    = "12345678-1234-1234-1234-123456789abe"
HEARTBEAT_CHAR_UUID= "12345678-1234-1234-1234-123456789abf"
SPEAKER_CHAR_UUID  = "12345678-1234-1234-1234-123456789ac0"

CMD_START_REC = bytes([0x01])
CMD_STOP_REC  = bytes([0x00])
CMD_PLAY      = bytes([0x20])
CMD_SD_SYNC   = bytes([0x10])

disconnected_event = asyncio.Event()

def on_disconnect(client):
    ts = time.strftime("%H:%M:%S")
    print(f"\n[{ts}] *** DISCONNECTED! ***")
    disconnected_event.set()

def on_text_notify(sender, data):
    ts = time.strftime("%H:%M:%S")
    hex_str = data.hex()
    print(f"[{ts}] TEXT notify: {hex_str} ({len(data)} bytes)")

def on_audio_notify(sender, data):
    ts = time.strftime("%H:%M:%S")
    pkt_type = data[0] if data else 0
    type_names = {0x01: "START", 0x02: "DATA", 0x03: "END"}
    name = type_names.get(pkt_type, f"0x{pkt_type:02X}")
    print(f"[{ts}] AUDIO notify: {name} ({len(data)} bytes)")

def on_heartbeat_notify(sender, data):
    ts = time.strftime("%H:%M:%S")
    if len(data) >= 4:
        counter = (data[1] << 8) | data[2]
        flags = data[3]
        batt = ((data[4] << 8) | data[5]) if len(data) >= 6 else 0
        print(f"[{ts}] HEARTBEAT #{counter} flags={flags:#04x} batt={batt}mV")
    else:
        print(f"[{ts}] HEARTBEAT: {data.hex()}")

async def main():
    print("=== Pinclaw BLE Diagnostic Test ===\n")

    # Step 1: Scan — use service UUID (name may be truncated in adv packet)
    print("[1] Scanning for Pinclaw device (by service UUID)...")
    devices = await BleakScanner.discover(
        timeout=10.0, return_adv=True,
        service_uuids=[SERVICE_UUID]
    )

    if not devices:
        print("ERROR: Pinclaw device not found! Make sure it's powered on.")
        sys.exit(1)

    addr, (device, adv) = next(iter(devices.items()))
    print(f"    Found: {device.name or '?'} [{addr}] RSSI={adv.rssi}")

    # Step 2: Connect
    print("\n[2] Connecting...")
    async with BleakClient(device, disconnected_callback=on_disconnect) as client:
        print(f"    Connected! MTU={client.mtu_size}")

        # Step 3: Discover and subscribe
        print("\n[3] Subscribing to notifications...")
        try:
            await client.start_notify(TEXT_CHAR_UUID, on_text_notify)
            print("    TEXT characteristic: subscribed")
        except Exception as e:
            print(f"    TEXT: failed ({e})")

        try:
            await client.start_notify(AUDIO_CHAR_UUID, on_audio_notify)
            print("    AUDIO characteristic: subscribed")
        except Exception as e:
            print(f"    AUDIO: failed ({e})")

        try:
            await client.start_notify(HEARTBEAT_CHAR_UUID, on_heartbeat_notify)
            print("    HEARTBEAT characteristic: subscribed")
        except Exception as e:
            print(f"    HEARTBEAT: failed ({e})")

        # Step 4: Wait for stable connection
        print("\n[4] Waiting 3s for stable connection...")
        await asyncio.sleep(3)
        if disconnected_event.is_set():
            print("    Connection lost during idle wait!")
            return

        print("    Connection stable.")

        # Step 5: Test CMD_PLAY (single tap simulation)
        print("\n[5] Sending CMD_PLAY (0x20) — simulates hardware button single tap...")
        try:
            await client.write_gatt_char(TEXT_CHAR_UUID, CMD_PLAY, response=True)
            print("    CMD_PLAY sent successfully (write with response)")
        except Exception as e:
            print(f"    CMD_PLAY failed: {e}")

        await asyncio.sleep(2)
        if disconnected_event.is_set():
            print("    *** DISCONNECTED after CMD_PLAY! ***")
            return
        print("    Connection OK after CMD_PLAY.")

        # Step 6: Test start recording
        print("\n[6] Sending START RECORDING (0x01) — simulates 'Hold to talk'...")
        try:
            await client.write_gatt_char(TEXT_CHAR_UUID, CMD_START_REC, response=True)
            print("    START_REC sent successfully")
        except Exception as e:
            print(f"    START_REC failed: {e}")

        print("    Waiting 3s for audio data...")
        await asyncio.sleep(3)
        if disconnected_event.is_set():
            print("    *** DISCONNECTED after START_REC! ***")
            return
        print("    Connection OK during recording.")

        # Step 7: Stop recording
        print("\n[7] Sending STOP RECORDING (0x00)...")
        try:
            await client.write_gatt_char(TEXT_CHAR_UUID, CMD_STOP_REC, response=True)
            print("    STOP_REC sent successfully")
        except Exception as e:
            print(f"    STOP_REC failed: {e}")

        await asyncio.sleep(2)
        if disconnected_event.is_set():
            print("    *** DISCONNECTED after STOP_REC! ***")
            return
        print("    Connection OK after stop recording.")

        # Step 8: Test write WITHOUT response
        print("\n[8] Sending CMD_PLAY with write-WITHOUT-response...")
        try:
            await client.write_gatt_char(TEXT_CHAR_UUID, CMD_PLAY, response=False)
            print("    CMD_PLAY (no response) sent successfully")
        except Exception as e:
            print(f"    CMD_PLAY (no response) failed: {e}")

        await asyncio.sleep(2)
        if disconnected_event.is_set():
            print("    *** DISCONNECTED after CMD_PLAY (no response)! ***")
            return

        # Step 9: Final status
        print("\n[9] All tests passed! Waiting 5s for any delayed disconnect...")
        await asyncio.sleep(5)
        if disconnected_event.is_set():
            print("    Late disconnect detected.")
        else:
            print("    Connection remained stable through all tests!")

    print("\n=== Test Complete ===")

if __name__ == "__main__":
    asyncio.run(main())
