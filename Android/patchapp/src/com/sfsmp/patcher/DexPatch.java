// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import java.nio.charset.StandardCharsets;

/**
 * v134：底座 APK 的 dex 里残留着早期的 System.loadLibrary("SFSMPTrampoline")。
 * 该 so 在部分 ROM（本机 vivo）上 dlopen 会 ETIMEDOUT，导致补丁版一启动就崩。
 *
 * dex 的 string_data_item = uleb128(UTF-16 长度) + MUTF-8 字节 + 0x00。
 * 因此改写必须同时改**长度前缀**，否则读出来的字符串会带 0 字节、dex 结构被破坏
 * （实测：只改内容不改长度 → 启动时报 ContentProvider ClassNotFoundException）。
 * 改短后剩余的旧字节留成 0，属于死区，不影响其它 string_data（各条目靠 string_ids 里的偏移独立寻址）。
 */
public final class DexPatch {

    /** 把 dex 里的 from 改写成 to（to 不能更长）；成功返回命中次数。 */
    public static int renameString(byte[] data, String from, String to) {
        byte[] f = from.getBytes(StandardCharsets.UTF_8);
        byte[] t = to.getBytes(StandardCharsets.UTF_8);
        if (t.length > f.length) throw new IllegalArgumentException("new name longer than old");
        int hits = 0;
        for (int i = 1; i + f.length + 1 <= data.length; i++) {
            /* 前一个字节必须是 uleb128 长度（单字节编码：< 0x80） */
            if ((data[i - 1] & 0xFF) != f.length) continue;
            boolean match = true;
            for (int j = 0; j < f.length; j++) {
                if (data[i + j] != f[j]) { match = false; break; }
            }
            if (!match) continue;
            if (data[i + f.length] != 0) continue;      /* 必须紧跟 0x00 终止符 */
            data[i - 1] = (byte) t.length;              /* 长度前缀同步改短 */
            for (int j = 0; j < f.length; j++) data[i + j] = (j < t.length) ? t[j] : (byte) 0;
            hits++;
        }
        return hits;
    }
    /** dex 头里有 SHA-1 签名（偏移 12..31）和 adler32 校验（8..11）；改字节后必须重算，否则整份 dex 被 ART 丢弃。 */
    public static void fixChecksums(byte[] dex) {
        try {
            java.security.MessageDigest md = java.security.MessageDigest.getInstance("SHA-1");
            md.update(dex, 32, dex.length - 32);
            byte[] sha = md.digest();
            System.arraycopy(sha, 0, dex, 12, 20);
            java.util.zip.Adler32 ad = new java.util.zip.Adler32();
            ad.update(dex, 12, dex.length - 12);
            long v = ad.getValue();
            dex[8] = (byte) v;
            dex[9] = (byte) (v >>> 8);
            dex[10] = (byte) (v >>> 16);
            dex[11] = (byte) (v >>> 24);
        } catch (Exception e) {
            throw new RuntimeException(e);
        }
    }
}
