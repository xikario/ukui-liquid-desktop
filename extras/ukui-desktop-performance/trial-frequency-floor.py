#!/usr/bin/python3
"""Temporarily test the saved OEM FTG340 frequency floor, then restore it.

Does not change persistent policy, governor, maximum, or polling interval.
Run in a local terminal with sudo; Ctrl+C also restores the original floor.
"""
import argparse
import json
import os
from pathlib import Path
import signal
import time

NODE = Path('/sys/class/devfreq/PHYT0048:00')
STATE = Path('/var/lib/ukui-ftg340-responsiveness/original.json')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=480)
    args = parser.parse_args()
    if not 30 <= args.seconds <= 900:
        parser.error('test duration must be 30–900 seconds')
    if os.geteuid() != 0:
        raise SystemExit('Administrator authentication required: run with sudo in your terminal.')
    if (NODE / 'device/driver').resolve().name != 'ftg340':
        raise SystemExit('Expected FTG340 device not found')
    if (NODE / 'governor').read_text().strip() != 'simple_ondemand':
        raise SystemExit('Expected simple_ondemand governor; no change made')
    saved = int(json.loads(STATE.read_text())['min_freq'])
    previous = int((NODE / 'min_freq').read_text())
    maximum = int((NODE / 'max_freq').read_text())
    available = list(map(int, (NODE / 'available_frequencies').read_text().split()))
    if saved not in available or not 0 < saved < previous <= maximum:
        raise SystemExit('Saved floor is not a supported lower setting; no change made')
    stopped = False
    def stop(signum, frame):
        nonlocal stopped
        stopped = True
    for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
        signal.signal(sig, stop)
    try:
        (NODE / 'min_freq').write_text(str(saved) + '\n')
        if int((NODE / 'min_freq').read_text()) != saved:
            raise RuntimeError('Driver rejected the test floor')
        print(f'Temporary GPU floor: {previous} -> {saved}. Restore in {args.seconds}s, or Ctrl+C.', flush=True)
        deadline = time.monotonic() + args.seconds
        while not stopped and time.monotonic() < deadline:
            time.sleep(1)
            current = int((NODE / 'min_freq').read_text())
            if current != saved:
                print('Another power-policy event changed the floor; ending the test.', flush=True)
                break
    finally:
        current = int((NODE / 'min_freq').read_text())
        if current == saved:
            (NODE / 'min_freq').write_text(str(previous) + '\n')
            if int((NODE / 'min_freq').read_text()) != previous:
                raise RuntimeError('Could not restore original frequency floor')
            print(f'Restored GPU floor: {previous}. Persistent policy unchanged.', flush=True)
        else:
            print(f'Preserved concurrent power-policy setting: {current}.', flush=True)


if __name__ == '__main__':
    main()
