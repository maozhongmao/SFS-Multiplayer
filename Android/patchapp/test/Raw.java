// SPDX-License-Identifier: GPL-3.0-or-later
import java.nio.file.*;
public class Raw {
    static int u16(byte[] b,int o){return (b[o]&0xFF)|((b[o+1]&0xFF)<<8);}
    static int u32(byte[] b,int o){return (b[o]&0xFF)|((b[o+1]&0xFF)<<8)|((b[o+2]&0xFF)<<16)|((b[o+3]&0xFF)<<24);}
    public static void main(String[] a) throws Exception {
        byte[] b = Files.readAllBytes(Paths.get(a[0]));
        System.out.println("len=" + b.length + " magic=" + u16(b,0) + " hsize=" + u16(b,2) + " fsize=" + u32(b,4));
        int off = 8, n = 0;
        while (off + 8 <= b.length && n < 8) {
            int t2 = u16(b, off), sz = u32(b, off + 4);
            System.out.println("chunk@" + off + " type=0x" + Integer.toHexString(t2) + " size=" + sz);
            if (t2 == 1) System.out.println("   count=" + u32(b, off+8) + " styleCount=" + u32(b, off+12) + " flags=0x" + Integer.toHexString(u32(b, off+16)) + " stringsStart=" + u32(b, off+20));
            if (sz <= 0) break;
            off += sz; n++;
        }
    }
}
