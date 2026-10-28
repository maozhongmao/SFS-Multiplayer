/* SPDX-License-Identifier: GPL-3.0-or-later */
// v12: try multiple hook targets on the main thread (patch IL2CPP methodPointer)
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
typedef void* (*fn_class_get_name)(void*);
typedef void* (*fn_class_get_parent)(void*);
typedef void* (*fn_get_method)(void*, const char*, int);

static void* g_old = NULL;
static volatile int g_ticks = 0;
static void our_update(void* self) {
    int t = __sync_add_and_fetch(&g_ticks, 1);
    if (t == 1) log_app("v12: MAIN THREAD HOOK FIRST TICK\n");
    if (t == 10) { char b[80]; snprintf(b,sizeof(b),"v12: ticks=%d\n",t); log_app(b); }
    if (g_old) { void (*orig)(void*) = (void (*)(void*))g_old; orig(self); }
}

typedef int (*fn_mprotect)(void*, size_t, int);

static int try_hook(void* cls, const char* mname, void* gm_fn, void* mp_fn, void* cn_fn) {
    fn_get_method gm = (fn_get_method)gm_fn;
    (void)mp_fn;
    void* m = gm(cls, mname, 0);
    if (!m) return 0;
    void** mi = (void**)m;
    void* oldPtr = mi[0];
    if (!oldPtr) return 0;
    long page = 4096;
    void* pageAddr = (void*)((uintptr_t)mi & ~(uintptr_t)(page-1));
    ((fn_mprotect)mp_fn)(pageAddr, page*2, PROT_READ|PROT_WRITE|PROT_EXEC);
    g_old = oldPtr;
    mi[0] = (void*)our_update;
    char b[160];
    
    (void)cn_fn;
    snprintf(b, sizeof(b), "v12: HOOKED %s::%s old=%p\n", "cls", mname, oldPtr);
    log_app(b);
    return 1;
}

static void* worker(void* arg) {
    (void)arg;
    sleep(8);
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v12: dlopen NULL\n"); return NULL; }
    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_class_from_name cf = (fn_class_from_name)dlsym(h, "il2cpp_class_from_name");
    fn_class_get_method_from_name gm = (fn_class_get_method_from_name)dlsym(h, "il2cpp_class_get_method_from_name");
    fn_class_get_name cn = (fn_class_get_name)dlsym(h, "il2cpp_class_get_name");
    fn_class_get_parent cp = (fn_class_get_parent)dlsym(h, "il2cpp_class_get_parent");
    fn_mprotect mp = (fn_mprotect)dlsym(dlopen("libc.so", RTLD_NOW), "mprotect");
    if (!dg||!ta||!ao||!gi||!cf||!gm) { log_app("v12: missing syms\n"); return NULL; }

    void* d = dg(); ta(d);
    const char* ASM[] = {"Assembly-CSharp","Assembly-CSharp-firstpass"};
    void* img = NULL;
    for (int i=0;i<2 && !img;i++){ void* a=ao(d,ASM[i]); if(a) img=gi(a); }
    if (!img) { log_app("v12: no image\n"); return NULL; }

    const char* classes[] = { "VCQSQMYGGZECNBPE", "FULMMHJZAEKABZAY", "WQPAXLGGCOAUZEOA", "FGKWZQVSXYZXUGHB" };
    const char* methods[] = { "Update", "FixedUpdate", "LateUpdate", "Update" };

    int hooked = 0;
    for (int ci=0; ci<4 && !hooked; ci++) {
        void* cls = NULL;
        const char* ns[] = {"SFS.World","SFS",""};
        for (int k=0;k<3 && !cls;k++) cls = cf(img, ns[k], classes[ci]);
        if (!cls) continue;
        void* cur = cls;
        for (int depth=0; depth<4 && cur; depth++) {
            for (int mi2=0; mi2<4; mi2++) {
                if (try_hook(cur, methods[mi2], gm, mp, cn)) { hooked = 1; break; }
            }
            if (hooked) break;
            cur = cp ? cp(cur) : NULL;
        }
    }
    if (!hooked) log_app("v12: NO hook target found\n");
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res) {
    (void)vm; (void)res;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
