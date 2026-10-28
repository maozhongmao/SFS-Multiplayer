#!/usr/bin/env python3
# dis3.py -- capstone + script.json symbolizer with proper VMA->file mapping
import sys, json, struct, bisect

import os
SO = os.environ.get("SO", "/mnt/d/SFS-Multiplayer/android/work/build/work/libil2cpp.so")
SJ = "/mnt/d/SFS-Multiplayer/android/dump/script.json"
_syms=None; _order=None; _b=None

def B():
    global _b
    if _b is None: _b=open(SO,"rb").read()
    return _b

def load_syms():
    global _syms,_order
    if _syms is None:
        d=json.load(open(SJ,encoding="utf-8"))
        m={}
        for e in d.get("ScriptMethod",[]):
            m.setdefault(int(e["Address"]), e["Name"])
        _syms=m; _order=sorted(m)
    return _syms,_order

def nm(addr):
    m,o=load_syms()
    if addr in m: return demangle(m[addr])
    i=bisect.bisect_right(o,addr)-1
    if i>=0 and addr-o[i]<0x800: return demangle(m[o[i]])+"+0x%X"%(addr-o[i])
    return None

def demangle(n):
    return n.replace("$$","::").replace("___",".")

def secs():
    b=B()
    e_shoff=struct.unpack_from("<Q",b,0x28)[0]
    ent=struct.unpack_from("<H",b,0x3A)[0]
    num=struct.unpack_from("<H",b,0x3C)[0]
    si=struct.unpack_from("<H",b,0x3E)[0]
    def sh(i): return struct.unpack_from("<IIQQQQIIQQ",b,e_shoff+i*ent)
    stroff=sh(si)[4]
    out=[]
    for i in range(num):
        s=sh(i)
        e=b.find(b"\0",stroff+s[0]); name=b[stroff+s[0]:e].decode()
        out.append(dict(i=i,name=name,addr=s[3],off=s[4],size=s[5],flags=s[2]))
    return out

def foff(vma):
    for s in secs():
        if s["size"] and s["addr"]<=vma<s["addr"]+s["size"] and s["name"]!=".bss":
            return s["off"]+(vma-s["addr"])
    return vma

def dis(rva,n):
    from capstone import Cs,CS_ARCH_ARM64,CS_MODE_ARM
    off=foff(rva)
    code=B()[off:off+n*4+64]
    md=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
    cnt=0
    for ins in md.disasm(code,rva):
        note=""
        if ins.mnemonic in ("bl","b") and ins.op_str.startswith("#"):
            t=int(ins.op_str[1:],16); x=nm(t)
            note="   ; "+(x if x else "->0x%X"%t)
        elif ins.mnemonic in ("blr","br"):
            note="   ; indirect"
        print("0x%X: %-8s %s%s"%(ins.address,ins.mnemonic,ins.op_str,note))
        cnt+=1
        if cnt>=n: break
        if ins.mnemonic=="ret": break

def xref(target):
    b=B()
    tgt=target
    found=[]
    for s in secs():
        if not (s["flags"] & 0x4): continue
        lo,sz=s["addr"],s["size"]
        if sz==0: continue
        for vma in range(lo,lo+sz,4):
            off=foff(vma)
            w=struct.unpack_from("<I",b,off)[0]
            if (w & 0xFC000000)==0x94000000:
                imm=w & 0x03FFFFFF
                if imm & 0x02000000: imm-=0x04000000
                if vma+imm*4==tgt: found.append(("BL",vma))
            elif (w & 0x7C000000)==0x14000000:
                imm=w & 0x03FFFFFF
                if imm & 0x02000000: imm-=0x04000000
                if vma+imm*4==tgt: found.append(("B",vma))
    for k,o in found:
        print("%s @0x%X  in %s"%(k,o,nm(o) or "?"))
    print("xrefs:",len(found))

if __name__=="__main__":
    cmd=sys.argv[1]
    if cmd=="dis":
        dis(int(sys.argv[2],0), int(sys.argv[3]) if len(sys.argv)>3 else 60)
    elif cmd=="xref": xref(int(sys.argv[2],0))
    elif cmd=="sym": print(nm(int(sys.argv[2],0)))
    elif cmd=="sec": [print(s) for s in secs()]