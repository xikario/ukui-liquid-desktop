#!/usr/bin/python3
"""Real /usr/bin/ukui-panel, separate X display/bus/config. Requires bwrap and PyQt5."""
import argparse,configparser,json,os,shutil,signal,socket,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
observations=[];children=[]
def checked(ok,message):
 if not ok:raise AssertionError(message)
 print('PASS:',message,flush=True)
with tempfile.TemporaryDirectory(prefix='liquid-pin-oem-') as directory:
 temp=Path(directory);home=Path.home();data=temp/'data';apps=data/'applications'
 for name in ('config/ukui','cache','log','data/applications'):(temp/name).mkdir(parents=True,exist_ok=True)
 config=configparser.ConfigParser();config.optionxform=str;config.read(home/'.config/ukui/panel.conf')
 for key in list(config['taskbar']):
  if key.startswith('apps\\'):del config['taskbar'][key]
 config['taskbar']['apps\\size']='0'
 with (temp/'config/ukui/panel.conf').open('w') as f:config.write(f)
 token='spaces "quotes" %U 中文'
 def appcmd(identity):return ['/usr/bin/python3',str(ROOT/'tests/oem_pin_app.py'),'--identity',identity,'--record',str(temp/(identity+'.jsonl')),'--token',token]
 # Use the production serializer for the missing case, and known valid desktop
 # quoting for the normal/NoDisplay cases. No application-specific production fix.
 def quote(s):return '"'+s.replace('%','%%').replace('\\','\\\\').replace('"','\\"').replace('$','\\$').replace('`','\\`')+'"'
 for identity in ('normal','hidden'):
  command=' '.join(quote(x) for x in appcmd(identity)).replace('\\','\\\\')
  (apps/('fixture-'+identity+'.desktop')).write_text('[Desktop Entry]\nType=Application\nName=Pin Fixture '+identity+'\nExec='+command+'\nStartupWMClass=LiquidPinFixture_'+identity+'\nNoDisplay='+str(identity=='hidden').lower()+'\n')
 endpoint=temp/'probe.sock'
 env=dict(os.environ,XDG_CONFIG_HOME=str(home/'.config'),XDG_CACHE_HOME=str(home/'.cache'),XDG_DATA_HOME=str(data),GSETTINGS_BACKEND='memory',
  QT_PLUGIN_PATH=str(ROOT/'build/test-plugins')+':'+str(ROOT/'build/plugins'),UKUI_PANEL_PIN_PROBE_SOCKET=str(endpoint),UKUI_LIQUID_GLASS_NO_GL='1')
 command=['bwrap','--die-with-parent','--ro-bind','/','/','--dev-bind','/dev','/dev','--proc','/proc','--bind','/tmp','/tmp',
  '--bind',str(temp/'config'),str(home/'.config'),'--bind',str(temp/'cache'),str(home/'.cache'),'--bind',str(temp/'log'),str(home/'.log'),'/usr/bin/ukui-panel','-style','pinprobe']
 fixture=subprocess.Popen(['/usr/bin/python3',str(ROOT/'tests/oem_pin_appdb.py')],env=dict(os.environ,PIN_APP_DATA=str(data)))
 wm=subprocess.Popen(['/usr/bin/ukui-kwin_x11','--no-kactivities'],stdout=open(a.output/'kwin.log','w'),stderr=subprocess.STDOUT)
 time.sleep(2);panel=None
 def start():
  global panel
  endpoint.unlink(missing_ok=True)
  panel=subprocess.Popen(command,env=env,stdout=open(a.output/'panel-stdout.log','a'),stderr=subprocess.STDOUT,start_new_session=True)
  for _ in range(100):
   if endpoint.exists():time.sleep(1);return
   if panel.poll() is not None:raise RuntimeError('panel exited '+str(panel.returncode))
   time.sleep(.1)
  raise RuntimeError('probe not ready')
 def stop():
  if panel and panel.poll() is None:
   os.killpg(panel.pid,signal.SIGTERM)
   try:panel.wait(timeout=5)
   except subprocess.TimeoutExpired:os.killpg(panel.pid,signal.SIGKILL);panel.wait()
 def call(action='state',**kw):
  with socket.socket(socket.AF_UNIX) as connection:
   connection.settimeout(15);connection.connect(str(endpoint));connection.sendall((json.dumps(dict(action=action,**kw))+'\n').encode());result=json.loads(connection.makefile('rb').readline())
  observations.append(dict(action=action,request=kw,**result));checked(result['ok'],action+' command');return result
 def records(identity):
  f=temp/(identity+'.jsonl');return [json.loads(line) for line in f.read_text().splitlines()] if f.exists() else []
 def until(check,message,seconds=8):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   result=check()
   if result:return result
   time.sleep(.15)
  raise AssertionError(message)
 def appbutton(state,window):return [b for b in state['buttons'] if b['window']==str(window)]
 def quick(state,desktop):return [b for b in state['buttons'] if b['window']=='0' and b['desktop']==desktop]
 try:
  start()
  for identity in ('normal','hidden','missing'):
   child=subprocess.Popen(appcmd(identity),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(child)
   record=until(lambda:records(identity),'fixture started')[-1];window=record['window']
   until(lambda:appbutton(call(wait=100),window),'OEM created task button')
   menu=call('menu',window=str(window));actions=[x for m in menu['menus'] for x in m['actions'] if x['name']=='liquidTaskbarPinAction']
   checked(len(actions)==1 and actions[0]['enabled'] and actions[0]['text']=='固定到任务栏',identity+' offers exactly one enabled pin')
   state=call('toggle',wait=900);buttons=appbutton(state,window)
   checked(len(buttons)==1,'running window remains in one group')
   desktop=buttons[0]['desktop'];checked(bool(quick(state,desktop)),identity+' creates a native quick-launch button')
   checked(desktop in (temp/'config/ukui/panel.conf').read_text(),identity+' saved in native panel.conf')
   before_extra=len(records(identity));extra=subprocess.Popen(appcmd(identity),stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL);children.append(extra)
   extra_record=until(lambda:records(identity) if len(records(identity))>before_extra else [],'second application window started')[-1]
   multi=until(lambda:(s if appbutton(s:=call(wait=100),extra_record['window']) else None),'second window appears')
   first_button=appbutton(multi,window)[0];second_button=appbutton(multi,extra_record['window'])[0]
   checked(first_button['desktop']==second_button['desktop']==desktop and first_button['group']==second_button['group'],identity+' additional windows stay in the same task group')
   extra.terminate();extra.wait(timeout=5)

   menu=call('menu',window=str(window));checked(any(x['text']=='从任务栏取消固定' for m in menu['menus'] for x in m['actions']),identity+' menu reflects pinned state');call('closeMenus')
   child.terminate();child.wait(timeout=5);time.sleep(.6)
   checked(any(b['visible'] for b in quick(call(),desktop)),identity+' pin survives application exit')
   stop();start();restarted=call();checked(bool(quick(restarted,desktop)),identity+' pin survives native panel restart');checked(all(b['routed'] for b in quick(restarted,desktop)),identity+' uses the safe launch callback after restart')
   before=len(records(identity));call('launch',desktop=desktop,wait=900)
   record=until(lambda:records(identity) if len(records(identity))>before else [],'pinned application relaunched')[-1]
   checked(record['token']==token,identity+' relaunch preserves actual arguments')
   state=until(lambda:(s if appbutton(s:=call(wait=100),record['window']) else None),'relaunch appears in taskbar')
   checked(appbutton(state,record['window'])[0]['desktop']==desktop,identity+' relaunch associates with the same pinned identity')
   checked(len(quick(state,desktop))==1,identity+' has no duplicate quick-launch pin')
   call('menu',window=str(record['window']));state=call('toggle',wait=700)
   checked(not quick(state,desktop),identity+' unpins while window is running')
   checked(desktop not in (temp/'config/ukui/panel.conf').read_text(),identity+' unpin persists in native settings')
   os.kill(record['pid'],signal.SIGTERM);time.sleep(.3)
  checked(panel.poll() is None,'actual OEM panel remains alive after all application cases')
 finally:
  for identity in ('normal','hidden','missing'):
   for record in records(identity):
    try:os.kill(record['pid'],signal.SIGTERM)
    except ProcessLookupError:pass
  for child in children:
   try:child.wait(timeout=3)
   except subprocess.TimeoutExpired:child.kill();child.wait()
  stop();fixture.terminate();fixture.wait(timeout=5);wm.terminate();wm.wait(timeout=5)
  (a.output/'observations.json').write_text(json.dumps(observations,ensure_ascii=False,indent=2))
  if (temp/'log/ukui-panel.log').exists():shutil.copy2(temp/'log/ukui-panel.log',a.output/'panel.log')
  shutil.copy2(temp/'config/ukui/panel.conf',a.output/'final-panel.conf')
