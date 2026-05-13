#!/usr/bin/env bash
#
# Creates a "full" UF2 containing SoftDevice S140 v7.3.0 + application firmware,
# for flashing onto devices with a non-Adafruit bootloader (e.g. LoRaWAN bootloader)
# that still supports UF2 drag-and-drop.
#
# Usage:
#   ./create-full-uf2.sh <pio-env> <softdevice.hex>
#
# Example:
#   ./create-full-uf2.sh t1000e_companion_radio_ble s140_nrf52_7.3.0_softdevice.hex
#
# The SoftDevice hex can be downloaded from Nordic Semiconductor:
#   https://www.nordicsemi.com/Products/Development-software/s140/download
#
# Output: out/<pio-env>-full.uf2

set -e

if [ -z "$1" ] || [ -z "$2" ]; then
  echo "Usage: $0 <pio-env> <softdevice.hex>"
  echo ""
  echo "Example:"
  echo "  $0 t1000e_companion_radio_ble s140_nrf52_7.3.0_softdevice.hex"
  exit 1
fi

PIO_ENV="$1"
SOFTDEVICE_HEX="$2"
FIRMWARE_HEX=".pio/build/${PIO_ENV}/firmware.hex"
MERGED_HEX=".pio/build/${PIO_ENV}/firmware-merged-sd.hex"
OUTPUT_UF2="out/${PIO_ENV}-full.uf2"

# Check that inputs exist
if [ ! -f "$SOFTDEVICE_HEX" ]; then
  echo "Error: SoftDevice hex not found: $SOFTDEVICE_HEX"
  exit 1
fi

if [ ! -f "$FIRMWARE_HEX" ]; then
  echo "Error: Firmware hex not found: $FIRMWARE_HEX"
  echo "Build the firmware first: pio run -e $PIO_ENV"
  exit 1
fi

mkdir -p out

# Merge SoftDevice + firmware hex files
# Remove the EOF record (:00000001FF) from SoftDevice, concatenate with firmware
echo "Merging SoftDevice + firmware hex files..."
grep -v '^:00000001FF' "$SOFTDEVICE_HEX" > "$MERGED_HEX"
cat "$FIRMWARE_HEX" >> "$MERGED_HEX"

# Convert merged hex to UF2
echo "Converting to UF2..."
python3 bin/uf2conv/uf2conv.py "$MERGED_HEX" -c -o "$OUTPUT_UF2" -f 0xADA52840

echo ""
echo "Done! Full UF2 (SoftDevice + firmware): $OUTPUT_UF2"
echo "Drop this file onto the T1000-E UF2 drive."
