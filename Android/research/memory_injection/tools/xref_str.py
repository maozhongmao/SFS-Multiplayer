import sys
sys.path.insert(0, '.')
from binf import B

b = B(sys.argv[1])
target = int(sys.argv[2], 16)

def scan():
    hits = []
    for a, e, off, name in b.secs:
        if name != '.text': continue
        b.f.seek(off); data = b.f.read(e - a)
        ins = list(b.md.disasm(data, a))
        pages = {}
        for i in ins:
            if i.mnemonic == 'adrp':
                pages[i.operands[0].reg] = i.operands[1].imm
            elif i.mnemonic == 'add' and len(i.operands) == 3 and i.operands[1].type == 1:
                base = i.operands[1].reg
                if base in pages:
                    t = pages[base] + i.operands[2].imm
                    if t == target:
                        hits.append(i.address)
            elif i.mnemonic == 'adr':
                if i.operands[1].imm == target:
                    hits.append(i.address)
    return hits

for h in scan():
    print(hex(h), b.sym_at(h))
