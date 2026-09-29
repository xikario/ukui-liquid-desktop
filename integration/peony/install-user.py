#!/usr/bin/env python3
from pathlib import Path
import shutil
import subprocess
source=Path(__file__).with_name('blur_compat.py').resolve()
target=Path.home()/'.local/libexec/ukui-liquid-desktop/peony-blur-compat.py'
target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
service=Path.home()/'.config/systemd/user/ukui-peony-blur-compat.service'
service.parent.mkdir(parents=True,exist_ok=True)
service.write_text('[Unit]\nDescription=UKUI system applications X11 blur animation compatibility\nAfter=graphical-session.target\nPartOf=graphical-session.target\n\n[Service]\nExecStart=/usr/bin/python3 '+str(target)+'\nRestart=on-failure\nRestartSec=3\n\n[Install]\nWantedBy=graphical-session.target\n')
subprocess.run(['systemctl','--user','import-environment','DISPLAY','XAUTHORITY'],check=True)
subprocess.run(['systemctl','--user','daemon-reload'],check=True)
subprocess.run(['systemctl','--user','enable','--now',service.name],check=True)
subprocess.run(['systemctl','--user','restart',service.name],check=True)
print('Installed:',service)
