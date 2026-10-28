import sys, re
sys.path.insert(0, '.')
from binf import B

p = sys.argv[1]
b = B(p)
allstr = []
# whole file scan for printable runs
for a, e, off, name in b.secs:
    if name in ('.bss', '.comment', '.shstrtab'): continue
    b.f.seek(off); data = b.f.read(e - a)
    cur = []
    start = 0
    for i, ch in enumerate(data):
        if 32 <= ch < 127:
            if not cur: start = i
            cur.append(chr(ch))
        else:
            if len(cur) >= 4: allstr.append((a + start, ''.join(cur)))
            cur = []
    if len(cur) >= 4: allstr.append((a + start, ''.join(cur)))

with open('strings_all.txt', 'w', encoding='utf8') as f:
    for a, s in allstr:
        f.write('%x\t%s\n' % (a, s))
print('total', len(allstr))
