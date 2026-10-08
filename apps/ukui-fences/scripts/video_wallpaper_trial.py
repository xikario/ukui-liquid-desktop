#!/usr/bin/env python3
"""X11 wallpaper: native-rate VAAPI/GL playback, cached Qt poster.
No transcoding, frame-rate cap, Python frame copies, or software-decode fallback.
The child has an empty input shape; the owner supplies exposed desktop regions.
"""
import argparse
import ctypes as C
import ctypes.util
import json
import os
from pathlib import Path
import signal
import select
import subprocess
import sys
from fractions import Fraction


def fill_geometry(width, height, aspect):
    # mpv panscan crops inside a screen-sized drawable, without offscreen pixels.
    return 0, 0, width, height


def subtract_rectangles(regions, cover):
    cx, cy, cw, ch = cover
    result = []
    for x, y, w, h in regions:
        left, top = max(x, cx), max(y, cy)
        right, bottom = min(x + w, cx + cw), min(y + h, cy + ch)
        if left >= right or top >= bottom:
            result.append((x, y, w, h)); continue
        for rect in ((x, y, w, top-y), (x, bottom, w, y+h-bottom),
                     (x, top, left-x, bottom-top), (right, top, x+w-right, bottom-top)):
            if rect[2] > 0 and rect[3] > 0: result.append(rect)
    return result


def exposed_fraction(regions, covers):
    total = sum(w*h for x,y,w,h in regions)
    if not total: return 0.0
    remaining = list(regions)
    for cover in covers:
        remaining = subtract_rectangles(remaining, cover)
        if len(remaining) > 4096: return 1.0  # Complex shapes: keep playing conservatively.
    return sum(w*h for x,y,w,h in remaining) / total


def should_pause(fraction, was_covered):
    # Hysteresis avoids rapid pause/resume while dragging a window near 10%.
    return fraction < (0.15 if was_covered else 0.10)


RESUME_DELAY_MS = 1500
STATE_FRACTION_STEP = 0.05


def visible_frame(x, y, width, height, shadow):
    # ARGB clients (Chrome, Electron, GTK CSD) draw shadows inside their X window;
    # _GTK_FRAME_EXTENTS (left,right,top,bottom) gives the transparent margin.
    if len(shadow) == 4:
        left, right, top, bottom = shadow
        x, y, width, height = x + left, y + top, width - left - right, height - top - bottom
    return (x, y, width, height) if width > 0 and height > 0 else None


def playback_action(wanted, playing, waiting, started, due=False):
    """Pause at once; resume only after the desktop stays exposed for a moment.
    The very first start is immediate so the poster hands over without delay."""
    if not wanted: return 'pause' if playing or waiting else 'none'
    if playing: return 'none'
    if due or not started: return 'play'
    return 'none' if waiting else 'wait'


def should_report(state_changed, fraction, reported):
    return state_changed or abs(fraction - reported) >= STATE_FRACTION_STEP


def inspect_media(path):
    source = Path(path).resolve(strict=True)
    metadata = json.loads(subprocess.check_output(
        ['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_streams',
         '-show_format', '-of', 'json', str(source)], timeout=5))
    info = metadata['streams'][0]
    fps = Fraction(info.get('avg_frame_rate', '0/1'))
    duration = float(metadata['format']['duration'])
    width, height = int(info['width']), int(info['height'])
    if not 0 < duration <= 60 or not 0 < fps <= 120 or min(width, height) <= 0:
        raise RuntimeError('trial requires a local clip <=60 seconds with a known frame rate <=120')
    if info.get('codec_name') != 'h264':
        raise RuntimeError('this hardware trial currently supports H.264 MP4')
    return str(source), width, height, fps, duration


class MpvEvent(C.Structure):
    _fields_ = [('event_id', C.c_int), ('error', C.c_int),
                ('reply_userdata', C.c_uint64), ('data', C.c_void_p)]


class Player:
    def __init__(self, window):
        self.lib = C.CDLL(ctypes.util.find_library('mpv') or 'libmpv.so.1')
        signatures = {
            'mpv_create': (C.c_void_p, []),
            'mpv_set_option_string': (C.c_int, [C.c_void_p, C.c_char_p, C.c_char_p]),
            'mpv_set_property_string': (C.c_int, [C.c_void_p, C.c_char_p, C.c_char_p]),
            'mpv_initialize': (C.c_int, [C.c_void_p]),
            'mpv_command': (C.c_int, [C.c_void_p, C.POINTER(C.c_char_p)]),
            'mpv_get_property_string': (C.c_void_p, [C.c_void_p, C.c_char_p]),
            'mpv_get_wakeup_pipe': (C.c_int, [C.c_void_p]),
            'mpv_wait_event': (C.POINTER(MpvEvent), [C.c_void_p, C.c_double]),
            'mpv_error_string': (C.c_char_p, [C.c_int]),
            'mpv_free': (None, [C.c_void_p]),
            'mpv_terminate_destroy': (None, [C.c_void_p])}
        for name, (result, arguments) in signatures.items():
            function = getattr(self.lib, name); function.restype = result; function.argtypes = arguments
        self.handle = self.lib.mpv_create()
        if not self.handle: raise RuntimeError('cannot create video player')
        try:
            options = {'config': 'no', 'load-scripts': 'no', 'wid': str(window),
                'vo': 'gpu', 'hwdec': 'vaapi', 'hwdec-codecs': 'h264',
                'vd-lavc-software-fallback': 'no', 'vd-lavc-threads': '2',
                'audio': 'no', 'loop-file': 'inf', 'pause': 'yes', 'terminal': 'no',
                'input-default-bindings': 'no', 'input-vo-keyboard': 'no',
                'osc': 'no', 'stop-screensaver': 'no', 'keep-open': 'yes',
                'cursor-autohide': 'no', 'panscan': '1.0',
                'gpu-dumb-mode': 'yes', 'scale': 'bilinear', 'dscale': 'bilinear',
                'cscale': 'bilinear', 'linear-downscaling': 'no', 'dither-depth': 'no'}
            for key, value in options.items():
                self.check(self.lib.mpv_set_option_string(self.handle, key.encode(), value.encode()))
            self.check(self.lib.mpv_initialize(self.handle))
        except Exception:
            self.close()
            raise

    def check(self, result):
        if result < 0:
            raise RuntimeError(self.lib.mpv_error_string(result).decode())
        return result

    def command(self, *args):
        values = (C.c_char_p * (len(args) + 1))(*(os.fsencode(a) for a in args), None)
        self.check(self.lib.mpv_command(self.handle, values))

    def property(self, name):
        value = self.lib.mpv_get_property_string(self.handle, name.encode())
        if not value: return None
        try: return C.string_at(value).decode(errors='replace')
        finally: self.lib.mpv_free(value)

    def pause(self, paused):
        self.check(self.lib.mpv_set_property_string(self.handle, b'pause', b'yes' if paused else b'no'))

    def stats(self):
        def number(name, default=0):
            try: return float(self.property(name))
            except (ValueError, TypeError): return default
        return {'decoder': self.property('hwdec-current'),
                'sourceFps': number('container-fps'), 'decodedFps': number('estimated-vf-fps'),
                'displayFps': number('display-fps'), 'frameDrops': number('frame-drop-count'),
                'decoderDrops': number('decoder-frame-drop-count'),
                'delayedFrames': number('vo-delayed-frame-count'),
                'position': number('time-pos'), 'width': number('width'), 'height': number('height')}

    def close(self):
        if self.handle:
            self.lib.mpv_terminate_destroy(self.handle)
            self.handle = None

class Rect(C.Structure):
    _fields_ = [('x', C.c_short), ('y', C.c_short), ('width', C.c_ushort), ('height', C.c_ushort)]


class WindowAttributes(C.Structure):
    _fields_ = [(n,t) for n,t in [
        ('background_pixmap',C.c_ulong),('background_pixel',C.c_ulong),
        ('border_pixmap',C.c_ulong),('border_pixel',C.c_ulong),
        ('bit_gravity',C.c_int),('win_gravity',C.c_int),('backing_store',C.c_int),
        ('backing_planes',C.c_ulong),('backing_pixel',C.c_ulong),
        ('save_under',C.c_int),('event_mask',C.c_long),('do_not_propagate_mask',C.c_long),
        ('override_redirect',C.c_int),('colormap',C.c_ulong),('cursor',C.c_ulong)]]


class Attributes(C.Structure):
    _fields_ = [(n, t) for n, t in [
        ('x', C.c_int), ('y', C.c_int), ('width', C.c_int), ('height', C.c_int),
        ('border_width', C.c_int), ('depth', C.c_int), ('visual', C.c_void_p),
        ('root', C.c_ulong), ('klass', C.c_int), ('bit_gravity', C.c_int), ('win_gravity', C.c_int),
        ('backing_store', C.c_int), ('backing_planes', C.c_ulong), ('backing_pixel', C.c_ulong),
        ('save_under', C.c_int), ('colormap', C.c_ulong), ('map_installed', C.c_int), ('map_state', C.c_int),
        ('all_event_masks', C.c_long), ('your_event_mask', C.c_long), ('do_not_propagate_mask', C.c_long),
        ('override_redirect', C.c_int), ('screen', C.c_void_p)]]

class PropertyEvent(C.Structure):
    _fields_ = [('type', C.c_int), ('serial', C.c_ulong), ('send_event', C.c_int),
                ('display', C.c_void_p), ('window', C.c_ulong), ('atom', C.c_ulong),
                ('time', C.c_ulong), ('state', C.c_int)]


class XErrorEvent(C.Structure):
    _fields_ = [('type', C.c_int), ('display', C.c_void_p), ('resource', C.c_ulong),
                ('serial', C.c_ulong), ('code', C.c_ubyte), ('request', C.c_ubyte),
                ('minor', C.c_ubyte)]


def should_rewind(hidden, visible, covered, ready):
    return bool(hidden and visible and not covered and ready)


class X11:
    def __init__(self, parent):
        self.lib = C.CDLL(ctypes.util.find_library('X11'))
        self.ext = C.CDLL(ctypes.util.find_library('Xext'))
        signatures = {
            'XOpenDisplay': (C.c_void_p, [C.c_char_p]),
            'XDefaultRootWindow': (C.c_ulong, [C.c_void_p]),
            'XConnectionNumber': (C.c_int, [C.c_void_p]),
            'XCreateSimpleWindow': (C.c_ulong, [C.c_void_p, C.c_ulong, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_uint, C.c_ulong, C.c_ulong]),
            'XInternAtom': (C.c_ulong, [C.c_void_p, C.c_char_p, C.c_int]),
            'XGetWindowAttributes': (C.c_int, [C.c_void_p, C.c_ulong, C.POINTER(Attributes)]),
            'XTranslateCoordinates': (C.c_int, [C.c_void_p, C.c_ulong, C.c_ulong, C.c_int, C.c_int, C.POINTER(C.c_int), C.POINTER(C.c_int), C.POINTER(C.c_ulong)]),
            'XGetWindowProperty': (C.c_int, [C.c_void_p, C.c_ulong, C.c_ulong, C.c_long, C.c_long, C.c_int, C.c_ulong, C.POINTER(C.c_ulong), C.POINTER(C.c_int), C.POINTER(C.c_ulong), C.POINTER(C.c_ulong), C.POINTER(C.c_void_p)]),
            'XChangeWindowAttributes': (C.c_int, [C.c_void_p, C.c_ulong, C.c_ulong, C.POINTER(WindowAttributes)]),
            'XMoveResizeWindow': (C.c_int, [C.c_void_p, C.c_ulong, C.c_int, C.c_int, C.c_uint, C.c_uint]),
            'XSelectInput': (C.c_int, [C.c_void_p, C.c_ulong, C.c_long]),
            'XMapWindow': (C.c_int, [C.c_void_p, C.c_ulong]),
            'XDestroyWindow': (C.c_int, [C.c_void_p, C.c_ulong]),
            'XFlush': (C.c_int, [C.c_void_p]), 'XPending': (C.c_int, [C.c_void_p]),
            'XNextEvent': (C.c_int, [C.c_void_p, C.c_void_p]),
            'XFree': (C.c_int, [C.c_void_p]), 'XCloseDisplay': (C.c_int, [C.c_void_p])}
        for name, (result, arguments) in signatures.items():
            function = getattr(self.lib, name); function.restype = result; function.argtypes = arguments
        self.ext.XShapeCombineRectangles.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.c_int, C.c_int, C.POINTER(Rect), C.c_int, C.c_int, C.c_int]
        self.display = self.lib.XOpenDisplay(None)
        if not self.display:
            raise RuntimeError('X11 display unavailable')
        self.atoms = {}
        self.rectangles = []
        self.was_covered = False
        self.visible_fraction = 1.0
        self.watched = set()
        self.properties = {}
        self.parent = parent; self.root = self.lib.XDefaultRootWindow(self.display)
        self.window = self.lib.XCreateSimpleWindow(self.display, parent, 0, 0, 1, 1, 0, 0, 0)
        # Keep the player child outside any SubstructureRedirect on the canvas.
        native = WindowAttributes(); native.override_redirect = 1
        self.lib.XChangeWindowAttributes(self.display, self.window, 1 << 9, C.byref(native))
        self.ext.XShapeCombineRectangles(self.display, self.window, 2, 0, 0, None, 0, 0, 0)
        self.ext.XShapeCombineRectangles(self.display, self.window, 0, 0, 0, None, 0, 0, 0)
        self.lib.XSelectInput(self.display, self.root, (1 << 22) | (1 << 19))
        self.lib.XSelectInput(self.display, self.parent, (1 << 17) | (1 << 16))

    def atom(self, name):
        if name not in self.atoms:
            self.atoms[name] = self.lib.XInternAtom(self.display, name.encode(), 0)
        return self.atoms[name]

    def values(self, window, name):
        cached_names = ('_NET_WM_WINDOW_TYPE', '_NET_WM_STATE', '_NET_WM_DESKTOP', '_NET_WM_WINDOW_OPACITY',
                        '_GTK_FRAME_EXTENTS')
        cached = window != self.root and name in cached_names
        if cached and (window, name) in self.properties:
            return self.properties[window, name]
        actual = C.c_ulong(); fmt = C.c_int(); count = C.c_ulong(); remaining = C.c_ulong(); data = C.c_void_p()
        self.lib.XGetWindowProperty(self.display, window, self.atom(name), 0, 65536, 0, 0,
                                   C.byref(actual), C.byref(fmt), C.byref(count), C.byref(remaining), C.byref(data))
        try:
            if data and fmt.value == 32:
                p = C.cast(data, C.POINTER(C.c_ulong)); result = [p[i] for i in range(count.value)]
            else: result = []
            if cached: self.properties[window, name] = result
            return result
        finally:
            if data: self.lib.XFree(data)

    def attributes(self, window):
        attributes = Attributes()
        return attributes if self.lib.XGetWindowAttributes(self.display, window, C.byref(attributes)) else None

    def origin(self, window):
        x = C.c_int(); y = C.c_int(); child = C.c_ulong()
        self.lib.XTranslateCoordinates(self.display, window, self.root, 0, 0, C.byref(x), C.byref(y), C.byref(child))
        return x.value, y.value

    def covered(self):
        if self.values(self.root, '_NET_SHOWING_DESKTOP')[:1] == [1]:
            self.was_covered = False; self.visible_fraction = 1.0
            return False
        clients = self.values(self.root, '_NET_CLIENT_LIST_STACKING')
        if self.parent not in clients:
            return False  # Fail open while the WM is still managing the canvas.
        current = self.values(self.root, '_NET_CURRENT_DESKTOP')[:1]
        px, py = self.origin(self.parent)
        covers = []
        live = set(clients)
        self.watched.intersection_update(live)
        self.properties = {key:value for key,value in self.properties.items() if key[0] in live}
        for window in clients[clients.index(self.parent)+1:]:
            if window not in self.watched:
                # Root events alone miss moves of reparented client windows.
                self.lib.XSelectInput(self.display, window, (1 << 22) | (1 << 17))
                self.watched.add(window)
            kinds = self.values(window, '_NET_WM_WINDOW_TYPE')
            if kinds and self.atom('_NET_WM_WINDOW_TYPE_NORMAL') not in kinds: continue
            if self.atom('_NET_WM_STATE_HIDDEN') in self.values(window, '_NET_WM_STATE'): continue
            desktop = self.values(window, '_NET_WM_DESKTOP')[:1]
            if current and desktop and desktop != current and desktop != [0xffffffff]: continue
            opacity = self.values(window, '_NET_WM_WINDOW_OPACITY')[:1]
            if opacity and opacity[0] < 0xffffffff: continue
            attributes = self.attributes(window)
            if not attributes or attributes.map_state != 2: continue
            wx, wy = self.origin(window)
            frame = visible_frame(wx-px, wy-py, attributes.width, attributes.height,
                                  self.values(window, '_GTK_FRAME_EXTENTS'))
            if frame: covers.append(frame)
        # The input regions already exclude Fences and desklets. Subtract each
        # ordinary window once, so overlapping windows are never double-counted.
        self.visible_fraction = exposed_fraction(self.rectangles, covers)
        self.was_covered = should_pause(self.visible_fraction, self.was_covered)
        return self.was_covered

    def shape(self, width, height, aspect, rectangles, show=True):
        self.rectangles = rectangles
        x, y, vw, vh = fill_geometry(width, height, aspect)
        self.lib.XMoveResizeWindow(self.display, self.window, x, y, vw, vh)
        self.set_clip(show)

    def set_clip(self, show):
        rectangles = self.rectangles if show else []
        x = y = 0  # Native drawable fills the desktop; mpv panscan handles cropping.
        values = (Rect * len(rectangles))(*(Rect(rx - x, ry - y, rw, rh) for rx, ry, rw, rh in rectangles))
        self.ext.XShapeCombineRectangles(self.display, self.window, 0, 0, 0, values, len(rectangles), 0, 0)
        self.lib.XFlush(self.display)

    def close(self):
        self.lib.XDestroyWindow(self.display, self.window)
        self.lib.XCloseDisplay(self.display)



def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--parent', type=int, required=True)
    parser.add_argument('--file', required=True)
    args = parser.parse_args()
    xlib = C.CDLL(ctypes.util.find_library('X11'))
    xlib.XInitThreads()
    # A client can disappear between reading the stacking list and its geometry.
    # Keep the callback alive for this process; such X11 races must not kill playback.
    handler_type = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)
    error_count = 0
    def xerror(display, event):
        nonlocal error_count
        if error_count < 5:
            details = C.cast(event, C.POINTER(XErrorEvent)).contents
            print('video X11 error code=%d request=%d resource=0x%x' %
                  (details.code, details.request, details.resource), file=sys.stderr, flush=True)
        error_count += 1
        return 0
    xerror_handler = handler_type(xerror)
    xlib.XSetErrorHandler.argtypes = [handler_type]
    xlib.XSetErrorHandler(xerror_handler)
    # Only opt this isolated player into the OEM driver on the matching device.
    driver = Path('/sys/class/drm/card0/device/driver')
    if driver.exists() and driver.resolve().name == 'ftd330':
        os.environ.setdefault('LIBVA_DRIVER_NAME', 'ftd330')
    from gi.repository import GLib, Gio
    media, width, height, fps, duration = inspect_media(args.file)
    x11 = X11(args.parent)
    # Establish geometry/clipping and map before libmpv creates its GL surface.
    # Initializing on an unmapped window produces black output on this OEM
    # driver, even though decode and presentation counters continue advancing.
    parent_attributes = x11.attributes(args.parent)
    if not parent_attributes:
        x11.close()
        raise RuntimeError('desktop window unavailable')
    initial = bytearray()
    while b'\n' not in initial:
        if not select.select([sys.stdin], [], [], 5)[0]:
            x11.close(); raise RuntimeError('desktop geometry was not supplied')
        chunk = os.read(sys.stdin.fileno(), 65536)
        if not chunk or len(initial) + len(chunk) > 1048576:
            x11.close(); return 0
        initial.extend(chunk)
    line, _, buffered = initial.partition(b'\n')
    initial_geometry = json.loads(line)
    if initial_geometry.get('command') == 'quit':
        x11.close(); return 0
    x11.shape(int(initial_geometry['width']), int(initial_geometry['height']), width / height,
              initial_geometry['rects'])
    x11.lib.XMapWindow(x11.display, x11.window)
    x11.lib.XFlush(x11.display)
    loop = GLib.MainLoop()
    player = None
    visible = bool(initial_geometry['visible']) and bool(initial_geometry['rects'])
    locked = False
    on_battery = False
    resume_timer = None
    ready = False
    playing = False
    pending = False
    loops = 0
    stats_timer = None
    reported_fraction = -1.0
    failed = False
    started = False
    resume_from_hidden = not visible

    def emit(**values):
        print(json.dumps(values), flush=True)

    def fail(reason):
        nonlocal failed
        if not failed: emit(event='error', reason=reason)
        failed = True
        loop.quit()

    def report():
        if playing:
            data = player.stats()
            if data['decoder'] != 'vaapi':
                fail('hardware decoding unavailable; software fallback is disabled')
                return False
            emit(event='stats', loops=loops, **data)
        return True

    def apply_playback(wanted, covered, parent):
        nonlocal playing, stats_timer, resume_from_hidden
        rewind = wanted and should_rewind(resume_from_hidden, visible, covered, ready)
        if wanted != playing or rewind:
            playing = wanted
            if rewind:
                player.command('seek', '0', 'absolute+exact')
                resume_from_hidden = False
            player.pause(not wanted)
            if stats_timer is not None:
                GLib.source_remove(stats_timer); stats_timer = None
            if wanted: stats_timer = GLib.timeout_add_seconds(5, report)
            return True
        return False

    def update(due=False):
        nonlocal pending, reported_fraction, resume_timer, started
        pending = False
        parent = x11.attributes(args.parent)
        covered = x11.covered()
        wanted = bool(ready and visible and not locked and not on_battery
                      and parent and parent.map_state == 2 and not covered)
        action = playback_action(wanted, playing, resume_timer is not None, started, due)
        if action in ('pause', 'play') and resume_timer is not None:
            GLib.source_remove(resume_timer); resume_timer = None
        if action == 'wait':
            # Alt-tab and quick window flicks must not spin the 4K decoder up and down.
            resume_timer = GLib.timeout_add(RESUME_DELAY_MS, resume)
        state_changed = action in ('pause', 'play') and apply_playback(action == 'play', covered, parent)
        if action == 'play': started = True
        if should_report(state_changed, x11.visible_fraction, reported_fraction):
            reported_fraction = x11.visible_fraction
            emit(event='state', state='starting' if not ready else 'playing' if playing else 'paused',
                 loops=loops, visible=visible, mapped=parent.map_state if parent else None,
                 locked=locked, battery=on_battery, covered=covered,
                 visibleFraction=round(x11.visible_fraction, 3))
        return False

    def resume():
        nonlocal resume_timer
        resume_timer = None
        update(due=True)
        return False

    def schedule():
        nonlocal pending
        # Coalesce bursts of window properties/motion; never poll per frame.
        if not pending: pending = True; GLib.timeout_add(100, update)

    input_buffer = bytearray(buffered)
    os.set_blocking(sys.stdin.fileno(), False)
    def commands(fd, condition):
        nonlocal visible, resume_from_hidden
        try:
            chunk = os.read(fd, 65536)
            if not chunk: loop.quit(); return False
        except BlockingIOError:
            chunk = b''
        input_buffer.extend(chunk)
        if len(input_buffer) > 1048576: fail('oversized control message'); return False
        try:
            while b'\n' in input_buffer:
                line, _, rest = input_buffer.partition(b'\n'); input_buffer[:] = rest
                value = json.loads(line)
                if value.get('command') == 'quit': loop.quit(); return False
                if value.get('command') == 'geometry':
                    if not value['visible']: resume_from_hidden = True
                    visible = bool(value['visible']) and bool(value['rects'])
                    x11.shape(max(1, int(value['width'])), max(1, int(value['height'])),
                              width / height, value['rects'])
                    schedule()
        except (ValueError, KeyError, TypeError, OverflowError) as error:
            fail('invalid geometry: ' + str(error)); return False
        return True

    def xevents(fd, condition):
        event = C.create_string_buffer(192)
        while x11.lib.XPending(x11.display):
            x11.lib.XNextEvent(x11.display, event)
            property_event = C.cast(event, C.POINTER(PropertyEvent)).contents
            if property_event.type == 28:  # PropertyNotify
                key = (property_event.window, property_event.atom)
                x11.properties = {k:v for k,v in x11.properties.items()
                                  if not (k[0] == key[0] and x11.atom(k[1]) == key[1])}
        schedule()
        return True

    def screen_lock(connection, sender, path, interface, name, parameters, data):
        nonlocal locked
        locked = bool(parameters.unpack()[0]); schedule()

    def power(connection, sender, path, interface, name, parameters, data):
        nonlocal on_battery
        changed = parameters.unpack()[1]
        if 'OnBattery' in changed:
            on_battery = bool(changed['OnBattery']); schedule()

    try:
        player = Player(x11.window)
        wake = player.lib.mpv_get_wakeup_pipe(player.handle)
        if wake < 0: raise RuntimeError('player event pipe unavailable')
        os.set_blocking(wake, False)
        def player_events(fd, condition):
            nonlocal ready, loops
            try:
                while os.read(fd, 4096): pass
            except BlockingIOError: pass
            while True:
                event = player.lib.mpv_wait_event(player.handle, 0).contents
                if event.event_id == 0: break
                if event.error < 0:
                    fail(player.lib.mpv_error_string(event.error).decode()); return False
                if event.event_id == 8:  # file loaded
                    ready = True
                    x11.lib.XMapWindow(x11.display, x11.window)
                    x11.lib.XFlush(x11.display)
                    schedule()
                elif event.event_id == 20 and ready and playing:  # loop seek
                    loops += 1
                elif event.event_id == 21:  # first frame / completed loop
                    data = player.stats()
                    if data['decoder'] != 'vaapi':
                        fail('hardware decoding unavailable; software fallback is disabled'); return False
                    emit(event='stats', loops=loops, **data)
                elif event.event_id == 7:  # an infinite local clip must not end
                    fail('video playback stopped unexpectedly'); return False
                elif event.event_id == 1:
                    loop.quit(); return False
            return True
        GLib.io_add_watch(wake, GLib.IO_IN, player_events)
        GLib.io_add_watch(sys.stdin.fileno(), GLib.IO_IN | GLib.IO_HUP, commands)
        if input_buffer:
            def buffered_commands():
                commands(sys.stdin.fileno(), GLib.IO_IN)
                return False
            GLib.idle_add(buffered_commands)
        GLib.io_add_watch(x11.lib.XConnectionNumber(x11.display), GLib.IO_IN, xevents)
        connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
        connection.signal_subscribe(None, 'org.freedesktop.ScreenSaver', 'ActiveChanged',
            None, None, Gio.DBusSignalFlags.NONE, screen_lock, None)
        try:
            locked = bool(connection.call_sync('org.freedesktop.ScreenSaver', '/ScreenSaver',
                'org.freedesktop.ScreenSaver', 'GetActive', None, None,
                Gio.DBusCallFlags.NONE, 500, None).unpack()[0])
        except GLib.Error: pass
        # A 4K decode is the largest battery cost on the desktop; the poster
        # stays on screen while unplugged. Missing UPower simply means mains.
        try:
            system = Gio.bus_get_sync(Gio.BusType.SYSTEM, None)
            system.signal_subscribe('org.freedesktop.UPower', 'org.freedesktop.DBus.Properties',
                'PropertiesChanged', '/org/freedesktop/UPower', 'org.freedesktop.UPower',
                Gio.DBusSignalFlags.NONE, power, None)
            on_battery = bool(system.call_sync('org.freedesktop.UPower', '/org/freedesktop/UPower',
                'org.freedesktop.DBus.Properties', 'Get', GLib.Variant('(ss)', ('org.freedesktop.UPower', 'OnBattery')),
                None, Gio.DBusCallFlags.NONE, 500, None).unpack()[0])
        except GLib.Error: pass
        def startup_deadline():
            if not ready: fail('video player did not become ready')
            return False
        GLib.timeout_add_seconds(15, startup_deadline)
        for sig in (signal.SIGINT, signal.SIGTERM):
            GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, sig, lambda: (loop.quit(), False)[1])
        emit(event='media', sourceFps=float(fps), width=width, height=height,
             decoder='vaapi', originalSource=True)
        player.command('loadfile', media)
        loop.run()
    finally:
        if player: player.close()
        x11.close()
    return 1 if failed else 0


if __name__ == '__main__':
    try: sys.exit(main())
    except Exception as error:
        print(json.dumps({'event': 'error', 'reason': str(error)}), flush=True)
        sys.exit(1)
