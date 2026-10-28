// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

/**
 * AXML 字符串池重写：给 AndroidManifest.xml 追加一个字符串，并把某个属性的值指向它。
 *
 * 用途：把 <activity android:name="游戏原本的启动类"> 改成我们自己的 HookActivity（子类），
 * 这样补丁包不需要替换游戏源码，我们只加一个几十行的子类。
 *
 * 做法：整块重建字符串池（原有字符串索引不变，新串追加在末尾），后面的 chunk 原样拷贝，
 * 只把那一个属性的 rawValue / typed-data 改成新索引。文件总长度与池 chunk 长度同步更新。
 */
public final class AxmlStrings {
    private AxmlStrings() {}

    private static int u16(byte[] b, int o) { return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8); }
    private static int u32(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }
    private static void put16(byte[] b, int o, int v) { b[o] = (byte) v; b[o + 1] = (byte) (v >>> 8); }
    private static void put32(byte[] b, int o, int v) {
        b[o] = (byte) v; b[o + 1] = (byte) (v >>> 8); b[o + 2] = (byte) (v >>> 16); b[o + 3] = (byte) (v >>> 24);
    }

    /** 读整个字符串池。styleCount 必须为 0（manifest 里不会有样式）。 */
    public static List<String> readPool(byte[] b, int pool) {
        int count = u32(b, pool + 8);
        int styleCount = u32(b, pool + 12);
        if (styleCount != 0) throw new IllegalStateException("AXML styleCount=" + styleCount + " unsupported");
        List<String> out = new ArrayList<String>(count);
        for (int i = 0; i < count; i++) out.add(Axml.string(b, pool, i));
        return out;
    }

    private static void writeLen(ByteArrayOutputStream out, int len) {
        if (len > 0x7FFF) { out.write(((len >> 16) & 0xFF) | 0x80); out.write((len >> 8) & 0xFF); out.write(len & 0xFF); }
        else if (len > 0x7F) { out.write(((len >> 8) & 0xFF) | 0x80); out.write(len & 0xFF); }
        else out.write(len);
    }

    /** 用 UTF-8 重新编码一个字符串池 chunk。 */
    private static byte[] buildPool(List<String> strings) {
        int n = strings.size();
        int stringsStart = 28 + 4 * n;
        while ((stringsStart & 3) != 0) stringsStart++;
        ByteArrayOutputStream data = new ByteArrayOutputStream();
        int[] offs = new int[n];
        for (int i = 0; i < n; i++) {
            offs[i] = data.size();
            String s = strings.get(i);
            byte[] utf = s.getBytes(StandardCharsets.UTF_8);
            if (utf.length > 0x7FFF) throw new IllegalStateException("string too long");
            // AXML 的 UTF-8 串是「两个长度」：先 UTF-16 码元数，再字节数，然后是数据 + 0
            int u16len = s.length();
            writeLen(data, u16len);
            writeLen(data, utf.length);
            data.write(utf, 0, utf.length);
            data.write(0);
        }
        while ((data.size() & 3) != 0) data.write(0);
        int size = stringsStart + data.size();
        byte[] chunk = new byte[size];
        put16(chunk, 0, 0x0001);
        put16(chunk, 2, 28);
        put32(chunk, 4, size);
        put32(chunk, 8, n);
        put32(chunk, 12, 0);
        put32(chunk, 16, 0x100);
        put32(chunk, 20, stringsStart);
        put32(chunk, 24, 0);
        for (int i = 0; i < n; i++) put32(chunk, 28 + 4 * i, offs[i]);
        byte[] d = data.toByteArray();
        System.arraycopy(d, 0, chunk, stringsStart, d.length);
        return chunk;
    }

    /**
     * 把值为 oldValue 的 android:name 属性改成 newValue（自动追加字符串池条目）。
     * @return 新的 manifest 字节；没找到 oldValue 返回 null
     */
    public static byte[] replaceAttrValue(byte[] src, final String oldValue, String newValue) {
        final int pool = Axml.findStringPool(src);
        if (pool < 0) return null;
        final List<String> strings = readPool(src, pool);
        final int newIndex = strings.size();
        strings.add(newValue);

        final int[] hit = { -1 };
        final int[] chunkOfHit = { -1 };
        Axml.forEachElement(src, new Axml.ElemVisitor() {
            @Override public void visit(int chunk, String name) {
                if (hit[0] >= 0) return;
                int attrStart = u16(src, chunk + 24);
                int attrCount = u16(src, chunk + 28);
                int base = chunk + 16 + attrStart;
                for (int i = 0; i < attrCount; i++) {
                    int a = base + i * 20;
                    int raw = u32(src, a + 8);
                    int type = src[a + 15] & 0xFF;
                    String v = null;
                    if (type == 0x03) { int sIdx = raw >= 0 ? raw : u32(src, a + 16); v = sIdx >= 0 && sIdx < strings.size() ? strings.get(sIdx) : null; }
                    if (oldValue.equals(v)) { hit[0] = a; chunkOfHit[0] = chunk; return; }
                }
            }
        });
        if (hit[0] < 0) return null;

        int oldPoolSize = u32(src, pool + 4);
        byte[] newPool = buildPool(strings);
        int tailLen = src.length - (pool + oldPoolSize);
        byte[] out = new byte[src.length - oldPoolSize + newPool.length];
        System.arraycopy(src, 0, out, 0, 8);
        put32(out, 4, out.length);
        System.arraycopy(newPool, 0, out, pool, newPool.length);
        System.arraycopy(src, pool + oldPoolSize, out, pool + newPool.length, tailLen);

        int shift = newPool.length - oldPoolSize;
        int patched = hit[0] + shift;
        put32(out, patched + 8, newIndex);
        out[patched + 15] = 0x03;
        put32(out, patched + 16, newIndex);
        return out;
    }

    /**
     * 把所有以 oldPrefix 开头的字符串属性值改成 newPrefix + 原尾部（用于改包名）。
     * 每个不同的新值追加一条字符串池条目。
     * @return 新的 manifest 字节；没命中返回 null
     */
    public static byte[] replaceAttrPrefix(byte[] src, final String oldPrefix, final String newPrefix) {
        final int pool = Axml.findStringPool(src);
        if (pool < 0) return null;
        final java.util.List<String> strings = readPool(src, pool);
        final int poolSize = strings.size();
        final java.util.ArrayList<int[]> hits = new java.util.ArrayList<int[]>();

        Axml.forEachElement(src, new Axml.ElemVisitor() {
            @Override public void visit(int chunk, String name) {
                int attrStart = u16(src, chunk + 24);
                int attrCount = u16(src, chunk + 28);
                int base = chunk + 16 + attrStart;
                for (int i = 0; i < attrCount; i++) {
                    int a = base + i * 20;
                    int raw = u32(src, a + 8);
                    int type = src[a + 15] & 0xFF;
                    if (type != 0x03) continue;
                    int sIdx = raw >= 0 ? raw : u32(src, a + 16);
                    if (sIdx < 0 || sIdx >= poolSize) continue;
                    String v = strings.get(sIdx);
                    if (v == null || !v.startsWith(oldPrefix)) continue;
                    String nv = newPrefix + v.substring(oldPrefix.length());
                    int ni = strings.size();
                    strings.add(nv);
                    hits.add(new int[] { a, ni });
                }
            }
        });
        if (hits.isEmpty()) return null;

        int oldPoolSize = u32(src, pool + 4);
        byte[] newPool = buildPool(strings);
        int tailLen = src.length - (pool + oldPoolSize);
        byte[] out = new byte[src.length - oldPoolSize + newPool.length];
        System.arraycopy(src, 0, out, 0, 8);
        put32(out, 4, out.length);
        System.arraycopy(newPool, 0, out, pool, newPool.length);
        System.arraycopy(src, pool + oldPoolSize, out, pool + newPool.length, tailLen);

        int shift = newPool.length - oldPoolSize;
        for (int[] h : hits) {
            int at = h[0] + shift;
            put32(out, at + 8, h[1]);
            out[at + 15] = 0x03;
            put32(out, at + 16, h[1]);
        }
        return out;
    }
}

