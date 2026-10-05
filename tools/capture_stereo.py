"""Observe eye texture barriers, reactivate native views, and read back retired eyes."""
import argparse
import ctypes as c
import json
import pathlib
import struct
import sys
import time
import zlib
from capture_game_state import Game, find_game, open_process, close
from bridge_game import ROOT, prepare
from observe_game import call_remote, call_with_payload
from probe_stereo import snapshot as stereo_snapshot


def snapshot(game, address):
    for _ in range(8):
        raw=game.read(address,1112)
        seq=game.read(address+16,8)
        if len(raw)!=1112 or len(seq)!=8:return None
        if struct.unpack_from('<3I',raw)!=(0x53454350,4,1112):raise RuntimeError('Eye capture protocol mismatch')
        sequence=struct.unpack_from('<Q',raw,16)[0]
        if sequence&1 or sequence!=struct.unpack('<Q',seq)[0]:continue
        data=dict(zip(('executes','barriers','error','captured'),struct.unpack_from('<2Q2I',raw,24)))
        data.update(sequence=sequence,status=struct.unpack_from('<I',raw,12)[0],eyes=[])
        for i in range(2):
            data['eyes'].append(dict(zip(('resource','transitions','pixels','state','known','width','height','bytes','flags'),
                struct.unpack_from('<3Q6I',raw,48+48*i))))
        data.update(zip(('raw_barriers','matched_barriers','own_barrier','own_reset'),struct.unpack_from('<4Q',raw,144)))
        data['command_types']=[dict(zip(('vtable','barrier','reset','count'),struct.unpack_from('<4Q',raw,176+i*32))) for i in range(8)]
        data['native_passes'],*data['eye_passes']=struct.unpack_from('<3Q',raw,432)
        data['copies'],data['matched_copies']=struct.unpack_from('<2Q',raw,456)
        data['candidates']=[dict(zip(('resource','identity','count','width','height','format','state'),
            struct.unpack_from('<3Q4I',raw,472+i*40))) for i in range(16)]
        return data
    return None


def write_png(path,width,height,pixels):
    def chunk(name,payload):
        return struct.pack('>I',len(payload))+name+payload+struct.pack('>I',zlib.crc32(name+payload))
    rows=b''.join(b'\0'+pixels[y*width*4:(y+1)*width*4] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',width,height,8,6,0,0,0))+
                     chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stereo',type=pathlib.Path,required=True)
    p.add_argument('--render',type=pathlib.Path,required=True)
    p.add_argument('--output',type=pathlib.Path,default=ROOT/'reports/eye-capture.json')
    a=p.parse_args()
    stereo=json.loads(a.stereo.read_text());render=json.loads(a.render.read_text())
    game=Game(find_game());process=None;exports=None;views=None;stopped=False;views_stopped=True
    result={}
    try:
        for source in (stereo,render):
            if source['pid']!=game.pid or int(source['module_base'],16)!=game.base:raise RuntimeError('Stale process report')
        queues={int(x['object'],16) for x in render['samples'] if x['kind']==2}
        if len(queues)!=1:raise RuntimeError('Exactly one game direct queue required')
        resources=[]
        for e in stereo['final']['eyes']:
            obj=game.pointer(game.pointer(e['view']+0x1f70)+0x40)
            if game.pointer(obj)!=game.base+0x4e56c80:raise RuntimeError('Unexpected eye texture wrapper')
            resources.append(game.pointer(game.pointer(obj+0x38)))
        process=open_process(0x0400|0x0010|0x0020|0x0008|0x0002,False,game.pid)
        if not process:raise c.WinError(c.get_last_error())
        exports,digest=prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_eye_capture.dll',
            ROOT/'reports/eye-capture-modules',('SpidyStart','SpidyStop','SpidyCapture','SpidyEyeData'))
        config=struct.pack('<4I6Q',0x53454346,1,64,game.pid,game.base,next(iter(queues)),
            *(e['view'] for e in stereo['final']['eyes']),*resources)
        code=call_with_payload(process,exports['SpidyStart'],config)
        if code:raise RuntimeError(f'Eye capture start: {code}')
        print('Eye-resource barrier tracking active.',flush=True)
        views,_=prepare(game.pid,process,ROOT/'build/windows-ninja/spidy_stereo_probe.dll',
            ROOT/'reports/stereo-modules',('SpidyStart','SpidyStop','SpidyStereoData'))
        size=stereo['final']['eyes'][0]['width']
        flags=1|(2 if stereo.get('native_readback') else 0)|(4 if stereo.get('display_pass') else 0)
        code=call_with_payload(process,views['SpidyStart'],struct.pack('<4IQ4I',0x53534346,1,40,game.pid,game.base,3000,flags,size,size))
        if code:raise RuntimeError(f'Stereo start: {code}')
        views_stopped=False
        time.sleep(4)
        code=call_remote(process,views['SpidyStop']);views_stopped=code==0
        if code:raise RuntimeError(f'Stereo stop: {code}')
        result.update(pid=game.pid,module_base=hex(game.base),dll_sha256=digest,
            before_capture=snapshot(game,exports['SpidyEyeData']),stereo=stereo_snapshot(game,views['SpidyStereoData']))
        code=call_remote(process,exports['SpidyCapture']);result['capture_result']=code
        result['final']=snapshot(game,exports['SpidyEyeData'])
        if not code and result['final']['captured']:
            for i,e in enumerate(result['final']['eyes']):
                if e['bytes']!=e['width']*e['height']*4 or e['bytes']>4*1024*1024:raise RuntimeError('Invalid pixel dimensions')
                pixels=game.read(e['pixels'],e['bytes'])
                if len(pixels)!=e['bytes']:raise RuntimeError('Incomplete pixel readback')
                path=a.output.with_name(a.output.stem+f'-{i}.png')
                write_png(path,e['width'],e['height'],pixels)
                e['image']=str(path.resolve());e['unique_rgba']=len(set(struct.iter_unpack('<I',pixels)))
        stopped=call_remote(process,exports['SpidyStop'])==0;result['stopped']=stopped
        a.output.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2))
        return 0 if not code and stopped else 1
    finally:
        if views and not views_stopped:call_remote(process,views['SpidyStop'])
        if exports and not stopped:call_remote(process,exports['SpidyStop'])
        if process:close(process)
        game.close()


if __name__=='__main__':
    sys.exit(main())
