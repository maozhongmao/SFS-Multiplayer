import sys
sys.path.insert(0, '.')
from binf import B

b = B(sys.argv[1])
target = int(sys.argv[2], 16)

# collect call sites
hits = []
for a, e, off, name in b.secs:
    if name != '.text': continue
    b.f.seek(off); data = b.f.read(e - a)
    for i in b.md.disasm(data, a):
        if i.mnemonic in ('bl', 'b') and i.operands and i.operands[0].type == 2:
            if i.operands[0].imm == target:
                hits.append((i.address, i.mnemonic))

# function start heuristic
starts = []
for a, e, off, name in b.secs:
    if name != '.text': continue
    b.f.seek(off); data = b.f.read(e - a)
    ins = list(b.md.disasm(data, a))
    for i in range(len(ins)):
        m = ins[i].mnemonic
        if m in ('stp', 'sub', 'str', 'bti', 'paciasp') and ('x29, x30, [sp' in ins[i].op_str or 'sp, sp, #' in ins[i].op_str):
            starts.append(ins[i].address)
starts = sorted(set(starts))
import bisect
def fstart(x):
    i = bisect.bisect_right(starts, x) - 1
    return starts[i] if i >= 0 else None

for h, m in hits:
    fs = fstart(h)
    print('%s  at 0x%x  in func 0x%x %s' % (m, h, fs if fs else 0, b.sym_at(fs) or ''))
