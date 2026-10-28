/* SPDX-License-Identifier: GPL-3.0-or-later */
// v13: hook UnityEngine.Time::get_time (called every frame on the main thread), chain to original
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdint.h>

#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
static void log_app(const char* m){ int fd=open(MARKER,O_WRONLY|O_CREAT|O_APPEND,0644); if(fd>=0){write(fd,m,strlen(m));close(fd);} }

typedef void* (*fn_domain_get)(void);
typedef void* (*fn_thread_attach)(void*);
typedef void* (*fn_assembly_open)(void*, const char*);
typedef void* (*fn_assembly_get_image)(void*);
typedef void* (*fn_class_from_name)(void*, const char*, const char*);
typedef void* (*fn_class_get_method_from_name)(void*, const char*, int);
typedef int   (*fn_mprotect)(void*, size_t, int);

static double (*g_orig_get_time)(void) = NULL;
static volatile int g_ticks = 0;

/* float get_time() — ABI: return in s0; we are not calling il2cpp so signature is plain C */
static float our_get_time(void) {
    int t = __sync_add_and_fetch(&g_ticks, 1);
    if (t == 1) log_app("v13: *** MAIN THREAD TICK ***\n");
    if (t == 60) log_app("v13: 60 ticks on main thread OK\n");
    if (g_orig_get_time) return (float)g_orig_get_time();
    return 0.0f;
}

static void* worker(void* arg) {
    (void)arg;
    sleep(8);
    char buf[256];
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v13: dlopen NULL\n"); return NULL; }
    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_class_from_name cf = (fn_class_from_name)dlsym(h, "il2cpp_class_from_name");
    fn_class_get_method_from_name gm = (fn_class_get_method_from_name)dlsym(h, "il2cpp_class_get_method_from_name");
    fn_mprotect mp = (fn_mprotect)dlsym(dlopen("libc.so", RTLD_NOW), "mprotect");
    if (!dg||!ta||!ao||!gi||!cf||!gm||!mp) { log_app("v13: missing syms\n"); return NULL; }

    void* d = dg(); ta(d);
    void* a = ao(d, "UnityEngine.CoreModule");
    if (!a) { log_app("v13: no CoreModule\n"); return NULL; }
    void* img = gi(a);
    void* timeCls = cf(img, "UnityEngine", "Time");
    if (!timeCls) { log_app("v13: Time nowhere\n"); return NULL; }
    void* m = gm(timeCls, "get_time", 0);
    if (!m) { log_app("v13: get_time not found\n"); return NULL; }

    void** mi = (void**)m;
    void* oldPtr = mi[0];
    snprintf(buf, sizeof(buf), "v13: found Time.get_time ptr=%p\n", oldPtr);
    log_app(buf);
    if (!oldPtr) return NULL;

    long page = 4096;
    void* pageAddr = (void*)((uintptr_t)mi & ~(uintptr_t)(page-1));
    if (mp(pageAddr, page*2, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) log_app("v13: mprotect failed\n");
    g_orig_get_time = (double (*)(void))oldPtr;
    mi[0] = (void*)our_get_time;
    log_app("v13: hooked Time.get_time\n");
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res) {
    (void)vm; (void)res;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
