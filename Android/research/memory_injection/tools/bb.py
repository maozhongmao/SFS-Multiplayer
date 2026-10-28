import sys, struct
sys.path.insert(0, '.')
from binf import B
from elftools.elf.relocation import RelocationSection

class BB(B):
    def __init__(self, p):
        B.__init__(self, p)
        self.rel = {}
        for sec in self.e.iter_sections():
            if isinstance(sec, RelocationSection):
                for r in sec.iter_relocations():
                    self.rel[r['r_offset']] = (r['r_info_type'], r['r_addend'])

    def ptr(self, addr):
        """resolve a pointer stored at addr, applying RELATIVE relocs"""
        if addr in self.rel:
            t, add = self.rel[addr]
            if t == 1027:  # R_AARCH64_RELATIVE
                return add
        return struct.unpack('<Q', self.read(addr, 8))[0]

    def s(self, addr, n=160):
        if not addr: return ''
        d = self.read(addr, n)
        return d.split(b'\x00')[0].decode('utf8', 'replace')
