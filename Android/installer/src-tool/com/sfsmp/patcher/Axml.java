// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

/**
 * Minimal binary AndroidManifest (AXML) editor.
 *
 * Appends ONE string to the string pool and repoints ONE attribute value to it.
 * Every other byte of the manifest is preserved verbatim; the XML header size and the
 * string pool chunk size are recomputed. Node chunks store string indices (not file
 * offsets), so they can be copied unchanged after the pool grows.
 */
public final class Axml {
    private static final int RES_XML_TYPE = 0x0003;
    private static final int RES_STRING_POOL_TYPE = 0x0001;
    private static final int RES_XML_START_ELEMENT_TYPE = 0x0102;
    private static final int TYPE_STRING = 0x03;
    private static final int NO_INDEX = 0xFFFFFFFF;

    private final byte[] data;

    private int poolStart;
    private int poolEnd;
    private int chunkSize;
    private int stringCount;
    private int styleCount;
    private int flags;
    private int stringsStart;
    private int stylesStart;
    private String[] strings;

    private Axml(byte[] data) {
        this.data = data;
    }

    public static byte[] replaceAttributeValue(byte[] manifest, String oldValue, String newValue)
            throws IOException {
        Axml a = new Axml(manifest);
        a.parse();

        List<Integer> hits = a.findAttributeHits(oldValue);
        if (hits.isEmpty()) {
            throw new IOException("attribute value not found in manifest: " + oldValue);
        }

        int newIndex = a.stringCount;
        byte[] newPool = a.buildPool(newValue);

        int newTailStart = 8 + newPool.length;
        int shift = newTailStart - a.poolEnd;
        byte[] out = new byte[newTailStart + (manifest.length - a.poolEnd)];

        System.arraycopy(manifest, 0, out, 0, 8);
        putU32(out, 4, out.length);
        System.arraycopy(newPool, 0, out, 8, newPool.length);
        System.arraycopy(manifest, a.poolEnd, out, newTailStart, manifest.length - a.poolEnd);

        for (int base : hits) {
            int p = base + shift;
            putU32(out, p + 8, newIndex);              // rawValue
            out[p + 15] = (byte) TYPE_STRING;          // typedValue.dataType
            putU32(out, p + 16, newIndex);             // typedValue.data
        }
        return out;
    }

    private void parse() throws IOException {
        if (data.length < 8) throw new IOException("manifest too small");
        if (u16(0) != RES_XML_TYPE) {
            throw new IOException("not an AXML file (type=0x" + Integer.toHexString(u16(0)) + ")");
        }
        poolStart = u16(2);
        if (u16(poolStart) != RES_STRING_POOL_TYPE) throw new IOException("first chunk is not a string pool");
        chunkSize = (int) u32(poolStart + 4);
        poolEnd = poolStart + chunkSize;
        stringCount = (int) u32(poolStart + 8);
        styleCount = (int) u32(poolStart + 12);
        flags = (int) u32(poolStart + 16);
        stringsStart = (int) u32(poolStart + 20);
        stylesStart = (int) u32(poolStart + 24);
        int dataStart = poolStart + stringsStart;
        strings = new String[stringCount];
        for (int i = 0; i < stringCount; i++) {
            int off = (int) u32(poolStart + 28 + i * 4);
            strings[i] = decodeString(dataStart + off);
        }
    }

    private boolean utf8() {
        return (flags & 0x100) != 0;
    }

    private String decodeString(int p) throws IOException {
        if (utf8()) {
            int p2 = p;
            int n1 = data[p2] & 0xff;
            if ((n1 & 0x80) != 0) {
                p2 += 2;
            } else {
                p2 += 1;
            }
            int p3 = p2;
            int n2 = data[p3] & 0xff;
            int u8len;
            if ((n2 & 0x80) != 0) {
                u8len = ((n2 & 0x7f) << 8) | (data[p3 + 1] & 0xff);
                p3 += 2;
            } else {
                u8len = n2;
                p3 += 1;
            }
            return new String(data, p3, u8len, "UTF-8");
        }
        int n = u16(p);
        int len;
        int p2;
        if ((n & 0x8000) != 0) {
            len = ((n & 0x7fff) << 16) | u16(p + 2);
            p2 = p + 4;
        } else {
            len = n;
            p2 = p + 2;
        }
        return new String(data, p2, len * 2, "UTF-16LE");
    }

    private byte[] encodeString(String s) throws IOException {
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        if (utf8()) {
            byte[] b = s.getBytes("UTF-8");
            writeLen8(o, s.length());
            writeLen8(o, b.length);
            o.write(b);
            o.write(0);
        } else {
            byte[] b = s.getBytes("UTF-16LE");
            int len = s.length();
            if (len >= 0x8000) {
                int first = 0x8000 | (len >>> 16);
                o.write(first & 0xff);
                o.write((first >> 8) & 0xff);
                o.write(len & 0xff);
                o.write((len >> 8) & 0xff);
            } else {
                o.write(len & 0xff);
                o.write((len >> 8) & 0xff);
            }
            o.write(b);
            o.write(0);
            o.write(0);
        }
        while ((o.size() & 3) != 0) o.write(0);
        return o.toByteArray();
    }

    private static void writeLen8(ByteArrayOutputStream o, int v) {
        if (v > 0x7f) {
            o.write(0x80 | (v >> 8));
            o.write(v & 0xff);
        } else {
            o.write(v);
        }
    }

    private byte[] buildPool(String newValue) throws IOException {
        int newCount = stringCount + 1;
        byte[] enc = encodeString(newValue);

        int oldDataLen = (stylesStart > 0 ? stylesStart : chunkSize) - stringsStart;
        int oldStyleLen = stylesStart > 0 ? chunkSize - stylesStart : 0;

        int newStringsStart = 28 + 4 * newCount + 4 * styleCount;
        int stringDataLen = oldDataLen + enc.length;
        int paddedStringDataLen = (stringDataLen + 3) & ~3;
        int newStylesStart = stylesStart > 0 ? newStringsStart + paddedStringDataLen : 0;
        int total = newStringsStart + paddedStringDataLen + oldStyleLen;
        total = (total + 3) & ~3;

        byte[] pool = new byte[total];
        putU16(pool, 0, RES_STRING_POOL_TYPE);
        putU16(pool, 2, 28);
        putU32(pool, 4, total);
        putU32(pool, 8, newCount);
        putU32(pool, 12, styleCount);
        putU32(pool, 16, flags & ~1); // clear SORTED_FLAG: we appended out of order
        putU32(pool, 20, newStringsStart);
        putU32(pool, 24, newStylesStart);

        int p = 28;
        for (int i = 0; i < stringCount; i++) {
            putU32(pool, p, (int) u32(poolStart + 28 + i * 4));
            p += 4;
        }
        putU32(pool, p, oldDataLen); // new string offset
        p += 4;
        int styleOffsetsStart = 28 + 4 * stringCount;
        for (int i = 0; i < styleCount; i++) {
            putU32(pool, p, (int) u32(poolStart + styleOffsetsStart + i * 4));
            p += 4;
        }
        System.arraycopy(data, poolStart + stringsStart, pool, newStringsStart, oldDataLen);
        System.arraycopy(enc, 0, pool, newStringsStart + oldDataLen, enc.length);
        if (oldStyleLen > 0) {
            System.arraycopy(data, poolStart + stylesStart, pool, newStylesStart, oldStyleLen);
        }
        return pool;
    }

    private List<Integer> findAttributeHits(String oldValue) throws IOException {
        List<Integer> hits = new ArrayList<>();
        int pos = poolEnd;
        while (pos + 8 <= data.length) {
            int type = u16(pos);
            int size = (int) u32(pos + 4);
            if (size < 8 || pos + size > data.length) break;
            if (type == RES_XML_START_ELEMENT_TYPE) {
                int attrStart = u16(pos + 24);
                int attrSize = u16(pos + 26);
                int attrCount = u16(pos + 28);
                if (attrSize >= 20) {
                    int base = pos + 16 + attrStart;
                    for (int i = 0; i < attrCount; i++) {
                        int b = base + i * attrSize;
                        int raw = (int) u32(b + 8);
                        boolean match = raw != NO_INDEX && raw < strings.length
                                && oldValue.equals(strings[raw]);
                        if (!match && data[b + 15] == (byte) TYPE_STRING) {
                            int dv = (int) u32(b + 16);
                            match = dv < strings.length && oldValue.equals(strings[dv]);
                        }
                        if (match) hits.add(b);
                    }
                }
            }
            pos += size;
        }
        return hits;
    }

    private int u16(int p) {
        return (data[p] & 0xff) | ((data[p + 1] & 0xff) << 8);
    }

    private long u32(int p) {
        return ((long) (data[p] & 0xff))
                | ((long) (data[p + 1] & 0xff) << 8)
                | ((long) (data[p + 2] & 0xff) << 16)
                | ((long) (data[p + 3] & 0xff) << 24);
    }

    private static void putU16(byte[] b, int p, int v) {
        b[p] = (byte) v;
        b[p + 1] = (byte) (v >> 8);
    }

    private static void putU32(byte[] b, int p, int v) {
        b[p] = (byte) v;
        b[p + 1] = (byte) (v >> 8);
        b[p + 2] = (byte) (v >> 16);
        b[p + 3] = (byte) (v >> 24);
    }
}
