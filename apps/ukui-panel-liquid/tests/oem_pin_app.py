#!/usr/bin/python3
"""Disposable GUI app for real OEM pin/relaunch tests; never installed."""
import argparse,ctypes,json,os,sys
from pathlib import Path
from PyQt5.QtWidgets import QApplication,QWidget
p=argparse.ArgumentParser();p.add_argument('--identity',required=True);p.add_argument('--record',required=True);p.add_argument('--token',default='argument with spaces %U 中文')
a=p.parse_args()
app=QApplication(sys.argv);window=QWidget();window.setWindowTitle('Liquid pin fixture: '+a.identity);window.resize(300,160)
lib=ctypes.CDLL('libX11.so.6');lib.XOpenDisplay.argtypes=[ctypes.c_char_p];lib.XOpenDisplay.restype=ctypes.c_void_p
class Hint(ctypes.Structure):_fields_=[('instance',ctypes.c_char_p),('klass',ctypes.c_char_p)]
lib.XSetClassHint.argtypes=[ctypes.c_void_p,ctypes.c_ulong,ctypes.POINTER(Hint)];lib.XCloseDisplay.argtypes=[ctypes.c_void_p]
display=lib.XOpenDisplay(None);identity=('LiquidPinFixture_'+a.identity).encode();hint=Hint(identity,identity)
lib.XSetClassHint(display,int(window.winId()),ctypes.byref(hint));lib.XCloseDisplay(display)
window.show();app.processEvents()
with Path(a.record).open('a') as f:f.write(json.dumps(dict(pid=os.getpid(),window=int(window.winId()),identity=a.identity,token=a.token))+'\n')
app.exec_()
