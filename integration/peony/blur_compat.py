#!/usr/bin/env python3
"""X11 workaround for the UKUI blur/magic-lamp rectangle.

Waits on X events (no polling). Removes blur requests from confirmed affected
system applications' normal windows; menus and desktop surfaces stay intact.
"""
import ctypes as C
import ctypes.util
import os

# Exact WM_CLASS resource names verified on UKUI. Avoid prefix matching:
# ukui-fences and other liquid surfaces intentionally keep their materials.
AFFECTED_APPLICATIONS = frozenset({
    'peony',
    'kylin-software-center',
    'ukui-control-center',
    'kylin-os-manager',
})

class PropertyEvent(C.Structure):
    _fields_ = [('type',C.c_int),('serial',C.c_ulong),('send_event',C.c_int),
                ('display',C.c_void_p),('window',C.c_ulong),('atom',C.c_ulong),
                ('time',C.c_ulong),('state',C.c_int)]
class Event(C.Union):
    _fields_ = [('type',C.c_int),('property',PropertyEvent),('pad',C.c_long*24)]
class ClassHint(C.Structure):
    _fields_ = [('name',C.c_void_p),('class_name',C.c_void_p)]

def matches(name, window_types, normal_type):
    return name in AFFECTED_APPLICATIONS and (not window_types or normal_type in window_types)

def run():
    x=C.CDLL(ctypes.util.find_library('X11'))
    def bind(name,args,result):
        fn=getattr(x,name);fn.argtypes=args;fn.restype=result;return fn
    ptr=C.c_void_p;ul=C.c_ulong;integer=C.c_int
    opening=bind('XOpenDisplay',[C.c_char_p],ptr)
    display=opening(None)
    if not display:raise RuntimeError('X11 display unavailable')
    root=bind('XDefaultRootWindow',[ptr],ul)(display)
    atom=bind('XInternAtom',[ptr,C.c_char_p,integer],ul)
    atoms={n:atom(display,n.encode(),0) for n in ['_NET_CLIENT_LIST','WM_CLASS','_NET_WM_WINDOW_TYPE','_NET_WM_WINDOW_TYPE_NORMAL','_KDE_NET_WM_BLUR_BEHIND_REGION']}
    free=bind('XFree',[ptr],integer)
    get=bind('XGetWindowProperty',[ptr,ul,ul,C.c_long,C.c_long,integer,ul,C.POINTER(ul),C.POINTER(integer),C.POINTER(ul),C.POINTER(ul),C.POINTER(ptr)],integer)
    hint=bind('XGetClassHint',[ptr,ul,C.POINTER(ClassHint)],integer)
    select=bind('XSelectInput',[ptr,ul,C.c_long],integer)
    delete=bind('XDeleteProperty',[ptr,ul,ul],integer)
    flush=bind('XFlush',[ptr],integer)
    nextevent=bind('XNextEvent',[ptr,C.POINTER(Event)],integer)
    # Windows may disappear between the list event and property lookup.
    handler=C.CFUNCTYPE(integer,ptr,ptr)(lambda *_:0)
    bind('XSetErrorHandler',[type(handler)],ptr)(handler)
    def prop(w,a):
        actual=ul();fmt=integer();count=ul();remaining=ul();data=ptr()
        rc=get(display,w,a,0,65536,0,0,C.byref(actual),C.byref(fmt),C.byref(count),C.byref(remaining),C.byref(data))
        try:
            values=list(C.cast(data,C.POINTER(ul))[:count.value]) if data and fmt.value==32 else []
            return actual.value!=0 and rc==0,values
        finally:
            if data:free(data)
    tracked=set()
    def apply(w):
        h=ClassHint();name=''
        if hint(display,w,C.byref(h)):
            if h.name:name=C.string_at(h.name).decode(errors='replace')
            if h.name:free(h.name)
            if h.class_name:free(h.class_name)
        types=prop(w,atoms['_NET_WM_WINDOW_TYPE'])[1]
        if matches(name,types,atoms['_NET_WM_WINDOW_TYPE_NORMAL']):
            if prop(w,atoms['_KDE_NET_WM_BLUR_BEHIND_REGION'])[0]:
                delete(display,w,atoms['_KDE_NET_WM_BLUR_BEHIND_REGION']);flush(display)
    def rescan():
        current=set(prop(root,atoms['_NET_CLIENT_LIST'])[1])
        for w in current-tracked:
            select(display,w,1<<22) # PropertyChangeMask on this connection only.
            apply(w)
        tracked.clear();tracked.update(current);flush(display)
    select(display,root,1<<22);rescan()
    event=Event()
    while True:
        nextevent(display,C.byref(event))
        if event.type!=28:continue
        e=event.property
        if e.window==root and e.atom==atoms['_NET_CLIENT_LIST']:rescan()
        elif e.window in tracked and e.state==0 and e.atom in (atoms['WM_CLASS'],atoms['_NET_WM_WINDOW_TYPE'],atoms['_KDE_NET_WM_BLUR_BEHIND_REGION']):apply(e.window)

if __name__=='__main__':
    run()
