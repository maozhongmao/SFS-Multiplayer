/* SPDX-License-Identifier: GPL-3.0-or-later */
// v10: isolate thread-safety — call Time.get_time (safe) from worker, then FindObjectsOfType
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
static void log_app(const char* m){ int fd=open(MARKER,O_WRONLY|O_CREAT|O_APPEND,0644); if(fd>=0){write(fd,m,strlen(m));close(fd);} }

typedef void* (*fn_domain_get)(void);
typedef void* (*fn_thread_attach)(void*);
typedef void* (*fn_assembly_open)(void*, const char*);
typedef void* (*fn_assembly_get_image)(void*);
typedef void* (*fn_class_from_name)(void*, const char*, const char*);
typedef void* (*fn_class_get_type)(void*);
typedef void* (*fn_class_get_method_from_name)(void*, const char*, int);
typedef void* (*fn_runtime_invoke)(void*, void*, void**, void**);
typedef int   (*fn_array_length)(void*);

static void* worker(void* arg) {
    (void)arg;
    sleep(8);
    char buf[512]; int n;
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v10: dlopen NULL\n"); return NULL; }
    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_class_from_name cf = (fn_class_from_name)dlsym(h, "il2cpp_class_from_name");
    fn_class_get_type cgt = (fn_class_get_type)dlsym(h, "il2cpp_class_get_type");
    fn_class_get_method_from_name gm = (fn_class_get_method_from_name)dlsym(h, "il2cpp_class_get_method_from_name");
    fn_runtime_invoke ri = (fn_runtime_invoke)dlsym(h, "il2cpp_runtime_invoke");
    fn_array_length al = (fn_array_length)dlsym(h, "il2cpp_array_length");

    void* d = dg(); ta(d);
    const char* ASM[] = {"Assembly-CSharp","UnityEngine.CoreModule","UnityEngine","Assembly-CSharp-firstpass"};
    void* img_core = NULL; void* img_cs = NULL;
    for (int i = 0; i < 4; i++) {
        void* a = ao(d, ASM[i]);
        if (a) { void* im = gi(a); if (im) { if (i == 1) img_core = im; else if (i == 0) img_cs = im; } }
    }
    if (!img_core) { log_app("v10: no CoreModule\n"); return NULL; }

    /* TEST 1: Time.get_time from worker thread (safe static getter) */
    void* timeCls = cf(img_core, "UnityEngine", "Time");
    if (!timeCls) { log_app("v10: Time not found\n"); return NULL; }
    void* mt = gm(timeCls, "get_time", 0);
    if (!mt) { log_app("v10: get_time not found\n"); return NULL; }
    void* exc = NULL;
    double t = 0;
    void* ret = ri(mt, NULL, NULL, &exc);
    (void)ret; (void)t;
    if (exc) { log_app("v10: Time.get_time EXCEPTION\n"); }
    else { log_app("v10: Time.get_time OK (worker thread safe) "); }

    /* TEST 2: FindObjectsOfType from worker thread */
    void* rocket = NULL;
    if (img_cs) {
        const char* ns[] = { "SFS.World", "SFS", "" };
        for (int k = 0; k < 3 && !rocket; k++) rocket = cf(img_cs, ns[k], "FULMMHJZAEKABZAY");
    }
    void* uobj = cf(img_core, "UnityEngine", "Object");
    if (!rocket || !uobj) { log_app("v10: rocket/object missing\n"); return NULL; }
    void* m = gm(uobj, "FindObjectsOfType", 1);
    if (!m) { log_app("v10: FindObjectsOfType missing\n"); return NULL; }
    void* params[1]; params[0] = cgt(rocket);
    exc = NULL;
    void* arr = ri(m, NULL, params, &exc);
    if (exc || !arr) { log_app("v10: FindObjectsOfType failed\n"); return NULL; }
    n = snprintf(buf, sizeof(buf), "ROCKETS=%d\n", al(arr));
    log_app(buf);
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res) {
    (void)vm; (void)res;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
