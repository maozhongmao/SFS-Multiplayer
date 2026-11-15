// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.bridge;

/** 单次心跳：每次只调一次 nativeTick，由 native（schedule）或 Java 侧各自负责续期。
 *  2026-11-15 修复：原来是 for(;;)+sleep(16) 死循环，被 schedule() 反复 postDelayed 之后
 *  会在 UI 线程上堆出多条永不返回的循环，几秒后输入事件就再也进不来了（表现为「有动效但点不动」）。 */
public final class Ticker implements Runnable {
    @Override public void run() {
        try { Bridge.nativeTick(); } catch (Throwable ignored) { }
    }
}
