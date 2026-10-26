package com.sfsmp.bridge;

/** UI 线程心跳：native 侧 poll_click 靠它每帧驱动（16ms ≈ 60fps）。 */
public final class Ticker implements Runnable {
    @Override public void run() {
        for (;;) {
            try { Bridge.nativeTick(); } catch (Throwable ignored) { }
            try { Thread.sleep(16L); } catch (Throwable ignored) { }
        }
    }
}
