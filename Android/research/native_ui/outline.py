from capstone import *
from capstone.arm64 import *
import pickle, sys
raw=open('D:/SFS-Pro/build/seller_libpatch.so','rb').read()
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM); md.detail=True
U=pickle.load(open('D:/SFS-Pro/build/research/native_ui/strings_utf8.pkl','rb'))
S=dict((o,s) for o,s in U)
HELP={0x155ed4:'clone(holder,tmpl)',0xeb8f0:'set_name',0x1033c0:'set_label',
      0x13e6f8:'add_sas_row',0x139b34:'log',0xb6e8c:'snprintf',0x13797c:'read_int_file',
      0x17e3dc:'make_delegates',0x1537f8:'get_input(row)',0x103914:'?103914',
      0xe8f58:'is_alive?',0xeac6c:'find_child(name)',0x1552d4:'set_text',
      0xeb8f0:'set_name',0xeba08:'find_by_name'}
def outline(lo,hi):
    reg={}; pend=[]
    for ins in md.disasm(raw[lo:hi],lo):
        if ins.id==ARM64_INS_ADRP: reg[ins.operands[0].reg]=ins.operands[1].imm
        elif ins.id==ARM64_INS_ADD and len(ins.operands)==3 and ins.operands[1].type==ARM64_OP_REG and ins.operands[2].type==ARM64_OP_IMM:
            b=reg.get(ins.operands[1].reg)
            if b is not None:
                v=b+ins.operands[2].imm; reg[ins.operands[0].reg]=v
                if v in S: pend.append(S[v])
        elif ins.id==ARM64_INS_MOV and len(ins.operands)==2 and ins.operands[1].type==ARM64_OP_IMM:
            pass
        elif ins.id==ARM64_INS_BL and ins.operands[0].type==ARM64_OP_IMM:
            t=ins.operands[0].imm
            if t in HELP:
                print('%06x  %-18s %s'%(ins.address,HELP[t],' | '.join(repr(x)[:60] for x in pend[-2:])))
            pend=pend[-4:]
        elif ins.id in (ARM64_INS_B,ARM64_INS_BLR,ARM64_INS_RET):
            pass
if __name__=='__main__':
    outline(int(sys.argv[1],16),int(sys.argv[2],16))
