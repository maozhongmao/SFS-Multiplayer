// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import java.nio.charset.StandardCharsets;

/** resources.arsc 的最小改写：只动 ResTable_package 的 128 字符包名字段（定长，不改长度）。 */
public final class Arsc {

    private Arsc() {}

    private static int u16(byte[] b, int o) { return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8); }

    private static int u32(byte[] b, int o) {
        return (b[o] & 0xFF) | ((b[o + 1] & 0xFF) << 8) | ((b[o + 2] & 0xFF) << 16) | ((b[o + 3] & 0xFF) << 24);
    }

    /**
     * 把 resources.arsc 里的包名改成 newPkg。
     * 结构：ResTable_header(12) 之后逐个 chunk，找到 type=0x0200 的 ResTable_package，
     * 其 +12 处是 char16 name[128]（256 字节 UTF-16LE，尾部补 0）。
     * @return 是否改成功
     */
    public static boolean renamePackage(byte[] b, String newPkg) {
        if (b == null || b.length < 12 || newPkg == null) return false;
        if (u16(b, 0) != 0x0002) return false;
        byte[] name = newPkg.getBytes(StandardCharsets.UTF_16LE);
        if (name.length > 255) return false;
        int off = 12;
        while (off + 8 <= b.length) {
            int type = u16(b, off);
            int size = u32(b, off + 4);
            if (size <= 0 || off + size > b.length) break;
            if (type == 0x0200) {
                for (int i = 0; i < 256; i++) b[off + 12 + i] = 0;
                System.arraycopy(name, 0, b, off + 12, name.length);
                return true;
            }
            off += size;
        }
        return false;
    }

    /** 读当前包名，用于校验。 */
    public static String readPackage(byte[] b) {
        if (b == null || b.length < 12 || u16(b, 0) != 0x0002) return null;
        int off = 12;
        while (off + 8 <= b.length) {
            int type = u16(b, off);
            int size = u32(b, off + 4);
            if (size <= 0 || off + size > b.length) break;
            if (type == 0x0200) {
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < 128; i++) {
                    int c = u16(b, off + 12 + i * 2);
                    if (c == 0) break;
                    sb.append((char) c);
                }
                return sb.toString();
            }
            off += size;
        }
        return null;
    }
}
