#!/usr/bin/env python3
import sys, struct
sys.path.insert(0,'/mnt/d/SFS-Multiplayer/android/research/input_fix')
import dis3
b=dis3.B()
targets=[int(x,0) for x in sys.argv[1:]]
hits={t:[] for t in targets}
for s in dis3.secs():
    if not (s["flags"] & 0x4): continue
    lo=s["addr"]; sz=s["size"]
    if sz==0: continue
    off0=s["off"]
    for i in range(0,sz,4):
        b4=b[off0+i:off0+i+4]
        if len(b4)<4: break
        w=struct.unpack("<I",b4)[0]
        vma=lo+i
        if (w & 0xFC000000)==0x94000000:
            imm=w & 0x03FFFFFF
            if imm & 0x02000000: imm-=0x04000000
            t=vma+imm*4
            if t in hits: hits[t].append(("BL",vma))
        elif (w & 0x7C000000)==0x14000000:
            imm=w & 0x03FFFFFF
            if imm & 0x02000000: imm-=0x04000000
            t=vma+imm*4
            if t in hits: hits[t].append(("B",vma))
for t in targets:
    print("== xref to 0x%X"%t)
    for k,o in hits[t]: print("  %s @0x%X in %s"%(k,o,dis3.nm(o) or "?"))
    if not hits[t]: print("  (none)")
