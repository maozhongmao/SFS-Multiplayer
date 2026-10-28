// SPDX-License-Identifier: GPL-3.0-or-later
import com.sfsmp.patcher.Axml;
import java.nio.file.*;

public class DumpAxml {
    public static void main(String[] a) throws Exception {
        final byte[] b = Files.readAllBytes(Paths.get(a[0]));
        final int pool = Axml.findStringPool(b);
        System.out.println("pool@" + pool + " count=" + Axml.string(b, pool, 0));
        Axml.forEachElement(b, new Axml.ElemVisitor() {
            public void visit(int chunk, String name) {
                if (!"manifest".equals(name) && !"application".equals(name) && !"activity".equals(name)) return;
                System.out.println("ELEM " + name + " @" + chunk);
                int attrStart = (b[chunk + 24] & 0xFF) | ((b[chunk + 25] & 0xFF) << 8);
                int attrCount = (b[chunk + 28] & 0xFF) | ((b[chunk + 29] & 0xFF) << 8);
                System.out.println("  attrStart=" + attrStart + " count=" + attrCount);
                int base = chunk + 16 + attrStart;
                for (int i = 0; i < attrCount; i++) {
                    int off = base + i * 20;
                    int nameIdx = (b[off+4]&0xFF)|((b[off+5]&0xFF)<<8)|((b[off+6]&0xFF)<<16)|((b[off+7]&0xFF)<<24);
                    int raw = (b[off+8]&0xFF)|((b[off+9]&0xFF)<<8)|((b[off+10]&0xFF)<<16)|((b[off+11]&0xFF)<<24);
                    int type = b[off+15] & 0xFF;
                    int data = (b[off+16]&0xFF)|((b[off+17]&0xFF)<<8)|((b[off+18]&0xFF)<<16)|((b[off+19]&0xFF)<<24);
                    System.out.println("   attr nameIdx=" + nameIdx + " name=" + Axml.string(b, pool, nameIdx) + " type=0x" + Integer.toHexString(type) + " raw=" + raw + " data=" + data);
                }
            }
        });
    }
}
