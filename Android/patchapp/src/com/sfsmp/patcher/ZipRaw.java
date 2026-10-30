// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import java.io.*;
import java.util.*;
import java.util.zip.CRC32;
import java.util.zip.Deflater;

/**
 * 极简 zip 重打包器：**原样搬运压缩流**（不解压、不重压），只对我们替换/新增的少量条目做压缩。
 * 目的：手机上打补丁不再把 105 MB 的 libil2cpp/libunity/assets 全部解压再压一遍。
 *
 * 支持：DEFLATED 原样复制、STORED 原样复制（并保持 4 字节对齐）、替换条目、追加新条目。
 * 不支持：ZIP64、加密条目、分卷（官方 SFS 包都不涉及）。
 */
final class ZipRaw {

    interface Log { void line(String s); }
    interface Skip { boolean skip(String name); }

    static final class Add {
        final String name; final byte[] data; final boolean stored;
        Add(String name, byte[] data, boolean stored) { this.name = name; this.data = data; this.stored = stored; }
    }

    private static final int EOCD_SIG = 0x06054b50;
    private static final int CD_SIG = 0x02014b50;
    private static final int LOCAL_SIG = 0x04034b50;

    static void build(File src, File dst, Map<String, byte[]> replace, Skip skip, List<Add> adds, Log log) throws IOException {
        RandomAccessFile raf = new RandomAccessFile(src, "r");
        try {
            long len = raf.length();
            byte[] tail = new byte[(int) Math.min(len, 66000)];
            raf.seek(len - tail.length);
            raf.readFully(tail);
            int eocd = -1;
            for (int i = tail.length - 22; i >= 0; i--) {
                if (le32(tail, i) == EOCD_SIG) { eocd = i; break; }
            }
            if (eocd < 0) throw new IOException("不是有效的 zip：找不到 EOCD");
            int count = le16(tail, eocd + 10);
            long cdOff = le32u(tail, eocd + 16);

            List<Cd> cds = new ArrayList<Cd>(count);
            raf.seek(cdOff);
            byte[] one = new byte[8192];
            for (int i = 0; i < count; i++) {
                raf.readFully(one, 0, 46);
                if (le32(one, 0) != CD_SIG) throw new IOException("中央目录损坏 @" + i);
                Cd c = new Cd();
                c.flags = le16(one, 8); c.method = le16(one, 10);
                c.time = le16(one, 12); c.date = le16(one, 14);
                c.crc = le32u(one, 16); c.csize = le32u(one, 20); c.usize = le32u(one, 24);
                int nameLen = le16(one, 28), extraLen = le16(one, 30), commentLen = le16(one, 32);
                c.localOff = le32u(one, 42);
                byte[] nm = new byte[nameLen];
                raf.readFully(nm);
                if (extraLen > 0) raf.seek(raf.getFilePointer() + extraLen);
                if (commentLen > 0) raf.seek(raf.getFilePointer() + commentLen);
                c.name = new String(nm, 0, nm.length, utf8(nm) ? "UTF-8" : "ISO-8859-1");
                c.nameBytes = nm;
                cds.add(c);
            }

            File parent = dst.getParentFile();
            if (parent != null) parent.mkdirs();
            Counting out = new Counting(new BufferedOutputStream(new FileOutputStream(dst), 1 << 16));
            List<Cd> written = new ArrayList<Cd>(cds.size() + adds.size());
            byte[] buf = new byte[1 << 16];

            for (Cd c : cds) {
                /* v133 修复：先看「替换表」，再看「跳过表」。
                 * 之前顺序反了 —— skip 里排除了 AndroidManifest.xml，于是替换内容永远不会被写出，
                 * 生成的 APK 里压根没有 manifest，系统报
                 *   INSTALL_PARSE_FAILED_UNEXPECTED_EXCEPTION: Failed to parse .../base.apk: AndroidManifest.xml
                 * 而界面表现就是「点了安装没反应」（其实会话立刻失败了）。 */
                byte[] rep = (replace == null) ? null : replace.get(c.name);
                if (rep == null && skip != null && skip.skip(c.name)) continue;
                if (rep != null) {
                    Cd n = new Cd();
                    n.name = c.name; n.nameBytes = c.nameBytes; n.time = c.time; n.date = c.date;
                    n.flags = (c.flags & 0x0800);
                    writeData(out, n, rep, c.method == 0, true);   /* 保持原条目的压缩方式（arsc 必须 STORED） */
                    written.add(n);
                    continue;
                }
                Cd n = new Cd();
                n.name = c.name; n.nameBytes = c.nameBytes; n.time = c.time; n.date = c.date;
                n.flags = (c.flags & 0x0800); n.method = c.method; n.crc = c.crc; n.csize = c.csize; n.usize = c.usize;
                long extraStart = c.localOff + 30 + c.nameBytes.length;
                long dataOff;
                raf.seek(c.localOff + 28);
                int localExtra = (raf.read() | (raf.read() << 8));
                dataOff = extraStart + localExtra;
                writeRawHeader(out, n, c.localOff);
                raf.seek(dataOff);
                long remain = c.csize;
                while (remain > 0) {
                    int want = (int) Math.min(remain, buf.length);
                    raf.readFully(buf, 0, want);
                    out.write(buf, 0, want);
                    remain -= want;
                }
                written.add(n);
            }

            if (adds != null) for (Add a : adds) {
                Cd n = new Cd();
                n.name = a.name; n.nameBytes = a.name.getBytes("UTF-8"); n.time = 0; n.date = 0x21;
                n.flags = utf8(n.nameBytes) ? 0x0800 : 0;
                writeData(out, n, a.data, a.stored, true);
                written.add(n);
            }

            long cdStart = out.count;
            for (Cd n : written) writeCd(out, n);
            long cdSize = out.count - cdStart;
            writeEocd(out, written.size(), cdStart, cdSize);
            out.flush();
            out.close();
            if (log != null) log.line("raw copy 完成: " + written.size() + " 条目, " + (out.count / 1048576) + " MB");
        } finally { raf.close(); }
    }

    private static boolean utf8(byte[] nm) { for (byte b : nm) if (b < 0) return true; return false; }

    private static void writeRawHeader(Counting out, Cd n, long srcLocalOff) throws IOException {
        if (n.method == 0) {
            int nameLen = n.nameBytes.length;
            int pad = (int) ((4 - ((out.count + 30 + nameLen) % 4)) % 4);
            byte[] extra = new byte[pad];
            if (pad >= 4) { extra[0] = 0x35; extra[1] = (byte) 0xD9; extra[2] = (byte) (pad - 4); }
            n.localOff = out.count;
            byte[] h = new byte[30];
            put32(h, 0, LOCAL_SIG); put16(h, 4, 20); put16(h, 6, n.flags); put16(h, 8, n.method);
            put16(h, 10, n.time); put16(h, 12, n.date); put32(h, 14, (int) n.crc);
            put32(h, 18, (int) n.csize); put32(h, 22, (int) n.usize);
            put16(h, 26, nameLen); put16(h, 28, extra.length);
            out.write(h, 0, 30); out.write(n.nameBytes, 0, nameLen); out.write(extra, 0, extra.length);
            return;
        }
        n.localOff = out.count;
        byte[] h = new byte[30];
        put32(h, 0, LOCAL_SIG); put16(h, 4, 20); put16(h, 6, n.flags); put16(h, 8, n.method);
        put16(h, 10, n.time); put16(h, 12, n.date); put32(h, 14, (int) n.crc);
        put32(h, 18, (int) n.csize); put32(h, 22, (int) n.usize);
        put16(h, 26, n.nameBytes.length); put16(h, 28, 0);
        out.write(h, 0, 30); out.write(n.nameBytes, 0, n.nameBytes.length);
    }

    private static void writeData(Counting out, Cd n, byte[] data, boolean stored, boolean align) throws IOException {
        CRC32 crc = new CRC32(); crc.update(data);
        n.crc = crc.getValue();
        n.usize = data.length;
        if (stored) {
            n.method = 0; n.csize = data.length;
            int nameLen = n.nameBytes.length;
            int pad = (int) ((4 - ((out.count + 30 + nameLen) % 4)) % 4);
            if (pad > 0 && pad < 4) pad += 4;
            byte[] extra = new byte[pad];
            if (pad >= 4) { extra[0] = 0x35; extra[1] = (byte) 0xD9; extra[2] = (byte) (pad - 4); }
            n.localOff = out.count;
            byte[] h = new byte[30];
            put32(h, 0, LOCAL_SIG); put16(h, 4, 20); put16(h, 6, n.flags); put16(h, 8, 0);
            put16(h, 10, n.time); put16(h, 12, n.date); put32(h, 14, (int) n.crc);
            put32(h, 18, data.length); put32(h, 22, data.length);
            put16(h, 26, nameLen); put16(h, 28, extra.length);
            out.write(h, 0, 30); out.write(n.nameBytes, 0, nameLen); out.write(extra, 0, extra.length);
            out.write(data, 0, data.length);
            return;
        }
        Deflater d = new Deflater(6, true);   /* v133: true = raw deflate（zip 要裸 deflate，不能带 zlib 头） */
        d.setInput(data); d.finish();
        ByteArrayOutputStream bo = new ByteArrayOutputStream(Math.max(64, data.length / 3));
        byte[] tmp = new byte[1 << 16];
        while (!d.finished()) { int k = d.deflate(tmp); if (k > 0) bo.write(tmp, 0, k); }
        d.end();
        byte[] c = bo.toByteArray();
        n.method = 8; n.csize = c.length; n.localOff = out.count;
        byte[] h = new byte[30];
        put32(h, 0, LOCAL_SIG); put16(h, 4, 20); put16(h, 6, n.flags); put16(h, 8, 8);
        put16(h, 10, n.time); put16(h, 12, n.date); put32(h, 14, (int) n.crc);
        put32(h, 18, c.length); put32(h, 22, data.length);
        put16(h, 26, n.nameBytes.length); put16(h, 28, 0);
        out.write(h, 0, 30); out.write(n.nameBytes, 0, n.nameBytes.length); out.write(c, 0, c.length);
    }

    private static void writeCd(Counting out, Cd n) throws IOException {
        byte[] h = new byte[46];
        put32(h, 0, CD_SIG); put16(h, 4, 20); put16(h, 6, 20); put16(h, 8, n.flags); put16(h, 10, n.method);
        put16(h, 12, n.time); put16(h, 14, n.date); put32(h, 16, (int) n.crc);
        put32(h, 20, (int) n.csize); put32(h, 24, (int) n.usize);
        put16(h, 28, n.nameBytes.length); put16(h, 30, 0); put16(h, 32, 0); put16(h, 34, 0);
        put16(h, 36, 0); put32(h, 38, 0); put32(h, 42, (int) n.localOff);
        out.write(h, 0, 46); out.write(n.nameBytes, 0, n.nameBytes.length);
    }

    private static void writeEocd(Counting out, int count, long cdStart, long cdSize) throws IOException {
        byte[] h = new byte[22];
        put32(h, 0, EOCD_SIG); put16(h, 4, 0); put16(h, 6, 0);
        put16(h, 8, count); put16(h, 10, count);
        put32(h, 12, (int) cdSize); put32(h, 16, (int) cdStart); put16(h, 20, 0);
        out.write(h, 0, 22);
    }

    private static int le16(byte[] b, int o) { return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8); }
    private static int le32(byte[] b, int o) { return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24); }
    private static long le32u(byte[] b, int o) { return le32(b, o) & 0xFFFFFFFFL; }
    private static void put16(byte[] b, int o, int v) { b[o] = (byte) v; b[o + 1] = (byte) (v >> 8); }
    private static void put32(byte[] b, int o, int v) { b[o] = (byte) v; b[o + 1] = (byte) (v >> 8); b[o + 2] = (byte) (v >> 16); b[o + 3] = (byte) (v >> 24); }

    private static final class Cd {
        String name; byte[] nameBytes; int flags, method, time, date;
        long crc, csize, usize, localOff;
    }

    static final class Counting extends FilterOutputStream {
        long count = 0;
        Counting(OutputStream o) { super(o); }
        @Override public void write(int b) throws IOException { out.write(b); count++; }
        @Override public void write(byte[] b, int off, int len) throws IOException { out.write(b, off, len); count += len; }
    }
}
