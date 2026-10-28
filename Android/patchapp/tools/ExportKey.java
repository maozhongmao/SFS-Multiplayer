// SPDX-License-Identifier: GPL-3.0-or-later
import java.io.*;
import java.security.KeyStore;
import java.security.PrivateKey;
import java.security.cert.X509Certificate;

/** 从 JKS 导出 PKCS#8 私钥 + X.509 证书 DER，供 Android 端直接构造（避开 PKCS12 provider 兼容问题）。 */
public class ExportKey {
    public static void main(String[] a) throws Exception {
        char[] pw = a[2].toCharArray();
        KeyStore ks = KeyStore.getInstance("JKS");
        try (InputStream in = new FileInputStream(a[0])) { ks.load(in, pw); }
        PrivateKey key = (PrivateKey) ks.getKey(a[1], pw);
        X509Certificate cert = (X509Certificate) ks.getCertificate(a[1]);
        try (OutputStream o = new FileOutputStream(a[3])) { o.write(key.getEncoded()); }
        try (OutputStream o = new FileOutputStream(a[4])) { o.write(cert.getEncoded()); }
        System.out.println("key=" + key.getEncoded().length + " crt=" + cert.getEncoded().length);
    }
}
