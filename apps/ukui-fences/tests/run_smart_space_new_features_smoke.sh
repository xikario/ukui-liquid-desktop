#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BINARY=${1:-"$PROJECT_DIR/build-smart-space/ukui-fences"}
RESULT_DIR="$PROJECT_DIR/test-results/new-features"
mkdir -p "$RESULT_DIR"
RUNTIME_DIR=$(mktemp -d /tmp/ukui-fences-new-features.XXXXXX)
cleanup() {
    [ -z "${APP_PID:-}" ] || kill "$APP_PID" 2>/dev/null || true
    rm -rf -- "$RUNTIME_DIR"
}
trap cleanup EXIT INT TERM

ROOT="$RUNTIME_DIR/documents"
CONFIG="$RUNTIME_DIR/config"
CACHE="$RUNTIME_DIR/cache"
mkdir -p "$ROOT" "$CONFIG/kylin" "$CONFIG/kyfences" "$CACHE"
printf '%s\n' 'ordinary body' > "$ROOT/Needle-title.txt"
printf '%s\n' 'Needle appears only in indexed content' > "$ROOT/content-hit.txt"
python3 - "$ROOT" <<'PY'
import os, sys
from pathlib import Path
root = Path(sys.argv[1])
(root / "large.txt").write_text("large " * 400, encoding="utf-8")
os.utime(root / "Needle-title.txt", (1_600_000_000, 1_600_000_000))
os.utime(root / "content-hit.txt", (1_700_000_000, 1_700_000_000))
PY
printf '%s\n' '[smartSpace]' \
    'defaultHidden=false' \
    'themeMode=6' 'customBaseSkin=1' 'customOpacity=88' \
    'customColorsEnabled=false' > "$CONFIG/kylin/ukui-fences.ini"

export XDG_CONFIG_HOME="$CONFIG"
export XDG_CACHE_HOME="$CACHE"
export UKUI_FENCES_SMARTSPACE_ROOTS="$ROOT"
export UKUI_FENCES_SMARTSPACE_OCR=0
export UKUI_FENCES_SMARTSPACE_AUTO_INDEX=1
export UKUI_FENCES_TEST_AUTO_INDEX=1
"$BINARY" --smart-space > "$RESULT_DIR/app.log" 2>&1 &
APP_PID=$!
INDEX="$CACHE/kylin/ukui-fences/smart-space/index.json"
i=0
while [ "$i" -lt 100 ] && [ ! -s "$INDEX" ]; do i=$((i+1)); sleep 0.1; done
[ -s "$INDEX" ]
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.moveSmartSpace 50 246 >/dev/null
sleep 0.5
xdotool mousemove 300 289 click 1 key ctrl+a
xdotool type --delay 20 'Needle'
xdotool mousemove 500 289 click 1
sleep 0.5
import -window root "$RESULT_DIR/relevance-name-first.png"

python3 - "$INDEX" "$CONFIG/kylin/ukui-fences.ini" \
    "$RESULT_DIR/relevance-name-first.png" <<'PY'
import json
import sys
from pathlib import Path
from PIL import Image, ImageStat

index = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
if len(index["items"]) < 4:
    raise SystemExit("index fixture is incomplete")
config_path = Path(sys.argv[2])
config = config_path.read_text(encoding="utf-8", errors="ignore")
if any(key in config for key in ("aiEnabled", "aiApiUrl", "aiApiKey", "aiModel")):
    raise SystemExit("retired AI settings were persisted")
if (config_path.parent.parent / "kyfences" / "smart-spaces.json").exists():
    raise SystemExit("retired smart-spaces.json was created")
image = Image.open(sys.argv[3]).convert("RGB")
if image.size != (1440, 900) or sum(ImageStat.Stat(image).var) < 50:
    raise SystemExit("invalid or blank screenshot")
print(json.dumps({"localSearch": True,
                  "spacePersistenceRemoved": True,
                  "aiEnhancementRemoved": True},
                 ensure_ascii=False))
PY
