#!/usr/bin/env bash
set -e

# Copy freertos_skeleton.uf2 (or another .uf2) to the Pico's RPI-RP2 USB drive.
# Put the board in BOOTSEL (hold BOOTSEL, plug USB), then run:
#   ./transfer_uf2.sh
#   ./transfer_uf2.sh E:
#   ./transfer_uf2.sh /e/
#   ./transfer_uf2.sh build/myname.uf2 F:

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEFAULT_UF2="$SCRIPT_DIR/build/freertos_skeleton.uf2"

usage() {
  echo "Usage: $0 [path/to/file.uf2] <destination>"
  echo ""
  echo "  destination: Pico mass-storage root, e.g. E:  E:/  /e/  (Git Bash)"
  echo ""
  echo "If you omit the .uf2 path, uses: build/freertos_skeleton.uf2"
  echo "Run ./build_uf2.sh first if that file is missing."
  exit 1
}

UF2=""
DEST=""

if [ $# -eq 0 ]; then
  usage
elif [ $# -eq 1 ]; then
  # One arg = Pico drive only (default UF2). If it looks like a .uf2 file, show usage.
  if [ -f "$1" ] && [[ "$1" == *.uf2 ]]; then
    echo "Pass the drive as well, e.g.: $0 \"$1\" E:"
    usage
  fi
  UF2="$DEFAULT_UF2"
  DEST="$1"
else
  UF2="$1"
  DEST="$2"
fi

if [ ! -f "$UF2" ]; then
  echo "UF2 not found: $UF2"
  echo "Build first: ./build_uf2.sh"
  exit 1
fi

DEST_DIR="${DEST%/}/"
if [ ! -d "$DEST_DIR" ]; then
  echo "Destination not found or not a directory: $DEST_DIR"
  echo "Is the Pico in BOOTSEL mode? Check the drive letter in Explorer."
  exit 1
fi

cp -f "$UF2" "$DEST_DIR"
echo "Copied $(basename "$UF2") -> $DEST_DIR"
echo "The Pico will reboot when the copy finishes; if not, unplug and plug again (run mode)."
