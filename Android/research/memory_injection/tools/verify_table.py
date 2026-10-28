import sys, struct
sys.path.insert(0, '.')
from bb import BB
from elftools.elf.elffile import ELFFile

def load_segments(path):
    f = open(path, 'rb')
    e = ELFFile(f)
    segs = []
    for s in e.iter_segments():
        if s['p_type'] == 'PT_LOAD':
            segs.append((s['p_vaddr'], s['p_vaddr'] + s['p_filesz'], s['p_offset']))
    return f, segs

def read_vaddr(f, segs, va, n):
    for a, b, off in segs:
        if a <= va and va + n <= b:
            f.seek(off + (va - a))
            return f.read(n)
    return None

def word_at(f, segs, va):
    d = read_vaddr(f, segs, va, 4)
    return struct.unpack('<I', d)[0] if d else None

if __name__ == '__main__':
    patch = sys.argv[1]
    target = sys.argv[2]
    table = int(sys.argv[3], 16)
    b = BB(patch)
    f, segs = load_segments(target)
    ok = bad = 0
    for i in range(96):
        e = table + i * 0x20
        rva, orig, new, pad = struct.unpack('<IIII', b.read(e, 16))
        if rva == 0: break
        cur = word_at(f, segs, rva)
        if cur is None:
            print(i, hex(rva), 'OUT OF RANGE'); continue
        st = 'MATCH' if cur == orig else ('ALREADY' if cur == new else 'MISMATCH')
        ok += st == 'MATCH'; bad += st == 'MISMATCH'
        if st != 'MATCH':
            print('%-3d 0x%-8x orig=0x%08x new=0x%08x GOT=0x%08x %s' % (i, rva, orig, new, cur, st))
    print('MATCH', ok, 'MISMATCH', bad)
