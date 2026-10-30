// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import android.content.Context;
import android.os.Build;

import com.android.apksig.ApkSigner;

import java.io.*;
import java.security.KeyFactory;
import java.security.PrivateKey;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.security.spec.PKCS8EncodedKeySpec;
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
 *   3) lib/<设备ABI>/libsfsmp.so：联机 mod 本体（只放当前设备那一个 ABI）
 * 其余条目：raw copy 原样搬运压缩流（不解压不重压），resources.arsc 保持 STORED + 4 字节对齐。
 */
public final class ApkPatcher {

    public interface Log { void line(String s); }

    public static final String HOOK_ACTIVITY = "com.sfsmp.patch.HookActivity";
    public static final String OLD_PKG = "com.StefMorojna.SpaceflightSimulator";
    public static final String COEXIST_PKG = "com.sfsmp.sfs";

    private static final Pattern DEX = Pattern.compile("^classes(\\d*)\\.dex$");

    public static byte[] readAll(InputStream in) throws IOException {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        byte[] buf = new byte[1 << 16];
        int n;
        while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
        return bo.toByteArray();
    }

    /** 当前设备主 ABI（只支持 ARM 两种；官方包本身也只有这两个）。 */
    public static String deviceAbi() {
        String a = Build.SUPPORTED_ABIS.length > 0 ? Build.SUPPORTED_ABIS[0] : "";
        if ("arm64-v8a".equals(a) || "armeabi-v7a".equals(a)) return a;
        return a;
    }

    public static boolean abiSupported(String abi) {
        return "arm64-v8a".equals(abi) || "armeabi-v7a".equals(abi);
    }

    /** 拆分发包：assets/abi.txt 写明本安装器只带哪个架构的注入库；both 版没有这个文件，返回 null。 */
    public static String installerAbi(Context ctx) {
        try {
            String s = new String(readAsset(ctx, "abi.txt"), "UTF-8").trim();
            if (abiSupported(s)) return s;
        } catch (Throwable ignored) { }
        return null;
    }

    private static String soAssetFor(String abi) {
        return "arm64-v8a".equals(abi) ? "libsfsmp-arm64.so" : "libsfsmp-armv7.so";
    }

    public static int buildUnsigned(Context ctx, File officialApk, File out, String oldLauncher, int newVersionCode, Log log) throws Exception {
        return buildUnsigned(ctx, officialApk, out, oldLauncher, newVersionCode, null, log);
    }

    public static int buildUnsigned(Context ctx, File officialApk, File out, String oldLauncher, int newVersionCode, String newPackage, Log log) throws Exception {
        if (!officialApk.exists()) throw new IOException("找不到官方 APK: " + officialApk);
        String abi = deviceAbi();
        if (!abiSupported(abi)) throw new IOException("不支持的设备架构: " + abi + "（官方包不含该架构的原生库，装了也起不来）");
        String want = installerAbi(ctx);
        if (want != null && !want.equals(abi))
            throw new IOException("这个安装器是 " + want + " 版，本机是 " + abi + "，请下载对应架构的安装器");
        byte[] hookDex = readAsset(ctx, "hook.dex");
        byte[] so = readAsset(ctx, soAssetFor(abi));
        String libEntry = "lib/" + abi + "/libsfsmp.so";
        log.line("设备 ABI: " + abi + " → 只打这一个包（丢弃其它指令集）");
        log.line("内置资源: hook.dex " + hookDex.length + " B, " + soAssetFor(abi) + " " + so.length + " B");

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

            Map<String, byte[]> replace = new LinkedHashMap<String, byte[]>();
            replace.put("AndroidManifest.xml", patched);
            if (newPackage != null) {
                ZipEntry ae = zf.getEntry("resources.arsc");
                if (ae == null) throw new IOException("官方包里没有 resources.arsc");
                byte[] ar = readAll(zf.getInputStream(ae));
                if (!Arsc.renamePackage(ar, newPackage)) throw new IOException("resources.arsc 改包名失败");
                replace.put("resources.arsc", ar);
                log.line("arsc: 包名 -> " + Arsc.readPackage(ar));
            }

            final String keepPrefix = "lib/" + abi + "/";
            ZipRaw.Skip skip = new ZipRaw.Skip() {
                public boolean skip(String name) {
                    if (name.equals("AndroidManifest.xml")) return true;
                    if (name.startsWith("lib/")) {
                        if (!name.startsWith(keepPrefix)) return true;          /* 其它指令集整个丢掉 */
                        if (name.endsWith("/libsfsmp.so")) return true;        /* 旧的我们自己的库，等会儿重写 */
                    }
                    return false;
                }
            };
            List<ZipRaw.Add> adds = new ArrayList<ZipRaw.Add>();
            adds.add(new ZipRaw.Add(newDex, hookDex, false));
            adds.add(new ZipRaw.Add(libEntry, so, false));

            File parent = out.getParentFile();
            if (parent != null) parent.mkdirs();
            long t0 = System.currentTimeMillis();
            try {
                ZipRaw.build(officialApk, out, replace, skip, adds, new ZipRaw.Log() { public void line(String s) { log.line(s); } });
                log.line("raw copy 用时 " + (System.currentTimeMillis() - t0) + " ms");
            } catch (Exception zre) {
                log.line("raw copy 失败(" + zre + ")，回退到重压模式（会很慢）");
                buildLegacy(zf, out, replace, newDex, hookDex, libEntry, so, skip, log);
            }
            return newVersionCode;
        } finally { zf.close(); }
    }

    /** 旧路径：逐条解压再压缩（慢），仅在 raw copy 失败时兜底。 */
    private static void buildLegacy(ZipFile zf, File out, Map<String, byte[]> replace, String newDex,
                                    byte[] hookDex, String libEntry, byte[] so, ZipRaw.Skip skip, Log log) throws IOException {
        CountingOutputStream cos = new CountingOutputStream(new BufferedOutputStream(new FileOutputStream(out), 1 << 16));
        ZipOutputStream zos = new ZipOutputStream(cos);
        zos.setLevel(6);
        try {
            Enumeration<? extends ZipEntry> en = zf.entries();
            while (en.hasMoreElements()) {
                ZipEntry e = en.nextElement();
                String name = e.getName();
                if (replace.containsKey(name) || skip.skip(name)) continue;
                InputStream in = zf.getInputStream(e);
                try { copyEntry(zos, cos, name, in, e.getMethod() == ZipEntry.STORED, e.getTime()); }
                finally { in.close(); }
            }
            for (Map.Entry<String, byte[]> kv : replace.entrySet()) writeBytes(zos, cos, kv.getKey(), kv.getValue(), false);
            writeBytes(zos, cos, newDex, hookDex, false);
            writeBytes(zos, cos, libEntry, so, false);
        } finally { zos.close(); }
    }

    /** assets 里是明文（不加壳、不加密）。 */
    private static byte[] readAsset(Context ctx, String name) throws IOException {
        InputStream in = ctx.getAssets().open(name);
        try { return readAll(in); } finally { in.close(); }
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
        writeBytes(zos, cos, name, readAll(in), true);
    }

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

    private static byte[] padding(long headerStart) {
        long base = headerStart + 30;
        int pad = (int) ((4 - (base % 4)) % 4);
        if (pad == 0) return new byte[0];
        if (pad < 4) pad += 4;
        byte[] ex = new byte[pad];
        ex[0] = (byte) 0x35; ex[1] = (byte) 0xD9; ex[2] = (byte) (pad - 4); ex[3] = 0;
        return ex;
    }

    /** 用内置 keystore 重签（v2；minSdk 26 不需要 v1）。 */
    public static void sign(Context ctx, File in, File out, Log log) throws Exception {
        byte[] keyDer = readAsset(ctx, "sfsmp.key");
        byte[] crtDer = readAsset(ctx, "sfsmp.crt");
        PrivateKey key = KeyFactory.getInstance("RSA").generatePrivate(new PKCS8EncodedKeySpec(keyDer));
        X509Certificate cert = (X509Certificate) CertificateFactory.getInstance("X.509")
                .generateCertificate(new ByteArrayInputStream(crtDer));
        ApkSigner.SignerConfig sc = new ApkSigner.SignerConfig.Builder("SFSMP", key, Collections.singletonList(cert)).build();
        log.line("签名中…");
        long t0 = System.currentTimeMillis();
        new ApkSigner.Builder(Collections.singletonList(sc))
                .setInputApk(in)
                .setOutputApk(out)
                .setV1SigningEnabled(false)
                .setV2SigningEnabled(true)
                .setMinSdkVersion(26)
                .build()
                .sign();
        log.line("签名完成: " + out.length() + " B（" + (System.currentTimeMillis() - t0) + " ms）");
    }

    public static final class CountingOutputStream extends FilterOutputStream {
        public long count = 0;
        public CountingOutputStream(OutputStream out) { super(out); }
        @Override public void write(int b) throws IOException { out.write(b); count++; }
        @Override public void write(byte[] b, int off, int len) throws IOException { out.write(b, off, len); count += len; }
    }
}
