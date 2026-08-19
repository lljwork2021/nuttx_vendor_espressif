#!/bin/sh

set -e

workspace="$(cd "$(dirname "$0")" && pwd)"
nuttx="$workspace/nuttx"
build="$nuttx/build-esp32c3-devkit"
config="$workspace/nuttx_vendor_espressif/boards/esp32c3/esp32c3-devkit/configs/nsh"
output="$workspace/output"
port="${ESPTOOL_PORT:-/dev/ttyACM0}"

apply_patches()
{
  patches_dir="$workspace/nuttx_vendor_espressif/patches"
  [ -d "$patches_dir" ] || { echo "-- no patches directory, skip"; return; }
  for target in nuttx apps; do
    src="$patches_dir/$target"
    [ -d "$src" ] || continue
    cp -r "$src/." "$workspace/$target/"
    echo "-- applied $(find "$src" -type f | wc -l) patch file(s) to $target/"
  done
}

build_firmware()
{
  echo "-- applying patches before build"
  apply_patches

  [ -f "$nuttx/.config" ] && make -C "$nuttx" distclean
  rm -f "$build/CMakeCache.txt"
  rm -rf "$build/CMakeFiles"
  rm -rf "$build/apps"

  cmake \
    -S "$nuttx" \
    -B "$build" \
    -DBOARD_CONFIG="$config" \
    -G Ninja

  cmake --build "$build" --parallel "$(nproc)"

  mkdir -p "$output"
  cp "$build/nuttx.bin" "$output/nuttx.bin"
}

flash_firmware()
{
  ESPTOOL_PORT="$port" cmake --build "$build" -t flash
}

case "${1:-}" in
  0)
    build_firmware
    flash_firmware
    ;;
  1)
    build_firmware
    ;;
  2)
    flash_firmware
    ;;
  *)
    echo "Usage: $0 {0|1|2}"
    echo "  0: build and flash"
    echo "  1: build only"
    echo "  2: flash only"
    exit 1
    ;;
esac
