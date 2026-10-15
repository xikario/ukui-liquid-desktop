#!/usr/bin/env python3
"""Route this user's native UKUI menu startup/activation to Liquid V2."""
import argparse
import datetime
import os
from pathlib import Path
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix', type=Path, default=Path.home() / '.local')
args = parser.parse_args()
launcher = args.prefix.resolve() / 'bin/ukui-kaishicaidan-launcher'
if not launcher.is_file() or not os.access(str(launcher), os.X_OK):
    parser.error('Install the Liquid launcher before configuring menu routing')
config = Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config'))
data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
source = Path(__file__).resolve().parent
backup = data / 'ukui-kaishicaidan/menu-routing-backups' / datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')
backup.mkdir(parents=True)
targets = {
    config / 'autostart/org.ukui.menu.desktop': (source / 'org.ukui.menu.desktop').read_text(),
    data / 'dbus-1/services/org.ukui.menu.service': (source / 'org.ukui.menu.service.in').read_text().replace(
        '@LAUNCHER@', '"' + str(launcher).replace('\\', '\\\\').replace('"', '\\"') + '"'),
}
for index, (target, contents) in enumerate(targets.items()):
    if target.exists():
        shutil.copy2(target, backup / (str(index) + '-' + target.name))
    (backup / (str(index) + '-target.txt')).write_text(str(target) + '\n')
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(contents)
try:
    subprocess.run(['gdbus', 'call', '--session', '--timeout', '5',
                    '--dest', 'org.freedesktop.DBus', '--object-path', '/org/freedesktop/DBus',
                    '--method', 'org.freedesktop.DBus.ReloadConfig'],
                   check=True, timeout=6, stdout=subprocess.DEVNULL)
except (OSError, subprocess.SubprocessError):
    print('Could not reload the session bus. Log out/in to refresh D-Bus activation routing.')
print('Menu routing installed. Backup/target paths:', backup)
print('Stop session module org.ukui.menu.desktop, then restart Liquid V2 to claim org.ukui.menu.')
