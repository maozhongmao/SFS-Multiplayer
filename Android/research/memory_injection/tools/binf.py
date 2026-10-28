import sys, struct
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN

class B:
    def __init__(self, path):
        self.f = open(path, 'rb'); self.e = ELFFile(self.f); self.path = path
        self.md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
        self.md.detail = True
        self.secs = []
        for s in self.e.iter_sections():
            self.secs.append((s['sh_addr'], s['sh_addr'] + s['sh_size'], s['sh_offset'], s.name))
        self.syms = []
        for s in self.e.iter_sections():
            if s.name == '.dynsym':
                for sym in s.iter_symbols():
                    if sym['st_value']:
                        self.syms.append((sym['st_value'], sym.name, sym['st_size']))
        self.syms.sort()

    def read(self, addr, n):
        for a, b, off, name in self.secs:
            if a <= addr and addr + n <= b:
                self.f.seek(off + (addr - a)); return self.f.read(n)
        return b''

    def sec_of(self, addr):
        for a, b, off, name in self.secs:
            if a <= addr < b: return name
        return None

    def sym_at(self, addr):
        best = None
        for v, n, sz in self.syms:
            if v <= addr and (best is None or v > best[0]): best = (v, n, sz)
        if best and addr - best[0] < max(best[2], 4):
            return '%s+0x%x' % (best[1], addr - best[0])
        return None

    def dis(self, addr, n, resolve=True):
        data = self.read(addr, n)
        out = []
        for i in self.md.disasm(data, addr):
            line = '0x%x:\t%s\t%s' % (i.address, i.mnemonic, i.op_str)
            if resolve:
                for op in i.operands:
                    if op.type == 2 and op.imm:  # IMM
                        t = op.imm
                        sn = self.sym_at(t) if t else None
                        if sn: line += '   ; ' + sn
                    if op.type == 3 and op.mem.base == 0 and op.mem.disp:  # PC-relative-ish
                        pass
            out.append(line)
        return '\n'.join(out)

    def xrefs_to(self, target, limit=50):
        """scan .text for ADRP/ADD or ADRP/LDR pairs and BL to target"""
        res = []
        for a, b, off, name in self.secs:
            if name not in ('.text',): continue
            data = self.f.read() if False else None
            self.f.seek(off); blob = self.f.read(b - a)
            for i in self.md.disasm(blob, a):
                if i.mnemonic in ('bl', 'b') and i.operands and i.operands[0].type == 2:
                    if i.operands[0].imm == target:
                        res.append((i.address, self.sym_at(i.address), i.mnemonic))
                if len(res) >= limit: return res
        return res
