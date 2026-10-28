import struct, pickle, sys
from capstone import *
from capstone.arm64 import *
F='D:/SFS-Pro/build/seller_libpatch.so'
raw=open(F,'rb').read()
strs=pickle.load(open('D:/SFS-Pro/build/libpatch_strings.pkl','rb'))
S={}
for o,s in strs:
    va=o if o<0x1eff70 else o+0x1000
    S[va]=s
# exports
dyn={}
e_phoff=struct.unpack_from('<Q',raw,0x20)[0]
# section based dynsym
e_shoff=struct.unpack_from('<Q',raw,0x28)[0]; e_shentsize=struct.unpack_from('<H',raw,0x3a)[0]; e_shnum=struct.unpack_from('<H',raw,0x3c)[0]
secs=[struct.unpack_from('<IIQQQQIIQQ',raw,e_shoff+i*e_shentsize) for i in range(e_shnum)]
dynsym=secs[3]; dynstr=secs[9]
n=dynsym[5]//24
for i in range(n):
    nmo,info,other,shndx,val,size=struct.unpack_from('<IBBHQQ',raw,dynsym[4]+i*24)
    nm=raw[dynstr[4]+nmo:].split(b'\0')[0].decode('utf8','replace')
    if val and nm: dyn.setdefault(val,[]).append(nm)
# plt map: .rela.plt gives got slot -> sym
plt={}
rela=secs[11]
for i in range(rela[5]//24):
    off,info,add=struct.unpack_from('<QQq',raw,rela[4]+i*24)
    symi=info>>32
    nmo=struct.unpack_from('<I',raw,dynsym[4]+symi*24)[0]
    nm=raw[dynstr[4]+nmo:].split(b'\0')[0].decode('utf8','replace')
    plt[off]=nm
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM); md.detail=True
def annot(ins):
    extra=''
    # find adrp+add pairs inside this insn
    if ins.id==ARM64_INS_ADD and len(ins.operands)==3 and ins.operands[2].type==ARM64_OP_IMM:
        pass
    return extra
def dump(lo,hi,ctx=0x20):
    reg={}
    # prime: scan a bit before for adrp
    out=[]
    for ins in md.disasm(raw[max(0,lo-0x40):hi], max(0,lo-0x40)):
        tag=''
        if ins.address<lo-ctx: continue
        if ins.id==ARM64_INS_ADRP:
            reg[ins.operands[0].reg]=ins.operands[1].imm
        elif ins.id==ARM64_INS_ADD and len(ins.operands)==3 and ins.operands[1].type==ARM64_OP_REG and ins.operands[2].type==ARM64_OP_IMM:
            b=reg.get(ins.operands[1].reg)
            if b is not None:
                v=b+ins.operands[2].imm
                reg[ins.operands[0].reg]=v
                if v in S: tag='  ; "%s"'%S[v][:110].replace('\n','\n')
        elif ins.id==ARM64_INS_MOV and len(ins.operands)==2 and ins.operands[1].type==ARM64_OP_REG and ins.operands[1].reg in reg:
            reg[ins.operands[0].reg]=reg[ins.operands[1].reg]
        elif ins.id==ARM64_INS_BL and ins.operands[0].type==ARM64_OP_IMM:
            t=ins.operands[0].imm
            if t in dyn: tag='  ; %s'%','.join(dyn[t][:2])
            elif t in plt: tag='  ; PLT %s'%plt[t]
        elif ins.id==ARM64_INS_ADR:
            v=ins.operands[1].imm
            if v in S: tag='  ; "%s"'%S[v][:110]
        print('%06x  %-8s %s%s'%(ins.address,ins.mnemonic,ins.op_str,tag))
if __name__=='__main__':
    lo=int(sys.argv[1],16); hi=int(sys.argv[2],16)
    dump(lo,hi)
