#!/bin/sh

set -e

workspace="$(cd "$(dirname "$0")" && pwd)"
nuttx="$workspace/nuttx"
build="$nuttx/build-esp32c3-devkit"
config="$workspace/nuttx_vendor_espressif/boards/esp32c3/esp32c3-devkit/configs/nsh"
output="$workspace/output"

[ -f "$nuttx/.config" ] && make -C "$nuttx" distclean
rm -f "$build/CMakeCache.txt"
rm -rf "$build/CMakeFiles"

cmake \
  -S "$nuttx" \
  -B "$build" \
  -DBOARD_CONFIG="$config" \
  -G Ninja

cmake --build "$build" --parallel "$(nproc)"

mkdir -p "$output"
cp "$build/nuttx.bin" "$output/nuttx.bin"
