import struct, pickle, sys
from capstone import *
from capstone.arm64 import *
F='D:/SFS-Pro/build/seller_libpatch.so'
raw=open(F,'rb').read()
TEXT_LO, TEXT_HI = 0xb39a0, 0x1ef070   # .text  (off==va in segment 0)
strs=pickle.load(open('D:/SFS-Pro/build/libpatch_strings.pkl','rb'))
def off2va(off): return off if off < 0x1eff70 else off+0x1000
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM); md.detail=True
xrefs={}
reg={}
def flush():
    pass
for ins in md.disasm(raw[TEXT_LO:TEXT_HI], TEXT_LO):
    m=ins.id
    if m==ARM64_INS_ADRP:
        r=ins.operands[0].reg
        reg[r]=(ins.operands[1].imm)&0xffffffffffffffff
    elif m in (ARM64_INS_ADD,ARM64_INS_ADDS,ARM64_INS_SUB):
        ops=ins.operands
        if len(ops)==3 and ops[0].type==ARM64_OP_REG and ops[1].type==ARM64_OP_REG:
            base=reg.get(ops[1].reg)
            if base is not None:
                if ops[2].type==ARM64_OP_IMM:
                    v=base+ops[2].imm if m!=ARM64_INS_SUB else base-ops[2].imm
                    v&=0xffffffffffffffff
                    if m==ARM64_INS_SUB:
                        reg[ops[0].reg]=v
                    else:
                        reg[ops[0].reg]=v
                        xrefs.setdefault(v,[]).append(ins.address)
                elif ops[2].type==ARM64_OP_REG and ops[2].reg in reg:
                    pass
    elif m==ARM64_INS_MOV and len(ins.operands)==2 and ins.operands[1].type==ARM64_OP_REG:
        if ins.operands[1].reg in reg: reg[ins.operands[0].reg]=reg[ins.operands[1].reg]
def hits(name):
    return [(off2va(o),a) for o,s in strs if s==name for a in xrefs.get(off2va(o),[])]
def byaddr(addr):
    best=None
    for va,al in xrefs.items():
        pass
    return None
if __name__=='__main__':
    for n in sys.argv[1:]:
        h=hits(n)
        print(hex(len(h)), n[:50], [hex(a) for _,a in h])
