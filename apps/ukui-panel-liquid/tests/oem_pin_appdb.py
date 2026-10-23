#!/usr/bin/python3
"""Isolated application database: ordinary, hidden, and absent launchers."""
import dbus,dbus.service,dbus.mainloop.glib,os,subprocess
from pathlib import Path
from gi.repository import GLib
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus=dbus.SessionBus();name=dbus.service.BusName('com.ukui.search.appdb.service',bus)
root=Path(os.environ['PIN_APP_DATA'])
class AppDB(dbus.service.Object):
 @dbus.service.method('org.ukui.search.appDBManager',in_signature='v',out_signature='s')
 def tranWinIdToDesktopFilePath(self,window):
  try:wmclass=subprocess.check_output(['xprop','-id',str(window),'WM_CLASS'],text=True,stderr=subprocess.DEVNULL,timeout=2)
  except Exception:return ''
  for identity in ('normal','hidden'):
   if 'LiquidPinFixture_'+identity in wmclass:return str(root/'applications'/('fixture-'+identity+'.desktop'))
  if 'LiquidPinFixture_missing' in wmclass:
   for path in sorted((root/'applications').glob('ukui-liquid-pinned-*.desktop')):
    if 'StartupWMClass=LiquidPinFixture_missing\n' in path.read_text():return str(path)
  return ''
app=AppDB(bus,'/org/ukui/search/appDataBase/dbManager');GLib.MainLoop().run()
