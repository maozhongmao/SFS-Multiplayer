// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import android.app.Activity;
import android.widget.ProgressBar;
import android.app.AlertDialog;
import android.content.pm.ApplicationInfo;
import android.app.PendingIntent;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class MainActivity extends Activity {

    static final String GAME = "com.StefMorojna.SpaceflightSimulator";

    final Handler ui = new Handler(Looper.getMainLooper());
    TextView status;
    TextView log;
    ScrollView logScroll;
    Button btnDetect, btnBackup, btnBuild, btnInstall, btnPick;
    ProgressBar pbBar;
    File lastSource; int lastVc; String lastLauncher;
    CheckBox cbCoexist;
    File lastPatch;

    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        FileLog.init(this);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(Color.parseColor("#121212"));
        root.setPadding(dp(20), dp(28), dp(20), dp(16));

        TextView title = new TextView(this);
        title.setText("SFS 联机补丁");
        title.setTextColor(Color.WHITE);
        title.setTextSize(24);
        title.setTypeface(title.getTypeface(), android.graphics.Typeface.BOLD);
        root.addView(title);

        TextView sub = new TextView(this);
        sub.setText("补丁安装器 · v" + selfVersion());
        sub.setTextColor(Color.parseColor("#9E9E9E"));
        sub.setTextSize(12);
        sub.setPadding(0, dp(2), 0, dp(10));
        root.addView(sub);


        status = new TextView(this);
        status.setTextColor(Color.parseColor("#E0E0E0"));
        status.setTextSize(12);
        status.setBackgroundColor(Color.parseColor("#1E1E1E"));
        status.setPadding(dp(12), dp(10), dp(12), dp(10));
        status.setText("正在读取游戏信息…");
        root.addView(status);

        cbCoexist = new CheckBox(this);
        cbCoexist.setText("共存模式（包名改为 " + ApkPatcher.COEXIST_PKG + "，不覆盖官方）");
        cbCoexist.setTextSize(13);
        cbCoexist.setChecked(true);
        cbCoexist.setTextColor(Color.parseColor("#B0BEC5"));
        root.addView(cbCoexist);

        btnDetect = button("检测游戏");
        btnBackup = button("① 备份存档");
        btnBuild = button("② 生成补丁包");
        btnInstall = button("③ 安装补丁包");
        pbBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        pbBar.setMax(100); pbBar.setProgress(0);
        root.addView(pbBar, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, dp(6)));
        root.addView(btnBackup);
        btnPick = button("选择本地安装包");
        root.addView(btnPick);
        btnPick.setVisibility(View.GONE);
        btnPick.setOnClickListener(new View.OnClickListener() { public void onClick(View v) { pickFile(); } });
        root.addView(btnBuild);
        root.addView(btnInstall);

        log = new TextView(this);
        log.setTextColor(Color.parseColor("#B0BEC5"));
        log.setTextSize(11);
        log.setPadding(dp(6), dp(10), dp(6), dp(6));
        log.setText("");
        logScroll = new ScrollView(this);
        logScroll.addView(log);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(150));
        lp.topMargin = dp(8);
        root.addView(logScroll, lp);

        ScrollView page = new ScrollView(this);
        page.setBackgroundColor(Color.parseColor("#121212"));
        page.addView(root);
        setContentView(page);

        btnDetect.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { detect(); }
        });
        btnBackup.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { doBackup(); }
        });
        btnBuild.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { doBuild(); }
        });
        btnInstall.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { doInstall(); }
        });

        detect();
        String auto = getIntent() == null ? null : getIntent().getStringExtra("auto");
        if (auto != null) { say("auto=" + auto); if ("build".equals(auto)) doBuild(); else if ("install".equals(auto)) doInstall(); }
    }

    Button button(String text) {
        Button b = new Button(this);
        b.setText(text);
        b.setAllCaps(false);
        b.setTextSize(16);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(44));
        lp.topMargin = dp(8);
        b.setLayoutParams(lp);
        return b;
    }

    int dp(int v) { return (int) (v * getResources().getDisplayMetrics().density); }

    String selfVersion() {
        try { return getPackageManager().getPackageInfo(getPackageName(), 0).versionName; }
        catch (Throwable t) { return "?"; }
    }

    void pg(final int v) { ui.post(new Runnable() { public void run() { if (pbBar != null) pbBar.setProgress(v); } }); }

    void say(final String s) {
        FileLog.line("UI", s);
        ui.post(new Runnable() { public void run() {
            log.append(s + "\n");
            logScroll.post(new Runnable() { public void run() { logScroll.fullScroll(View.FOCUS_DOWN); } });
        } });
    }

    /** 统一失败出口：完整堆栈写文件，界面只留一行。 */
    void fail(String what, Throwable t) {
        FileLog.line("FAIL", what);
        FileLog.t("FAIL:" + what, t);
        say(what + ": " + t);
    }

    PackageInfo gameInfo() {
        try { return getPackageManager().getPackageInfo(GAME, 0); } catch (Throwable t) { return null; }
    }

    String gameLauncher() {
        try {
            Intent i = getPackageManager().getLaunchIntentForPackage(GAME);
            if (i == null) return null;
            return i.getComponent().getClassName();
        } catch (Throwable t) { return null; }
    }

    void detect() {
        PackageInfo pi = gameInfo();
        if (pi == null) {
            status.setText("游戏：未安装\n\n请安装官方 Spaceflight Simulator，或点下面的「选择本地安装包」。");
            say("未找到游戏包 " + GAME);
            if (btnPick != null) btnPick.setVisibility(View.VISIBLE);
            return;
        }
        if (btnPick != null) btnPick.setVisibility(View.GONE);
        ApplicationInfo ai = pi.applicationInfo;
        File apk = new File(ai.sourceDir);
        StringBuilder sb = new StringBuilder();
        sb.append("游戏：已安装\n");
        sb.append("版本：").append(pi.versionName).append(" (").append(pi.versionCode).append(")\n");
        sb.append("启动类：").append(String.valueOf(gameLauncher())).append("\n");
        sb.append("路径：").append(ai.sourceDir).append("\n");
        sb.append("大小：").append(apk.length() / (1024 * 1024)).append(" MB\n");
        sb.append("架构：").append(Build.SUPPORTED_ABIS[0]).append("\n");
        sb.append("日志：").append(FileLog.path());
        status.setText(sb.toString());
        say("检测到游戏 " + pi.versionName + " (" + pi.versionCode + ")");
    }

    void doBackup() {
        final File src = new File("/sdcard/Android/data/" + GAME);
        final File dst = new File("/sdcard/SFSMP-backup/" + new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date()));
        if (!hasAllFiles()) { askAllFiles(); return; }
        new Thread(new Runnable() { public void run() {
            try {
                long n = copyTree(src, dst);
                say("备份完成: " + dst.getAbsolutePath() + " (" + (n / 1024 / 1024) + " MB)");
            } catch (Throwable t) { say("备份失败: " + t); }
        } }).start();
    }

    long copyTree(File src, File dst) throws Exception {
        if (!src.exists()) return 0;
        if (src.isFile()) {
            dst.getParentFile().mkdirs();
            InputStream in = new FileInputStream(src);
            OutputStream out = new FileOutputStream(dst);
            try {
                byte[] buf = new byte[1 << 16];
                long total = 0;
                int n;
                while ((n = in.read(buf)) > 0) { out.write(buf, 0, n); total += n; }
                return total;
            } finally { in.close(); out.close(); }
        }
        dst.mkdirs();
        long total = 0;
        File[] kids = src.listFiles();
        if (kids != null) for (File k : kids) total += copyTree(k, new File(dst, k.getName()));
        return total;
    }

    void pickApk() {
        final java.util.List<PackageInfo> apps = new java.util.ArrayList<PackageInfo>();
        final java.util.List<String> items = new java.util.ArrayList<String>();
        items.add("浏览本地 APK 文件…");
        try {
            for (PackageInfo pi : getPackageManager().getInstalledPackages(0)) {
                if (pi.applicationInfo == null) continue;
                if ((pi.applicationInfo.flags & ApplicationInfo.FLAG_SYSTEM) != 0) continue;
                if (getPackageName().equals(pi.packageName)) continue;
                apps.add(pi);
                items.add(getPackageManager().getApplicationLabel(pi.applicationInfo) + "  ·  " + pi.packageName);
            }
        } catch (Throwable t) { say("枚举应用失败: " + t); }
        new AlertDialog.Builder(this).setTitle("选择要打补丁的应用")
            .setItems(items.toArray(new String[0]), new android.content.DialogInterface.OnClickListener() {
                public void onClick(android.content.DialogInterface d, int which) {
                    if (which == 0) { pickFile(); return; }
                    PackageInfo pi = apps.get(which - 1);
                    String lc = null;
                    try { Intent i = getPackageManager().getLaunchIntentForPackage(pi.packageName); lc = i == null ? null : i.getComponent().getClassName(); } catch (Throwable ignored) {}
                    useSource(new File(pi.applicationInfo.sourceDir), pi.versionCode, lc, pi.packageName);
                }
            }).show();
    }

    void useSource(File apk, int vc, String launcher, String pkgLabel) {
        lastSource = apk; lastVc = vc; lastLauncher = launcher;
        say("已选来源: " + pkgLabel + " (" + vc + ")，启动类 " + launcher);
        String nl = String.valueOf((char) 10);
        status.setText("来源：" + pkgLabel + nl + "版本码：" + vc + nl + "大小：" + (apk.length() / 1048576) + " MB" + nl + "启动类：" + launcher + nl + "路径：" + apk.getAbsolutePath());
    }

    void pickFile() {
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        i.addCategory(Intent.CATEGORY_OPENABLE);
        i.setType("*/*");
        try { startActivityForResult(i, 7001); } catch (Throwable t) { say("打不开文件选择器: " + t); }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != 7001 || res != RESULT_OK || data == null || data.getData() == null) return;
        try {
            File dst = new File(getCacheDir(), "source.apk");
            InputStream in = getContentResolver().openInputStream(data.getData());
            FileOutputStream out = new FileOutputStream(dst);
            byte[] buf = new byte[1 << 16]; int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            in.close(); out.close();
            PackageInfo pi = getPackageManager().getPackageArchiveInfo(dst.getAbsolutePath(), PackageManager.GET_ACTIVITIES);
            if (pi == null) { say("这个 APK 解析不了（可能不是完整包）"); return; }
            String lc = (pi.activities != null && pi.activities.length > 0) ? pi.activities[0].name : null;
            lastSource = dst; lastVc = pi.versionCode; lastLauncher = lc;
            useSource(dst, pi.versionCode, lc, "本地 APK");
        } catch (Throwable t) { say("读取失败: " + t); }
    }

    void doBuild() {
        final PackageInfo pi = gameInfo();
        final File src = (lastSource != null && lastSource.isFile()) ? lastSource : (pi == null ? null : new File(pi.applicationInfo.sourceDir));
        final String launcher = (lastSource != null && lastSource.isFile()) ? lastLauncher : gameLauncher();
        final int baseVc = (lastSource != null && lastSource.isFile()) ? lastVc : (pi == null ? 0 : pi.versionCode);
        if (src == null || launcher == null) { say("先装官方游戏，或点「选择本地 APK」"); return; }
        btnBuild.setEnabled(false);
        new Thread(new Runnable() { public void run() {
            try {
                File cache = getCacheDir();
                File unsigned = new File(cache, "patched-unsigned.apk");
                File signed = new File(getExternalFilesDir(null), "SFS-联机补丁-" + baseVc + "-" + (baseVc + 1) + ".apk");
                say("开始生成补丁包…");
                pg(5);
                long t0 = System.currentTimeMillis();
                ApkPatcher.buildUnsigned(MainActivity.this, src, unsigned, launcher, baseVc + 1,
                        cbCoexist.isChecked() ? ApkPatcher.COEXIST_PKG : null, new ApkPatcher.Log() {
                    public void line(String s) { say(s); }
                });
                say("未签名包 " + unsigned.length() / 1048576 + " MB，用时 " + ((System.currentTimeMillis() - t0) / 1000) + " s");
                pg(55);
                ApkPatcher.sign(MainActivity.this, unsigned, signed, new ApkPatcher.Log() {
                    public void line(String s) { say(s); }
                });
                lastPatch = signed;
                say("完成: " + signed.getAbsolutePath());
                pg(90);
                ui.post(new Runnable() { public void run() { btnInstall.setEnabled(true); } });
            } catch (Throwable t) {
                say("生成失败: " + t);
            } finally {
                ui.post(new Runnable() { public void run() { btnBuild.setEnabled(true); } });
            }
        } }).start();
    }

    boolean hasAllFiles() {
        return Build.VERSION.SDK_INT < 30 || Environment.isExternalStorageManager();
    }

    void askAllFiles() {
        say("需要「所有文件访问」权限（备份/恢复存档要用），正在打开设置…");
        try {
            Intent i = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
            i.setData(Uri.parse("package:" + getPackageName()));
            startActivity(i);
        } catch (Throwable t) { say("打开设置失败: " + t); }
    }

    File findPatch() {
        File dir = getExternalFilesDir(null);
        if (dir == null) return null;
        File[] fs = dir.listFiles();
        File best = null;
        if (fs != null) for (File f : fs) {
            String n = f.getName();
            if (!n.startsWith("SFS-联机补丁-") || !n.endsWith(".apk")) continue;
            if (best == null || f.lastModified() > best.lastModified()) best = f;
        }
        return best;
    }

    boolean pkgInstalled(String pkg) {
        try { getPackageManager().getPackageInfo(pkg, 0); return true; } catch (Throwable t) { return false; }
    }

    boolean waitState(boolean installed, long timeoutMs) {
        return waitPkg(GAME, installed, timeoutMs);
    }

    boolean waitPkg(String pkg, boolean installed, long timeoutMs) {
        long end = System.currentTimeMillis() + timeoutMs;
        while (System.currentTimeMillis() < end) {
            if (pkgInstalled(pkg) == installed) return true;
            try { Thread.sleep(500); } catch (InterruptedException e) { return false; }
        }
        return false;
    }

    void installApk(File apk) throws Exception {
        FileLog.line("INSTALL", "apk=" + apk.getAbsolutePath() + " bytes=" + apk.length()
                + " exists=" + apk.isFile() + " readable=" + apk.canRead());
        PackageInstaller pi = getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams p = new PackageInstaller.SessionParams(
                PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        p.setSize(apk.length());
        int id = pi.createSession(p);
        FileLog.line("INSTALL", "createSession id=" + id);
        PackageInstaller.Session s = pi.openSession(id);
        FileLog.line("INSTALL", "openSession ok");
        try {
            InputStream in = new FileInputStream(apk);
            OutputStream out = s.openWrite("base.apk", 0, apk.length());
            long written = 0;
            try {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) { out.write(buf, 0, n); written += n; }
                FileLog.line("INSTALL", "write done bytes=" + written);
                s.fsync(out);
                FileLog.line("INSTALL", "fsync ok");
            } finally { in.close(); out.close(); }
            PendingIntent cb = PendingIntent.getActivity(this, 2,
                    new Intent(this, ConfirmActivity.class),
                    PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE);
            FileLog.line("INSTALL", "calling commit sender=" + (cb == null ? "null" : "ok"));
            s.commit(cb.getIntentSender());
            FileLog.line("INSTALL", "commit returned (session committed)");
            say("安装会话已提交，等待系统安装器…");
        } catch (Throwable t) {
            FileLog.line("INSTALL", "installApk threw");
            FileLog.t("INSTALL", t);
            throw t;
        } finally { s.close(); FileLog.line("INSTALL", "session closed"); }
    }

    void doInstall() {
        final File apk = findPatch();
        if (apk == null || !apk.isFile()) { say("还没有补丁包，先点「② 生成补丁包」"); return; }
        if (!cbCoexist.isChecked() && !hasAllFiles()) { askAllFiles(); return; }
        if (Build.VERSION.SDK_INT >= 26 && !getPackageManager().canRequestPackageInstalls()) {
            say("需要允许「安装未知应用」，正在打开设置…");
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                        Uri.parse("package:" + getPackageName())));
            } catch (Throwable t) { say("打开设置失败: " + t); }
            return;
        }
        btnInstall.setEnabled(false);
        say("补丁包: " + apk.getName() + " (" + (apk.length() / 1048576) + " MB)");
        if (cbCoexist.isChecked()) {
            say("共存模式：直接安装，不卸载官方、不动存档");
            new Thread(new Runnable() { public void run() {
                try {
                    installApk(apk);
                    String target = ApkPatcher.COEXIST_PKG;
                    if (!waitPkg(target, true, 180000)) {
                        say("安装未完成：系统里没有 " + target + "（可能没弹确认框或被取消）");
                        return;
                    }
                    say("补丁已安装：" + target);
                } catch (Throwable t) { fail("安装失败", t); }
                finally { ui.post(new Runnable() { public void run() { btnInstall.setEnabled(true); } }); }
            } }).start();
            return;
        }
        new Thread(new Runnable() { public void run() {
            File backup = new File("/sdcard/SFSMP-backup/" +
                    new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).format(new Date()));
            try {
                long n = copyTree(new File("/sdcard/Android/data/" + GAME), new File(backup, GAME));
                say("已备份存档 " + (n / 1048576) + " MB → " + backup.getAbsolutePath());
                if (gameInfo() != null) {
                    say("卸载官方游戏…");
                    PendingIntent pi = PendingIntent.getActivity(MainActivity.this, 1,
                            new Intent(MainActivity.this, ConfirmActivity.class),
                            PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_MUTABLE);
                    getPackageManager().getPackageInstaller().uninstall(GAME, pi.getIntentSender());
                    if (!waitState(false, 90000)) { say("卸载超时，请手动卸载后重试"); return; }
                    say("官方已卸载");
                }
                say("正在安装补丁包（系统会弹一次确认）…");
                installApk(apk);
                if (!waitState(true, 180000)) { say("安装超时或被取消"); return; }
                say("补丁已安装，正在恢复存档…");
                long m = copyTree(new File(backup, GAME), new File("/sdcard/Android/data/" + GAME));
                say("存档已恢复 " + (m / 1048576) + " MB");
                say("全部完成，可以启动游戏了");
            } catch (Throwable t) {
                fail("安装流程失败", t);
                say("存档备份仍在: " + backup.getAbsolutePath());
            } finally {
                ui.post(new Runnable() { public void run() { btnInstall.setEnabled(true); } });
            }
        } }).start();
    }
}
