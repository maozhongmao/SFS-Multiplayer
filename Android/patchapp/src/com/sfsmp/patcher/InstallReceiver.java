// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public class InstallReceiver extends BroadcastReceiver {
    public static final String A_INSTALL = "com.sfsmp.patcher.INSTALL";
    public static final String A_UNINSTALL = "com.sfsmp.patcher.UNINSTALL";

    @Override public void onReceive(Context c, Intent i) {
        try {
            FileLog.init(c);
            FileLog.line("RCV", "action=" + (i == null ? "null" : i.getAction()));
            FileLog.extras("RCV", i);
        } catch (Throwable t) { FileLog.t("RCV", t); }
    }
}
