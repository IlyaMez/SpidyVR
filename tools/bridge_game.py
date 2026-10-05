"""Load and test this project's native game camera/input bridge.

This uses in-game input queries, never OS keyboard injection. Experimental
controls expire in <=500 ms unless renewed. Every invocation disables its
hooks on exit; the DLL remains resident until game exit.
"""
import argparse
import ctypes as c
import hashlib
import json
import pathlib
import shutil
import struct
import sys
import time
from datetime import datetime, timezone

from capture_game_state import Game, find_game, open_process, close
from observe_game import (modules, call_remote, call_with_payload, remote_system_export,
                          load_library, free_library, get_export)
from inspect_game import PE

ROOT = pathlib.Path(__file__).resolve().parent.parent
DLL = ROOT / 'build/windows-ninja/spidy_bridge.dll'
STAGING = ROOT / 'reports/bridge-modules'
HOOKS = (0x1e1d600, 0x1ce2f40, 0x1cdfa70, 0x1cdf840)


def prepare(pid, process, dll=DLL, staging=STAGING,
            names=('SpidyStart','SpidyStop','SpidySubmit','SpidyBridgeData')):
    digest = hashlib.sha256(dll.read_bytes()).hexdigest()
    loaded = next((x for x in modules(pid) if x['name'].lower() == dll.name.lower()), None)
    if loaded:
        path = pathlib.Path(loaded['path']).resolve()
        if staging not in path.parents or path.parent.name != digest:
            raise RuntimeError('Different bridge build is resident. Restart the game before replacing it.')
    else:
        path = staging / digest / dll.name
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists(): shutil.copyfile(dll, path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError('Bridge staging hash mismatch')
    if not loaded:
        call_with_payload(process, remote_system_export(pid, b'LoadLibraryW'),
                          (str(path)+'\0').encode('utf-16-le'))
        loaded = next((x for x in modules(pid) if x['name'].lower() == dll.name.lower()), None)
    if not loaded or pathlib.Path(loaded['path']).resolve() != path:
        raise RuntimeError('Bridge failed to load from the expected staging path')
    local = load_library(str(path))
    if not local: raise c.WinError(c.get_last_error())
    try:
        result = {}
        for name in names:
            address = get_export(local,name.encode())
            if not address: raise RuntimeError(f'Missing export: {name}')
            result[name] = loaded['base'] + address - local
        return result, digest
    finally: free_library(local)


def snapshot(game, address):
    for _ in range(4):
        raw = game.read(address,384)
        after = game.read(address+16,8)
        if len(raw)!=384 or len(after)!=8: return None
        if struct.unpack_from('<3I',raw)!=(0x53424441,1,384):
            raise RuntimeError('Bridge protocol mismatch')
        sequence=struct.unpack_from('<Q',raw,16)[0]
        if sequence&1 or sequence!=struct.unpack('<Q',after)[0]: continue
        names=('camera_calls','matched','camera_writes','input_frames','digital_queries','analog_queries',
               'input_served','qpc','mover','target','camera_transform','player_transform','input_self','caller')
        frame=dict(zip(names,struct.unpack_from('<14Q',raw,24)))
        frame.update(state=struct.unpack_from('<I',raw,12)[0],sequence=sequence)
        frame['thread'],frame['error']=struct.unpack_from('<2I',raw,136)
        for name,offset in [('before',144),('after',208),('player',272)]:
            matrix=struct.unpack_from('<16f',raw,offset)
            frame[name]=dict(position=matrix[12:15],basis=[matrix[i:i+3] for i in (0,4,8)])
        words=struct.unpack_from('<8I',raw,336)
        frame['queried_keys']=[i for i in range(256) if words[i//32] & (1<<(i%32))]
        frame['serial'],frame['expired']=struct.unpack_from('<2Q',raw,368)
        return frame
    return None


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seed-capture',type=pathlib.Path,required=True)
    parser.add_argument('--seconds',type=float,default=8)
    parser.add_argument('--jump',action='store_true',help='One 350ms native Space press after 1s of baseline')
    parser.add_argument('--camera-offset',type=float,nargs=3,metavar=('X','Y','Z'))
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'reports/bridge-capture.json')
    args=parser.parse_args()
    if not 2<=args.seconds<=60: parser.error('Use 2..60 seconds')
    if args.camera_offset and any(abs(x)>2 for x in args.camera_offset): parser.error('Offset bound is 2 meters')
    game=Game(find_game()) # Full executable hash gate before writable handle.
    process=None; exports=None; stopped=False; frames=[]
    try:
        seed=json.loads(args.seed_capture.read_text(encoding='utf-8'))
        if seed['pid']!=game.pid or int(seed['module_base'],16)!=game.base:
            raise RuntimeError('Seed belongs to a different game process')
        heroes=[game.candidate(int(x['object'],16),'hero_local') for x in seed['candidates'] if x['kind']=='hero_local']
        heroes=[x for x in heroes if x]
        if len(heroes)!=1: raise RuntimeError('Exactly one validated local hero is required')
        hero=heroes[0]
        pe=PE(pathlib.Path(game.path).read_bytes())
        originals={rva:pe.bytes(rva,16) for rva in HOOKS}
        for rva,code in originals.items():
            if game.read(game.base+rva,16)!=code:
                raise RuntimeError(f'Integration entry {rva:#x} is already patched')
        process=open_process(0x0400|0x0010|0x0020|0x0008|0x0002,False,game.pid)
        if not process: raise c.WinError(c.get_last_error())
        exports,digest=prepare(game.pid,process)
        config=struct.pack('<4I3Q',0x53424346,1,40,game.pid,game.base,int(hero['object'],16),int(hero['actor_record'],16))
        status=call_with_payload(process,exports['SpidyStart'],config)
        if status: raise RuntimeError(f'Bridge start returned {status}')
        print(f'Native bridge active in PID {game.pid}. Sampling {args.seconds:g}s.',flush=True)
        start=time.monotonic(); serial=time.time_ns(); last_submit=0; previous=None; submitted_jump=False
        while (elapsed:=time.monotonic()-start)<args.seconds:
            # Camera experiment: 1s baseline, controlled interval, then 1s stock.
            camera=args.camera_offset and 1<=elapsed<args.seconds-1
            jump=args.jump and not submitted_jump and elapsed>=1
            if jump or (camera and elapsed-last_submit>=.15):
                serial+=1
                modes=(1 if jump else 0)|(2 if camera else 0)
                offset=args.camera_offset or (0,0,0)
                payload=struct.pack('<4IQ2I8f',0x53424354,1,64,modes,serial,350,16 if jump else 0,
                                    *offset,0,0,0,0,1)
                status=call_with_payload(process,exports['SpidySubmit'],payload)
                if status: raise RuntimeError(f'Control rejected: {status}')
                submitted_jump |= jump
                last_submit=elapsed
            frame=snapshot(game,exports['SpidyBridgeData'])
            if frame and frame['sequence']!=previous:
                frame['seconds']=elapsed; frames.append(frame); previous=frame['sequence']
            time.sleep(.01)
        status=call_remote(process,exports['SpidyStop']); stopped=status==0
        restored=all(game.read(game.base+rva,16)==code for rva,code in originals.items())
        result=dict(pid=game.pid,module_base=hex(game.base),utc=datetime.now(timezone.utc).isoformat(),
                    bridge_sha256=digest,jump=args.jump,camera_offset=args.camera_offset,samples=frames,
                    stopped=stopped,entries_restored=restored,native_stereo_verified=False)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        if frames:
            first,last=frames[0],frames[-1]
            summary={name:last[name]-first[name] for name in ('camera_calls','matched','camera_writes',
                'input_frames','digital_queries','analog_queries','input_served')}
            positions=[f['player']['position'] for f in frames if f['matched']]
            if positions:
                summary['player_height_range']=max(p[1] for p in positions)-min(p[1] for p in positions)
            summary.update(stopped=stopped,entries_restored=restored,queried_keys=last['queried_keys'])
            print(json.dumps(summary,indent=2))
        print(f'Saved {args.output}')
        return 0 if frames and stopped and restored else 1
    finally:
        if process and exports and not stopped:
            try: call_remote(process,exports['SpidyStop'])
            except Exception as error: print(f'Cleanup failed: {error}; close the game.',file=sys.stderr)
        if process: close(process)
        game.close()


if __name__=='__main__':
    try: sys.exit(main())
    except (OSError,ValueError,RuntimeError) as error:
        print(f'Bridge test failed: {error}',file=sys.stderr);sys.exit(2)
