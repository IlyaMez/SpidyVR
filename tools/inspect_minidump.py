"""Read exception registers and executable-address candidates from a game minidump.

Stack entries are candidates, not an unwound call stack. No process is started.
"""
import argparse
import json
import pathlib
import struct


def inspect(path):
    data=path.read_bytes()
    if data[:4]!=b'MDMP':raise ValueError('Not a minidump')
    count,directory=struct.unpack_from('<II',data,8)
    streams={kind:(offset,size) for kind,size,offset in
             struct.iter_unpack('<III',data[directory:directory+12*count])}
    modules=[]
    if 4 in streams:
        off,size=streams[4];n=struct.unpack_from('<I',data,off)[0]
        for i in range(n):
            start=off+4+108*i
            base,length,checksum,timestamp,name=struct.unpack_from('<Q4I',data,start)
            text_size=struct.unpack_from('<I',data,name)[0]
            title=data[name+4:name+4+text_size].decode('utf-16-le')
            modules.append(dict(base=base,size=length,name=pathlib.PureWindowsPath(title).name))
    def symbol(p):
        for m in modules:
            if m['base']<=p<m['base']+m['size']:return f"{m['name']}+{p-m['base']:#x}"
        return hex(p)
    if 6 not in streams:raise ValueError('No exception stream')
    off,size=streams[6]
    thread=struct.unpack_from('<I',data,off)[0]
    code,flags,record,address,n,_=struct.unpack_from('<IIQQII',data,off+8)
    arguments=struct.unpack_from('<15Q',data,off+40)[:min(n,15)]
    context_size,context=struct.unpack_from('<II',data,off+160)
    registers=dict(zip(('rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15','rip'),
                       struct.unpack_from('<17Q',data,context+120)))
    candidates=[]
    if 3 in streams:
        off,size=streams[3];n=struct.unpack_from('<I',data,off)[0]
        for i in range(n):
            start=off+4+48*i
            if struct.unpack_from('<I',data,start)[0]!=thread:continue
            stack_base,stack_size,stack_rva=struct.unpack_from('<QII',data,start+24)
            delta=max(0,registers['rsp']-stack_base)
            for j in range(delta,min(stack_size,delta+8192)-7,8):
                pointer=struct.unpack_from('<Q',data,stack_rva+j)[0]
                name=symbol(pointer)
                if '+' in name:
                    candidates.append(dict(offset=hex(stack_base+j-registers['rsp']),address=name))
    return dict(thread=thread,exception=hex(code),fault=symbol(address),arguments=[hex(x) for x in arguments],
                registers={k:hex(v) for k,v in registers.items()},stack_candidates=candidates,
                modules=modules,stack_is_unwound=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('dump',type=pathlib.Path)
    p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
    result=inspect(a.dump);a.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='modules'},indent=2))
