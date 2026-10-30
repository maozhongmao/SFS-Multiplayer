// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.os.Build;
import android.os.Process;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.Set;

/**
 * v131 底层诊断日志：/sdcard/Android/media/<包名>/log.txt
 *
 * 设计目标（用户要求「从应用底层开始记录」）：
 *  1. 每条都带 时间戳 + 线程名 + tag，能看出卡在哪个线程；
 *  2. 所有异常都打**完整堆栈**（t()），不再只有 e.toString()；
 *  3. 进程级未被捕获异常（FATAL）也会落盘后再交给系统；
 *  4. 注册 PackageInstaller.SessionCallback，记录会话全生命周期（created/active/finished）；
 *  5. 启动时把环境一次性写清（型号/系统/ABI/权限状态/系统安装器 Intent 是否可解析）。
 * 该 vivo 机型会吞 logcat，所以 logcat 只作为附加，文件是唯一可靠出口。
 */
public final class FileLog {
    private static final Object LOCK = new Object();
    private static final String TAG = "sfsmp";
    private static File file;
    private static boolean inited;
    private static Thread.UncaughtExceptionHandler prevHandler;

    public static synchronized void init(Context ctx) {
        if (inited) return;
        inited = true;
        try {
            File dir = null;
            File[] media = ctx.getExternalMediaDirs();
            if (media != null && media.length > 0) dir = media[0];
            if (dir == null) dir = new File("/sdcard/Android/media/" + ctx.getPackageName());
            dir.mkdirs();
            File f = new File(dir, "log.txt");
            if (f.isFile() && f.length() > 1024 * 1024) {
                File old = new File(dir, "log.old.txt");
                old.delete();
                f.renameTo(old);
            }
            file = f;
            line("BOOT", "pid=" + Process.myPid() + " model=" + Build.MODEL + " brand=" + Build.BRAND
                    + " sdk=" + Build.VERSION.SDK_INT + " abis=" + join(Build.SUPPORTED_ABIS));
            try {
                line("BOOT", "patcher=" + ctx.getPackageManager()
                        .getPackageInfo(ctx.getPackageName(), 0).versionName);
            } catch (Throwable t) { t("BOOT", t); }
            line("BOOT", "file=" + f.getAbsolutePath());
            bootEnv(ctx);
            installCrashHandler();
            watchSessions(ctx);
        } catch (Throwable t) {
            android.util.Log.e(TAG, "FileLog.init failed", t);
        }
    }

    /** 环境快照：权限、系统安装器可用性、目录可写性 —— 出问题时第一时间能排除环境因素。 */
    private static void bootEnv(Context ctx) {
        try {
            level("ENV", "canRequestPackageInstalls=" + ctx.getPackageManager().canRequestPackageInstalls());
        } catch (Throwable t) { t("ENV", t); }
        try {
            if (Build.VERSION.SDK_INT >= 30) {
                level("ENV", "isExternalStorageManager=" + android.os.Environment.isExternalStorageManager());
            }
        } catch (Throwable t) { t("ENV", t); }
        try {
            Intent i = new Intent(Intent.ACTION_INSTALL_PACKAGE);
            i.setType("application/vnd.android.package-archive");
            level("ENV", "ACTION_INSTALL_PACKAGE resolvers="
                    + ctx.getPackageManager().queryIntentActivities(i, 0).size());
        } catch (Throwable t) { t("ENV", t); }
        try {
            File probe = new File("/sdcard/Android/media/" + ctx.getPackageName(), ".wtest");
            FileOutputStream fo = new FileOutputStream(probe);
            fo.write(1);
            fo.close();
            probe.delete();
            level("ENV", "media dir writable=yes");
        } catch (Throwable t) { t("ENV", t); }
    }

    /** 进程级未捕获异常：完整堆栈落盘后再交回系统，避免「闪退但没日志」。 */
    private static void installCrashHandler() {
        prevHandler = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler(new Thread.UncaughtExceptionHandler() {
            @Override public void uncaughtException(Thread th, Throwable e) {
                try {
                    line("FATAL", "thread=" + (th == null ? "?" : th.getName()));
                    t("FATAL", e);
                } catch (Throwable ignored) { }
                if (prevHandler != null) prevHandler.uncaughtException(th, e);
            }
        });
    }

    /** 安装会话全生命周期 —— 之前「点了没反应」就是这一段完全看不到。 */
    private static void watchSessions(Context ctx) {
        try {
            final PackageInstaller pi = ctx.getPackageManager().getPackageInstaller();
            pi.registerSessionCallback(new PackageInstaller.SessionCallback() {
                @Override public void onCreated(int id) { level("SESSION", "onCreated id=" + id); }
                @Override public void onBadgingChanged(int id) { level("SESSION", "onBadgingChanged id=" + id); }
                @Override public void onActiveChanged(int id, boolean active) {
                    level("SESSION", "onActiveChanged id=" + id + " active=" + active);
                }
                @Override public void onProgressChanged(int id, float progress) { }
                @Override public void onFinished(int id, boolean success) {
                    level("SESSION", "onFinished id=" + id + " success=" + success);
                }
            });
            /* v133：清理自己遗留的僵尸会话。
             * 之前每次安装失败都会留下一个 progress=0.8 的会话，实测堆积到 41 个，
             * 会拖慢甚至干扰后续安装（adb 也无法代我们 abandon，必须本进程做）。 */
            int abandoned = 0;
            for (PackageInstaller.SessionInfo si : pi.getMySessions()) {
                level("SESSION", "existing id=" + si.getSessionId()
                        + " pkg=" + si.getAppPackageName() + " progress=" + si.getProgress());
                try {
                    PackageInstaller.Session stale = pi.openSession(si.getSessionId());
                    stale.abandon();
                    abandoned++;
                } catch (Throwable t) { t("SESSION:abandon", t); }
            }
            if (abandoned > 0) level("SESSION", "abandoned " + abandoned + " stale session(s)");
        } catch (Throwable t) { t("SESSION", t); }
    }

    public static String path() { return file == null ? "(未初始化)" : file.getAbsolutePath(); }

    public static void line(String s) { line("UI", s); }

    public static void line(String tag, String s) {
        String ts = new SimpleDateFormat("MM-dd HH:mm:ss.SSS", Locale.US).format(new Date());
        String th = Thread.currentThread().getName();
        String out = ts + " [" + th + "] " + tag + ": " + s + "\n";
        android.util.Log.i(TAG, tag + ": " + s);
        if (file == null) return;
        synchronized (LOCK) {
            try {
                FileOutputStream fo = new FileOutputStream(file, true);
                fo.write(out.getBytes("UTF-8"));
                fo.flush();
                fo.close();
            } catch (Throwable t) { }
        }
    }

    /** 异常：消息 + 完整堆栈（每一帧一行，带 4 空格缩进）。 */
    public static void t(String tag, Throwable e) {
        if (e == null) return;
        StringWriter sw = new StringWriter();
        e.printStackTrace(new PrintWriter(sw));
        for (String s : sw.toString().split("\n")) line(tag, "  " + s);
    }

    /** Intent 全量 extras 转储（安装结果、确认 Intent 都在这里面）。 */
    public static void extras(String tag, Intent i) {
        if (i == null) { line(tag, "intent=null"); return; }
        line(tag, "action=" + i.getAction() + " component=" + i.getComponent() + " flags=" + i.getFlags());
        try {
            Set<String> keys = i.getExtras() == null ? null : i.getExtras().keySet();
            if (keys == null) { line(tag, "extras=(none)"); return; }
            for (String k : keys) {
                Object v;
                try { v = i.getExtras().get(k); } catch (Throwable t) { v = "<get failed>"; }
                line(tag, "  " + k + " = " + v);
            }
        } catch (Throwable t) { t(tag, t); }
    }

    private static void level(String tag, String s) { line(tag, s); }

    private static String join(String[] a) {
        if (a == null || a.length == 0) return "?";
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < a.length; i++) { if (i > 0) sb.append(','); sb.append(a[i]); }
        return sb.toString();
    }
}
