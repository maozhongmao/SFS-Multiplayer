/* SPDX-License-Identifier: GPL-3.0-or-later */
// v17: mass-hook every parameterless Update/FixedUpdate/LateUpdate in Assembly-CSharp,
// log the first tick of each slot so we learn which class actually runs in-world.
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
typedef size_t (*fn_image_class_count)(void*);
typedef void* (*fn_image_get_class)(void*, size_t);
typedef void* (*fn_class_get_methods)(void*, void**);
typedef const char* (*fn_method_get_name)(void*);
typedef const char* (*fn_class_get_name)(void*);
typedef const char* (*fn_class_get_namespace)(void*);
typedef int (*fn_mprotect)(void*, size_t, int);

#define MAXH 48
static void* g_orig[MAXH];
static char  g_cls[MAXH][72];
static char  g_mth[MAXH][24];
static volatile int g_n = 0;
static volatile int g_first[MAXH];

static void on_tick(int slot, void* self){
    if (__sync_bool_compare_and_swap(&g_first[slot], 0, 1)) {
        char b[200];
        snprintf(b,sizeof(b),"v17: TICK slot=%d %s::%s\n", slot, g_cls[slot], g_mth[slot]);
        log_app(b);
    }
    void* o = g_orig[slot];
    if (o) ((void(*)(void*))o)(self);
}
#define MKHOOK(N) static void hook##N(void* s){ on_tick(N,s); }
MKHOOK(0) MKHOOK(1) MKHOOK(2) MKHOOK(3) MKHOOK(4) MKHOOK(5) MKHOOK(6) MKHOOK(7)
MKHOOK(8) MKHOOK(9) MKHOOK(10) MKHOOK(11) MKHOOK(12) MKHOOK(13) MKHOOK(14) MKHOOK(15)
MKHOOK(16) MKHOOK(17) MKHOOK(18) MKHOOK(19) MKHOOK(20) MKHOOK(21) MKHOOK(22) MKHOOK(23)
MKHOOK(24) MKHOOK(25) MKHOOK(26) MKHOOK(27) MKHOOK(28) MKHOOK(29) MKHOOK(30) MKHOOK(31)
MKHOOK(32) MKHOOK(33) MKHOOK(34) MKHOOK(35) MKHOOK(36) MKHOOK(37) MKHOOK(38) MKHOOK(39)
MKHOOK(40) MKHOOK(41) MKHOOK(42) MKHOOK(43) MKHOOK(44) MKHOOK(45) MKHOOK(46) MKHOOK(47)
static void* hooks[MAXH] = {
    (void*)hook0,(void*)hook1,(void*)hook2,(void*)hook3,(void*)hook4,(void*)hook5,(void*)hook6,(void*)hook7,
    (void*)hook8,(void*)hook9,(void*)hook10,(void*)hook11,(void*)hook12,(void*)hook13,(void*)hook14,(void*)hook15,
    (void*)hook16,(void*)hook17,(void*)hook18,(void*)hook19,(void*)hook20,(void*)hook21,(void*)hook22,(void*)hook23,
    (void*)hook24,(void*)hook25,(void*)hook26,(void*)hook27,(void*)hook28,(void*)hook29,(void*)hook30,(void*)hook31,
    (void*)hook32,(void*)hook33,(void*)hook34,(void*)hook35,(void*)hook36,(void*)hook37,(void*)hook38,(void*)hook39,
    (void*)hook40,(void*)hook41,(void*)hook42,(void*)hook43,(void*)hook44,(void*)hook45,(void*)hook46,(void*)hook47
};

static void* worker(void* arg) {
    (void)arg;
    sleep(8);
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v17: no il2cpp\n"); return NULL; }
    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_image_class_count cc = (fn_image_class_count)dlsym(h, "il2cpp_image_get_class_count");
    fn_image_get_class gc = (fn_image_get_class)dlsym(h, "il2cpp_image_get_class");
    fn_class_get_methods cms = (fn_class_get_methods)dlsym(h, "il2cpp_class_get_methods");
    fn_method_get_name mn = (fn_method_get_name)dlsym(h, "il2cpp_method_get_name");
    fn_class_get_name cn = (fn_class_get_name)dlsym(h, "il2cpp_class_get_name");
    fn_class_get_namespace cns = (fn_class_get_namespace)dlsym(h, "il2cpp_class_get_namespace");
    fn_mprotect mp = (fn_mprotect)dlsym(dlopen("libc.so", RTLD_NOW), "mprotect");
    if (!dg||!ta||!ao||!gi||!cc||!gc||!cms||!mn||!cn||!mp) { log_app("v17: missing syms\n"); return NULL; }
    void* d = dg(); ta(d);
    void* a = ao(d, "Assembly-CSharp");
    if (!a) { log_app("v17: no assembly\n"); return NULL; }
    void* img = gi(a);
    if (!img) { log_app("v17: no image\n"); return NULL; }

    size_t ncls = cc(img);
    char b[200];
    snprintf(b,sizeof(b),"v17: image classes=%lu, installing\n",(unsigned long)ncls);
    log_app(b);
    int found = 0;
    for (size_t i=0; i<ncls && g_n<MAXH; i++) {
        void* cls = gc(img, i);
        if (!cls) continue;
        void* it = NULL;
        void* m;
        while ((m = cms(cls, &it)) != NULL) {
            if (g_n >= MAXH) break;
            const char* name = mn(m);
            if (!name) continue;
            if (strcmp(name,"Update") && strcmp(name,"FixedUpdate") && strcmp(name,"LateUpdate")) continue;
            void** mi = (void**)m;
            void* oldPtr = mi[0];
            if (!oldPtr) continue;
            uintptr_t page = 4096;
            void* pa = (void*)((uintptr_t)mi & ~(page-1));
            if (mp(pa, page*2, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) continue;
            int slot = g_n;
            g_orig[slot] = oldPtr;
            const char* cname = cn(cls); const char* nsp = cns ? cns(cls) : "";
            snprintf(g_cls[slot], sizeof(g_cls[slot]), "%s%s%s", (nsp&&nsp[0])?nsp:"", (nsp&&nsp[0])?".":"", cname?cname:"?");
            snprintf(g_mth[slot], sizeof(g_mth[slot]), "%s", name);
            mi[0] = hooks[slot];
            found++;
            g_n = slot + 1;
        }
    }
    snprintf(b,sizeof(b),"v17: installed %d hooks\n", found);
    log_app(b);
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res) {
    (void)vm; (void)res;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
