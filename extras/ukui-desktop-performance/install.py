#!/usr/bin/python3
"""Install/uninstall the narrow AC-only GPU policy; run through pkexec."""
from pathlib import Path
import json, os, shutil, subprocess, sys
BASE = Path(__file__).resolve().parent
UNIT = 'ukui-ftg340-responsiveness.service'
FILES = {
    'ftg340-responsiveness.py': Path('/usr/local/libexec/ukui-ftg340-responsiveness'),
    UNIT: Path('/etc/systemd/system')/UNIT,
    '90-ukui-ftg340-responsiveness.rules': Path('/etc/udev/rules.d/90-ukui-ftg340-responsiveness.rules'),
}
CONFIG = Path('/etc/ukui-ftg340-responsiveness.json')
def call(*args):
    subprocess.run(args, check=True)
if os.geteuid() != 0:
    raise SystemExit('Administrator authentication required')
if sys.argv[1:] == ['--remove']:
    call('systemctl', 'disable', '--now', UNIT)
    for target in FILES.values():
        target.unlink(missing_ok=True)
    CONFIG.unlink(missing_ok=True)
    call('systemctl', 'daemon-reload')
    call('udevadm', 'control', '--reload-rules')
    raise SystemExit(0)
if len(sys.argv) != 2 or sys.argv[1] not in ('600000', '800000'):
    raise SystemExit('Usage: install.py {600000|800000|--remove}')
node = Path('/sys/class/devfreq/PHYT0048:00')
if (node/'device/driver').resolve().name != 'ftg340':
    raise SystemExit('FTG340 not detected')
if int((node/'min_freq').read_text()) != 200000 and not CONFIG.exists():
    raise SystemExit('Temporary trial is still active or baseline changed; refusing to record it as default')
for source, target in FILES.items():
    if target.exists() and not CONFIG.exists():
        raise SystemExit('Unmanaged file exists: '+str(target))
for source, target in FILES.items():
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(BASE/source, target)
    target.chmod(0o755 if source.endswith('.py') else 0o644)
CONFIG.write_text(json.dumps({'ac_min_freq': int(sys.argv[1])})+'\n')
call('systemctl', 'daemon-reload')
call('udevadm', 'control', '--reload-rules')
try:
    call('systemctl', 'enable', '--now', UNIT)
    call('systemctl', 'reload', UNIT)
except Exception:
    subprocess.run(['systemctl', 'disable', '--now', UNIT])
    raise
print('Installed AC-only GPU floor:',sys.argv[1])
