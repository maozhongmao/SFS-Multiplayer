// SPDX-License-Identifier: GPL-3.0-or-later
import java.io.*;
import java.util.Enumeration;
import java.util.zip.*;

public class RenameTest {
    static final String OLD = "com.StefMorojna.SpaceflightSimulator";

    public static void main(String[] a) throws Exception {
        File in = new File(a[0]);
        File out = new File(a[1]);
        String newPkg = a[2];
        ZipFile zf = new ZipFile(in);
        ZipOutputStream zos = new ZipOutputStream(new BufferedOutputStream(new FileOutputStream(out), 1 << 16));
        int man = 0, arsc = 0;
        Enumeration<? extends ZipEntry> en = zf.entries();
        while (en.hasMoreElements()) {
            ZipEntry e = en.nextElement();
            if (e.isDirectory()) continue;
            String n = e.getName();
            InputStream is = zf.getInputStream(e);
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] buf = new byte[1 << 16];
            int k;
            while ((k = is.read(buf)) > 0) bo.write(buf, 0, k);
            is.close();
            byte[] d = bo.toByteArray();
            if (n.equals("AndroidManifest.xml")) {
                byte[] r = com.sfsmp.patcher.AxmlStrings.replaceAttrPrefix(d, OLD, newPkg);
                if (r == null) throw new IOException("manifest prefix not found");
                d = r; man++;
                System.out.println("manifest patched: " + d.length + " B");
            } else if (n.equals("resources.arsc")) {
                if (!com.sfsmp.patcher.Arsc.renamePackage(d, newPkg)) throw new IOException("arsc rename failed");
                arsc++;
                System.out.println("arsc package now: " + com.sfsmp.patcher.Arsc.readPackage(d));
            }
            ZipEntry o = new ZipEntry(n);
            if (e.getMethod() == ZipEntry.STORED) {
                CRC32 c = new CRC32(); c.update(d);
                o.setMethod(ZipEntry.STORED); o.setSize(d.length); o.setCompressedSize(d.length); o.setCrc(c.getValue());
            }
            zos.putNextEntry(o); zos.write(d); zos.closeEntry();
        }
        zf.close(); zos.close();
        System.out.println("manifest=" + man + " arsc=" + arsc + " out=" + out.length());
    }
}
