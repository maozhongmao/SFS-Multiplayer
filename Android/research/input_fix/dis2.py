#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""dis2.py -- capstone + script.json symbolizer for SFS 1.6.00.22 libil2cpp.so
usage: python dis2.py 0xRVA [count]
RVA == file offset in this build (.text VA 0x13BD4D0, il2cpp seg VA 0x166435C)
"""
import sys, json, bisect
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

SO = r"D:/SFS-Multiplayer/android/work/native/libil2cpp.so"
SJ = r"D:/SFS-Multiplayer/android/dump/script.json"
_syms = None

def syms():
    global _syms
    if _syms is None:
        d = json.load(open(SJ, encoding="utf-8"))
        m = {}
        for e in d["ScriptMethod"]:
            m.setdefault(e["Address"], e["Name"])
        _syms = m
    return _syms

def demangle(n):
    # SFS_Foo_Bar___method -> SFS.Foo.Bar::method
    n = n.replace("$$", "::").replace("___", ".")
    return n

def run(rva, count=80, out=None):
    f = open(SO, "rb")
    f.seek(rva)
    code = f.read(count * 4 + 16)
    f.close()
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = True
    S = syms()
    lines = []
    for ins in md.disasm(code, rva):
        s = "0x%X  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str)
        note = ""
        if ins.mnemonic in ("bl", "b", "br", "blr") and ins.op_str.startswith("#"):
            try:
                t = int(ins.op_str[1:], 16)
                if t in S:
                    note = "   ; " + demangle(S[t])
                elif ins.mnemonic in ("bl", "b"):
                    note = "   ; ->0x%X" % t
            except Exception:
                pass
        lines.append(s + note)
        if len(lines) >= count:
            break
        if ins.mnemonic in ("ret",) and len(lines) > 2:
            break
    txt = "\n".join(lines)
    if out:
        print(txt)
    return txt

if __name__ == "__main__":
    rva = int(sys.argv[1], 0)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 80
    run(rva, n)
