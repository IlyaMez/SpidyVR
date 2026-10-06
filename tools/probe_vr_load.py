"""Measure what limits the game's frame rate with VR's views, in the running game without a headset.

At the player's current spot, in phases:

    stock      the game alone
    vr         Spidy's two eye views at the headset's resolution and the game's own view moved to the
               head (views 13), as a VR session renders them, looking where the game camera looks
    turn       the same while the head turns a full circle
    narrow     the same eyes with a 60-degree lens: fewer objects in view, the same pixels
    after      the game alone again, once the eyes have left the render list

Each phase reports the game's frame rate (frames counted by spidy_render_memory.dll), its per-frame
render memory, GPU utilization from nvidia-smi, the game's busiest threads (CPU time of each, in
percent of one core), the whole PC's CPU use, and the game's disk reads. A thread near 100% of a core
while the GPU is well below 100% means the game waits on that thread; a GPU near 100% means pixels or
draw work on the GPU set the frame rate.

Start the game with tools/vr_launcher.py (or probe_menu_pad.py start), load a save, and run this with
the game window in front. The eye size cannot change within one game process.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import json
import math
import pathlib
import struct
import subprocess
import sys
import threading
import time
from bridge_game import ROOT, prepare
from capture_game_state import Game, find_game, open_process, close
from observe_game import call_remote, call_with_payload, modules, write
from probe_game_screen import game_window, user32
from probe_stereo import snapshot as stereo_snapshot, frame_snapshot, render_memory_snapshot
from probe_stereo_gpu import discover_queue, snapshot as gpu_snapshot
from run_game_vr import rgb_rows, write_rgb_png
from vr_launcher import enlarge_render_memory

HEAD = 1.6        # eye height above the feet, metres
HALF_IPD = .032
# Quest 3 through Virtual Desktop: the game view of the 10:16 session on 2026-10-06 (110.6 x 98.7
# degrees, 5% margin) gives 54 degrees to each eye's outer side and 48 up and down.
OUTER, INNER, VERTICAL = math.radians(54), math.radians(44), math.radians(48)
NARROW = math.radians(30)

k32 = c.WinDLL('kernel32', use_last_error=True)
ntdll = c.WinDLL('ntdll')


class ThreadEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('tid', w.DWORD), ('pid', w.DWORD),
                ('base_priority', w.LONG), ('delta_priority', w.LONG), ('flags', w.DWORD)]


class IoCounters(c.Structure):
    _fields_ = [(name, c.c_uint64) for name in ('reads', 'writes', 'others', 'read_bytes', 'write_bytes',
                                                'other_bytes')]


for _function, _result, _arguments in (
        (k32.CreateToolhelp32Snapshot, w.HANDLE, (w.DWORD, w.DWORD)),
        (k32.Thread32First, w.BOOL, (w.HANDLE, c.POINTER(ThreadEntry))),
        (k32.Thread32Next, w.BOOL, (w.HANDLE, c.POINTER(ThreadEntry))),
        (k32.OpenThread, w.HANDLE, (w.DWORD, w.BOOL, w.DWORD)),
        (k32.GetThreadTimes, w.BOOL, (w.HANDLE, *[c.POINTER(c.c_uint64)]*4)),
        (k32.GetThreadDescription, c.c_long, (w.HANDLE, c.POINTER(c.c_void_p))),
        (k32.LocalFree, c.c_void_p, (c.c_void_p,)),
        (k32.CloseHandle, w.BOOL, (w.HANDLE,)),
        (k32.GetSystemTimes, w.BOOL, (c.POINTER(c.c_uint64),)*3),
        (k32.GetProcessIoCounters, w.BOOL, (w.HANDLE, c.POINTER(IoCounters))),
        (k32.SuspendThread, w.DWORD, (w.HANDLE,)), (k32.ResumeThread, w.DWORD, (w.HANDLE,)),
        (k32.GetThreadContext, w.BOOL, (w.HANDLE, c.c_void_p)),
        (ntdll.NtQueryInformationThread, c.c_long, (w.HANDLE, c.c_int, c.c_void_p, w.ULONG, c.c_void_p))):
    _function.restype, _function.argtypes = _result, _arguments


class Threads:
    """CPU time of each of the game's threads, with its name or start address."""

    def __init__(self, pid, base):
        self.pid, self.base = pid, base
        self.handles, self.names = {}, {}

    def label(self, handle, tid):
        text = c.c_void_p()
        if k32.GetThreadDescription(handle, c.byref(text)) >= 0 and text.value:
            name = c.wstring_at(text.value)
            k32.LocalFree(text)
            if name:
                return name
        start = c.c_uint64()
        if ntdll.NtQueryInformationThread(handle, 9, c.byref(start), 8, None) == 0 and start.value:
            offset = start.value-self.base
            return f'Spider-Man.exe+{offset:x}' if 0 <= offset < 0x10000000 else f'{start.value:#x}'
        return f'thread {tid}'

    def sample(self):
        snap = k32.CreateToolhelp32Snapshot(4, 0)
        entry = ThreadEntry()
        entry.size = c.sizeof(entry)
        ids = []
        ok = k32.Thread32First(snap, c.byref(entry))
        while ok:
            if entry.pid == self.pid:
                ids.append(entry.tid)
            ok = k32.Thread32Next(snap, c.byref(entry))
        k32.CloseHandle(snap)
        times = {}
        for tid in ids:
            handle = self.handles.get(tid)
            if handle is None:
                handle = k32.OpenThread(0x0040 | 0x0800, False, tid)
                if not handle:
                    continue
                self.handles[tid] = handle
                self.names[tid] = self.label(handle, tid)
            created, ended, kernel, user = (c.c_uint64() for _ in range(4))
            if k32.GetThreadTimes(handle, c.byref(created), c.byref(ended), c.byref(kernel), c.byref(user)):
                times[tid] = kernel.value+user.value
        return times

    def close(self):
        for handle in self.handles.values():
            k32.CloseHandle(handle)
        self.handles.clear()


class Gpu:
    """nvidia-smi samples: utilization (share of time a kernel ran), graphics clock, board power."""

    def __init__(self):
        self.samples = []
        try:
            self.process = subprocess.Popen(
                ['nvidia-smi', '--query-gpu=utilization.gpu,clocks.gr,power.draw', '--format=csv,noheader,nounits',
                 '-lms', '100'], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        except OSError:
            self.process = None
            return
        threading.Thread(target=self.read, daemon=True).start()

    def read(self):
        for line in self.process.stdout:
            try:
                utilization, clock, power = (float(x) for x in line.split(','))
            except ValueError:
                continue
            self.samples.append((time.perf_counter(), utilization, clock, power))

    def window(self, start, end):
        return [s for s in self.samples if start <= s[0] <= end]

    def stop(self):
        if self.process:
            self.process.terminate()


class Profiler:
    """Where chosen game threads spend their time: instruction pointers sampled about 1000 times a second.

    Each sample suspends the thread, reads its instruction and stack pointers, and scans the top of its
    stack for return addresses into the game, so time inside a driver or system call is charged to the
    game code that called it as well.
    """
    CONTEXT_BYTES, CONTROL = 1232, 0x100001  # x64 CONTEXT; CONTEXT_CONTROL

    def __init__(self, game, tids, module_list):
        self.game, self.samples = game, {tid: [] for tid in tids}
        self.handles = {tid: k32.OpenThread(0x0002 | 0x0008 | 0x0040, False, tid) for tid in tids}
        self.modules = sorted((m['base'], m['base']+m['size'], m['name']) for m in module_list)
        self.raw = c.create_string_buffer(self.CONTEXT_BYTES+16)
        self.context = (c.addressof(self.raw)+15) & ~15
        self.running = False

    def module(self, address):
        for begin, end, name in self.modules:
            if begin <= address < end:
                return name, address-begin
        return '?', address

    def sample(self, handle):
        c.memset(self.context, 0, self.CONTEXT_BYTES)
        c.c_uint32.from_address(self.context+0x30).value = self.CONTROL
        if k32.SuspendThread(handle) == 0xffffffff:
            return None
        try:
            if not k32.GetThreadContext(handle, self.context):
                return None
            rip = c.c_uint64.from_address(self.context+0xf8).value
            rsp = c.c_uint64.from_address(self.context+0x98).value
            stack = self.game.read(rsp, 2048)
        finally:
            k32.ResumeThread(handle)
        exe_begin, exe_end = self.game.base, self.game.base+0x10000000
        callers = []
        for (value,) in struct.iter_unpack('<Q', stack[:len(stack)//8*8]):
            if exe_begin <= value < exe_end:
                callers.append(value-exe_begin)
                if len(callers) == 4:
                    break
        return rip, callers

    def run(self, seconds):
        self.running = True
        until = time.perf_counter()+seconds
        while self.running and time.perf_counter() < until:
            for tid, handle in self.handles.items():
                if handle:
                    value = self.sample(handle)
                    if value:
                        self.samples[tid].append(value)
            time.sleep(.001)

    def report(self, top=15):
        result = {}
        for tid, samples in self.samples.items():
            if not samples:
                continue
            by_module, by_code, by_caller = {}, {}, {}
            for rip, callers in samples:
                name, offset = self.module(rip)
                by_module[name] = by_module.get(name, 0)+1
                key = f'{name}+{offset & ~0x3f:x}' if name != '?' else '?'
                by_code[key] = by_code.get(key, 0)+1
                if name.lower() != 'spider-man.exe' and callers:
                    caller = f'{name} from Spider-Man.exe+{callers[0]:x}'
                    by_caller[caller] = by_caller.get(caller, 0)+1

            def share(counts):
                return [(k, round(100*v/len(samples), 1)) for k, v in
                        sorted(counts.items(), key=lambda kv: -kv[1])[:top]]
            result[str(tid)] = dict(samples=len(samples), modules=share(by_module), code=share(by_code),
                                    outside_game_from=share(by_caller))
        return result

    def close(self):
        for handle in self.handles.values():
            if handle:
                k32.CloseHandle(handle)


def system_times():
    idle, kernel, user = c.c_uint64(), c.c_uint64(), c.c_uint64()
    k32.GetSystemTimes(c.byref(idle), c.byref(kernel), c.byref(user))
    return idle.value, kernel.value+user.value


def eye_rig(feet, heading, outer=OUTER, inner=INNER, vertical=VERTICAL):
    """Both eyes at standing height looking along `heading`: rows right, down, forward, position."""
    forward = (heading[0], 0., heading[2])
    right = (-forward[2], 0., forward[0])
    down = (0., -1., 0.)
    centre = (feet[0], feet[1]+HEAD, feet[2])
    eyes = []
    for side, lens in ((-HALF_IPD, (-outer, inner)), (HALF_IPD, (-inner, outer))):
        position = tuple(p+r*side for p, r in zip(centre, right))
        eyes.append(((*right, 0, *down, 0, *forward, 0, *position, 1), (*lens, -vertical, vertical)))
    return eyes


def eye_command(serial, eyes, feet):
    """native_eyes::Command v2, anchored to the player's position."""
    raw = struct.pack('<4IQ2I', 0x53455043, 2, 208, 1, serial, 250, 0)
    for matrix, fov in eyes:
        raw += struct.pack('<20f', *matrix, *fov)
    return raw+struct.pack('<3fI', *feet, 1)


def summarize(name, samples, gpu, threads, cores, settle):
    """Rates over a phase's samples after `settle` seconds."""
    start = samples[0]['t']+settle
    kept = [s for s in samples if s['t'] >= start]
    if len(kept) < 2:
        return dict(phase=name, error='too short')
    first, last = kept[0], kept[-1]
    seconds = last['t']-first['t']
    frames = last['frames']-first['frames']
    per_thread = []
    for tid, end in last['threads'].items():
        begin = first['threads'].get(tid)
        if begin is not None:
            per_thread.append((100*(end-begin)/1e7/seconds, tid))
    per_thread.sort(reverse=True)
    idle = last['system'][0]-first['system'][0]
    total = last['system'][1]-first['system'][1]
    used = [s for s in gpu.window(first['t'], last['t'])]
    memory = [s['frame_mb'] for s in kept if s['frame_mb'] is not None]
    return dict(
        phase=name, seconds=round(seconds, 2), fps=round(frames/seconds, 1),
        frame_ms=round(1000*seconds/frames, 2) if frames else None,
        eye_pairs_per_s=round((last['pairs']-first['pairs'])/seconds, 1) if last['pairs'] is not None else None,
        render_mb_per_frame=round(sum(memory)/len(memory), 1) if memory else None,
        gpu_utilization=round(sum(s[1] for s in used)/len(used), 1) if used else None,
        gpu_utilization_min=min((s[1] for s in used), default=None),
        gpu_clock_mhz=round(sum(s[2] for s in used)/len(used)) if used else None,
        gpu_power_w=round(sum(s[3] for s in used)/len(used)) if used else None,
        game_cpu_percent_of_pc=round(sum(p for p, _ in per_thread)/cores, 1),
        pc_cpu_percent=round(100*(total-idle)/total, 1) if total else None,
        disk_read_mb_per_s=round((last['read_bytes']-first['read_bytes'])/seconds/2**20, 1),
        busiest_threads=[dict(thread=tid, name=threads.names.get(tid), percent_of_core=round(p, 1))
                         for p, tid in per_thread[:8]])


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--width', type=int, default=3072, help='eye width in pixels (headset: 3072)')
    p.add_argument('--height', type=int, default=3264, help='eye height in pixels (headset: 3264)')
    p.add_argument('--views', type=int, default=13, choices=(5, 13, 21, 29),
                   help="13 moves the game's own view to the head, as a VR session does; +16 gives each eye "
                        "occlusion culling")
    p.add_argument('--seconds', type=float, default=12, help='measured seconds per phase')
    p.add_argument('--phases', default='stock,vr,turn,narrow,after',
                   help="stock, vr, turn, narrow, after; vr_off, turn_off and narrow_off take the eyes' "
                        'occlusion away for the phase (views 21/29); shots saves eye images at four headings '
                        'with and without it (--gpu capture)')
    p.add_argument('--profile-threads', default='',
                   help='comma-separated thread ids to sample during the vr phase (from an earlier report)')
    p.add_argument('--gpu', choices=('staged', 'none', 'capture'), default='staged',
                   help="Spidy's GPU bridge, which starts once per game process: staged copies each eye pair as "
                        'VR does; capture reads every pair back for the shots phase, which costs GPU time')
    p.add_argument('--output', type=pathlib.Path, default=ROOT/'reports/vr-load.json')
    a = p.parse_args()
    phases = a.phases.split(',')
    known = {'stock', 'vr', 'turn', 'narrow', 'after', 'vr_off', 'turn_off', 'narrow_off', 'shots'}
    if not all(64 <= x <= 4096 for x in (a.width, a.height)) or not set(phases) <= known:
        p.error('Eye sizes are 64..4096; phases are ' + ', '.join(sorted(known)))
    if ('shots' in phases) != (a.gpu == 'capture'):
        p.error('The shots phase and --gpu capture go together')
    game = Game(find_game())
    process = None
    stops = []
    threads = Threads(game.pid, game.base)
    gpu = Gpu()
    cores = __import__('os').cpu_count()
    result = dict(width=a.width, height=a.height, views=a.views, cores=cores, phases=[])
    try:
        window = game_window(game.pid)
        if user32.GetForegroundWindow() != window:
            raise RuntimeError('The game window is not in front, so the game is paused. Bring it to the front.')
        objects = game.registered_candidates()
        heroes = [o for o in objects if o['kind'] == 'hero_local']
        if len(heroes) != 1:
            raise RuntimeError('Exactly one local hero is required. Load a save first.')
        feet = tuple(heroes[0]['player']['position'])
        cameras = [o for o in objects if o['kind'] == 'hero_camera_manager' and 'camera' in o]
        heading = (0., 0., 1.)
        if cameras:
            camera = cameras[0]['camera']['position']
            dx, dz = feet[0]-camera[0], feet[2]-camera[2]
            if math.hypot(dx, dz) > .1:
                heading = (dx/math.hypot(dx, dz), 0., dz/math.hypot(dx, dz))
        result.update(feet=feet, heading=heading)
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, game.pid)
        if not process:
            raise c.WinError(c.get_last_error())
        render = enlarge_render_memory(game.pid)
        queue = None if a.gpu == 'none' else discover_queue(game, process)
        eyes_dll, digest = prepare(game.pid, process, ROOT/'build/windows-ninja/spidy_stereo_probe.dll',
                                   ROOT/'reports/stereo-modules',
                                   ('SpidyStart', 'SpidyStop', 'SpidyStereoData', 'SpidySetEyes', 'SpidyStereoFrames',
                                    'SpidyGpuStart', 'SpidyGpuStop', 'SpidyGpuData', 'SpidyGpuFreeze'))
        result['dll_sha256'] = digest
        io = IoCounters()
        # Eye commands must outnumber any earlier run's in this game process.
        serial = time.time_ns()//1000
        eyes_on = False

        def sample():
            memory = render_memory_snapshot(game, render['SpidyRenderMemoryData'])
            frames = frame_snapshot(game, eyes_dll['SpidyStereoFrames']) if eyes_on else None
            k32.GetProcessIoCounters(game.handle, c.byref(io))
            return dict(t=time.perf_counter(), frames=memory['frames'] if memory else 0,
                        frame_mb=memory['last_frame_mb'] if memory else None,
                        pairs=frames['left_copies'] if frames else None,
                        threads=threads.sample(), system=system_times(), read_bytes=io.read_bytes)

        profile_ids = [int(x) for x in a.profile_threads.split(',') if x.strip()]

        def run(name, seconds, look=None):
            nonlocal serial
            samples = []
            profiler = None
            if profile_ids and name == 'vr':
                profiler = Profiler(game, profile_ids, modules(game.pid))
                threading.Thread(target=profiler.run, args=(seconds-2,), daemon=True).start()
            began = time.perf_counter()
            next_sample = began
            while time.perf_counter()-began < seconds:
                now = time.perf_counter()
                if look:
                    serial += 1
                    code = call_with_payload(process, eyes_dll['SpidySetEyes'],
                                             eye_command(serial, look(now-began), feet))
                    if code and code != 4003:
                        raise RuntimeError(f'Eye command: {code}')
                if now >= next_sample:
                    samples.append(sample())
                    next_sample += .5
                if user32.GetForegroundWindow() != window:
                    if profiler:
                        profiler.running = False
                    raise RuntimeError('Another window came to the front (the game pauses). Stopped.')
                time.sleep(.02)
            if profiler:
                profiler.running = False
                time.sleep(.05)
                result['profile'] = profiler.report()
                profiler.close()
                for tid, found in result['profile'].items():
                    print(f'  thread {tid}: {found["samples"]} samples; modules {found["modules"][:6]}', flush=True)
                    print(f'    code {found["code"][:8]}', flush=True)
                    print(f'    outside the game, called from {found["outside_game_from"][:6]}', flush=True)
            settle = 2 if name != 'stock' else 1
            summary = summarize(name, samples, gpu, threads, cores, settle)
            result['phases'].append(summary)
            print(json.dumps({k: v for k, v in summary.items() if k != 'busiest_threads'}), flush=True)
            for t in summary.get('busiest_threads', [])[:5]:
                print(f"    {t['percent_of_core']:5.1f}% of a core  {t['name']}", flush=True)

        def straight(_):
            return eye_rig(feet, heading)

        def turning(elapsed):
            angle = math.atan2(heading[2], heading[0])+2*math.pi*elapsed/(a.seconds+2)
            return eye_rig(feet, (math.cos(angle), 0., math.sin(angle)))

        def narrow(_):
            return eye_rig(feet, heading, NARROW, NARROW, NARROW)

        eye_views = []
        occluders = {}

        def occlusion(on):
            """Probe-only A/B: each eye's occlusion object (view +1ba0) put back, or taken away.

            Every reader loads the pointer once and tests it for null, so a view without one simply culls
            nothing by occlusion. +1208 is no switch for this: cleared, the eye renders nothing at all.
            """
            for view in eye_views:
                saved = occluders.setdefault(view, game.pointer(view+0x1ba0))
                if not saved:
                    continue
                count = c.c_size_t()
                value = struct.pack('<Q', saved if on else 0)
                if not write(process, view+0x1ba0, value, 8, c.byref(count)) or count.value != 8:
                    raise c.WinError(c.get_last_error())

        def hold(seconds, look):
            nonlocal serial
            began = time.perf_counter()
            while time.perf_counter()-began < seconds:
                serial += 1
                code = call_with_payload(process, eyes_dll['SpidySetEyes'], eye_command(serial, look(0), feet))
                if code and code != 4003:
                    raise RuntimeError(f'Eye command: {code}')
                if user32.GetForegroundWindow() != window:
                    raise RuntimeError('Another window came to the front (the game pauses). Stopped.')
                time.sleep(.02)

        def difference(x, y):
            """Mean absolute difference, and the share of pixels whose largest channel moved by over 48."""
            total = changed = 0
            for i in range(0, len(x), 3):
                d = max(abs(x[i]-y[i]), abs(x[i+1]-y[i+1]), abs(x[i+2]-y[i+2]))
                total += d
                changed += d > 48
            return dict(mean=round(total/(len(x)//3), 2), changed_percent=round(100*changed/(len(x)//3), 3))

        def shots():
            """Left eye images at four headings: occlusion on, off, and on again, with each one's render memory."""
            folder = a.output.with_name(a.output.stem+'-eyes')
            folder.mkdir(parents=True, exist_ok=True)
            base_angle = math.atan2(heading[2], heading[0])
            found = []
            for quarter in range(4):
                angle = base_angle+quarter*math.pi/2
                look = (math.cos(angle), 0., math.sin(angle))
                images, memory = {}, {}
                for label, on in (('on', True), ('off', False), ('again', True)):
                    occlusion(on)
                    hold(2.5, lambda _: eye_rig(feet, look))
                    sizes = []
                    for _ in range(10):
                        state = render_memory_snapshot(game, render['SpidyRenderMemoryData'])
                        sizes.append(state['last_frame_mb'])
                        hold(.05, lambda _: eye_rig(feet, look))
                    code = call_remote(process, eyes_dll['SpidyGpuFreeze'])
                    state = gpu_snapshot(game, eyes_dll['SpidyGpuData'])
                    if code or not state or state['width'] != a.width or state['height'] != a.height:
                        raise RuntimeError(f'Eye capture: {code} {state}')
                    pixels = game.read(state['left_pixels'], a.width*a.height*4)
                    columns, rows, rgb = rgb_rows(pixels, a.width*4, 0, 0, a.width, a.height, 2)
                    write_rgb_png(folder/f'heading{quarter}-{label}.png', columns, rows, rgb)
                    images[label] = rgb
                    memory[label] = round(sum(sizes)/len(sizes), 1)
                occlusion(True)
                entry = dict(heading=quarter*90, render_mb=memory,
                             on_vs_off=difference(images['on'], images['off']),
                             on_vs_again=difference(images['on'], images['again']))
                found.append(entry)
                print('shots', json.dumps(entry), flush=True)
            result['shots'] = dict(folder=str(folder), headings=found)

        for name in phases:
            if name not in ('stock', 'after') and not eyes_on:
                if a.gpu != 'none':
                    copy, read_back = (0, 1) if a.gpu == 'capture' else (2, 0)
                    code = call_with_payload(process, eyes_dll['SpidyGpuStart'],
                                             struct.pack('<4I2Q4I', 0x53475043, 3, 48, game.pid, game.base, queue,
                                                         0, copy, read_back, 0))
                    if code:
                        raise RuntimeError(f'GPU bridge start: {code}')
                    stops.append(('gpu', eyes_dll['SpidyGpuStop']))
                code = call_with_payload(process, eyes_dll['SpidyStart'],
                                         struct.pack('<4IQ4I', 0x53534346, 1, 40, game.pid, game.base, 0, a.views,
                                                     a.width, a.height))
                if code:
                    raise RuntimeError(f'Eye views start: {code}')
                stops.insert(0, ('eyes', eyes_dll['SpidyStop']))
                eyes_on = True
                # The views are created on the next frames; give streaming a moment.
                run('warmup', 4, straight)
                result['phases'].pop()
                views = stereo_snapshot(game, eyes_dll['SpidyStereoData'])
                if views:
                    result['eye_views'] = [{k: e[k] for k in ('width', 'height', 'render_width', 'render_height',
                                                              'viewport_width', 'viewport_height', 'occlusion')}
                                           for e in views['eyes']]
                    print('eye views:', result['eye_views'], flush=True)
                    eye_views[:] = [e['view'] for e in views['eyes']]
            if name == 'shots':
                shots()
                continue
            if name == 'after' and eyes_on:
                while stops:
                    label, export = stops.pop(0)
                    result[f'{label}_stop'] = call_remote(process, export)
                eyes_on = False
                time.sleep(3)
            off = name.endswith('_off')
            if off:
                occlusion(False)
            run(name, a.seconds+(2 if name != 'stock' else 1),
                {'vr': straight, 'turn': turning, 'narrow': narrow}.get(name.removesuffix('_off')))
            if off:
                occlusion(True)
        result['render_memory'] = render_memory_snapshot(game, render['SpidyRenderMemoryData'])
    finally:
        while stops:
            label, export = stops.pop(0)
            try:
                result[f'{label}_stop'] = call_remote(process, export)
            except OSError:
                pass
        gpu.stop()
        threads.close()
        if process:
            close(process)
        game.close()
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(result, indent=2)+'\n')
        print(f'Report: {a.output.resolve()}', flush=True)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'VR load probe failed: {error}', file=sys.stderr)
        sys.exit(2)
