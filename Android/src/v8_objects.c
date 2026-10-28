/* SPDX-License-Identifier: GPL-3.0-or-later */
// v8: enumerate live game objects — FindObjectsOfType(Rocket) and report the count
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
typedef void* (*fn_class_get_type)(void*);
typedef void* (*fn_class_get_method_from_name)(void*, const char*, int);
typedef void* (*fn_runtime_invoke)(void*, void*, void**, void**);
typedef int   (*fn_array_length)(void*);
typedef void* (*fn_object_new)(void*, void*);

static void* worker(void* arg) {
    (void)arg;
    sleep(8);
    char buf[512]; int n = 0;
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { wr("v8: dlopen NULL\n"); return NULL; }
    fn_domain_get dg = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    fn_thread_attach ta = (fn_thread_attach)dlsym(h, "il2cpp_thread_attach");
    fn_assembly_open ao = (fn_assembly_open)dlsym(h, "il2cpp_domain_assembly_open");
    fn_assembly_get_image gi = (fn_assembly_get_image)dlsym(h, "il2cpp_assembly_get_image");
    fn_class_from_name cf = (fn_class_from_name)dlsym(h, "il2cpp_class_from_name");
    fn_class_get_type cgt = (fn_class_get_type)dlsym(h, "il2cpp_class_get_type");
    fn_class_get_method_from_name gm = (fn_class_get_method_from_name)dlsym(h, "il2cpp_class_get_method_from_name");
    fn_runtime_invoke ri = (fn_runtime_invoke)dlsym(h, "il2cpp_runtime_invoke");
    fn_array_length al = (fn_array_length)dlsym(h, "il2cpp_array_length");
    if (!dg||!ta||!ao||!gi||!cf||!cgt||!gm||!ri||!al) { wr("v8: missing symbols\n"); return NULL; }

    void* d = dg(); ta(d);
    void* a = ao(d, "Assembly-CSharp");
    void* img = gi(a);

    void* rocket = cf(img, "SFS.World", "FULMMHJZAEKABZAY");
    if (!rocket) rocket = cf(img, "SFS", "FULMMHJZAEKABZAY");
    if (!rocket) { wr("v8: Rocket class not found\n"); return NULL; }
    n += snprintf(buf+n, sizeof(buf)-n, "v8: Rocket cls OK ");

    void* rocketType = cgt(rocket);
    if (!rocketType) { wr("v8: type NULL\n"); return NULL; }

    void* uobj = cf(img, "UnityEngine", "Object");
    if (!uobj) { wr("v8: UnityEngine.Object not found\n"); return NULL; }
    void* m = gm(uobj, "FindObjectsOfType", 1);
    if (!m) { wr("v8: FindObjectsOfType(Type) not found\n"); return NULL; }
    n += snprintf(buf+n, sizeof(buf)-n, "method OK ");

    void* params[1]; params[0] = rocketType;
    void* exc = NULL;
    void* arr = ri(m, NULL, params, &exc);
    if (exc) { wr("v8: invoke exception\n"); return NULL; }
    if (!arr) { wr("v8: null array\n"); return NULL; }
    int len = al(arr);
    snprintf(buf+n, sizeof(buf)-n, "ROCKETS=%d\n", len);
    wr(buf);
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res) {
    (void)vm; (void)res;
    pthread_t t;
    if (pthread_create(&t, NULL, worker, NULL) == 0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
