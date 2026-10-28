#!/usr/bin/env python3
# dis4.py -- fast raw disassembler, no script.json (VMA->file via ELF sections)
import sys, struct, os
SO = os.environ.get("SO", "/mnt/d/SFS-Multiplayer/android/work/build/work/libil2cpp.so")
_b = open(SO, "rb").read()
def secs():
    e_shoff = struct.unpack_from("<Q", _b, 0x28)[0]
    ent = struct.unpack_from("<H", _b, 0x3A)[0]
    num = struct.unpack_from("<H", _b, 0x3C)[0]
    si = struct.unpack_from("<H", _b, 0x3E)[0]
    def sh(i): return struct.unpack_from("<IIQQQQIIQQ", _b, e_shoff + i * ent)
    stroff = sh(si)[4]
    out = []
    for i in range(num):
        s = sh(i)
        e = _b.find(b"\0", stroff + s[0])
        name = _b[stroff + s[0]:e].decode()
        out.append((name, s[3], s[4], s[5], s[2]))
    return out
SEC = secs()
def foff(vma):
    for name, addr, off, size, flags in SEC:
        if size and addr <= vma < addr + size and name != ".bss":
            return off + (vma - addr)
    return vma
def dis(rva, n):
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
    off = foff(rva)
    code = _b[off:off + n * 4 + 64]
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    cnt = 0
    for ins in md.disasm(code, rva):
        ops = ins.op_str
        note = ""
        if ins.mnemonic in ("bl", "b", "cbz", "cbnz", "tbz", "tbnz", "adr", "adrp") and "#" in ops:
            try:
                parts = ops.split("#")
                t = int(parts[1].split(",")[0], 16)
                if ins.mnemonic == "adrp":
                    t = int(parts[1], 16)
                note = "  ->0x%X" % t
            except Exception:
                pass
        print("0x%X: %-8s %s%s" % (ins.address, ins.mnemonic, ops, note))
        cnt += 1
        if cnt >= n: break
        if ins.mnemonic == "ret": break
if __name__ == "__main__":
    dis(int(sys.argv[1], 0), int(sys.argv[2]) if len(sys.argv) > 2 else 60)
