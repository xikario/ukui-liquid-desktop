#!/bin/sh
set -eu

BIN=${1:?path to ukui-fences binary}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/ukui-fences-screen-change.XXXXXX")
trap 'if [ -n "${PID:-}" ]; then kill "$PID" 2>/dev/null || true; wait "$PID" 2>/dev/null || true; fi; rm -rf "$TMP"' EXIT

mkdir -p "$TMP/config" "$TMP/cache"
export XDG_CONFIG_HOME="$TMP/config"
export XDG_CACHE_HOME="$TMP/cache"
export UKUI_FENCES_GLASS_NO_GL=1

"$BIN" --autostart >"$TMP/app.log" 2>&1 &
PID=$!
sleep 2
kill -0 "$PID"

# Xvfb reports a RandR BadValue while shrinking its single output, but still
# applies the root-screen size. This matches the transient topology seen by
# Qt during a projector mode switch.
xrandr --fb 1920x1080 >"$TMP/randr-1080.log" 2>&1 || true
sleep 1
kill -0 "$PID"
xrandr --query | grep -q 'current 1920 x 1080'

xrandr --fb 2880x1800 >"$TMP/randr-1800.log" 2>&1 || true
sleep 1
kill -0 "$PID"
xrandr --query | grep -q 'current 2880 x 1800'

printf '%s\n' 'screen_change_survived=true'
