package com.sfsmp.patcher;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

public class InstallReceiver extends BroadcastReceiver {
    public static final String A_INSTALL = "com.sfsmp.patcher.INSTALL";
    public static final String A_UNINSTALL = "com.sfsmp.patcher.UNINSTALL";

    @Override public void onReceive(Context c, Intent i) {
        // 结果靠轮询 PackageManager 判断，这里只记录状态码方便排查
        try {
            if (i != null && i.getExtras() != null) {
                int status = i.getIntExtra("android.content.pm.extra.STATUS", Integer.MIN_VALUE);
                if (status != Integer.MIN_VALUE) {
                    android.util.Log.i("sfsmp", "installer status=" + status);
                }
            }
        } catch (Throwable t) { /* ignore */ }
    }
}
