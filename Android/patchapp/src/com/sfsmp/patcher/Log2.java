// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import android.content.Context;

import java.io.File;
import java.io.FileWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/** 该 vivo 机型吞 logcat，统一写文件。 */
final class Log2 {
    private Log2() {}
    static void w(String s) {
        try {
            File f = new File("/sdcard/Android/data/com.sfsmp.patcher/files/patcher.log");
            File p = f.getParentFile();
            if (p != null) p.mkdirs();
            FileWriter w = new FileWriter(f, true);
            w.write(new SimpleDateFormat("HH:mm:ss", Locale.US).format(new Date()) + " [sys] " + s + "\n");
            w.close();
        } catch (Throwable t) { /* ignore */ }
    }
}
