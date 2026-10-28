// SPDX-License-Identifier: GPL-3.0-or-later
import com.sfsmp.patcher.Axml;
import java.nio.file.*;
public class Dump2 {
    public static void main(String[] a) throws Exception {
        final byte[] b = Files.readAllBytes(Paths.get(a[0]));
        final StringBuilder sb = new StringBuilder();
        Axml.forEachElement(b, new Axml.ElemVisitor() {
            public void visit(int chunk, String name) { sb.append(chunk).append(':').append(name).append(' '); }
        });
        System.out.println("ELEMENTS: " + sb.toString());
    }
}
