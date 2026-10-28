// SPDX-License-Identifier: GPL-3.0-or-later
import com.sfsmp.patcher.Axml;
import com.sfsmp.patcher.AxmlStrings;
import java.nio.file.*;

public class PatchTest {
    public static void main(String[] a) throws Exception {
        byte[] m = Files.readAllBytes(Paths.get(a[0]));
        String oldCls = a[1], newCls = a[2];
        int newCode = Integer.parseInt(a[3]);
        byte[] r = AxmlStrings.replaceAttrValue(m, oldCls, newCls);
        if (r == null) { System.out.println("REPLACE_FAILED"); return; }
        int old = Axml.bumpVersionCode(r, newCode);
        Files.write(Paths.get(a[4]), r);
        System.out.println("ok oldVC=" + old + " newVC=" + newCode + " size " + m.length + " -> " + r.length);
    }
}
