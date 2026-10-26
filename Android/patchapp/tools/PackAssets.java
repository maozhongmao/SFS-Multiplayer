import java.io.*;
import java.nio.file.*;

/** 加壳：把给定源文件 XOR 后写到 outDir/<文件名>（同一套密钥见 Pack.java）。 */
public class PackAssets {
    static final String CERT = "2caa0386be65b9fcde8ec7522d0528f65e96b3333dfdaaff5d94a665fbd45abf";
    public static void main(String[] a) throws Exception {
        byte[] s = CERT.getBytes("US-ASCII");
        byte[] k = new byte[16];
        for (int i = 0; i < 16; i++) k[i] = (byte) (s[8 + i] ^ s[40 + i]);
        Path out = Paths.get(a[0]);
        for (int n = 1; n < a.length; n++) {
            Path src = Paths.get(a[n]);
            byte[] d = Files.readAllBytes(src);
            for (int i = 0; i < d.length; i++) d[i] ^= k[i & 15];
            Path dst = out.resolve(src.getFileName().toString());
            Files.write(dst, d);
            System.out.println("packed " + dst.getFileName() + " " + d.length + " B");
        }
    }
}
