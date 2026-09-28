#!/usr/bin/env python3
"""Run the installed OEM panel on a separate display/bus/config, never the desktop."""
import os, pathlib, subprocess, tempfile, time, signal, shutil
root=pathlib.Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='liquid-panel-oem-') as temp:
    home=pathlib.Path.home()
    for name in ['config/ukui','cache','data','log']: (pathlib.Path(temp)/name).mkdir(parents=True,exist_ok=True)
    for name in ['panel.conf','panel-commission.ini']:
        source=home/'.config/ukui'/name
        if source.exists(): shutil.copy2(source,pathlib.Path(temp)/'config/ukui'/name)
    env=os.environ.copy()
    env.update(XDG_CONFIG_HOME=str(home/'.config'),XDG_CACHE_HOME=str(home/'.cache'),XDG_DATA_HOME=temp+'/data',GSETTINGS_BACKEND='memory',QT_PLUGIN_PATH=str(root/'build/plugins'))
    logpath=root/'artifacts/oem-smoke.log'
    with logpath.open('w') as log:
        command=['bwrap','--die-with-parent','--ro-bind','/','/','--dev-bind','/dev','/dev','--proc','/proc','--bind','/tmp','/tmp',
            '--bind',temp+'/config',str(home/'.config'),'--bind',temp+'/cache',str(home/'.cache'),
            '--bind',temp+'/log',str(home/'.log'),'/usr/bin/ukui-panel','-style','ukuiliquidpanel']
        p=subprocess.Popen(command,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        qtlog=pathlib.Path(temp)/'log/ukui-panel.log' 
        try:
            for _ in range(40):
                time.sleep(.25)
                if p.poll() is not None: raise RuntimeError('OEM panel exited: '+str(p.returncode))
                if qtlog.exists() and '[LiquidPanel] attached' in qtlog.read_text(errors='replace'): break
            else: raise RuntimeError('OEM panel was not attached')
            time.sleep(2)
            tree=subprocess.check_output(['xwininfo','-root','-tree'],env=env,text=True)
            (root/'artifacts/oem-windows.txt').write_text(tree)
            subprocess.run(['import','-window','root',str(root/'artifacts/oem-smoke.png')],env=env,timeout=5,check=True)
            time.sleep(1)
            candidates=[]
            for item in pathlib.Path('/proc').iterdir():
                if not item.name.isdigit(): continue
                try:
                    if os.getpgid(int(item.name))==p.pid and (item/'comm').read_text().strip()=='ukui-panel': candidates.append(item)
                except (OSError,ProcessLookupError): pass
            assert candidates, 'OEM process missing'
            loaded=(candidates[0]/'maps').read_text()
            shutil.copy2(qtlog,root/'artifacts/oem-qt.log')
            window=subprocess.check_output(['xdotool','search','--name','^UKUI Panel$'],env=env,text=True).strip().splitlines()[0]
            subprocess.run(['xdotool','mousemove','--window',window,'1350','20','click','3'],env=env,check=True)
            time.sleep(.4)
            subprocess.run(['xdotool','key','End','Right'],env=env,check=True)
            time.sleep(.4)
            subprocess.run(['import','-window','root',str(root/'artifacts/oem-menu.png')],env=env,timeout=5,check=True)
            subprocess.run(['xdotool','key','Escape','Escape'],env=env,check=True)

            assert 'libukuiliquidpanel.so' in loaded
            for name in ['libtaskbar.so','libstartbar.so','libstatusnotifier.so']:
                assert name in loaded, 'OEM plugin not loaded: '+name
            print('PASS: actual /usr/bin/ukui-panel runs with liquid style and OEM task/start/tray plugins')
        finally:
            os.killpg(p.pid,signal.SIGTERM)
            try:p.wait(timeout=5)
            except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);p.wait()
