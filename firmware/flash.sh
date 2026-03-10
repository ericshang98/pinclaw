#!/bin/bash
# Pinclaw Firmware Flash Script
# Usage: ./flash.sh [blink_test|mic_test]

set -e

FQBN="Seeeduino:nrf52:xiaonRF52840Sense"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SKETCH="${1:-blink_test}"

if [ ! -d "$SCRIPT_DIR/$SKETCH" ]; then
  echo "Error: Sketch '$SKETCH' not found"
  echo "Available: blink_test, mic_test"
  exit 1
fi

echo "=== Pinclaw Firmware Flasher ==="
echo "Sketch: $SKETCH"
echo ""

# Find the board port
PORT=$(arduino-cli board list 2>/dev/null | grep -i "nrf52840\|XIAO\|seeed" | awk '{print $1}' | head -1)

if [ -z "$PORT" ]; then
  # Try to find any USB modem device
  PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
fi

if [ -z "$PORT" ]; then
  echo "ERROR: No XIAO nRF52840 detected!"
  echo ""
  echo "Please:"
  echo "  1. Connect the board via USB-C"
  echo "  2. If it still doesn't show up, double-tap the RESET button"
  echo "     (the board enters bootloader mode, orange LED pulses)"
  echo ""
  echo "Connected devices:"
  arduino-cli board list 2>/dev/null || true
  exit 1
fi

echo "Found board on: $PORT"
echo "Compiling..."
arduino-cli compile --fqbn "$FQBN" "$SCRIPT_DIR/$SKETCH"

echo ""
echo "Uploading..."
arduino-cli upload --fqbn "$FQBN" --port "$PORT" "$SCRIPT_DIR/$SKETCH"

echo ""
echo "=== Done! ==="
echo ""

if [ "$SKETCH" = "blink_test" ]; then
  echo "You should see RED -> GREEN -> BLUE LED cycling."
  echo "Open serial monitor: arduino-cli monitor -p $PORT -c baudrate=115200"
elif [ "$SKETCH" = "mic_test" ]; then
  echo "GREEN LED = mic active. BLUE flash = voice detected."
  echo "Open serial monitor to see audio levels:"
  echo "  arduino-cli monitor -p $PORT -c baudrate=115200"
fi
