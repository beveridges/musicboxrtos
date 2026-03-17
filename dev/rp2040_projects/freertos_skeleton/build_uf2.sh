#!/usr/bin/env bash
set -e

# Arg 1: base name for .uf2 output (script adds extension). Default: freertos_skeleton
# Arg 2: optional — copy UF2 here after build (e.g. /e/ for Pico RPI-RP2 drive in Git Bash, or E:)
NAME="${1:-freertos_skeleton}"
DEST="$2"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Remove build dir for a clean build; if it's locked (e.g. by Dropbox/IDE), clean in-tree instead
if ! rm -rf build 2>/dev/null; then
  echo "Note: could not remove build/ (device or resource busy); doing in-tree clean."
  (cd build && ninja clean 2>/dev/null) || true
fi
mkdir -p build
cd build
cmake -G Ninja ..
cmake --build .

"$SCRIPT_DIR/../../picotool-2.2.0-a4-x64-win/picotool/picotool.exe" uf2 convert freertos_skeleton.elf "${NAME}.uf2"

echo "Done. ${NAME}.uf2 is in build/"

if [ -n "$DEST" ]; then
  UF2="$SCRIPT_DIR/build/${NAME}.uf2"
  # Normalize destination (e.g. E: -> /e/, ensure trailing slash for dir)
  DEST_DIR="${DEST%/}/"
  if [ -d "$DEST_DIR" ] || [ -d "$DEST" ]; then
    cp "$UF2" "$DEST_DIR"
    echo "Copied ${NAME}.uf2 to $DEST_DIR (eject Pico or reset to run firmware)."
  else
    echo "Transfer skipped: destination not found ($DEST). Put Pico in BOOTSEL mode and pass its drive (e.g. /e/ or E:)."
    exit 1
  fi
fi
