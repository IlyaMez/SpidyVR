"""Opt-in in-process camera diagnostic, restricted to the supported Spider-Man build.

Loads this project's observer DLL for the current game process only. Installs one
temporary camera-update hook, samples its published state, then disables the hook.
The DLL remains mapped until game exit so in-flight calls can return safely.
No UI automation, controller injection, camera writes, or game-directory install.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
from datetime import datetime, timezone
import hashlib
import json
import pathlib
import shutil
import struct
import sys
import time

from capture_game_state import (Game, ModuleEntry, bind, checked_snapshot, close,
                                find_game, first_module, open_process, FUNCTIONS)

ROOT = pathlib.Path(__file__).resolve().parent.parent
BUILD_DLL = ROOT / 'build/windows-ninja/spidy_observer.dll'
next_module = bind('Module32NextW', w.BOOL, w.HANDLE, c.POINTER(ModuleEntry))
allocate = bind('VirtualAllocEx', c.c_void_p, w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD, w.DWORD)
release = bind('VirtualFreeEx', w.BOOL, w.HANDLE, c.c_void_p, c.c_size_t, w.DWORD)
write = bind('WriteProcessMemory', w.BOOL, w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t,
             c.POINTER(c.c_size_t))
create_thread = bind('CreateRemoteThread', w.HANDLE, w.HANDLE, c.c_void_p, c.c_size_t,
                     c.c_void_p, c.c_void_p, w.DWORD, c.c_void_p)
wait = bind('WaitForSingleObject', w.DWORD, w.HANDLE, w.DWORD)
exit_code = bind('GetExitCodeThread', w.BOOL, w.HANDLE, c.POINTER(w.DWORD))
load_library = bind('LoadLibraryW', w.HMODULE, w.LPCWSTR)
free_library = bind('FreeLibrary', w.BOOL, w.HMODULE)
get_export = bind('GetProcAddress', c.c_void_p, w.HMODULE, c.c_char_p)
get_module = bind('GetModuleHandleW', w.HMODULE, w.LPCWSTR)
module_from_address = bind('GetModuleHandleExW', w.BOOL, w.DWORD, c.c_void_p, c.POINTER(w.HMODULE))
module_filename = bind('GetModuleFileNameW', w.DWORD, w.HMODULE, w.LPWSTR, w.DWORD)


def modules(pid):
    snap = checked_snapshot(0x18, pid)
    entry = ModuleEntry(); entry.size = c.sizeof(entry)
    result = []
    try:
        ok = first_module(snap, c.byref(entry))
        while ok:
            result.append(dict(name=entry.name, path=entry.path, base=entry.base, size=entry.bytes))
            ok = next_module(snap, c.byref(entry))
    finally:
        close(snap)
    return result


def remote_system_export(pid, name):
    local = get_export(get_module('kernel32.dll'), name)
    if not local: raise c.WinError(c.get_last_error())
    owner = w.HMODULE()
    # Resolve forwarded exports to their actual owner; do not assume the two
    # processes map system DLLs at the same address.
    if not module_from_address(0x4 | 0x2, local, c.byref(owner)):
        raise c.WinError(c.get_last_error())
    path = c.create_unicode_buffer(32768)
    if not module_filename(owner, path, len(path)): raise c.WinError(c.get_last_error())
    matching = [m for m in modules(pid) if m['path'].lower() == path.value.lower()]
    if len(matching) != 1: raise RuntimeError('Could not resolve the remote system module.')
    return matching[0]['base'] + local - owner.value


def call_remote(process, address, parameter=0):
    handle = create_thread(process, None, 0, address, parameter, 0, None)
    if not handle: raise c.WinError(c.get_last_error())
    try:
        status = wait(handle, 15000)
        if status == 258:
            raise TimeoutError('Remote call timed out; its argument allocation was retained. Do not retry blindly.')
        if status != 0: raise c.WinError(c.get_last_error())
        code = w.DWORD()
        if not exit_code(handle, c.byref(code)): raise c.WinError(c.get_last_error())
        return code.value
    finally:
        close(handle)


def call_with_payload(process, address, payload):
    remote = allocate(process, None, len(payload), 0x3000, 4)
    if not remote: raise c.WinError(c.get_last_error())
    safe_to_release = True
    try:
        count = c.c_size_t()
        if not write(process, remote, payload, len(payload), c.byref(count)) or count.value != len(payload):
            raise c.WinError(c.get_last_error())
        try:
            return call_remote(process, address, remote)
        except TimeoutError:
            safe_to_release = False
            raise
    finally:
        if safe_to_release: release(process, remote, 0, 0x8000)


def observer_module(pid):
    found = [m for m in modules(pid) if m['name'].lower() == 'spidy_observer.dll']
    if len(found) > 1: raise RuntimeError('Multiple observer modules are loaded; restart the game before observing.')
    return found[0] if found else None


def ensure_loaded(pid, process):
    if not BUILD_DLL.is_file(): raise RuntimeError('Build first with tools/build.ps1 -Observer.')
    digest = hashlib.sha256(BUILD_DLL.read_bytes()).hexdigest()
    existing = observer_module(pid)
    if existing:
        loaded_path = pathlib.Path(existing['path']).resolve()
        if ROOT / 'reports/observer-modules' not in loaded_path.parents:
            raise RuntimeError('The loaded observer is not from this project staging directory.')
        if hashlib.sha256(loaded_path.read_bytes()).hexdigest() != digest:
            raise RuntimeError('A different observer build is resident; restart the game before replacing it.')
        return existing, digest
    # A staged immutable copy lets the project continue building while the game
    # holds its DLL open. Never overwrite a resident binary or installed mod.
    staged = ROOT / 'reports/observer-modules' / digest / BUILD_DLL.name
    staged.parent.mkdir(parents=True, exist_ok=True)
    if staged.exists():
        if hashlib.sha256(staged.read_bytes()).hexdigest() != digest:
            raise RuntimeError('Observer staging hash mismatch.')
    else:
        shutil.copyfile(BUILD_DLL, staged)
    payload = (str(staged) + '\0').encode('utf-16-le')
    call_with_payload(process, remote_system_export(pid, b'LoadLibraryW'), payload)
    loaded = observer_module(pid)
    if not loaded or pathlib.Path(loaded['path']).resolve() != staged.resolve():
        raise RuntimeError('The observer DLL did not load from the expected path.')
    return loaded, digest


def exports(loaded):
    loaded_path = pathlib.Path(loaded['path']).resolve()
    if ROOT / 'reports/observer-modules' not in loaded_path.parents:
        raise RuntimeError('Observer is outside this project staging directory.')
    if hashlib.sha256(loaded_path.read_bytes()).hexdigest() != loaded_path.parent.name:
        raise RuntimeError('Loaded observer does not match its staging hash.')
    # DllMain only disables thread notifications. Explicit SpidyStart is never
    # invoked locally. Reading exports from this exact image gives their RVAs.
    local = load_library(loaded['path'])
    if not local: raise c.WinError(c.get_last_error())
    try:
        result = {}
        for name in ('SpidyStart', 'SpidyStop', 'SpidyObserverData'):
            address = get_export(local, name.encode('ascii'))
            if not address: raise RuntimeError(f'Missing observer export: {name}')
            result[name] = loaded['base'] + address - local
        return result
    finally:
        free_library(local)


def read_snapshot(game, address):
    for _ in range(4):
        data = game.read(address, 256)
        after = game.read(address + 24, 8)
        if len(data) != 256 or len(after) != 8: return None
        magic, version, size, pid = struct.unpack_from('<4I', data)
        if (magic, version, size) != (0x5350594f, 2, 256):
            raise RuntimeError('Observer protocol mismatch')
        frequency, sequence, calls, rejected, overlap, qpc, manager, record, transform = struct.unpack_from('<q8Q', data, 16)
        if sequence & 1 or sequence != struct.unpack('<Q', after)[0]: continue
        thread, state, dt, flags = struct.unpack_from('<IIfI', data, 88)
        update_object, update_vtable = struct.unpack_from('<2Q', data, 104)
        camera = struct.unpack_from('<16f', data, 120)
        player = struct.unpack_from('<16f', data, 184)
        error, validation = struct.unpack_from('<2I', data, 248)
        return dict(pid=pid, frequency=frequency, sequence=sequence, calls=calls,
                    rejected=rejected, overlapping=overlap, qpc=qpc,
                    manager=hex(manager), actor_record=hex(record), actor_transform=hex(transform),
                    update_object=hex(update_object), update_vtable=hex(update_vtable), validation=validation,
                    thread=thread, state=state, dt=dt, flags=flags, error=error,
                    camera=dict(position=camera[12:15], basis=[camera[i:i+3] for i in (0, 4, 8)]),
                    player=dict(position=player[12:15], basis=[player[i:i+3] for i in (0, 4, 8)]))
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, default=10)
    parser.add_argument('--seed-capture', type=pathlib.Path, help='Current-process camera/player discovery JSON')
    parser.add_argument('--output', type=pathlib.Path, default=ROOT / 'reports/native-camera.json')
    parser.add_argument('--stop', action='store_true', help='Disable an already loaded observer without starting it')
    args = parser.parse_args()
    if not 0 < args.seconds <= 120: parser.error('--seconds must be between 0 and 120')
    if not args.stop and not args.seed_capture: parser.error('--seed-capture is required when starting the observer')
    pid = find_game()
    game = Game(pid) # SHA-256 gate BEFORE any writable process handle or remote execution.
    process = None
    addresses = None
    samples = []
    stopped = None
    try:
        process = open_process(0x0400 | 0x0010 | 0x0020 | 0x0008 | 0x0002, False, pid)
        if not process: raise c.WinError(c.get_last_error())
        if args.stop:
            loaded = observer_module(pid)
            if not loaded: print('No observer is loaded.'); return 0
            addresses = exports(loaded)
            status = call_remote(process, addresses['SpidyStop'])
            if status: raise RuntimeError(f'Observer stop failed: {status}')
            rva, pattern = FUNCTIONS['camera_update']
            if game.read(game.base + rva, len(bytes.fromhex(pattern))) != bytes.fromhex(pattern):
                raise RuntimeError('Stop returned, but the camera entry does not match its original signature.')
            print('Observer disabled. DLL remains mapped until game exit.'); return 0
        seed = json.loads(args.seed_capture.read_text(encoding='utf-8'))
        if seed['pid'] != pid or int(seed['module_base'], 16) != game.base:
            raise RuntimeError('Camera discovery belongs to a different process; discover again.')
        valid_candidates = [candidate for item in seed['candidates']
                            if (candidate := game.candidate(int(item['object'], 16), item['kind']))]
        records = {item['actor_record'] for item in valid_candidates if item['kind'] == 'hero_local'}
        cameras = [item for item in valid_candidates
                   if item['kind'] == 'hero_camera_manager' and item['actor_record'] in records]
        if len(cameras) != 1: raise RuntimeError('Need exactly one currently valid camera/player pair.')
        config = struct.pack('<4I2Q', 0x53505943, 1, 32, pid, game.base, int(cameras[0]['object'], 16))
        loaded, digest = ensure_loaded(pid, process)
        addresses = exports(loaded)
        status = call_with_payload(process, addresses['SpidyStart'], config)
        if status: raise RuntimeError(f'Observer start failed: {status}')
        print(f'Camera observer active in PID {pid}; collecting {args.seconds:g}s.', flush=True)
        started = time.monotonic()
        started_utc = datetime.now(timezone.utc).isoformat()
        initial = read_snapshot(game, addresses['SpidyObserverData'])
        previous = initial['calls'] if initial else None
        while time.monotonic() - started < args.seconds:
            frame = read_snapshot(game, addresses['SpidyObserverData'])
            if frame and frame['pid'] != pid: raise RuntimeError('Observer PID mismatch')
            if frame and frame['calls'] and frame['calls'] != previous:
                frame['seconds'] = time.monotonic() - started
                samples.append(frame)
                previous = frame['calls']
            time.sleep(.005)
        status = call_remote(process, addresses['SpidyStop'])
        stopped = status == 0
        rva, pattern = FUNCTIONS['camera_update']
        restored = game.read(game.base + rva, len(bytes.fromhex(pattern))) == bytes.fromhex(pattern)
        result = dict(pid=pid, module_base=hex(game.base), started_utc=started_utc,
                      observer_sha256=digest, observer_module=loaded['path'], samples=samples,
                      stopped=stopped, camera_entry_restored=restored,
                      native_stereo_verified=False,
                      note='Camera-update observations only. No render hook or movement API is established.')
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(f'{len(samples)} distinct updates sampled. Hook disabled: {stopped}; entry restored: {restored}. Saved {args.output}')
        valid = [sample for sample in samples if sample['validation'] == 0 and sample['qpc']]
        print(f'{len(valid)}/{len(samples)} sampled updates contained valid transforms.')
        return 0 if valid and stopped and restored else 1
    finally:
        # Disable on Ctrl+C or sampling/output failure as well. Never unload a DLL
        # whose callback could still be returning on another thread.
        if process and addresses and not args.stop and stopped is not True:
            try:
                result = call_remote(process, addresses['SpidyStop'])
                if result: print(f'Observer cleanup status: {result}', file=sys.stderr)
            except (OSError, RuntimeError) as error:
                print(f'Observer cleanup failed: {error}. Close the game to unload it.', file=sys.stderr)
        if process: close(process)
        game.close()


if __name__ == '__main__':
    try: sys.exit(main())
    except (OSError, ValueError, RuntimeError) as error:
        print(f'Observer unavailable: {error}', file=sys.stderr); sys.exit(2)
