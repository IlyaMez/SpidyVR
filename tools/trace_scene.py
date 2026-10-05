"""Trace native scene scheduling while reusing retained diagnostic eye views."""
import argparse
import ctypes as c
import json
import pathlib
import struct
import time
from capture_game_state import Game,find_game,open_process,close
from bridge_game import ROOT,prepare
from observe_game import call_remote,call_with_payload
from probe_stereo import snapshot as stereo_snapshot


def snapshot(game,address):
    for _ in range(8):
        raw=game.read(address,279592);seq=game.read(address+16,8)
        if len(raw)!=279592 or len(seq)!=8:return None
        if struct.unpack_from('<3I',raw)!=(0x53545243,2,279592):raise RuntimeError('Scene trace protocol mismatch')
        sequence=struct.unpack_from('<Q',raw,16)[0]
        if sequence&1 or sequence!=struct.unpack('<Q',seq)[0]:continue
        loops,count,error=struct.unpack_from('<Q2I',raw,24)
        if count>32:raise RuntimeError('Invalid trace count')
        result=dict(loops=loops,error=error,samples=[])
        for i in range(count):
            start=40+i*8736
            s=dict(zip(('view','owner','count','caller','kind','flags','active','thread'),struct.unpack_from('<4Q4I',raw,start)))
            s['pose']=struct.unpack_from('<16f',raw,start+48)
            s['view_bytes']=raw[start+112:start+112+0x1f70].hex()
            s['tone_settings']=raw[start+112+0x1f70:start+8736].hex()
            result['samples'].append(s)
        return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('stereo',type=pathlib.Path)
    p.add_argument('--output',type=pathlib.Path,default=ROOT/'reports/scene-trace.json');args=p.parse_args()
    old=json.loads(args.stereo.read_text());game=Game(find_game());process=None;exports=None;views=None
    stopped=False;views_stopped=True
    try:
        if old['pid']!=game.pid or int(old['module_base'],16)!=game.base:raise RuntimeError('Stale process report')
        process=open_process(0x0400|0x0010|0x0020|0x0008|0x0002,False,game.pid)
        if not process:raise c.WinError(c.get_last_error())
        exports,digest=prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_scene_trace.dll',ROOT/'reports/scene-trace-modules',
            ('SpidyStart','SpidyStop','SpidyTraceData'))
        config=struct.pack('<4I3Q',0x53544346,1,40,game.pid,game.base,*(e['view'] for e in old['final']['eyes']))
        code=call_with_payload(process,exports['SpidyStart'],config)
        if code:raise RuntimeError(f'Trace start: {code}')
        views,_=prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_stereo_probe.dll',ROOT/'reports/stereo-modules',
            ('SpidyStart','SpidyStop','SpidyStereoData'))
        size=old['final']['eyes'][0]['width']
        flags=1|(2 if old.get('native_readback') else 0)|(4 if old.get('display_pass') else 0)
        code=call_with_payload(process,views['SpidyStart'],struct.pack('<4IQ4I',0x53534346,1,40,game.pid,game.base,3000,flags,size,size))
        if code:raise RuntimeError(f'View start: {code}')
        views_stopped=False;samples=[];begin=time.monotonic()
        while time.monotonic()-begin<4:
            samples.append(dict(seconds=time.monotonic()-begin,stereo=stereo_snapshot(game,views['SpidyStereoData'])))
            time.sleep(.25)
        views_stopped=call_remote(process,views['SpidyStop'])==0
        stopped=call_remote(process,exports['SpidyStop'])==0
        final=snapshot(game,exports['SpidyTraceData'])
        result=dict(pid=game.pid,base=hex(game.base),sha256=digest,stopped=stopped,views_stopped=views_stopped,samples=samples,final=final)
        args.output.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(dict(stopped=stopped,views_stopped=views_stopped,loops=final['loops'],error=final['error'],
            samples=[{k:v for k,v in s.items() if k not in ('view_bytes','tone_settings','pose')} for s in final['samples']]),indent=2))
    finally:
        if views and not views_stopped:call_remote(process,views['SpidyStop'])
        if exports and not stopped:call_remote(process,exports['SpidyStop'])
        if process:close(process)
        game.close()


if __name__=='__main__':main()
