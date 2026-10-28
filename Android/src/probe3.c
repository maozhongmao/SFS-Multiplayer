/* SPDX-License-Identifier: GPL-3.0-or-later */
/* multiplayersfs probe #3 — built with the Android NDK clang.
 * Proves: our injected .so can dlopen libil2cpp, resolve il2cpp_* and call it.
 * Writes a marker file into the game's external files dir as positive proof.
 */
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>

#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"

static void write_marker(const char* msg) {
    int fd = open(MARKER, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return;
    write(fd, msg, strlen(msg));
    close(fd);
}

typedef void* (*fn_domain_get)(void);

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    (void)vm; (void)reserved;
    char buf[256];
    int n = 0;

    void* h = NULL;
    for (int i = 0; i < 400000 && h == NULL; i++) {
        h = dlopen("libil2cpp.so", RTLD_NOW);
    }
    if (h == NULL) {
        write_marker("probe3: libil2cpp=NO\n");
        return JNI_VERSION_1_6;
    }
    n += snprintf(buf + n, sizeof(buf) - n, "probe3: libil2cpp=OK ");

    fn_domain_get f = (fn_domain_get)dlsym(h, "il2cpp_domain_get");
    if (f == NULL) {
        snprintf(buf + n, sizeof(buf) - n, "domain_get=NO\n");
        write_marker(buf);
        return JNI_VERSION_1_6;
    }
    n += snprintf(buf + n, sizeof(buf) - n, "domain_get=OK ");
    void* d = f();
    if (d != NULL) snprintf(buf + n, sizeof(buf) - n, "domain=OK\n");
    else           snprintf(buf + n, sizeof(buf) - n, "domain=NULL\n");
    write_marker(buf);
    return JNI_VERSION_1_6;
}
