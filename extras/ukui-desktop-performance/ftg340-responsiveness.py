#!/usr/bin/python3
"""AC-only FTG340 frequency floor for responsive desktop animations."""
import fcntl
import json
import os
from pathlib import Path
import sys

NODE = Path('/sys/class/devfreq/PHYT0048:00')
POWER = Path('/sys/class/power_supply')
CONFIG = Path('/etc/ukui-ftg340-responsiveness.json')
STATE = Path('/var/lib/ukui-ftg340-responsiveness/original.json')
MARKER = Path('/run/ukui-ftg340-responsiveness.active')
LOCK = Path('/run/ukui-ftg340-responsiveness.lock')


def ac_online(power):
    for source in power.iterdir():
        try:
            if (source/'type').read_text().strip() == 'Mains' and (source/'online').read_text().strip() == '1':
                return True
        except OSError:
            continue
    return False


def desired_floor(original, requested, maximum, frequencies, on_ac):
    if not on_ac:
        return original
    eligible = [f for f in frequencies if f <= min(requested, maximum)]
    return max(original, max(eligible, default=original))


def write_floor(value):
    parameter = NODE/'min_freq'
    if int(parameter.read_text()) != value:
        parameter.write_text(str(value)+'\n')
    if int(parameter.read_text()) != value:
        raise RuntimeError('Driver did not accept the requested frequency floor')


def run(action):
    if (NODE/'device/driver').resolve().name != 'ftg340':
        raise RuntimeError('Expected FTG340 device is not available')
    if action == 'stop':
        MARKER.unlink(missing_ok=True)
        if STATE.exists():
            write_floor(int(json.loads(STATE.read_text())['min_freq']))
        return
    if action == 'apply' and not MARKER.exists():
        return  # A udev event must not reactivate a stopped service.
    if (NODE/'governor').read_text().strip() != 'simple_ondemand':
        print('Skipping: another GPU governor is selected')
        return
    config = json.loads(CONFIG.read_text())
    if not STATE.exists():
        STATE.parent.mkdir(parents=True, exist_ok=True)
        STATE.write_text(json.dumps({'min_freq': int((NODE/'min_freq').read_text())})+'\n')
    original = int(json.loads(STATE.read_text())['min_freq'])
    on_ac = ac_online(POWER)
    value = desired_floor(original, int(config['ac_min_freq']), int((NODE/'max_freq').read_text()),
                          [int(f) for f in (NODE/'available_frequencies').read_text().split()], on_ac)
    try:
        write_floor(value)
        if action == 'start':
            MARKER.touch()
    except Exception:
        if action == 'start':
            write_floor(original)
        raise
    print('AC={} min_freq={} (original={})'.format(on_ac, value, original), flush=True)


def main():
    if os.geteuid() != 0:
        raise SystemExit('Administrator authentication required')
    if len(sys.argv) != 2 or sys.argv[1] not in ('start', 'apply', 'stop'):
        raise SystemExit('Usage: ftg340-responsiveness {start|apply|stop}')
    with LOCK.open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        run(sys.argv[1])


if __name__ == '__main__':
    main()
