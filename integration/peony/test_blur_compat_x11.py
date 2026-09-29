#!/usr/bin/env python3
"""Run only under xvfb-run: real X11 property/lifecycle regression."""
import ctypes as C
import ctypes.util
from pathlib import Path
import subprocess
import sys
import time
from blur_compat import AFFECTED_APPLICATIONS

x = C.CDLL(ctypes.util.find_library('X11'))
def bind(name, args, result):
    fn = getattr(x, name); fn.argtypes = args; fn.restype = result; return fn
P, W, I = C.c_void_p, C.c_ulong, C.c_int
opening = bind('XOpenDisplay', [C.c_char_p], P)
d = opening(None)
assert d, 'X11 display required; run under xvfb-run'
root = bind('XDefaultRootWindow', [P], W)(d)
atom = bind('XInternAtom', [P, C.c_char_p, I], W)
a = {n: atom(d, n.encode(), 0) for n in ['WM_CLASS', '_NET_CLIENT_LIST',
    '_NET_WM_WINDOW_TYPE', '_NET_WM_WINDOW_TYPE_NORMAL', '_NET_WM_WINDOW_TYPE_DOCK',
    '_NET_WM_WINDOW_TYPE_DESKTOP', '_NET_WM_WINDOW_TYPE_POPUP_MENU',
    '_KDE_NET_WM_BLUR_BEHIND_REGION']}
get = bind('XGetWindowProperty', [P, W, W, C.c_long, C.c_long, I, W,
    C.POINTER(W), C.POINTER(I), C.POINTER(W), C.POINTER(W), C.POINTER(P)], I)
free = bind('XFree', [P], I)
change = bind('XChangeProperty', [P, W, W, W, I, I, P, I], I)
flush = bind('XFlush', [P], I)
create = bind('XCreateSimpleWindow', [P,W,I,I,C.c_uint,C.c_uint,C.c_uint,W,W], W)
destroy = bind('XDestroyWindow', [P,W], I)
close = bind('XCloseDisplay', [P], I)

def exists(w, key):
    actual, fmt, count, left, data = W(), I(), W(), W(), P()
    get(d,w,a[key],0,1024,0,0,C.byref(actual),C.byref(fmt),C.byref(count),C.byref(left),C.byref(data))
    if data: free(data)
    return bool(actual.value)

# Never replace the real desktop's client list if invoked without isolation.
assert not exists(root, '_NET_CLIENT_LIST'), 'Refusing to change an existing desktop client list'
windows = []
def values(w, key, value, kind=6):
    items = (W * len(value))(*value)
    change(d,w,a[key],kind,32,0,items,len(value)); flush(d)
def publish(): values(root, '_NET_CLIENT_LIST', windows, 33)
def wmclass(w, name):
    data = (name + '\0test\0').encode()
    change(d,w,a['WM_CLASS'],31,8,0,C.c_char_p(data),len(data)); flush(d)
def blur(w): values(w, '_KDE_NET_WM_BLUR_BEHIND_REGION', [])
def window(name, kind='_NET_WM_WINDOW_TYPE_NORMAL'):
    w = create(d,root,0,0,50,50,0,0,0); windows.append(w)
    wmclass(w,name); values(w,'_NET_WM_WINDOW_TYPE',[a[kind]],4); blur(w)
    return w
def wait_clear(w, label):
    end = time.monotonic() + 3
    while exists(w, '_KDE_NET_WM_BLUR_BEHIND_REGION') and time.monotonic() < end:
        time.sleep(.01)
    assert not exists(w, '_KDE_NET_WM_BLUR_BEHIND_REGION'), label
    print('PASS:',label,flush=True)

existing = [window(name) for name in sorted(AFFECTED_APPLICATIONS)]
excluded = [window(name) for name in ['ukui-fences', 'ukui-kaishicaidan-v2', 'chrome', 'kylin-unverified-app']]
excluded += [window('peony-qt-desktop','_NET_WM_WINDOW_TYPE_DESKTOP'),
             window('ukui-panel','_NET_WM_WINDOW_TYPE_DOCK'),
             window('kylin-software-center','_NET_WM_WINDOW_TYPE_POPUP_MENU')]
publish()
service = subprocess.Popen([sys.executable,str(Path(__file__).with_name('blur_compat.py'))])
try:
    for w in existing: wait_clear(w,'existing affected window loses rectangular blur')
    for name in sorted(AFFECTED_APPLICATIONS):
        w = window(name); publish(); wait_clear(w,'new window: '+name)
        for _ in range(3):
            blur(w); wait_clear(w,'repeated blur request: '+name)
    late = window('unmatched'); publish(); wmclass(late,'ukui-control-center')
    wait_clear(late,'late WM_CLASS identification is handled')
    popup = window('kylin-os-manager','_NET_WM_WINDOW_TYPE_POPUP_MENU');publish()
    values(popup,'_NET_WM_WINDOW_TYPE',[a['_NET_WM_WINDOW_TYPE_NORMAL']],4)
    wait_clear(popup,'late normal-window type is handled')
    for _ in range(20):
        w=window('peony');publish();destroy(d,w);windows.remove(w);publish()
    time.sleep(.1)
    assert service.poll() is None, 'service survives destroyed-window races'
    assert all(exists(w,'_KDE_NET_WM_BLUR_BEHIND_REGION') for w in excluded), 'non-target surfaces keep blur'
    print('PASS: unrelated applications, desktop, panel and menus retain blur; destruction races survive')
finally:
    service.terminate();service.wait(timeout=5)
    for w in windows:destroy(d,w)
    close(d)
