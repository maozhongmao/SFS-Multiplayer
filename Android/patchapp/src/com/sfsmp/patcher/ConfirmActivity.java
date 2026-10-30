// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.Intent;
import android.os.Bundle;

/**
 * 安装会话的 PendingIntent 目标。
 * 系统在会话进入 PENDING_USER_ACTION 时把「安装确认」Intent 塞进 EXTRA_INTENT，
 * 必须由 Activity 去 startActivity，否则会话停在那里（不弹框、不报错）。
 * v131：把收到的 Intent 与全部 extras 落盘，便于判断「系统到哪一步没往下走」。
 */
public class ConfirmActivity extends Activity {
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        FileLog.init(this);
        FileLog.line("CONFIRM", "onCreate");
        Intent src = getIntent();
        FileLog.extras("CONFIRM", src);

        int status = src == null ? Integer.MIN_VALUE
                : src.getIntExtra("android.content.pm.extra.STATUS", Integer.MIN_VALUE);
        String msg = src == null ? null : src.getStringExtra("android.content.pm.extra.STATUS_MESSAGE");
        Intent next = null;
        if (src != null) {
            try { next = src.getParcelableExtra("android.content.pm.extra.INTENT"); } catch (Throwable t) { FileLog.t("CONFIRM", t); }
            if (next == null) {
                try { next = src.getParcelableExtra(Intent.EXTRA_INTENT); } catch (Throwable t) { FileLog.t("CONFIRM", t); }
            }
        }
        FileLog.line("CONFIRM", "status=" + status + " (" + statusName(status) + ") msg=" + msg
                + " nextIntent=" + (next != null));

        if (next != null) {
            try {
                FileLog.extras("CONFIRM:next", next);
                next.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                startActivity(next);
                FileLog.line("CONFIRM", "startActivity(install confirm) ok");
            } catch (Throwable t) {
                FileLog.t("CONFIRM", t);
            }
        } else {
            FileLog.line("CONFIRM", "no confirm intent -> session only reported a result");
        }
        finish();
    }

    private static String statusName(int s) {
        switch (s) {
            case android.content.pm.PackageInstaller.STATUS_PENDING_USER_ACTION: return "PENDING_USER_ACTION";
            case android.content.pm.PackageInstaller.STATUS_SUCCESS: return "SUCCESS";
            case android.content.pm.PackageInstaller.STATUS_FAILURE: return "FAILURE";
            case android.content.pm.PackageInstaller.STATUS_FAILURE_BLOCKED: return "FAILURE_BLOCKED";
            case android.content.pm.PackageInstaller.STATUS_FAILURE_ABORTED: return "FAILURE_ABORTED";
            case android.content.pm.PackageInstaller.STATUS_FAILURE_INVALID: return "FAILURE_INVALID";
            case android.content.pm.PackageInstaller.STATUS_FAILURE_CONFLICT: return "FAILURE_CONFLICT";
            case android.content.pm.PackageInstaller.STATUS_FAILURE_STORAGE: return "FAILURE_STORAGE";
            case android.content.pm.PackageInstaller.STATUS_FAILURE_INCOMPATIBLE: return "FAILURE_INCOMPATIBLE";
            default: return Integer.MIN_VALUE == s ? "NONE" : "UNKNOWN";
        }
    }
}
