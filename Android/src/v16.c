/* SPDX-License-Identifier: GPL-3.0-or-later */
// v16: hook methods that definitely run in-world (WorldTime.Update etc), chain to originals
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
typedef const char* (*fn_class_get_name)(void*);
typedef int (*fn_mprotect)(void*, size_t, int);

#define MAXH 8
static void* g_orig[MAXH]; static int g_n = 0;
static volatile int g_ticks = 0; static volatile long g_which = -1;

static void common_hook(void* self) {
    int t = __sync_add_and_fetch(&g_ticks, 1);
    if (t == 1) { char b[96]; snprintf(b,sizeof(b),"v16: *** MAIN THREAD TICK (slot=%ld) ***\n", g_which); log_app(b); }
    if (t == 60) log_app("v16: 60 ticks on main thread OK\n");
    if (g_which >= 0 && g_which < MAXH && g_orig[g_which]) {
        void (*f)(void*) = (void (*)(void*))g_orig[g_which];
        f(self);
    }
}
#define MKHOOK(N) static void hook##N(void* s){ g_which=N; common_hook(s); g_which=-1; }
MKHOOK(0) MKHOOK(1) MKHOOK(2) MKHOOK(3) MKHOOK(4) MKHOOK(5) MKHOOK(6) MKHOOK(7)
static void* hooks[MAXH] = {(void*)hook0,(void*)hook1,(void*)hook2,(void*)hook3,(void*)hook4,(void*)hook5,(void*)hook6,(void*)hook7};

static void* worker(void* arg) {
    (void)arg;
    sleep(8);
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_class_from_name cf = (fn_class_from_name)dlsym(h, "il2cpp_class_from_name");
    fn_class_get_method_from_name gm = (fn_class_get_method_from_name)dlsym(h, "il2cpp_class_get_method_from_name");
    fn_class_get_name cn = (fn_class_get_name)dlsym(h, "il2cpp_class_get_name");
    fn_mprotect mp = (fn_mprotect)dlsym(dlopen("libc.so", RTLD_NOW), "mprotect");
    if (!dg||!ta||!ao||!gi||!cf||!gm||!mp) { log_app("v16: missing syms\n"); return NULL; }
    void* d = dg(); ta(d);
    const char* ASM[] = {"Assembly-CSharp","Assembly-CSharp-firstpass"};
    void* img = NULL;
    for (int i=0;i<2 && !img;i++){ void* a=ao(d,ASM[i]); if(a) img=gi(a); }
    if (!img) { log_app("v16: no image\n"); return NULL; }

    const char* clsNames[] = { "VCQSQMYGGZECNBPE", "FULMMHJZAEKABZAY", "WQPAXLGGCOAUZEOA", "FGKWZQVSXYZXUGHB" };
    const char* methods[]  = { "Update", "FixedUpdate", "LateUpdate" };
    char b[160];

    for (int ci=0; ci<4; ci++) {
        void* cls = NULL;
        const char* ns[] = {"SFS.World","SFS",""};
        for (int k=0;k<3 && !cls;k++) cls = cf(img, ns[k], clsNames[ci]);
        if (!cls) continue;
        for (int mi2=0; mi2<3; mi2++) {
            if (g_n >= MAXH) break;
            void* m = gm(cls, methods[mi2], 0);
            if (!m) continue;
            void** mi = (void**)m;
            void* oldPtr = mi[0];
            if (!oldPtr) continue;
            long page = 4096;
            void* pa = (void*)((uintptr_t)mi & ~(uintptr_t)(page-1));
            mp(pa, page*2, PROT_READ|PROT_WRITE|PROT_EXEC);
            g_orig[g_n] = oldPtr;
            mi[0] = hooks[g_n];
            snprintf(b,sizeof(b),"v16: hooked %s::%s slot=%d\n", cn ? cn(cls) : clsNames[ci], methods[mi2], g_n);
            log_app(b);
            g_n++;
        }
    }
    if (g_n == 0) log_app("v16: nothing hooked\n");
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res) {
    (void)vm; (void)res;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
