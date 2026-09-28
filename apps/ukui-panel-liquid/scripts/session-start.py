#!/usr/bin/env python3
"""One-shot login repair for OEM UKUI which ignores required-panel overrides."""
from pathlib import Path
import fcntl
import os
import subprocess
import time


def panels():
    result = {}
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if proc.stat().st_uid != os.getuid() or os.readlink(proc/'exe') != '/usr/bin/ukui-panel':
                continue
            environment = (proc/'environ').read_bytes().split(b'\0')
            if ('DISPLAY='+os.environ.get('DISPLAY', '')).encode() not in environment:
                continue
            result[int(proc.name)] = 'libukuiliquidpanel.so' in (proc/'maps').read_text()
        except (OSError, ProcessLookupError):
            continue
    return result


def wait_for(predicate, seconds):
    deadline = time.monotonic()+seconds
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(.25)
    return bool(predicate())


def ensure_panel(log):
    home = Path.home()
    launcher = home/'.local/bin/ukui-panel-liquid'
    plugin = home/'.local/lib/ukui-panel-liquid/plugins/styles/libukuiliquidpanel.so'
    if not launcher.is_file() or not plugin.is_file():
        raise RuntimeError('Launcher or plugin missing; original panel left untouched')
    # Application-phase startup normally follows Panel. Also tolerate a slow
    # session startup without launching a competing instance immediately.
    wait_for(lambda: bool(panels()), 20)
    time.sleep(1)
    current = panels()
    if any(current.values()):
        print('Liquid plugin already loaded; no change.', file=log, flush=True)
        return
    if current:
        print('Stopping native session panel: '+str(list(current)), file=log, flush=True)
        subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.gnome.SessionManager',
                        '--object-path', '/org/gnome/SessionManager', '--method',
                        'org.gnome.SessionManager.stopModule', 'ukui-panel.desktop'],
                       stdout=log, stderr=log, timeout=10, check=True)
        # Do not forcibly kill an unmanaged panel or create a restart race.
        if not wait_for(lambda: not panels(), 8):
            raise RuntimeError('Session panel did not stop; no duplicate launched')
    child = subprocess.Popen([str(launcher)], stdin=subprocess.DEVNULL,
                             stdout=log, stderr=log, start_new_session=True)
    if wait_for(lambda: any(panels().values()), 15):
        print('Liquid plugin loaded; PID '+str(child.pid), file=log, flush=True)
        return
    # A missing/unloadable plugin must not leave the desktop without a panel.
    if child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=5)
        except subprocess.TimeoutExpired:
            raise RuntimeError('Panel still running without plugin; inspect log')
    if not panels():
        subprocess.Popen(['/usr/bin/ukui-panel'], stdin=subprocess.DEVNULL,
                         stdout=log, stderr=log, start_new_session=True)
    raise RuntimeError('Liquid plugin failed to load; native panel retained/restored')


def main():
    runtime = Path(os.environ.get('XDG_RUNTIME_DIR', '/run/user/'+str(os.getuid())))
    with (runtime/'ukui-panel-liquid-start.lock').open('w') as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return
        logdir = Path.home()/'.local/state/ukui-panel-liquid'
        logdir.mkdir(parents=True, exist_ok=True)
        with (logdir/'session-start.log').open('w', buffering=1) as log:
            print(time.strftime('%Y-%m-%d %H:%M:%S'), file=log)
            try:
                ensure_panel(log)
            except Exception as error:
                print('ERROR: '+str(error), file=log)
                raise


if __name__ == '__main__':
    main()
