/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
static void wr(const char* m){ int fd=open(MARKER,O_WRONLY|O_CREAT|O_TRUNC,0644); if(fd>=0){write(fd,m,strlen(m));close(fd);} }
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* r) {
    (void)vm; (void)r;
    char buf[128];
    void* h = dlopen("libc.so", RTLD_NOW);
    snprintf(buf, sizeof(buf), "v4: dlopen(libc)=%s\n", h ? "OK" : "NULL");
    wr(buf);
    return JNI_VERSION_1_6;
}
