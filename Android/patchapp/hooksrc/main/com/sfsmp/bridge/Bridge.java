// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.bridge;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.view.View;

/**
 * native 侧（v64.c）硬依赖这个类：
 *   GetStaticFieldID(g_bridge, "INSTANCE", "Lcom/sfsmp/bridge/Bridge;")
 *   Java_com_sfsmp_bridge_Bridge_nativeInit / _nativeTick / _nativeOnClick
 *
 * 2026-11-15 修复两处 UI 线程阻塞：
 *   1) 原来 a.runOnUiThread(new Ticker())：onCreate 就在 UI 线程上，runOnUiThread 会同步执行
 *      Ticker 里 for(;;) 死循环 -> onCreate 永不返回 -> 卡系统启动画面。
 *   2) Ticker 改成单次心跳后，先只投递一次；后续由 native 的 schedule() 续期。
 */
public class Bridge implements View.OnClickListener {
    private static final Bridge INSTANCE = new Bridge();

    public static native void nativeInit(Activity a);
    public static native void nativeTick();
    public static native void nativeOnClick(View v);

    @Override public void onClick(View v) {
        try { nativeOnClick(v); } catch (Throwable ignored) { }
    }

    public static void bridgeInit(Activity a) {
        System.loadLibrary("sfsmp");
        nativeInit(a);
        new Handler(Looper.getMainLooper()).postDelayed(new Ticker(), 2000L);
    }
}
