#!/usr/bin/env python3
"""Install an opt-in user session entry; never overwrite packaged binaries."""
from pathlib import Path
import shutil, datetime, json, hashlib
root=Path(__file__).resolve().parents[1]
home=Path.home()
release=root/'releases'/datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
release.mkdir(parents=True)
paths=[home/'.config/autostart/ukui-panel.desktop',home/'.local/bin/ukui-panel-liquid',home/'.local/lib/ukui-panel-liquid/plugins/styles/libukuiliquidpanel.so']
paths += [home/'.config/autostart/ukui-panel-liquid-session.desktop', home/'.local/bin/ukui-panel-liquid-session']
for dest in paths:
    if dest.exists(): shutil.copy2(dest,release/dest.name)
for name in ['panel.conf','panel-commission.ini','liquid-panel.ini']:
    p=home/'.config/ukui'/name
    if p.exists():shutil.copy2(p,release/name)
for source,dest in [(root/'build/plugins/styles/libukuiliquidpanel.so',paths[2]),(root/'scripts/ukui-panel-liquid',paths[1]),(root/'scripts/session-start.py',paths[4])]:
    dest.parent.mkdir(parents=True,exist_ok=True)
    staging=dest.with_name(dest.name+'.new');shutil.copy2(source,staging);staging.chmod(0o755);staging.replace(dest)
system=Path('/etc/xdg/autostart/ukui-panel.desktop')
text=system.read_text().replace('Exec=ukui-panel','Exec='+str(paths[1]))
text+='\nX-LiquidPanel-Managed=true\n'
shutil.copy2(root/'scripts/restore.py',home/'.local/bin/ukui-panel-liquid-restore')
(home/'.local/bin/ukui-panel-liquid-restore').chmod(0o755)
paths[0].parent.mkdir(parents=True,exist_ok=True);paths[0].write_text(text)
paths[3].write_text('[Desktop Entry]\nType=Application\nName=液态面板登录恢复\n'
    +'Exec="'+str(paths[4])+'"\nTerminal=false\nOnlyShowIn=UKUI;\nNoDisplay=true\n'
    +'X-UKUI-Autostart-Phase=Application\nX-UKUI-AutoRestart=false\nX-LiquidPanel-Managed=true\n')
manifest={'backup':str(release),'installed':[str(p) for p in paths], 'system_binary_sha256':hashlib.sha256(Path('/usr/bin/ukui-panel').read_bytes()).hexdigest()}
(release/'installed.json').write_text(json.dumps(manifest,indent=2))
print(json.dumps(manifest,indent=2))
