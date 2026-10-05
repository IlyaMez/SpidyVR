"""Collect passive native rendering call stacks from the supported game."""
import argparse
import ctypes as c
import json
import pathlib
import struct
import sys
import time
from capture_game_state import Game,find_game,open_process,close
from bridge_game import ROOT,prepare
from observe_game import call_remote,modules


def snapshot(game,address):
    for _ in range(5):
        raw=game.read(address,7984)
        seq=game.read(address+16,8)
        if len(raw)!=7984 or len(seq)!=8:return None
        magic,version,size,state=struct.unpack_from('<4I',raw)
        if (magic,version,size)!=(0x53525042,1,7984):raise RuntimeError('Render probe protocol mismatch')
        sequence=struct.unpack_from('<Q',raw,16)[0]
        if sequence&1 or sequence!=struct.unpack('<Q',seq)[0]:continue
        presents,executes,count,error=struct.unpack_from('<2Q2I',raw,24)
        if count>32:raise RuntimeError('Invalid render sample count')
        table=modules(game.pid)
        def symbol(address):
            module=next((m for m in table if m['base']<=address<m['base']+m['size']),None)
            return f"{module['name']}+{address-module['base']:#x}" if module else hex(address)
        samples=[]
        for i in range(count):
            offset=48+i*248
            n,obj,dev,qpc,thread,kind,width,height,fmt,depth=struct.unpack_from('<4Q6I',raw,offset)
            stack=struct.unpack_from('<24Q',raw,offset+56)[:min(depth,24)]
            samples.append(dict(count=n,object=hex(obj),device=hex(dev),qpc=qpc,thread=thread,
                                kind=kind,width=width,height=height,format=fmt,stack=[symbol(a) for a in stack]))
        return dict(state=state,presents=presents,executes=executes,error=error,samples=samples)
    return None


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds',type=float,default=6)
    parser.add_argument('--output',type=pathlib.Path,default=ROOT/'reports/render-probe.json')
    args=parser.parse_args()
    if not 1<=args.seconds<=60:parser.error('Use 1..60 seconds')
    game=Game(find_game());process=None;exports=None;stopped=False
    try:
        process=open_process(0x0400|0x0010|0x0020|0x0008|0x0002,False,game.pid)
        if not process:raise c.WinError(c.get_last_error())
        exports,digest=prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_render_probe.dll',
                               ROOT/'reports/render-modules',('SpidyStart','SpidyStop','SpidyRenderData'))
        status=call_remote(process,exports['SpidyStart'])
        if status:raise RuntimeError(f'Render probe start: {status}')
        print('Passive render probe collecting call stacks.',flush=True)
        start=time.monotonic()
        while time.monotonic()-start<args.seconds:time.sleep(.1)
        result=snapshot(game,exports['SpidyRenderData'])
        status=call_remote(process,exports['SpidyStop']);stopped=status==0
        if not result:raise RuntimeError('No coherent render sample')
        result.update(pid=game.pid,module_base=hex(game.base),sha256=digest,stopped=stopped)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(result,indent=2))
        return 0 if result['samples'] and stopped else 1
    finally:
        if process and exports and not stopped:call_remote(process,exports['SpidyStop'])
        if process:close(process)
        game.close()


if __name__=='__main__':
    try:sys.exit(main())
    except (OSError,ValueError,RuntimeError) as error:
        print(f'Render probe failed: {error}',file=sys.stderr);sys.exit(2)
