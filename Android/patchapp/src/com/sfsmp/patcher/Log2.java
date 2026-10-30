// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

/** 兼容旧调用点：统一转发到 FileLog（底层诊断日志）。 */
final class Log2 {
    private Log2() {}
    static void w(String s) { FileLog.line("sys", s); }
}
