// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.bridge;

import android.app.Activity;
import android.os.Build;
import android.os.Process;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

/**
 * 补丁版 SFS 的「Java 层」日志：/sdcard/Android/media/<包名>/log-java.txt
 * 与 native 层（v64.c 写的 log-native.txt）分开：两份文件可以对时间戳交叉比对
 * —— Java 层看生命周期/加载/异常，native 层看 hook/联机/崩溃栈。
 */
public final class JavaLog {
    private static final Object LOCK = new Object();
    private static File file;
    private static boolean inited;
    private static Thread.UncaughtExceptionHandler prev;

    public static synchronized void init(Activity a) {
        if (inited || a == null) return;
        inited = true;
        try {
            File dir = new File("/sdcard/Android/media/" + a.getPackageName());
            dir.mkdirs();
            File f = new File(dir, "log-java.txt");
            if (f.isFile() && f.length() > 512 * 1024) {
                File old = new File(dir, "log-java.old.txt");
                old.delete();
                f.renameTo(old);
            }
            file = f;
            line("JAVA", "boot pid=" + Process.myPid() + " model=" + Build.MODEL
                    + " sdk=" + Build.VERSION.SDK_INT + " pkg=" + a.getPackageName()
                    + " activity=" + a.getClass().getName());
            prev = Thread.getDefaultUncaughtExceptionHandler();
            Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
                @Override public void uncaughtException(Thread th, Throwable e) {
                    line("FATAL", "thread=" + (th == null ? "?" : th.getName()));
                    t("FATAL", e);
                    if (prev != null) prev.uncaughtException(th, e);
                }
            });
        } catch (Throwable t) { /* 日志失败不影响游戏 */ }
    }

    public static String path() { return file == null ? "(未初始化)" : file.getAbsolutePath(); }

    public static void line(String tag, String s) {
        String ts = new SimpleDateFormat("MM-dd HH:mm:ss.SSS", Locale.US).format(new Date());
        String out = ts + " [" + Thread.currentThread().getName() + "] " + tag + ": " + s + "\n";
        if (file == null) { android.util.Log.i("sfsmp-java", tag + ": " + s); return; }
        synchronized (LOCK) {
            try {
                FileOutputStream fo = new FileOutputStream(file, true);
                fo.write(out.getBytes("UTF-8"));
                fo.flush();
                fo.close();
            } catch (Throwable t) { }
        }
    }

    public static void t(String tag, Throwable e) {
        if (e == null) return;
        StringWriter sw = new StringWriter();
        e.printStackTrace(new PrintWriter(sw));
        for (String s : sw.toString().split("\n")) line(tag, "  " + s);
    }
}
