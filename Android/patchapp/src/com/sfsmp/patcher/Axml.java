// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import java.nio.charset.StandardCharsets;

/**
 * 最小 AndroidManifest.xml(AXML) 二进制读写器。
 *
 * 只做安装器真正需要的事：
 *   1) bumpVersionCode：把 <manifest android:versionCode> 的 typed data 原地改成新 int
 *      （长度不变、不动字符串池，安全）。用于「每次给游戏打补丁包，versionCode +1」。
 *   2) getStringAttr：读字符串型属性（给换 launcher Activity 用）。
 *
 * AXML 布局（小端）：
 *   文件头 8 字节: type(2)=0x0003 headerSize(2)=8 size(4)
 *   之后 chunk: type(2) headerSize(2) size(4) body
 *     0x0001 字符串池: +8 stringCount +12 styleCount +16 flags(0x100=UTF8) +20 stringsStart
 *     0x0180 资源映射表
 *     0x0102 起始元素: +16 lineNumber +20 comment +24 ns +28 name +32 attributeStart +34 attributeSize +36 attributeCount
 *     每个属性 20 字节: ns(4) name(4) rawValue(4) size(2) res0(1) dataType(1) data(4)
 */
public final class Axml {
    private Axml() {}

    private static int u16(byte[] b, int o) { return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8); }
    private static int u32(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }
    private static void put32(byte[] b, int o, int v) {
        b[o] = (byte) v; b[o + 1] = (byte) (v >>> 8); b[o + 2] = (byte) (v >>> 16); b[o + 3] = (byte) (v >>> 24);
    }

    public static String string(byte[] b, int pool, int idx) {
        if (idx < 0 || pool < 0) return null;
        int count = u32(b, pool + 8);
        if (idx >= count) return null;
        int flags = u32(b, pool + 16);
        boolean utf8 = (flags & 0x100) != 0;
        int stringsStart = u32(b, pool + 20);
        int off = pool + stringsStart + u32(b, pool + 28 + idx * 4);
        if (utf8) {
            // UTF-8 池的串头是「两个长度」：先 UTF-16 码元数，再 UTF-8 字节数
            int a0 = b[off] & 0xFF; off += 1;
            if ((a0 & 0x80) != 0) { a0 = ((a0 & 0x7F) << 8) | (b[off] & 0xFF); off += 1; }
            int nbytes = b[off] & 0xFF; off += 1;
            if ((nbytes & 0x80) != 0) { nbytes = ((nbytes & 0x7F) << 8) | (b[off] & 0xFF); off += 1; }
            if (nbytes < 0 || off + nbytes > b.length) return null;
            return new String(b, off, nbytes, StandardCharsets.UTF_8);
        }
        int len = u16(b, off);
        off += 2;
        if ((len & 0x8000) != 0) { len = ((len & 0x7FFF) << 16) | u16(b, off); off += 2; }
        return new String(b, off, len * 2, StandardCharsets.UTF_16LE);
    }

    public static int findStringPool(byte[] b) {
        int off = 8;
        while (off + 8 <= b.length) {
            int type = u16(b, off);
            int size = u32(b, off + 4);
            if (type == 0x0001) return off;
            if (size <= 0) break;
            off += size;
        }
        return -1;
    }

    public interface ElemVisitor { void visit(int chunk, String name); }

    public static void forEachElement(byte[] b, ElemVisitor v) {
        int pool = findStringPool(b);
        if (pool < 0) return;
        int off = 8;
        while (off + 8 <= b.length) {
            int type = u16(b, off);
            int size = u32(b, off + 4);
            if (size <= 0) break;
            if (type == 0x0102) {
                String name = string(b, pool, u32(b, off + 20));
                if (name != null) v.visit(off, name);
            }
            off += size;
        }
    }

    public static int findAttr(byte[] b, int chunk, String attrName) {
        int pool = findStringPool(b);
        if (pool < 0) return -1;
        int attrStart = u16(b, chunk + 24);
        int attrCount = u16(b, chunk + 28);
        int base = chunk + 16 + attrStart;
        for (int i = 0; i < attrCount; i++) {
            int a = base + i * 20;
            if (a + 20 > b.length) return -1;
            String n = string(b, pool, u32(b, a + 4));
            if (attrName.equals(n)) return a;
        }
        return -1;
    }

    public static int getIntAttr(byte[] b, int attr, int def) {
        if (attr < 0) return def;
        return u32(b, attr + 16);
    }

    /** 把 <manifest android:versionCode> 改成 newCode（原地 4 字节）。返回改之前的值，找不到返回 -1。 */
    public static int bumpVersionCode(byte[] b, final int newCode) {
        final int[] found = { -1 };
        forEachElement(b, new ElemVisitor() {
            @Override public void visit(int chunk, String name) {
                if (found[0] >= 0 || !"manifest".equals(name)) return;
                int a = findAttr(b, chunk, "versionCode");
                if (a < 0) return;
                int old = u32(b, a + 16);
                put32(b, a + 16, newCode);
                found[0] = old;
            }
        });
        return found[0];
    }

    public static String getStringAttr(byte[] b, int chunk, String attrName) {
        int a = findAttr(b, chunk, attrName);
        if (a < 0) return null;
        int pool = findStringPool(b);
        int raw = u32(b, a + 8);
        if (raw >= 0) return string(b, pool, raw);
        return string(b, pool, u32(b, a + 16));
    }
}
