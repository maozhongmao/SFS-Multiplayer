// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.hook;

import android.os.Bundle;

/**
 * Launcher activity of the patched APK. The installer rewrites the manifest so that
 * android:name points here instead of com.teamcuriosity.addons.ExtendedUnityActivity.
 * Everything else is inherited unchanged.
 */
public class HookActivity extends com.teamcuriosity.addons.ExtendedUnityActivity {
    private static boolean sInited;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        android.util.Log.i("sfsmp", "SFSMP-HOOK onCreate: launcher reached");
        try {
            if (!sInited) {
                sInited = true;
                System.loadLibrary("sfsmp");
                com.sfsmp.bridge.Bridge.bridgeInit(this);
            }
        } catch (Throwable t) {
            android.util.Log.e("sfsmp", "hook bridge init failed", t);
        }
    }
}
