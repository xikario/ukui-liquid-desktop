"""Start through a real private session-bus service, before any menu exists."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


def call(service, path, method, *args):
    return subprocess.check_output(['gdbus', 'call', '--session', '--timeout', '10',
                                    '--dest', service, '--object-path', path,
                                    '--method', method, *args], text=True)


def visible():
    tree = subprocess.check_output(['xwininfo', '-root', '-tree'], text=True)
    for line in tree.splitlines():
        if '"开始菜单"' in line and 'ukui-kaishicaidan-v2' in line:
            window = re.search(r'0x[0-9a-f]+', line)[0]
            return 'IsViewable' in subprocess.check_output(['xwininfo', '-id', window], text=True)
    return False


def wait_for(predicate, label):
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        if predicate():
            print('PASS:', label, flush=True)
            return
        time.sleep(.05)
    raise AssertionError(label)


def child():
    own = ('org.ukui.kaishicaidan.v2', '/ukuiKaishicaidanV2')
    try:
        assert 'false' in call('org.freedesktop.DBus', '/org/freedesktop/DBus',
                               'org.freedesktop.DBus.NameHasOwner', 'org.ukui.menu')
        call('org.ukui.menu', '/org/ukui/menu', 'org.ukui.menu.WinKeyResponse')
        wait_for(visible, 'first native WinKeyResponse cold activation shows menu')
        call(*own, 'org.ukui.kaishicaidan.v2.hideMenu')
        wait_for(lambda: not visible(), 'hide after cold activation')
        # This is an independent native-only request, beyond the documented
        # 500 ms cross-connection deduplication window. Delayed duplicates
        # inside that window are checked by button-follow-test.py.
        time.sleep(.6)
        call('org.ukui.menu', '/org/ukui/menu', 'org.ukui.menu.WinKeyResponse')
        wait_for(visible, 'native-only request still works without recorded gesture')
    finally:
        call(*own, 'org.ukui.kaishicaidan.v2.quitApp')


if '--child' in sys.argv:
    child()
else:
    with tempfile.TemporaryDirectory(prefix='menu-cold-') as tmp:
        root = Path(tmp)
        prefix = root / 'prefix'
        (prefix / 'bin').mkdir(parents=True)
        binary = prefix / 'bin/ukui-kaishicaidan-v2'
        binary.symlink_to(Path(os.environ['UKUI_KAISHICAIDAN_V2_BIN']).resolve())
        launcher = prefix / 'bin/ukui-kaishicaidan-launcher'
        packaging = Path(__file__).resolve().parents[1] / 'packaging'
        shutil.copy2(packaging / launcher.name, launcher)
        launcher.chmod(0o755)
        services = root / 'data/dbus-1/services'
        services.mkdir(parents=True)
        (services / 'org.ukui.menu.service').write_text(
            '[D-BUS Service]\nName=org.ukui.menu\nExec="' + str(launcher) + '" --autostart\n')
        env = dict(os.environ, XDG_CONFIG_HOME=str(root / 'config'),
                   XDG_DATA_HOME=str(root / 'data'), XDG_CACHE_HOME=str(root / 'cache'),
                   GSETTINGS_SCHEMA_DIR=os.environ.get('GSETTINGS_SCHEMA_DIR', '/usr/share/glib-2.0/schemas'))
        subprocess.run(['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve()), '--child'],
                       env=env, check=True, timeout=30)
