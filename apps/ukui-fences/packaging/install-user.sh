#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${1:-"$PROJECT_DIR/build"}
USER_PREFIX=${UKUI_FENCES_USER_PREFIX:-"$HOME/.local"}

if [ ! -x "$BUILD_DIR/ukui-fences" ]; then
    echo "ukui-fences build not found: $BUILD_DIR/ukui-fences" >&2
    exit 2
fi

cmake --install "$BUILD_DIR" --prefix "$USER_PREFIX"

# XDG autostart is per-user and is not the same directory as prefix/etc.
# Keep it independent of the source/build tree so upgrades cannot leave a
# session entry pointing at an obsolete build-v10 executable.
install -d "$HOME/.config/autostart" "$HOME/.local/share/applications"
autostart_file="$HOME/.config/autostart/ukui-fences.desktop"
install -m 0644 "$PROJECT_DIR/packaging/ukui-fences-autostart.desktop" "$autostart_file"
# Desktop sessions do not consistently include ~/.local/bin in PATH. Resolve
# the launcher at install time so a reboot always starts this user installation.
sed -i "s|^Exec=.*|Exec=$USER_PREFIX/bin/ukui-fences-launcher --autostart|" "$autostart_file"
install -m 0644 "$PROJECT_DIR/packaging/ukui-fences.desktop" \
    "$HOME/.local/share/applications/ukui-fences.desktop"

# Some UKUI sessions expose a relative user icon search path. A resolved icon
# path lets the panel display this settings icon independently of its cwd/cache.
settings_file="$HOME/.local/share/applications/ukui-fences-settings.desktop"
awk -v launcher="$USER_PREFIX/bin/ukui-fences-launcher" \
    -v icon="$USER_PREFIX/share/icons/hicolor/scalable/apps/ukui-fences-settings.svg" \
    '/^Exec=/ { print "Exec=\"" launcher "\" --settings"; next }
     /^Icon=/ { print "Icon=" icon; next } { print }' \
    "$PROJECT_DIR/packaging/ukui-fences-settings.desktop" > "$settings_file"

# Hidden task identity: a diagnosis icon without another application grid item.
monitor_file="$HOME/.local/share/applications/ukui-fences-monitor.desktop"
awk -v launcher="$USER_PREFIX/bin/ukui-fences-launcher" \
    -v icon="$USER_PREFIX/share/icons/hicolor/scalable/apps/ukui-fences-monitor.svg" \
    '/^Exec=/ { print "Exec=\"" launcher "\" --system-monitor"; next }
     /^Icon=/ { print "Icon=" icon; next } { print }' \
    "$PROJECT_DIR/packaging/ukui-fences-monitor.desktop" > "$monitor_file"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1 || true
fi

echo "Installed ukui-fences to $USER_PREFIX"
echo "Autostart: $HOME/.config/autostart/ukui-fences.desktop"
