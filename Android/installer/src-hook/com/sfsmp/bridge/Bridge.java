// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.bridge;

import android.app.Activity;
import android.view.View;

/** JNI bridge. Must stay binary-compatible with libsfsmp.so. */
public final class Bridge implements View.OnClickListener {
    public static final Bridge INSTANCE = new Bridge();

    private Bridge() {
    }

    public static native void nativeInit(Activity activity);

    public static native void nativeOnClick(View view);

    public static native void nativeTick();

    public static void bridgeInit(Activity activity) {
        nativeInit(activity);
        activity.runOnUiThread(new Ticker());
    }

    @Override
    public void onClick(View view) {
        nativeOnClick(view);
    }
}
