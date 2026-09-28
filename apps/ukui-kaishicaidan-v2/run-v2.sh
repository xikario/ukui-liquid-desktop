#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN=${UKUI_KAISHICAIDAN_V2_BIN:-$SCRIPT_DIR/build-v2/ukui-kaishicaidan-v2}

if [ ! -x "$BIN" ]; then
    echo "V2 binary not found: $BIN" >&2
    echo "Build it with: cmake -S . -B build-v2 -DCMAKE_BUILD_TYPE=Release && cmake --build build-v2 -j\$(nproc)" >&2
    exit 127
fi

# Manual visual test only. This never installs or enables autostart.
exec "$BIN" --show "$@"
