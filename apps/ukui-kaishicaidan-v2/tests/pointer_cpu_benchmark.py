#!/usr/bin/env python3
"""Compare binaries in an isolated Xvfb/session bus, never the personal desktop.

Example from repository root:
  xvfb-run -a -s '-screen 0 1920x1200x24' dbus-run-session -- \
    python3 apps/ukui-kaishicaidan-v2/tests/pointer_cpu_benchmark.py \
    /path/to/before /path/to/after --output /tmp/launcher-pointer-cpu.json
"""
import argparse
import ctypes
import json
import math
import os
from pathlib import Path
import subprocess
import tempfile
import time


def ticks(pid):
    values = Path('/proc/%d/stat' % pid).read_text().rsplit(')', 1)[1].split()
    return int(values[11]) + int(values[12])  # utime + stime, including all threads


def bus(method):
    subprocess.run(['gdbus', 'call', '--session', '--timeout', '3',
        '--dest', 'org.ukui.kaishicaidan.v2', '--object-path', '/ukuiKaishicaidanV2',
        '--method', 'org.ukui.kaishicaidan.v2.' + method],
        check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)


def benchmark(binary, dpy):
    with tempfile.TemporaryDirectory(prefix='launcher-pointer-cpu-') as temporary:
        root = Path(temporary)
        for name in ['home', 'config', 'data/applications', 'cache', 'runtime']:
            (root / name).mkdir(parents=True, exist_ok=True)
        (root / 'runtime').chmod(0o700)
        config = root / 'config/ukui-kaishicaidan-v2'
        config.mkdir()
        (config / 'settings.conf').write_text('[General]\nskin=5\n')
        pinned = []
        for i in range(18):
            app = root / ('data/applications/fixture-%d.desktop' % i)
            app.write_text('[Desktop Entry]\nType=Application\nName=Fixture %d\nExec=/bin/true\nIcon=utilities-terminal\n' % i)
            pinned.append(str(app))
        (config / 'pinned.conf').write_text('\n'.join(pinned) + '\n')
        env = os.environ.copy()
        env.update(HOME=str(root / 'home'), XDG_CONFIG_HOME=str(root / 'config'),
            XDG_DATA_HOME=str(root / 'data'), XDG_DATA_DIRS=str(root / 'data'),
            XDG_CACHE_HOME=str(root / 'cache'), XDG_RUNTIME_DIR=str(root / 'runtime'),
            QT_FONT_DPI='96', QT_SCALE_FACTOR='1.5', QT_SCREEN_SCALE_FACTORS='1', QT_STYLE_OVERRIDE='Fusion',
            GSETTINGS_SCHEMA_DIR='/usr/share/glib-2.0/schemas',
            KAISHICAIDAN_GLASS_FAST='1', UKUI_LIQUID_GLASS_NO_GL='1')
        with (root / 'process.log').open('wb') as log:
            proc = subprocess.Popen([str(binary)], env=env, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 8
                while True:
                    if proc.poll() is not None:
                        raise RuntimeError('isolated menu exited: ' + str(proc.returncode) + '\n' + (root / 'process.log').read_text(errors='replace')[-2500:])
                    try:
                        bus('hideMenu')
                        break
                    except (subprocess.CalledProcessError, subprocess.TimeoutExpired):
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(0.05)
                time.sleep(1.2)
                hz = os.sysconf('SC_CLK_TCK')

                def sample(seconds, motion=None):
                    before = ticks(proc.pid)
                    started = time.monotonic()
                    count = 0
                    while time.monotonic() - started < seconds:
                        if motion:
                            x, y = motion(time.monotonic() - started)
                            dpy.move(int(x), int(y))
                            count += 1
                        time.sleep(1 / 120)
                    elapsed = time.monotonic() - started
                    return {'cpuOneCorePercent': round((ticks(proc.pid) - before) / hz / elapsed * 100, 2),
                            'seconds': round(elapsed, 3), 'motionEvents': count}

                results = {'closed': sample(2)}
                bus('showMenu')
                time.sleep(1.2)
                candidates = subprocess.check_output(
                    ['xdotool', 'search', '--onlyvisible', '--pid', str(proc.pid)], text=True).split()
                geometry = None
                for window in candidates:
                    output = subprocess.check_output(['xdotool', 'getwindowgeometry', '--shell', window], text=True)
                    values = dict(line.split('=', 1) for line in output.splitlines() if '=' in line)
                    if int(values['WIDTH']) > 500 and int(values['HEIGHT']) > 500:
                        geometry = values
                        break
                if geometry is None:
                    raise RuntimeError('visible menu window not found')
                scale = int(geometry['WIDTH']) / 680
                origin_x, origin_y = int(geometry['X']), int(geometry['Y'])
                results['openStill'] = sample(2)

                def motion(t):
                    section = int(t) % 3
                    if section == 0:  # search capsule
                        x, y = 130 + 480 * (0.5 + 0.5 * math.sin(t * 15)), 52
                    elif section == 1:  # rail
                        x, y = 32 + 18 * math.sin(t * 11), 70 + 570 * (0.5 + 0.5 * math.sin(t * 8))
                    else:  # application grid
                        x, y = 130 + 470 * (0.5 + 0.5 * math.sin(t * 14)), 150 + 230 * (0.5 + 0.5 * math.sin(t * 10))
                    return origin_x + x * scale, origin_y + y * scale

                results['openMoving'] = sample(6, motion)
                time.sleep(1.2)
                results['openAfterSettled'] = sample(2)
                bus('hideMenu')
                time.sleep(0.2)
                results['closedAgain'] = sample(2)
                bus('quitApp')
                proc.wait(timeout=8)
                return results
            finally:
                if proc.poll() is None:
                    # Only the disposable test process, never a session app.
                    proc.terminate()
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.wait()


class Pointer:
    def __init__(self):
        self.x11 = ctypes.CDLL('libX11.so.6')
        self.x11.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.x11.XOpenDisplay.restype = ctypes.c_void_p
        self.x11.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        self.x11.XDefaultRootWindow.restype = ctypes.c_ulong
        self.x11.XWarpPointer.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong,
                                         ctypes.c_int, ctypes.c_int, ctypes.c_uint, ctypes.c_uint,
                                         ctypes.c_int, ctypes.c_int]
        self.x11.XFlush.argtypes = [ctypes.c_void_p]
        self.x11.XCloseDisplay.argtypes = [ctypes.c_void_p]
        self.display = self.x11.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError('cannot connect to isolated X server')
        self.root = self.x11.XDefaultRootWindow(self.display)

    def move(self, x, y):
        self.x11.XWarpPointer(self.display, 0, self.root, 0, 0, 0, 0, x, y)
        self.x11.XFlush(self.display)

    def close(self):
        self.x11.XCloseDisplay(self.display)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binaries', nargs='+', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not os.environ.get('DISPLAY', '').startswith(':'):
        raise SystemExit('Run inside a disposable Xvfb and dbus-run-session')
    # Explicit guard against using the host session bus by mistake.
    name = Path('/proc/%d/exe' % os.getppid()).resolve().name
    if name != 'dbus-run-session':
        raise SystemExit('Run directly under dbus-run-session in an isolated Xvfb')
    dpy = Pointer()
    try:
        result = {'scope': 'isolated Xvfb, 150%, CPU cached material, 120 Hz scripted pointer, one-core process CPU',
                  'binaries': {}}
        for binary in args.binaries:
            result['binaries'][str(binary.resolve())] = benchmark(binary.resolve(), dpy)
        args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps(result, ensure_ascii=False, indent=2))
    finally:
        dpy.close()


if __name__ == '__main__':
    main()
