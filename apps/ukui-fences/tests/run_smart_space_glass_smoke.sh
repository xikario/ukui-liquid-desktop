#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BINARY=${1:-"$PROJECT_DIR/build-smart-space-glass/ukui-fences"}
RESULT_DIR="$PROJECT_DIR/test-results/glass"
mkdir -p "$RESULT_DIR"
RUNTIME_DIR=$(mktemp -d /tmp/ukui-fences-glass.XXXXXX)
APP_PID=
cleanup() {
    if [ -n "${APP_PID:-}" ]; then
        gdbus call --session --dest org.ukui.fences \
            --object-path /ukuiFences --method org.ukui.fences.quitApp \
            >/dev/null 2>&1 || true
        kill "$APP_PID" 2>/dev/null || true
        wait "$APP_PID" 2>/dev/null || true
    fi
    rm -rf -- "$RUNTIME_DIR"
}
trap cleanup EXIT INT TERM

ROOT_DIR="$RUNTIME_DIR/documents"
CONFIG_DIR="$RUNTIME_DIR/config"
CACHE_DIR="$RUNTIME_DIR/cache"
mkdir -p "$ROOT_DIR" "$CONFIG_DIR/kylin" "$CACHE_DIR"
printf '%s\n' 'Liquid glass Smart Space fixture' > "$ROOT_DIR/glass-fixture.txt"
printf '%s\n' '[smartSpace]' \
    'defaultHidden=false' \
    'themeMode=3' \
    'autoStart=false' > "$CONFIG_DIR/kylin/ukui-fences.ini"

export XDG_CONFIG_HOME="$CONFIG_DIR"
export XDG_CACHE_HOME="$CACHE_DIR"
export UKUI_FENCES_SMARTSPACE_ROOTS="$ROOT_DIR"
export UKUI_FENCES_SMARTSPACE_OCR=0
unset UKUI_FENCES_SMARTSPACE_AUTO_INDEX || true

mkdir -p "$RUNTIME_DIR/home" "$RUNTIME_DIR/data"
env HOME="$RUNTIME_DIR/home" XDG_DATA_HOME="$RUNTIME_DIR/data" "$BINARY" --smart-space > "$RESULT_DIR/app.log" 2>&1 &
APP_PID=$!
attempt=0
while [ "$attempt" -lt 50 ] && ! gdbus introspect --session \
    --dest org.ukui.fences --object-path /ukuiFences >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    sleep 0.1
done

gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.moveSmartSpace 40 180 >/dev/null
sleep 0.8
import -window root "$RESULT_DIR/smart-space-glass.png"

python3 - "$RESULT_DIR/smart-space-glass.png" <<'PY'
import sys
from PIL import Image, ImageStat

image = Image.open(sys.argv[1]).convert("RGB")
if image.size != (1440, 900):
    raise SystemExit("unexpected screenshot size")
region = image.crop((30, 160, 1020, 760))
if sum(ImageStat.Stat(region).var) < 80:
    raise SystemExit("liquid glass Smart Space screenshot appears blank")
PY

gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.quitApp >/dev/null
wait "$APP_PID"
APP_PID=
grep -q '^themeMode=3$' "$CONFIG_DIR/kylin/ukui-fences.ini"
printf '%s\n' '{"glassSkin":true,"screenshot":true,"themePersisted":true}'
