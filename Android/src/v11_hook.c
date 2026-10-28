/* SPDX-License-Identifier: GPL-3.0-or-later */
// v11: main-thread hook via IL2CPP methodPointer patching (no Dobby needed)
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

static void* g_old_update = NULL;
static volatile int g_ticks = 0;

/* our replacement: same ABI as the original (this in x0) */
static void our_update(void* self) {
    int t = __sync_add_and_fetch(&g_ticks, 1);
    char b[96];
    if (t == 1) { log_app("v11: MAIN THREAD HOOK FIRST TICK\n"); }
    if (t == 5) {
        snprintf(b, sizeof(b), "v11: hook alive, ticks=%d\n", t);
        log_app(b);
    }
    if (g_old_update) {
        void (*orig)(void*) = (void (*)(void*))g_old_update;
        orig(self);
    }
}

typedef int (*fn_mprotect)(void*, size_t, int);

static void* worker(void* arg) {
    (void)arg;
    sleep(8);
    char buf[256];
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v11: dlopen NULL\n"); return NULL; }
    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_class_from_name cf = (fn_class_from_name)dlsym(h, "il2cpp_class_from_name");
    fn_class_get_method_from_name gm = (fn_class_get_method_from_name)dlsym(h, "il2cpp_class_get_method_from_name");
    fn_mprotect mp = (fn_mprotect)dlsym(h, "mprotect");
    if (!mp) mp = (fn_mprotect)dlsym(dlopen("libc.so", RTLD_NOW), "mprotect");
    if (!dg||!ta||!ao||!gi||!cf||!gm) { log_app("v11: missing syms\n"); return NULL; }

    void* d = dg(); ta(d);
    const char* ASM[] = {"Assembly-CSharp","Assembly-CSharp-firstpass"};
    void* img = NULL;
    for (int i=0;i<2 && !img;i++){ void* a=ao(d,ASM[i]); if(a) img=gi(a); }
    if (!img) { log_app("v11: no image\n"); return NULL; }

    void* rocket = NULL;
    const char* ns[] = {"SFS.World","SFS",""};
    for (int k=0;k<3 && !rocket;k++) rocket = cf(img, ns[k], "FULMMHJZAEKABZAY");
    if (!rocket) { log_app("v11: Rocket not found\n"); return NULL; }

    void* upd = gm(rocket, "Update", 0);
    if (!upd) { log_app("v11: Rocket.Update not found\n"); return NULL; }
    log_app("v11: Rocket.Update found, patching methodPointer\n");

    /* MethodInfo layout: { void* methodPointer; void* invoker; void* name; ... } */
    void** mi = (void**)upd;
    void* oldPtr = mi[0];
    if (!oldPtr) { log_app("v11: methodPointer NULL\n"); return NULL; }

    /* make the page writable then swap the pointer */
    long page = 4096;
    void* pageAddr = (void*)((uintptr_t)mi & ~(uintptr_t)(page-1));
    if (mp && mp(pageAddr, page*2, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) {
        log_app("v11: mprotect failed, trying anyway\n");
    }
    g_old_update = oldPtr;
    mi[0] = (void*)our_update;
    snprintf(buf, sizeof(buf), "v11: hooked old=%p new=%p\n", oldPtr, (void*)our_update);
    log_app(buf);
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res) {
    (void)vm; (void)res;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
