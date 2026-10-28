/* SPDX-License-Identifier: GPL-3.0-or-later */
// v27: Java/JNI bridge + Android overlay button.  NO IL2CPP hook at all.
// Rationale (group-member reference design): on Android the Activity/UI thread IS UnityMain,
// so anything we run from a click listener or a View.postDelayed Runnable is already on the
// Unity main thread -- game APIs can be called directly from there later.
#include <jni.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
static void log_app(const char* m){ int fd=open(MARKER,O_WRONLY|O_CREAT|O_APPEND,0644); if(fd>=0){write(fd,m,strlen(m));close(fd);} }

static JavaVM* g_vm = NULL;
static jobject g_activity = NULL;
static jclass  g_bridge = NULL;
static jobject g_btn = NULL;
static jobject g_label = NULL;
static jobject g_ticker = NULL;
static jmethodID g_mPostDelayed = NULL;
static volatile int g_click = 0;
static volatile int g_installed = 0;
static int g_ticks = 0;

/* drain and log a pending java exception, returns 1 if there was one */
static int drain(JNIEnv* env, const char* tag){
    if (!(*env)->ExceptionCheck(env)) return 0;
    jthrowable ex = (*env)->ExceptionOccurred(env);
    (*env)->ExceptionClear(env);
    char b[260];
    jclass tc = (*env)->GetObjectClass(env, ex);
    jmethodID gm = (*env)->GetMethodID(env, tc, "toString", "()Ljava/lang/String;");
    jstring js = gm ? (jstring)(*env)->CallObjectMethod(env, ex, gm) : NULL;
    const char* cs = js ? (*env)->GetStringUTFChars(env, js, NULL) : NULL;
    snprintf(b, sizeof(b), "%s EXC: %s\n", tag, cs ? cs : "?");
    log_app(b);
    if (cs) (*env)->ReleaseStringUTFChars(env, js, cs);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    return 1;
}

static void schedule(JNIEnv* env, jlong delay){
    if (g_btn && g_ticker && g_mPostDelayed){
        (*env)->CallBooleanMethod(env, g_btn, g_mPostDelayed, g_ticker, delay);
        drain(env, "v27: postDelayed");
    }
}

static jobject make_button(JNIEnv* env, const char* txt, int textsize, int pad){
    jclass cButton = (*env)->FindClass(env, "android/widget/Button");
    if (!cButton) { drain(env, "v27: FindClass Button"); return NULL; }
    jmethodID ctor = (*env)->GetMethodID(env, cButton, "<init>", "(Landroid/content/Context;)V");
    jobject btn = (*env)->NewObject(env, cButton, ctor, g_activity);
    if (drain(env, "v27: NewObject Button") || !btn) return NULL;
    jclass cTextView = (*env)->FindClass(env, "android/widget/TextView");
    jmethodID mSetText = (*env)->GetMethodID(env, cTextView, "setText", "(Ljava/lang/CharSequence;)V");
    jstring s = (*env)->NewStringUTF(env, txt);
    (*env)->CallVoidMethod(env, btn, mSetText, s);
    (*env)->DeleteLocalRef(env, s);
    jmethodID mSize = (*env)->GetMethodID(env, cTextView, "setTextSize", "(F)V");
    (*env)->CallVoidMethod(env, btn, mSize, (jfloat)textsize);
    jclass cView = (*env)->FindClass(env, "android/view/View");
    jmethodID mPad = (*env)->GetMethodID(env, cView, "setPadding", "(IIII)V");
    (*env)->CallVoidMethod(env, btn, mPad, pad, pad, pad, pad);
    drain(env, "v27: button text/pad");
    return btn;
}

static void install_overlay(JNIEnv* env){
    if (!g_activity || !g_bridge){ log_app("v27: install: no activity/bridge\n"); return; }

    jclass cActivity = (*env)->GetObjectClass(env, g_activity);
    jmethodID mGetWindow = (*env)->GetMethodID(env, cActivity, "getWindow", "()Landroid/view/Window;");
    jobject win = (*env)->CallObjectMethod(env, g_activity, mGetWindow);
    if (drain(env, "v27: getWindow") || !win) return;
    jclass cWindow = (*env)->FindClass(env, "android/view/Window");
    jmethodID mGetDecor = (*env)->GetMethodID(env, cWindow, "getDecorView", "()Landroid/view/View;");
    jobject decor = (*env)->CallObjectMethod(env, win, mGetDecor);
    if (drain(env, "v27: getDecorView") || !decor) return;

    jobject btn = make_button(env, "\xe5\xa4\x9a\xe4\xba\xba MP", 15, 26);
    if (!btn) return;
    /* click listener = Bridge.INSTANCE (implements View.OnClickListener in smali) */
    jfieldID fInst = (*env)->GetStaticFieldID(env, g_bridge, "INSTANCE", "Lcom/sfsmp/bridge/Bridge;");
    jobject listener = fInst ? (*env)->GetStaticObjectField(env, g_bridge, fInst) : NULL;
    jclass cView = (*env)->FindClass(env, "android/view/View");
    jmethodID mSetClick = (*env)->GetMethodID(env, cView, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V");
    if (listener && mSetClick) (*env)->CallVoidMethod(env, btn, mSetClick, listener);
    drain(env, "v27: setOnClickListener");

    /* android.widget.FrameLayout.LayoutParams(WRAP_CONTENT= -2, -2) ; gravity TOP|LEFT ; margins */
    jclass cFLP = (*env)->FindClass(env, "android/widget/FrameLayout$LayoutParams");
    jmethodID ctorP = (*env)->GetMethodID(env, cFLP, "<init>", "(II)V");
    jobject lp = (*env)->NewObject(env, cFLP, ctorP, -2, -2);
    jfieldID fGrav = (*env)->GetFieldID(env, cFLP, "gravity", "I");
    jfieldID fLM = (*env)->GetFieldID(env, cFLP, "leftMargin", "I");
    jfieldID fTM = (*env)->GetFieldID(env, cFLP, "topMargin", "I");
    if (lp && fGrav) (*env)->SetIntField(env, lp, fGrav, 51);   /* Gravity.TOP|LEFT */
    if (lp && fLM) (*env)->SetIntField(env, lp, fLM, 30);
    if (lp && fTM) (*env)->SetIntField(env, lp, fTM, 300);
    drain(env, "v27: layoutparams");

    jclass cFrame = (*env)->FindClass(env, "android/widget/FrameLayout");
    jmethodID mAdd = (*env)->GetMethodID(env, cFrame, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
    (*env)->CallVoidMethod(env, decor, mAdd, btn, lp);
    if (drain(env, "v27: addView(button)")) {
        /* keep the view even if layout params failed: retry plain addView(View) */
        jmethodID mAdd1 = (*env)->GetMethodID(env, cFrame, "addView", "(Landroid/view/View;)V");
        (*env)->CallVoidMethod(env, decor, mAdd1, btn);
        drain(env, "v27: addView(button,1arg)");
    }
    g_btn = (*env)->NewGlobalRef(env, btn);

    /* status label under the button */
    jobject lab = make_button(env, "MPS idle", 12, 10);
    if (lab){
        jobject lp2 = (*env)->NewObject(env, cFLP, ctorP, -2, -2);
        if (lp2 && fGrav) (*env)->SetIntField(env, lp2, fGrav, 51);
        if (lp2 && fLM)  (*env)->SetIntField(env, lp2, fLM, 30);
        if (lp2 && fTM)  (*env)->SetIntField(env, lp2, fTM, 380);
        (*env)->CallVoidMethod(env, decor, mAdd, lab, lp2);
        drain(env, "v27: addView(label)");
        g_label = (*env)->NewGlobalRef(env, lab);
    }

    /* ticker Runnable instance (smali class) */
    jclass cTicker = (*env)->FindClass(env, "com/sfsmp/bridge/Ticker");
    if (cTicker){
        jmethodID ctorT = (*env)->GetMethodID(env, cTicker, "<init>", "()V");
        jobject tk = (*env)->NewObject(env, cTicker, ctorT);
        if (tk && !drain(env, "v27: new Ticker")) g_ticker = (*env)->NewGlobalRef(env, tk);
    } else drain(env, "v27: FindClass Ticker");
    g_mPostDelayed = (*env)->GetMethodID(env, cView, "postDelayed", "(Ljava/lang/Runnable;J)Z");
    drain(env, "v27: get postDelayed");

    g_installed = 1;
    log_app("v27: overlay installed (button + label)\n");
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved){
    (void)reserved; g_vm = vm;
    log_app("v27: JNI_OnLoad\n");
    return JNI_VERSION_1_6;
}

/* called from Bridge.bridgeInit(activity) inside UnityPlayerActivity.onCreate */
JNIEXPORT void JNICALL Java_com_sfsmp_bridge_Bridge_nativeInit(JNIEnv* env, jclass cls, jobject act){
    log_app("v27: nativeInit called\n");
    if (act) g_activity = (*env)->NewGlobalRef(env, act);
    if (cls) g_bridge = (jclass)(*env)->NewGlobalRef(env, cls);
    install_overlay(env);
}

/* runs on the Android UI thread == UnityMain */
JNIEXPORT void JNICALL Java_com_sfsmp_bridge_Bridge_nativeTick(JNIEnv* env, jclass cls){
    (void)cls;
    g_ticks++;
    if (!g_installed) install_overlay(env);
    if (g_ticks % 5 == 0){
        char b[160];
        snprintf(b, sizeof(b), "v27: tick %d clicks=%d installed=%d\n", g_ticks, g_click, g_installed);
        log_app(b);
    }
    schedule(env, 1000);
}

JNIEXPORT void JNICALL Java_com_sfsmp_bridge_Bridge_nativeOnClick(JNIEnv* env, jclass cls, jobject v){
    (void)env; (void)cls; (void)v;
    g_click++;
    char b[120];
    snprintf(b, sizeof(b), "v27: *** BUTTON TAPPED *** n=%d\n", g_click);
    log_app(b);
}
