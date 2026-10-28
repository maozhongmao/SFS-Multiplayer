/* SPDX-License-Identifier: GPL-3.0-or-later */
// v15: proper hook of Rocket.FixedUpdate — runs on Unity main thread every physics tick
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

static void (*g_orig)(void* self) = NULL;
static volatile int g_ticks = 0;

static void our_fixed_update(void* self) {
    int t = __sync_add_and_fetch(&g_ticks, 1);
    if (t == 1)  log_app("v15: *** MAIN THREAD TICK 1 ***\n");
    if (t == 50) log_app("v15: 50 main-thread ticks OK\n");
    if (t == 200) log_app("v15: 200 ticks OK\n");
    if (g_orig) g_orig(self);   /* chain to the real FixedUpdate with the correct instance */
}

static void* worker(void* arg) {
    (void)arg;
    sleep(8);
    char buf[256];
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v15: dlopen NULL\n"); return NULL; }
    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_class_from_name cf = (fn_class_from_name)dlsym(h, "il2cpp_class_from_name");
    fn_class_get_method_from_name gm = (fn_class_get_method_from_name)dlsym(h, "il2cpp_class_get_method_from_name");
    fn_mprotect mp = (fn_mprotect)dlsym(dlopen("libc.so", RTLD_NOW), "mprotect");

    void* d = dg(); ta(d);
    void* img = gi(ao(d, "Assembly-CSharp"));
    if (!img) { log_app("v15: no image\n"); return NULL; }
    void* rocket = NULL;
    const char* ns[] = {"SFS.World","SFS",""};
    for (int k=0;k<3&&!rocket;k++) rocket = cf(img, ns[k], "FULMMHJZAEKABZAY");
    if (!rocket) { log_app("v15: no Rocket\n"); return NULL; }
    void* m = gm(rocket, "FixedUpdate", 0);
    if (!m) { log_app("v15: no FixedUpdate\n"); return NULL; }

    void** mi = (void**)m;
    void* oldPtr = mi[0];
    long page = 4096;
    void* pageAddr = (void*)((uintptr_t)mi & ~(uintptr_t)(page-1));
    mp(pageAddr, page*2, PROT_READ|PROT_WRITE|PROT_EXEC);
    g_orig = (void (*)(void*))oldPtr;
    mi[0] = (void*)our_fixed_update;
    snprintf(buf, sizeof(buf), "v15: hooked Rocket.FixedUpdate (old=%p)\n", oldPtr);
    log_app(buf);
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res) {
    (void)vm; (void)res;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
