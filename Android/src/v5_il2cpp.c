/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
static void wr(const char* m){ int fd=open(MARKER,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,m,strlen(m));close(fd);} }
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* r) {
    (void)vm; (void)r;
    char buf[160];
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { snprintf(buf,sizeof(buf),"v5: dlopen(libil2cpp)=NULL (%s)\n", dlerror()); wr(buf); return JNI_VERSION_1_6; }
    void* p = dlsym(h, "il2cpp_domain_get");
    snprintf(buf,sizeof(buf),"v5: libil2cpp=OK dlsym=%s\n", p ? "OK" : "NULL");
    wr(buf);
    return JNI_VERSION_1_6;
}
