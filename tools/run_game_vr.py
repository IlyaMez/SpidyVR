"""Launch/attach native VR, with optional bounded tests and automatic eye resolution."""
import argparse
import ctypes as c
from ctypes import wintypes
import json
import math
import os
import pathlib
import re
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
from bridge_game import ROOT, HOOKS, prepare, snapshot as bridge_snapshot
from inspect_game import PE
from observe_game import call_remote, call_with_payload, modules
from probe_stereo_gpu import discover_queue, snapshot as gpu_snapshot, save_eye_images
from probe_stereo import frame_snapshot, render_memory_snapshot, snapshot as stereo_snapshot
from probe_native_rays import snapshot as ray_snapshot
from probe_game_swing import snapshot as swing_snapshot
from probe_native_motion import snapshot as motion_snapshot
from vr_launcher import LauncherLock, alive, bring_to_front, wait_for_game, enlarge_render_memory, RENDER_MEMORY_HOOKS
import vr_display
import xr_runtime

GAME_HOOKS = (*HOOKS, 0x2e67010, 0x1fbe360, 0x1fbda50, 0xa7b3a0, 0x1f9db60,
              0x18a0bb0, 0x189bd30, 0x186cc00, 0x1846c20, 0x19223e0,
              0x189e310, 0x189e3a0, 0x1873470, 0x17991a0, 0x1920310, 0x676dd0, 0x1920240,
              # The web shooter: the camera's update (shots go out on the main thread) and the weapons' muzzle.
              0x897d30, 0x2150c40)
# What the game process commits in a VR session at 3072 x 3264 per eye (16.7-17.1 GB on October 5),
# with Spidy's render memory ring and some room to grow.
VR_COMMIT_MB = 19000
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


def game_commit_mb():
    """What a game that is already running has been promised, or None when no single game runs."""
    try:
        return process_commit_mb(find_game())
    except RuntimeError:
        return None


def commit_warning(free_mb, game_mb=None, needed_mb=VR_COMMIT_MB):
    """Text for the console when Windows has less memory left to promise than a VR session takes.

    Windows then grows its page file while the game plays, and requests for memory stall or fail
    meanwhile; the session of 14:39 on October 5 crashed with 0.6 GB left. `game_mb` is what a
    game that is already running holds of the total.
    """
    needed = needed_mb-(game_mb or 0)
    if free_mb is None or free_mb >= needed:
        return None
    return (f'WARNING: Windows can promise programs only {free_mb/1024:.1f} GB more memory, and '
            f"{'VR takes about' if game_mb else 'the game in VR takes about'} {needed/1024:.0f} GB"
            f"{' on top of the running game' if game_mb else ''}. Close large programs (browsers, chat and "
            'launcher apps) or enlarge the Windows page file, or the game may stall and can crash.')


def announce_low_memory(warning, interactive, ask=input):
    """Print the memory warning. At a console, the person there decides whether to go on.

    Ctrl+C at the question cancels the launch before anything has been started or changed.
    """
    if not warning:
        return
    print(warning, flush=True)
    if interactive:
        try:
            ask('Press Enter to start anyway, or Ctrl+C to stop and make room first. ')
        except EOFError:
            pass


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


def preflight(manifest):
    probe = ROOT/'build/windows-ninja/spidy_headset_probe.exe'
    if not probe.is_file():
        raise RuntimeError('Build the headset probe first')
    runtime = xr_runtime.name(manifest)
    print(f'VR runtime: {runtime} ({manifest}).', flush=True)
    result = subprocess.run([str(probe)], env={**os.environ, 'XR_RUNTIME_JSON': str(manifest)},
                            capture_output=True, text=True, timeout=10)
    if result.returncode:
        raise RuntimeError((result.stderr or result.stdout).strip() +
                           f' Connect your headset in {runtime} before starting Spidy VR.')
    print(result.stdout.strip(), flush=True)


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


PRESENTATIONS = {0: 'none', 1: 'immersive', 2: 'flat', 3: 'game_screen'}
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
    """The VR settings a session starts with, as its samples' vr_settings report them."""
    return dict(aim_markers=not a.no_aim_markers, web_grab=not a.no_web_grab, air_webs=not a.no_air_webs,
                web_shooter=not a.no_web_shooter, punch=not a.no_punch, body=not a.no_body,
                swing_speed=round(a.swing_speed, 1),
                snap_turn=a.snap_turn, haptics=a.haptics, screen_size=a.screen_size)


def settings_line(start, sample):
    """The line for the launcher, or None while the settings are as the session started."""
    now = (sample or {}).get('vr_settings')
    if not now or now == start:
        return None
    return SETTINGS_LINE + ' '.join(f'{k}={int(round(v))}' for k, v in now.items())


def snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 704)
        if len(raw) != 704:
            return None
        if struct.unpack_from('<3I', raw) != (0x53585244, 11, 704):
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
        presentation, = struct.unpack_from('<I', raw, 576)
        result.update(presentation=PRESENTATIONS.get(presentation, presentation),
                      screen_submitted=struct.unpack_from('<Q', raw, 584)[0])
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
        # B presses the game got as its Y (interact, web strike), whether the aim markers show (X
        # switches them in VR), and the markers drawn so far (one per hand per headset frame).
        interacts, aim_markers = struct.unpack_from('<2I', raw, 648)
        result.update(interacts=interacts, aim_markers=bool(aim_markers),
                      markers=struct.unpack_from('<Q', raw, 656)[0])
        # The VR settings now (the SPIDY VR tab in the game's Settings changes them, X the aim markers),
        # the tab's changes, the times the game built its Settings with it, whether its hooks are in, and
        # why it is missing (game_menu.hpp: 93xx-94xx not hooked, 95xx not built).
        flags, snap_turn, haptics, screen_size = struct.unpack_from('<4I', raw, 664)
        swing_speed, changes = struct.unpack_from('<fI', raw, 680)
        result.update(vr_settings=dict(aim_markers=bool(aim_markers), web_grab=bool(flags & 1),
                                       air_webs=bool(flags & 8), web_shooter=bool(flags & 16),
                                       punch=bool(flags & 2), body=bool(flags & 4),
                                       swing_speed=round(swing_speed, 1), snap_turn=snap_turn,
                                       haptics=haptics, screen_size=screen_size),
                      setting_changes=changes)
        menu_tabs, menu_installed, menu_status = struct.unpack_from('<Q2I', raw, 688)
        result.update(menu_tabs=menu_tabs, menu_installed=bool(menu_installed), menu_status=menu_status)
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
    between its pose job and the render (radians), which the body is off by while the hero turns, and how often
    the hero's pose jobs changed rig (the body's blend restarted at each change before October 6 evening)."""
    for _ in range(8):
        raw = game.read(address, 136)
        if len(raw) != 136:
            return None
        if struct.unpack_from('<3I', raw) != (0x53424453, 1, 136):
            raise RuntimeError('Body protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        v = struct.unpack('<4Iq3Q2I2Q3fI2ff2fIQ2Id', raw)
        return dict(state=BODY_STATES.get(v[3], v[3]), jobs=v[5], hero_jobs=v[6], solved=v[7],
                    problem=BODY_PROBLEMS.get(v[8], v[8]), joints=v[9], weight=round(v[12], 3),
                    scale=round(v[13], 4), yaw=round(v[14], 4), grounded=bool(v[15]),
                    hand_error_m=[round(v[16], 4), round(v[17], 4)], head_error_m=round(v[18], 4),
                    turn_last=round(v[19], 5), turn_max=round(v[20], 5), rig=hex(v[10]), rig_switches=v[21],
                    renders=v[22], hero_jobs_last_frame=v[23], hero_jobs_max=v[24], solve_ms=round(v[25], 3))
    return None


def punch_snapshot(game, address):
    """Punches (game_punch::Data): landed per hand, the game's damage requests issued and dropped, the bots
    within reach of a fist, and the latest punch."""
    for _ in range(8):
        raw = game.read(address, 160)
        if len(raw) != 160:
            return None
        if struct.unpack_from('<3I', raw) != (0x53505544, 1, 160):
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
        return dict(status=status, samples=samples, punches=punches, issued=issued, dropped=dropped, bots=bots,
                    error=error, hands=hands, last_point=[round(x, 3) for x in point],
                    last_direction=[round(x, 3) for x in direction], last_damage=round(damage, 1),
                    last_speed=round(speed, 2))
    return None


def shooter_snapshot(game, address):
    """Web-shooter shots (game_shooter::Data): pulls that asked for a shot, shots the game fired and requests it
    dropped, those aimed at a thug and those whose target the game took, the hero's gadget, the main-thread frames
    the module saw, shots per hand, and the latest shot."""
    for _ in range(8):
        raw = game.read(address, 160)
        if len(raw) != 160:
            return None
        if struct.unpack_from('<3I', raw) != (0x53484f44, 1, 160):
            raise RuntimeError('Shooter protocol mismatch')
        if struct.unpack_from('<Q', raw, 16)[0] & 1 or raw[16:24] != game.read(address+16, 8):
            continue
        status = struct.unpack_from('<I', raw, 12)[0]
        samples, requested, fired, dropped, targeted, resolved, weapon, frames = struct.unpack_from('<8Q', raw, 24)
        bots, error = struct.unpack_from('<2I', raw, 88)
        hands = [dict(shots=shots, last_target=hex(target))
                 for shots, target in (struct.unpack_from('<2Q', raw, 96+i*16) for i in range(2))]
        origin, aim_point = struct.unpack_from('<3f', raw, 128), struct.unpack_from('<3f', raw, 140)
        return dict(status=status, samples=samples, requested=requested, fired=fired, dropped=dropped,
                    targeted=targeted, resolved=resolved, weapon=hex(weapon), frames=frames, bots=bots, error=error,
                    hands=hands, last_origin=[round(x, 3) for x in origin],
                    last_aim_point=[round(x, 3) for x in aim_point],
                    last_shot=hex(struct.unpack_from('<Q', raw, 152)[0]))
    return None


def appearance_snapshot(game, address):
    for _ in range(8):
        raw = game.read(address, 328)
        if len(raw) != 328:
            return None
        if struct.unpack_from('<3I', raw) != (0x53415044, 5, 328):
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
        shared_hero, lag_last, lag_max, lag_sum = struct.unpack_from('<Q2fd', raw, 256)
        web_start_last, web_start_max = struct.unpack_from('<2f', raw, 280)
        gap_frames, gap_last, gap_max, gap_sum = struct.unpack_from('<Q2fd', raw, 288)
        active_lag_frames, active_lag_last, active_lag_max = struct.unpack_from('<Q2f', raw, 312)
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
                    active_view_shift_m=active_shift, shared_hero_frames=shared_hero,
                    hero_lag_last_m=lag_last, hero_lag_max_m=lag_max,
                    hero_lag_mean_m=lag_sum/shared_hero if shared_hero else 0.0,
                    web_start_error_m=web_start_last if web_start_last >= 0 else None,
                    web_start_error_max_m=web_start_max if web_start_max >= 0 else None,
                    # Tracked web shooter to the game's rope in the same image; near zero when webs leave the hands.
                    web_hand_gap_frames=gap_frames, web_hand_gap_last_m=gap_last, web_hand_gap_max_m=gap_max,
                    web_hand_gap_mean_m=gap_sum/gap_frames if gap_frames else 0.0,
                    active_view_lag_frames=active_lag_frames, active_view_lag_last_m=active_lag_last,
                    active_view_lag_max_m=active_lag_max)
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
    if not (64 <= width <= 4096 and 64 <= height <= 4096 and width*4 <= pitch <= 4096*4+4096) or shot['pixels'] < 0x10000:
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


def wait_for_renderer(game, process, render_data, timeout=180):
    """The game's graphics queue, as soon as the game draws frames (its intro, seconds after it starts).

    `render_data` is the render memory module's telemetry, when that module is loaded: its frame count
    shows the renderer running before the queue is looked for.
    """
    deadline = time.monotonic()+timeout
    problem = 'the game drew no frames'
    while alive(game) and time.monotonic() < deadline:
        memory = render_memory_snapshot(game, render_data) if render_data else None
        if render_data and (not memory or memory['frames'] < 30):
            time.sleep(.25)
            continue
        try:
            return discover_queue(game, process)
        except RuntimeError as error:
            # Startup can show no queue, or a second one, for a moment.
            problem = str(error)
            time.sleep(.5)
    if not alive(game):
        raise RuntimeError('The game closed while starting.')
    raise RuntimeError(f'No game graphics queue within {timeout:.0f} seconds: {problem}.')


def game_running():
    try:
        find_game()
        return True
    except RuntimeError as error:
        return not str(error).startswith('Spider-Man is not running.')


def settle_display(running):
    """Restore display values saved for a VR launch once the game has closed."""
    if not vr_display.pending() or getattr(settle_display, 'deferred', False):
        return
    if running:
        settle_display.deferred = True
        print('Your display settings are restored after the game closes, the next time Spidy VR starts it '
              r'(or run tools\vr_display.py --restore).', flush=True)
    elif vr_display.restore():
        print('Restored your desktop display settings.', flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--seconds', type=float, default=0,help='0 runs until game exit or Ctrl+C; 2..25 runs a timed test')
    p.add_argument('--size', type=int, default=0,help='0 uses the runtime recommendation; otherwise a square override')
    p.add_argument('--auto-launch',action='store_true')
    p.add_argument('--stop-after',type=float,default=0,help='End an untimed session after this many seconds for validation')
    p.add_argument('--capture-images', action='store_true',help='Enable diagnostic CPU eye readback (adds overhead)')
    p.add_argument('--overlay-webs', action='store_true',help="Draw Spidy's overlay webs instead of the game's web lines")
    p.add_argument('--no-web-grab', action='store_true', help='Webs never catch props or thugs; they only swing')
    p.add_argument('--no-air-webs', action='store_true',
                   help='A web that meets nothing within reach misses, instead of holding in open air 100 m out')
    p.add_argument('--no-body', action='store_true',
                   help="Keep the hero hidden in VR (gloves drawn over the image) instead of your own body")
    p.add_argument('--no-punch', action='store_true', help='Fists pass through thugs instead of punching them')
    p.add_argument('--no-web-shooter', action='store_true',
                   help="A free hand's trigger shoots nothing, instead of the game's web-shooter web balls")
    p.add_argument('--no-aim-markers', action='store_true',
                   help="Start with the aim markers (where each hand's web would land) hidden; X shows them in VR")
    p.add_argument('--no-eye-occlusion', action='store_true',
                   help='Each eye draws everything in its view, hidden behind buildings or not (builds before '
                        'October 6 did; about half the frame rate)')
    p.add_argument('--stock-monitor-view', action='store_true',
                   help="Keep the stock camera as the game's active view; culling and shadows then follow it, not the head")
    p.add_argument('--swing-speed', type=float, default=32, help='Native swing speed cap in m/s (default 32)')
    p.add_argument('--snap-turn', type=int, default=30,
                   help='Degrees a flick of the right stick turns you (0: no snap turning; default 30)')
    p.add_argument('--haptics', type=int, default=100, help='Controller vibration in percent (default 100)')
    p.add_argument('--screen-size', type=int, choices=(0, 1, 2), default=1,
                   help='The game screen for menus, cutscenes and flat mode: 0 small, 1 medium, 2 large')
    p.add_argument('--full-desktop-view', action='store_true',
                   help="Start the game with its own display settings instead of a small VR window")
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/game-vr.json')
    p.add_argument('--xr-runtime', type=pathlib.Path,
                   help="OpenXR runtime manifest; by default Virtual Desktop's if installed, else Windows' active one")
    p.add_argument('--stop-event', help='Named event that stops VR as Ctrl+C does (for a launcher window)')
    a = p.parse_args()
    if a.stop_after and (a.seconds or not 5<=a.stop_after<=300):
        p.error('--stop-after requires --seconds 0 and a value from 5 to 300')
    if (a.seconds!=0 and not 2 <= a.seconds <= 25) or (a.size!=0 and not 64 <= a.size <= 4096) or not 1 <= a.swing_speed <= 65:
        p.error('Use 0 or 2..25 seconds, 0 or 64..4096 pixels, and 1..65 m/s')
    if not 0 <= a.snap_turn <= 90 or not 0 <= a.haptics <= 100:
        p.error('Use 0..90 degrees of snap turn and 0..100% vibration')
    if a.stop_event:
        watch_stop_event(a.stop_event)
    manifest = xr_runtime.choose(a.xr_runtime)
    preflight(manifest)
    announce_low_memory(commit_warning(free_commit_mb(), game_commit_mb()),
                        bool(sys.stdin and sys.stdin.isatty()))

    def prepare_display():
        values = vr_display.prepare_launch(small=not a.full_desktop_view)
        if values:
            print(f"Desktop view: {values['WindowWidth']} x {values['WindowHeight']} window to save GPU time; "
                  'your display settings return when the game closes.', flush=True)

    render_module = {}

    def render_memory_module(pid):
        # In time only in a game that has just started: it creates its render memory once.
        try:
            render_module.update(enlarge_render_memory(pid))
        except (OSError, RuntimeError) as error:
            print(f'WARNING: render memory module unavailable ({error}).', flush=True)

    # VR starts with the game: its intro, menus and loading show on a screen in the headset until
    # there is gameplay. The worker finds each player itself (a loaded save, a respawn, a character
    # switch); before October 5's seventh build the launcher found one, once, and VR stayed on the
    # screen after any reload.
    if a.auto_launch:
        game = wait_for_game(prepare=prepare_display, early=render_memory_module, ready=lambda game: True)
    else:
        game = Game(find_game())
        render_memory_module(game.pid)
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
        queue = wait_for_renderer(game, process, render_data)
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
                                       'SpidyPunchData', 'SpidyShooterData'))
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
                                           'SpidyBodyData'))
        config = struct.pack('<4I7Q2IfI', 0x53585243, 11, 624, game.pid, game.base, queue,
                             bridge_module, ray_module, motion_module, 0, 0,
                             int(a.seconds*1000), a.size, a.swing_speed,
                             int(a.capture_images) | (2 if a.overlay_webs else 0) |
                             (4 if a.stock_monitor_view else 0) | (8 if a.no_web_grab else 0) |
                             (16 if a.no_eye_occlusion else 0) | (32 if a.no_body else 0) |
                             (64 if a.no_punch else 0) | (128 if a.no_aim_markers else 0) |
                             (256 if a.no_air_webs else 0) | (512 if a.no_web_shooter else 0)) + \
            runtime_path(manifest) + struct.pack('<4I', a.snap_turn, a.haptics, a.screen_size, 0)
        code = call_with_payload(process, xr['SpidyXrStart'], config)
        if code:
            raise RuntimeError(f'Game XR start: {code}')
        xr_active = True
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
        appearance = None
        # The player's body and fists: the latest of each, and a sample when punches land.
        body = punch = None
        punch_samples = deque(maxlen=2000)
        # The web shooter: the latest, and a sample when a shot is asked for, fired or dropped.
        shooter = None
        shooter_samples = deque(maxlen=2000)
        eye_jobs = None
        render_memory = None
        lowest_commit = start_commit = free_commit_mb()
        printed_ring = False
        previous = None
        printed_dimensions=False
        renewed=0
        stop_requested=False
        settings_start = start_settings(a)

        def hand_over_settings(sample):
            """Tell the launcher what the VR settings were left at, for the next session."""
            line = settings_line(settings_start, sample)
            if line:
                print(line, flush=True)

        def session_report(**extra):
            """Everything sampled so far. It is written however the session ends."""
            return dict(pid=game.pid, eye_size=a.size, swing_speed=a.swing_speed, vr_settings=settings_start,
                        motion_hash=motion_hash,
                        xr_hash=xr_hash, ray_hash=ray_hash, samples=list(samples),
                        swing_samples=list(swing_samples), motion_samples=list(motion_samples),
                        grab_samples=list(grab_samples), body=body, punch=punch,
                        punch_samples=list(punch_samples), shooter=shooter, shooter_samples=list(shooter_samples),
                        ray_samples=list(ray_samples), appearance=appearance, eye_jobs=eye_jobs,
                        render_memory=render_memory,
                        free_commit_mb=dict(start=start_commit, lowest=lowest_commit),
                        eye_snapshots=list(eye_shots), eye_snapshot_folder=str(eye_folder) if eye_shots else None,
                        frame_rates=frame_rates(list(samples)), final=samples[-1] if samples else None, **extra)

        def write_report(result):
            nonlocal report_written
            a.output.parent.mkdir(parents=True, exist_ok=True)
            # Texture and video memory pressure and the game's own frame rate, minute by minute.
            copy, memory = keep_game_log(a.output)
            result.update(game_log=str(copy) if copy else None, game_memory=memory)
            a.output.write_text(json.dumps(result, indent=2)+'\n')
            report_written = True

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
                    body = body_snapshot(game, xr['SpidyBodyData']) or body
                    if body:
                        sample['body'] = {k: body[k] for k in ('state', 'problem', 'weight', 'hand_error_m',
                                                               'head_error_m', 'turn_last', 'grounded', 'scale',
                                                               'rig', 'rig_switches')}
                    landed = punch_snapshot(game, rays['SpidyPunchData'])
                    if landed:
                        # Thugs within reach of a fist, and each fist's speed relative to the head.
                        sample['punch'] = dict(bots=landed['bots'], punches=landed['punches'],
                                               speed=[h['speed'] for h in landed['hands']])
                        if landed['punches'] != (punch or {}).get('punches') or \
                                landed['dropped'] != (punch or {}).get('dropped'):
                            punch_samples.append(dict(landed, seconds=round(time.monotonic()-started, 3)))
                        punch = landed
                    shots = shooter_snapshot(game, rays['SpidyShooterData'])
                    if shots:
                        sample['shooter'] = {k: shots[k] for k in ('status', 'fired', 'dropped', 'bots')}
                        if any(shots[k] != (shooter or {}).get(k) for k in ('requested', 'fired', 'dropped', 'error')):
                            shooter_samples.append(dict(shots, seconds=round(time.monotonic()-started, 3)))
                        shooter = shots
                    # Eye job copies the game dropped unrendered (reclaimed by age).
                    eye_jobs = frame_snapshot(game, xr['SpidyStereoFrames']) or eye_jobs
                    sample['eye_jobs_reclaimed'] = eye_jobs['reclaimed'] if eye_jobs else None
                    # Eyes with their own occlusion culling (0 with --no-eye-occlusion or before VR starts).
                    views = stereo_snapshot(game, xr['SpidyStereoData'])
                    sample['eye_occlusion'] = sum(e['occlusion'] for e in views['eyes'] if e['view']) if views else None
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
                    sample['free_commit_mb'] = free_commit_mb()
                    if sample['free_commit_mb'] is not None:
                        lowest_commit = min(lowest_commit or sample['free_commit_mb'], sample['free_commit_mb'])
                    samples.append(sample)
                    if sample['eye_width'] and not printed_dimensions:
                        print(f"Rendering {sample['eye_width']} x {sample['eye_height']} pixels per eye.",flush=True)
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
                                                 'flings', 'refused', 'drive_failures', 'error')]
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
            print(f'Game closed. Session report: {a.output.resolve()}',flush=True)
            return 0
        if loop_error is not None:
            write_report(session_report(game_exited=False, error=str(loop_error)))
            hand_over_settings(samples[-1] if samples else None)
            print(f'Session report: {a.output.resolve()}',flush=True)
            raise loop_error
        xr_stop = call_remote(process, xr['SpidyXrStop'])
        xr_active = xr_stop != 0
        bridge_stop = call_remote(process, bridge['SpidyStop'])
        bridge_active = bridge_stop != 0
        final_render_memory = render_memory_snapshot(game, render_data) if render_data else None
        render_stop = call_remote(process, render_module['SpidyRenderMemoryStop']) if render_module else 0
        result = dict(pid=game.pid, module_base=hex(game.base), bridge_hash=bridge_hash, xr_hash=xr_hash,
            eye_size=a.size,capture_images=a.capture_images,timing=timing_snapshot(game,xr['SpidyXrTimingData']),
            ray_hash=ray_hash, ray_samples=list(ray_samples), rays=ray_snapshot(game, rays['SpidyRayData']),
            motion_hash=motion_hash, swing_speed=a.swing_speed, vr_settings=settings_start,
            swing_samples=list(swing_samples),
            grab_samples=list(grab_samples), grab=grab_snapshot(game, rays['SpidyGrabData']),
            body=body_snapshot(game, xr['SpidyBodyData']) or body,
            punch=punch_snapshot(game, rays['SpidyPunchData']) or punch, punch_samples=list(punch_samples),
            shooter=shooter_snapshot(game, rays['SpidyShooterData']) or shooter,
            shooter_samples=list(shooter_samples),
            motion_samples=list(motion_samples), appearance=appearance_snapshot(game,xr['SpidyAppearanceData']),
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
        sys.exit(2)
