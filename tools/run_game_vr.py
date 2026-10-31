"""Launch/attach native VR, with optional bounded tests and automatic eye resolution."""
import argparse
import ctypes as c
from ctypes import wintypes
import hashlib
import json
import math
import pathlib
import re
import shutil
import struct
import subprocess
import sys
import time
import signal
import threading
import uuid
import zlib
import _thread
from collections import deque
from capture_game_state import Game, find_game, open_process, close
from bridge_game import (ROOT, HOOKS, prepare, snapshot as bridge_snapshot, GameNeedsRestart, other_build_text,
                         spidy_folder, staged)
from inspect_game import PE, EXPECTED_VERSION
from observe_game import call_remote, call_with_payload, modules
from probe_stereo_gpu import MAX_EYE_SIZE, discover_queue, snapshot as gpu_snapshot, save_eye_images
from probe_stereo import frame_snapshot, render_memory_snapshot, snapshot as stereo_snapshot
from probe_native_rays import snapshot as ray_snapshot
from probe_game_swing import snapshot as swing_snapshot
from probe_native_motion import snapshot as motion_snapshot
from vr_launcher import (LauncherLock, alive, bring_to_front, wait_for_game, enlarge_render_memory, elevated,
                         RENDER_MEMORY_HOOKS, NeedsAdministrator, NEEDS_ADMINISTRATOR_EXIT, CLOSE_GAME_EXIT,
                         close_game)
import game_freeze
import vr_display
import xr_runtime

GAME_HOOKS = (*HOOKS, 0x2e67010, 0x1fbe360, 0x1fbda50, 0xa7b3a0, 0x1f9db60,
              0x18a0bb0, 0x189bd30, 0x186cc00, 0x1846c20, 0x19223e0,
              0x189e310, 0x189e3a0, 0x1873470, 0x17991a0, 0x1920310, 0x676dd0, 0x1920240,
              # The web shooter: the camera's update (shots go out on the main thread), the weapons' muzzle
              # and the shots' events (what they strike).
              0x897d30, 0x2150c40, 0xd2a570,
              # Slow motion: the update of the game's time system (game_time.hpp).
              0x19bb430,
              # The HUD in VR (native_hud.hpp): its panel's placement, its texture's stream, the render command
              # that draws its second movie, and the projections of its world markers.
              0x73ab80, 0x2104400, 0x1d2b6c0, 0x1f10ad0, 0x1f10b60)
# The render scale's range in percent (spidy::minimumRenderScale and maximumRenderScale).
RENDER_SCALES = (50, 200)
# Upscaling: the percent of the eye's size, per side, the eyes' scene is rendered at, and its default
# (spidy::minimumEyeUpscale, maximumEyeUpscale and defaultEyeUpscale).
UPSCALES, DEFAULT_UPSCALE = (50, 100), 67
# How often the game's window gets the left eye in immersive VR: presents a second, 0 for every frame
# (include/spidy/monitor_rate.hpp).
MONITOR_RATES, DEFAULT_MONITOR_RATE = (0, 240), 30
# What a session takes as a T-pose calibration, millimetres (body_calibration.hpp): the eye height and the arm
# length (from Spider-Man's shoulder at the player's size to the wrist) the body is sized to.
EYE_HEIGHTS_MM, ARM_LENGTHS_MM = (1000, 2500), (250, 1200)
# Eye snapshots kept per session (the newest), and the header fields saved with each.
EYE_SHOTS = 72
EYE_SHOT_FIELDS = ('count', 'generation', 'serial', 'width', 'height', 'flat_screen', 'speed_mps',
                   'web_hand_gap_m', 'game_web', 'wrist_px', 'rope_start_px')


GAME_NAME = "Marvel's Spider-Man Remastered"  # its folder in Documents, and its log's name there
GAME_MEMORY_FIELDS = {'Working set': 'working_set_mb', 'Page file': 'commit_mb', 'Video Usage': 'video_usage_mb',
                      'Video Budget': 'video_budget_mb', 'Tex Usage': 'texture_usage_mb',
                      'Tex Budget': 'texture_budget_mb', 'Demoted': 'demoted_mb', 'fps': 'fps'}


def documents_folder():
    """The user's Documents folder, wherever OneDrive or the user has moved it."""
    known, path = (c.c_ubyte*16).from_buffer_copy(uuid.UUID('FDD39AD0-238F-46AF-ADB4-6C85480369C7').bytes_le), c.c_void_p()
    shell, ole = c.windll.shell32, c.windll.ole32
    shell.SHGetKnownFolderPath.argtypes = [c.c_void_p, wintypes.DWORD, wintypes.HANDLE, c.POINTER(c.c_void_p)]
    ole.CoTaskMemFree.argtypes = [c.c_void_p]
    if shell.SHGetKnownFolderPath(known, 0, None, c.byref(path)) or not path.value:
        return pathlib.Path.home()/'Documents'
    try:
        return pathlib.Path(c.wstring_at(path.value))
    finally:
        ole.CoTaskMemFree(path)


def game_memory(log):
    """The memory and frame rate line the game writes to its log about once a minute."""
    rows = []
    for line in log.splitlines():
        if '[Render] Working set:' not in line:
            continue
        found = dict(re.findall(r'(%s): ?([0-9.]+)' % '|'.join(GAME_MEMORY_FIELDS), line))
        if len(found) == len(GAME_MEMORY_FIELDS):
            rows.append(dict(time=line[:8], **{GAME_MEMORY_FIELDS[k]: float(v) for k, v in found.items()}))
    return rows


def keep_game_log(report, source=None):
    """Copy the game's log beside the report; the game overwrites it the next time it starts.

    Returns the copy's path and the game's memory lines, or (None, []) without a readable log.
    """
    source = source or documents_folder()/GAME_NAME/(GAME_NAME+'.log')
    try:
        log = source.read_text(encoding='utf-8', errors='replace')
        copy = report.with_name(report.stem+'-game.log')
        copy.write_text(log, encoding='utf-8')
    except OSError:
        return None, []
    return copy, game_memory(log)


class MemoryStatus(c.Structure):
    _fields_ = [('dwLength', wintypes.DWORD), ('dwMemoryLoad', wintypes.DWORD), ('ullTotalPhys', c.c_uint64),
                ('ullAvailPhys', c.c_uint64), ('ullTotalPageFile', c.c_uint64), ('ullAvailPageFile', c.c_uint64),
                ('ullTotalVirtual', c.c_uint64), ('ullAvailVirtual', c.c_uint64),
                ('ullAvailExtendedVirtual', c.c_uint64)]


def free_commit_mb():
    """Memory Windows can still promise to programs (RAM plus page file, minus what is committed)."""
    status = MemoryStatus(dwLength=c.sizeof(MemoryStatus))
    if not c.windll.kernel32.GlobalMemoryStatusEx(c.byref(status)):
        return None
    return status.ullAvailPageFile >> 20


class ProcessMemory(c.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD), ('PeakWorkingSetSize', c.c_size_t),
                ('WorkingSetSize', c.c_size_t), ('QuotaPeakPagedPoolUsage', c.c_size_t),
                ('QuotaPagedPoolUsage', c.c_size_t), ('QuotaPeakNonPagedPoolUsage', c.c_size_t),
                ('QuotaNonPagedPoolUsage', c.c_size_t), ('PagefileUsage', c.c_size_t),
                ('PeakPagefileUsage', c.c_size_t), ('PrivateUsage', c.c_size_t)]


def process_commit_mb(pid):
    """Memory Windows has promised a process (its private bytes), or None if it cannot be asked."""
    process = open_process(0x1000, False, pid)
    if not process:
        return None
    try:
        counters = ProcessMemory(cb=c.sizeof(ProcessMemory))
        query = c.windll.kernel32.K32GetProcessMemoryInfo
        query.argtypes = [wintypes.HANDLE, c.POINTER(ProcessMemory), wintypes.DWORD]
        return counters.PrivateUsage >> 20 if query(process, c.byref(counters), counters.cb) else None
    finally:
        close(process)


def scaled_eye_size(width, height, percent, limit=MAX_EYE_SIZE):
    """The eye size for `percent` of the runtime's recommended `width` x `height`, as spidy::scaledEyeSize
    computes it: the recommendation itself at 100, else in multiples of 8, shrunk with its shape to fit
    `limit` (the session's runtime may allow less)."""
    w, h = float(width), float(height)
    if percent != 100:
        w = max(64., math.floor(w*percent/800+.5)*8)
        h = max(64., math.floor(h*percent/800+.5)*8)
    fit = min(1., min(limit, MAX_EYE_SIZE)/w, min(limit, MAX_EYE_SIZE)/h)
    if fit < 1:
        w, h = math.floor(w*fit/8+1e-6)*8, math.floor(h*fit/8+1e-6)*8
    return int(w), int(h)


def eye_render_size(width, height, percent):
    """The size an eye's scene is rendered at for `percent` of a `width` x `height` eye, as
    spidy::eyeRenderSize computes it: in multiples of 8."""
    if percent >= 100:
        return width, height
    return max(64, (width*percent//100+7) & ~7), max(64, (height*percent//100+7) & ~7)


def rendering_line(width, height, percent, size, recommended, upscale=100, antialiased=True):
    """The console's line on the eye size the session renders, beside what the headset asked for, and on
    the size the eyes' scene is rendered at where that is smaller (upscaling)."""
    line = f'Rendering {width} x {height} pixels per eye'
    if size:
        line += ' (a square size instead of the headset\'s)'
    elif recommended:
        wanted = scaled_eye_size(*recommended, percent)
        of = f"{percent}% of the headset's {recommended[0]} x {recommended[1]}"
        line += f' ({of})' if (width, height) == wanted else \
            f' ({of} would be {wanted[0]} x {wanted[1]}, more than the VR runtime takes)'
    if not antialiased:
        return f'{line}, without anti-aliasing.'
    scene = eye_render_size(width, height, upscale)
    if scene == (width, height):
        return f'{line}, anti-aliased.'
    return f'{line}: the scene at {scene[0]} x {scene[1]} ({upscale}% per side), anti-aliased and upscaled.'


def eye_reflections(setting, refused=False, ray_traced=0):
    """Whether the eyes get the targets the game's screen-space reflections need, and the console's line.

    They follow the game's own setting (Graphics > Screen Space Reflections; `setting` is its saved value,
    0 for Off, None where the game never saved one) as it is when the session starts: an eye's buffers are
    made once. With it off, or with --no-eye-reflections (`refused`), the eyes are as before October 11's
    second build. So they are with ray-traced reflections on (`ray_traced`, the saved RTReflections): the
    game then takes another way through the pass, for which an eye's buffers lack the targets, and that
    was never run in the eyes."""
    if refused:
        return False, 'Reflections: none in the eyes (--no-eye-reflections).'
    if ray_traced:
        return False, ("Reflections: none in the eyes, as Ray-Traced Reflections is on in the game's graphics "
                       'settings and the eyes cannot have those. With it off and Screen Space Reflections on, '
                       'start VR again for reflections in the eyes.')
    if setting == 0:
        return False, ("Reflections: none in the eyes, as Screen Space Reflections is off in the game's "
                       'graphics settings. Turn it on there and start VR again to have them.')
    return True, ("Reflections: the game's screen-space reflections, in each eye (the game's Screen Space "
                  'Reflections setting, as it is when VR starts).')


# The console's line where a session turned the player's Ray-Traced Reflections off (vr_display.SESSION
# has why).
RAY_TRACING_OFF = ('Ray-Traced Reflections: off for VR. The eyes cannot have them, and with them on the game '
                   'ended with its display driver error (DXGI_ERROR_DEVICE_HUNG) as a menu, the map or a '
                   'cutscene opened. Your setting returns when the game closes.')


# The console's line where a session put the player's Aspect Ratio at Auto (vr_display.SESSION has why).
ASPECT_RATIO_AUTO = ("Aspect Ratio: Auto for VR. With a fixed shape chosen in the game's display settings, "
                     "players' games ended as VR started. Your setting returns when the game closes.")

# The console's line where a session turned the player's NVIDIA Reflex off (vr_display.SESSION has why).
REFLEX_OFF = ("NVIDIA Reflex: off for VR. The headset paces the game's frames, not the monitor, and one "
              "player's game with it on lost its graphics device as VR started. Your setting returns when the "
              'game closes.')


def device_removed(log):
    """The reason the game logged for losing its graphics device ('0x887A002B'), or None. Its own dialog
    says only "A problem has occurred with your display driver" with the same number."""
    found = re.search(r'Device removed detected \((0x[0-9A-Fa-f]{8})', log or '')
    return '0x'+found.group(1)[2:].upper() if found else None


def game_adapters(log):
    """The graphics adapters the game listed at its start, in its order. The October 16 player whose
    device was lost as VR began had his one RTX 4070 SUPER listed twice (adapters 0 and 1, NVIDIA's own
    list naming one), which no game log kept here has."""
    return re.findall(r'\[Render\]\s+Adapter \d+:\s*\n[^\n]*Description\s*:\s*([^\r\n]*?)\s*$', log or '', re.M)


# What the reasons the game logs are (DXGI's names).
DEVICE_REASONS = {'0x887A0005': 'DXGI_ERROR_DEVICE_REMOVED', '0x887A0006': 'DXGI_ERROR_DEVICE_HUNG',
                  '0x887A0007': 'DXGI_ERROR_DEVICE_RESET', '0x887A0020': 'DXGI_ERROR_DRIVER_INTERNAL_ERROR',
                  '0x887A002B': 'DXGI_ERROR_ACCESS_DENIED', '0x8007000E': 'E_OUTOFMEMORY'}


def device_lost_advice(reason, play_frames):
    """The console's lines after a session in which the game lost its graphics device (the game shows
    "A problem has occurred with your display driver"). `play_frames`: frames of VR play before it (the
    report's final `tracked`). Ray-Traced Reflections, a fixed Aspect Ratio, frame generation and NVIDIA
    Reflex are off for VR already (vr_display.SESSION); what is left to try is what the player can change."""
    name = DEVICE_REASONS.get(reason, 'unknown')
    when = 'as VR play began' if play_frames is not None and play_frames < 300 else 'in VR'
    return (f'The game lost its graphics device {when} ({reason}, {name}): that is its "problem has occurred '
            'with your display driver" message. Try these one at a time, pressing START VR after each: '
            'OPTIONS > Normal camera on the monitor: On; in the game\'s Display settings, Monitor: your main '
            'display; the newest graphics driver. If it happens again, post the session report in #spidy-bugs '
            'on Discord (press Reports, zip the three newest game-vr- files).')

# Windows' own libraries, by name. A program loads a library from its own folder first, so a file with
# one of these names beside Spider-Man.exe is neither Windows' nor the game's, whose folder holds none of
# them: it is how another mod, an overlay or a trainer gets into the game, and it takes the calls the
# game meant for Windows. REAL_VR: what the R.E.A.L. VR mod keeps there beside its dxgi.dll. With that
# mod in the game a player's headset stayed black on SteamVR and on Virtual Desktop alike: in the six
# sessions of October 10-12 that reached VR, none of 1,270 to 4,899 frames had a picture for the
# headset, slow motion never found the game's time scale where it looks (9702), and once the eyes were
# on another graphics device than the game's (3502). His folder and the game's module list of October
# 13 showed the mod's dxgi.dll loaded.
WRAPPERS = ('dxgi.dll', 'd3d12.dll', 'd3d11.dll', 'dinput8.dll', 'version.dll', 'winmm.dll', 'winhttp.dll',
            'xinput1_3.dll', 'xinput1_4.dll', 'xinput9_1_0.dll', 'dsound.dll', 'opengl32.dll')
REAL_VR = ('realvr.ini', 'realvr64.log', 'realconfig.bat', 'realrepo')


def other_mods(folder):
    """What else loads into the game from its own `folder`, by the names the files have there: (the
    R.E.A.L. VR mod's files, any other library that stands in for one of Windows'). One of the two is
    empty: R.E.A.L. VR's own library is among the first."""
    try:
        names = {entry.name.lower(): entry.name for entry in pathlib.Path(folder).iterdir()}
    except OSError:
        return [], []
    wrappers = [names[name] for name in WRAPPERS if name in names]
    real = [names[name] for name in REAL_VR if name in names]
    return (wrappers+real, []) if wrappers and real else ([], wrappers)


def real_vr_refusal(folder, files):
    """The error that ends a session in a game the R.E.A.L. VR mod is in."""
    return (f"The R.E.A.L. VR mod is in the game's folder ({', '.join(files)}). It loads into the game with "
            "Spidy and takes the game's picture for itself: the headset stays black and the controllers do "
            f'nothing. Close the game, move those files out of {folder} (Verify integrity of game files in '
            'Steam leaves them there), then press START VR.')


def other_mods_warning(folder, files):
    """The console's line on any other library in the game's folder that loads into the game."""
    return (f"WARNING: another mod or tool loads into the game from its folder ({', '.join(files)}). Spidy "
            'has not been run beside it. If the headset stays black or the game closes, close the game, move '
            f"{'it' if len(files) == 1 else 'them'} out of {folder} and press START VR again.")


def ray_traced_view(ray_traced):
    """Whether the game's own view keeps rendering in VR play, and the console's line (None: nothing to say).

    `ray_traced` is the saved RTReflections of the game that runs. A session turns it off in a game it
    starts (vr_display.SESSION), so it is on only in a game Spidy did not start, or one whose settings
    Windows kept from Spidy. There the view must not rest: the first frame it drew again hung the graphics
    device. It renders as with --game-monitor-view, about a fifth fewer frames."""
    if not ray_traced:
        return False, None
    return True, ('Ray-Traced Reflections is on in this game, so its own view keeps rendering beside the eyes '
                  '(about a fifth fewer frames): with that view resting, the game ended with its display '
                  'driver error as a menu opened. In a game Spidy starts itself the setting is off for VR: '
                  'close the game and press START VR.')


def command_text(command, timeout=10):
    """What a Windows command printed, '' where it could not run. Its text is in the console's code page,
    not Python's (UTF-8 under the launcher): read as Python's, one letter outside ASCII in it (a Windows
    in another language, an adapter's or a network's name) left no output at all, and no session of 0.2.13
    started on such a PC. A byte that is no letter there either becomes a question mark."""
    try:
        printed = subprocess.run(command, capture_output=True, timeout=timeout,
                                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0)).stdout
    except (OSError, subprocess.SubprocessError):
        return ''
    return (printed or b'').decode('oem', 'replace')


# A Steam Frame's own link. The headset makes a 6 GHz network of its own and the PC joins it through the
# adapter from the headset's box: it carries the headset's picture and is not the PC's way to the router.
# A Frame player's PC on October 13 was on "frame_CBC1602B" beside its network cable, and the session told
# them to turn the PC's Wi-Fi off. The name's form is from that one report.
HEADSET_LINK = re.compile(r'frame_[0-9A-Fa-f]{6,}$')


def network_links(wlan=None, addresses=None):
    """How this PC is on the network: the Wi-Fi network it is connected to (None without one) and whether
    a cable carries an address too. A headset's own network the PC is on (HEADSET_LINK) is no such Wi-Fi:
    it is named as `headset_link`. `wlan` and `addresses` are `netsh wlan show interfaces` and `ipconfig`
    output (run here when not given). Both are read by their English words: a Windows in another language
    shows neither."""
    wlan = command_text(['netsh', 'wlan', 'show', 'interfaces']) if wlan is None else wlan
    addresses = command_text(['ipconfig']) if addresses is None else addresses
    wifi = headset = None
    # One block for each Wi-Fi adapter, from its Name line on.
    for block in re.split(r'^(?=[ \t]*Name[ \t]*:)', wlan, flags=re.M):
        if not re.search(r'^\s*State\s*:\s*connected\s*$', block, re.M):
            continue
        name = re.search(r'^\s*SSID\s*:\s*(.*?)\s*$', block, re.M)
        name = name.group(1) if name and name.group(1) else 'Wi-Fi'
        if HEADSET_LINK.match(name):
            headset = headset or name
        else:
            wifi = wifi or name
    cable = False
    for block in re.split(r'^(?=\S)', addresses, flags=re.M):
        if block.startswith('Ethernet adapter') and 'Bluetooth' not in block.splitlines()[0] and \
                re.search(r'IPv4 Address', block):
            cable = True
    links = dict(wifi=wifi, cable=cable)
    if headset:
        links['headset_link'] = headset
    return links


def link_warning(links, runtime_name):
    """The console's warning for a PC that streams to the headset over Wi-Fi, or None.

    A wireless headset's picture comes over the network. From a PC that is on Wi-Fi itself every picture
    crosses the air twice, and the stream stalls for a second or two at a time whatever the game's frame
    rate: Virtual Desktop then holds each of the game's frames for a tenth of a second (`stream_stalls`).
    On October 11 the session with the PC on its cable alone had none; the three with it on Wi-Fi had 5 to
    8 each."""
    if not links.get('wifi') or not re.search(r'virtual desktop|steamvr|alvr', runtime_name or '', re.I):
        return None
    if links.get('cable'):
        return (f"WARNING: this PC is on Wi-Fi ({links['wifi']}) as well as on a network cable. A headset that "
                "connected over the Wi-Fi keeps using it, and its picture then stalls for a second or two at a "
                "time. Turn this PC's Wi-Fi off and connect the headset again.")
    return (f"WARNING: this PC is on Wi-Fi ({links['wifi']}), not on a network cable. A wireless headset's "
            'picture then crosses the Wi-Fi twice and stalls for a second or two at a time, whatever the frame '
            'rate. Connect this PC to the router by cable.')


def stream_stalls(samples):
    """Rounds of the headset's loop that the VR runtime held for about a tenth of a second (its submit
    timed out: Virtual Desktop was not taking frames), from the samples' `xr_ms`; and their seconds."""
    rounds = {s.get('frames') for s in samples if s.get('xr_ms') and s['xr_ms'][0] >= 90 and
              s['xr_ms'][XR_STAGES.index('end_frame')]+s['xr_ms'][XR_STAGES.index('wait_frame')] >= 80}
    return dict(rounds=len(rounds), seconds=round(len(rounds)/10, 1))


# What the headset's loop spent on its newest frame, in milliseconds, in a session's samples (`xr_ms`).
XR_STAGES = ('frame', 'wait_frame', 'tracking', 'prepare', 'acquire', 'copy_wait', 'hand_overlay', 'release',
             'end_frame')


def frame_rates(samples):
    """Measure active presentation; reused images are not new scene frames."""
    active = [s for s in samples if s.get('status') == 3 and s.get('submitted', 0) > 0]
    if len(active) < 2:
        return None
    # Older workers kept status=3 during cleanup. Ignore trailing samples of
    # the same stopped frame, while retaining dropped frames during active work.
    counter = 'frames' if all('frames' in s for s in active) else 'submitted'
    while len(active) > 1 and active[-1][counter] == active[-2][counter]:
        active.pop()
    if len(active) < 2:
        return None
    first, last = active[0], active[-1]
    seconds = last['seconds'] - first['seconds']
    if seconds <= 0:
        return None
    result = dict(sample_seconds=seconds, submitted_fps=(last['submitted']-first['submitted'])/seconds)
    for name, field in (('new_scene_fps', 'unique_submitted'), ('reused_fps', 'reused_submitted')):
        result[name] = (last[field]-first[field])/seconds if field in first and field in last else None
    return result


def accepted(result):
    """A stopped worker alone is not a successful presentation or clean test."""
    final, gpu, swing = (result.get(name) or {} for name in ('final', 'gpu', 'swing'))
    return bool(final.get('status') == 4 and not final.get('error', 1) and final.get('submitted', 0) > 0 and
                gpu.get('status') == 2 and not gpu.get('error', 1) and gpu.get('pairs', 0) > 0 and
                0 < gpu.get('fence', 0) <= gpu.get('completed', 0) < 0xffffffffffffffff and
                not swing.get('error', 0) and not any(s.get('error') for s in result.get('samples', [])) and
                not any(s.get('error') for s in result.get('swing_samples', [])) and
                not any(s.get('error') for s in result.get('motion_samples', [])) and
                result.get('game_entries_restored') and result.get('xr_stop') == 0 and result.get('bridge_stop') == 0)


def preflight(requested):
    """The session's OpenXR runtime, once it has a headset: the one asked for, or by default ('auto') the one
    the headset is connected to (xr_runtime.detect). No session is started. Returns (manifest, what the
    report says about it)."""
    automatic = not requested or str(requested).lower() == 'auto'
    if not automatic:
        manifest = xr_runtime.choose(requested)
        runtime = xr_runtime.name(manifest)
        print(f'VR runtime: {runtime} ({manifest}).', flush=True)
        found, said = xr_runtime.probe(manifest)
        if not found:
            raise RuntimeError(f'{said} Connect your headset in {runtime} before starting Spidy VR.')
    else:
        manifest, said = xr_runtime.detect(say=lambda line: print(line, flush=True))
        runtime = xr_runtime.name(manifest)
        print(f'VR runtime: {runtime} ({manifest}), found automatically.', flush=True)
    print(said, flush=True)
    headset = next((line[len('Headset available: '):].split(';')[0] for line in said.splitlines()
                    if line.startswith('Headset available: ')), None)
    eye = re.search(r'^Recommended eye 0: (\d+)x(\d+)', said, re.M)
    return manifest, dict(name=runtime, manifest=str(manifest), automatic=automatic, headset=headset,
                          recommended_eye=[int(eye[1]), int(eye[2])] if eye else None)


class _Echo:
    """A console stream that writes to the session's console log as well."""

    def __init__(self, stream, log):
        self.stream, self.log = stream, log

    def write(self, text):
        if self.stream:
            self.stream.write(text)
        try:
            self.log.write(text)
            self.log.flush()
        except (OSError, ValueError):
            pass  # a full disk must not end the session
        return len(text)

    def flush(self):
        if self.stream:
            self.stream.flush()

    def __getattr__(self, name):
        return getattr(self.stream, name)


def keep_console(report):
    """Everything the session prints goes to <report>-console.log too. The launcher's window used to be the
    only place it went: the October 7 Steam Frame player's reports folder said nothing about why VR never
    started. Returns the log's path, or None when it cannot be written."""
    path = report.with_name(report.stem+'-console.log')
    try:
        report.parent.mkdir(parents=True, exist_ok=True)
        log = path.open('a', encoding='utf-8')
    except OSError:
        return None
    sys.stdout, sys.stderr = _Echo(sys.stdout, log), _Echo(sys.stderr, log)
    return path


class Startup:
    """How far a session got before VR started. A session that ends there writes this as its report, with the
    game's log and the modules in the game's process (overlays and capture tools among them)."""

    def __init__(self, output):
        self.output, self.stage, self.reported, self.fields = output, 'finding the headset', False, {}

    def ended(self, error, modules_of=modules):
        if self.reported or (isinstance(error, KeyboardInterrupt) and 'game_pid' not in self.fields):
            return  # written already, or cancelled before anything started
        report = dict(stage=self.stage, error=str(error) or type(error).__name__,
                      cancelled=isinstance(error, KeyboardInterrupt), vr_started=False,
                      free_commit_mb=free_commit_mb(), **self.fields)
        try:
            if 'game_pid' in self.fields:
                copy, memory = keep_game_log(self.output)
                report.update(game_log=str(copy) if copy else None, game_memory=memory)
                try:
                    report['game_modules'] = sorted({m['name'] for m in modules_of(self.fields['game_pid'])})
                except (OSError, RuntimeError, ValueError):
                    report['game_modules'] = None  # the game has closed
            self.output.parent.mkdir(parents=True, exist_ok=True)
            self.output.write_text(json.dumps(report, indent=2)+'\n')
            self.reported = True
        except (OSError, ValueError):
            pass


def runtime_path(manifest):
    """The runtime manifest as the game XR worker's config carries it: UTF-16, 260 characters."""
    encoded = str(manifest).encode('utf-16-le')
    if len(encoded) > 518:
        raise RuntimeError(f'The OpenXR runtime path is too long: {manifest}')
    return encoded.ljust(520, b'\0')


def watch_stop_event(name):
    """Treat the named event as Ctrl+C. A launcher window without a console stops VR this way."""
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.OpenEventW.restype, kernel.OpenEventW.argtypes = wintypes.HANDLE, [wintypes.DWORD, wintypes.BOOL,
                                                                             wintypes.LPCWSTR]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    handle = kernel.OpenEventW(0x00100000, False, name)  # SYNCHRONIZE
    if not handle:
        raise c.WinError(c.get_last_error())

    def wait():
        if kernel.WaitForSingleObject(handle, 0xFFFFFFFF) == 0:
            _thread.interrupt_main()
    threading.Thread(target=wait, name='stop-event', daemon=True).start()


# game_screen_3d: the game screen with a scene's own two eye images on it (a cutscene in stereo).
PRESENTATIONS = {0: 'none', 1: 'immersive', 2: 'flat', 3: 'game_screen', 4: 'game_screen_3d'}
# Why a frame had no gameplay (game_xr.cpp GateReason).
GATE_REASONS = ((1, 'no_player'), (2, 'bridge_stopped'), (4, 'no_camera_commit'), (8, 'other_camera'),
                (16, 'tracking'))
# Camera movers (Camera2 vtables, image offsets) the gate has names for.
CAMERA_MOVERS = {0x3871fd8: 'follow', 0x38720d0: 'combat', 0x38727f0: 'death', 0x3872860: 'exterior',
                 0x38721e0: 'look', 0x38728f0: 'look_game', 0x3872a08: 'melee_animation',
                 0x385b3f0: 'relative_animation', 0x3872c18: 'turret', 0x3872d28: 'vehicle',
                 0x4f76d90: 'photo_mode'}


# What the VR settings (the SPIDY VR tab in the game's Settings, and X) changed, as XrData reports it. A session
# that ends with other values than it started with prints them on one line, from which the launcher
# starts the next session (launcher_text.hpp, headsetSettings).
SETTINGS_LINE = 'VR settings from the headset: '


def start_settings(a):
    """The VR settings a session starts with, as its samples' vr_settings report them; with them the player's T-pose
    calibration (millimetres, 0: none) and whether a session without one asks for it at its first gameplay."""
    return dict(aim_markers=not a.no_aim_markers, web_grab=not a.no_web_grab, air_webs=not a.no_air_webs,
                web_shooter=not a.no_web_shooter, punch=not a.no_punch, body=not a.no_body,
                swing_speed=round(a.swing_speed, 1), weight=a.weight,
                snap_turn=a.snap_turn, smooth_turn=a.smooth_turn, haptics=a.haptics, screen_size=a.screen_size,
                flips=a.flips, flip_speed=a.flip_speed, trigger_webs=a.trigger_webs, hud=a.hud,
                stand_on_walls=not a.no_stand_on_walls, screen_depth=a.screen_depth, arcade_webs=a.arcade_webs,
                body_haptics=a.body_haptics, eye_height_mm=a.eye_height, arm_length_mm=a.arm_length,
                calibration_prompt=not a.no_calibration_prompt)


def settings_line(start, sample):
    """The line for the launcher, or None while the settings are as the session started."""
    now = (sample or {}).get('vr_settings')
    if not now or now == start:
        return None
    return SETTINGS_LINE + ' '.join(f'{k}={int(round(v))}' for k, v in now.items())


PAD_IGNORED = ("WARNING: the game is not reading the VR controllers on its screen. Keep the game window in front "
               "on the desktop. If it is in front, close the game and start VR again from Spidy's launcher: a game "
               "Spidy starts reads controllers through XInput only, and one started otherwise can take SteamVR's "
               'virtual gamepads instead. If the buttons stay dead and the game shows keyboard keys in its prompts, '
               'turn Steam Input off for the game: in Steam, right-click the game > Properties > Controller > '
               'Disable Steam Input.')


def pad_ignored(watch, sample, quiet=5):
    """True once: the game screen showed and a button was held while the game had not read Spidy's controller
    for `quiet` seconds. `watch` (reads, since, warned) carries what earlier samples showed."""
    if watch['warned'] or not sample.get('pad_installed'):
        return False
    if sample.get('pad_reads') != watch['reads']:
        watch.update(reads=sample.get('pad_reads'), since=sample['seconds'])
        return False
    if sample.get('presentation') in ('game_screen', 'game_screen_3d') and \
            sample.get('pad_buttons', '0x0') != '0x0' and \
            sample['seconds']-watch['since'] >= quiet:
        watch['warned'] = True
        return True
    return False


# What the game makes while it runs - eye images, frames of its screen, camera commits, reads of Spidy's
# controller - and how long they may all stand still, seconds, before a game whose window Windows calls not
# responding counts as frozen (game_freeze.py).
FREEZE_COUNTERS = ('unique_submitted', 'screen_submitted', 'camera_commits', 'pad_reads')
FREEZE_SECONDS = 8


def game_frozen(watch, sample, quiet=FREEZE_SECONDS):
    """Whether the game has made none of FREEZE_COUNTERS for `quiet` seconds. `watch` carries the counters and
    since when they stood still. A game paused behind another window stands still too: the caller asks Windows
    whether its window still answers."""
    counters = tuple(sample.get(name) for name in FREEZE_COUNTERS)
    if counters != watch.get('counters'):
        watch.update(counters=counters, since=sample['seconds'])
        return False
    return sample['seconds']-watch['since'] >= quiet


def freeze_line(freeze):
    """What the session log says once the game froze and its threads were saved (or could not be)."""
    if not freeze.get('dump'):
        return (f"The game stopped answering {freeze['seconds']:.0f} s into VR (error {freeze.get('error')} saving "
                'where its threads stood). Close it from Task Manager if it stays frozen.')
    return (f"The game stopped answering {freeze['seconds']:.0f} s into VR. Where each of its threads stood is saved "
            f"in {pathlib.Path(freeze['dump']).name} ({freeze['dump_mb']} MB) beside the session report: send both, "
            'they show what the game waits for. Close it from Task Manager if it stays frozen.')


# The layout the VR runtime uses for a hand's controller (controller_layout.hpp ControllerKind).
CONTROLLER_KINDS = {0: None, 1: 'touch', 2: 'index', 3: 'steam_frame', 4: 'runtime'}


# Under SteamVR a Quest's left menu button is SteamVR's own (its dashboard), and so is an Index controller's
# system button: the game's pause comes from both thumbsticks held in (game_pad::PausePress).
STEAMVR_PAUSE = (' SteamVR keeps the menu button for its own dashboard: to pause the game, hold both thumbsticks '
                 'in for a moment.')


def controllers_line(controllers, runtime=None):
    """What the session log says about the layout the VR runtime uses for the controllers in the
    player's hands (left, right), or None while it has not said. runtime: the VR runtime's name."""
    kinds = {kind for kind in controllers if kind}
    if not kinds:
        return None
    if kinds == {'steam_frame'}:
        return ("Controllers: Steam Frame, with their own layout: A, B, X and Y on the right are Spidy's A, B, X "
                "and Y, Menu pauses, View opens the game menu and the D-pad is the game's (down heals).")
    pause = STEAMVR_PAUSE if runtime == 'SteamVR' else ''
    if 'runtime' in kinds:
        return ("Controllers: the VR runtime uses a layout of its own for them, so Spidy's buttons may sit "
                'elsewhere. Its controller settings show where.' + pause)
    names = {'touch': 'Touch', 'index': 'Index', 'steam_frame': 'Steam Frame'}
    return f"Controllers: the {' and '.join(sorted(names[kind] for kind in kinds))} layout." + pause


# The headset's eye tracking as XrData's eye_gaze gives it (XrRuntime::EyeTracking + 1; 0: not known yet).
EYE_TRACKING = {1: 'none', 2: 'no_tracker', 3: 'tracker'}


def eye_aim_line(eye_aim, runtime_name=None):
    """What the session log says about EYE AIM and the headset's eye tracking, or None: nothing while it is not
    known, nor for a headset without eye tracking whose player never asked for it."""
    tracking = eye_aim.get('eye_tracking')
    wanted = [name for name, on in (('web balls', eye_aim.get('web_balls')), ('zips', eye_aim.get('zips'))) if on]
    if tracking == 'tracker':
        if wanted:
            return f"Eye aim: the headset's eye tracker aims {' and '.join(wanted)}."
        return 'Eye tracking: this headset tracks the eyes. SPIDY VR > EYE AIM can aim web balls and zips with them.'
    if tracking and wanted:
        why = ('this VR runtime offers no eye tracking' if tracking == 'none' else
               'the VR runtime has no eye tracker for this headset')
        # Meta's runtime has eye gaze, and offers it only with eye tracking over Link switched on.
        link = (' A Quest Pro over Quest Link: switch on eye tracking over Link in the Meta Quest Link app '
                '(Settings > Beta, beside Developer Runtime Features), then start VR again.'
                if tracking == 'none' else '')
        # Virtual Desktop's offers it for every headset, and has a tracker only where the headset forwards the
        # eyes as the game starts VR (its runtime asks once per game).
        if tracking == 'no_tracker' and runtime_name == 'Virtual Desktop':
            link = (' A Quest Pro: switch on eye tracking in the headset (Settings > Movement tracking) and '
                    "Forward face/eye tracking to PC in Virtual Desktop's Streaming settings, then close the "
                    'game and start VR again.')
        return f'Eye aim: {why}, so the hands aim web balls and the head zips, as without it.' + link
    return None


# A demand as prompt_buttons::pack lays it out: the pad's button bits, then each trigger, the hold, and
# two bits an axis of each stick (0 no way, 1 back or left, 2 forward or right).
PAD_BUTTONS = ((0x1, 'D-pad up'), (0x2, 'D-pad down'), (0x4, 'D-pad left'), (0x8, 'D-pad right'),
               (0x10, 'Start'), (0x20, 'Back'), (0x40, 'L3'), (0x80, 'R3'), (0x100, 'LB'), (0x200, 'RB'),
               (0x1000, 'A'), (0x2000, 'B'), (0x4000, 'X'), (0x8000, 'Y'))
PAD_STICKS = ((19, 'left stick left', 'left stick right'), (21, 'left stick back', 'left stick forward'),
              (23, 'right stick left', 'right stick right'), (25, 'right stick back', 'right stick forward'))
# What VR play can send on its own, without the pad layer or the interact button carrying it
# (prompt_buttons::reachableInPlay): each bumper and each trigger only with its pair, L3 only with R3 (both
# triggers at the chest: the suit power), Back from the phone's MENU.
PLAY_BUTTONS = 0x1000 | 0x2000 | 0x8000 | 0x10 | 0x20 | 0x2 | 0x40 | 0x80 | 0x100 | 0x200


def demand_name(word):
    """What the game asked for, from a packed demand; None for nothing."""
    if not word:
        return None
    names = [name for bit, name in PAD_BUTTONS if word & bit]
    if word >> 16 & 1:
        names.append('LT')
    if word >> 17 & 1:
        names.append('RT')
    for shift, back, forward in PAD_STICKS:
        way = word >> shift & 3
        if way:
            names.append(back if way == 1 else forward)
    # A QTE that asks for a stick to be moved any way, and a prompt that named
    # a key or a mouse button (prompt_buttons packs those in bits 27 to 29).
    if word >> 27 & 1:
        names.append('left stick (any way)')
    if word >> 28 & 1:
        names.append('right stick (any way)')
    if word >> 29 & 1:
        names.append('a key or a mouse button')
    if not names:
        return None
    return ' + '.join(names) + (' (hold)' if word >> 18 & 1 else '')


def reachable_in_play(word):
    """Whether VR play sends that demand as it stands (prompt_buttons::reachableInPlay)."""
    if not word:
        return True
    if word >> 23 & 15 or word >> 28 & 1:                 # the right stick turns the player in play
        return False
    if (word >> 16 & 1) != (word >> 17 & 1):              # a trigger reaches the game only with its pair
        return False
    shoulders = word & 0x300
    if shoulders and shoulders != 0x300:
        return False
    if word & 0x40 and not word & 0x80:                   # L3 goes out only with R3
        return False
    return not word & 0xffff & ~PLAY_BUTTONS


# The VR control that sends each named button while the game names it (game_pad::NamedPress: press what you
# see), in PAD_BUTTONS' order; LT and RT are the triggers' own.
NAMED_CONTROLS = ((0x1, 'the right stick pushed up'), (0x2, 'the right stick pushed down'),
                  (0x4, 'the right stick pushed left'), (0x8, 'the right stick pushed right'), (0x10, 'Menu'),
                  (0x20, "the phone's MENU"), (0x40, 'a click of the left stick'), (0x80, 'a click of the right stick'),
                  (0x100, 'the left trigger'), (0x200, 'the right trigger'), (0x1000, 'A'), (0x2000, 'B'),
                  (0x4000, 'X'), (0x8000, 'Y'))


def named_controls(word):
    """What a player in a headset presses for a packed demand, or None for nothing. The game's right stick is
    not among them: in VR play that stick turns the player. Both bumpers together are X's (since October 24),
    unless the game names its own X too; a trigger is its bumper only when that one is named alone."""
    controls = []
    pair = word & 0x300 == 0x300 and not word & 0x4000
    for bit, control in NAMED_CONTROLS:
        if pair and bit in (0x100, 0x200):
            continue
        if word & bit and control not in controls:
            controls.append(control)
    if pair:
        controls.append('X')
    for shift, control in ((16, 'the left trigger'), (17, 'the right trigger')):
        if word >> shift & 1 and control not in controls:
            controls.append(control)
    for shift, back, forward in PAD_STICKS[:2]:
        way = word >> shift & 3
        if way:
            controls.append(back if way == 1 else forward)
    if word >> 27 & 1:
        controls.append('the left stick (any way)')
    if not controls:
        return None
    return ' + '.join(controls) + (', held' if word >> 18 & 1 else '')


def prompts_line(word):
    """What the session log says when the game asks a player for a button, or None for nothing."""
    asked = demand_name(word)
    if not asked:
        return None
    notes = []
    press = named_controls(word)
    if press:
        notes.append(f'in VR: {press}')
    if word >> 23 & 15 or word >> 28 & 1:
        notes.append("the game's right stick is not sent in VR play, where it turns you: SWITCH TO GAME "
                     'SCREEN in the pause menu plays the moment with the controllers as an Xbox controller')
    if word >> 29 & 1:
        notes.append('it shows a key or a mouse button: press something on the controllers so it shows the pad')
    return (f"The game is asking for {asked} - {'; '.join(notes)}. While the game names a button, the control "
            'of that name sends it; clicking both thumbsticks latches the whole pad.')


# The T-pose calibration's phases and what its panel asks for (body_calibration.hpp Phase, Hint).
CALIBRATION_PHASES = {0: 'none', 1: 'waiting', 2: 'holding', 3: 'done'}
CALIBRATION_HINTS = {0: None, 1: 'tracking', 2: 'stand_up', 3: 'arms_out', 4: 'straight', 5: 'look_ahead',
                     6: 'triggers', 7: 'still', 8: 'floor'}


# Why a grip pressed at the hip drew no phone (phone.hpp's PocketDraw::Miss): the headset had lost the hand,
# it was above the pocket or below it, too near the middle or too far out, or not behind.
POCKET_MISSES = ('lost', 'high', 'low', 'central', 'wide', 'ahead')


# Why the view rig could not place a frame of play (GameTrackingRig::Inactive bits).
TRACKING_GAP_REASONS = ((1, 'not_playing'), (2, 'unfocused'), (4, 'no_head'), (8, 'no_feet'), (16, 'no_camera'),
                        (32, 'frame_time'), (64, 'stale_display'), (128, 'no_eyes'))


def snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 1312)
        if len(raw) != 1312:
            return None
        if struct.unpack_from('<3I', raw) != (0x53585244, 43, 1312):
            raise RuntimeError('Game XR protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        result = dict(zip(('frames', 'tracked', 'submitted', 'dropped', 'left_hands', 'right_hands', 'serial', 'generation'),
                          struct.unpack_from('<8Q', raw, 24)))
        result.update(status=struct.unpack_from('<I', raw, 12)[0],
                      native_keys=struct.unpack_from('<I', raw, 88)[0],
                      error=struct.unpack_from('<I', raw, 92)[0],
                      head=struct.unpack_from('<16f', raw, 96),
                      hands=[struct.unpack_from('<16f', raw, 160+i*64) for i in range(2)],
                      message=raw[288:544].split(b'\0', 1)[0].decode('utf-8', 'replace'))
        result.update(zip(('unique_submitted','reused_submitted'),struct.unpack_from('<2Q',raw,544)))
        result.update(zip(('flat_screen','toggles','eye_width','eye_height'),struct.unpack_from('<4I',raw,560)))
        # What the last headset frame showed; game_screen frames showed the game's own camera on the
        # virtual screen because gameplay was unavailable (menus, hint cards, cutscenes, animated cameras).
        # Then the B presses the game got as its B (the dodge).
        presentation, dodges = struct.unpack_from('<2I', raw, 576)
        # screen_3d_submitted: the game-screen frames that showed a scene in stereo (among screen_submitted).
        result.update(presentation=PRESENTATIONS.get(presentation, presentation), dodges=dodges,
                      screen_submitted=struct.unpack_from('<Q', raw, 584)[0],
                      screen_3d_submitted=struct.unpack_from('<Q', raw, 808)[0])
        # Why gameplay was unavailable, and which camera the game used meanwhile (a scene that
        # stays on the screen although it is played: its camera is missing from the gate).
        gate, mover = struct.unpack_from('<2I', raw, 592)
        players, record = struct.unpack_from('<2Q', raw, 600)
        pad_buttons, pad_installed = struct.unpack_from('<2I', raw, 616)
        result.update(gate=[name for bit, name in GATE_REASONS if gate & bit],
                      camera_mover=CAMERA_MOVERS.get(mover, hex(mover) if mover else None),
                      players=players, player_record=hex(record), pad_buttons=hex(pad_buttons),
                      pad_installed=bool(pad_installed), pad_reads=struct.unpack_from('<Q', raw, 624)[0])
        # Camera commits, and those on the player by the follow or combat camera. Both rising while
        # the gate says other_camera: another camera commits after the player's every frame.
        result.update(zip(('camera_commits', 'player_commits'), struct.unpack_from('<2Q', raw, 632)))
        # Y presses the game got as its Y (interact, web strike), whether the aim markers show, and the
        # markers drawn so far (one per hand per headset frame).
        interacts, aim_markers = struct.unpack_from('<2I', raw, 648)
        # The walks a held Y began: the player stepping up to what the game's Y is for.
        result.update(interact_steps=struct.unpack_from('<I', raw, 800)[0])
        # Pushes of the right stick the game got as its D-pad down (heal) and as its LT + RT (the zip to a point),
        # with the zips sent as a hold of it was let go in the game's aim mode (ZipPress), counted again on their
        # own in zip_releases: the press went out a second time, to the point the aim held.
        result.update(zip(('heals', 'zips'), struct.unpack_from('<2I', raw, 816)))
        result.update(zip_releases=struct.unpack_from('<I', raw, 852)[0])
        # The game's own camera, which decides what its Y takes, against where the head looks: the angle between
        # them now in degrees (the head to the camera's right: more than 0; 0 while it is not steered), and the
        # times the virtual controller's right stick began to turn the camera there.
        look_error, look_turns = struct.unpack_from('<fI', raw, 824)
        result.update(look_error=round(look_error, 1), look_turns=look_turns)
        # The scans a trigger pulled at the temple began (the game's R3), and the Charge Jumps a held A
        # began (its RT + A).
        result.update(zip(('scans', 'charge_jumps'), struct.unpack_from('<2I', raw, 832)))
        # What the game asked a player for, read through its own UI (game_prompts.hpp): its calls into
        # Flash and the prompt and QTE interface's among them ('calls' at 0 while 'invokes' rises says the
        # names no longer match the game), prompts raised and taken away, texts that named a button and
        # texts rewritten for VR; what it is asking for now and the last thing it asked for, the asks VR
        # play could not have sent on its own (what the interact button carries) and texts that came with
        # the keyboard's icons, which name no pad button at all. Then the presses of the interact button
        # that sent an ask instead of the game's Y, and the pad layer's latches.
        invokes, calls, raised, cleared, texts, rewrites = struct.unpack_from('<6Q', raw, 856)
        demand, last_demand, beyond_play, keyboard_texts = struct.unpack_from('<4I', raw, 904)
        context_presses, pad_entries, pad_layer, wall_interacts = struct.unpack_from('<4I', raw, 920)
        # What the controllers themselves sent, beside what reached the game: a button in neither was
        # never pressed. ControllerButton bits, as xr_session gives them.
        vr_now, vr_played = struct.unpack_from('<2I', raw, 936)
        # Presses of the interact that handed the player back to the game's own wall crawl, because the
        # swing held him on a wall of its own and the game was offering nothing (game_pad::WallHandover).
        wall_handovers, handovers_offered = struct.unpack_from('<2I', raw, 944)
        # The eyes held out of what the world is built of (HeadClearance, version 37 in the slots of the
        # room around the player): frames they were moved more than a centimetre, and the most they were.
        head_push_frames, head_push_most_mm = struct.unpack_from('<2I', raw, 952)
        # The game's own wall crawl, now that the view turns onto it as it does onto a wall of the swing's.
        # Frames it held the player, and of those the frames the view had turned. Then what the game made of
        # the stick there (CrawlStick): the mean turn from the way sent to the way taken, in the surface's own
        # axes, how alike those turns were (near 1 a frame fixed against the surface, which the stick wants
        # rotating by that turn; near 0 no fixed frame at all, and the mover wants driving instead) and the
        # mean speed taken. The crawl is the only hold the game itself calls standing on a wall, so it is the
        # only one whose interacts work there.
        crawl_frames, crawl_stood, crawl_stick = struct.unpack_from('<3I', raw, 960)
        crawl_turn, crawl_agreement, crawl_speed = struct.unpack_from('<3f', raw, 972)
        # The head stopping short of the surface over the player (headroom): frames it was brought down, and
        # the least room there ever was over his feet. Inside the vent it stood at the player's own eye
        # height over its floor and so out through its roof.
        head_dropped, = struct.unpack_from('<I', raw, 984)
        headroom_least, = struct.unpack_from('<f', raw, 988)
        # Handing him from the swing's own wall to the game's, indoors (WallToGame): tries, those the game
        # answered by taking him - a vent that works - those it did not and the swing took the wall straight
        # back, and the frames he was driven into the wall. `handed` against `recovered` is the whole answer
        # to whether the game catches a player let go of at a wall.
        wtg_tries, wtg_handed, wtg_recovered, wtg_push = struct.unpack_from('<4I', raw, 992)
        result.update(wall_to_game=dict(tries=wtg_tries, handed=wtg_handed, recovered=wtg_recovered,
                                        push_frames=wtg_push))
        # The game's own crawl under the player at rest on the swing's wall (game_warp): crawls asked for,
        # those its state machine took, refused, faulted, and how the hand-overs ended (the crawl took him,
        # or the swing had him back); frames of the game's own wall run (its RT with the stick up the wall)
        # and frames the view held its surface through a gap in the swing's reports.
        (w_asked, w_applied, w_refused, w_faulted, w_taken, w_declined, w_run,
         w_grace) = struct.unpack_from('<8I', raw, 1008)
        result.update(warp=dict(asked=w_asked, applied=w_applied, refused=w_refused, faulted=w_faulted,
                                taken=w_taken, declined=w_declined, game_run_frames=w_run,
                                hold_grace_frames=w_grace))
        # Frames of play the view rig could not place, and every reason seen (one shorter than half a second
        # keeps the view standing where it stood); the game's crawl on a floor (a vent, a duct), the frames
        # the head was held at the crawl's own height there, and the frames the body knelt under a held head;
        # the game camera's look and up at the latest frame - the frame the game's crawl takes its stick in.
        gap_frames, gap_reasons, floor_frames, cap_frames, kneel_frames = struct.unpack_from('<5I', raw, 1040)
        camera_forward = struct.unpack_from('<3f', raw, 1064)
        camera_up = struct.unpack_from('<3f', raw, 1076)
        result.update(tracking_gaps=dict(frames=gap_frames,
                                         reasons=[name for bit, name in TRACKING_GAP_REASONS if gap_reasons & bit]),
                      floor_crawl=dict(frames=floor_frames, capped_frames=cap_frames, kneel_frames=kneel_frames),
                      game_camera=dict(forward=[round(v, 3) for v in camera_forward],
                                       up=[round(v, 3) for v in camera_up]))
        # The game's interactions on Spidy's wall (game_offers, offer_pointing): frames a hand pointed at one the
        # game would let the player use and at one to come closer to; presses of the interact that asked for a
        # use, those the game refused at once, those that came to the game's interact and of those the ones the
        # game had picked the offer for first, those given up (its crawl did not take him); the most offers it
        # gathered near him at once.
        # And whether its hooks went in (off, hooked, failed - with the reason: 9601 another game build).
        (o_point, o_reach, o_asks, o_refused, o_presses, o_picked, o_failed,
         o_seen) = struct.unpack_from('<8I', raw, 1088)
        o_state, = struct.unpack_from('<I', raw, 1060)
        # The module's own counts: updates run with the crawl's mask, offers checked against the crawl's pose,
        # masked updates whose gather checked any offer and the most at once, spot searches and those that found
        # one, and the offers near the player at the end.
        (o_masked, o_posed, o_gathered, o_most, o_searches, o_spots, o_near,
         o_retries) = struct.unpack_from('<8I', raw, 1120)
        result.update(offers=dict(hooks={0: 'off', 1: 'hooked', 3: 'failed'}.get(o_state & 0xff, o_state & 0xff),
                                  hook_error=o_state >> 8, point_frames=o_point, reach_frames=o_reach, asks=o_asks,
                                  refused=o_refused, presses=o_presses, picked=o_picked, failed=o_failed,
                                  most_seen=o_seen, masked_updates=o_masked, posed_checks=o_posed,
                                  gathers=o_gathered, most_checked=o_most, spot_searches=o_searches,
                                  spots_found=o_spots, near_now=o_near, press_retries=o_retries))
        # Spider-Sense in the headset (spider_sense.hpp): warnings that showed, frames that drew a mark over an
        # attacker and in the sightline (one out of view), tingles sent to the controllers, marks a counter broke,
        # and the game's dodge pressed while a warning stood (what a red mark asks for).
        s_shown, s_frames, s_sight, s_tingles, s_broken, s_dodges = struct.unpack_from('<6I', raw, 1152)
        result.update(sense_headset=dict(shown=s_shown, mark_frames=s_frames, sight_frames=s_sight,
                                         tingles=s_tingles, broken=s_broken, dodges=s_dodges))
        # Version 40: suit powers asked for with both hands at the chest, dodges made by moving out of a warned
        # attack's way, dives (the player's own flight into the marked enemy, feet first), and the blows whose
        # sound the headset played itself because the game's own was not there (game_sound.hpp).
        c_suit, c_moved, c_dives, c_own = struct.unpack_from('<4I', raw, 1176)
        result.update(combat_headset=dict(suit_powers=c_suit, move_dodges=c_moved, dives=c_dives,
                                          own_sounds=c_own))
        # Version 41: seated play (seated.hpp) - SEATED on and a seated eye height measured, HEIGHT (cm, 0 AUTO),
        # the seated eye height last measured and how far the tracking space is raised now, and how often it
        # was measured.
        s_flags, s_height, s_eye, s_lift, s_measures = struct.unpack_from('<5I', raw, 1192)
        result.update(seated=dict(on=bool(s_flags & 1), measured=bool(s_flags & 2), height_cm=s_height,
                                  seated_eye_m=s_eye/1000, lift_m=s_lift/1000, measures=s_measures))
        # Version 42: the phone drawn from a back pocket (phone.hpp's PocketDraw::Stats) - grip presses that may
        # have been for it, draws begun, phones out, draws too slow or let go of on the way, presses that began
        # none by why (POCKET_MISSES), and the latest such miss (0 none, else its POCKET_MISSES index + 1): why,
        # the hand from the neck in the body's level axes (m: x right, y up, z ahead), how long the headset had
        # lost it and how far the head looked from the body's way (degrees, left positive).
        (p_presses, p_begun, p_draws, p_slow, p_let_go, *p_missed, p_last, p_x, p_y, p_z, p_lost,
         p_look) = struct.unpack_from('<5I6II3iIi', raw, 1216)
        result.update(pocket=dict(
            presses=p_presses, begun=p_begun, draws=p_draws, too_slow=p_slow, let_go=p_let_go,
            missed={name: count for name, count in zip(POCKET_MISSES, p_missed) if count},
            last_miss=None if not 0 < p_last <= len(POCKET_MISSES) else dict(
                why=POCKET_MISSES[p_last - 1], hand_m=[p_x/1000, p_y/1000, p_z/1000], lost_s=p_lost/1000,
                look_deg=p_look/10)))
        # Version 43: EYE AIM - the headset's eye tracking (EYE_TRACKING; None until the XR session says), the
        # row (web balls, zips), frames of play with it on and those with the eyes tracked, web balls the eyes
        # aimed, and zips they aimed that went out with the game's camera on their point or, late, without.
        e_state, e_frames, e_gazed, e_shots, e_zips, e_late = struct.unpack_from('<6I', raw, 1288)
        result.update(eye_aim=dict(
            eye_tracking=EYE_TRACKING.get(e_state & 0xff), web_balls=bool(e_state >> 8 & 1),
            zips=bool(e_state >> 8 & 2), frames=e_frames, gaze_frames=e_gazed, shots=e_shots, zips_aimed=e_zips,
            zips_late=e_late))
        result.update(crawl=dict(frames=crawl_frames, stood=crawl_stood, stick_samples=crawl_stick,
                                 stick_turn_degrees=round(math.degrees(crawl_turn), 1),
                                 stick_agreement=round(crawl_agreement, 3),
                                 stick_speed=round(crawl_speed, 2)),
                      headroom=dict(dropped_frames=head_dropped, least=round(headroom_least, 2)),
                      head_clearance=dict(pushed_frames=head_push_frames, most_m=head_push_most_mm / 1000))
        result.update(vr_buttons=controller_buttons(vr_now), vr_buttons_played=controller_buttons(vr_played))
        result.update(prompts=dict(invokes=invokes, calls=calls, raised=raised, cleared=cleared, texts=texts,
                                   rewrites=rewrites, asking=demand_name(demand),
                                   last_asked=demand_name(last_demand), beyond_play=beyond_play,
                                   keyboard_texts=keyboard_texts, context_presses=context_presses,
                                   pad_entries=pad_entries, pad_layer=bool(pad_layer),
                                   wall_interacts=wall_interacts, wall_handovers=wall_handovers,
                                   wall_handovers_offered=handovers_offered),
                      prompt_demand=demand, prompt_last_demand=last_demand)
        # The layout the VR runtime uses for each hand's controller (left, right; None until it says), and
        # the layouts it took Spidy's controls for. A Steam Frame on 'touch' plays SteamVR's translation.
        left, right, layouts = struct.unpack_from('<3I', raw, 840)
        result.update(controllers=[CONTROLLER_KINDS.get(kind, kind) for kind in (left, right)],
                      controller_layouts=[name for kind, name in CONTROLLER_KINDS.items()
                                          if name and layouts & 1 << kind])
        result.update(interacts=interacts, aim_markers=bool(aim_markers),
                      markers=struct.unpack_from('<Q', raw, 656)[0])
        # The VR settings now (the SPIDY VR tab in the game's Settings changes them),
        # the tab's changes, the times the game built its Settings with it, whether its hooks are in, and
        # why it is missing (game_menu.hpp: 93xx-94xx not hooked, 95xx not built).
        flags, snap_turn, haptics, screen_size = struct.unpack_from('<4I', raw, 664)
        swing_speed, changes = struct.unpack_from('<fI', raw, 680)
        smooth_turn, = struct.unpack_from('<I', raw, 704)
        # The weight (percent of real gravity) and the HUD (0 off, 1-3 small to large).
        weight, hud = struct.unpack_from('<2I', raw, 744)
        # The T-pose calibration: its phase and what its panel asks for, the hold (0-1), the eye height and arm
        # length the body is sized to (mm, 0: the headset's running height and Spider-Man's own arms),
        # calibrations finished and skipped this session, whether a session without one asks for none at its
        # first gameplay (bit 0), and each arm's reach at the last one.
        phase, hint, progress = struct.unpack_from('<2If', raw, 752)
        eye_height, arm_length, calibrations, skips, calibration_flags = struct.unpack_from('<5I', raw, 764)
        reach = struct.unpack_from('<2f', raw, 784)
        # The flip speed: degrees a second at full tilt of the left stick. Then how far the view leans from the
        # world's up now, degrees: 90 while the player stands on a wall (STAND ON WALLS).
        flip_speed, view_tilt = struct.unpack_from('<If', raw, 792)
        # The GAME SCREEN 3D row: 0 off (cutscenes flat), 1 low, 2 medium, 3 high.
        screen_depth, = struct.unpack_from('<I', raw, 804)
        result.update(vr_settings=dict(aim_markers=bool(aim_markers), web_grab=bool(flags & 1),
                                       air_webs=bool(flags & 8), web_shooter=bool(flags & 16),
                                       punch=bool(flags & 2), body=bool(flags & 4),
                                       swing_speed=round(swing_speed, 1), weight=weight, snap_turn=snap_turn,
                                       smooth_turn=smooth_turn, haptics=haptics, screen_size=screen_size,
                                       flips=bool(flags & 32), flip_speed=flip_speed,
                                       trigger_webs=bool(flags & 64), hud=hud,
                                       stand_on_walls=bool(flags & 128),
                                       screen_depth=screen_depth, arcade_webs=bool(flags & 512),
                                       eye_height_mm=eye_height, arm_length_mm=arm_length,
                                       calibration_prompt=not calibration_flags & 1),
                      setting_changes=changes,
                      calibration=dict(phase=CALIBRATION_PHASES.get(phase, phase),
                                       hint=CALIBRATION_HINTS.get(hint, hint), progress=round(progress, 3),
                                       done=calibrations, skipped=skips, reach_m=[round(r, 3) for r in reach]))
        menu_tabs, menu_installed, menu_status = struct.unpack_from('<Q2I', raw, 688)
        result.update(menu_tabs=menu_tabs, menu_installed=bool(menu_installed), menu_status=menu_status)
        # Walls and ceilings the game held the player on (its wall crawl): the stretches and frames of play
        # on one, the player's up now (its actor's), the head's stand-off from the surface now, and on the
        # last frame on one the head's height over it as placed from the feet, before and after the stand-off.
        entries, frames = struct.unpack_from('<IQ', raw, 708)
        up = struct.unpack_from('<3f', raw, 720)
        stand_off, height, clearance = struct.unpack_from('<3f', raw, 732)
        result.update(surface=dict(entries=entries, frames=frames, hero_up=[round(v, 3) for v in up],
                                   stand_off_m=round(stand_off, 3), head_height_m=round(height, 3),
                                   head_clearance_m=round(clearance, 3), view_tilt_deg=round(view_tilt, 1)))
        return result
    return None


HERO_STATES = {0: 'unknown', 1: 'valid', 2: 'no_instance', 3: 'unregistered', 4: 'bad_transform'}
WEB_STATES = {0: 'off', 1: 'waiting', 2: 'game_webs', 3: 'failed'}


def lens_degrees(left, right, top, bottom):
    """Horizontal and vertical field of view of native lens tangents."""
    return [math.degrees(math.atan(right) - math.atan(left)), math.degrees(math.atan(bottom) - math.atan(top))]


BODY_STATES = {0: 'off', 1: 'waiting', 2: 'active', 3: 'failed'}
BODY_PROBLEMS = {0: None, 1: 'no_hero', 2: 'no_rig', 3: 'unknown_rig', 4: 'bad_rest_pose', 5: 'bad_instance'}


def body_snapshot(game, address):
    """The player's body on the hero (native_body::Status): whether it turns the hero's joints, how far each
    wrist and the head joint land from the controllers and the headset (metres), how far the hero turned
    between its pose job and the render (radians), which the body is off by while the hero turns, how often
    the hero's pose jobs changed rig (the body's blend restarted at each change before October 6 evening), the
    body's scale and its arms' on top of it, and whether those are from the player's T-pose calibration."""
    for _ in range(8):
        raw = game.read(address, 144)
        if len(raw) != 144:
            return None
        if struct.unpack_from('<3I', raw) != (0x53424453, 2, 144):
            raise RuntimeError('Body protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        v = struct.unpack('<4Iq3Q2I2Q3fI2ff2fIQ2IdfI', raw)
        return dict(state=BODY_STATES.get(v[3], v[3]), jobs=v[5], hero_jobs=v[6], solved=v[7],
                    problem=BODY_PROBLEMS.get(v[8], v[8]), joints=v[9], weight=round(v[12], 3),
                    scale=round(v[13], 4), yaw=round(v[14], 4), grounded=bool(v[15]),
                    hand_error_m=[round(v[16], 4), round(v[17], 4)], head_error_m=round(v[18], 4),
                    turn_last=round(v[19], 5), turn_max=round(v[20], 5), rig=hex(v[10]), rig_switches=v[21],
                    renders=v[22], hero_jobs_last_frame=v[23], hero_jobs_max=v[24], solve_ms=round(v[25], 3),
                    arm_scale=round(v[26], 4), calibrated=bool(v[27]))
    return None


BLOW_KINDS = ('hit', 'heavy', 'launch', 'juggle', 'slam', 'finisher')
CUE_WAYS = ('none', 'left', 'right', 'up', 'down')


def rope_snapshot(game, address):
    """Physical webs (game_swing::Ropes): the bends made round edges and taken off again, the webs cut off
    because something stayed in the way, the steps a web paid out while the game walked the player, each
    hand's web now (its bends, the rope beyond the last and all of it, metres), the times a hand took hold of the
    other hand's web, whether both hold one now, and (version 3) the thin or loose things arcade webs went
    through: a shot past one, a swing's line coming onto one."""
    for _ in range(8):
        raw = game.read(address, 96)
        if len(raw) != 96:
            return None
        magic, version, size = struct.unpack_from('<3I', raw)
        if (magic, size) != (0x53575250, 96) or version not in (2, 3):
            raise RuntimeError('Rope protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        wraps, unwraps, cut, paid_out = struct.unpack_from('<4Q', raw, 24)
        return dict(physical=bool(struct.unpack_from('<I', raw, 12)[0]), wraps=wraps, unwraps=unwraps, cut=cut,
                    paid_out=paid_out, bends=list(struct.unpack_from('<2I', raw, 56)),
                    free_m=[round(x, 2) for x in struct.unpack_from('<2f', raw, 64)],
                    whole_m=[round(x, 2) for x in struct.unpack_from('<2f', raw, 72)],
                    joins=struct.unpack_from('<Q', raw, 80)[0], shared=bool(struct.unpack_from('<I', raw, 88)[0]),
                    passed=struct.unpack_from('<I', raw, 92)[0] if version >= 3 else 0)
    return None
STRIKE_PHASES = ('idle', 'flying', 'hanging')


def strike_snapshot(game, address):
    """Fighting across the room (game_punch::Strikes): the web strikes opened, landed and missed, every blow
    landed (fists' and strikes') and of what kind, the thugs' flights given a blow's velocity, the strike in
    progress, the flow's count, and the latest strike and blow. One enemy at a time: `locked` is the enemy a blow
    locked the player onto, `knockouts` those his blows knocked out, `swung` the strikes a fist opened by
    travelling the called way, `strike_called` the strikes' blows that answered a call, and `passed_over` the
    swings that pointed at another enemy than the marked one and opened nothing."""
    for _ in range(8):
        raw = game.read(address, 328)
        if len(raw) != 328:
            return None
        if struct.unpack_from('<3I', raw) != (0x53535452, 3, 328):
            raise RuntimeError('Strike protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        opened, landed, missed, held = struct.unpack_from('<4Q', raw, 24)
        blows, heavies, launches, juggles, slams, finishers, flown, unflown = struct.unpack_from('<8Q', raw, 56)
        phase, hand, flow, enemies = struct.unpack_from('<4I', raw, 120)
        distance, seconds = struct.unpack_from('<2f', raw, 156)
        strength, damage = struct.unpack_from('<2f', raw, 188)
        kind, count, last_hand, by_strike, last_called = struct.unpack_from('<5I', raw, 196)
        # Called blows: the calls made, answered and taken away by another blow; and the call that shows
        # now (on whom, which way its arrow points, whether it calls the finisher, how much time it has).
        calls, called, missed_calls, cue_target = struct.unpack_from('<4Q', raw, 216)
        cue_way, cue_finisher, cue_left = struct.unpack_from('<2If', raw, 260)
        locked, knockouts = struct.unpack_from('<2Q', raw, 272)
        cue_locked, = struct.unpack_from('<I', raw, 300)
        swung, strike_called, passed_over = struct.unpack_from('<3Q', raw, 304)
        return dict(status=struct.unpack_from('<I', raw, 12)[0], opened=opened, landed=landed, missed=missed,
                    calls=calls, called=called, missed_calls=missed_calls, last_called=bool(last_called),
                    cue=dict(target=hex(cue_target), way=CUE_WAYS[cue_way] if cue_way < len(CUE_WAYS) else cue_way,
                             finisher=bool(cue_finisher), left=round(cue_left, 2), locked=bool(cue_locked),
                             point=[round(x, 3) for x in struct.unpack_from('<3f', raw, 248)]) if cue_target
                    else None,
                    locked=hex(locked), knockouts=knockouts,
                    knockout_point=[round(x, 3) for x in struct.unpack_from('<3f', raw, 288)],
                    swung=swung, strike_called=strike_called, passed_over=passed_over,
                    held=held, blows=blows, heavies=heavies, launches=launches, juggles=juggles, slams=slams,
                    finishers=finishers, flown=flown, unflown=unflown,
                    phase=STRIKE_PHASES[phase] if phase < len(STRIKE_PHASES) else phase, hand=hand, flow=flow,
                    enemies=enemies, target=hex(struct.unpack_from('<Q', raw, 136)[0]),
                    destination=[round(x, 3) for x in struct.unpack_from('<3f', raw, 144)],
                    distance=round(distance, 2), flight_seconds=round(seconds, 3),
                    last_point=[round(x, 3) for x in struct.unpack_from('<3f', raw, 164)],
                    last_direction=[round(x, 3) for x in struct.unpack_from('<3f', raw, 176)],
                    last_strength=round(strength, 3), last_damage=round(damage, 1),
                    last_kind=BLOW_KINDS[kind] if kind < len(BLOW_KINDS) else kind, last_count=count,
                    last_hand=last_hand, last_by_strike=bool(by_strike))
    return None


FOCUS_STATUS = {0: 'off', 2: 'running', 3: 'another build'}


def focus_snapshot(game, address):
    """The game's Focus for Spidy's blows (game_focus.hpp): the punches', strikes' and counters' and the web's
    yanks and throws, which the damage alone never earned. `asked` blows that asked for it, `given` those the game
    was given (its own amount for its own kind of blow), `missed` those that found no meter; `now` and `most` the
    meter after the latest given (its segments' fill and their most)."""
    for _ in range(8):
        raw = game.read(address, 56)
        if len(raw) != 56:
            return None
        if struct.unpack_from('<3I', raw) != (0x53464344, 1, 56):
            raise RuntimeError('Focus protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        status, = struct.unpack_from('<I', raw, 12)
        asked, given, missed = struct.unpack_from('<3Q', raw, 24)
        now, most = struct.unpack_from('<2f', raw, 48)
        return dict(status=FOCUS_STATUS.get(status, status), asked=asked, given=given, missed=missed,
                    now=round(now, 2), most=round(most, 2))
    return None


SOUND_STATUS = {0: 'off', 2: 'running', 3: 'another build'}


def sound_snapshot(game, address):
    """The game's own melee impact sounds for Spidy's blows (game_sound.hpp): `asked` blows that asked for one,
    `played` those the game played, `refused` those it gave no sound back for, `dropped` those that never reached
    it (nobody to sound it on, a full queue); `on_player` blows sounded on the player's own sound sources, the one
    struck having none; the sound sources the latest blow's switches were set on and the switches that took; `live`
    while the headset leaves the blows' sound to the game."""
    for _ in range(8):
        raw = game.read(address, 72)
        if len(raw) != 72:
            return None
        if struct.unpack_from('<3I', raw) != (0x53534e44, 1, 72):
            raise RuntimeError('Sound protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        status, = struct.unpack_from('<I', raw, 12)
        asked, played, refused, dropped = struct.unpack_from('<4Q', raw, 24)
        on_player, sources, live, switches = struct.unpack_from('<4I', raw, 56)
        return dict(status=SOUND_STATUS.get(status, status), asked=asked, played=played, refused=refused,
                    dropped=dropped, on_player=on_player, sources=sources, switches=switches, live=bool(live))
    return None


DODGE_STATUS = {0: 'off', 1: 'running', 3: 'another build'}
DODGE_ANSWERS = ('none', 'plain', 'accurate', 'perfect')


def dodge_snapshot(game, address):
    """The game's own dodge for a player a web carries (game_dodge.hpp): `frames` the hero's dodge manager was
    updated in; dodges `asked` for, those the game `taken` and its `answers` to them (none: no warning it could
    answer; plain: no immunity; accurate; perfect); dodges Spidy `ended` and those the game ended first
    (`cut_short`); the latest one's answer, the warnings standing as it started, its way and the player's speed
    then; `carried` while a web carries the player."""
    for _ in range(8):
        raw = game.read(address, 128)
        if len(raw) != 128:
            return None
        if struct.unpack_from('<3I', raw) != (0x53444744, 1, 128):
            raise RuntimeError('Dodge protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        status, = struct.unpack_from('<I', raw, 12)
        frames, asked, taken = struct.unpack_from('<3Q', raw, 24)
        answers = struct.unpack_from('<4Q', raw, 48)
        ended, cut_short = struct.unpack_from('<2Q', raw, 80)
        last, warnings, error, carried = struct.unpack_from('<4I', raw, 96)
        way = struct.unpack_from('<3f', raw, 112)
        speed, = struct.unpack_from('<f', raw, 124)
        return dict(status=DODGE_STATUS.get(status, status), frames=frames, asked=asked, taken=taken,
                    answers=dict(zip(DODGE_ANSWERS, answers)), ended=ended, cut_short=cut_short,
                    last_answer=DODGE_ANSWERS[last] if last < len(DODGE_ANSWERS) else last,
                    last_warnings=warnings, last_way=[round(x, 3) for x in way], last_speed=round(speed, 2),
                    error=error, carried=bool(carried))
    return None


SENSE_ANSWERS = ('none', 'counter', 'dodge', 'jump')
# BotAttackDodgeData's Properties, as the game's reflection table names the bits (spider_sense.hpp attack::).
ATTACK_FLAGS = ('dodgeable', 'counterable', 'never_beaten', 'dodge_button', 'jump_button', 'dodge_left',
                'dodge_right', 'dodge_away', 'dodge_toward', 'no_auto_camera', 'reversable', 'camera_on_jump',
                'no_ranged_fx')


def attack_flags(flags):
    """An attack's flags by name; bits above the game's own thirteen as one number."""
    names = [name for bit, name in enumerate(ATTACK_FLAGS) if flags >> bit & 1]
    return names + ([hex(flags & ~0x1fff)] if flags & ~0x1fff else [])


def sense_snapshot(game, address):
    """Spider-Sense (game_sense::Data): the attacks the game warned the player of, read from the hero's own
    HeroDodgeManager at each input sample. `read` of `samples` found the manager; `managers` counts a new one per
    hero; `attacks` it holds now and `most_attacks` at once. `warnings` went off, `asked` by what they asked for
    (counter, dodge, jump), `unknown` named no actor in play. Counters: `thrown` by a fist thrown at the
    attacker, `landed` by a blow landing during his warning, `perfect` in its last fifth of a second;
    `counter_blows` the blows after them, `breaks` the flinches sent as the fist went out. `standing` the warnings
    now, soonest blow first; `census` what the game's warnings were made of (each set of flags, how often, and
    the longest warning it gave, game seconds)."""
    for _ in range(8):
        raw = game.read(address, 632)
        if len(raw) != 632:
            return None
        # Telemetry only: a mismatch is reported, never a reason to end the session (prompt_snapshot's rule).
        if struct.unpack_from('<3I', raw) != (0x53534e53, 1, 632):
            magic, version, size = struct.unpack_from('<3I', raw)
            return dict(protocol_mismatch=dict(magic=hex(magic), version=version, bytes=size))
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        samples, read, faults = struct.unpack_from('<3Q', raw, 24)
        managers, most, attacks, count = struct.unpack_from('<4I', raw, 48)
        manager, now = struct.unpack_from('<Qd', raw, 64)
        warnings, *asked = struct.unpack_from('<5Q', raw, 80)
        unknown, thrown, landed, perfect, blows, breaks, last = struct.unpack_from('<7Q', raw, 120)
        last_point = struct.unpack_from('<3f', raw, 176)
        last_left, last_hand = struct.unpack_from('<fI', raw, 188)
        standing = []
        for i in range(min(count, 6)):
            at = 200+56*i
            attacker, = struct.unpack_from('<Q', raw, at)
            head = struct.unpack_from('<3f', raw, at+8)
            to_impact, warned, flags, state = struct.unpack_from('<2f2I', raw, at+32)
            answer, timed, countered = struct.unpack_from('<3B', raw, at+48)
            left, = struct.unpack_from('<f', raw, at+52)
            standing.append(dict(attacker=hex(attacker), head=[round(x, 2) for x in head],
                                 to_impact=round(to_impact, 3) if timed else None, warned=round(warned, 3),
                                 flags=attack_flags(flags), state=hex(state),
                                 answer=SENSE_ANSWERS[answer] if answer < 4 else answer,
                                 countered=bool(countered), left=round(left, 3)))
        flags = struct.unpack_from('<8I', raw, 536)
        counts = struct.unpack_from('<8I', raw, 568)
        leads = struct.unpack_from('<8f', raw, 600)
        census = [dict(flags=attack_flags(f), bits=hex(f), count=n, longest=round(lead, 3) if lead >= 0 else None)
                  for f, n, lead in zip(flags, counts, leads) if n]
        return dict(status=struct.unpack_from('<I', raw, 12)[0], samples=samples, read=read, faults=faults,
                    managers=managers, most_attacks=most, attacks=attacks, manager=hex(manager),
                    clock=round(now, 3), warnings=warnings, asked=dict(zip(SENSE_ANSWERS, asked)),
                    unknown=unknown, thrown=thrown, landed=landed, perfect=perfect, counter_blows=blows,
                    breaks=breaks, last_counter=hex(last), last_point=[round(x, 2) for x in last_point],
                    last_left=round(last_left, 3) if last_left >= 0 else None,
                    last_hand=('left', 'right', 'blow')[last_hand] if last_hand < 3 else last_hand,
                    standing=standing, census=census)
    return None


def punch_snapshot(game, address):
    """Punches (game_punch::Data): landed per hand, the game's damage requests issued and dropped, the bots
    within reach of a fist, and the latest punch. unnamed: the issued requests (punches, web pulls and web
    balls alike) that went out without their damager, to a bot whose attack responses have no AwarenessDB
    to look one up in (the game crashes on a damager there)."""
    for _ in range(8):
        raw = game.read(address, 168)
        if len(raw) != 168:
            return None
        if struct.unpack_from('<3I', raw) != (0x53505544, 1, 168):
            raise RuntimeError('Punch protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        status = struct.unpack_from('<I', raw, 12)[0]
        samples, punches, issued, dropped = struct.unpack_from('<4Q', raw, 24)
        bots, error = struct.unpack_from('<2I', raw, 56)
        hands = []
        for i in range(2):
            count, target, strength, speed, knockback, busy = struct.unpack_from('<2Q2f2I', raw, 64+i*32)
            hands.append(dict(punches=count, last_target=hex(target), last_strength=round(strength, 3),
                              speed=round(speed, 2), last_knockback=knockback, busy=bool(busy)))
        point, direction = struct.unpack_from('<3f', raw, 128), struct.unpack_from('<3f', raw, 140)
        damage, speed = struct.unpack_from('<2f', raw, 152)
        return dict(status=status, samples=samples, punches=punches, issued=issued, dropped=dropped,
                    unnamed=struct.unpack_from('<Q', raw, 160)[0], bots=bots,
                    error=error, hands=hands, last_point=[round(x, 3) for x in point],
                    last_direction=[round(x, 3) for x in direction], last_damage=round(damage, 1),
                    last_speed=round(speed, 2))
    return None


GADGETS = ('none', 'web_shooter', 'impact_web', 'web_bomb', 'electric_web', 'trip_mine', 'concussion_blast',
           'spider_drone', 'suspension_matrix', 'game_web_shooter')


def gadgets_of(raw):
    """The player's gadgets in game_shooter::Data (v4): the weapon manager, the equipped weapon's id, why none was
    read, each gadget with its place on the wheel and its charges, the latest equip's result and how many were asked
    for, the equipped gadget's shots from the hand, and the pulls that found it without a charge."""
    manager, count, error, equipped, equip_result, equips = struct.unpack_from('<Q2I2HI', raw, 192)
    owned = []
    for i in range(min(count, 8)):
        weapon, kind, place, ammo, most = struct.unpack_from('<H4B', raw, 216 + i * 8)
        owned.append(dict(gadget=GADGETS[kind] if kind < len(GADGETS) else kind, weapon=weapon, place=place,
                          ammo=ammo, most=most))
    gadget_shots, empty_pulls = struct.unpack_from('<2Q', raw, 280)
    return dict(weapon_manager=hex(manager), gadget_error=error, equipped=equipped, gadgets=owned,
                equip_result=equip_result, equips=equips, gadget_shots=gadget_shots, empty_pulls=empty_pulls)


def shooter_snapshot(game, address):
    """Web-shooter shots (game_shooter::Data): pulls that asked for a shot, shots the game fired and requests it
    dropped, those aimed at a thug and those whose target the game took, the hero's gadget, the main-thread frames
    the module saw, shots per hand, the latest shot, and what the shots struck (v2): shots that hit anything,
    webbing blows dealt to thugs, props knocked, and the latest actor hit (0x0: the world)."""
    for _ in range(8):
        raw = game.read(address, 296)
        if len(raw) != 296:
            return None
        if struct.unpack_from('<3I', raw) != (0x53484f44, 4, 296):
            raise RuntimeError('Shooter protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        status = struct.unpack_from('<I', raw, 12)[0]
        samples, requested, fired, dropped, targeted, resolved, weapon, frames = struct.unpack_from('<8Q', raw, 24)
        bots, error = struct.unpack_from('<2I', raw, 88)
        hands = [dict(shots=shots, last_target=hex(target))
                 for shots, target in (struct.unpack_from('<2Q', raw, 96+i*16) for i in range(2))]
        origin, aim_point = struct.unpack_from('<3f', raw, 128), struct.unpack_from('<3f', raw, 140)
        collisions, webbed, pushed, last_hit = struct.unpack_from('<4Q', raw, 160)
        return dict(status=status, samples=samples, requested=requested, fired=fired, dropped=dropped,
                    targeted=targeted, resolved=resolved, weapon=hex(weapon), frames=frames, bots=bots, error=error,
                    hands=hands, last_origin=[round(x, 3) for x in origin],
                    last_aim_point=[round(x, 3) for x in aim_point],
                    last_shot=hex(struct.unpack_from('<Q', raw, 152)[0]), collisions=collisions, webbed=webbed,
                    pushed=pushed, last_hit=hex(last_hit), **gadgets_of(raw))
    return None


def appearance_snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 360)
        if len(raw) != 360:
            return None
        if struct.unpack_from('<3I', raw) != (0x53415044, 6, 360):
            raise RuntimeError('Native appearance protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        state = struct.unpack_from('<I', raw, 12)[0]
        left, right, srgb_left, srgb_right, actor = struct.unpack_from('<5Q', raw, 24)
        hidden_frames, hides, restores = struct.unpack_from('<3Q', raw, 64)
        handle, flags = struct.unpack_from('<2I', raw, 88)
        near_left, near_right, near, model = struct.unpack_from('<4Q', raw, 96)
        near_distance, near_radius = struct.unpack_from('<2f', raw, 128)
        near_handle, near_flags = struct.unpack_from('<2I', raw, 136)
        anchored, rejected = struct.unpack_from('<2Q', raw, 144)
        anchor_last, anchor_max = struct.unpack_from('<2f', raw, 160)
        anchor_sum = struct.unpack_from('<d', raw, 168)[0]
        web_state, web_live = struct.unpack_from('<2I', raw, 176)
        web_creates, web_releases, web_failures, web_updates = struct.unpack_from('<4Q', raw, 184)
        active_aligned, active_rejected = struct.unpack_from('<2Q', raw, 216)
        active_bounds = struct.unpack_from('<4f', raw, 232)
        active_shift = struct.unpack_from('<f', raw, 248)[0]
        exposure_shared = struct.unpack_from('<I', raw, 252)[0]
        shared_hero, lag_last, lag_max, lag_sum = struct.unpack_from('<Q2fd', raw, 256)
        web_start_last, web_start_max = struct.unpack_from('<2f', raw, 280)
        gap_frames, gap_last, gap_max, gap_sum = struct.unpack_from('<Q2fd', raw, 288)
        active_lag_frames, active_lag_last, active_lag_max = struct.unpack_from('<Q2f', raw, 312)
        eye_scale, eye_detail, detail_frames, request_jobs, request_raise, impostor_frames = \
            struct.unpack_from('<2f2QfI', raw, 328)
        return dict(hidden_avatar=[left, right], srgb_overlay=[srgb_left, srgb_right], player_actor=hex(actor),
                    hero_state=HERO_STATES.get(state, state), hero_handle=hex(handle), hero_flags=hex(flags),
                    native_hidden_frames=hidden_frames, native_hides=hides, native_restores=restores,
                    near_instances=[near_left, near_right], near_instance=hex(near), near_model=hex(model),
                    near_distance=near_distance, near_radius=near_radius, near_handle=hex(near_handle),
                    near_flags=hex(near_flags), anchored_frames=anchored, anchor_rejected=rejected,
                    anchor_last_m=anchor_last, anchor_max_m=anchor_max,
                    anchor_mean_m=anchor_sum/anchored if anchored else 0.0,
                    web_state=WEB_STATES.get(web_state, web_state), web_live=[bool(web_live & 1), bool(web_live & 2)],
                    web_creates=web_creates, web_releases=web_releases, web_failures=web_failures,
                    web_updates=web_updates, active_view_aligned=active_aligned,
                    active_view_rejected=active_rejected,
                    active_view_fov_deg=lens_degrees(*active_bounds) if active_aligned else None,
                    active_view_shift_m=active_shift,
                    # Eye images exposed from the game's own view (two a frame); 0 = each eye on its own.
                    exposure_shared=exposure_shared, shared_hero_frames=shared_hero,
                    hero_lag_last_m=lag_last, hero_lag_max_m=lag_max,
                    hero_lag_mean_m=lag_sum/shared_hero if shared_hero else 0.0,
                    web_start_error_m=web_start_last if web_start_last >= 0 else None,
                    web_start_error_max_m=web_start_max if web_start_max >= 0 else None,
                    # Tracked web shooter to the game's rope in the same image; near zero when webs leave the hands.
                    web_hand_gap_frames=gap_frames, web_hand_gap_last_m=gap_last, web_hand_gap_max_m=gap_max,
                    web_hand_gap_mean_m=gap_sum/gap_frames if gap_frames else 0.0,
                    active_view_lag_frames=active_lag_frames, active_view_lag_last_m=active_lag_last,
                    active_view_lag_max_m=active_lag_max,
                    # The engine's distance scale for the left eye's lens (1 for its 68-degree lens) and the
                    # multiplier the eyes got for it; 0 frames = the eyes kept their lenses' own distances.
                    eye_distance_scale=round(eye_scale, 4), eye_detail=round(eye_detail, 4),
                    eye_detail_frames=detail_frames,
                    # Eye model jobs whose texture requests were sized for the eye's image rather than its
                    # rendered scene, and how many times stronger the last one asked (1 / upscale).
                    texture_request_jobs=request_jobs, texture_request_raise=round(request_raise, 4),
                    # Frames the left eye chose which far buildings draw their full models (lit windows),
                    # standing in for the game's own view while it does not render; 0 = the game's view did.
                    impostor_frames=impostor_frames)
    return None


def stream_snapshot(game, address):
    """SpidyStreamData (native_stream.hpp): the far buildings' cone about the head, the scene's slot pool their
    full models share with the nearby details, and the texture streamer's budget."""
    for _ in range(8):
        raw = game.read(address, 88)
        if len(raw) != 88:
            return None
        if struct.unpack_from('<3I', raw) != (0x53545244, 1, 88):
            raise RuntimeError('Native stream protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        features = struct.unpack_from('<I', raw, 12)[0]
        cone_frames, own_frames = struct.unpack_from('<2Q', raw, 24)
        margin, across, up_down = struct.unpack_from('<3f', raw, 40)
        capacity, used, most, full = struct.unpack_from('<3iI', raw, 52)
        texture_game, texture_budget, video_budget, video_usage, drops = struct.unpack_from('<5I', raw, 68)
        # Frames the left eye's query for far buildings took the cone (the lens over both eyes, widened by
        # the margin), and frames it kept its own lens while standing in for the game's view.
        return dict(cone=bool(features & 1), cone_frames=cone_frames, own_lens_frames=own_frames,
                    cone_margin_deg=round(margin, 2), cone_across_deg=round(across, 1),
                    cone_up_down_deg=round(up_down, 1),
                    # The slot pool: its size, slots in use at the last frame, the most, and frames 85% full.
                    pool_capacity=capacity, pool_used=used, pool_most=most, pool_full_frames=full,
                    # The texture budget, MB: the game's and the one in force, and the card's budget for the
                    # game and the game's use of it at the last look; drops = given back for lack of room.
                    texture_budget_raise=bool(features & 2), texture_game_mb=texture_game,
                    texture_budget_mb=texture_budget, video_budget_mb=video_budget, video_usage_mb=video_usage,
                    texture_drops=drops)
    return None


def eye_snapshot(game, address):
    """Header of the newest copy of the left eye as shown in the headset (include/spidy/eye_snapshot.hpp)."""
    for _ in range(8):
        raw = game.read(address, 112)
        if len(raw) != 112:
            return None
        if struct.unpack_from('<3I', raw) != (0x53455353, 1, 112):
            raise RuntimeError('Eye snapshot protocol mismatch')
        sequence = struct.unpack_from('<Q', raw, 16)[0]
        if sequence & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        flags = struct.unpack_from('<I', raw, 12)[0]
        count, pixels, generation, serial = struct.unpack_from('<4Q', raw, 24)
        width, height, row_pitch, flat = struct.unpack_from('<4I', raw, 56)
        speed, gap = struct.unpack_from('<2f', raw, 72)
        points = struct.unpack_from('<8f', raw, 80)
        pixel = lambda i: [points[i], points[i+1]] if points[i] >= 0 and points[i+1] >= 0 else None
        return dict(sequence=sequence, count=count, pixels=pixels, generation=generation, serial=serial,
                    width=width, height=height, row_pitch=row_pitch, flat_screen=bool(flat), speed_mps=speed,
                    web_hand_gap_m=gap if gap >= 0 else None, game_web=[bool(flags & 1), bool(flags & 2)],
                    wrist_px=[pixel(0), pixel(2)], rope_start_px=[pixel(4), pixel(6)])
    return None


def write_rgb_png(path, width, height, rgb):
    def chunk(name, payload):
        return struct.pack('>I', len(payload))+name+payload+struct.pack('>I', zlib.crc32(name+payload))
    rows = b''.join(b'\0'+rgb[y*width*3:(y+1)*width*3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(rows, 3))+chunk(b'IEND', b''))


def rgb_rows(data, row_pitch, left, top, width, height, step=1):
    """RGB bytes of every `step`-th pixel of a rectangle of RGBA rows. Returns (columns, rows, bytes)."""
    out = []
    columns = len(range(0, width, step))
    for y in range(top, top+height, step):
        row = data[y*row_pitch+left*4:y*row_pitch+(left+width)*4]
        rgb = bytearray(columns*3)
        for channel in range(3):
            rgb[channel::3] = row[channel::4*step]
        out.append(bytes(rgb))
    return columns, len(out), b''.join(out)


def remove_eye_files(folder, files):
    for name in files.values():
        if isinstance(name, str):
            (folder/name).unlink(missing_ok=True)


def crop_origin(center, size, limit):
    """Left or top edge of a `size`-pixel window around `center`, kept inside 0..limit."""
    return max(0, min(int(round(center))-size//2, limit-size))


def save_eye_snapshot(game, address, shot, folder, name, view=544, crop=512):
    """Save the newest eye copy: the whole image reduced to about `view` pixels, and a full-resolution
    `crop` around a hand that holds a game web. Returns the file names, or None if the copy was replaced
    or unreadable."""
    width, height, pitch = shot['width'], shot['height'], shot['row_pitch']
    if not (64 <= width <= MAX_EYE_SIZE and 64 <= height <= MAX_EYE_SIZE and width*4 <= pitch <= MAX_EYE_SIZE*4+4096) \
            or shot['pixels'] < 0x10000:
        return None
    size = pitch*(height-1)+width*4
    data = game.read(shot['pixels'], size)
    again = eye_snapshot(game, address)
    if len(data) != size or not again or (again['count'], again['sequence']) != (shot['count'], shot['sequence']):
        return None
    folder.mkdir(parents=True, exist_ok=True)
    files = {}
    step = max(1, math.ceil(max(width, height)/view))
    columns, rows, rgb = rgb_rows(data, pitch, 0, 0, width, height, step)
    write_rgb_png(folder/f'{name}-view.png', columns, rows, rgb)
    files['view'] = f'{name}-view.png'
    files['view_scale'] = step
    for hand, label in enumerate(('left', 'right')):
        point = shot['rope_start_px'][hand] or (shot['wrist_px'][hand] if shot['game_web'][hand] else None)
        if not point or not (0 <= point[0] < width and 0 <= point[1] < height):
            continue
        side = min(crop, width, height)
        left, top = crop_origin(point[0], side, width), crop_origin(point[1], side, height)
        columns, rows, rgb = rgb_rows(data, pitch, left, top, side, side)
        write_rgb_png(folder/f'{name}-{label}-hand.png', columns, rows, rgb)
        files[f'{label}_hand'] = f'{name}-{label}-hand.png'
        files[f'{label}_hand_origin'] = [left, top]
    return files


HUD_SHOTS = 6
HUD_CAPTURE_SKIPS = ('copied', 'no_resource', 'unknown_state', 'other_device', 'busy', 'format', 'failed')
HUD_CAPTURE_PATHS = ('none', 'texture', 'wrapped', 'target')


def hud_capture_snapshot(game, address):
    """The steadier-HUD probe (include/spidy/hud_capture.hpp): the panel texture's resource, its barriers
    and copies, the header of its newest snapshot, and STEADY HUD's layer (include/spidy/steady_hud.hpp)."""
    for _ in range(8):
        raw = game.read(address, 184)
        if len(raw) != 184:
            return None
        if struct.unpack_from('<3I', raw) != (0x53484350, 2, 184):
            raise RuntimeError('HUD capture protocol mismatch')
        sequence = struct.unpack_from('<Q', raw, 16)[0]
        if sequence & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        path = struct.unpack_from('<I', raw, 12)[0]
        resource = struct.unpack_from('<Q', raw, 24)[0]
        width, height, fmt, mips, flags, samples = struct.unpack_from('<6I', raw, 32)
        barriers, executed, other, odd, mismatched = struct.unpack_from('<5Q', raw, 56)
        state, known = struct.unpack_from('<2I', raw, 96)
        presents, copies = struct.unpack_from('<2Q', raw, 104)
        skip, refused = struct.unpack_from('<2I', raw, 120)
        snapshots, pixels = struct.unpack_from('<2Q', raw, 128)
        row_pitch, swap = struct.unpack_from('<2I', raw, 144)
        staged, layer_copies, layer_frames = struct.unpack_from('<3Q', raw, 152)
        stage_skip = struct.unpack_from('<I', raw, 176)[0]
        return dict(sequence=sequence, path=HUD_CAPTURE_PATHS[path] if path < len(HUD_CAPTURE_PATHS) else path,
                    found=bool(resource), width=width, height=height, format=fmt, mips=mips, flags=flags,
                    samples=samples, barriers=barriers, executed=executed, other_queue=other, odd=odd,
                    mismatched=mismatched, state=state, known=bool(known), presents=presents, copies=copies,
                    skip=HUD_CAPTURE_SKIPS[skip] if skip < len(HUD_CAPTURE_SKIPS) else skip, refused=refused,
                    snapshots=snapshots, pixels=pixels, row_pitch=row_pitch, swap=bool(swap), staged=staged,
                    layer_copies=layer_copies, layer_frames=layer_frames,
                    stage_skip=HUD_CAPTURE_SKIPS[stage_skip] if stage_skip < len(HUD_CAPTURE_SKIPS) else stage_skip)
    return None


def save_hud_capture(game, address, shot, folder, name, view=960):
    """Save the newest HUD snapshot: its colours and its alpha (as grey), reduced to about `view` pixels.
    Returns the file names, or None if the snapshot was replaced or unreadable."""
    width, height, pitch = shot['width'], shot['height'], shot['row_pitch']
    if not (64 <= width <= 16384 and 64 <= height <= 16384 and pitch == width*4) or shot['pixels'] < 0x10000:
        return None
    data = game.read(shot['pixels'], pitch*height)
    again = hud_capture_snapshot(game, address)
    if len(data) != pitch*height or not again or again['snapshots'] != shot['snapshots']:
        return None
    if shot['swap']:
        swapped = bytearray(data)
        swapped[0::4], swapped[2::4] = data[2::4], data[0::4]
        data = bytes(swapped)
    folder.mkdir(parents=True, exist_ok=True)
    step = max(1, math.ceil(max(width, height)/view))
    columns, rows, rgb = rgb_rows(data, pitch, 0, 0, width, height, step)
    write_rgb_png(folder/f'{name}-color.png', columns, rows, rgb)
    alpha = bytearray(data)
    alpha[0::4] = alpha[1::4] = alpha[2::4] = data[3::4]
    columns, rows, grey = rgb_rows(bytes(alpha), pitch, 0, 0, width, height, step)
    write_rgb_png(folder/f'{name}-alpha.png', columns, rows, grey)
    lit = sum(1 for a in data[3::4*step*step] if a)
    return dict(color=f'{name}-color.png', alpha=f'{name}-alpha.png', scale=step,
                covered=round(lit/max(1, len(data[3::4*step*step])), 4))


def timing_snapshot(game, address):
    names=('frame','wait_frame','tracking','prepare','acquire','copy_wait','hand_overlay','release','end_frame','display_period')
    for _ in range(8):
        raw=game.read(address,344)
        if len(raw)!=344:
            return None
        if struct.unpack_from('<3I',raw)!=(0x5358544d,1,344):
            raise RuntimeError('XR timing protocol mismatch')
        if struct.unpack_from('<Q',raw,16)[0]&1 or raw[16:24]!=game.read(address+16,8):
            continue
        result={}
        for i,name in enumerate(names):
            count,total,maximum,last=struct.unpack_from('<Q3d',raw,24+32*i)
            result[name]=dict(count=count,mean_ms=total/count if count else 0,max_ms=maximum,last_ms=last)
        return result
    return None


# Slow motion's counters (SlowMotionData in game_xr.cpp); a change of one is a sample.
def steam_snapshot(game, address):
    """The guard on the game's Steam ticket call (game_steam.hpp): whether it is in (status 9801: the game held
    no Steam interface yet), the tickets asked for, those from the game's call on its way to the main menu, of
    those the ones that came with something for an identity (the game passes none: its stack's), and the latest
    such something."""
    raw = game.read(address, 56)
    if len(raw) != 56 or struct.unpack_from('<3I', raw) != (0x53544b54, 1, 56):
        return None
    installed, status = struct.unpack_from('<2I', raw, 12)
    calls, from_game, cleared, identity = struct.unpack_from('<4Q', raw, 24)
    return dict(installed=bool(installed), status=status, calls=calls, from_game=from_game, cleared=cleared,
                last_identity=hex(identity))


SLOW_MOTION_COUNTS = ('starts', 'stops', 'refused', 'emptied', 'interrupted')
SLOW_MOMENTS = ('blow', 'called_blow', 'air_pull', 'perfect_dodge')


def slow_motion_snapshot(game, address):
    """Slow motion: the player's presses, focus and the game's time (status 0: the game's time is hooked)."""
    for _ in range(8):
        raw = game.read(address, 120)
        if len(raw) != 120:
            return None
        if struct.unpack_from('<3I', raw) != (0x574f4c53, 2, 120):
            raise RuntimeError('Slow motion protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        result = dict(status=struct.unpack_from('<I', raw, 12)[0])
        counts = struct.unpack_from('<6I', raw, 24)
        result.update(zip(SLOW_MOTION_COUNTS, counts[:5]), active=bool(counts[5]))
        # Real seconds in it so far, focus and how far into it now (0-1), the time scale asked of the
        # game; the game's own scale and its world's in its last time update, and Havok's step then.
        result.update(zip(('seconds', 'focus', 'blend', 'wanted', 'game_scale', 'world_scale', 'physics_step'),
                          (round(v, 6) for v in struct.unpack_from('<7f', raw, 48))))
        result['physics_scaled'] = bool(struct.unpack_from('<I', raw, 76)[0])
        result.update(zip(('updates', 'slowed_updates'), struct.unpack_from('<2Q', raw, 80)))
        # Moments (time slowed by itself): those begun by kind, whether one is on, and its time scale.
        moments = struct.unpack_from('<4I', raw, 96)
        active, scale = struct.unpack_from('<If', raw, 112)
        result.update(moments=dict(zip(SLOW_MOMENTS, moments)), moment_active=bool(active),
                      moment_scale=round(scale, 3))
        return result
    return None


# Body haptics on bHaptics devices (game_xr.cpp HapticsData; bhaptics_output.hpp's Status, body_haptics.hpp's
# Device bits and Kind order).
HAPTICS_STATUS = ('off', 'starting', 'no_library', 'no_app', 'no_player', 'waiting', 'connected', 'playing', 'failed',
                  'rejected')
HAPTIC_DEVICES = ((1, 'vest'), (2, 'left_sleeve'), (4, 'right_sleeve'), (8, 'visor'))
HAPTIC_KINDS = ('web_shot', 'web_attach', 'web_taken', 'web_release', 'zip', 'grab_caught', 'grab_yanked', 'grab_held',
                'grab_let_go', 'air_pull', 'web_pull', 'wall_land', 'wall_jump', 'landing', 'leap_charge', 'leap',
                'punch', 'heavy_blow', 'finisher', 'strike_rush', 'knockout', 'counter', 'spider_sense',
                'sense_dodge', 'hit', 'heal', 'slow_start', 'slow_stop', 'perfect_dodge', 'dodge', 'calibrated',
                'scan', 'gadget', 'phone', 'wall_grip')
HAPTICS_BYTES = 116 + 4*len(HAPTIC_KINDS)
# The groups the SPIDY VR tab's switches leave on (body_haptics.hpp's Group bits).
HAPTIC_GROUPS = ((1, 'webs'), (2, 'wind'), (4, 'combat'), (8, 'movement'))
# The output thread's ticks a second (bhaptics_output.hpp's tickMs).
HAPTIC_TICKS = 25


def haptics_snapshot(game, address):
    """Body haptics: what the connection to the bHaptics Player does (no_player for a PC without one), BODY
    HAPTICS now (percent; `written` once the session has said), the devices on now and at any time, the Player's
    connections, events posted, frames sent to a device and refused, blows the player took (the aimed ones placed
    by the attacker the game warned of), landings, the seconds of VR play with the Player connected and each
    device's share of them with something playing and its strongest motor's mean over those (percent), the
    groups the tab's switches leave on, and the events by kind that came (played or not)."""
    for _ in range(8):
        raw = game.read(address, HAPTICS_BYTES)
        if len(raw) != HAPTICS_BYTES:
            return None
        if struct.unpack_from('<3I', raw) != (0x53485044, 2, HAPTICS_BYTES):
            raise RuntimeError('Body haptics protocol mismatch')
        sequence = struct.unpack_from('<Q', raw, 16)[0]
        if sequence & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        status = struct.unpack_from('<I', raw, 12)[0]
        level, devices, seen, connects = struct.unpack_from('<4I', raw, 24)
        events, frames, refused = struct.unpack_from('<3Q', raw, 40)
        hits, aimed, landings, live = struct.unpack_from('<4I', raw, 64)
        on = struct.unpack_from('<4I', raw, 80)
        peaks = struct.unpack_from('<4I', raw, 96)
        kinds = struct.unpack_from(f'<{len(HAPTIC_KINDS)}I', raw, 112)
        groups = struct.unpack_from('<I', raw, 112 + 4*len(HAPTIC_KINDS))[0]
        played = {name: dict(share=round(on[i]/live, 3), mean_peak=round(peaks[i]/on[i], 1))
                  for i, (_, name) in enumerate(HAPTIC_DEVICES) if on[i] and live}
        return dict(written=sequence > 0,
                    status=HAPTICS_STATUS[status] if status < len(HAPTICS_STATUS) else status, level=level,
                    devices=[name for bit, name in HAPTIC_DEVICES if devices & bit],
                    devices_seen=[name for bit, name in HAPTIC_DEVICES if seen & bit], connects=connects,
                    events=events, frames=frames, refused=refused, hits=hits, aimed_hits=aimed, landings=landings,
                    live_seconds=round(live/HAPTIC_TICKS, 1), played=played,
                    groups=[name for bit, name in HAPTIC_GROUPS if groups & bit],
                    kinds={kind: n for kind, n in zip(HAPTIC_KINDS, kinds) if n})
    return None


def stage_haptics(digest):
    """bHaptics' library beside the staged stereo module (reports/stereo-modules/<digest>), which loads it from its
    own folder once body haptics are on (bhaptics_output.hpp). Without it a session plays no body haptics, and says
    so; a copy the game already holds stays."""
    source = ROOT/'build/windows-ninja/bhaptics_library.dll'
    target = ROOT/'reports/stereo-modules'/digest/source.name
    if not source.exists():
        return
    try:
        if not target.exists() or target.read_bytes() != source.read_bytes():
            shutil.copyfile(source, target)
    except OSError as error:
        print(f'WARNING: the bHaptics library could not be staged ({error}): no body haptics this session.',
              flush=True)


# The prompt and QTE interface's eight names, in the order PromptData counts them.
PROMPT_CALL_NAMES = ('ShowPlaceablePrompt', 'HidePlaceablePrompt', 'SetPlaceablePromptProgress',
                     'UpdateInWorldPrompt', 'POIInteractAvailable', 'AddQTE', 'UpdateQTE', 'ClearQTE',
                     'ShowText', 'HideText')


# The VR controllers' own buttons (ControllerButton, tracking.hpp). On Touch, A and B are the right
# controller's and X and Y the left one's, so a session where B arrives and Y never does says which hand.
CONTROLLER_BUTTONS = ((1, 'a'), (2, 'b'), (4, 'x'), (8, 'y'), (16, 'menu'), (32, 'view'),
                      (64, 'dpad_up'), (128, 'dpad_down'), (256, 'dpad_left'), (512, 'dpad_right'))


def controller_buttons(bits):
    """The names of the controller buttons in `bits`."""
    return [name for bit, name in CONTROLLER_BUTTONS if bits & bit]


def prompt_snapshot(game, address):
    """What the game asked a player for (game_prompts.hpp PromptData v2): its calls into Flash and the prompt
    and QTE interface's among them, prompts raised and taken away, texts that named a button and texts
    rewritten for VR, what it is asking for now and last, the asks VR play could not have sent on its own,
    and texts that came with the keyboard's icons.

    Then the two lists that say where a prompt's button and text actually arrive, no prompt text having
    reached the localized lookup on October 13: `recent`, the newest prompt calls with their arguments, and
    `census`, every distinct name the game's UI called with how often and one example of its arguments."""
    for _ in range(8):
        raw = game.read(address, 12112)
        if len(raw) != 12112:
            return None
        # A mismatch here is reported, never raised. Everything in this struct is
        # diagnostics, and on October 13 raising it ended three sessions: the module
        # still said version 1 while this reader wanted 2 (the census raised the size
        # but not the version), so the loop died and the game dropped out of VR to
        # the flat screen. Nothing a player is doing is worth that.
        magic, version, size = struct.unpack_from('<3I', raw)
        if (magic, version, size) != (0x53505244, 4, 12112):
            return dict(protocol_mismatch=dict(magic=hex(magic), version=version, bytes=size,
                                               wanted=dict(magic=hex(0x53505244), version=4, bytes=12112)))
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        text_at = lambda at, size: raw[at:at+size].split(b'\0', 1)[0].decode('utf-8', 'replace')
        result = dict(zip(('invokes', 'calls', 'raised', 'cleared', 'texts', 'rewrites'),
                          struct.unpack_from('<6Q', raw, 24)))
        demand, last_demand, beyond_play, keyboard_texts = struct.unpack_from('<4I', raw, 72)
        result.update(asking=demand_name(demand), last_asked=demand_name(last_demand),
                      beyond_play=beyond_play, keyboard_texts=keyboard_texts)
        lines = struct.unpack_from('<I', raw, 88)[0]
        counts = struct.unpack_from('<10I', raw, 96)
        result['by_name'] = {name: n for name, n in zip(PROMPT_CALL_NAMES, counts) if n}
        # One timeline: the game's prompt calls and this session's own presses, in order,
        # each stamped with the milliseconds since the first of them ('>>' marks ours).
        result['timeline'] = [t for t in (text_at(136+i*128, 128) for i in range(min(lines, 32))) if t]
        names = struct.unpack_from('<I', raw, 4232)[0]
        calls = struct.unpack_from('<48I', raw, 4240)
        census = []
        for i in range(min(names, 48)):
            example = text_at(4432+i*160, 160)
            if example:
                census.append(dict(call=example, times=calls[i]))
        result['census'] = sorted(census, key=lambda c: -c['times'])
        return result
    return None


def hud_snapshot(game, address):
    """The HUD in VR (native_hud::Data v3): its panel in front of the head, where the lazy follow turned it, its
    texture at the window's size, its second movie (markers, subtitles, prompts) drawn on it, markers projected
    onto it, and the HUD setting (size 0 off, 1-3 small to large) with what leaving it out of the eyes did."""
    for _ in range(8):
        raw = game.read(address, 192)
        if len(raw) != 192:
            return None
        if struct.unpack_from('<3I', raw) != (0x53485544, 3, 192):
            raise RuntimeError('HUD protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        result = dict(zip(('game_frames', 'placed_frames', 'rejected_frames'), struct.unpack_from('<3Q', raw, 40)))
        result.update(game_scale=[round(v, 4) for v in struct.unpack_from('<3f', raw, 64)])
        result.update(zip(('game_distance', 'view_half_width', 'distance', 'half_width', 'offset_mismatch_m'),
                          (round(v, 4) for v in struct.unpack_from('<5f', raw, 76))))
        result.update(texture=list(struct.unpack_from('<2I', raw, 96)),
                      game_texture=list(struct.unpack_from('<2I', raw, 104)))
        result.update(zip(('texture_resizes', 'texture_restores', 'layer_frames', 'marker_projections'),
                          struct.unpack_from('<4Q', raw, 112)))
        layer_status, restored = struct.unpack_from('<2I', raw, 144)
        result.update(layer_status=layer_status, restored=bool(restored))
        result['size'] = struct.unpack_from('<I', raw, 152)[0]
        # Markers are projected before the game moves the player, from the head foreseen for the frame: the
        # largest distance from it to the frame's head (0 from builds before October 10's fourth).
        result['marker_mismatch_m'] = round(struct.unpack_from('<f', raw, 156)[0], 4)
        result.update(zip(('hidden_frames', 'hidden_draws', 'followed_frames'), struct.unpack_from('<3Q', raw, 160)))
        result.update(zip(('follow_angle', 'follow_angle_max'),
                          (round(v, 2) for v in struct.unpack_from('<2f', raw, 184))))
        return result
    return None


def wait_for_renderer(game, process, render_data, timeout=180, problems=None, tell_every=20):
    """The game's graphics queue, as soon as the game draws frames (its intro, seconds after it starts).

    `render_data` is the render memory module's telemetry, when that module is loaded: its frame count
    shows the renderer running before the queue is looked for. `problems` collects why each look found
    no queue (for the session's report); the console hears the latest every `tell_every` seconds.
    """
    deadline = time.monotonic()+timeout
    problem = 'the game drew no frames'
    told, fronted = time.monotonic(), False
    while alive(game) and time.monotonic() < deadline:
        memory = render_memory_snapshot(game, render_data) if render_data else None
        if render_data and (not memory or memory['frames'] < 30):
            time.sleep(.25)
            continue
        try:
            return discover_queue(game, process)
        except TimeoutError as error:
            # A call into the game that never returned: asking again would leave another one waiting there.
            raise RuntimeError(f"The game did not answer Spidy's render probe: {error}") from error
        except GameNeedsRestart:
            raise  # another build's render probe in the game: asking for three minutes changes nothing
        except (OSError, RuntimeError) as error:
            # Startup can show no queue, or a second one, for a moment; a module list can fail meanwhile.
            problem = str(error)
            if problems is not None and problem not in problems[-1:]:
                problems.append(problem)
            if 'no graphics work' in problem and not fronted:
                fronted = True  # once: it taps Alt
                bring_to_front(game.pid)  # a minimized game may draw nothing
            if time.monotonic()-told >= tell_every:
                print(f"Still looking for the game's graphics queue: {problem}", flush=True)
                told = time.monotonic()
            time.sleep(.5)
    if not alive(game):
        raise RuntimeError('The game closed while starting.')
    raise RuntimeError(f'No game graphics queue within {timeout:.0f} seconds: {problem.rstrip(".")}.')


def game_running():
    try:
        find_game()
        return True
    except RuntimeError as error:
        return not str(error).startswith('Spider-Man is not running.')


# Spidy's modules start once per game process and stay in it until the game exits. The XR module goes in
# just before VR starts, so a game that has it has had its VR session: a second start returns 1000.
XR_MODULE = 'spidy_stereo_probe.dll'
VR_RAN = ('VR already ran in this game, and Spidy VR starts once per game launch. Close Spider-Man, then '
          'press START VR: the launcher starts the game again.')


def had_vr(pid, modules_of=modules):
    """Whether VR already ran in the game process `pid` (STOP VR, or VR that ended, leaves the game running)."""
    try:
        return any(m['name'].lower() == XR_MODULE for m in modules_of(pid))
    except (OSError, RuntimeError):
        return False  # closing, or still being set up; the XR start refuses a second session anyway


# The modules a session loads into the game, each staged in its folder under reports/ (bridge_game.prepare):
# the render memory module first, as the game starts, then the render probe, the bridges and the XR module.
SESSION_MODULES = (('spidy_render_memory.dll', 'render-memory-modules'), ('spidy_render_probe.dll', 'render-modules'),
                   ('spidy_bridge.dll', 'bridge-modules'), ('spidy_ray_bridge.dll', 'ray-modules'),
                   ('spidy_movement_bridge.dll', 'motion-modules'), (XR_MODULE, 'stereo-modules'))


def other_build(pid, modules_of=modules, root=ROOT):
    """Another Spidy build's module in the game process `pid`: (True, the Spidy folder it came from, None when
    its path says none), or None when every Spidy module there is this folder's build. A game left running
    while Spidy was updated, or one another Spidy folder put VR in, holds such a module until it exits, and
    each of this build's then fails to go in (flardy, October 16: "Different bridge build is resident")."""
    try:
        loaded = {m['name'].lower(): m['path'] for m in modules_of(pid)}
    except (OSError, RuntimeError):
        return None  # closing, or still being set up
    for name, staging in SESSION_MODULES:
        if name not in loaded:
            continue
        try:
            digest = hashlib.sha256((root/'build/windows-ninja'/name).read_bytes()).hexdigest()
        except OSError:
            digest = None
        if digest is None or not staged(loaded[name], root/'reports'/staging, digest):
            return True, spidy_folder(pathlib.Path(loaded[name]).resolve())
    return None


def restart_reason(pid, modules_of=modules):
    """Why the running game `pid` cannot start VR until it is closed and started again (VR ran in it, or
    another build's modules are in it), or None."""
    found = other_build(pid, modules_of)
    if found:
        return other_build_text(found[1])
    return VR_RAN if had_vr(pid, modules_of) else None


def exit_code(error):
    """What a session that could not start tells the launcher's window: 2, or the code at which it offers to
    start again as administrator, or to close the game and start VR again."""
    if isinstance(error, NeedsAdministrator):
        return NEEDS_ADMINISTRATOR_EXIT
    return CLOSE_GAME_EXIT if isinstance(error, GameNeedsRestart) else 2


def settle_display(running):
    """Restore the game settings saved for a VR launch (vr_display) once the game has closed."""
    if not vr_display.pending() or getattr(settle_display, 'deferred', False):
        return
    if running:
        settle_display.deferred = True
        print('Your game settings (display, frame generation, controllers) are restored after the game closes, '
              r'the next time Spidy VR starts it (or run tools\vr_display.py --restore).', flush=True)
        return
    try:
        if vr_display.restore():
            print('Restored your game settings (display, frame generation, controllers).', flush=True)
    except OSError as error:
        settle_display.deferred = True
        print(f'WARNING: Windows refused restoring your game settings ({error}); the next time Spidy VR '
              r'starts the game it tries again (or run tools\vr_display.py --restore).', flush=True)


def picture_kept(a):
    """The game's picture settings a session leaves as the player has them (vr_display.PICTURE names)."""
    return tuple(name for name, kept in (('depth_of_field', a.game_depth_of_field),
                                         ('sharpening', a.game_sharpening)) if kept)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--seconds', type=float, default=0,help='0 runs until game exit or Ctrl+C; 2..25 runs a timed test')
    p.add_argument('--size', type=int, default=0,help='0 uses the runtime recommendation; otherwise a square override')
    p.add_argument('--render-scale', type=int, default=100,
                   help="Eye resolution in percent of the runtime's recommendation, per side (50-200; default "
                        '100). Above 100 is sharper and costs frame rate and memory.')
    p.add_argument('--upscale', type=int, default=DEFAULT_UPSCALE,
                   help="Percent of the eye's size, per side, the eyes' scene is rendered at (50..100; default "
                        "%d): the game's temporal anti-aliasing brings it to the eye's size. 100 only "
                        'anti-aliases; lower is faster and softer.' % DEFAULT_UPSCALE)
    p.add_argument('--no-eye-antialiasing', action='store_true',
                   help='The eyes get no anti-aliasing and no upscaling: each is its scene as rendered, at the '
                        "eye's size (builds before October 10's eighth did; jagged and slower)")
    p.add_argument('--game-monitor-view', action='store_true',
                   help='The game renders a view of its own on your head for the monitor, as builds before '
                        "October 10's eighth did (about a fifth fewer frames); without it the monitor shows the "
                        'left eye')
    p.add_argument('--monitor-rate', type=int, default=DEFAULT_MONITOR_RATE,
                   help='How often the game window gets your left eye in immersive VR: pictures a second (1..240; '
                        'default %d), or 0 for every frame, as builds before October 11 did. Each one costs the '
                        'graphics card time the headset could have.' % DEFAULT_MONITOR_RATE)
    p.add_argument('--no-eye-reflections', action='store_true',
                   help="The eyes without the game's screen-space reflections, as builds before October 11's "
                        "second did. Without it they follow the game's own Screen Space Reflections setting")
    p.add_argument('--own-shadows', action='store_true',
                   help="Each eye makes its own set of the sun's shadow maps, as builds before October 11 did; "
                        'without it both use one set, fitted to a lens covering both (fewer render commands, '
                        'shadow edges a little softer)')
    p.add_argument('--building-reach', type=int, default=150,
                   help="How far away the far buildings draw their full models (lit windows, the rooms behind them) "
                        "in percent of the game's own distance, 145 m (50-400; default 150). More costs a little "
                        'frame rate and video memory')
    p.add_argument('--eye-lens-distances', action='store_true',
                   help="The eyes keep the distances their own wide lenses give the game, as builds before "
                        'October 19 did: models get their detail, people and props appear and textures sharpen '
                        "at about half the distance the flat game has them, and an upscaled eye's textures are "
                        'asked for at its smaller rendered size (about 2 percent more frames)')
    p.add_argument('--auto-launch',action='store_true')
    p.add_argument('--close-game', action='store_true',
                   help='A running game that already holds Spidy (VR ran in it, or another build is in it) is '
                        'closed as its close button does, then started again (the launcher asks the player first)')
    p.add_argument('--stop-after',type=float,default=0,help='End an untimed session after this many seconds for validation')
    p.add_argument('--capture-images', action='store_true',help='Enable diagnostic CPU eye readback (adds overhead)')
    p.add_argument('--overlay-webs', action='store_true',help="Draw Spidy's overlay webs instead of the game's web lines")
    p.add_argument('--no-web-grab', action='store_true', help='Webs never catch props or thugs; they only swing')
    p.add_argument('--no-air-webs', action='store_true',
                   help='A web that meets nothing within reach misses, instead of holding in open air 100 m out')
    p.add_argument('--flips', action='store_true',
                   help='Basic flips: airborne right stick; up prioritizes zip/aim, release holds, A returns upright')
    p.add_argument('--flip-speed', type=int, default=180,
                   help='How fast a flip turns you at full tilt of its steering stick, in degrees a second (90..480; '
                        'default 180)')
    p.add_argument('--game-depth-of-field', action='store_true',
                   help="Keep the game's own Depth of Field setting; without it a session turns it off (in the "
                        'headset the blur is not where you look)')
    p.add_argument('--game-sharpening', action='store_true',
                   help="Keep the game's own Sharpness setting; without it a session sets it to its lowest")
    p.add_argument('--no-stand-on-walls', action='store_true',
                   help='Walking a wall or stopped on it, your view stays upright beside it instead of turning so '
                        'that the wall is your floor')
    p.add_argument('--no-wall-run', action='store_true',
                   help="A player who flies into a wall is the game's, as before: its wall crawl holds him still, "
                        'then sticks him to the wall (for comparison; no setting offers it)')
    p.add_argument('--trigger-webs', action='store_true',
                   help='The trigger shoots and holds webs, and the grip reels in and shoots web balls (the two '
                        'swapped)')
    p.add_argument('--arcade-webs', action='store_true',
                   help='Arcade webs (WEB STYLE: ARCADE): webs go through lamp posts, signs, cars and other thin or '
                        'loose things, and swings stay above the street')
    p.add_argument('--no-body', action='store_true',
                   help="Keep the hero hidden in VR (gloves drawn over the image) instead of your own body")
    p.add_argument('--no-punch', action='store_true', help='Fists pass through thugs instead of punching them')
    p.add_argument('--no-web-shooter', action='store_true',
                   help="A free hand's trigger shoots nothing, instead of the game's web-shooter web balls")
    p.add_argument('--no-prompt-help', action='store_true',
                   help="Leave the game's own button prompts alone: Spidy neither reads which button they "
                        'ask for nor names the VR control in them, and the interact button is the game\'s Y '
                        'as before (the pad layer still gives the whole controller)')
    p.add_argument('--no-aim-markers', action='store_true',
                   help="Start with the aim markers (where each hand's web would land) hidden; AIM MARKERS in the "
                        "SPIDY VR tab shows them")
    p.add_argument('--no-eye-occlusion', action='store_true',
                   help='Each eye draws everything in its view, hidden behind buildings or not (builds before '
                        'October 6 did; about half the frame rate)')
    p.add_argument('--stock-monitor-view', action='store_true',
                   help="Keep the stock camera as the game's active view; culling and shadows then follow it, not the head")
    p.add_argument('--swing-speed', type=float, default=32, help='Native swing speed cap in m/s (default 32)')
    p.add_argument('--weight', type=int, default=100,
                   help='How heavy you are while webs fly you, in percent of real gravity (40..300; default 100)')
    p.add_argument('--eye-height', type=int, default=0,
                   help="Your eye height in millimetres from a T-pose calibration (with --arm-length): Spider-Man's "
                        'body is sized to it. 0 (default): none yet, the first gameplay asks for one')
    p.add_argument('--arm-length', type=int, default=0,
                   help="Your arm in millimetres from a T-pose calibration, from Spider-Man's shoulder at your size "
                        'to the wrist (with --eye-height)')
    p.add_argument('--no-calibration-prompt', action='store_true',
                   help='Without a calibration, do not ask for the T-pose at the first gameplay (the SPIDY VR tab '
                        'in the game\'s Settings still offers it)')
    p.add_argument('--snap-turn', type=int, default=30,
                   help='Degrees a flick of the right stick turns you (0: no snap turning; default 30)')
    p.add_argument('--smooth-turn', type=int, default=0,
                   help='Degrees a second the right stick turns you while held over, at full tilt '
                        '(0: it snap turns instead; default 0)')
    p.add_argument('--haptics', type=int, default=100, help='Controller vibration in percent (default 100)')
    p.add_argument('--body-haptics', type=int, default=100,
                   help='How strongly a bHaptics vest, sleeves and visor play, in percent (0: off; default 100). '
                        'They play through the bHaptics Player, which has to be running')
    p.add_argument('--screen-size', type=int, choices=(0, 1, 2), default=1,
                   help='The game screen for menus, cutscenes and flat mode: 0 small, 1 medium, 2 large')
    p.add_argument('--screen-depth', type=int, choices=(0, 1, 2, 3), default=2,
                   help='The depth cutscenes have on the game screen (the game renders them once for each eye): '
                        '0 off (flat), 1 low, 2 medium (default), 3 high, everything as far away as it is from '
                        "the game's camera")
    p.add_argument('--hud', type=int, choices=(0, 1, 2, 3), default=2,
                   help="The game's HUD in the headset: 0 off, 1 small (50 degrees across), 2 medium (60, the "
                        'default), 3 large (70)')
    p.add_argument('--full-desktop-view', action='store_true',
                   help="Start the game with its own display settings instead of a small VR window")
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/game-vr.json')
    p.add_argument('--xr-runtime', default='auto',
                   help='OpenXR runtime manifest, or auto (default): the runtime the headset is connected to '
                        "(Virtual Desktop, then one whose VR service runs, then Windows' active runtime)")
    p.add_argument('--stop-event', help='Named event that stops VR as Ctrl+C does (for a launcher window)')
    a = p.parse_args()
    if a.stop_after and (a.seconds or not 5<=a.stop_after<=300):
        p.error('--stop-after requires --seconds 0 and a value from 5 to 300')
    if (a.seconds!=0 and not 2 <= a.seconds <= 25) or (a.size!=0 and not 64 <= a.size <= MAX_EYE_SIZE) or \
            not 1 <= a.swing_speed <= 65:
        p.error(f'Use 0 or 2..25 seconds, 0 or 64..{MAX_EYE_SIZE} pixels, and 1..65 m/s')
    if not RENDER_SCALES[0] <= a.render_scale <= RENDER_SCALES[1] or (a.size and a.render_scale != 100):
        p.error('Use a render scale of %d..%d%%, and not with --size' % RENDER_SCALES)
    if not UPSCALES[0] <= a.upscale <= UPSCALES[1]:
        p.error('Use an upscale of %d..%d%%' % UPSCALES)
    if not 50 <= a.building_reach <= 400:
        p.error('Use a building reach of 50..400% of the game\'s distance')
    if not MONITOR_RATES[0] <= a.monitor_rate <= MONITOR_RATES[1]:
        p.error('Use a monitor rate of %d..%d a second (0: every frame)' % MONITOR_RATES)
    if not 0 <= a.snap_turn <= 90 or not 0 <= a.smooth_turn <= 360 or not 0 <= a.haptics <= 100:
        p.error('Use 0..90 degrees of snap turn, 0..360 degrees a second of smooth turn and 0..100% vibration')
    if not 0 <= a.body_haptics <= 100:
        p.error('Use 0..100% body haptics')
    if not 40 <= a.weight <= 300:
        p.error('Use a weight of 40..300% of real gravity')
    if not 90 <= a.flip_speed <= 480:
        p.error('Use a flip speed of 90..480 degrees a second')
    if (a.eye_height == 0) != (a.arm_length == 0) or a.eye_height and (
            not EYE_HEIGHTS_MM[0] <= a.eye_height <= EYE_HEIGHTS_MM[1] or
            not ARM_LENGTHS_MM[0] <= a.arm_length <= ARM_LENGTHS_MM[1]):
        p.error('Give both --eye-height (%d..%d mm) and --arm-length (%d..%d mm), or neither'
                % (*EYE_HEIGHTS_MM, *ARM_LENGTHS_MM))
    keep_console(a.output)
    startup = Startup(a.output)
    try:
        return session(a, startup)
    except BaseException as error:
        startup.ended(error)
        raise


def session(a, startup):
    """One VR session from the headset check to the report; `startup` follows it until VR starts."""
    if a.stop_event:
        watch_stop_event(a.stop_event)
    # First: nothing later can start VR in such a game.
    startup.stage = 'checking the running game'
    # An administrator's game is closed to a session without those rights (NeedsAdministrator).
    startup.fields['administrator'] = elevated()
    if game_running():
        pid = find_game()
        reason = restart_reason(pid)
        if reason and a.close_game:
            # The launcher asked the player first (kCloseGameExit). A game started by another Spidy folder left
            # the player's own display settings in that folder's backup: back they go before this one saves any.
            startup.stage = 'closing the game'
            found = other_build(pid)
            print('Closing Spider-Man as its close button does (it saves as it quits)...', flush=True)
            if not close_game(pid):
                raise RuntimeError('Spider-Man did not close within 30 seconds. Quit it from its own menu, then press '
                                   'START VR.')
            print('Spider-Man closed.', flush=True)
            elsewhere = found and found[1] and found[1].resolve() != ROOT
            if elsewhere:
                try:
                    vr_display.restore(backup=found[1]/'reports'/vr_display.BACKUP.name)
                except (OSError, RuntimeError, ValueError):
                    pass  # that folder's own next session restores them
        elif reason:
            raise GameNeedsRestart(reason)
    startup.stage = 'finding the headset'
    manifest, runtime = preflight(a.xr_runtime)
    startup.fields['xr_runtime'] = runtime
    links = network_links()
    startup.fields['network'] = links
    if link_warning(links, runtime.get('name')):
        print(link_warning(links, runtime.get('name')), flush=True)
    startup.stage = 'starting the game'

    def prepare_display():
        try:
            values = vr_display.prepare_launch(small=not a.full_desktop_view, keep=picture_kept(a))
        except OSError as error:
            # VR runs without them: the window only saves GPU time, and the two settings matter where
            # frame generation is on or under SteamVR.
            startup.fields['game_settings_refused'] = str(error)
            print(f"WARNING: Spidy could not apply the game's VR settings ({error}). "
                  'The game starts with its current settings: turn '
                  'frame generation off in its graphics settings, and a smaller window leaves more of the GPU '
                  "for the headset. Under SteamVR the game's menus may not follow the VR controllers.",
                  flush=True)
            if vr_display.pending():
                print('Some saved game settings still need restoring; Spidy will retry after the game closes.',
                      flush=True)
            return
        if values:
            print(f"Desktop view: {values['WindowWidth']} x {values['WindowHeight']} window to save GPU time; "
                  'your display settings return when the game closes.', flush=True)
        print('Game settings for VR: frame generation off, vertical sync off (the game would wait for the '
              "monitor), controllers through XInput only (SteamVR's virtual gamepads would take the menus from "
              'the VR controllers); they return when the game closes.', flush=True)
        changed = vr_display.session_changes()
        if changed.get('RTReflections'):
            print(RAY_TRACING_OFF, flush=True)
        if changed.get('ForcedAspectRatio'):
            print(ASPECT_RATIO_AUTO, flush=True)
        if changed.get('ReflexMode'):
            print(REFLEX_OFF, flush=True)
        kept = picture_kept(a)
        off = [text for name, text in (('depth_of_field', 'depth of field off'),
                                       ('sharpening', 'sharpening at its lowest')) if name not in kept]
        if off:
            print(f"Game picture for VR: {' and '.join(off)} (the launcher's options keep the game's own); "
                  'they return when the game closes.', flush=True)

    render_module = {}

    def render_memory_module(pid):
        # In time only in a game that has just started: it creates its render memory once. That is the graphics
        # card's memory for each frame's drawing, not the PC's (a player with 32 GB read it so on October 16).
        try:
            render_module.update(enlarge_render_memory(pid))
        except GameNeedsRestart:
            raise
        except (OSError, RuntimeError) as error:
            print(f"WARNING: Spidy's module for the game's per-frame graphics memory is unavailable ({error}).",
                  flush=True)

    # VR starts with the game: its intro, menus and loading show on a screen in the headset until
    # there is gameplay. The worker finds each player itself (a loaded save, a respawn, a character
    # switch); before October 5's seventh build the launcher found one, once, and VR stayed on the
    # screen after any reload.
    if a.auto_launch:
        game = wait_for_game(prepare=prepare_display, early=render_memory_module, ready=lambda game: True)
    else:
        game = Game(find_game())
        render_memory_module(game.pid)
    startup.fields['game_pid'] = game.pid
    startup.fields['steam_build'] = game.steam_build
    if not game.steam_build:
        print(f'This Spider-Man.exe is version {EXPECTED_VERSION} but not Steam\'s file (an Epic Games Store copy?). '
              'Spidy tries it: should the game\'s code differ where Spidy works with it, VR stops with a code '
              'to tell us.', flush=True)
    # Before anything of Spidy's goes into the game: another VR mod there leaves the headset black.
    startup.stage = "checking the game's folder"
    real_vr, others = other_mods(game.path.parent)
    startup.fields['other_mods'] = real_vr+others
    if real_vr:
        raise RuntimeError(real_vr_refusal(game.path.parent, real_vr))
    if others:
        print(other_mods_warning(game.path.parent, others), flush=True)
    startup.stage = "looking for the game's graphics queue"
    process = bridge = xr = rays = None
    bridge_active = xr_active = False
    previous_signal=None
    session_report = console_handler = None
    report_written = False
    closing, flushed = threading.Event(), threading.Event()
    try:
        pe = PE(game.path.read_bytes())
        original_entries = {rva:pe.bytes(rva,16) for rva in (*GAME_HOOKS, *RENDER_MEMORY_HOOKS)}
        if any(game.read(game.base+rva,16) != original_entries[rva] for rva in GAME_HOOKS):
            raise RuntimeError('A required game entry is already patched. Start a fresh game process.')
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        render_data = render_module.get('SpidyRenderMemoryData')
        queue = wait_for_renderer(game, process, render_data, problems=startup.fields.setdefault('queue_search', []))
        startup.fields['queue'] = hex(queue)
        startup.stage = "loading Spidy's modules into the game"
        if not bring_to_front(game.pid):
            print('Click the game window once: the game pauses while another window is in front.', flush=True)
        bridge, bridge_hash = prepare(game.pid, process,
            names=('SpidyStart', 'SpidyStop', 'SpidySubmit', 'SpidyRetarget', 'SpidyBridgeData',
                   'SpidyBridgeSample'))
        # No player yet: the VR worker hands the bridge each one it finds.
        config = struct.pack('<4I3Q', 0x53424346, 1, 40, game.pid, game.base, 0, 0)
        code = call_with_payload(process, bridge['SpidyStart'], config)
        if code:
            raise RuntimeError(f'Input bridge start: {code}')
        bridge_active = True
        bridge_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_bridge.dll')
        rays, ray_hash = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_ray_bridge.dll',
            ROOT/'reports/ray-modules', ('SpidyRayStart', 'SpidyRaySubmit', 'SpidyRayStop',
                                       'SpidyRaySample', 'SpidyRayData', 'SpidySwingData', 'SpidyGrabData',
                                       'SpidyPunchData', 'SpidyStrikeData', 'SpidyShooterData', 'SpidyRopeData',
                                       'SpidySenseData', 'SpidyFocusData', 'SpidySoundData',
                                       'SpidyDodgeData'))
        # Imported here: probe_game_grab imports from this module.
        from probe_game_grab import grab_snapshot
        ray_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_ray_bridge.dll')
        motion, motion_hash = prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_movement_bridge.dll',
            ROOT/'reports/motion-modules',('SpidyMotionStart','SpidyMotionSubmit','SpidyMotionSample',
                                         'SpidyMotionStop','SpidyMotionData'))
        motion_module = next(m['base'] for m in modules(game.pid) if m['name'].lower() == 'spidy_movement_bridge.dll')
        xr, xr_hash = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_stereo_probe.dll',
            ROOT/'reports/stereo-modules', ('SpidyXrStart', 'SpidyXrStop', 'SpidyXrKeepAlive', 'SpidyXrData',
                                           'SpidyGpuData', 'SpidyXrTimingData', 'SpidyAppearanceData',
                                           'SpidyStereoFrames', 'SpidyXrSnapshot', 'SpidyStereoData',
                                           'SpidyBodyData', 'SpidySlowMotionData', 'SpidyHudData',
                                           'SpidySteamData', 'SpidyPromptData', 'SpidyImpostorReach',
                                           'SpidyHapticsData', 'SpidyStreamData', 'SpidyHudCapture'))
        stage_haptics(xr_hash)
        saved = vr_display.Registry().read(('ScreenSpaceReflections', 'RTReflections'))
        reflections, reflections_line = eye_reflections(saved.get('ScreenSpaceReflections'), a.no_eye_reflections,
                                                        saved.get('RTReflections', 0))
        game_view, game_view_line = ray_traced_view(saved.get('RTReflections', 0))
        config = struct.pack('<4I7Q2IfI', 0x53585243, 24, 664, game.pid, game.base, queue,
                             bridge_module, ray_module, motion_module, 0, 0,
                             int(a.seconds*1000), a.size, a.swing_speed,
                             int(a.capture_images) | (2 if a.overlay_webs else 0) |
                             (4 if a.stock_monitor_view else 0) | (8 if a.no_web_grab else 0) |
                             (16 if a.no_eye_occlusion else 0) | (32 if a.no_body else 0) |
                             (64 if a.no_punch else 0) | (128 if a.no_aim_markers else 0) |
                             (256 if a.no_air_webs else 0) | (512 if a.no_web_shooter else 0) |
                             (1024 if a.no_calibration_prompt else 0) | (2048 if a.flips else 0) |
                             (4096 if a.trigger_webs else 0) | (8192 if a.no_stand_on_walls else 0) |
                             (16384 if a.no_wall_run else 0) | (0x8000 if a.no_eye_antialiasing else 0) |
                             (0x10000 if a.game_monitor_view or game_view else 0) |
                             (0x20000 if a.own_shadows else 0) | (0 if reflections else 0x40000) |
                             (0x100000 if a.no_prompt_help else 0) | (0x80000 if a.eye_lens_distances else 0) |
                             (0x200000 if a.arcade_webs else 0)) + \
            runtime_path(manifest) + struct.pack('<14I', a.snap_turn, a.haptics, a.screen_size, a.smooth_turn,
                                                 a.render_scale, a.weight, a.eye_height, a.arm_length, a.hud,
                                                 a.flip_speed, a.upscale, a.monitor_rate, a.screen_depth,
                                                 a.body_haptics)
        startup.stage = 'starting VR'
        code = call_with_payload(process, xr['SpidyXrStart'], config)
        if code:
            raise GameNeedsRestart(VR_RAN) if code == 1000 else RuntimeError(f'Game XR start: {code}')
        xr_active = True
        # The far buildings' full models, while the left eye chooses them for the game's resting view.
        if call_remote(process, xr['SpidyImpostorReach'], a.building_reach):
            print(f'WARNING: the building reach {a.building_reach}% was refused; the eyes keep 150%.', flush=True)
        started = time.monotonic()
        samples = deque(maxlen=12000)
        ray_samples = deque(maxlen=12000)
        swing_samples = deque(maxlen=12000)
        # The web grab: a sample when a counter or a hand's grab changes, else once a second.
        grab_samples = deque(maxlen=12000)
        motion_samples = deque(maxlen=12000)
        eye_shots = deque()
        eye_folder = a.output.with_name(a.output.stem+'-eyes')
        shot_count = 0
        # The steadier-HUD probe's snapshots, the newest few.
        hud_shots = deque()
        hud_folder = a.output.with_name(a.output.stem+'-hud')
        hud_shot_count = 0
        hud_capture = None
        appearance = streaming = None
        # The player's body and fists: the latest of each, and a sample when punches land.
        body = punch = None
        punch_samples = deque(maxlen=2000)
        strikes, strike_samples = None, deque(maxlen=2000)
        sense, sense_samples = None, deque(maxlen=2000)
        # The game's Focus for those blows: the latest.
        focus = None
        sound = None
        # The game's own dodges for a player a web carries: the latest.
        dodge = None
        # Read while the game runs: a session that ends with the game has nothing to read them from.
        ropes = steam = None
        # The web shooter: the latest, and a sample when a shot is asked for, fired or dropped.
        shooter = None
        shooter_samples = deque(maxlen=2000)
        # Slow motion: the latest, and a sample when it starts, ends or a press is refused.
        slow_motion = None
        slow_motion_samples = deque(maxlen=2000)
        # The HUD: the latest (native_hud::Data), for a report written after the game closed.
        hud_last = None
        # Body haptics: the latest, once the session has written it.
        haptics_last = None
        prompt_last = None
        eye_jobs = None
        timing = None
        render_memory = None
        lowest_commit = start_commit = free_commit_mb()
        printed_ring = False
        previous = None
        printed_controllers = printed_eye_aim = None
        printed_prompt = 0
        printed_dimensions=False
        renewed=0
        stop_requested=False
        settings_start = start_settings(a)
        # The game's reads of Spidy's controller: none while buttons are held on the game screen means its
        # menus cannot be played (the October 7 Steam Link session: the game had turned XInput off).
        pad_watch = dict(reads=None, since=0.0, warned=False)
        # A game that stops answering: the counters it stood still with, and its threads once saved.
        freeze_watch, freeze = {}, None

        # How the eyes are rendered: upscaled from `upscale` percent of their size per side and anti-aliased
        # (or neither), whether the game renders a view of its own for the monitor as well, how often the
        # monitor gets the left eye otherwise, whether both eyes use one set of sun shadow maps, and whether
        # they judge distances as the game's 68-degree lens does (appearance's eye_detail says by how much).
        eye_options = dict(upscale=100 if a.no_eye_antialiasing else a.upscale,
                           antialiased=not a.no_eye_antialiasing,
                           game_monitor_view=bool(a.game_monitor_view or a.stock_monitor_view or game_view),
                           monitor_rate=a.monitor_rate, shared_shadows=not a.own_shadows,
                           reflections=reflections, ray_traced=game_view, game_distances=not a.eye_lens_distances,
                           building_reach=a.building_reach,
                           render_size=None)

        def hand_over_settings(sample):
            """Tell the launcher what the VR settings were left at, for the next session. BODY HAPTICS rides its
            own struct, as written last."""
            if sample and sample.get('vr_settings') and haptics_last:
                sample = dict(sample, vr_settings=dict(sample['vr_settings'], body_haptics=haptics_last['level']))
            line = settings_line(settings_start, sample)
            if line:
                print(line, flush=True)

        def prompt_section(samples):
            """The report's prompts section, from the two structs that carry it.

            The census, the timeline and the game's own calls come from PromptData (`prompt_last`); the counters
            the session keeps - the interact's presses on a wall, its hand-overs to the game, the pad layer -
            ride XrData and reach here in the last sample. The report wants both, and only taking the first is
            how the hand-over counters went missing from the October 13 session that was meant to measure them.
            """
            out = dict(prompt_last or {})
            for key, value in ((samples[-1].get('prompts') if samples else None) or {}).items():
                out.setdefault(key, value)
            return out

        def session_report(**extra):
            """Everything sampled so far. It is written however the session ends."""
            return dict(pid=game.pid, eye_size=a.size, render_scale=a.render_scale, swing_speed=a.swing_speed,
                        eyes=eye_options, vr_settings=settings_start,
                        game_settings_refused=startup.fields.get('game_settings_refused'),
                        xr_runtime=runtime, motion_hash=motion_hash,
                        xr_hash=xr_hash, ray_hash=ray_hash, samples=list(samples),
                        swing_samples=list(swing_samples), motion_samples=list(motion_samples),
                        grab_samples=list(grab_samples), body=body, punch=punch,
                        punch_samples=list(punch_samples), strikes=strikes, strike_samples=list(strike_samples),
                        focus=focus, sense=sense, sense_samples=list(sense_samples), dodge=dodge,
                        ropes=ropes, steam_guard=steam,
                        shooter=shooter, shooter_samples=list(shooter_samples),
                        slow_motion=slow_motion, slow_motion_samples=list(slow_motion_samples), hud=hud_last,
                        haptics=haptics_last, prompts=prompt_section(list(samples)),
                        ray_samples=list(ray_samples), appearance=appearance, streaming=streaming,
                        eye_jobs=eye_jobs,
                        render_memory=render_memory, timing=timing, network=links,
                        stream_stalls=stream_stalls(list(samples)),
                        free_commit_mb=dict(start=start_commit, lowest=lowest_commit),
                        eye_snapshots=list(eye_shots), eye_snapshot_folder=str(eye_folder) if eye_shots else None,
                        hud_capture=hud_capture, hud_captures=list(hud_shots),
                        hud_capture_folder=str(hud_folder) if hud_shots else None,
                        frame_rates=frame_rates(list(samples)), freeze=freeze,
                        final=samples[-1] if samples else None, **extra)

        lost_device = {}

        def write_report(result):
            nonlocal report_written
            a.output.parent.mkdir(parents=True, exist_ok=True)
            # Texture and video memory pressure and the game's own frame rate, minute by minute.
            copy, memory = keep_game_log(a.output)
            result.update(game_log=str(copy) if copy else None, game_memory=memory)
            try:
                log = copy.read_text(encoding='utf-8', errors='replace') if copy else ''
            except OSError:
                log = ''
            reason = device_removed(log)
            result['game_adapters'] = game_adapters(log)
            if reason:
                result['device_removed'] = reason
                lost_device.update(reason=reason, frames=(result.get('final') or {}).get('tracked'))
            a.output.write_text(json.dumps(result, indent=2)+'\n')
            report_written = startup.reported = True

        def tell_device_lost():
            # Once, after the report that found it.
            reason = lost_device.pop('reason', None)
            if reason:
                print(device_lost_advice(reason, lost_device.get('frames')), flush=True)

        def request_stop(_signal,_frame):
            nonlocal stop_requested
            stop_requested=True
            print('Stopping VR and restoring game hooks...',flush=True)
        previous_signal=signal.signal(signal.SIGINT,request_stop)

        def console_event(kind):
            # Closing the console window (2), logoff (5), or shutdown (6) ends this process within
            # about five seconds. Hold Windows off until the report is on disk.
            if kind not in (2, 5, 6):
                return False
            closing.set()
            flushed.wait(4.5)
            return True
        console_handler = c.WINFUNCTYPE(wintypes.BOOL, wintypes.DWORD)(console_event)
        c.windll.kernel32.SetConsoleCtrlHandler(console_handler, True)
        loop_error = None
        try:
            while (not stop_requested and not closing.is_set() and
                   (not a.seconds or time.monotonic()-started < a.seconds+40) and alive(game)):
                if a.stop_after and time.monotonic()-started>=a.stop_after: break
                if not a.seconds and time.monotonic()-renewed>=1:
                    if call_remote(process,xr['SpidyXrKeepAlive']): break
                    renewed=time.monotonic()
                sample = snapshot(game, xr['SpidyXrData'])
                if sample:
                    sample['seconds'] = time.monotonic()-started
                    appearance = appearance_snapshot(game, xr['SpidyAppearanceData']) or appearance
                    sample['appearance'] = appearance
                    streaming = stream_snapshot(game, xr['SpidyStreamData']) or streaming
                    if streaming:
                        sample['streaming'] = {k: streaming[k] for k in ('cone_margin_deg', 'pool_used',
                                                                         'texture_budget_mb', 'video_usage_mb')}
                    body = body_snapshot(game, xr['SpidyBodyData']) or body
                    if body:
                        sample['body'] = {k: body[k] for k in ('state', 'problem', 'weight', 'hand_error_m',
                                                               'head_error_m', 'turn_last', 'grounded', 'scale',
                                                               'arm_scale', 'rig', 'rig_switches')}
                    landed = punch_snapshot(game, rays['SpidyPunchData'])
                    if landed:
                        # Thugs within reach of a fist, and each fist's speed relative to the head.
                        sample['punch'] = dict(bots=landed['bots'], punches=landed['punches'],
                                               speed=[h['speed'] for h in landed['hands']])
                        if any(landed[k] != (punch or {}).get(k) for k in ('punches', 'dropped', 'unnamed')):
                            punch_samples.append(dict(landed, seconds=round(time.monotonic()-started, 3)))
                        punch = landed
                    ropes = rope_snapshot(game, rays['SpidyRopeData']) or ropes
                    steam = steam_snapshot(game, xr['SpidySteamData']) or steam
                    fight = strike_snapshot(game, rays['SpidyStrikeData'])
                    if fight:
                        # One row per web strike opened or ended and per blow landed.
                        if any(fight[k] != (strikes or {}).get(k)
                               for k in ('opened', 'landed', 'missed', 'blows', 'calls', 'knockouts', 'locked',
                                         'passed_over')):
                            strike_samples.append(dict(fight, seconds=round(time.monotonic()-started, 3)))
                        strikes = fight
                    focus = focus_snapshot(game, rays['SpidyFocusData']) or focus
                    sound = sound_snapshot(game, rays['SpidySoundData']) or sound
                    dodge = dodge_snapshot(game, rays['SpidyDodgeData']) or dodge
                    senses = sense_snapshot(game, rays['SpidySenseData'])
                    if senses and 'protocol_mismatch' in senses:
                        sense = senses
                    elif senses:
                        # Warnings standing now; one row per warning gone off and per counter.
                        sample['sense'] = dict(standing=len(senses['standing']), attacks=senses['attacks'])
                        if any(senses[k] != (sense or {}).get(k)
                               for k in ('warnings', 'thrown', 'landed', 'counter_blows', 'breaks', 'managers')):
                            sense_samples.append(dict(senses, seconds=round(time.monotonic()-started, 3)))
                        sense = senses
                    shots = shooter_snapshot(game, rays['SpidyShooterData'])
                    if shots:
                        sample['shooter'] = {k: shots[k] for k in ('status', 'fired', 'dropped', 'bots', 'collisions',
                                                                   'webbed')}
                        if any(shots[k] != (shooter or {}).get(k)
                               for k in ('requested', 'fired', 'dropped', 'error', 'collisions', 'webbed', 'pushed')):
                            shooter_samples.append(dict(shots, seconds=round(time.monotonic()-started, 3)))
                        shooter = shots
                    slow = slow_motion_snapshot(game, xr['SpidySlowMotionData'])
                    if slow:
                        sample['slow_motion'] = {k: slow[k] for k in ('active', 'focus', 'wanted', 'world_scale')}
                        if any(slow[k] != (slow_motion or {}).get(k) for k in SLOW_MOTION_COUNTS):
                            slow_motion_samples.append(dict(slow, seconds=round(time.monotonic()-started, 3)))
                        slow_motion = slow
                    # Body haptics: whether the Player and the devices are there, and what they played.
                    felt = haptics_snapshot(game, xr['SpidyHapticsData'])
                    if felt and felt['written']:
                        haptics_last = felt
                        sample['haptics'] = {k: felt[k] for k in ('status', 'devices', 'frames', 'hits', 'landings')}
                        if sample.get('vr_settings'):
                            sample['vr_settings']['body_haptics'] = felt['level']
                    # What the game asked for, and the census of the calls its UI
                    # makes: kept for the report however the session ends.
                    prompt_last = prompt_snapshot(game, xr['SpidyPromptData']) or prompt_last
                    hud = hud_snapshot(game, xr['SpidyHudData'])
                    capture = hud_capture_snapshot(game, xr['SpidyHudCapture'])
                    if capture:
                        hud_capture = capture
                        sample['hud_capture'] = {k: capture[k] for k in ('found', 'known', 'copies', 'snapshots',
                                                                         'skip', 'state', 'executed', 'staged',
                                                                         'layer_copies', 'layer_frames',
                                                                         'stage_skip')}
                        if capture['snapshots'] != hud_shot_count and capture['pixels']:
                            hud_shot_count = capture['snapshots']
                            try:
                                files = save_hud_capture(game, xr['SpidyHudCapture'], capture, hud_folder,
                                                         f"{capture['snapshots']:04d}")
                            except OSError:
                                files = None  # a full disk must not end the session
                            if files:
                                hud_shots.append(dict(seconds=sample['seconds'], files=files))
                                while len(hud_shots) > HUD_SHOTS:
                                    for name in ('color', 'alpha'):
                                        (hud_folder/hud_shots[0]['files'][name]).unlink(missing_ok=True)
                                    hud_shots.popleft()
                    if hud:
                        hud_last = hud
                        sample['hud'] = {k: hud[k] for k in ('placed_frames', 'layer_frames', 'marker_projections',
                                                             'followed_frames', 'follow_angle', 'size',
                                                             'hidden_frames', 'hidden_draws')}
                    # Eye job copies the game dropped unrendered (reclaimed by age).
                    eye_jobs = frame_snapshot(game, xr['SpidyStereoFrames']) or eye_jobs
                    sample['eye_jobs_reclaimed'] = eye_jobs['reclaimed'] if eye_jobs else None
                    # Eyes with their own occlusion culling (0 with --no-eye-occlusion or before VR starts).
                    views = stereo_snapshot(game, xr['SpidyStereoData'])
                    sample['eye_occlusion'] = sum(e['occlusion'] for e in views['eyes'] if e['view']) if views else None
                    if views and views['eyes'][0]['view'] and views['eyes'][0]['render_width']:
                        eye_options['render_size'] = [views['eyes'][0]['render_width'],
                                                      views['eyes'][0]['render_height']]
                    # Frames whose render memory request did not fit lose a view's work.
                    if render_data:
                        render_memory = render_memory_snapshot(game, render_data) or render_memory
                    if render_memory:
                        sample['render_frame_mb'] = render_memory['last_frame_mb']
                        sample['render_overflow_frames'] = render_memory['overflow_frames']
                        if render_memory['frames'] > 2 and not printed_ring:
                            printed_ring = True
                            if render_memory['ring'] == 'spidy_ring':
                                print(f"Render memory: {render_memory['ring_mb']:.0f} MB for two frames "
                                      f"(the game's own: {render_memory['game_ring_mb']:.0f} MB).", flush=True)
                            else:
                                print("WARNING: the game is on its own 128 MB of render memory, which three "
                                      'views overflow in heavy scenes: eye frames drop and the game can '
                                      'crash. Close the game and start it with Launch Spidy VR.', flush=True)
                    # What the headset's loop spent on its newest frame (XR_STAGES, milliseconds), and the
                    # eye pairs handed to the graphics card that it had not finished: a loop that falls
                    # behind the headset shows here which step it waited in.
                    timing = timing_snapshot(game, xr['SpidyXrTimingData']) or timing
                    if timing:
                        sample['xr_ms'] = [round(timing[name]['last_ms'], 1) for name in XR_STAGES]
                    pair = gpu_snapshot(game, xr['SpidyGpuData'])
                    if pair and pair['completed'] != 0xffffffffffffffff:
                        sample['gpu_waiting'] = pair['fence']-pair['completed']
                    sample['free_commit_mb'] = free_commit_mb()
                    if sample['free_commit_mb'] is not None:
                        lowest_commit = min(lowest_commit or sample['free_commit_mb'], sample['free_commit_mb'])
                    samples.append(sample)
                    if pad_ignored(pad_watch, sample):
                        print(PAD_IGNORED, flush=True)
                    # Saved once, while it still hangs: the dump is what says where (game_freeze.py).
                    if freeze is None and game_frozen(freeze_watch, sample) and \
                            time.monotonic() >= freeze_watch.get('asked', 0)+1:
                        freeze_watch['asked'] = time.monotonic()
                        if game_freeze.window_hung(game.pid):
                            freeze = dict(game_freeze.capture(game.pid, a.output.with_name(a.output.stem +
                                                                                           '-freeze.dmp')),
                                          seconds=round(freeze_watch['since'], 1))
                            print(freeze_line(freeze), flush=True)
                    if sample['eye_width'] and not printed_dimensions:
                        print(rendering_line(sample['eye_width'], sample['eye_height'], a.render_scale, a.size,
                                             runtime['recommended_eye'], a.upscale, not a.no_eye_antialiasing),
                              flush=True)
                        print(reflections_line, flush=True)
                        if game_view_line:
                            print(game_view_line, flush=True)
                        printed_dimensions=True
                    aim = ray_snapshot(game, rays['SpidyRayData'])
                    if aim and aim['serial'] and (not ray_samples or aim['serial'] != ray_samples[-1]['serial']):
                        ray_samples.append(aim)
                    swing = swing_snapshot(game,rays['SpidySwingData'])
                    if swing and swing['steps'] and (not swing_samples or swing['steps'] != swing_samples[-1]['steps']):
                        swing_samples.append(swing)
                    grab = grab_snapshot(game, rays['SpidyGrabData'])
                    if grab:
                        grab['seconds'] = round(time.monotonic()-started, 3)
                        key = [grab[k] for k in ('grabs', 'yanks', 'catches', 'throws', 'releases', 'lost', 'landed',
                                                 'flings', 'refused', 'drive_failures', 'error', 'launches',
                                                 'impacts', 'struck')]
                        key += [h['phase'] for h in grab['hands']]
                        last = grab_samples[-1] if grab_samples else None
                        if not last or last['key'] != key or grab['seconds']-last['seconds'] >= 1:
                            grab['key'] = key
                            grab_samples.append(grab)
                    movement = motion_snapshot(game, motion['SpidyMotionData'])
                    if movement and movement['steps'] and (not motion_samples or movement['steps'] != motion_samples[-1]['steps']):
                        motion_samples.append(movement)
                    # What the headset showed: the newest copy of the left eye, when there is a new one.
                    shot = eye_snapshot(game, xr['SpidyXrSnapshot'])
                    if shot and shot['pixels'] and shot['count'] != shot_count:
                        shot_count = shot['count']
                        try:
                            files = save_eye_snapshot(game, xr['SpidyXrSnapshot'], shot, eye_folder,
                                                      f"{shot['count']:04d}")
                        except OSError:
                            files = None  # a full disk must not end the session
                        if files:
                            eye_shots.append(dict(seconds=sample['seconds'], files=files,
                                                  **{k: shot[k] for k in EYE_SHOT_FIELDS}))
                            while len(eye_shots) > EYE_SHOTS:
                                remove_eye_files(eye_folder, eye_shots.popleft()['files'])
                    # Which layout the runtime uses for the controllers, once it says and when it changes.
                    layout = controllers_line(sample['controllers'], runtime.get('name'))
                    if layout and layout != printed_controllers:
                        print(layout, flush=True)
                        printed_controllers = layout
                    # And the headset's eye tracking, for EYE AIM, once known and when the row changes.
                    eyes = eye_aim_line(sample['eye_aim'], runtime.get('name'))
                    if eyes and eyes != printed_eye_aim:
                        print(eyes, flush=True)
                        printed_eye_aim = eyes
                    # And what the game asks for, each time it asks for something new: the moments that
                    # used to stop a player are the ones where this names a button VR play never had.
                    asked = sample.get('prompt_last_demand', 0)
                    if asked and asked != printed_prompt:
                        printed_prompt = asked
                        line = prompts_line(asked)
                        if line:
                            print(line, flush=True)
                    state = (sample['status'], sample['message'])
                    if state != previous:
                        print(sample['message'], flush=True)
                        previous = state
                    if sample['status'] in (4, 5):
                        break
                time.sleep(.05)
        except (OSError, RuntimeError) as error:
            # A closing game fails remote calls while its process handle still reports it running.
            # The 12:05 session on October 5 ended that way and its report was never written.
            loop_error = error
        if closing.is_set():
            write_report(session_report(launcher_closed=True))
            flushed.set()
        if loop_error is not None:
            leaving = time.monotonic()+10
            while alive(game) and time.monotonic() < leaving:
                time.sleep(.1)
        if not alive(game):
            xr_active=bridge_active=False
            write_report(session_report(game_exited=True, error=str(loop_error) if loop_error else None))
            hand_over_settings(samples[-1] if samples else None)
            stalls = stream_stalls(list(samples))
            if stalls['rounds'] >= 5:
                print(f"The headset's picture stalled: the VR runtime held {stalls['rounds']} frames for a tenth "
                      f"of a second each ({stalls['seconds']} s in all). That is the stream to the headset, not "
                      'the game' + (': this PC was on Wi-Fi.' if links.get('wifi') else '.'), flush=True)
            tell_device_lost()
            print(f'Game closed. Session report: {a.output.resolve()}',flush=True)
            return 0
        if loop_error is not None:
            write_report(session_report(game_exited=False, error=str(loop_error)))
            hand_over_settings(samples[-1] if samples else None)
            tell_device_lost()
            print(f'Session report: {a.output.resolve()}',flush=True)
            raise loop_error
        xr_stop = call_remote(process, xr['SpidyXrStop'])
        xr_active = xr_stop != 0
        bridge_stop = call_remote(process, bridge['SpidyStop'])
        bridge_active = bridge_stop != 0
        final_render_memory = render_memory_snapshot(game, render_data) if render_data else None
        if (felt := haptics_snapshot(game, xr['SpidyHapticsData'])) and felt['written']:
            haptics_last = felt
        render_stop = call_remote(process, render_module['SpidyRenderMemoryStop']) if render_module else 0
        result = dict(pid=game.pid, module_base=hex(game.base), bridge_hash=bridge_hash, xr_hash=xr_hash,
            eye_size=a.size,render_scale=a.render_scale,eyes=eye_options,capture_images=a.capture_images,
            timing=timing_snapshot(game,xr['SpidyXrTimingData']),
            ray_hash=ray_hash, ray_samples=list(ray_samples), rays=ray_snapshot(game, rays['SpidyRayData']),
            motion_hash=motion_hash, swing_speed=a.swing_speed, vr_settings=settings_start, xr_runtime=runtime,
            game_settings_refused=startup.fields.get('game_settings_refused'),
            swing_samples=list(swing_samples),
            grab_samples=list(grab_samples), grab=grab_snapshot(game, rays['SpidyGrabData']),
            body=body_snapshot(game, xr['SpidyBodyData']) or body,
            punch=punch_snapshot(game, rays['SpidyPunchData']) or punch, punch_samples=list(punch_samples),
            strikes=strike_snapshot(game, rays['SpidyStrikeData']) or strikes, strike_samples=list(strike_samples),
            focus=focus_snapshot(game, rays['SpidyFocusData']) or focus,
            sound=sound_snapshot(game, rays['SpidySoundData']) or sound,
            dodge=dodge_snapshot(game, rays['SpidyDodgeData']) or dodge,
            sense=sense_snapshot(game, rays['SpidySenseData']) or sense, sense_samples=list(sense_samples),
            ropes=rope_snapshot(game, rays['SpidyRopeData']) or ropes,
            steam_guard=steam_snapshot(game, xr['SpidySteamData']) or steam,
            shooter=shooter_snapshot(game, rays['SpidyShooterData']) or shooter,
            shooter_samples=list(shooter_samples),
            slow_motion=slow_motion_snapshot(game, xr['SpidySlowMotionData']) or slow_motion,
            slow_motion_samples=list(slow_motion_samples), haptics=haptics_last,
            motion_samples=list(motion_samples), appearance=appearance_snapshot(game,xr['SpidyAppearanceData']),
            streaming=stream_snapshot(game, xr['SpidyStreamData']) or streaming,
            hud=hud_snapshot(game, xr['SpidyHudData']),
            hud_capture=hud_capture_snapshot(game, xr['SpidyHudCapture']) or hud_capture,
            hud_captures=list(hud_shots), hud_capture_folder=str(hud_folder) if hud_shots else None,
            prompts=prompt_snapshot(game, xr['SpidyPromptData']),
            eye_jobs=frame_snapshot(game, xr['SpidyStereoFrames']) or eye_jobs,
            render_memory=final_render_memory or render_memory, render_stop=render_stop,
            free_commit_mb=dict(start=start_commit, lowest=lowest_commit),
            swing=swing_snapshot(game,rays['SpidySwingData']),
            samples=list(samples), final=snapshot(game, xr['SpidyXrData']), gpu=gpu_snapshot(game, xr['SpidyGpuData']),
            eye_snapshots=list(eye_shots), eye_snapshot_folder=str(eye_folder) if eye_shots else None,
            bridge=bridge_snapshot(game, bridge['SpidyBridgeData']), xr_stop=xr_stop, bridge_stop=bridge_stop)
        result['game_entries_restored'] = all(game.read(game.base+rva,16) == code
                                              for rva,code in original_entries.items())
        result['frame_rates'] = frame_rates(list(samples))
        a.output.parent.mkdir(parents=True, exist_ok=True)
        try:
            if a.capture_images:
                result.update(save_eye_images(game, result['gpu'], a.output))
        except (OSError, ValueError, RuntimeError) as error:
            result['image_capture_error'] = str(error)
        result['passed'] = accepted(result)
        write_report(result)
        hand_over_settings(result['final'] or (samples[-1] if samples else None))
        final = result['final']
        print(json.dumps(dict(final=final, gpu=result['gpu'], appearance=result['appearance'],
                              xr_stop=xr_stop, bridge_stop=bridge_stop), indent=2))
        if result['timing']:
            print('Mean milliseconds after warmup: '+', '.join(f'{k}={v["mean_ms"]:.2f}' for k,v in result['timing'].items()),flush=True)
        if result['frame_rates']:
            rates=result['frame_rates']
            print(f"New native scene pairs: {rates['new_scene_fps']:.1f}/s; "
                  f"XR submissions including reuse: {rates['submitted_fps']:.1f}/s; "
                  f"reused pairs: {rates['reused_fps']:.1f}/s.",flush=True)
        if result['render_memory']:
            memory = result['render_memory']
            print(f"Render memory: two frames used at most {memory['worst_two_frames_mb']:.0f} of "
                  f"{memory['ring_mb']:.0f} MB; {memory['overflow_frames']} of {memory['frames']} frames "
                  'did not fit.', flush=True)
        tell_device_lost()
        print(f"Test {'passed' if result['passed'] else 'failed'}; report: {a.output.resolve()}", flush=True)
        return 0 if result['passed'] else 1
    finally:
        if previous_signal is not None: signal.signal(signal.SIGINT,previous_signal)
        if session_report and not report_written:
            # Stopping failed part-way. Keep what was sampled.
            try: write_report(session_report(incomplete=True))
            except (OSError, ValueError): pass
        flushed.set()
        if console_handler: c.windll.kernel32.SetConsoleCtrlHandler(console_handler, False)
        try:
            if xr_active and alive(game):
                call_remote(process, xr['SpidyXrStop'])
            if bridge_active and alive(game):
                call_remote(process, bridge['SpidyStop'])
            # Last: it only counts frames, and the other two drive the game.
            if render_module and process and alive(game):
                call_remote(process, render_module['SpidyRenderMemoryStop'])
        except OSError:
            pass  # the game is closing; its exit removes the hooks with it
        if process:
            close(process)
        settle_display(alive(game))
        game.close()


if __name__ == '__main__':
    try:
        with LauncherLock(): sys.exit(main())
    except KeyboardInterrupt:
        print('VR launch cancelled.',file=sys.stderr)
        settle_display(game_running())
        sys.exit(130)
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f'Spidy VR unavailable: {error}', file=sys.stderr)
        settle_display(game_running())
        sys.exit(exit_code(error))
