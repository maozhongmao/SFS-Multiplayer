package com.sfsmp.patcher;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/**
 * 安装会话的 PendingIntent 目标。
 * 系统在会话进入 PENDING_USER_ACTION 时会把「安装确认」的 Intent 塞进 EXTRA_INTENT，
 * 必须由一个 Activity 去 startActivity，否则会话永远停在那里（不弹框、不报错）。
 */
public class ConfirmActivity extends Activity {
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        Intent src = getIntent();
        int status = src == null ? Integer.MIN_VALUE
                : src.getIntExtra("android.content.pm.extra.STATUS", Integer.MIN_VALUE);
        Intent next = null;
        if (src != null) {
            next = src.getParcelableExtra("android.content.pm.extra.INTENT");
            if (next == null) next = src.getParcelableExtra(Intent.EXTRA_INTENT);
        }
        Log2.w("确认界面 status=" + status + " next=" + (next != null));
        if (next != null) {
            try {
                next.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(next);
            } catch (Throwable t) { Log2.w("启动安装确认失败: " + t); }
        }
        finish();
    }
}
