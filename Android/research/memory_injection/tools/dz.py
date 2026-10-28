import sys
sys.path.insert(0, '.')
from binf import B
from elftools.elf.relocation import RelocationSection

b = B(sys.argv[1] if len(sys.argv) > 1 else 'D:/SFS-Multiplayer/android/work/native/libpatch.so')

def pltmap():
    m = {}
    k = 0
    for sec in b.e.iter_sections():
        if isinstance(sec, RelocationSection) and sec.name == '.rela.plt':
            for r in sec.iter_relocations():
                k += 1
                sym = b.e.get_section(sec['sh_link']).get_symbol(r['r_info_sym'])
                m[0x1cd4b0 + 0x20 + (k - 1) * 16] = sym.name
    return m

PLT = pltmap()

def s(a, maxlen=300):
    d = b.read(a, maxlen)
    if not d: return None
    return d.split(b'\x00')[0].decode('utf8', 'replace')

def dis(a, n):
    out = []
    for i in b.md.disasm(b.read(a, n), a):
        line = '0x%x  %-8s %s' % (i.address, i.mnemonic, i.op_str)
        note = []
        if i.address in PLT: note.append('-> ' + PLT[i.address])
        if i.mnemonic in ('bl', 'b') and i.operands and i.operands[0].type == 2:
            t = i.operands[0].imm
            sn = b.sym_at(t)
            if sn: note.append(sn)
        # adrp/add pair handled by caller
        if note: line = line.ljust(52) + ' ; ' + ' '.join(note)
        out.append(line)
    return '\n'.join(out)

def adrp_target(addr):
    """if instr at addr is adrp, return (reg, page)"""
    for i in b.md.disasm(b.read(addr, 4), addr):
        if i.mnemonic == 'adrp':
            return i.operands[0].reg, i.operands[1].imm
    return None

def strref(addr):
    """decode adrp+add at addr -> string"""
    for i in b.md.disasm(b.read(addr, 8), addr):
        pass
    ins = list(b.md.disasm(b.read(addr, 8), addr))
    if len(ins) == 2 and ins[0].mnemonic == 'adrp' and ins[1].mnemonic == 'add':
        base = ins[0].operands[1].imm
        off = ins[1].operands[2].imm
        return s(base + off)
    return None

if __name__ == '__main__':
    if len(sys.argv) > 2 and sys.argv[2] == 'plt':
        for a, n in sorted(PLT.items()):
            print(hex(a), n)
    else:
        base = int(sys.argv[2], 16); n = int(sys.argv[3], 16)
        print(dis(base, n))
