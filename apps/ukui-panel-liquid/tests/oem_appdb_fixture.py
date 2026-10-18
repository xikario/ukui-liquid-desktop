import dbus,dbus.service,dbus.mainloop.glib,os
from pathlib import Path
from gi.repository import GLib
dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus=dbus.SessionBus();name=dbus.service.BusName('com.ukui.search.appdb.service',bus)
class AppDB(dbus.service.Object):
 @dbus.service.method('org.ukui.search.appDBManager',in_signature='v',out_signature='s')
 def tranWinIdToDesktopFilePath(self,window):
  ident=str(window);p=Path(os.environ['LAYOUT_APP_DATA'])/('layout-app-'+ident+'.desktop')
  p.write_text('[Desktop Entry]\nType=Application\nName=Layout Test '+ident+'\nExec=/bin/true\nIcon=application-x-executable\n')
  return str(p)
app=AppDB(bus,'/org/ukui/search/appDataBase/dbManager');GLib.MainLoop().run()
