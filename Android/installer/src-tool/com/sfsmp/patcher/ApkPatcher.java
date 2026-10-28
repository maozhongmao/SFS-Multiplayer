// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Enumeration;
import java.util.List;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/**
 * Rewrites an official SFS APK into a patched APK:
 *   - AndroidManifest.xml : launcher activity name repointed to our HookActivity
 *   - classesN.dex        : our hook dex appended as the highest numbered classesN
 *   - lib/arm64-v8a/libsfsmp.so : our native library
 * All other entries are copied byte-for-byte (STORE stays STORE).
 */
public final class ApkPatcher {
    public static final String SO_ENTRY = "lib/arm64-v8a/libsfsmp.so";

    public static Report patch(File inApk, File outApk, byte[] newManifest, File dex, File so,
                               String hookClass, String oldClass) throws IOException {
        Report rep = new Report();
        try (ZipFile zip = new ZipFile(inApk);
             ZipOutputStream zos = new ZipOutputStream(new FileOutputStream(outApk))) {
            if (zip.getComment() != null) zos.setComment(zip.getComment());

            int maxDex = 0;
            Enumeration<? extends ZipEntry> en = zip.entries();
            while (en.hasMoreElements()) {
                ZipEntry e = (ZipEntry) en.nextElement();
                String name = e.getName();
                if (name.equals("AndroidManifest.xml")) {
                    rep.manifestOldLength = (int) e.getSize();
                    writeEntry(zos, name, newManifest, false, e.getTime());
                    rep.manifestNewLength = newManifest.length;
                    rep.manifestPatched = true;
                    continue;
                }
                if (name.equals(SO_ENTRY)) {
                    rep.removedOldSo = true;
                    continue;
                }
                if (name.matches("classes[0-9]*\\.dex")) {
                    int idx = dexIndex(name);
                    if (idx > maxDex) maxDex = idx;
                }
                copyEntry(zip, zos, e);
                rep.copiedEntries++;
            }

            int newIdx = maxDex + 1;
            String dexName = newIdx == 1 ? "classes.dex" : ("classes" + newIdx + ".dex");
            writeEntry(zos, dexName, readAll(dex), false, 0L);
            rep.addedDex = dexName;
            writeEntry(zos, SO_ENTRY, readAll(so), false, 0L);
            rep.addedSo = true;
        }
        return rep;
    }

    private static int dexIndex(String name) {
        if (name.equals("classes.dex")) return 1;
        String digits = name.substring("classes".length(), name.length() - ".dex".length());
        try {
            return Integer.parseInt(digits);
        } catch (NumberFormatException ex) {
            return 0;
        }
    }

    private static void copyEntry(ZipFile zip, ZipOutputStream zos, ZipEntry e) throws IOException {
        ZipEntry out = new ZipEntry(e.getName());
        out.setTime(e.getTime());
        if (e.getMethod() == ZipEntry.STORED) {
            out.setMethod(ZipEntry.STORED);
            out.setSize(e.getSize());
            out.setCompressedSize(e.getSize());
            out.setCrc(e.getCrc());
        } else {
            out.setMethod(ZipEntry.DEFLATED);
        }
        if (e.getComment() != null) out.setComment(e.getComment());
        zos.putNextEntry(out);
        try (InputStream is = zip.getInputStream(e)) {
            copy(is, zos);
        }
        zos.closeEntry();
    }

    private static void writeEntry(ZipOutputStream zos, String name, byte[] data, boolean store, long time)
            throws IOException {
        ZipEntry out = new ZipEntry(name);
        if (time > 0) out.setTime(time);
        if (store) {
            CRC32 crc = new CRC32();
            crc.update(data);
            out.setMethod(ZipEntry.STORED);
            out.setSize(data.length);
            out.setCompressedSize(data.length);
            out.setCrc(crc.getValue());
        } else {
            out.setMethod(ZipEntry.DEFLATED);
        }
        zos.putNextEntry(out);
        zos.write(data);
        zos.closeEntry();
    }

    private static void copy(InputStream is, OutputStream os) throws IOException {
        byte[] buf = new byte[64 * 1024];
        int n;
        while ((n = is.read(buf)) > 0) os.write(buf, 0, n);
    }

    /** Reads a single entry out of a zip/apk without extracting the whole archive. */
    public static byte[] readAllEntry(File apk, String entryName) throws IOException {
        try (ZipFile zip = new ZipFile(apk)) {
            ZipEntry e = zip.getEntry(entryName);
            if (e == null) throw new IOException("entry not found: " + entryName + " in " + apk);
            java.io.ByteArrayOutputStream bos = new java.io.ByteArrayOutputStream((int) Math.max(1024, e.getSize()));
            try (InputStream is = zip.getInputStream(e)) {
                copy(is, bos);
            }
            return bos.toByteArray();
        }
    }

    public static byte[] readAll(File f) throws IOException {
        byte[] b = new byte[(int) f.length()];
        try (InputStream is = new java.io.FileInputStream(f)) {
            int off = 0;
            while (off < b.length) {
                int n = is.read(b, off, b.length - off);
                if (n < 0) break;
                off += n;
            }
            if (off != b.length) throw new IOException("short read: " + f);
        }
        return b;
    }

    public static final class Report {
        public int copiedEntries;
        public boolean manifestPatched;
        public int manifestOldLength;
        public int manifestNewLength;
        public boolean removedOldSo;
        public String addedDex;
        public boolean addedSo;

        @Override
        public String toString() {
            return "copied=" + copiedEntries
                    + " manifest=" + manifestOldLength + "->" + manifestNewLength
                    + " removedOldSo=" + removedOldSo
                    + " addedDex=" + addedDex
                    + " addedSo=" + addedSo;
        }
    }
}
