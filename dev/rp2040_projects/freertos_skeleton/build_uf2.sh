#!/usr/bin/env bash
set -e

# Args:
#   (none)           — build freertos_skeleton.uf2, no copy
#   -u / --upload    — after build, copy UF2 to volume labeled RPI-RP2 (Windows Git Bash or Linux)
#                     If the drive is missing, tries "picotool reboot -f -u" then waits (no BOOTSEL button).
#   /f/  E:          — build default UF2 and copy to that drive
#   myname           — build myname.uf2, no copy
#   myname /f/       — build myname.uf2 and copy to drive
#   myname -u        — build myname.uf2 and auto-upload (flag can appear before or after name)
NAME="freertos_skeleton"
DEST=""
AUTO_UPLOAD=0

POSITIONAL=()
for arg in "$@"; do
  case "$arg" in
    -u | --upload)
      AUTO_UPLOAD=1
      ;;
    *)
      POSITIONAL+=("$arg")
      ;;
  esac
done
set -- "${POSITIONAL[@]}"

if [ $# -eq 0 ]; then
  :
elif [ $# -eq 1 ]; then
  case "$1" in
    /[a-zA-Z] | /[a-zA-Z]/ | [a-zA-Z]: | [a-zA-Z]:/ | [a-zA-Z]:\\)
      DEST="$1"
      ;;
    *)
      NAME="$1"
      ;;
  esac
else
  NAME="$1"
  DEST="$2"
fi

sb1_detect_rpi_rp2_windows() {
  if ! command -v powershell.exe >/dev/null 2>&1; then
    return 1
  fi
  local letter
  letter="$(powershell.exe -NoProfile -Command \
    "Get-Volume -FileSystemLabel 'RPI-RP2' -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty DriveLetter" \
    2>/dev/null | tr -d '\r\n' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//')"
  if [ -z "$letter" ]; then
    return 1
  fi
  letter="$(printf '%s' "$letter" | tr '[:upper:]' '[:lower:]')"
  echo "/${letter}/"
}

sb1_detect_rpi_rp2_linux() {
  if ! command -v lsblk >/dev/null 2>&1; then
    return 1
  fi
  local mp
  mp="$(lsblk -o LABEL,MOUNTPOINT -n -r 2>/dev/null | awk '$1=="RPI-RP2" && $2!=""{print $2; exit}')"
  if [ -z "$mp" ]; then
    return 1
  fi
  printf '%s' "$mp"
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PICOTOOL="$SCRIPT_DIR/../../picotool-2.2.0-a4-x64-win/picotool/picotool.exe"
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

"$PICOTOOL" uf2 convert freertos_skeleton.elf "${NAME}.uf2"

echo "Done. ${NAME}.uf2 is in build/"

# After a successful build, resolve RPI-RP2 when -u with no explicit drive
if [ "$AUTO_UPLOAD" -eq 1 ] && [ -z "$DEST" ]; then
  case "$(uname -s)" in
    MINGW* | MSYS* | CYGWIN*)
      DEST=""
      if d="$(sb1_detect_rpi_rp2_windows)"; then
        DEST="$d"
      fi
      if [ -z "$DEST" ] && [ -f "$PICOTOOL" ]; then
        echo "Auto-upload: RPI-RP2 not visible; running picotool reboot -f -u (USB boot, no BOOTSEL button)..."
        if ! "$PICOTOOL" reboot -f -u; then
          echo "Note: picotool reboot failed (device busy, wrong USB, or firmware not visible to picotool)."
        fi
        echo "Auto-upload: waiting for RPI-RP2 (up to ~20 s)..."
        for ((i = 1; i <= 40; i++)); do
          if d="$(sb1_detect_rpi_rp2_windows)"; then
            DEST="$d"
            break
          fi
          sleep 0.5
        done
      fi
      if [ -z "$DEST" ]; then
        echo "Auto-upload: RPI-RP2 still not found."
        echo "  - Unplug/replug USB after picotool reboot, or add reset_usb_boot(0,0) to firmware and trigger it once."
        echo "  - Or put the board in BOOTSEL and run: ./build_uf2.sh E:"
        exit 1
      fi
      ;;
    Linux)
      DEST=""
      if d="$(sb1_detect_rpi_rp2_linux)"; then
        DEST="$d"
      fi
      if [ -z "$DEST" ] && command -v picotool >/dev/null 2>&1; then
        echo "Auto-upload: RPI-RP2 not visible; running picotool reboot -f -u..."
        picotool reboot -f -u 2>/dev/null || true
        echo "Auto-upload: waiting for RPI-RP2 (up to ~20 s)..."
        for ((i = 1; i <= 40; i++)); do
          if d="$(sb1_detect_rpi_rp2_linux)"; then
            DEST="$d"
            break
          fi
          sleep 0.5
        done
      fi
      if [ -z "$DEST" ]; then
        echo "Auto-upload: no RPI-RP2 mount found (lsblk). Put Pico in BOOTSEL or use picotool reboot -f -u, then retry."
        exit 1
      fi
      ;;
    *)
      echo "Auto-upload (-u) is only set up for Windows (Git Bash) and Linux. Pass the drive explicitly, e.g. ./build_uf2.sh E:"
      exit 1
      ;;
  esac
  echo "Auto-upload: using destination $DEST"
fi

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
