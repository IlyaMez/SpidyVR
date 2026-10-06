"""Read-only, bounded camera/player discovery for the running supported game.

No injection, game writes, hooks, raw memory dumps, or process suspension.
The game must already be running. Results are research candidates only.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
import math
import pathlib
import struct
import sys
import time
from inspect_game import EXPECTED_SHA256, VTABLES, FUNCTIONS

# Address fact independently checked by discover_render_types.py.
LIVE_VTABLES = {**VTABLES, 'follow_camera': 0x38715C8}

if sys.platform != 'win32':
    raise SystemExit('Live capture requires Windows; inspect_game.py works offline.')

k32 = c.WinDLL('kernel32', use_last_error=True)
SIZE_T = c.c_size_t

class ProcessEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('usage', w.DWORD), ('pid', w.DWORD), ('heap', SIZE_T),
                ('module', w.DWORD), ('threads', w.DWORD), ('parent', w.DWORD),
                ('priority', w.LONG), ('flags', w.DWORD), ('name', w.WCHAR * 260)]

class ModuleEntry(c.Structure):
    _fields_ = [('size', w.DWORD), ('module_id', w.DWORD), ('pid', w.DWORD),
                ('global_usage', w.DWORD), ('process_usage', w.DWORD), ('base', c.c_void_p),
                ('bytes', w.DWORD), ('module', w.HMODULE), ('name', w.WCHAR * 256),
                ('path', w.WCHAR * 260)]

class MemoryInfo(c.Structure):
    _fields_ = [('base', c.c_void_p), ('allocation_base', c.c_void_p),
                ('allocation_protect', w.DWORD), ('partition', w.WORD), ('size', SIZE_T),
                ('state', w.DWORD), ('protect', w.DWORD), ('kind', w.DWORD)]

def bind(name, result, *arguments):
    fn = getattr(k32, name)
    fn.restype, fn.argtypes = result, list(arguments)
    return fn

snapshot = bind('CreateToolhelp32Snapshot', w.HANDLE, w.DWORD, w.DWORD)
close = bind('CloseHandle', w.BOOL, w.HANDLE)
first_process = bind('Process32FirstW', w.BOOL, w.HANDLE, c.POINTER(ProcessEntry))
next_process = bind('Process32NextW', w.BOOL, w.HANDLE, c.POINTER(ProcessEntry))
first_module = bind('Module32FirstW', w.BOOL, w.HANDLE, c.POINTER(ModuleEntry))
open_process = bind('OpenProcess', w.HANDLE, w.DWORD, w.BOOL, w.DWORD)
read_memory = bind('ReadProcessMemory', w.BOOL, w.HANDLE, c.c_void_p, c.c_void_p, SIZE_T, c.POINTER(SIZE_T))
query_memory = bind('VirtualQueryEx', SIZE_T, w.HANDLE, c.c_void_p, c.POINTER(MemoryInfo), SIZE_T)
query_path = bind('QueryFullProcessImageNameW', w.BOOL, w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD))

def checked_snapshot(flags, pid, patience=2):
    # A module snapshot of a process that is loading or unloading modules fails with
    # ERROR_BAD_LENGTH (24), and Windows documents retrying it until it succeeds. The game loads
    # dozens of DLLs in its first seconds, while the launcher loads Spidy's modules into it.
    deadline = time.monotonic() + patience
    while True:
        handle = snapshot(flags, pid)
        if handle != c.c_void_p(-1).value: return handle
        error = c.get_last_error()
        if error != 24 or time.monotonic() >= deadline: raise c.WinError(error)
        time.sleep(.01)

def find_game():
    handle = checked_snapshot(2, 0)
    entry = ProcessEntry(); entry.size = c.sizeof(entry)
    matches = []
    try:
        ok = first_process(handle, c.byref(entry))
        while ok:
            if entry.name.lower() == 'spider-man.exe': matches.append(entry.pid)
            ok = next_process(handle, c.byref(entry))
    finally: close(handle)
    if not matches: raise RuntimeError('Spider-Man is not running. Load free roam before live capture.')
    if len(matches) != 1: raise RuntimeError('More than one Spider-Man process exists; close the extra instance.')
    return matches[0]

class Game:
    def __init__(self, pid):
        self.pid = pid
        # Deliberately no PROCESS_VM_WRITE, VM_OPERATION, or CREATE_THREAD rights.
        self.handle = open_process(0x0400 | 0x0010, False, pid)
        if not self.handle: raise c.WinError(c.get_last_error())
        try:
            size = w.DWORD(32768); path = c.create_unicode_buffer(size.value)
            if not query_path(self.handle, 0, path, c.byref(size)): raise c.WinError(c.get_last_error())
            self.path = pathlib.Path(path.value)
            if hashlib.sha256(self.path.read_bytes()).hexdigest() != EXPECTED_SHA256:
                raise RuntimeError('The running executable is not the supported Steam build.')
            snap = checked_snapshot(0x08 | 0x10, pid)
            try:
                module = ModuleEntry(); module.size = c.sizeof(module)
                if not first_module(snap, c.byref(module)): raise c.WinError(c.get_last_error())
                if module.name.lower() != 'spider-man.exe': raise RuntimeError('Unexpected main module')
                self.base = module.base
            finally: close(snap)
        except BaseException:
            self.close(); raise

    def close(self):
        if self.handle: close(self.handle); self.handle = None

    def read(self, address, size):
        if not address or address < 0x10000: return b''
        data = c.create_string_buffer(size); count = SIZE_T()
        if not read_memory(self.handle, address, data, size, c.byref(count)): return b''
        return data.raw[:count.value]

    def pointer(self, address):
        data = self.read(address, 8)
        return struct.unpack('<Q', data)[0] if len(data) == 8 else 0

    def regions(self):
        address = 0x10000
        while address < 0x7fffffffffff:
            info = MemoryInfo()
            if not query_memory(self.handle, address, c.byref(info), c.sizeof(info)): break
            end = (info.base or 0) + info.size
            if end <= address: break
            # CPU objects do not live in uncacheable/write-combined GPU upload heaps.
            if info.state == 0x1000 and info.kind == 0x20000 and info.protect & 0xcc and not info.protect & 0x701:
                yield info.base, info.size
            address = end

    def transform(self, address):
        first = self.read(address, 64)
        if len(first) != 64 or first != self.read(address, 64): return None
        values = struct.unpack('<16f', first)
        if not all(math.isfinite(v) and abs(v) < 1e7 for v in values): return None
        rows = [values[i:i + 3] for i in (0, 4, 8)]
        if any(abs(sum(v * v for v in row) - 1) > .15 for row in rows): return None
        if any(abs(sum(a * b for a, b in zip(rows[i], rows[j]))) > .1
               for i, j in ((0, 1), (0, 2), (1, 2))): return None
        return dict(position=values[12:15], basis=rows)

    def candidate(self, address, kind):
        if kind not in LIVE_VTABLES or self.pointer(address) != self.base + LIVE_VTABLES[kind]: return None
        if kind == 'follow_camera':
            data = self.read(address + 0x74, 16)
            if len(data) != 16: return None
            fov, _, near, far = struct.unpack('<4f', data)
            if not all(math.isfinite(v) for v in (fov, near, far)): return None
            if not (.2 < fov < 3 and .001 < near < 2 and 10 < far < 1e7): return None
            return dict(kind=kind, object=hex(address), lens=dict(fov_radians=fov, near=near, far=far))
        record = self.pointer(address + 8)
        transform = self.pointer(record)
        player = self.transform(transform)
        if not player: return None
        result = dict(kind=kind, object=hex(address), actor_record=hex(record),
                      actor_transform=hex(transform), player=player)
        if kind == 'hero_camera_manager':
            camera = self.transform(address + 0x744)
            if not camera: return None
            result['camera'] = camera
        return result

    def registered_candidates(self, max_seconds=3):
        """Read the native component registry, independently decoded at 16798f0.

        A slot is accepted only while its generation, object pointer, and the
        object's own handle agree. Callers can fall back to bounded discovery.
        This samples CPU objects; it is not a simulation-thread snapshot.
        """
        table = self.pointer(self.base + 0x7a44320)
        raw_count = self.read(self.base + 0x7a44340, 4)
        if not table or len(raw_count) != 4:
            return []
        count = struct.unpack('<i', raw_count)[0]
        if not 0 < count <= 0x100000:
            return []
        slots = self.read(table, count*16)
        if len(slots) != count*16:
            return []
        started = time.monotonic()
        types = {self.base + rva: name for name, rva in LIVE_VTABLES.items()}
        found = []
        for i in range(count):
            if time.monotonic()-started >= max_seconds:
                break
            address, generation = struct.unpack_from('<QI', slots, i*16)
            if address < 0x10000 or address % 8 or not 0 < generation <= 0xfff:
                continue
            header = self.read(address, 24)
            if len(header) != 24:
                continue
            kind = types.get(struct.unpack_from('<Q', header)[0])
            if not kind or struct.unpack_from('<I', header, 0x14)[0] != (generation << 20 | i):
                continue
            if self.read(table+i*16, 12) != slots[i*16:i*16+12]:
                continue
            value = self.candidate(address, kind)
            if value:
                found.append(value)
        if self.pointer(self.base + 0x7a44320) != table:
            return []
        return found

    def discover(self, max_bytes, max_seconds):
        needles = {struct.pack('<Q', self.base + rva): name for name, rva in LIVE_VTABLES.items()}
        found = {}; scanned = 0; started = time.monotonic(); complete = True
        self.reference_hits = {name: 0 for name in LIVE_VTABLES}
        # Reach small CPU allocations before multi-gigabyte pools. Budgets still
        # apply to every byte and the report explicitly marks a partial scan.
        regions = sorted(self.regions(), key=lambda region: (region[1] > 256 << 20, region[0]))
        self.eligible_mib = sum(size for _, size in regions) / (1 << 20)
        for base, size in regions:
            for offset in range(0, size, 1 << 20):
                if scanned >= max_bytes or time.monotonic() - started >= max_seconds:
                    return found, scanned, False
                length = min(1 << 20, size - offset, max_bytes - scanned)
                data = self.read(base + offset, length); scanned += length
                for needle, name in needles.items():
                    cursor = 0
                    while True:
                        hit = data.find(needle, cursor)
                        if hit < 0: break
                        address = base + offset + hit
                        if address % 8 == 0:
                            self.reference_hits[name] += 1
                            candidate = self.candidate(address, name)
                            if candidate: found[(address, name)] = candidate
                        cursor = hit + 1
                if len(found) >= 64: return found, scanned, False
                time.sleep(.001)
        return found, scanned, complete

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path('reports/live-camera.json'))
    parser.add_argument('--scan-mib', type=int, default=8192)
    parser.add_argument('--scan-seconds', type=float, default=45)
    parser.add_argument('--sample-seconds', type=float, default=3)
    parser.add_argument('--seed-capture', type=pathlib.Path,
                        help='Reuse validated objects from this process, without another heap scan')
    args = parser.parse_args()
    if not (1 <= args.scan_mib <= 16384 and 0 < args.scan_seconds <= 60 and 0 <= args.sample_seconds <= 30):
        parser.error('Limits: scan 1..16384 MiB, scan time 0..60 s, sample time 0..30 s')
    if c.sizeof(c.c_void_p) != 8: raise RuntimeError('Use 64-bit Python')
    pid = find_game(); game = Game(pid)
    try:
        if args.seed_capture:
            seed = json.loads(args.seed_capture.read_text(encoding='utf-8'))
            if seed['pid'] != pid or int(seed['module_base'], 16) != game.base:
                raise RuntimeError('Seed belongs to a different process; discover again.')
            candidates = {}
            for item in seed['candidates']:
                address, kind = int(item['object'], 16), item['kind']
                if value := game.candidate(address, kind): candidates[address, kind] = value
            scanned, complete = 0, False
        else:
            candidates, scanned, complete = game.discover(args.scan_mib << 20, args.scan_seconds)
        print(f'Discovery: {len(candidates)} candidates; starting {args.sample_seconds:g}s capture.', flush=True)
        samples = []; started = time.monotonic()
        started_utc = datetime.now(timezone.utc).isoformat()
        while candidates and time.monotonic() - started < args.sample_seconds:
            objects = [v for (address, name) in candidates if (v := game.candidate(address, name))]
            samples.append(dict(seconds=time.monotonic() - started, candidates=objects))
            time.sleep(1 / 30)
        result = dict(pid=pid, module_base=hex(game.base), executable=str(game.path),
                      started_utc=started_utc, seed_capture=str(args.seed_capture) if args.seed_capture else None,
                      scan_complete=complete, scanned_mib=scanned / (1 << 20),
                      eligible_mib=getattr(game, 'eligible_mib', None),
                      reference_hits=getattr(game, 'reference_hits', None),
                      function_entries={name: dict(rva=hex(rva),
                          matches_offline_signature=game.read(game.base + rva, len(bytes.fromhex(pattern))) == bytes.fromhex(pattern))
                          for name, (rva, pattern) in FUNCTIONS.items()},
                      candidates=list(candidates.values()), samples=samples,
                      runtime_abi_verified=False, native_stereo_verified=False,
                      note='Read-only object candidates. Polling is not synchronized to the game simulation.')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(f'{len(candidates)} candidates, {len(samples)} samples; scan complete: {complete}. Saved {args.output}')
        return 0 if candidates else 1
    finally: game.close()

if __name__ == '__main__':
    try: sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Capture unavailable: {error}', file=sys.stderr); sys.exit(2)
