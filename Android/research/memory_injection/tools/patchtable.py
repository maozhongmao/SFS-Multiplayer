import sys, struct
sys.path.insert(0, '.')
from binf import B

b = B('D:/SFS-Multiplayer/android/work/native/libpatch.so')
lib = open('D:/SFS-Multiplayer/android/work/native/libil2cpp.so', 'rb')
libsz = 0
import os
libsz = os.path.getsize('D:/SFS-Multiplayer/android/work/native/libil2cpp.so')

def s(a):
    if a == 0: return ''
    d = b.read(a, 200)
    return d.split(b'\x00')[0].decode('utf8', 'replace')

base = 0x1d0828
print('%-4s %-10s %-10s %-10s %-40s %s' % ('#', 'RVA', 'orig', 'new', 'name', 'note'))
for i in range(80):
    e = base + i * 0x20
    rva, orig, new, pad = struct.unpack('<IIII', b.read(e, 16))
    namep, notep = struct.unpack('<QQ', b.read(e + 16, 16))
    if rva == 0 and orig == 0:
        continue
    # read libil2cpp bytes at rva
    if 0 < rva < libsz:
        lib.seek(rva); cur = struct.unpack('<I', lib.read(4))[0]
    else:
        cur = -1
    print('%-4d 0x%-8x 0x%-8x 0x%-8x %-40s %s   [cur=0x%08x %s]' % (
        i, rva, orig, new, s(namep)[:38], s(notep)[:40], cur & 0xffffffff,
        'MATCH' if cur == orig else ('ALREADY' if cur == new else 'MISMATCH')))
