#!/bin/bash
# Auto-detect ESP32 serial port and upload firmware using PlatformIO

# List possible ESP32 serial ports (macOS)
PORT=$(pio device list | grep -Eo '/dev/cu\.usb.*' | head -n 1)

if [ -z "$PORT" ]; then
  echo "No ESP32 serial port detected!"
  exit 1
fi

echo "Using port: $PORT"
pio run -e master --target upload --upload-port "$PORT"
