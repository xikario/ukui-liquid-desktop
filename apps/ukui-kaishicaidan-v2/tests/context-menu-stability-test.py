#!/usr/bin/python3
"""Run with Xvfb + dbus-run-session; watches the real async app context menu."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

app_root = Path(__file__).resolve().parents[1]
binary = Path(os.environ.get('UKUI_KAISHICAIDAN_V2_BIN', app_root/'build-v2/ukui-kaishicaidan-v2'))
artifacts = app_root/'build-v2/artifacts'
artifacts.mkdir(exist_ok=True)
scale = os.environ.get('QT_SCALE_FACTOR', '1')
evidence_name = os.environ.get('CONTEXT_TEST_EVIDENCE_PREFIX', 'context-stability')+'-'+scale

def geometry(wid):
    fields = dict(line.split('=', 1) for line in subprocess.check_output(
        ['xdotool', 'getwindowgeometry', '--shell', wid], text=True).splitlines() if '=' in line)
    return {key: int(fields[key]) for key in ('X', 'Y', 'WIDTH', 'HEIGHT')}

def visible_windows(pid):
    result = subprocess.run(['xdotool', 'search', '--all', '--onlyvisible', '--pid', str(pid)],
                            capture_output=True, text=True)
    return [(wid, geometry(wid)) for wid in result.stdout.split()] if result.returncode == 0 else []

def dbus(method):
    subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.ukui.kaishicaidan.v2',
        '--object-path', '/ukuiKaishicaidanV2', '--method', 'org.ukui.kaishicaidan.v2.'+method],
        stdout=subprocess.DEVNULL, check=True, timeout=5)

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    applications = root/'data/applications'
    applications.mkdir(parents=True)
    desktop = applications/'context-menu-stability-fixture.desktop'
    desktop.write_text('[Desktop Entry]\nType=Application\nName=Context Menu Fixture\n'
                       'Exec=context-menu-fixture-no-command\nIcon=applications-system\n')
    config = root/'config/ukui-kaishicaidan-v2'
    config.mkdir(parents=True)
    (config/'pinned.conf').write_text(str(desktop)+'\n')
    (config/'settings.conf').write_text('[General]\nfontSize=14\nskin=5\n')
    fake_bin = root/'bin'
    fake_bin.mkdir()
    query = fake_bin/'dpkg-query'
    query.write_text('#!/bin/sh\n'
                     'if [ "$1" = "-S" ]; then\n'
                     '  : > "$CONTEXT_LOOKUP_STARTED"\n'
                     '  sleep 0.6\n'
                     '  : > "$CONTEXT_LOOKUP_FINISHED"\n'
                     'fi\nexit 1\n')
    query.chmod(0o700)
    env = os.environ.copy()
    env.update(XDG_CONFIG_HOME=str(root/'config'), XDG_DATA_HOME=str(root/'data'),
               XDG_CACHE_HOME=str(root/'cache'), PATH=str(fake_bin)+os.pathsep+env['PATH'],
               KAISHICAIDAN_GLASS_FAST='1', CONTEXT_LOOKUP_STARTED=str(root/'started'),
               CONTEXT_LOOKUP_FINISHED=str(root/'finished'))
    log_path = artifacts/(evidence_name+'.log')
    with log_path.open('w') as log:
        app = subprocess.Popen([str(binary)], env=env, stdout=log, stderr=log)
        try:
            time.sleep(.8)
            dbus('showMenu')
            deadline = time.monotonic()+6
            main = None
            while time.monotonic() < deadline:
                main = next(((wid, rect) for wid, rect in visible_windows(app.pid)
                             if rect['WIDTH'] > 500 and rect['HEIGHT'] > 500), None)
                if main:
                    break
                time.sleep(.03)
            assert main, 'Main menu did not appear.'
            # The sole pinned fixture occupies the first standard application tile.
            native_scale = main[1]['WIDTH']/680
            x = main[1]['X']+round(137*native_scale)
            y = main[1]['Y']+round(162*native_scale)
            subprocess.run(['xdotool', 'mousemove', str(x), str(y), 'click', '3'], check=True)
            started = time.monotonic()
            samples = []
            popup_id = None
            while time.monotonic()-started < 1.7:
                popup = next(((wid, rect) for wid, rect in visible_windows(app.pid)
                              if wid != main[0] and rect['WIDTH'] > 100 and rect['HEIGHT'] > 100), None)
                if popup:
                    popup_id = popup[0]
                    samples.append({'ms': round((time.monotonic()-started)*1000),
                                    'lookup_finished': (root/'finished').exists(),
                                    'geometry': popup[1]})
                time.sleep(.01)
            assert popup_id and len(samples) > 10, 'Async popup was not observed.'
            assert (root/'started').exists() and (root/'finished').exists(), 'Source lookup did not complete.'
            assert any(not sample['lookup_finished'] for sample in samples), 'Missed the pending phase.'
            assert any(sample['lookup_finished'] for sample in samples), 'Missed the result phase.'
            unique = {tuple(sample['geometry'].values()) for sample in samples}
            evidence = {'scale': scale, 'samples': samples, 'unique_geometries': len(unique)}
            (artifacts/(evidence_name+'.json')).write_text(json.dumps(evidence, indent=2))
            if shutil.which('import'):
                subprocess.run(['import', '-window', popup_id,
                                str(artifacts/(evidence_name+'.png'))], check=True)
            assert len(unique) == 1, 'The real menu resized or moved during installation-source lookup.'
            print('PASS: pending and final native menu geometry stays fixed at scale', scale,
                  'for', len(samples), 'samples', flush=True)
            # A click outside the popup closes it without selecting an action.
            subprocess.run(['xdotool', 'mousemove', str(main[1]['X']+80),
                            str(main[1]['Y']+80), 'click', '1'], check=True)
            dbus('quitApp')
            assert app.wait(timeout=5) == 0
        except Exception:
            print(log_path.read_text()[-4000:], flush=True)
            raise
        finally:
            if app.poll() is None:
                app.terminate()
                app.wait(timeout=5)
