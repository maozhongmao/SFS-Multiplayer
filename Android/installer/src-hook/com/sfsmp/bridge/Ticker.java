// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.bridge;

public final class Ticker implements Runnable {
    public Ticker() {
    }

    @Override
    public void run() {
        Bridge.nativeTick();
    }
}
