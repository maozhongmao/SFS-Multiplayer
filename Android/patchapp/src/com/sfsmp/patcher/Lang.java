// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import java.util.Locale;

/**
 * 安装器语言：跟随系统。
 * 系统语言是中文 -> 中文；其余全部回退英文。
 * 目前只支持中文/英文两种，避免引入未翻译语言。
 */
public final class Lang {
    private static final boolean ZH =
            "zh".equalsIgnoreCase(Locale.getDefault().getLanguage());

    private Lang() {}

    public static boolean isChinese() { return ZH; }

    public static String t(String zh, String en) {
        return ZH ? zh : en;
    }
}
