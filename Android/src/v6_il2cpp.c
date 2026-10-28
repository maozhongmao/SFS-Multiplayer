/* SPDX-License-Identifier: GPL-3.0-or-later */
// multiplayersfs v6: actually call il2cpp and resolve the (obfuscated) Rocket class
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
static void wr(const char* m){ int fd=open(MARKER,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,m,strlen(m));close(fd);} }

typedef void* (*fn_domain_get)(void);
typedef void* (*fn_thread_attach)(void*);
typedef void* (*fn_assembly_open)(void*, const char*);
typedef void* (*fn_assembly_get_image)(void*);
typedef void* (*fn_class_from_name)(void*, const char*, const char*);
typedef const char* (*fn_class_get_name)(void*);

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res) {
    (void)vm; (void)res;
    char buf[512];
    int n = 0;

    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { wr("v6: dlopen NULL\n"); return JNI_VERSION_1_6; }
    n += snprintf(buf+n, sizeof(buf)-n, "v6: libil2cpp OK ");

    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_class_from_name cf = (fn_class_from_name)dlsym(h, "il2cpp_class_from_name");
    fn_class_get_name cn = (fn_class_get_name)dlsym(h, "il2cpp_class_get_name");
    if (!dg||!ta||!ao||!gi||!cf) { wr("v6: missing symbols\n"); return JNI_VERSION_1_6; }

    void* d = dg();
    if (!d) { wr("v6: domain NULL\n"); return JNI_VERSION_1_6; }
    n += snprintf(buf+n, sizeof(buf)-n, "domain OK ");
    ta(d);

    void* a = ao(d, "Assembly-CSharp");
    if (!a) { wr("v6: assembly NULL\n"); return JNI_VERSION_1_6; }
    void* img = gi(a);
    if (!img) { wr("v6: image NULL\n"); return JNI_VERSION_1_6; }
    n += snprintf(buf+n, sizeof(buf)-n, "image OK ");

    /* Rocket was obfuscated to FULMMHJZAEKABZAY; try a few namespace guesses */
    const char* ns[] = { "SFS.World", "SFS", "" };
    void* cls = NULL;
    const char* used = "?";
    for (int i = 0; i < 3 && !cls; i++) {
        cls = cf(img, ns[i], "FULMMHJZAEKABZAY");
        if (cls) used = ns[i];
    }
    if (!cls) { wr("v6: class NOT found\n"); return JNI_VERSION_1_6; }
    const char* real = cn ? cn(cls) : "?";
    snprintf(buf+n, sizeof(buf)-n, "Rocket cls OK ns=%s name=%s\n", used, real);
    wr(buf);
    return JNI_VERSION_1_6;
}
