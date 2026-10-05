"""Bounded native camera/projection test. Does not implement a stereo render loop."""
import argparse
import ctypes as c
import json
import math
import pathlib
import struct
import sys
import time
from capture_game_state import Game, find_game, open_process, close
from bridge_game import ROOT, prepare
from observe_game import call_remote, call_with_payload
from inspect_game import PE


def snapshot(game,address):
    for _ in range(8):
        raw=game.read(address,2248)
        seq=game.read(address+16,8)
        if len(raw)!=2248 or len(seq)!=8:return None
        if struct.unpack_from('<3I',raw)!=(0x53564441,1,2248):raise RuntimeError('View protocol mismatch')
        sequence=struct.unpack_from('<Q',raw,16)[0]
        if sequence&1 or sequence!=struct.unpack('<Q',seq)[0]:continue
        result=dict(zip(('calls','matched','writes','view','caller'),struct.unpack_from('<5Q',raw,24)))
        result.update(state=struct.unpack_from('<I',raw,12)[0],sequence=sequence)
        result['thread'],result['error']=struct.unpack_from('<2I',raw,64)
        for name,offset in [('before',72),('after',1160)]:
            data=struct.unpack_from('<272f',raw,offset)
            result[name]=dict(pose=data[:16],projection=data[16:32],
                near=data[0x3f8//4],far=data[0x3fc//4],bounds=data[0x400//4:0x410//4],
                jitter=data[0x428//4:0x430//4])
        return result
    return None


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--seconds',type=float,default=5)
    p.add_argument('--rebuild',action='store_true',help='Rebuild with the same native lens and pose')
    p.add_argument('--yaw',type=float,default=0,help='Relative OpenXR yaw in degrees')
    p.add_argument('--offset',type=float,nargs=3,default=(0,0,0))
    p.add_argument('--fov',type=float,nargs=4,help='OpenXR left,right,down,up in degrees')
    p.add_argument('--output',type=pathlib.Path,default=ROOT/'reports/view-capture.json')
    a=p.parse_args()
    if not 3<=a.seconds<=60:p.error('Use 3..60 seconds')
    if not math.isfinite(a.yaw) or abs(a.yaw)>45:p.error('Yaw must be within 45 degrees')
    if any(not math.isfinite(v) or abs(v)>2 for v in a.offset):p.error('Offset bound is 2m')
    if a.fov and (any(not math.isfinite(f) or abs(f)>85 for f in a.fov) or
                  not(a.fov[0]<-1 and a.fov[1]>1 and a.fov[2]<-1 and a.fov[3]>1)):
        p.error('Use signed left/right/down/up bounds in (-85,85) degrees')
    game=Game(find_game());process=None;exports=None;stopped=False;frames=[]
    try:
        pe=PE(pathlib.Path(game.path).read_bytes());code=pe.bytes(0x1899ab0,16)
        if game.read(game.base+0x1899ab0,16)!=code:raise RuntimeError('View submit already patched')
        process=open_process(0x0400|0x0010|0x0020|0x0008|0x0002,False,game.pid)
        if not process:raise c.WinError(c.get_last_error())
        exports,digest=prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_view.dll',
            ROOT/'reports/view-modules',('SpidyStart','SpidyStop','SpidySubmit','SpidyViewData'))
        config=struct.pack('<4IQ',0x53564346,1,24,game.pid,game.base)
        status=call_with_payload(process,exports['SpidyStart'],config)
        if status:raise RuntimeError(f'View start: {status}')
        print('Native view probe active.',flush=True)
        start=time.monotonic();serial=time.time_ns();last_submit=-1;last_seq=None
        while (elapsed:=time.monotonic()-start)<a.seconds:
            modes=(4 if a.rebuild else 0)|(1 if a.yaw or any(a.offset) else 0)|(2 if a.fov else 0)
            if modes and 1<=elapsed<a.seconds-1 and elapsed-last_submit>=.15:
                serial+=1;yaw=math.radians(a.yaw)/2
                payload=struct.pack('<4IQ2I12f',0x53564354,1,80,modes,serial,350,0,
                    *a.offset,0,0,math.sin(yaw),0,math.cos(yaw),
                    *(math.radians(f) for f in (a.fov or (0,0,0,0))))
                status=call_with_payload(process,exports['SpidySubmit'],payload)
                if status:raise RuntimeError(f'View command: {status}')
                last_submit=elapsed
            frame=snapshot(game,exports['SpidyViewData'])
            if frame and frame['sequence']!=last_seq:
                frame['seconds']=elapsed;frames.append(frame);last_seq=frame['sequence']
            time.sleep(.02)
        stopped=call_remote(process,exports['SpidyStop'])==0
        restored=game.read(game.base+0x1899ab0,16)==code
        result=dict(pid=game.pid,module_base=hex(game.base),dll_sha256=digest,
            stopped=stopped,entry_restored=restored,native_stereo_verified=False,
            test=dict(yaw=a.yaw,offset=a.offset,fov=a.fov,rebuild=a.rebuild),samples=frames)
        a.output.parent.mkdir(parents=True,exist_ok=True)
        a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        if frames:
            first,last=frames[0],frames[-1]
            summary={k:last[k]-first[k] for k in ('calls','matched','writes')}
            summary.update(stopped=stopped,entry_restored=restored,thread=last['thread'],
                last_pose=last['after']['pose'],bounds=last['before']['bounds'])
            print(json.dumps(summary,indent=2))
        return 0 if frames and frames[-1]['matched'] and stopped and restored else 1
    finally:
        if process and exports and not stopped:call_remote(process,exports['SpidyStop'])
        if process:close(process)
        game.close()


if __name__=='__main__':
    try:sys.exit(main())
    except (OSError,ValueError,RuntimeError) as e:print(f'View test failed: {e}',file=sys.stderr);sys.exit(2)
