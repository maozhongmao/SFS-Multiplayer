// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patch;

import android.os.Bundle;

import com.sfsmp.bridge.Bridge;
import com.teamcuriosity.addons.ExtendedUnityActivity;

/**
 * 补丁包的启动 Activity：manifest 里把 launcher 的 android:name 改成这个类。
 * 只做一件事 —— super.onCreate 之后加载 libsfsmp.so 并调 bridgeInit。
 * v154：退回最小实现（去掉 JavaLog 生命周期打点），以对齐「已验证能渲染」的那一版行为。
 */
public class HookActivity extends ExtendedUnityActivity {
    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try { Bridge.bridgeInit(this); } catch (Throwable ignored) { }
    }
}
