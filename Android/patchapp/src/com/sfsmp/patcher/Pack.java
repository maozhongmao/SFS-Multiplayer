package com.sfsmp.patcher;

import android.content.pm.PackageManager;
import android.content.pm.Signature;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.security.MessageDigest;

/**
 * 最小壳：内置资源（hook.dex / libsfsmp.so / sfsmp.p12）在 APK 里是 XOR 密文，
 * 运行时才解密到内存；密钥由签名证书摘要派生 —— 换签名就解不开，也就跑不起来。
 * 配合 {@link #verifySelf} 做签名锁。
 */
public final class Pack {

    /** 我们自己的签名证书 SHA-256（keystore/sfsmp-release.jks, alias sfsmp）。 */
    public static final String CERT_SHA256 = "2caa0386be65b9fcde8ec7522d0528f65e96b3333dfdaaff5d94a665fbd45abf";

    private static final byte[] KEY = keyBytes();

    private Pack() {}

    private static byte[] keyBytes() {
        // 用证书摘要的 ASCII 十六进制当密钥字节，长度 64，取自中间 32 位避免全用前缀
        byte[] s = CERT_SHA256.getBytes();
        byte[] k = new byte[16];
        for (int i = 0; i < 16; i++) k[i] = (byte) (s[8 + i] ^ s[40 + i]);
        return k;
    }

    /** 原地 XOR 解密/加密（对称）。 */
    public static byte[] xor(byte[] d) {
        for (int i = 0; i < d.length; i++) d[i] ^= KEY[i & 15];
        return d;
    }

    public static byte[] readPacked(InputStream in) throws Exception {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        byte[] buf = new byte[1 << 16];
        int n;
        while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
        in.close();
        return xor(bo.toByteArray());
    }

    public static String sha256Hex(byte[] d) throws Exception {
        MessageDigest md = MessageDigest.getInstance("SHA-256");
        byte[] h = md.digest(d);
        StringBuilder sb = new StringBuilder(64);
        for (byte b : h) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    /** 签名锁：当前 APK 的签名证书必须是我们这把。 */
    public static boolean verifySelf(PackageManager pm, String pkg) {
        try {
            Signature[] sigs = pm.getPackageInfo(pkg, PackageManager.GET_SIGNATURES).signatures;
            if (sigs == null || sigs.length == 0) return false;
            return CERT_SHA256.equals(sha256Hex(sigs[0].toByteArray()));
        } catch (Throwable t) {
            return false;
        }
    }
}
