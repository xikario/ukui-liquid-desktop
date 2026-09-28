#!/usr/bin/env python3
"""Restore original startup; --restart also switches the running panel."""
from pathlib import Path
import subprocess,sys,time,os,signal
home=Path.home()
entries=[home/'.config/autostart'/name for name in ['ukui-panel.desktop','ukui-panel-liquid-session.desktop']]
for entry in entries:
    if entry.exists() and 'X-LiquidPanel-Managed=true' not in entry.read_text():
        raise SystemExit('Refusing to remove an unrelated autostart entry: '+str(entry))
for entry in entries:
    if entry.exists():entry.unlink()
if '--restart' in sys.argv:
    for name in ['ukui-panel','ukui-panel.desktop']:
        subprocess.run(['gdbus','call','--session','--dest','org.gnome.SessionManager','--object-path','/org/gnome/SessionManager','--method','org.gnome.SessionManager.stopModule',name],timeout=10,check=False)
    time.sleep(1)
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():continue
        try:
            if os.readlink(str(proc/'exe'))=='/usr/bin/ukui-panel' and 'libukuiliquidpanel.so' in (proc/'maps').read_text():os.kill(int(proc.name),signal.SIGTERM)
        except OSError:pass
    time.sleep(1)
    subprocess.Popen(['/usr/bin/ukui-panel'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,start_new_session=True)
print('Original panel startup restored.')
