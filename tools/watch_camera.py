"""Bounded hardware read/write watch of the validated final camera position."""
import argparse
import ctypes as c
import json
import pathlib
import struct
import sys
import time
from capture_game_state import Game,find_game,open_process,close
from bridge_game import ROOT,prepare
from observe_game import call_remote,call_with_payload,modules


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge-capture',type=pathlib.Path,required=True)
    parser.add_argument('--seconds',type=float,default=3)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'reports/camera-watch.json')
    args=parser.parse_args()
    if not .1<=args.seconds<=10:parser.error('Use 0.1..10 seconds')
    game=Game(find_game());process=None;exports=None;stopped=False
    try:
        seed=json.loads(args.bridge_capture.read_text(encoding='utf-8'))
        if seed['pid']!=game.pid or int(seed['module_base'],16)!=game.base:
            raise RuntimeError('Camera capture belongs to a different process')
        latest=seed['samples'][-1]
        camera=latest['camera_transform']
        if not game.transform(camera) or game.pointer(game.pointer(latest['mover']+8))!=camera:
            raise RuntimeError('Camera actor is no longer valid')
        process=open_process(0x0400|0x0010|0x0020|0x0008|0x0002,False,game.pid)
        if not process:raise c.WinError(c.get_last_error())
        exports,digest=prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_camera_watch.dll',
            ROOT/'reports/watch-modules',('SpidyStart','SpidyStop','SpidyWatchData'))
        payload=struct.pack('<6I2Q',0x53574346,1,40,game.pid,latest['thread'],int(args.seconds*1000),
                            camera+0x30,game.base)
        status=call_with_payload(process,exports['SpidyStart'],payload)
        if status:raise RuntimeError(f'Watch start returned {status}')
        print(f'Watching final camera position for {args.seconds:g}s on thread {latest["thread"]}.',flush=True)
        end=time.monotonic()+args.seconds+.25
        while time.monotonic()<end:time.sleep(.05)
        status=call_remote(process,exports['SpidyStop']);stopped=status==0
        raw=game.read(exports['SpidyWatchData'],22576)
        if len(raw)!=22576:raise RuntimeError('Watch data unreadable')
        magic,version,size,state,sequence,hits,count,error,restored,thread=struct.unpack_from('<4I2Q4I',raw)
        if (magic,version,size)!=(0x53574154,1,22576) or sequence&1 or count>128:
            raise RuntimeError('Watch protocol mismatch or overlapping write')
        table=modules(game.pid)
        def symbol(address):
            m=next((m for m in table if m['base']<=address<m['base']+m['size']),None)
            return f"{m['name']}+{address-m['base']:#x}" if m else hex(address)
        samples=[]
        for i in range(count):
            values=struct.unpack_from('<22Q',raw,48+i*176)
            samples.append(dict(rip=symbol(values[0]),count=values[1],
                registers=[hex(x) for x in values[2:6]],stack=[symbol(x) for x in values[6:] if x]))
        result=dict(pid=game.pid,module_base=hex(game.base),state=state,hits=hits,error=error,
                    stopped=stopped,restored=restored,thread=thread,sha256=digest,samples=samples)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(result,indent=2))
        return 0 if stopped and restored and hits else 1
    finally:
        if process and exports and not stopped:call_remote(process,exports['SpidyStop'])
        if process:close(process)
        game.close()


if __name__=='__main__':
    try:sys.exit(main())
    except (OSError,ValueError,RuntimeError) as error:
        print(f'Camera watch failed: {error}',file=sys.stderr);sys.exit(2)
