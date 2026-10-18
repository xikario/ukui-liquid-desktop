#!/usr/bin/python3
"""Run under isolated Xvfb + dbus-run-session at 150% scale; see docs/BUILD.md."""
import ctypes as c
import os
from pathlib import Path
import subprocess
import tempfile
import time

x=c.CDLL('libX11.so.6')
def bind(name,args,result):
 f=getattr(x,name);f.argtypes=args;f.restype=result;return f
P=c.c_void_p;W=c.c_ulong;I=c.c_int
open_display=bind('XOpenDisplay',[c.c_char_p],P);root_window=bind('XDefaultRootWindow',[P],W)
atom=bind('XInternAtom',[P,c.c_char_p,I],W)
create=bind('XCreateSimpleWindow',[P,W,I,I,c.c_uint,c.c_uint,c.c_uint,W,W],W)
prop=bind('XChangeProperty',[P,W,W,W,I,I,P,I],I)
map_window=bind('XMapWindow',[P,W],I);destroy=bind('XDestroyWindow',[P,W],I)
move=bind('XMoveResizeWindow',[P,W,I,I,c.c_uint,c.c_uint],I);flush=bind('XFlush',[P],I)
d=open_display(None);root=root_window(d)
def publish(windows):
 values=(W*len(windows))(*windows);prop(d,root,atom(d,b'_NET_CLIENT_LIST',0),33,32,0,values,len(windows));flush(d)
def panel(left):
 w=create(d,root,left,1128,1000,72,0,0,0)
 cls=c.create_string_buffer(b'ukui-panel\0ukui-panel\0');prop(d,w,atom(d,b'WM_CLASS',0),31,8,0,cls,len(cls.raw)-1)
 types=(W*1)(atom(d,b'_NET_WM_WINDOW_TYPE_DOCK',0));prop(d,w,atom(d,b'_NET_WM_WINDOW_TYPE',0),4,32,0,types,1)
 map_window(d,w);publish([w]);return w

def windows(name):
 r=subprocess.run(['xdotool','search','--onlyvisible','--name','^'+name+'$'],capture_output=True,text=True)
 return r.stdout.split()
def menu_visible():
 tree=subprocess.check_output(['xwininfo','-root','-tree'],text=True)
 import re
 for line in tree.splitlines():
  if 'ukui-kaishicaidan-v2' in line:
   found=re.search(r'(0x[0-9a-f]+).*? (\d+)x(\d+)[+-]',line)
   if found and int(found[2])>500 and int(found[3])>500:
    details=subprocess.check_output(['xwininfo','-id',found[1]],text=True)
    if 'IsViewable' in details:return True
 return False

def wait_for(test,message):
 for _ in range(60):
  if test():print('PASS:',message,flush=True);return
  time.sleep(.1)
 raise AssertionError(message)
def geometry():
 ids=windows('开始菜单按钮')
 if not ids:return {}
 s=subprocess.check_output(['xdotool','getwindowgeometry','--shell',ids[0]],text=True)
 return dict(line.split('=',1) for line in s.splitlines() if '=' in line)
def tracks(left):
 g=geometry();return bool(g) and abs(int(g['X'])-(left+3))<=2 and int(g['Y'])>=1128
publish([])
with tempfile.TemporaryDirectory() as tmp:
 env=os.environ.copy();env.update(XDG_CONFIG_HOME=tmp+'/config',XDG_DATA_HOME=tmp+'/data',XDG_CACHE_HOME=tmp+'/cache')
 with open(tmp+'/app.log','w') as log:
  app=subprocess.Popen([os.environ.get('UKUI_KAISHICAIDAN_V2_BIN', str(Path(__file__).resolve().parents[1]/'build-v2/ukui-kaishicaidan-v2'))],env=env,stdout=log,stderr=log)
  try:
   time.sleep(1);assert not windows('开始菜单按钮');print('PASS: no fallback click trap before panel startup',flush=True)
   w=panel(400);wait_for(lambda:tracks(400),'overlay follows late panel startup')
   move(d,w,600,1128,900,72);flush(d);wait_for(lambda:tracks(600),'overlay follows panel movement and resize')
   g=geometry();subprocess.run(['xdotool','mousemove',str(int(g['X'])+30),str(int(g['Y'])+30),'click','1'],check=True)
   wait_for(menu_visible,'mouse click opens menu after movement')
   subprocess.run(['gdbus','call','--session','--dest','org.ukui.kaishicaidan.v2','--object-path','/ukuiKaishicaidanV2','--method','org.ukui.kaishicaidan.v2.hideMenu'],stdout=subprocess.DEVNULL,check=True)
   destroy(d,w);publish([]);wait_for(lambda:not windows('开始菜单按钮'),'overlay disappears when panel exits')
   w=panel(200);wait_for(lambda:tracks(200),'overlay recovers after panel replacement')
   subprocess.run(['xdotool','key','Super_L'],check=True);wait_for(menu_visible,'Windows key still opens menu')
   subprocess.run(['gdbus','call','--session','--dest','org.ukui.menu','--object-path','/org/ukui/menu','--method','org.ukui.menu.WinKeyResponse'],stdout=subprocess.DEVNULL,check=True)
   time.sleep(.3);assert menu_visible();print('PASS: native WinKeyResponse does not toggle the same key twice',flush=True)
   subprocess.run(['xdotool','key','Super_L'],check=True);wait_for(lambda:not menu_visible(),'second Windows key closes menu')
   for key in ['Super_L+e','Super_L+r','Super_R+e']:
    subprocess.run(['xdotool','key',key],check=True)
    subprocess.run(['gdbus','call','--session','--dest','org.ukui.menu','--object-path','/org/ukui/menu','--method','org.ukui.menu.WinKeyResponse'],stdout=subprocess.DEVNULL,check=True)
    time.sleep(.2);assert not menu_visible()
   print('PASS: Super combinations do not open menu',flush=True)
   # A native notification can arrive before XRecord has delivered release.
   for key in ['Super_L', 'Super_R']:
    subprocess.run(['xdotool','keydown',key],check=True)
    time.sleep(.1)
    subprocess.run(['gdbus','call','--session','--dest','org.ukui.menu','--object-path','/org/ukui/menu','--method','org.ukui.menu.WinKeyResponse'],stdout=subprocess.DEVNULL,check=True)
    subprocess.run(['xdotool','keyup',key],check=True)
    wait_for(menu_visible,'native-before-release opens exactly once for '+key)
    time.sleep(.15);assert menu_visible()
    subprocess.run(['xdotool','key',key],check=True)
    wait_for(lambda:not menu_visible(),'recorded '+key+' closes menu')
   # Exercise the opposite ordering: the native notification arrives first,
   # then the recorded press/release is delayed past the 80ms native fallback.
   # The delayed XRecord gesture must be consumed instead of toggling twice.
   time.sleep(.35)
   subprocess.run(['gdbus','call','--session','--dest','org.ukui.menu','--object-path','/org/ukui/menu','--method','org.ukui.menu.WinKeyResponse'],stdout=subprocess.DEVNULL,check=True)
   time.sleep(.1)
   subprocess.run(['xdotool','key','Super_L'],check=True)
   wait_for(menu_visible,'delayed XRecord gesture is deduplicated after native fallback')
   time.sleep(.1);assert menu_visible()
   subprocess.run(['xdotool','key','Super_L'],check=True)
   wait_for(lambda:not menu_visible(),'close menu after delayed native fallback')
   subprocess.run(['xdotool','key','Super_L'],check=True)
   wait_for(menu_visible,'recorded Win press opens before a delayed native notification')
   time.sleep(.3)
   subprocess.run(['gdbus','call','--session','--dest','org.ukui.menu','--object-path','/org/ukui/menu','--method','org.ukui.menu.WinKeyResponse'],stdout=subprocess.DEVNULL,check=True)
   time.sleep(.2);assert menu_visible()
   print('PASS: delayed native notification is deduplicated after XRecord toggle',flush=True)
   subprocess.run(['xdotool','key','Super_L'],check=True)
   wait_for(lambda:not menu_visible(),'close menu after delayed native notification')
   subprocess.run(['xdotool','keydown','Super_L'],check=True)
   time.sleep(.5)
   subprocess.run(['gdbus','call','--session','--dest','org.ukui.menu','--object-path','/org/ukui/menu','--method','org.ukui.menu.WinKeyResponse'],stdout=subprocess.DEVNULL,check=True)
   subprocess.run(['xdotool','keyup','Super_L'],check=True)
   time.sleep(.2);assert not menu_visible()
   print('PASS: long held Super plus native notification does not open menu',flush=True)
   subprocess.run(['gdbus','call','--session','--dest','org.ukui.menu','--object-path','/org/ukui/menu','--method','org.ukui.menu.Activate','{}'],stdout=subprocess.DEVNULL,check=True)
   wait_for(menu_visible,'native D-Bus Activate opens Liquid menu')
   assert app.poll() is None
   subprocess.run(['gdbus','call','--session','--dest','org.ukui.kaishicaidan.v2','--object-path','/ukuiKaishicaidanV2','--method','org.ukui.kaishicaidan.v2.quitApp'],stdout=subprocess.DEVNULL,check=True,timeout=5)
   assert app.wait(timeout=5)==0
   print('PASS: normal quit releases the XRecord connection',flush=True)
  except:
   print(Path(tmp+'/app.log').read_text()[-6000:]);raise
  finally:
   if app.poll() is None:
    app.terminate();app.wait(timeout=5)
