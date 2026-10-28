// SPDX-License-Identifier: GPL-3.0-or-later
import com.sfsmp.patcher.Axml;
import java.nio.file.*;

public class AxmlTest {
    public static void main(String[] a) throws Exception {
        byte[] m = Files.readAllBytes(Paths.get(a[0]));
        int target = Integer.parseInt(a[1]);
        String before = Axml.getStringAttr(m, findManifest(m), "versionName");
        int old = Axml.bumpVersionCode(m, target);
        Files.write(Paths.get(a[2]), m);
        System.out.println("oldVersionCode=" + old + " newVersionCode=" + target + " versionName=" + before);
    }
    static int findManifest(byte[] b) {
        final int[] r = { -1 };
        Axml.forEachElement(b, new Axml.ElemVisitor() {
            public void visit(int chunk, String name) { if (r[0] < 0 && "manifest".equals(name)) r[0] = chunk; }
        });
        return r[0];
    }
}
