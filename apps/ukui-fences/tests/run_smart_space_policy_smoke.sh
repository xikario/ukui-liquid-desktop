#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BINARY=${1:-"$PROJECT_DIR/build-smart-space/ukui-fences"}
RESULT_DIR="$PROJECT_DIR/test-results"
RUNTIME_DIR=$(mktemp -d /tmp/ukui-fences-policy.XXXXXX)
APP_PID=
cleanup() {
    if [ -n "${APP_PID:-}" ]; then
        kill "$APP_PID" 2>/dev/null || true
        wait "$APP_PID" 2>/dev/null || true
    fi
    rm -rf -- "$RUNTIME_DIR"
}
trap cleanup EXIT INT TERM

ROOT_DIR="$RUNTIME_DIR/documents"
CONFIG_DIR="$RUNTIME_DIR/config"
CACHE_DIR="$RUNTIME_DIR/cache"
mkdir -p "$ROOT_DIR" "$CONFIG_DIR/kylin" "$CACHE_DIR" "$RESULT_DIR"
printf '%s\n' 'This file must not be indexed merely by opening the widget.' \
    > "$ROOT_DIR/manual-only.txt"
printf '%s\n' '[systemMonitor]' 'skin=1' 'opacity=100' \
    '[smartSpace]' 'autoStart=true' \
    > "$CONFIG_DIR/kylin/ukui-fences.ini"

export XDG_CONFIG_HOME="$CONFIG_DIR"
export XDG_CACHE_HOME="$CACHE_DIR"
export UKUI_FENCES_SMARTSPACE_ROOTS="$ROOT_DIR"
unset UKUI_FENCES_SMARTSPACE_AUTO_INDEX || true

"$BINARY" --autostart > "$RESULT_DIR/policy-smoke.log" 2>&1 &
APP_PID=$!
attempt=0
while [ "$attempt" -lt 40 ]; do
    if gdbus introspect --session --dest org.ukui.fences \
        --object-path /ukuiFences >/dev/null 2>&1; then break; fi
    attempt=$((attempt + 1))
    sleep 0.1
done
sleep 2

visible=$(gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences --method org.ukui.fences.smartSpaceVisible)
case "$visible" in
    *true*) ;;
    *) echo "Smart Space auto-start setting did not show the widget" >&2; exit 1 ;;
esac
# The explicit command is ensure-visible, not a toggle.  Calling it twice must
# never make an already visible widget disappear.
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.showSmartSpaceWidget >/dev/null
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.showSmartSpaceWidget >/dev/null
visible=$(gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences --method org.ukui.fences.smartSpaceVisible)
case "$visible" in
    *true*) ;;
    *) echo "repeated ensure-visible call hid Smart Space" >&2; exit 1 ;;
esac

INDEX_PATH="$CACHE_DIR/kylin/ukui-fences/smart-space/index.json"
if [ -e "$INDEX_PATH" ]; then
    echo "opening Smart Space unexpectedly started indexing" >&2
    exit 1
fi
if pgrep -P "$APP_PID" -f smart_space_indexer.py >/dev/null 2>&1; then
    echo "indexer process is running in manual mode" >&2
    exit 1
fi
import -window root "$RESULT_DIR/smart-space-manual-no-index.png"
# The real desktop context menu exposes System Monitor and Smart Space as
# sibling checkable widgets, plus a widget autostart submenu.
xdotool mousemove 1100 400 click 3
sleep 0.4
import -window root "$RESULT_DIR/smart-space-desktop-context-menu.png"
xdotool key Escape
sleep 0.3
SETTINGS_BEFORE=$(xdotool search --name '智能空间设置' 2>/dev/null || true)
SMART_X=$(gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceX | tr -cd '0-9-')
SMART_Y=$(gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceY | tr -cd '0-9-')
# The compact overflow button remains near the bottom of the 48 px left rail.
MORE_X=$((SMART_X + 38))
MORE_Y=$((SMART_Y + 345))
xdotool mousemove "$MORE_X" "$MORE_Y" click 1 key End Up Return
attempt=0
SETTINGS_WINDOW=
while [ "$attempt" -lt 20 ]; do
    for candidate in $(xdotool search --name '智能空间设置' 2>/dev/null || true); do
        case " $SETTINGS_BEFORE " in
            *" $candidate "*) ;;
            *) SETTINGS_WINDOW=$candidate ;;
        esac
    done
    if [ -n "$SETTINGS_WINDOW" ]; then break; fi
    attempt=$((attempt + 1))
    sleep 0.1
done
if [ -z "$SETTINGS_WINDOW" ]; then
    echo "Smart Space settings dialog did not open" >&2
    exit 1
fi
sleep 0.3
xdotool windowmap "$SETTINGS_WINDOW" 2>/dev/null || true
xdotool windowraise "$SETTINGS_WINDOW" 2>/dev/null || true
sleep 0.05
import -window root "$RESULT_DIR/smart-space-light-settings.png"
xdotool key Escape
python3 - "$RESULT_DIR/smart-space-manual-no-index.png" \
    "$RESULT_DIR/smart-space-desktop-context-menu.png" \
    "$RESULT_DIR/smart-space-light-settings.png" <<'PY'
import sys
from PIL import Image, ImageChops, ImageStat
image = Image.open(sys.argv[1]).convert("RGB")
panel_box = ((50, 260, 985, 800) if image.width <= 1600
             else (70, 860, 1425, 1660))
panel = image.crop(panel_box)
if sum(ImageStat.Stat(panel).mean) / 3 < 150:
    raise SystemExit("Smart Space did not follow the resource monitor light skin")
menu = Image.open(sys.argv[2]).convert("RGB")
if not ImageChops.difference(image, menu).getbbox():
    raise SystemExit("desktop context menu did not become visible")
menu_region = menu.crop((1080, 380, 1435, 890))
if sum(ImageStat.Stat(menu_region).var) < 40:
    raise SystemExit("desktop widget context menu appears blank")
settings = Image.open(sys.argv[3]).convert("RGB")
if settings.size != image.size or not ImageChops.difference(image, settings).getbbox():
    raise SystemExit("Smart Space settings dialog did not become visible")
dialog_box = ((340, 100, 1100, 750) if settings.width <= 1600
              else (880, 330, 1980, 1280))
dialog_region = settings.crop(dialog_box)
if sum(ImageStat.Stat(dialog_region).var) < 80:
    raise SystemExit("Smart Space settings dialog appears blank")
PY

gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.quitApp >/dev/null
wait "$APP_PID"
APP_PID=
printf '%s\n' '{"manualOpenDidNotIndex":true,"smartSpaceAutoStart":true,"ensureVisibleIdempotent":true,"desktopContextMenu":true}'
