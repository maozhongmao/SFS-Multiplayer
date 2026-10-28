/* SPDX-License-Identifier: GPL-3.0-or-later */
// v9: search across assemblies; enumerate Rocket instances via FindObjectsOfType
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
static void log_app(const char* m){
    int fd = open(MARKER, O_WRONLY|O_CREAT|O_APPEND, 0644);
    if (fd >= 0) { write(fd, m, strlen(m)); close(fd); }
}

typedef void* (*fn_domain_get)(void);
typedef void* (*fn_thread_attach)(void*);
typedef void* (*fn_assembly_open)(void*, const char*);
typedef void* (*fn_assembly_get_image)(void*);
typedef void* (*fn_class_from_name)(void*, const char*, const char*);
typedef void* (*fn_class_get_type)(void*);
typedef void* (*fn_class_get_method_from_name)(void*, const char*, int);
typedef void* (*fn_runtime_invoke)(void*, void*, void**, void**);
typedef int   (*fn_array_length)(void*);

static const char* ASSEMBLIES[] = {
    "Assembly-CSharp", "UnityEngine.CoreModule", "UnityEngine", "UnityEngine.PhysicsModule", "Assembly-CSharp-firstpass"
};
static int N_ASM = 5;

static void* worker(void* arg) {
    (void)arg;
    sleep(8);
    char buf[512]; int n;
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v9: dlopen NULL\n"); return NULL; }
    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_class_from_name cf = (fn_class_from_name)dlsym(h, "il2cpp_class_from_name");
    fn_class_get_type cgt = (fn_class_get_type)dlsym(h, "il2cpp_class_get_type");
    fn_class_get_method_from_name gm = (fn_class_get_method_from_name)dlsym(h, "il2cpp_class_get_method_from_name");
    fn_runtime_invoke ri = (fn_runtime_invoke)dlsym(h, "il2cpp_runtime_invoke");
    fn_array_length al = (fn_array_length)dlsym(h, "il2cpp_array_length");
    if (!dg||!ta||!ao||!gi||!cf||!cgt||!gm||!ri||!al) { log_app("v9: missing symbols\n"); return NULL; }

    void* d = dg(); ta(d);
    void* images[8]; int ni = 0;
    for (int i = 0; i < N_ASM; i++) {
        void* a = ao(d, ASSEMBLIES[i]);
        if (a) { void* im = gi(a); if (im) images[ni++] = im; }
    }
    n = snprintf(buf, sizeof(buf), "v9: assemblies=%d ", ni); log_app(buf);

    /* find Rocket (obfuscated) in any image */
    void* rocket = NULL;
    for (int i = 0; i < ni && !rocket; i++) {
        const char* ns[] = { "SFS.World", "SFS", "" };
        for (int k = 0; k < 3 && !rocket; k++) rocket = cf(images[i], ns[k], "FULMMHJZAEKABZAY");
    }
    if (!rocket) { log_app("Rocket NOT found\n"); return NULL; }
    log_app("Rocket OK ");

    /* find UnityEngine.Object in any image */
    void* uobj = NULL;
    for (int i = 0; i < ni && !uobj; i++) uobj = cf(images[i], "UnityEngine", "Object");
    if (!uobj) { log_app("UnityEngine.Object NOT found\n"); return NULL; }
    log_app("UnityEngine.Object OK ");

    void* m = gm(uobj, "FindObjectsOfType", 1);
    if (!m) { log_app("FindObjectsOfType NOT found\n"); return NULL; }
    void* rocketType = cgt(rocket);
    void* params[1]; params[0] = rocketType;
    void* exc = NULL;
    void* arr = ri(m, NULL, params, &exc);
    if (exc || !arr) { log_app("invoke failed\n"); return NULL; }
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
