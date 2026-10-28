from capstone import *
import struct
raw=open('D:/SFS-Pro/build/seller_libpatch.so','rb').read()
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM); md.detail=True
_ins=list(md.disasm(raw[0xb39a0:0x1ef070],0xb39a0))
_addr=[i.address for i in _ins]
def fnstart(a):
    import bisect
    i=bisect.bisect_right(_addr,a)-1
    # walk back to a prologue: 'sub sp, sp, #imm' or 'stp x29, x30, [sp, #-imm]!'
    for j in range(i,-1,-1):
        s=_ins[j].op_str; m=_ins[j].mnemonic
        if m=='ret' or m=='b' and False: pass
        if (m=='stp' and s.startswith('x29, x30, [sp, #-')) or (m=='sub' and s.startswith('sp, sp, #')):
            # confirm next insn is another stp or add x29
            if j+1<len(_ins) and (_ins[j+1].mnemonic in ('stp','str','mov','add','sub','mrs')):
                return _ins[j].address
    return None
if __name__=='__main__':
    import sys
    for a in sys.argv[1:]:
        a=int(a,16); print(hex(a),'fn',hex(fnstart(a) or 0))
