import sys
sys.path.insert(0, '.')
from binf import B
from dz import PLT, s

b = B(sys.argv[1] if len(sys.argv) > 1 else 'D:/SFS-Multiplayer/android/work/native/libpatch.so')

def dump(addr, n, show_hex=False):
    data = b.read(addr, n)
    ins = list(b.md.disasm(data, addr))
    pages = {}
    outs = []
    for i in ins:
        if i.mnemonic == 'adrp':
            pages[i.operands[0].reg] = i.operands[1].imm
        note = ''
        # resolve add xN, xM, #imm where xM is a page reg
        if i.mnemonic == 'add' and len(i.operands) == 3 and i.operands[1].type == 1:
            base = i.operands[1].reg
            if base in pages:
                tgt = pages[base] + i.operands[2].imm
                st = s(tgt)
                if st is not None and all(32 <= ord(c) < 127 or c in '\t\n' for c in st) and len(st) >= 2:
                    note = repr(st)
                else:
                    note = 'ADDR 0x%x' % tgt
        if i.mnemonic == 'adr':
            tgt = i.operands[1].imm
            st = s(tgt)
            if st and all(32 <= ord(c) < 127 for c in st) and len(st) >= 2:
                note = repr(st)
        if i.mnemonic in ('bl', 'b') and i.operands and i.operands[0].type == 2:
            t = i.operands[0].imm
            if t in PLT: note = '-> ' + PLT[t]
            else:
                sn = b.sym_at(t)
                if sn: note = sn
        line = '0x%x  %-8s %-40s' % (i.address, i.mnemonic, i.op_str)
        if note: line += ' ; ' + note
        outs.append(line)
    return '\n'.join(outs)

if __name__ == '__main__':
    dump(int(sys.argv[2], 16), int(sys.argv[3], 16))
