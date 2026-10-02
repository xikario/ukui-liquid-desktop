#!/bin/sh
set -eu
# CTest supplies a private X server and session bus. No confirmation bypasses:
# these calls must return while their real modal dialog is still open.
runtime=$(mktemp -d /tmp/fences-dbus-dialog.XXXXXX)
app_pid=
cleanup() {
    if [ -n "$app_pid" ]; then
        kill "$app_pid" 2>/dev/null || true
        wait "$app_pid" 2>/dev/null || true
    fi
    rm -rf -- "$runtime"
}
trap cleanup EXIT INT TERM
mkdir -p "$runtime/config/kylin" "$runtime/data" "$runtime/documents"
export XDG_CONFIG_HOME="$runtime/config" XDG_CACHE_HOME="$runtime/cache"
export XDG_DATA_HOME="$runtime/data" UKUI_FENCES_SMARTSPACE_ROOTS="$runtime/documents"
unset UKUI_FENCES_TEST_CONFIRM_IDLE UKUI_FENCES_TEST_CONFIRM_EXCLUDE UKUI_FENCES_TEST_AUTO_INDEX || true
printf '%s\n' '[systemMonitor]' 'autoStart=false' '[smartSpace]' 'defaultHidden=false' \
    > "$runtime/config/kylin/ukui-fences.ini"
mkdir -p "$runtime/home"
env HOME="$runtime/home" "$1" --smart-space > "$runtime/app.log" 2>&1 &
app_pid=$!
call() {
    gdbus call --session --timeout 2 --dest org.ukui.fences --object-path /ukuiFences \
        --method "org.ukui.fences.$@"
}
i=0
until call smartSpaceVisible >/dev/null 2>&1; do
    i=$((i + 1)); [ "$i" -lt 50 ] || exit 1
    sleep .1
done
check_dialog() {
    i=0
    while ! xdotool search --onlyvisible --name "$1" >/dev/null 2>&1; do
        i=$((i + 1)); [ "$i" -lt 30 ] || exit 1
        sleep .1
    done
    call smartSpaceVisible | grep -q true
    xdotool key Escape
    sleep .2
}
call startSmartSpaceFullIndex >/dev/null
check_dialog '快速全量索引'
call smartSpaceIndexBusy | grep -q false
call excludeSmartSpaceFolder "$runtime/documents" >/dev/null
check_dialog '取消索引文件夹'
if grep -q '^excludedFolders=.*documents' "$runtime/config/kylin/ukui-fences.ini"; then
    echo 'cancelled dialog still excluded directory' >&2
    exit 1
fi
echo 'PASS: D-Bus returns before confirmation; cancelling preserves state'
