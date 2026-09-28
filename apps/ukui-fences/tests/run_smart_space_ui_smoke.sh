#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BINARY=${1:-"$PROJECT_DIR/build-smart-space/ukui-fences"}
RESULT_DIR="$PROJECT_DIR/test-results"
mkdir -p "$RESULT_DIR"

RUNTIME_DIR=$(mktemp -d /tmp/ukui-fences-ui.XXXXXX)
PREVIOUS_ACTIVE_WINDOW=
cleanup() {
    if [ -n "${APP_PID:-}" ]; then
        kill "$APP_PID" 2>/dev/null || true
        wait "$APP_PID" 2>/dev/null || true
    fi
    if [ -n "${PREVIOUS_ACTIVE_WINDOW:-}" ]; then
        xdotool windowmap "$PREVIOUS_ACTIVE_WINDOW" 2>/dev/null || true
        xdotool windowactivate "$PREVIOUS_ACTIVE_WINDOW" 2>/dev/null || true
    fi
    rm -rf -- "$RUNTIME_DIR"
}
trap cleanup EXIT INT TERM

ROOT_DIR="$RUNTIME_DIR/documents"
CONFIG_DIR="$RUNTIME_DIR/config"
CACHE_DIR="$RUNTIME_DIR/cache"
mkdir -p "$ROOT_DIR/Project A/.ts" "$ROOT_DIR/Project A/Plans" \
    "$ROOT_DIR/Project B" "$CONFIG_DIR/kylin" "$CONFIG_DIR/kyfences" "$CACHE_DIR"
printf '%s\n' 'Alpha roadmap searchable content for smart space.' \
    > "$ROOT_DIR/Project A/alpha-roadmap.txt"
printf '%s\n' 'Nested milestone document.' > "$ROOT_DIR/Project A/Plans/milestone.docx"
printf '%s\n' 'Budget spreadsheet placeholder.' > "$ROOT_DIR/Project B/budget.xlsx"
printf '%s\n' 'Presentation placeholder.' > "$ROOT_DIR/Project B/launch.pptx"
python3 - "$ROOT_DIR/reference.pdf" <<'PY'
import sys
from reportlab.lib.pagesizes import A4
from reportlab.pdfgen import canvas
output = canvas.Canvas(sys.argv[1], pagesize=A4)
output.setFont("Helvetica", 22)
output.drawString(72, 760, "Reference PDF preview")
output.setFont("Helvetica", 13)
output.drawString(72, 725, "Smart Space on-demand first page rendering")
output.showPage()
output.save()
PY
python3 - "$ROOT_DIR/Project B" <<'PY'
import os
import sys
from pathlib import Path
root = Path(sys.argv[1])
target = root / "zz-bank-source.txt"
target.write_text("宁波银行 2025 年度报告 经营分析和风险管理", encoding="utf-8")
os.utime(target, (1_600_000_000, 1_600_000_000))
for index in range(130):
    path = root / ("zzz-recent-%03d.txt" % index)
    path.write_text("unrelated recent placeholder", encoding="utf-8")
PY
printf '%s\n' '{"sentinel":"must remain byte-for-byte unchanged","tags":[{"title":"legacy"}]}' \
    > "$ROOT_DIR/Project A/.ts/alpha-roadmap.txt.json"
cp "$ROOT_DIR/Project A/.ts/alpha-roadmap.txt.json" "$RUNTIME_DIR/sidecar-before.json"
# Keep the initial indexer alive briefly after filesystem scanning so the real
# determinate progress line and its hover details can be captured reliably.
python3 - "$RUNTIME_DIR/slow-provider.py" <<'PY'
import sys
from pathlib import Path
Path(sys.argv[1]).write_text(
    "import json\nimport sys\nimport time\nfrom pathlib import Path\n"
    "json.load(sys.stdin)\nstate=Path(sys.argv[1])\n"
    "if not state.exists():\n state.touch()\n time.sleep(3)\n"
    "json.dump([], sys.stdout)\n",
    encoding="utf-8")
PY
printf '%s\n' '{"providers":[{"name":"progress-hold","type":"command","program":"python3","arguments":["'"$RUNTIME_DIR"'/slow-provider.py","'"$RUNTIME_DIR"'/provider-ran"],"timeout":10}]}' \
    > "$RUNTIME_DIR/providers.json"

printf '%s\n' '[smartSpace]' 'defaultHidden=true' \
    "providerConfig=$RUNTIME_DIR/providers.json" \
    > "$CONFIG_DIR/kylin/ukui-fences.ini"

export XDG_CONFIG_HOME="$CONFIG_DIR"
export XDG_CACHE_HOME="$CACHE_DIR"
export UKUI_FENCES_SMARTSPACE_ROOTS="$ROOT_DIR"
export UKUI_FENCES_SMARTSPACE_OCR=0
export UKUI_FENCES_SMARTSPACE_AUTO_INDEX=1
export UKUI_FENCES_TEST_AUTO_INDEX=1
export UKUI_FENCES_TEST_CONFIRM_IDLE=1
export UKUI_FENCES_TEST_CONFIRM_EXCLUDE=1

# The desktop widget intentionally lives below normal application windows.
# Keep the caller out of the way for deterministic screenshots and restore it
# in cleanup, even if an assertion fails.
PREVIOUS_ACTIVE_WINDOW=$(xdotool getactivewindow 2>/dev/null || true)
if [ -n "$PREVIOUS_ACTIVE_WINDOW" ]; then
    xdotool windowminimize "$PREVIOUS_ACTIVE_WINDOW" 2>/dev/null || true
fi

"$BINARY" --smart-space > "$RESULT_DIR/ui-smoke.log" 2>&1 &
APP_PID=$!
INDEX_PATH="$CACHE_DIR/kylin/ukui-fences/smart-space/index.json"
attempt=0
while [ "$attempt" -lt 50 ] && ! pgrep -P "$APP_PID" -f smart_space_indexer.py >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    sleep 0.05
done
attempt=0
edge_state=
while [ "$attempt" -lt 50 ]; do
    edge_state=$(gdbus call --session --dest org.ukui.fences \
        --object-path /ukuiFences \
        --method org.ukui.fences.smartSpaceEdgeHidden 2>/dev/null || true)
    case "$edge_state" in *true*) break;; esac
    attempt=$((attempt + 1))
    sleep 0.1
done
case "$edge_state" in *true*) ;; *)
    echo "default hidden state was not enabled at startup" >&2
    exit 1
;; esac
sleep 0.4
import -window root "$RESULT_DIR/smart-space-default-hidden.png"
gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.revealSmartSpaceFromEdge >/dev/null
xdotool mousemove 1200 800
sleep 0.2
xdotool mousemove 450 324
sleep 1.5
import -window root "$RESULT_DIR/smart-space-index-progress.png"
attempt=0
while [ "$attempt" -lt 80 ] && [ ! -s "$INDEX_PATH" ]; do
    attempt=$((attempt + 1))
    sleep 0.1
done
[ -s "$INDEX_PATH" ] || { echo "Smart Space index did not become ready" >&2; exit 1; }
sleep 0.8
gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.moveSmartSpace 50 246 >/dev/null
sleep 0.3
# The first synthetic click activates the desktop canvas; subsequent clicks
# reach the child controls on X11/UKUI.
xdotool mousemove 350 289 click 1
sleep 0.2
overlap_state=$(gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceOverlapsDesktopIcons)
case "$overlap_state" in *false*) ;; *) echo "desktop icon overlaps Smart Space" >&2; exit 1;; esac
import -window root "$RESULT_DIR/smart-space-initial.png"

# Result density has eight persisted positions.  Exercise both endpoints and
# return to standard without changing the indexed result set.  D-Bus keeps
# this deterministic even when the desktop layer does not accept focus.
gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.setSmartSpaceDensity -- -5 >/dev/null
sleep 0.4
import -window root "$RESULT_DIR/smart-space-density-compact.png"
grep -q '^resultDensity=-5$' "$CONFIG_DIR/kylin/ukui-fences.ini"
gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.setSmartSpaceDensity 2 >/dev/null
sleep 0.3
grep -q '^resultDensity=2$' "$CONFIG_DIR/kylin/ukui-fences.ini"
gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.setSmartSpaceDensity 0 >/dev/null
sleep 0.3

# Type dropdown supports simultaneous PDF + spreadsheet selection and marks
# selected/unselected entries in its menu.
xdotool mousemove 935 289 click 1 key Home Down Down Return
xdotool mousemove 935 289 click 1 key Home Down Down Down Down Down Return
sleep 0.5
import -window root "$RESULT_DIR/smart-space-type-filter.png"
xdotool mousemove 935 289 click 1
sleep 0.3
import -window root "$RESULT_DIR/smart-space-type-menu.png"
xdotool key Escape
xdotool mousemove 935 289 click 1 key Home Return

# Full-text filtering is explicit: typing alone does not refresh; Enter or the
# search button commits whitespace-separated AND terms.
xdotool mousemove 350 289 click 1 key ctrl+a
xdotool type --delay 20 'Alpha txt'
sleep 0.5
import -window root "$RESULT_DIR/smart-space-search-pending.png"
xdotool mousemove 535 289 click 1
sleep 0.5
import -window root "$RESULT_DIR/smart-space-filtered.png"

# Explicit re: mode compiles a Unicode/case-insensitive regular expression
# once on submission.  Keep clipboard input so the backslash is exact.
xdotool mousemove 350 289 click 1 key ctrl+a
printf '%s' 're:^alpha.*\.txt$' | xclip -selection clipboard
xdotool key ctrl+v Return
sleep 0.5
import -window root "$RESULT_DIR/smart-space-regex-filtered.png"
xdotool mousemove 350 289 click 1 key ctrl+a BackSpace
xdotool key Return
sleep 0.4
xdotool mousemove 200 388 click 1
sleep 0.5
import -window root "$RESULT_DIR/smart-space-folder-drilldown.png"
# Clicking the otherwise empty lower part of the folder pane clears the scope
# and collapses every drilldown level back to the default root presentation.
xdotool mousemove 300 680 click 1
sleep 0.5
import -window root "$RESULT_DIR/smart-space-folder-blank-reset.png"

# Hovering a match exposes only a compact, highlighted relevant excerpt.
xdotool mousemove 350 289 click 1 key ctrl+a
xdotool type --delay 20 'Alpha'
xdotool key Return
sleep 0.3
xdotool mousemove 1200 800
sleep 0.2
xdotool mousemove 700 403
sleep 1.5
import -window root "$RESULT_DIR/smart-space-hover-index.png"

# The eye action opens an on-demand text preview drawer without changing
# single-click-to-open behavior on the filename/card.
xdotool mousemove 938 403 click 1
sleep 0.6
import -window root "$RESULT_DIR/smart-space-text-preview.png"
xdotool key Escape

# A real PDF fixture validates the low-priority first-page render path.
xdotool mousemove 750 289 click 1 key Home Down Down Down Return
sleep 0.5
xdotool mousemove 938 403 click 1
sleep 1.2
import -window root "$RESULT_DIR/smart-space-pdf-preview.png"
xdotool key Escape

# Local full-text search stays inside the widget and does not call a remote
# model or endpoint.
xdotool mousemove 750 289 click 1 key Home Return
xdotool mousemove 350 289 click 1 key ctrl+a
printf '%s' '宁波银行2025年报告' | xclip -selection clipboard
xdotool key ctrl+v
xdotool key Return
sleep 0.6
import -window root "$RESULT_DIR/smart-space-local-search.png"
xdotool mousemove 350 289 click 1 key ctrl+a BackSpace
xdotool key Return
sleep 0.4

# Always-on-top is a real top-level tool-window mode and is reversible.
geometry_state() {
    for method in smartSpaceX smartSpaceY smartSpaceWidth smartSpaceHeight; do
        gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
            --method "org.ukui.fences.$method" | tr -cd '0-9,-'
        printf ' '
    done
}
geometry_before_pin=$(geometry_state)
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.setSmartSpaceAlwaysOnTop true >/dev/null
pin_state=$(gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceAlwaysOnTop)
case "$pin_state" in *true*) ;; *) echo "pin state was not enabled" >&2; exit 1;; esac
sleep 0.4
import -window root "$RESULT_DIR/smart-space-pinned.png"
PIN_WINDOW=$(xdotool search --name '^智能空间$' 2>/dev/null | tail -n 1 || true)
[ -n "$PIN_WINDOW" ] || { echo "pinned Smart Space native window was not found" >&2; exit 1; }
if [ -n "$PIN_WINDOW" ]; then
    xprop -id "$PIN_WINDOW" WM_TRANSIENT_FOR _NET_WM_STATE \
        > "$RESULT_DIR/smart-space-pinned-xprop.txt" 2>&1 || true
    xwininfo -id "$PIN_WINDOW" \
        >> "$RESULT_DIR/smart-space-pinned-xprop.txt" 2>&1 || true
    if ! grep -Eq 'Depth:[[:space:]]+32' \
        "$RESULT_DIR/smart-space-pinned-xprop.txt"; then
        echo "pinned Smart Space did not use a 32-bit ARGB surface" >&2
        exit 1
    fi
fi

# The compact star uses the same ARGB window path while pinned.  This catches
# the native QRegion staircase that originally appeared only after pinning.
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.hideSmartSpaceToEdge >/dev/null
sleep 0.4
import -window root "$RESULT_DIR/smart-space-pinned-edge-star.png"
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.revealSmartSpaceFromEdge >/dev/null
geometry_after_pinned_edge=$(geometry_state)
if [ "$geometry_before_pin" != "$geometry_after_pinned_edge" ]; then
    echo "pinned edge-hide changed geometry: before=$geometry_before_pin after=$geometry_after_pinned_edge" >&2
    exit 1
fi
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.setSmartSpaceAlwaysOnTop false >/dev/null
pin_state=$(gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceAlwaysOnTop)
case "$pin_state" in *false*) ;; *) echo "pin state was not disabled" >&2; exit 1;; esac
geometry_after_pin=$(geometry_state)
if [ "$geometry_before_pin" != "$geometry_after_pin" ]; then
    echo "pin round-trip changed geometry: before=$geometry_before_pin after=$geometry_after_pin" >&2
    exit 1
fi

# Hide to the nearest screen edge, leave only the star, then restore in-place.
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.hideSmartSpaceToEdge >/dev/null
edge_state=$(gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceEdgeHidden)
case "$edge_state" in *true*) ;; *) echo "edge-hidden state was not enabled" >&2; exit 1;; esac
sleep 0.4
import -window root "$RESULT_DIR/smart-space-edge-star.png"
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.revealSmartSpaceFromEdge >/dev/null
edge_state=$(gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceEdgeHidden)
case "$edge_state" in *false*) ;; *) echo "edge-hidden state was not disabled" >&2; exit 1;; esac
geometry_after_edge=$(geometry_state)
if [ "$geometry_before_pin" != "$geometry_after_edge" ]; then
    echo "edge-hide round-trip changed geometry: before=$geometry_before_pin after=$geometry_after_edge" >&2
    exit 1
fi
sleep 0.4

# Local knowledge, Skill and file-format settings remain visible and inherit
# the monitor palette.
xdotool mousemove 88 579 click 1 key End Up Return
sleep 0.4
import -window root "$RESULT_DIR/smart-space-index-settings.png"
xdotool mousemove 680 118 click 1
sleep 0.4
import -window root "$RESULT_DIR/smart-space-skill-settings.png"
xdotool mousemove 466 118 click 1
sleep 0.4
import -window root "$RESULT_DIR/smart-space-format-settings.png"
xdotool key Escape

# Dragging inside the snap threshold must finish flush against the left edge.
xdotool mousemove 88 289 mousedown 1 mousemove 5 289 mouseup 1
sleep 0.5
import -window root "$RESULT_DIR/smart-space-edge-snapped.png"

# Moving the widget over the desktop icon grid must push icons to their
# nearest available cells outside the Smart Space reservation.
xdotool mousemove 35 289 mousedown 1 mousemove 500 58 mouseup 1
sleep 0.8
overlap_state=$(gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceOverlapsDesktopIcons)
case "$overlap_state" in *false*) ;; *) echo "desktop icon was not pushed out of Smart Space" >&2; exit 1;; esac
import -window root "$RESULT_DIR/smart-space-icon-exclusion.png"

# The compact breakpoint keeps the command bar and result cards usable while
# collapsing the folder pane.  Exercise it through the same public D-Bus
# surface used by desktop integration tests rather than synthesising an X11
# resize that may be rejected by the window manager.
gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.resizeSmartSpace 680 480 >/dev/null
sleep 0.8
compact_width=$(gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceWidth | tr -cd '0-9')
if [ "$compact_width" -gt 720 ]; then
    echo "compact responsive resize was not applied: $compact_width" >&2
    exit 1
fi
overlap_state=$(gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.smartSpaceOverlapsDesktopIcons)
case "$overlap_state" in *false*) ;; *) echo "compact Smart Space overlaps a desktop icon" >&2; exit 1;; esac
import -window root "$RESULT_DIR/smart-space-compact-responsive.png"

# The reduced rail remains usable at the declared minimum
# at the declared minimum
# widget size without overflowing the rounded rail.
gdbus call --session --dest org.ukui.fences \
    --object-path /ukuiFences \
    --method org.ukui.fences.resizeSmartSpace 620 360 >/dev/null
sleep 0.6
import -window root "$RESULT_DIR/smart-space-minimum-rail.png"

# Folder context actions are intentionally different: hide keeps searchable
# entries, while exclude removes the whole subtree from current/future index.
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.resizeSmartSpace 920 520 >/dev/null
gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.moveSmartSpace 50 246 >/dev/null
sleep 0.6
xdotool mousemove 350 289 click 1 key ctrl+a BackSpace Return
sleep 0.5
xdotool mousemove 200 388 click 3
sleep 0.2
xdotool key Down Down Return
sleep 0.5
grep -q 'hiddenFolders=.*Project A' "$CONFIG_DIR/kylin/ukui-fences.ini"
import -window root "$RESULT_DIR/smart-space-folder-hidden.png"
xdotool mousemove 200 388 click 3
sleep 0.2
xdotool key Down Down Down Return
sleep 0.6
grep -q 'excludedFolders=.*Project B' "$CONFIG_DIR/kylin/ukui-fences.ini"
import -window root "$RESULT_DIR/smart-space-folder-excluded.png"

python3 - "$INDEX_PATH" "$ROOT_DIR" "$RUNTIME_DIR/sidecar-before.json" \
    "$CONFIG_DIR/kylin/ukui-fences.ini" \
    "$RESULT_DIR/smart-space-initial.png" \
    "$RESULT_DIR/smart-space-type-filter.png" \
    "$RESULT_DIR/smart-space-filtered.png" \
    "$RESULT_DIR/smart-space-folder-drilldown.png" \
    "$RESULT_DIR/smart-space-hover-index.png" \
    "$RESULT_DIR/smart-space-local-search.png" \
    "$RESULT_DIR/smart-space-skill-settings.png" \
    "$RESULT_DIR/smart-space-compact-responsive.png" \
    "$RESULT_DIR/smart-space-default-hidden.png" <<'PY'
import json
import sys
from pathlib import Path
from PIL import Image, ImageStat

index = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
root = Path(sys.argv[2])
before = Path(sys.argv[3]).read_bytes()
after = (root / "Project A/.ts/alpha-roadmap.txt.json").read_bytes()
if before != after:
    raise SystemExit("existing sidecar was modified")
required = {"alpha-roadmap.txt", "budget.xlsx", "launch.pptx", "reference.pdf"}
names = {item["name"] for item in index["items"]}
if required - names:
    raise SystemExit("missing indexed fixtures: " + repr(sorted(required - names)))
if index.get("tagsEnabled") is not False or index.get("tags") != []:
    raise SystemExit("tag metadata is enabled")
if not index.get("fullRebuild") or index.get("indexMode") != "fast-full":
    raise SystemExit("fast-full snapshot was not published")
config = Path(sys.argv[4]).read_text(encoding="utf-8", errors="ignore")
if "currentSpace" in config:
    raise SystemExit("retired currentSpace setting was persisted")
if (Path(sys.argv[4]).parent.parent / "kyfences" / "smart-spaces.json").exists():
    raise SystemExit("retired smart-spaces.json was created")
images = [Image.open(path).convert("RGB") for path in sys.argv[5:]]
for image in images:
    if image.size != (1440, 900) or sum(ImageStat.Stat(image).var) < 50:
        raise SystemExit("invalid or blank screenshot")
print(json.dumps({"items": len(index["items"]),
                  "screenshots": len(images),
                  "fixedAllFilesScope": True,
                  "spacePersistenceRemoved": True,
                  "defaultHidden": True},
                 ensure_ascii=False))
PY

gdbus call --session --dest org.ukui.fences --object-path /ukuiFences \
    --method org.ukui.fences.quitApp >/dev/null
wait "$APP_PID"
APP_PID=
