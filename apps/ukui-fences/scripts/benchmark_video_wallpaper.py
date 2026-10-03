#!/usr/bin/env python3
"""Read-only X11 wallpaper CPU/RSS sampler; never changes the desktop.

CPU is normalized to all logical CPUs. The desktop group includes shared
compositor/display-driver work, so it is not a causal wallpaper-only metric.
"""
import argparse
import ast
import json
import math
import os
from pathlib import Path
import subprocess
import time


def playback_status():
    try:
        output = subprocess.check_output([
            'gdbus', 'call', '--session', '--timeout', '2', '--dest', 'org.ukui.fences',
            '--object-path', '/ukuiFences', '--method',
            'org.ukui.fences.videoWallpaperTrialStatus'], text=True,
            stderr=subprocess.DEVNULL, timeout=3)
        return json.loads(ast.literal_eval(output)[0])
    except (OSError, ValueError, subprocess.SubprocessError):
        return {'state': 'unavailable'}


def snapshot():
    total = list(map(int, Path('/proc/stat').read_text().splitlines()[0].split()[1:9]))
    processes = {}
    names = {'ukui-fences', 'ukui-kwin_x11', 'Xorg', 'python3', 'ftg340_deamon/0-0'}
    for path in Path('/proc').iterdir():
        if not path.name.isdigit():
            continue
        try:
            name = (path / 'comm').read_text().strip()
            if name not in names:
                continue
            command = (path / 'cmdline').read_bytes()
            if name == 'python3' and not any(token in command for token in
                    (b'ukui-fences-video-trial', b'video_wallpaper_trial.py')):
                continue
            fields = (path / 'stat').read_text().rsplit(')', 1)[1].split()
            processes[path.name] = {
                'name': name, 'start': fields[19],
                'ticks': int(fields[11]) + int(fields[12]),
                'rssMiB': int(fields[21]) * os.sysconf('SC_PAGE_SIZE') / 1048576}
        except (OSError, ValueError, IndexError):
            continue
    return time.monotonic(), total, processes


def distribution(values):
    if not values:
        return None
    ordered = sorted(values)
    return {'mean': sum(values) / len(values),
            'p95': ordered[max(0, math.ceil(len(values) * .95) - 1)],
            'max': max(values)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=int, default=60)
    parser.add_argument('--warmup', type=int, default=5)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.seconds <= 0 or not 0 <= args.warmup < args.seconds:
        parser.error('require seconds > warmup >= 0')
    cpus = os.cpu_count() or 1
    before = playback_status()
    previous = snapshot()
    samples = []
    for i in range(args.seconds):
        time.sleep(1)
        current = snapshot()
        elapsed = current[0] - previous[0]
        total = sum(current[1]) - sum(previous[1])
        idle = sum(current[1][3:5]) - sum(previous[1][3:5])
        processes = {}
        for pid, value in current[2].items():
            old = previous[2].get(pid)
            # No lifetime CPU charge when a process appears or a PID is reused.
            ticks = value['ticks'] - old['ticks'] if old and old['start'] == value['start'] else 0
            processes[pid] = {'name': value['name'], 'rssMiB': value['rssMiB'],
                'cpuMachine': max(0, ticks) / os.sysconf('SC_CLK_TCK') / elapsed * 100 / cpus}
        status = playback_status()
        sample = {'seconds': elapsed, 'playback': status, 'processes': processes,
                  'desktopCpuMachine': sum(p['cpuMachine'] for p in processes.values()),
                  'systemCpuMachine': 100 * (1 - idle / total) if total else 0}
        samples.append(sample)
        previous = current
        if (i + 1) % 30 == 0:
            print(json.dumps({'sample': i + 1, 'state': status.get('state'),
                              'desktopCpuMachine': sample['desktopCpuMachine']}), flush=True)
    usable = samples[args.warmup:]
    states = sorted({s['playback'].get('state', 'unknown') for s in usable})
    summary = {state: {'samples': sum(s['playback'].get('state') == state for s in usable),
        'desktopCpuMachine': distribution([s['desktopCpuMachine'] for s in usable
                                           if s['playback'].get('state') == state])}
        for state in states}
    record = {'cpus': cpus, 'warmupSamples': args.warmup, 'before': before,
              'after': playback_status(), 'byState': summary, 'samples': samples}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2))
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
