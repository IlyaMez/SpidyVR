"""Check, in the running game and without a headset, what the headset's game screen shows.

Without gameplay (pause menu, hint cards, cutscenes, animated cameras) the VR
session shows the game's own presented frame on a virtual screen, copied from
the back buffer at each Present. For each named phase this probe saves that
copy (`-screen.png`), a left eye view copying the stock camera (eye command
mode 2, `-eye.png`), and the game window as the monitor shows it
(`-window.png`), with brightness and alpha statistics. The eye views render at
the window's size, so all three share one pixel grid. Steps run in order:

  NAME        capture a phase (eye image, window image, comparison)
  key:esc     press Esc in the game window (also key:enter)
  wait:N      keep the eye views running for N seconds

Example, in free roam: gameplay key:esc wait:3 pause key:esc wait:3 resumed

Start the game with tools\\vr_launcher.py and load the save first: a load can
replace the game's view pool, which ends the eye views for that process. The
game must be restarted before another run.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import pathlib
import struct
import sys
import time
from capture_game_state import Game, find_game, open_process, close
from bridge_game import ROOT, prepare
from inspect_game import PE
from observe_game import call_remote, call_with_payload
from probe_stereo import snapshot as stereo_snapshot, frame_snapshot
from probe_stereo_gpu import discover_queue, snapshot as gpu_snapshot
from run_game_vr import write_rgb_png

HOOKS = (0x18a0bb0, 0x189bd30, 0x186cc00, 0x1846c20, 0x19223e0, 0x189e310, 0x189e3a0, 0x1873470,
         0x17991a0, 0x1920310, 0x1899ab0, 0x1920240)
SCANCODES = {'esc': 0x01, 'enter': 0x1c}
user32 = c.WinDLL('user32', use_last_error=True)
gdi32 = c.WinDLL('gdi32', use_last_error=True)
for function, result, arguments in (
        (user32.GetForegroundWindow, w.HWND, ()), (user32.GetDC, w.HDC, (w.HWND,)),
        (user32.ReleaseDC, c.c_int, (w.HWND, w.HDC)), (user32.PrintWindow, w.BOOL, (w.HWND, w.HDC, w.UINT)),
        (user32.GetClientRect, w.BOOL, (w.HWND, c.POINTER(w.RECT))), (user32.ShowWindow, w.BOOL, (w.HWND, c.c_int)),
        (user32.SetForegroundWindow, w.BOOL, (w.HWND,)), (user32.IsWindowVisible, w.BOOL, (w.HWND,)),
        (user32.GetWindowTextLengthW, c.c_int, (w.HWND,)),
        (user32.GetWindowThreadProcessId, w.DWORD, (w.HWND, c.POINTER(w.DWORD))),
        (gdi32.CreateCompatibleDC, w.HDC, (w.HDC,)), (gdi32.CreateCompatibleBitmap, w.HBITMAP, (w.HDC, c.c_int, c.c_int)),
        (gdi32.SelectObject, w.HGDIOBJ, (w.HDC, w.HGDIOBJ)), (gdi32.DeleteObject, w.BOOL, (w.HGDIOBJ,)),
        (gdi32.DeleteDC, w.BOOL, (w.HDC,)),
        (gdi32.GetDIBits, c.c_int, (w.HDC, w.HBITMAP, w.UINT, w.UINT, c.c_void_p, c.c_void_p, w.UINT))):
    function.restype, function.argtypes = result, arguments


class KeyInput(c.Structure):
    _fields_ = [('vk', w.WORD), ('scan', w.WORD), ('flags', w.DWORD), ('time', w.DWORD), ('extra', c.c_size_t)]


class Input(c.Structure):
    class Union(c.Union):
        # MOUSEINPUT is the union's largest member; its size keeps INPUT at 40 bytes.
        _fields_ = [('ki', KeyInput), ('padding', c.c_byte*32)]
    _anonymous_ = ('u',)
    _fields_ = [('type', w.DWORD), ('u', Union)]


def game_window(pid):
    """The game's visible top-level window."""
    found = []
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)

    def visit(hwnd, _):
        owner = w.DWORD()
        user32.GetWindowThreadProcessId(hwnd, c.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd) and user32.GetWindowTextLengthW(hwnd):
            found.append(hwnd)
        return True
    user32.EnumWindows(callback(visit), 0)
    if not found:
        raise RuntimeError('The game has no visible window')
    return found[0]


def client_size(hwnd):
    rect = w.RECT()
    user32.GetClientRect(hwnd, c.byref(rect))
    return rect.right-rect.left, rect.bottom-rect.top


def window_rgb(hwnd, width, height):
    """The window's client area as the game presented it, RGB rows. BitBlt from the screen is black."""
    window_dc = user32.GetDC(hwnd)
    memory_dc = gdi32.CreateCompatibleDC(window_dc)
    bitmap = gdi32.CreateCompatibleBitmap(window_dc, width, height)
    old = gdi32.SelectObject(memory_dc, bitmap)
    try:
        if not user32.PrintWindow(hwnd, memory_dc, 3):  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
            raise RuntimeError('PrintWindow failed')
        header = struct.pack('<IiiHHIIiiII', 40, width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
        info = c.create_string_buffer(header, 44)
        bgra = c.create_string_buffer(width*height*4)
        if gdi32.GetDIBits(memory_dc, bitmap, 0, height, bgra, info, 0) != height:
            raise RuntimeError('GetDIBits failed')
    finally:
        gdi32.SelectObject(memory_dc, old)
        gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(memory_dc)
        user32.ReleaseDC(hwnd, window_dc)
    raw = bgra.raw
    rgb = bytearray(width*height*3)
    rgb[0::3], rgb[1::3], rgb[2::3] = raw[2::4], raw[1::4], raw[0::4]
    return bytes(rgb)


def press(hwnd, name):
    """A real key press (scan code) with the game window in front; the game ignores posted messages."""
    user32.ShowWindow(hwnd, 9)
    if user32.GetForegroundWindow() != hwnd:
        alt = (Input * 2)()
        for i, flags in enumerate((0, 2)):
            alt[i].type, alt[i].ki.vk, alt[i].ki.flags = 1, 0x12, flags
        user32.SendInput(2, alt, c.sizeof(Input))  # lets this process bring a window forward
        user32.SetForegroundWindow(hwnd)
        time.sleep(.3)
    if user32.GetForegroundWindow() != hwnd:
        raise RuntimeError('The game window could not be brought to the front')
    keys = (Input * 2)()
    for i, flags in enumerate((8, 8 | 2)):  # KEYEVENTF_SCANCODE, then with KEYEVENTF_KEYUP
        keys[i].type, keys[i].ki.scan, keys[i].ki.flags = 1, SCANCODES[name], flags
    for key in keys:
        user32.SendInput(1, c.byref(key), c.sizeof(Input))
        time.sleep(.08)


def luma(r, g, b):
    return .2126*r+.7152*g+.0722*b


def linear(v):
    v /= 255
    return v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4


def compare(eye, window, width, height, step=2):
    """Brightness of both images and their per-pixel difference (eye minus window), on every
    `step`-th pixel. Luma is Rec. 709 weights on the stored sRGB values (0-255); the linear
    means are relative light (0-1)."""
    table = [linear(v) for v in range(256)]
    eye_l, win_l, diffs, dark, mid = [], [], [], [], []
    eye_light = window_light = 0.
    for y in range(0, height, step):
        row = y*width*3
        for x in range(0, width, step):
            i = row+x*3
            a = luma(eye[i], eye[i+1], eye[i+2])
            b = luma(window[i], window[i+1], window[i+2])
            eye_light += luma(table[eye[i]], table[eye[i+1]], table[eye[i+2]])
            window_light += luma(table[window[i]], table[window[i+1]], table[window[i+2]])
            eye_l.append(a)
            win_l.append(b)
            diffs.append(a-b)
            if b < 48:
                dark.append(a-b)
            elif b < 128:
                mid.append(a-b)

    def percentiles(values):
        ordered = sorted(values)
        return {f'p{p}': round(ordered[min(len(ordered)-1, len(ordered)*p//100)], 1) for p in (10, 50, 90)}
    return dict(eye_luma_mean=round(sum(eye_l)/len(eye_l), 2), window_luma_mean=round(sum(win_l)/len(win_l), 2),
                eye_luma=percentiles(eye_l), window_luma=percentiles(win_l),
                eye_linear_mean=round(eye_light/len(eye_l), 4), window_linear_mean=round(window_light/len(win_l), 4),
                difference_mean=round(sum(diffs)/len(diffs), 2),
                difference_abs_mean=round(sum(map(abs, diffs))/len(diffs), 2),
                difference_in_darks=round(sum(dark)/len(dark), 2) if dark else None,
                difference_in_midtones=round(sum(mid)/len(mid), 2) if mid else None,
                pixels_compared=len(diffs))


def screen_snapshot(game, address):
    """native_gpu::ScreenData: copies of the game's presented frame, and the newest read back."""
    for _ in range(8):
        raw = game.read(address, 72)
        if len(raw) != 72:
            return None
        if struct.unpack_from('<3I', raw) != (0x53475353, 2, 72):
            raise RuntimeError('Screen copy protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        status, = struct.unpack_from('<I', raw, 12)
        frames, pixels = struct.unpack_from('<2Q', raw, 24)
        width, height, view, row_bytes = struct.unpack_from('<4I', raw, 40)
        presents, skip, back_buffer = struct.unpack_from('<Q2I', raw, 56)
        skips = ('copied', 'other_device', 'no_buffer', 'format', 'shape', 'stopped', 'texture', 'busy')
        return dict(status=status, frames=frames, pixels=pixels, width=width, height=height, format=view,
                    row_bytes=row_bytes, presents=presents, skip=skips[skip] if skip < len(skips) else skip,
                    back_buffer_format=back_buffer)
    return None


def screen_rgb(rows, width, height, view, row_bytes):
    """RGB bytes of the copied back buffer: R8G8B8A8 (28), B8G8R8A8 (87) or R10G10B10A2 (24) UNORM."""
    rgb = bytearray(width*height*3)
    for y in range(height):
        row = rows[y*row_bytes:y*row_bytes+width*4]
        out = rgb[y*width*3:(y+1)*width*3]
        if view == 28:
            out[0::3], out[1::3], out[2::3] = row[0::4], row[1::4], row[2::4]
        elif view == 87:
            out[0::3], out[1::3], out[2::3] = row[2::4], row[1::4], row[0::4]
        elif view == 24:
            for x, (v,) in enumerate(struct.iter_unpack('<I', row)):
                out[x*3:x*3+3] = bytes(((v >> s & 1023)*255+511)//1023 for s in (0, 10, 20))
        else:
            raise RuntimeError(f'Unexpected back buffer format {view}')
        rgb[y*width*3:(y+1)*width*3] = out
    return bytes(rgb)


def difference_image(eye, window):
    """|eye - window| per channel, four times brighter, to show where the images differ."""
    return bytes(min(255, 4*abs(a-b)) for a, b in zip(eye, window))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('steps', nargs='+')
    p.add_argument('--seconds', type=float, default=2, help='eye views running before each capture')
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/game-screen.json')
    a = p.parse_args()
    for step in a.steps:
        kind, _, value = step.partition(':')
        if kind == 'key' and value not in SCANCODES or kind == 'wait' and not 0 < float(value or 0) <= 60:
            p.error(f'Unsupported step {step}')
    try:
        user32.SetProcessDpiAwarenessContext(c.c_void_p(-4))  # window pixels, not scaled ones
    except AttributeError:
        user32.SetProcessDPIAware()
    game = Game(find_game())
    process = exports = None
    views_active = gpu_active = False
    try:
        hwnd = game_window(game.pid)
        width, height = client_size(hwnd)
        if not 64 <= width <= 4096 or not 64 <= height <= 4096:
            raise RuntimeError(f'Unexpected window size {width} x {height}')
        pe = PE(pathlib.Path(game.path).read_bytes())
        entries = {rva: pe.bytes(rva, 16) for rva in HOOKS}
        if any(game.read(game.base+rva, 16) != code for rva, code in entries.items()):
            raise RuntimeError('A view entry is already patched. Start a fresh game process.')
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        queue = discover_queue(game, process)
        exports, digest = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_stereo_probe.dll',
                                  ROOT/'reports/stereo-modules',
                                  ('SpidyStart', 'SpidyStop', 'SpidyStereoData', 'SpidySetEyes', 'SpidyStereoFrames',
                                   'SpidyGpuStart', 'SpidyGpuStop', 'SpidyGpuData', 'SpidyGpuFreeze',
                                   'SpidyGpuScreen', 'SpidyScreenData'))
        # Capture only, untimed: the eye views live while commands arrive, as in a VR session.
        code = call_with_payload(process, exports['SpidyGpuStart'],
                                 struct.pack('<4I2Q4I', 0x53475043, 3, 48, game.pid, game.base, queue, 0, 0, 1, 0))
        if code:
            raise RuntimeError(f'GPU capture start: {code}')
        gpu_active = True
        # Copy each presented frame, as the VR session does while gameplay is unavailable.
        hooked = screen_snapshot(game, exports['SpidyScreenData'])
        if not hooked or hooked['status'] != 1 or call_remote(process, exports['SpidyGpuScreen'], 1):
            raise RuntimeError('The game\'s Present could not be hooked')
        # The VR session's view flags (13); mode 2 leaves the game's own view where it is.
        code = call_with_payload(process, exports['SpidyStart'],
                                 struct.pack('<4IQ4I', 0x53534346, 1, 40, game.pid, game.base, 0, 13, width, height))
        if code:
            raise RuntimeError(f'Native eye views start: {code}')
        views_active = True
        serial = 0

        def run_views(seconds):
            nonlocal serial
            end = time.monotonic()+seconds
            while time.monotonic() < end:
                serial += 1
                command = struct.pack('<4IQ2I40f3fI', 0x53455043, 2, 208, 2, serial, 250, 0, *([0.]*40), 0, 0, 0, 0)
                code = call_with_payload(process, exports['SpidySetEyes'], command)
                if code:
                    raise RuntimeError(f'Game screen command: {code}')
                time.sleep(.02)

        phases = {}
        for step in a.steps:
            kind, _, value = step.partition(':')
            if kind == 'key':
                press(hwnd, value)
                run_views(.5)
                continue
            if kind == 'wait':
                run_views(float(value))
                continue
            before = frame_snapshot(game, exports['SpidyStereoFrames'])
            pairs_before = (gpu_snapshot(game, exports['SpidyGpuData']) or {}).get('pairs', 0)
            screen_before = call_remote(process, exports['SpidyGpuScreen'], 2) == 0 and \
                screen_snapshot(game, exports['SpidyScreenData'])['frames']
            run_views(a.seconds)
            code = call_remote(process, exports['SpidyGpuFreeze'])
            screen_code = call_remote(process, exports['SpidyGpuScreen'], 2)
            shot = window_rgb(hwnd, width, height)
            gpu = gpu_snapshot(game, exports['SpidyGpuData'])
            after = frame_snapshot(game, exports['SpidyStereoFrames'])
            phase = dict(freeze=code, new_pairs=gpu['pairs']-pairs_before if gpu else None,
                         eye_job_copies=after['left_copies']-before['left_copies'] if before and after else None)
            # The headset's game screen: the presented frame the game copied at Present.
            copied = screen_snapshot(game, exports['SpidyScreenData'])
            phase.update(screen_read=screen_code, screen_frames=copied['frames']-(screen_before or 0),
                         screen_size=[copied['width'], copied['height']], screen_format=copied['format'],
                         presents_seen=copied['presents'], last_skip=copied['skip'],
                         back_buffer_format=copied['back_buffer_format'])
            if not screen_code and copied['pixels'] and (copied['width'], copied['height']) == (width, height):
                rows = game.read(copied['pixels'], copied['row_bytes']*height)
                screen = screen_rgb(rows, width, height, copied['format'], copied['row_bytes'])
                write_rgb_png(a.output.with_name(f'{a.output.stem}-{step}-screen.png'), width, height, screen)
                versus = compare(screen, shot, width, height)
                phase['screen_vs_window'] = dict(mean=versus['eye_luma_mean'], window=versus['window_luma_mean'],
                                                 difference_abs_mean=versus['difference_abs_mean'])
            else:
                phase['error'] = 'The presented frame was not copied in this phase'
            if code or not gpu or (gpu['width'], gpu['height']) != (width, height):
                phase['error'] = 'No eye image was captured in this phase'
                phases[step] = phase
                print(step, json.dumps(phase), flush=True)
                continue
            rgba = game.read(gpu['left_pixels'], width*height*4)
            if len(rgba) != width*height*4:
                raise RuntimeError('Incomplete eye pixels')
            eye = bytearray(width*height*3)
            eye[0::3], eye[1::3], eye[2::3] = rgba[0::4], rgba[1::4], rgba[2::4]
            eye = bytes(eye)
            # The headset receives alpha too; a runtime that blends with it darkens the image.
            alpha = rgba[3::4]
            phase.update(alpha_mean=round(sum(alpha)/len(alpha), 2), alpha_min=min(alpha),
                         alpha_below_255=round(sum(v < 255 for v in alpha)/len(alpha), 4))
            stem = a.output.with_name(f'{a.output.stem}-{step}')
            for name, image in (('eye', eye), ('window', shot), ('difference', difference_image(eye, shot))):
                write_rgb_png(stem.with_name(f'{stem.name}-{name}.png'), width, height, image)
            phase.update(compare(eye, shot, width, height), images=str(stem)+'-*.png')
            phases[step] = phase
            print(step, json.dumps(phase), flush=True)
        call_remote(process, exports['SpidyGpuScreen'], 0)
        code = call_remote(process, exports['SpidyStop'])
        views_active = code != 0
        if code:
            raise RuntimeError(f'Native eye views stop: {code}')
        gpu_stop = call_remote(process, exports['SpidyGpuStop'])
        gpu_active = False
        result = dict(pid=game.pid, module_base=hex(game.base), dll_sha256=digest, window=[width, height],
                      steps=a.steps, phases=phases, gpu_stop=gpu_stop,
                      stereo=stereo_snapshot(game, exports['SpidyStereoData']),
                      entries_restored=all(game.read(game.base+rva, 16) == code for rva, code in entries.items()))
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(result, indent=2)+'\n')
        print(f"Entries restored: {result['entries_restored']}; report: {a.output.resolve()}", flush=True)
        return 0 if result['entries_restored'] and not gpu_stop and all('error' not in x for x in phases.values()) else 1
    finally:
        try:
            if views_active:
                call_remote(process, exports['SpidyStop'])
            if gpu_active:
                call_remote(process, exports['SpidyGpuStop'])
        except OSError:
            pass
        if process:
            close(process)
        game.close()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Game screen check failed: {error}', file=sys.stderr)
        sys.exit(2)
