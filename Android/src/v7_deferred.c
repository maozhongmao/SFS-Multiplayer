/* SPDX-License-Identifier: GPL-3.0-or-later */
// v7: deferred init — spawn a worker thread, sleep past IL2CPP startup, then call il2cpp
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
static void wr(const char* m){ int fd=open(MARKER,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,m,strlen(m));close(fd);} }

typedef void* (*fn_domain_get)(void);
typedef void* (*fn_thread_attach)(void*);
typedef void* (*fn_assembly_open)(void*, const char*);
typedef void* (*fn_assembly_get_image)(void*);
typedef void* (*fn_class_from_name)(void*, const char*, const char*);
typedef const char* (*fn_class_get_name)(void*);

static void* worker(void* arg) {
    (void)arg;
    sleep(8);   /* let Unity/IL2CPP finish initialising */

    char buf[512]; int n = 0;
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { wr("v7: dlopen NULL\n"); return NULL; }
    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_class_from_name cf = (fn_class_from_name)dlsym(h, "il2cpp_class_from_name");
    fn_class_get_name cn = (fn_class_get_name)dlsym(h, "il2cpp_class_get_name");
    if (!dg||!ta||!ao||!gi||!cf) { wr("v7: missing symbols\n"); return NULL; }

    void* d = dg();
    if (!d) { wr("v7: domain NULL\n"); return NULL; }
    ta(d);
    n += snprintf(buf+n, sizeof(buf)-n, "v7: domain OK ");

    void* a = ao(d, "Assembly-CSharp");
    if (!a) { a = ao(d, "Assembly-CSharp-firstpass"); }
    if (!a) { wr("v7: no assembly\n"); return NULL; }
    void* img = gi(a);
    if (!img) { wr("v7: no image\n"); return NULL; }
    n += snprintf(buf+n, sizeof(buf)-n, "image OK ");

    const char* ns[] = { "SFS.World", "SFS", "" };
    void* cls = NULL; const char* used = "?";
    for (int i = 0; i < 3 && !cls; i++) { cls = cf(img, ns[i], "FULMMHJZAEKABZAY"); if (cls) used = ns[i]; }
    if (!cls) { wr("v7: class NOT found\n"); return NULL; }
    snprintf(buf+n, sizeof(buf)-n, "Rocket OK ns=%s name=%s\n", used, cn ? cn(cls) : "?");
    wr(buf);
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res) {
    (void)vm; (void)res;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
    else wr("v7: pthread_create failed\n");
    return JNI_VERSION_1_6;
}
