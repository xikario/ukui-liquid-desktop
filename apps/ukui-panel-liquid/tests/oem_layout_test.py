#!/usr/bin/env python3
"""Run under xvfb-run + dbus-run-session; configuration is isolated by bwrap."""
import argparse,configparser,json,os,shutil,signal,socket,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--output',type=Path,default=ROOT/'build/artifacts/layout-compat')
args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
def checked(value,message):
    assert value,message
    print('PASS:',message,flush=True)
with tempfile.TemporaryDirectory(prefix='panel-layout-oem-') as directory:
    temp=Path(directory);home=Path.home()
    for name in ('config/ukui','cache','log','data'):(temp/name).mkdir(parents=True,exist_ok=True)
    for name in ('panel.conf','panel-commission.ini'):
        src=home/'.config/ukui'/name
        if src.exists():shutil.copy2(src,temp/'config/ukui'/name)
    native_before=(temp/'config/ukui/panel.conf').read_bytes()
    def native_layout_fields(data):
        parser=configparser.ConfigParser();parser.read_string(data.decode(errors='replace'))
        section=parser['panel1'] if parser.has_section('panel1') else {}
        return {key:section.get(key) for key in ('position','width','width-percent','alignment')}
    native_layout_before=native_layout_fields(native_before)
    endpoint=temp/'probe.sock'
    env=dict(os.environ,XDG_CONFIG_HOME=str(home/'.config'),XDG_CACHE_HOME=str(home/'.cache'),
             XDG_DATA_HOME=str(temp/'data'),GSETTINGS_BACKEND='memory',
             QT_PLUGIN_PATH=str(ROOT/'build/test-plugins')+':'+str(ROOT/'build/plugins'),
             UKUI_PANEL_LAYOUT_PROBE_SOCKET=str(endpoint),UKUI_LIQUID_PANEL_LAYOUT_TRACE='1')
    command=['bwrap','--die-with-parent','--ro-bind','/','/','--dev-bind','/dev','/dev','--proc','/proc',
             '--bind','/tmp','/tmp','--bind',str(temp/'config'),str(home/'.config'),
             '--bind',str(temp/'cache'),str(home/'.cache'),'--bind',str(temp/'log'),str(home/'.log'),
             '/usr/bin/ukui-panel','-style','layoutprobe']
    fixture=subprocess.Popen(['/usr/bin/python3',str(ROOT/'tests/oem_appdb_fixture.py')],env=dict(os.environ,LAYOUT_APP_DATA=str(temp/'data')))
    time.sleep(.5)
    wm=subprocess.Popen(['/usr/bin/ukui-kwin_x11','--no-kactivities'],stdout=open(args.output/'kwin.log','w'),stderr=subprocess.STDOUT)
    time.sleep(2)
    process=None;observations=[]
    def find(w,cls):
        if w['class']==cls:return w
        for c in w['children']:
            result=find(c,cls)
            if result:return result
    def start():
        global process
        endpoint.unlink(missing_ok=True)
        process=subprocess.Popen(command,env=env,stdout=open(args.output/'panel-stdout.log','a'),stderr=subprocess.STDOUT,start_new_session=True)
        for _ in range(100):
            if endpoint.exists():time.sleep(2);return
            if process.poll() is not None:raise RuntimeError('panel exited: '+str(process.returncode))
            time.sleep(.1)
        raise RuntimeError('probe not ready')
    def stop():
        if process and process.poll() is None:
            os.killpg(process.pid,signal.SIGTERM)
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:os.killpg(process.pid,signal.SIGKILL);process.wait()
    def call(action='state',**kw):
        with socket.socket(socket.AF_UNIX) as connection:
            connection.settimeout(15);connection.connect(str(endpoint))
            connection.sendall((json.dumps(dict(action=action,**kw))+'\n').encode())
            result=json.loads(connection.makefile('rb').readline())
        observations.append(dict(action=action,request=kw,**result));checked(result['ok'],action+' command')
        return result
    def strut(result):return subprocess.check_output(['xprop','-id',result['winId'],'_NET_WM_STRUT_PARTIAL','_NET_WM_STRUT'],text=True)
    try:
        start();initial=call();width=initial['panel']['w'];native_strut=strut(initial)
        checked(initial['backend']=='widget','actual OEM runtime selects widget backend')
        call('settings')
        for percent in (30,68,95):
            state=call('layout',enabled=True,percent=percent);actual=state['panel']['w']
            checked(actual>=width*percent//100 and state['panel']['x']==(width-actual)//2,str(percent)+' percent actual width and position')
            checked(strut(state)==native_strut,'native full-edge strut retained')
            state=call('realign');checked(state['panel']['w']==actual and state['panel']['x']==(width-actual)//2,'native realign retains target')
        call('layout',enabled=True,percent=30)
        apps=call('applications',count=24,wait=2000)
        bar=find(apps['panel'],'UKUITaskBar')
        checked(bar and bar['scroll']['maximum']>0,'application overflow uses native scroll area')
        before=bar['scroll']['value']
        paged=call('click',name='TaskBar_KPushButton_pageDownBtn',repeat=5,wait=2000)
        checked(find(paged['panel'],'UKUITaskBar')['scroll']['value']>before,'native page-down reaches overflow icons')
        checked(all(event['w']==width*30//100 and event['x']==(width-width*30//100)//2
                    for event in paged['events'] if event['type'] in (13,14)),
                'repeated taskbar clicks never expose the native full-width geometry')
        call('applications',count=0,wait=1000)
        call('layout',enabled=True,percent=68);call('closeSettings');stop();start();restored=call()
        checked(restored['panel']['w']==width*68//100 and restored['panel']['x']==(width-width*68//100)//2,'restart restores actual width and position')
        resized=call('height',value=64)
        checked(resized['panel']['h']==64 and resized['panel']['w']==width*68//100
                and resized['panel']['x']==(width-resized['panel']['w'])//2,
                'panel height change preserves centered width and position')
        call('height',value=48)
        call('theme',value=False)
        theme_off=call()
        checked(theme_off['panel']['w']==width*68//100
                and theme_off['panel']['x']==(width-theme_off['panel']['w'])//2,
                'turning liquid appearance off retains compatibility layout')
        call('theme',value=True)
        theme_on=call()
        checked(theme_on['panel']['w']==width*68//100
                and theme_on['panel']['x']==(width-theme_on['panel']['w'])//2,
                'turning liquid appearance on retains compatibility layout')
        call('hidable',value=True);hidden=call('hide',wait=1300);shown=call('show',wait=900)
        checked(hidden['panel']['y']>=shown['panel']['y']+shown['panel']['h']-hidden['panel']['h'],'native hide reaches screen edge')
        checked(hidden['panel']['w']==shown['panel']['w']==width*68//100,'hide and show retain width')
        for sample in (hidden,shown):
            expected_width=width*68//100;expected_x=(width-expected_width)//2
            checked(all(e['x']==expected_x for e in sample['events']
                        if e['type']==13 and e['w']==expected_width),
                    'centered animation frames have no horizontal drift')
        call('hidable',value=False);first=call(wait=100);idle=call(wait=2000)
        checked(first['corrections']==idle['corrections'],'idle has no geometry corrections')
        call('settings');full=call('layout',enabled=False,percent=68)
        checked(full['panel']['x']==0 and full['panel']['w']==width,'disable restores native full width')
        call('closeSettings')
        native_after=(temp/'config/ukui/panel.conf').read_bytes()
        checked(native_layout_fields(native_after)==native_layout_before
                and b'centeredLength' not in native_after and b'centered=' not in native_after,
                'compat did not write native layout fields')
    finally:
        if (temp/'log/ukui-panel.log').exists():shutil.copy2(temp/'log/ukui-panel.log',args.output/'panel.log')
        (args.output/'observations.json').write_text(json.dumps(observations,ensure_ascii=False,indent=2))
        stop()
        wm.terminate();wm.wait(timeout=5)
        fixture.terminate();fixture.wait(timeout=5)
