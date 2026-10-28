/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* r) {
    (void)vm; (void)r;
    const char* m = "v3: dlsym-null\n";
    void* p = dlsym(RTLD_DEFAULT, "il2cpp_domain_get");
    if (p) m = "v3: dlsym OK\n";
    int fd = open(MARKER, O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd >= 0) { write(fd, m, strlen(m)); close(fd); }
    return JNI_VERSION_1_6;
}
