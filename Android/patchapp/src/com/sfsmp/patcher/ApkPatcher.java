// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import android.content.Context;

import com.android.apksig.ApkSigner;

import java.io.*;
import java.security.KeyStore;
import java.security.PrivateKey;
import java.security.cert.X509Certificate;
import java.util.*;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;
import java.util.zip.ZipOutputStream;

/**
 * 补丁包生成器：读手机上已装的官方 base.apk，产出一个可安装的补丁 APK。
 *
 * 只加东西、不替换游戏逻辑：
 *   1) AndroidManifest.xml：launcher 的 android:name 指到 com.sfsmp.patch.HookActivity；versionCode +1
 *   2) classes{N}.dex：Bridge / Ticker / HookActivity（我们自己的类）
 *   3) lib/arm64-v8a/libsfsmp.so：联机 mod 本体
 * 其余条目按原有压缩方式原样搬运（resources.arsc 必须保持 STORED + 4 字节对齐）。
 */
public final class ApkPatcher {

    public interface Log { void line(String s); }

    public static final String HOOK_ACTIVITY = "com.sfsmp.patch.HookActivity";
    public static final String LIB_ENTRY = "lib/arm64-v8a/libsfsmp.so";

    private static final Pattern DEX = Pattern.compile("^classes(\\d*)\\.dex$");

    public static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        byte[] buf = new byte[1 << 16];
        int n;
        while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
        return bo.toByteArray();
    }

    public static final String OLD_PKG = "com.StefMorojna.SpaceflightSimulator";
    /** 共存版包名（短、独立、和官方不冲突）。 */
    public static final String COEXIST_PKG = "com.sfsmp.sfs";

    /** 生成未签名补丁包（不换包名），返回新 versionCode。 */
    public static int buildUnsigned(Context ctx, File officialApk, File out, String oldLauncher, int newVersionCode, Log log) throws Exception {
        return buildUnsigned(ctx, officialApk, out, oldLauncher, newVersionCode, null, log);
    }

    /** 生成未签名补丁包；newPackage 非空时顺便改包名（共存模式），返回新 versionCode。 */
    public static int buildUnsigned(Context ctx, File officialApk, File out, String oldLauncher, int newVersionCode, String newPackage, Log log) throws Exception {
        if (!officialApk.exists()) throw new IOException("找不到官方 APK: " + officialApk);
        byte[] hookDex = readAsset(ctx, "hook.dex");
        byte[] so = readAsset(ctx, "libsfsmp.so");
        log.line("内置资源: hook.dex " + hookDex.length + " B, libsfsmp.so " + so.length + " B");

        ZipFile zf = new ZipFile(officialApk);
        try {
            ZipEntry me = zf.getEntry("AndroidManifest.xml");
            if (me == null) throw new IOException("官方包里没有 AndroidManifest.xml");
            byte[] raw = readAll(zf.getInputStream(me));
            byte[] patched = AxmlStrings.replaceAttrValue(raw, oldLauncher, HOOK_ACTIVITY);
            if (patched == null) throw new IOException("manifest 里找不到启动类 " + oldLauncher);
            int oldVc = Axml.bumpVersionCode(patched, newVersionCode);
            log.line("manifest: launcher -> " + HOOK_ACTIVITY + ", versionCode " + oldVc + " -> " + newVersionCode);
            if (newPackage != null) {
                byte[] renamed = AxmlStrings.replaceAttrPrefix(patched, OLD_PKG, newPackage);
                if (renamed == null) throw new IOException("manifest 里没有以 " + OLD_PKG + " 开头的属性");
                patched = renamed;
                log.line("manifest: 包名 " + OLD_PKG + " -> " + newPackage);
            }

            int maxDex = 0;
            Enumeration<? extends ZipEntry> en = zf.entries();
            while (en.hasMoreElements()) {
                Matcher m = DEX.matcher(en.nextElement().getName());
                if (m.matches()) { String g = m.group(1); int idx = g.isEmpty() ? 1 : Integer.parseInt(g); if (idx > maxDex) maxDex = idx; }
            }
            String newDex = "classes" + (maxDex + 1) + ".dex";
            log.line("新增 dex: " + newDex + " (已有 classes.dex..classes" + maxDex + ".dex)");

            File parent = out.getParentFile();
            if (parent != null) parent.mkdirs();
            CountingOutputStream cos = new CountingOutputStream(new BufferedOutputStream(new FileOutputStream(out), 1 << 16));
            ZipOutputStream zos = new ZipOutputStream(cos);
            zos.setLevel(6);
            try {
                en = zf.entries();
                while (en.hasMoreElements()) {
                    ZipEntry e = en.nextElement();
                    String name = e.getName();
                    if (name.equals("AndroidManifest.xml")) continue;
                    if (name.equals(newDex)) continue;
                    if (name.equals(LIB_ENTRY)) continue;
                    if (e.isDirectory()) { copyDir(zos, cos, name); continue; }
                    if (name.equals("resources.arsc") && newPackage != null) {
                        byte[] ar;
                        InputStream ain = zf.getInputStream(e);
                        try { ar = readAll(ain); } finally { ain.close(); }
                        if (!Arsc.renamePackage(ar, newPackage)) throw new IOException("resources.arsc 改包名失败");
                        log.line("arsc: 包名 -> " + Arsc.readPackage(ar));
                        writeBytes(zos, cos, name, ar, true);   /* arsc 必须 STORED */
                        continue;
                    }
                    InputStream in = zf.getInputStream(e);
                    try { copyEntry(zos, cos, name, in, e.getMethod() == ZipEntry.STORED, e.getTime()); }
                    finally { in.close(); }
                }
                writeBytes(zos, cos, "AndroidManifest.xml", patched, false);
                writeBytes(zos, cos, newDex, hookDex, false);
                writeBytes(zos, cos, LIB_ENTRY, so, false);
            } finally { zos.close(); }
            return newVersionCode;
        } finally { zf.close(); }
    }

    /** 读取内置资源；assets 里是 XOR 密文（见 Pack），这里解密。 */
    private static byte[] readAsset(Context ctx, String name) throws IOException {
        try {
            return Pack.readPacked(ctx.getAssets().open(name));
        } catch (IOException e) {
            throw e;
        } catch (Exception e) {
            throw new IOException("解包失败 " + name + ": " + e, e);
        }
    }

    private static void copyDir(ZipOutputStream zos, CountingOutputStream cos, String name) throws IOException {
        ZipEntry e = new ZipEntry(name);
        e.setMethod(ZipEntry.STORED);
        e.setSize(0); e.setCompressedSize(0); e.setCrc(0);
        zos.putNextEntry(e);
        zos.closeEntry();
    }

    private static void copyEntry(ZipOutputStream zos, CountingOutputStream cos, String name, InputStream in, boolean stored, long time) throws IOException {
        if (!stored) {
            ZipEntry e = new ZipEntry(name);
            e.setMethod(ZipEntry.DEFLATED);
            if (time > 0) e.setTime(time);
            zos.putNextEntry(e);
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) zos.write(buf, 0, n);
            zos.closeEntry();
            return;
        }
        byte[] data = readAll(in);
        writeBytes(zos, cos, name, data, true);
    }

    /** 写一个 STORED 条目，并按 4 字节对齐补 extra（resources.arsc 必须是 STORED+对齐）。 */
    private static void writeBytes(ZipOutputStream zos, CountingOutputStream cos, String name, byte[] data, boolean stored) throws IOException {
        ZipEntry e = new ZipEntry(name);
        if (!stored) {
            e.setMethod(ZipEntry.DEFLATED);
            zos.putNextEntry(e);
            zos.write(data);
            zos.closeEntry();
            return;
        }
        CRC32 crc = new CRC32();
        crc.update(data);
        e.setMethod(ZipEntry.STORED);
        e.setSize(data.length);
        e.setCompressedSize(data.length);
        e.setCrc(crc.getValue());
        e.setExtra(padding(cos.count + name.getBytes("UTF-8").length));
        zos.putNextEntry(e);
        zos.write(data);
        zos.closeEntry();
    }

    /** 计算 4 字节对齐所需的 extra 字段（含 4 字节头）。 */
    private static byte[] padding(long headerStart) {
        long base = headerStart + 30; // 不含 extra 的本地头
        int pad = (int) ((4 - (base % 4)) % 4);
        if (pad == 0) return new byte[0];
        if (pad < 4) pad += 4;
        byte[] ex = new byte[pad];
        ex[0] = (byte) 0x35; ex[1] = (byte) 0xD9;
        ex[2] = (byte) (pad - 4); ex[3] = 0;
        return ex;
    }

    /** 用内置 keystore 重签。 */
    public static void sign(Context ctx, File in, File out, Log log) throws Exception {
        byte[] keyDer = readAsset(ctx, "sfsmp.key");
        byte[] crtDer = readAsset(ctx, "sfsmp.crt");
        PrivateKey key = java.security.KeyFactory.getInstance("RSA")
                .generatePrivate(new java.security.spec.PKCS8EncodedKeySpec(keyDer));
        X509Certificate cert = (X509Certificate) java.security.cert.CertificateFactory.getInstance("X.509")
                .generateCertificate(new java.io.ByteArrayInputStream(crtDer));
        ApkSigner.SignerConfig sc = new ApkSigner.SignerConfig.Builder("SFSMP", key, Collections.singletonList(cert)).build();
        log.line("签名中…");
        new ApkSigner.Builder(Collections.singletonList(sc))
                .setInputApk(in)
                .setOutputApk(out)
                .setV1SigningEnabled(true)
                .setV2SigningEnabled(true)
                .setMinSdkVersion(26)
                .build()
                .sign();
        log.line("签名完成: " + out.length() + " B");
    }

    public static final class CountingOutputStream extends FilterOutputStream {
        public long count = 0;
        public CountingOutputStream(OutputStream out) { super(out); }
        @Override public void write(int b) throws IOException { out.write(b); count++; }
        @Override public void write(byte[] b, int off, int len) throws IOException { out.write(b, off, len); count += len; }
    }
}
