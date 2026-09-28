#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BINARY=${1:-"$PROJECT_DIR/build-smart-space/ukui-fences"}
RUNTIME_DIR=$(mktemp -d /tmp/ukui-fences-resource.XXXXXX)
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
mkdir -p "$ROOT_DIR" "$CONFIG_DIR" "$CACHE_DIR"
python3 - "$ROOT_DIR" <<'PY'
import sys
from pathlib import Path
root = Path(sys.argv[1])
for folder_index in range(25):
    folder = root / ("folder-%02d" % folder_index)
    folder.mkdir()
    for file_index in range(1000):
        (folder / ("document-%04d.txt" % file_index)).write_text(
            "bounded searchable resource benchmark content\n", encoding="utf-8")
PY

export XDG_CONFIG_HOME="$CONFIG_DIR"
export XDG_CACHE_HOME="$CACHE_DIR"
export UKUI_FENCES_SMARTSPACE_ROOTS="$ROOT_DIR"
export UKUI_FENCES_SMARTSPACE_OCR=0
export UKUI_FENCES_SMARTSPACE_AUTO_INDEX=1
export UKUI_FENCES_TEST_AUTO_INDEX=1
export UKUI_FENCES_TEST_CONFIRM_IDLE=1

"$BINARY" > "$RUNTIME_DIR/app.log" 2>&1 &
APP_PID=$!
attempt=0
while [ "$attempt" -lt 50 ]; do
    if gdbus introspect --session --dest org.ukui.fences \
        --object-path /ukuiFences >/dev/null 2>&1; then break; fi
    attempt=$((attempt + 1))
    sleep 0.1
done
if ! kill -0 "$APP_PID" 2>/dev/null; then
    echo "resource benchmark application exited during startup" >&2
    sed -n '1,160p' "$RUNTIME_DIR/app.log" >&2 || true
    exit 1
fi
if ! gdbus introspect --session --dest org.ukui.fences \
    --object-path /ukuiFences >/dev/null 2>&1; then
    echo "resource benchmark D-Bus service did not become ready" >&2
    sed -n '1,160p' "$RUNTIME_DIR/app.log" >&2 || true
    exit 1
fi

BASE_RSS=$(awk '/VmRSS:/ {print $2}' "/proc/$APP_PID/status")
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.toggleSmartSpace >/dev/null

INDEX_PATH="$CACHE_DIR/kylin/ukui-fences/smart-space/index.json"
MAIN_PEAK=$BASE_RSS
INDEXER_PEAK=0
INDEXER_NICE=0
attempt=0
while [ "$attempt" -lt 600 ]; do
    attempt=$((attempt + 1))
    current=$(awk '/VmRSS:/ {print $2}' "/proc/$APP_PID/status")
    [ "$current" -le "$MAIN_PEAK" ] || MAIN_PEAK=$current
    child=$(pgrep -P "$APP_PID" -n 2>/dev/null || true)
    if [ -n "$child" ] && [ -r "/proc/$child/status" ]; then
        # The short-lived indexer can exit between the readability check and
        # opening /proc.  Treat that sampling race as an empty measurement.
        child_rss=$(awk '/VmRSS:/ {print $2}' "/proc/$child/status" 2>/dev/null || true)
        if [ -n "$child_rss" ]; then
            [ "$child_rss" -le "$INDEXER_PEAK" ] || INDEXER_PEAK=$child_rss
        fi
        child_nice=$(ps -o ni= -p "$child" | tr -d ' ' || true)
        [ -z "$child_nice" ] || INDEXER_NICE=$child_nice
    fi
    if [ -s "$INDEX_PATH" ] && [ -z "$child" ]; then break; fi
    sleep 0.05
done
# Let JSON loading, tag aggregation and first paint settle before measuring
# steady-state CPU.  Keep observing RSS so this stabilization is still part
# of the peak-memory assertion.
settle=0
while [ "$settle" -lt 50 ]; do
    current=$(awk '/VmRSS:/ {print $2}' "/proc/$APP_PID/status")
    [ "$current" -le "$MAIN_PEAK" ] || MAIN_PEAK=$current
    sleep 0.1
    settle=$((settle + 1))
done
SMART_RSS=$(awk '/VmRSS:/ {print $2}' "/proc/$APP_PID/status")
[ "$SMART_RSS" -le "$MAIN_PEAK" ] || MAIN_PEAK=$SMART_RSS

ticks_before=$(awk '{print $14+$15}' "/proc/$APP_PID/stat")
sleep 5
ticks_after=$(awk '{print $14+$15}' "/proc/$APP_PID/stat")
clock_ticks=$(getconf CLK_TCK)
IDLE_CPU=$(awk -v d="$((ticks_after - ticks_before))" -v h="$clock_ticks" \
    'BEGIN { printf "%.3f", (d / h) / 5 * 100 }')

# Rapid typing must coalesce into one result/folder rebuild.  Sampling process
# ticks across the input burst catches the former per-character O(N*folders)
# refresh without relying on subjective animation timing.
typing_ticks_before=$(awk '{print $14+$15}' "/proc/$APP_PID/stat")
typing_start_ns=$(date +%s%N)
xdotool mousemove 350 289 click 1 key ctrl+a
xdotool type --delay 5 'semantic-report-typing-benchmark-2025-final-version'
sleep 0.7
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceWidth >/dev/null
typing_end_ns=$(date +%s%N)
typing_ticks_after=$(awk '{print $14+$15}' "/proc/$APP_PID/stat")
TYPING_CPU=$(awk -v d="$((typing_ticks_after - typing_ticks_before))" \
    -v h="$clock_ticks" -v ns="$((typing_end_ns - typing_start_ns))" \
    'BEGIN { seconds=ns/1000000000; printf "%.3f", (d/h)/seconds*100 }')
xdotool key ctrl+a BackSpace
sleep 0.5

# Run the explicit fast-full rebuild against the same 25,000-file tree.
# The compact JSON keeps the previous fullRebuild=false value until the new
# snapshot is atomically installed, so it is also a reliable completion marker.
xdotool mousemove 88 394 click 1
IDLE_INDEXER_PEAK=0
IDLE_INDEXER_NICE=0
attempt=0
while [ "$attempt" -lt 1200 ]; do
    attempt=$((attempt + 1))
    current=$(awk '/VmRSS:/ {print $2}' "/proc/$APP_PID/status")
    [ "$current" -le "$MAIN_PEAK" ] || MAIN_PEAK=$current
    child=$(pgrep -P "$APP_PID" -n 2>/dev/null || true)
    if [ -n "$child" ] && [ -r "/proc/$child/status" ]; then
        child_rss=$(awk '/VmRSS:/ {print $2}' "/proc/$child/status" 2>/dev/null || true)
        if [ -n "$child_rss" ]; then
            [ "$child_rss" -le "$IDLE_INDEXER_PEAK" ] || IDLE_INDEXER_PEAK=$child_rss
        fi
        child_nice=$(ps -o ni= -p "$child" | tr -d ' ' || true)
        [ -z "$child_nice" ] || IDLE_INDEXER_NICE=$child_nice
    fi
    if grep -q '"fullRebuild":true' "$INDEX_PATH" 2>/dev/null && [ -z "$child" ]; then
        break
    fi
    sleep 0.05
done
if [ "$attempt" -ge 1200 ]; then
    echo "idle full resource benchmark timed out" >&2
    exit 1
fi
SMART_RSS=$(awk '/VmRSS:/ {print $2}' "/proc/$APP_PID/status")
[ "$SMART_RSS" -le "$MAIN_PEAK" ] || MAIN_PEAK=$SMART_RSS

# Hiding destroys the embedded index UI and trims released heap pages.  This
# prevents a one-time search from permanently leaving the desktop process at
# its peak RSS.
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.toggleSmartSpace >/dev/null
sleep 2
HIDDEN_RSS=$(awk '/VmRSS:/ {print $2}' "/proc/$APP_PID/status")

python3 - "$INDEX_PATH" "$BASE_RSS" "$MAIN_PEAK" "$SMART_RSS" \
    "$INDEXER_PEAK" "$INDEXER_NICE" "$IDLE_CPU" \
    "$TYPING_CPU" "$IDLE_INDEXER_PEAK" "$IDLE_INDEXER_NICE" "$HIDDEN_RSS" \
    "$PROJECT_DIR/test-results/resource-benchmark.json" <<'PY'
import json
import sys
from pathlib import Path
index_path = Path(sys.argv[1])
payload = json.loads(index_path.read_text(encoding="utf-8"))
base, peak, smart, indexer = map(int, sys.argv[2:6])
result = {
    "fixtureFiles": 25000,
    "indexedItems": len(payload["items"]),
    "truncated": payload["stats"].get("truncated", 0),
    "contentChars": payload["stats"].get("contentChars", 0),
    "indexMiB": round(index_path.stat().st_size / 1024 / 1024, 2),
    "baseRssMiB": round(base / 1024, 2),
    "smartSpaceRssMiB": round(smart / 1024, 2),
    "mainPeakDeltaMiB": round((peak - base) / 1024, 2),
    "indexerPeakMiB": round(indexer / 1024, 2),
    "indexerNice": int(sys.argv[6] or 0),
    "idleCpuPercent": float(sys.argv[7]),
    "typingCpuPercent": float(sys.argv[8]),
    "idleFullIndexerPeakMiB": round(int(sys.argv[9]) / 1024, 2),
    "idleFullIndexerNice": int(sys.argv[10] or 0),
    "idleFullIoPriority": payload.get("capabilities", {}).get("ioPriority", ""),
    "hiddenRssMiB": round(int(sys.argv[11]) / 1024, 2),
    "hiddenDeltaMiB": round((int(sys.argv[11]) - base) / 1024, 2),
}
print(json.dumps(result, ensure_ascii=False, indent=2))
Path(sys.argv[12]).parent.mkdir(parents=True, exist_ok=True)
Path(sys.argv[12]).write_text(
    json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
if result["indexedItems"] <= 25000 or result["truncated"]:
    raise SystemExit("idle full index did not traverse beyond the 25k daily limit")
if result["mainPeakDeltaMiB"] > 128:
    raise SystemExit("Smart Space main-process memory delta exceeded 128 MiB")
if result["hiddenDeltaMiB"] > 32:
    raise SystemExit("hidden Smart Space retained more than 32 MiB over baseline")
if result["indexerPeakMiB"] > 128:
    raise SystemExit("indexer peak memory exceeded 128 MiB")
if result["idleFullIndexerPeakMiB"] > 128:
    raise SystemExit("idle full indexer peak memory exceeded 128 MiB")
if result["idleCpuPercent"] > 1.0:
    raise SystemExit("idle CPU exceeded 1%")
if result["typingCpuPercent"] > 70.0:
    raise SystemExit("rapid typing consumed too much CPU; debounce may be broken")
if result["indexMiB"] > 40:
    raise SystemExit("index snapshot exceeded 40 MiB")
if result["indexerNice"] < 10:
    raise SystemExit("indexer was not lowered to nice 10")
if result["idleFullIndexerNice"] < 19:
    raise SystemExit("idle full indexer was not lowered to nice 19")
if "idle" not in result["idleFullIoPriority"].casefold():
    raise SystemExit("idle full indexer did not use ionice idle")
if not payload.get("fullRebuild") or payload.get("indexMode") != "fast-full":
    raise SystemExit("resource run did not use fast-full mode")
if payload.get("ocrImages"):
    raise SystemExit("fast-full resource run unexpectedly enabled OCR")
if payload.get("capabilities", {}).get("maxItems") != 0:
    raise SystemExit("idle full index still had an item-count limit")
PY

gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.quitApp >/dev/null
wait "$APP_PID"
APP_PID=
