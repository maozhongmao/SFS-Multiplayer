// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patch;

import android.os.Bundle;

import com.sfsmp.bridge.Bridge;
import com.teamcuriosity.addons.ExtendedUnityActivity;

/**
 * 补丁包的启动 Activity：manifest 里把 launcher 的 android:name 改成这个类。
 * 它只做一件事 —— super.onCreate 之后加载 libsfsmp.so 并调 bridgeInit。
 * 游戏自己的 dex / 资源一个字节都不改。
 */
public class HookActivity extends ExtendedUnityActivity {
    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try { Bridge.bridgeInit(this); } catch (Throwable ignored) { }
    }
}
