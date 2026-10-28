import struct, pickle, sys
from capstone import *
from capstone.arm64 import *
F='D:/SFS-Pro/build/seller_libpatch.so'
raw=open(F,'rb').read()
TEXT_END=0x1eff70
def va2off(va): return va if va<TEXT_END else va-0x1000
def off2va(off): return off if off<TEXT_END else off+0x1000
strs=pickle.load(open('D:/SFS-Pro/build/libpatch_strings.pkl','rb'))
S={o:s for o,s in strs}
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM); md.detail=True
code=raw[:TEXT_END]
# two-pass: collect adrp page regs, then add
xrefs={}  # va -> [addr,...]
def scan(lo,hi):
    adrp_reg={}  # per linear scan; reset conservatively every function-ish
    for ins in md.disasm(code[lo:hi], lo):
        if ins.id==ARM64_INS_ADRP:
            r=ins.operands[0].reg
            imm=ins.operands[1].imm
            adrp_reg[r]=imm
        elif ins.id==ARM64_INS_ADD and len(ins.operands)==3 and ins.operands[1].type==ARM64_OP_REG:
            src=ins.operands[1].reg; imm=ins.operands[2].imm
            if src in adrp_reg and ins.operands[2].type==ARM64_OP_IMM:
                tgt=(adrp_reg[src]+imm)&0xffffffffffffffff
                xrefs.setdefault(tgt,[]).append((ins.address,0))
                # 8-byte aligned reg pair trick: add #N,+8 also
                xrefs.setdefault(tgt+8 if False else tgt,[]).append((ins.address,0))
        # allow "add x, x, #imm" chained (x = x+imm before add)
        elif ins.id==ARM64_INS_ADD and len(ins.operands)==3 and ins.operands[1].type==ARM64_OP_REG and ins.operands[1].reg in adrp_reg and ins.operands[2].type==ARM64_OP_IMM:
            tgt=adrp_reg[ins.operands[1].reg]+ins.operands[2].imm
            xrefs.setdefault(tgt,[]).append((ins.address,0))
    return
scan(0,TEXT_END)
def where(name):
    hits=[(o,s) for o,s in strs if s==name]
    out=[]
    for o,s in hits:
        va=off2va(o)
        for a,_ in xrefs.get(va,[]):
            out.append((hex(va),hex(a)))
    print(name,'->',out)
if __name__=='__main__':
    for n in sys.argv[1:]:
        where(n)
