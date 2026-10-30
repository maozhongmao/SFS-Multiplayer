// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.bridge;

import android.app.Activity;
import android.view.View;

/**
 * native 侧（v64.c）硬依赖这个类：
 *   GetStaticFieldID(g_bridge, "INSTANCE", "Lcom/sfsmp/bridge/Bridge;")
 *   Java_com_sfsmp_bridge_Bridge_nativeInit / _nativeTick / _nativeOnClick
 * v154：去掉 JavaLog 调用，恢复最小行为。
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
        a.runOnUiThread(new Ticker());
    }
}
