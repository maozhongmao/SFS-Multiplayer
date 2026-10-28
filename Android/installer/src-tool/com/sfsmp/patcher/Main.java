// SPDX-License-Identifier: GPL-3.0-or-later
package com.sfsmp.patcher;

import java.io.File;
import java.util.HashMap;
import java.util.Map;

/** Desktop driver: java -cp classes com.sfsmp.patcher.Main --apk in.apk --out out.apk ... */
public final class Main {
    public static void main(String[] args) throws Exception {
        Map<String, String> a = new HashMap<>();
        for (int i = 0; i < args.length - 1; i++) {
            if (args[i].startsWith("--")) a.put(args[i].substring(2), args[i + 1]);
        }
        File in = need(a, "apk");
        File out = new File(a.getOrDefault("out", in.getParent() + "/patched.apk"));
        File dex = need(a, "dex");
        File so = need(a, "so");
        String oldClass = a.getOrDefault("old", "com.teamcuriosity.addons.ExtendedUnityActivity");
        String newClass = a.getOrDefault("new", "com.sfsmp.hook.HookActivity");

        System.out.println("in      = " + in + " (" + in.length() + " B)");
        System.out.println("out     = " + out);
        System.out.println("dex     = " + dex + " (" + dex.length() + " B)");
        System.out.println("so      = " + so + " (" + so.length() + " B)");
        System.out.println("launcher: " + oldClass + "  ->  " + newClass);

        byte[] manifest = ApkPatcher.readAllEntry(in, "AndroidManifest.xml");
        byte[] patched = Axml.replaceAttributeValue(manifest, oldClass, newClass);
        System.out.println("manifest: " + manifest.length + " -> " + patched.length + " B");

        ApkPatcher.Report rep = ApkPatcher.patch(in, out, patched, dex, so, newClass, oldClass);
        System.out.println("patched: " + rep);
        System.out.println("PATCHED_OK " + out.getAbsolutePath());
    }

    private static File need(Map<String, String> a, String k) {
        String v = a.get(k);
        if (v == null) throw new IllegalArgumentException("missing --" + k);
        return new File(v);
    }
}
