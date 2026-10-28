/* SPDX-License-Identifier: GPL-3.0-or-later */
// v29: SFS-Pro style in-game button, selected by CODE POINTER (RVA) validation.
//   * keeps the v22/v26 IL2CPP plumbing + WorldTime.Update main-thread hook
//   * adds the v27 Android overlay button, but the button does NOT draw our UI any more:
//     tapping it just sets a flag, and the clone is executed on the next WorldTime.Update
//     tick (= proven UnityMain thread).
//   * Instantiate overload is picked by (methodPointer - libil2cpp base) == RVA from dump.cs:
//         Instantiate(Object,Transform,bool)  RVA 0x2FC1B58   <- the one SFS-Pro uses
//         Instantiate(Object)                 RVA 0x2FC19C4
//         get_name RVA 0x2FC1138 / set_name RVA 0x2FC1280 are used to calibrate the base.
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>

#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
#define MODEFILE "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-mode.txt"
static volatile int g_mode = 1;
static int read_mode(void){
    int fd = open(MODEFILE, O_RDONLY);
    if (fd < 0) return 1;
    char b[16]; int n = (int)read(fd, b, 15); close(fd);
    if (n <= 0) return 1;
    b[n] = 0;
    int m = atoi(b);
    if (m < 0 || m > 9) m = 1;
    return m;
}
static void log_app(const char* m){ int fd=open(MARKER,O_WRONLY|O_CREAT|O_APPEND,0644); if(fd>=0){write(fd,m,strlen(m));close(fd);} }

typedef void* (*fn0)(void);
typedef void* (*fn_ta)(void*);
typedef void* (*fn_ao)(void*, const char*);
typedef void* (*fn_gi)(void*);
typedef void* (*fn_cf)(void*, const char*, const char*);
typedef void* (*fn_gm)(void*, const char*, int);
typedef void* (*fn_cgfn)(void*, const char*);
typedef void* (*fn_fgt)(void*);
typedef void* (*fn_tgoc)(void*);
typedef void* (*fn_tgo)(void*);
typedef void* (*fn_cgt)(void*);
typedef int32_t (*fn_fgo)(void*);
typedef void* (*fn_ogc)(void*);
typedef const char* (*fn_cgn)(void*);
typedef void* (*fn_ri)(void*, void*, void**, void**);
typedef size_t (*fn_alg)(void*);
typedef void* (*fn_unbox)(void*);
typedef uint16_t* (*fn_schars)(void*);
typedef int (*fn_mp)(void*, size_t, int);
typedef void* (*fn_snew)(const char*);
typedef void* (*fn_cms)(void*, void**);
typedef const char* (*fn_mn)(void*);
typedef int32_t (*fn_mpc)(void*);
typedef void* (*fn_mgp)(void*, uint32_t);
typedef int32_t (*fn_tgt)(void*);
typedef int32_t (*fn_mig)(void*);
typedef const char* (*fn_tn)(void*);

static void* g_orig_update = NULL;
static void* g_findMethod = NULL, *g_typeObj = NULL, *g_rocketCls = NULL;
static void* g_mGetRot = NULL, *g_mGetTransform = NULL, *g_mGetName = NULL;
static void* g_mGetChildCount = NULL, *g_mGetChild = NULL, *g_mGetValue = NULL;
static void* g_canvasCls = NULL, *g_typeCanvas = NULL;
static void* g_rectCls = NULL, *g_typeRect = NULL, *g_mGetParent = NULL;
static void* g_mInstantiate = NULL, *g_mSetName = NULL, *g_mGetGO = NULL;
static void* g_mGetComponent = NULL, *g_mSetSibling = NULL, *g_mSetText = NULL;
static void* g_tmpCls = NULL, *g_typeTmp = NULL, *g_mGetText = NULL;
static void* objCls_ = NULL;
static void* g_setParent = NULL;
static void* g_mInst1 = NULL, *g_mInst3 = NULL;
static fn_snew g_sNew = NULL;
static fn_cms g_cms=NULL; static fn_mn g_mn=NULL; static fn_mpc g_mpc=NULL;
static fn_mgp g_mgp=NULL; static fn_tgt g_tgt=NULL; static fn_mig g_mig=NULL;
static fn_tgoc g_tgoc=NULL; static fn_tn g_tn=NULL;
static volatile int g_pick_kind = 0;
static volatile int g_clone_done = 0;
static volatile int g_do_clone = 0;
static int g_offName = -1, g_offLoc = -1;
static fn_ri g_ri=NULL; static fn_alg g_alg=NULL; static fn_unbox g_unbox=NULL; static fn_schars g_schars=NULL;
static volatile int g_frame = 0;
static volatile int g_ready = 0;
static volatile int g_scene_done = 0;
static uintptr_t g_base = 0;

#define RVA_GET_NAME 0x2FC1138ULL
#define RVA_SET_NAME 0x2FC1280ULL
#define RVA_INST_3   0x2FC1B58ULL
#define RVA_INST_1   0x2FC19C4ULL

/* ---- JNI / Android side (from v27) ---- */
static JavaVM* g_vm = NULL;
static jobject g_activity = NULL;
static jclass  g_bridge = NULL;
static jobject g_btn = NULL;
static jobject g_label = NULL;
static jobject g_ticker = NULL;
static jmethodID g_mPostDelayed = NULL;
static volatile int g_click = 0;
static volatile int g_installed = 0;
static volatile int g_ui_ready = 0;
static int g_ticks = 0;

static void u16_to_ascii(void* s, char* out, int cap){
    out[0]=0; if(!s) return;
    int len = *(int*)((char*)s + 0x10);
    uint16_t* ch = g_schars(s);
    if(!ch) return;
    int k=0;
    for(int j=0;j<len && k<cap-1;j++){ unsigned c=ch[j]; out[k++]=(c<128)?(char)c:(char)'?'; }
    out[k]=0;
}

static void obj_name(void* o, char* out, int cap){
    out[0]=0;
    if (!o || !g_mGetName) return;
    void* e=NULL;
    void* s = g_ri(g_mGetName, o, NULL, &e);
    if (s && !e) u16_to_ascii(s, out, cap);
}

static void dump_scene(void){
    if (g_scene_done) return;
    g_scene_done = 1;
    log_app("m5: === scene explorer (rect transforms) ===\n");
    if (!g_typeRect || !g_mGetTransform || !g_mGetParent) { log_app("m5: no rect plumbing\n"); return; }
    void* params[1]; params[0] = g_typeRect;
    void* exc=NULL;
    void* arr = g_ri(g_findMethod, NULL, params, &exc);
    if (exc || !arr) { log_app("m5: rect find failed\n"); return; }
    int n = (int)g_alg(arr);
    char b[200];
    snprintf(b,sizeof(b),"m5: rects=%d\n", n);
    log_app(b);
    int lim = n>40?40:n;
    for (int i=0;i<lim;i++){
        void* r = ((void**)((char*)arr+0x20))[i];
        if (!r) continue;
        void* e=NULL;
        void* tr = g_ri(g_mGetTransform, r, NULL, &e);
        if (!tr || e) continue;
        char nm[40]; obj_name(tr, nm, 40);
        char pn[40]; pn[0]=0;
        void* e2=NULL;
        void* par = g_ri(g_mGetParent, tr, NULL, &e2);
        if (par && !e2) obj_name(par, pn, 40);
        snprintf(b,sizeof(b),"m5: rect[%d] '%s'  parent='%s'\n", i, nm, pn);
        log_app(b);
    }
    log_app("m5: === scene explorer end ===\n");
}

static void dump_rockets(void){
    void* params[1]; params[0] = g_typeObj;
    void* exc = NULL;
    void* arr = g_ri(g_findMethod, NULL, params, &exc);
    if (exc || !arr) return;
    int n = arr ? (int)g_alg(arr) : 0;
    char b[300];
    for (int i=0;i<(n>2?2:n);i++){
        void* ro = ((void**)((char*)arr + 0x20))[i];
        if (!ro) continue;
        char nm[48]; nm[0]=0;
        if (g_offName>=0) u16_to_ascii(*(void**)((char*)ro + g_offName), nm, 48);
        void* loc = (g_offLoc>=0) ? *(void**)((char*)ro + g_offLoc) : NULL;
        double t=0, px=0, py=0, vx=0, vy=0;
        char pname[40]; pname[0]=0;
        if (loc && g_mGetValue){
            void* e3=NULL;
            void* lv = g_ri(g_mGetValue, loc, NULL, &e3);
            if (lv && !e3){
                t  = *(double*)((char*)lv + 0x10);
                px = *(double*)((char*)lv + 0x20);
                py = *(double*)((char*)lv + 0x28);
                vx = *(double*)((char*)lv + 0x30);
                vy = *(double*)((char*)lv + 0x38);
                void* pl = *(void**)((char*)lv + 0x18);
                if (pl) obj_name(pl, pname, 40);
            }
        }
        float rot=0.0f;
        if (g_mGetRot){ void* e4=NULL; void* r=g_ri(g_mGetRot,ro,NULL,&e4); if(r&&!e4) rot=*(float*)g_unbox(r); }
        snprintf(b,sizeof(b),"m1: [%d] name='%s' planet='%s' t=%.2f pos=(%.3f, %.3f) vel=(%.3f, %.3f) rot=%.2f\n",
                 i, nm, pname, t, px, py, vx, vy, rot);
        log_app(b);
    }
}

static void char_name(void* comp, char* out, int cap){
    out[0]=0;
    if (!comp || !g_mGetTransform) return;
    void* e=NULL;
    void* tr = g_ri(g_mGetTransform, comp, NULL, &e);
    if (tr && !e) obj_name(tr, out, cap);
}

/* v29: enumerate the Instantiate overloads and identify them by code pointer (RVA). */
static void select_instantiate(void* objCls, void* trCls){
    char b[380];
    if (!g_cms || !g_mn || !g_mpc || !g_mgp) { log_app("v29: no reflection api\n"); return; }
    if (g_mGetName){
        uintptr_t p = (uintptr_t)(*(void**)g_mGetName);
        g_base = p - (uintptr_t)RVA_GET_NAME;
        snprintf(b,sizeof(b),"v29: base=0x%llx (get_name ptr=0x%llx)\n",(unsigned long long)g_base,(unsigned long long)p);
        log_app(b);
        if (g_mSetName){
            uintptr_t p2=(uintptr_t)(*(void**)g_mSetName);
            uintptr_t ex=(uintptr_t)(g_base + (uintptr_t)RVA_SET_NAME);
            snprintf(b,sizeof(b),"v29: calib set_name ptr=0x%llx expect=0x%llx %s\n",(unsigned long long)p2,(unsigned long long)ex,(p2==ex)?"OK":"MISMATCH");
            log_app(b);
        }
    }
    void* it=NULL; void* m; int cnt=0;
    while ((m=g_cms(objCls,&it))!=NULL){
        const char* nm=g_mn(m);
        if(!nm||strcmp(nm,"Instantiate")!=0) continue;
        int pc=g_mpc(m); int gen=g_mig?g_mig(m):0;
        void* ptr=*(void**)m;
        unsigned long long rva=(unsigned long long)((uintptr_t)ptr - (uintptr_t)g_base);
        char ps[180]; ps[0]=0;
        for(int i=0;i<pc&&i<5;i++){
            const char* tn=g_tn?g_tn(g_mgp(m,(uint32_t)i)):NULL;
            char one[44]; snprintf(one,sizeof(one)," [%s]",tn?tn:"?");
            strncat(ps,one,sizeof(ps)-strlen(ps)-1);
        }
        int hit3 = (!gen && rva==RVA_INST_3);
        int hit1 = (!gen && rva==RVA_INST_1);
        snprintf(b,sizeof(b),"v29: inst pc=%d gen=%d rva=0x%llx%s%s%s\n",pc,gen,rva,hit3?"  <== MATCH3":"",hit1?"  <== MATCH1":"",ps);
        log_app(b);
        if (hit3 && !g_mInst3) g_mInst3=m;
        if (hit1 && !g_mInst1) g_mInst1=m;
        cnt++;
    }
    snprintf(b,sizeof(b),"v29: instantiate total=%d m1=%p m3=%p\n",cnt,g_mInst1,g_mInst3);
    log_app(b);
    if (!g_mInst3 && !g_mInst1 && g_tgoc && g_tgt){
        void* it2=NULL; void* mm;
        while ((mm=g_cms(objCls,&it2))!=NULL){
            const char* nm=g_mn(mm);
            if(!nm||strcmp(nm,"Instantiate")!=0) continue;
            if (g_mig && g_mig(mm)) continue;
            int pc=g_mpc(mm);
            if (pc==3 && g_tgoc(g_mgp(mm,0))==objCls && g_tgoc(g_mgp(mm,1))==trCls && g_tgt(g_mgp(mm,2))==2){ g_mInst3=mm; log_app("v29: fallback pc=3 (Object,Transform,bool)\n"); break; }
            if (pc==1 && g_tgoc(g_mgp(mm,0))==objCls && !g_mInst1) g_mInst1=mm;
        }
    }
    g_mInstantiate = g_mInst3 ? g_mInst3 : g_mInst1;
    g_pick_kind = g_mInst3 ? 3 : (g_mInst1 ? 1 : 0);
}

static void ui_clone(void){
    if (g_clone_done) return;
    g_clone_done = 1;
    char b[240];
    log_app("m5: --- clone attempt (cloned off the game's own button) ---\n");
    if (!g_typeRect || !g_mInstantiate || !g_mGetGO) { log_app("m5: missing plumbing\n"); return; }
    void* params[1]; params[0] = g_typeRect;
    void* exc=NULL;
    void* arr = g_ri(g_findMethod, NULL, params, &exc);
    if (exc || !arr) { log_app("m5: rect find failed\n"); return; }
    int n = (int)g_alg(arr);
    void* srcComp = NULL;
    for (int i=0;i<n;i++){
        void* r = ((void**)((char*)arr+0x20))[i];
        if (!r) continue;
        char nm[40]; char_name(r, nm, 40);
        if (strcmp(nm, "Map Button") == 0) { srcComp = r; break; }
    }
    if (!srcComp) { log_app("m5: 'Map Button' not found\n"); return; }
    log_app("m5: found Map Button\n");
    void* e=NULL;
    void* srcTr = g_ri(g_mGetTransform, srcComp, NULL, &e);
    void* srcGo = g_ri(g_mGetGO, srcComp, NULL, &e);
    void* par   = (srcTr && !e) ? g_ri(g_mGetParent, srcTr, NULL, &e) : NULL;
    snprintf(b,sizeof(b),"m5: src tr=%p go=%p parent=%p\n", srcTr, srcGo, par); log_app(b);
    if (!srcGo) return;
    int mode = read_mode();
    snprintf(b,sizeof(b),"m5: MODE=%d\n", mode); log_app(b);
    {   /* prove the source object is alive before cloning */
        char nm[48]; obj_name(srcGo, nm, 48);
        snprintf(b,sizeof(b),"m5: srcGo name='%s'\n", nm); log_app(b);
    }
    if (mode == 0){ log_app("m5: mode 0 = dump only, no clone\n"); return; }
    void* clone = NULL;
    if (mode == 1 || mode == 3 || mode == 4){
        if (!g_mInstantiate){ log_app("m5: no method\n"); return; }
        if (mode == 1){
            log_app("m5: mode1 runtime_invoke Instantiate(Object) ip=&obj\n");
            void* a0 = srcGo; void* ip[1]; ip[0]=&a0; void* e2=NULL;
            clone = g_ri(g_mInst1, NULL, ip, &e2);
            snprintf(b,sizeof(b),"m5: -> clone=%p exc=%p\n", clone, e2); log_app(b);
        } else if (mode == 3){
            log_app("m5: mode3 runtime_invoke Instantiate(Object,Transform,bool) with parent\n");
            void* a0 = srcGo; void* a1 = par; int32_t a2 = 0;
            void* ip[3]; ip[0]=&a0; ip[1]=&a1; ip[2]=&a2; void* e2=NULL;
            clone = g_ri(g_mInst3, NULL, ip, &e2);
            snprintf(b,sizeof(b),"m5: -> clone=%p exc=%p\n", clone, e2); log_app(b);
        } else {
            log_app("m5: mode4 runtime_invoke (Object,Transform,bool) with parent=NULL\n");
            void* a0 = srcGo; void* a1 = NULL; int32_t a2 = 0;
            void* ip[3]; ip[0]=&a0; ip[1]=&a1; ip[2]=&a2; void* e2=NULL;
            clone = g_ri(g_mInst3, NULL, ip, &e2);
            snprintf(b,sizeof(b),"m5: -> clone=%p exc=%p\n", clone, e2); log_app(b);
        }
    } else if (mode == 2){
        log_app("m5: mode2 DIRECT call Instantiate(Object) compiled fn\n");
        void* p = *(void**)g_mInst1;
        snprintf(b,sizeof(b),"m5: fn=%p arg0=%p\n", p, srcGo); log_app(b);
        clone = ((void*(*)(void*))p)(srcGo);
        snprintf(b,sizeof(b),"m5: -> clone=%p\n", clone); log_app(b);
    } else if (mode == 5){
        log_app("m5: mode5 runtime_invoke Instantiate(Object,Vector3,Quaternion,Transform)\n");
        void* m4 = NULL;
        {   void* it=NULL; void* mm;
            while ((mm=g_cms(objCls_,&it))!=NULL){
                const char* nm=g_mn(mm);
                if (!nm || strcmp(nm,"Instantiate")!=0) continue;
                if (g_mig && g_mig(mm)) continue;
                if ((int)((uintptr_t)(*(void**)mm) - (uintptr_t)g_base) == 0x2FC16F8){ m4 = mm; break; }
            }
        }
        if (!m4){ log_app("m5: no 4-arg Instantiate\n"); return; }
        float vbuf[3] = {0,0,0}; float qbuf[4] = {0,0,0,1};
        void* a0 = srcGo; void* a1 = vbuf; void* a2 = qbuf; void* a3 = par;
        void* ip[4]; ip[0]=&a0; ip[1]=&a1; ip[2]=&a2; ip[3]=&a3; void* e2=NULL;
        clone = g_ri(m4, NULL, ip, &e2);
        snprintf(b,sizeof(b),"m5: -> clone=%p exc=%p\n", clone, e2); log_app(b);
    } else if (mode == 6){
        log_app("m5: mode6 DIRECT call Instantiate(Object,Transform,bool) compiled fn\n");
        void* p = *(void**)g_mInst3;
        snprintf(b,sizeof(b),"m5: fn=%p orig=%p parent=%p\n", p, srcGo, par); log_app(b);
        clone = ((void*(*)(void*,void*,int))p)(srcGo, par, 0);
        snprintf(b,sizeof(b),"m5: -> clone=%p\n", clone); log_app(b);
    } else { log_app("m5: unknown mode\n"); return; }
    if (!clone) { log_app("m5: clone is NULL, stop\n"); return; }
    {
        char nm[48]; obj_name(clone, nm, 48);
        snprintf(b,sizeof(b),"m5: *** CLONE OK *** clone=%p name='%s'\n", clone, nm); log_app(b);
    }
    if (1){
        if (g_mSetName && g_sNew){
            void* sname = g_sNew("MP Button");
            void* np[1]; np[0]=&sname;
            void* e3=NULL;
            g_ri(g_mSetName, clone, np, &e3);
            snprintf(b,sizeof(b),"m5: set_name exc=%p\n", e3); log_app(b);
        }
        if (g_mSetSibling && srcTr && par){
            void* e4=NULL; void* e5=NULL;
            void* cTr = g_ri(g_mGetTransform, clone, NULL, &e4);
            void* pc  = g_ri(g_mGetChildCount, par, NULL, &e5);
            int32_t cnt = (pc && !e5) ? *(int32_t*)g_unbox(pc) : 1;
            int32_t idx = cnt;
            void* sp[1]; sp[0]=&idx;
            void* e6=NULL;
            if (cTr && !e4) g_ri(g_mSetSibling, cTr, sp, &e6);
            snprintf(b,sizeof(b),"m5: set_sibling idx=%d exc=%p\n", cnt, e6); log_app(b);
        }
        if (g_mGetComponent && g_typeTmp && g_mSetText && g_mGetChild){
            void* e7=NULL;
            void* cTr = g_ri(g_mGetTransform, clone, NULL, &e7);
            void* cc = (cTr && !e7) ? g_ri(g_mGetChildCount, cTr, NULL, &e7) : NULL;
            int cnt = (cc && !e7) ? *(int32_t*)g_unbox(cc) : 0;
            snprintf(b,sizeof(b),"m5: clone children=%d\n", cnt); log_app(b);
            int hit = 0;
            for (int i=0;i<cnt && !hit;i++){
                int32_t ci = i;
                void* gp[1]; gp[0]=&ci;
                void* e8=NULL;
                void* cch = g_ri(g_mGetChild, cTr, gp, &e8);
                if (!cch || e8) continue;
                void* cgo = g_ri(g_mGetGO, cch, NULL, &e8);
                if (!cgo) continue;
                void* tp[1]; tp[0]=&g_typeTmp;
                void* e9=NULL;
                void* tmp = g_ri(g_mGetComponent, cgo, tp, &e9);
                if (tmp && !e9){
                    void* txt = g_sNew("\xE5\xA4\x9A\xE4\xBA\xBA MP");
                    void* tp2[1]; tp2[0]=&txt;
                    void* e10=NULL;
                    g_ri(g_mSetText, tmp, tp2, &e10);
                    snprintf(b,sizeof(b),"m5: TMP set_text child=%d exc=%p\n", i, e10); log_app(b);
                    hit = 1;
                }
            }
            if (!hit) log_app("m5: no TMP_Text in direct children\n");
        }
        log_app("m5: *** CLONE DONE (check the HUD) ***\n");
    }
}

static void hook_update(void* self){
    int f = __sync_add_and_fetch(&g_frame, 1);
    if (g_ready && f % 300 == 1) dump_rockets();
    if (g_ready && f == 120) dump_scene();
    if (g_ready && g_do_clone){ g_do_clone = 0; ui_clone(); }
    if (g_orig_update) ((void(*)(void*))g_orig_update)(self);
}

/* ---------- Android overlay (independent of IL2CPP) ---------- */
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
        drain(env, "v29: postDelayed");
    }
}

static jobject make_button(JNIEnv* env, const char* txt, int textsize, int pad){
    jclass cButton = (*env)->FindClass(env, "android/widget/Button");
    if (!cButton) { drain(env, "v29: FindClass Button"); return NULL; }
    jmethodID ctor = (*env)->GetMethodID(env, cButton, "<init>", "(Landroid/content/Context;)V");
    jobject btn = (*env)->NewObject(env, cButton, ctor, g_activity);
    if (drain(env, "v29: NewObject Button") || !btn) return NULL;
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
    drain(env, "v29: button text/pad");
    return btn;
}

static void set_label(JNIEnv* env, const char* txt){
    if (!g_label) return;
    jclass cTextView = (*env)->FindClass(env, "android/widget/TextView");
    jmethodID mSetText = (*env)->GetMethodID(env, cTextView, "setText", "(Ljava/lang/CharSequence;)V");
    jstring s = (*env)->NewStringUTF(env, txt);
    (*env)->CallVoidMethod(env, g_label, mSetText, s);
    (*env)->DeleteLocalRef(env, s);
    drain(env, "v29: set_label");
}

static void install_overlay(JNIEnv* env){
    if (!g_activity || !g_bridge){ log_app("v29: install: no activity/bridge\n"); return; }
    jclass cActivity = (*env)->GetObjectClass(env, g_activity);
    jmethodID mGetWindow = (*env)->GetMethodID(env, cActivity, "getWindow", "()Landroid/view/Window;");
    jobject win = (*env)->CallObjectMethod(env, g_activity, mGetWindow);
    if (drain(env, "v29: getWindow") || !win) return;
    jclass cWindow = (*env)->FindClass(env, "android/view/Window");
    jmethodID mGetDecor = (*env)->GetMethodID(env, cWindow, "getDecorView", "()Landroid/view/View;");
    jobject decor = (*env)->CallObjectMethod(env, win, mGetDecor);
    if (drain(env, "v29: getDecorView") || !decor) return;

    jobject btn = make_button(env, "\xe5\x90\x8c\xe6\xad\xa5\xe6\xb5\x8b\xe8\xaf\x95", 13, 18);
    if (!btn) return;
    jfieldID fInst = (*env)->GetStaticFieldID(env, g_bridge, "INSTANCE", "Lcom/sfsmp/bridge/Bridge;");
    jobject listener = fInst ? (*env)->GetStaticObjectField(env, g_bridge, fInst) : NULL;
    jclass cView = (*env)->FindClass(env, "android/view/View");
    jmethodID mSetClick = (*env)->GetMethodID(env, cView, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V");
    if (listener && mSetClick) (*env)->CallVoidMethod(env, btn, mSetClick, listener);
    drain(env, "v29: setOnClickListener");

    jclass cFLP = (*env)->FindClass(env, "android/widget/FrameLayout$LayoutParams");
    jmethodID ctorP = (*env)->GetMethodID(env, cFLP, "<init>", "(II)V");
    jfieldID fGrav = (*env)->GetFieldID(env, cFLP, "gravity", "I");
    jfieldID fLM = (*env)->GetFieldID(env, cFLP, "leftMargin", "I");
    jfieldID fTM = (*env)->GetFieldID(env, cFLP, "topMargin", "I");
    jclass cFrame = (*env)->FindClass(env, "android/widget/FrameLayout");
    jmethodID mAdd = (*env)->GetMethodID(env, cFrame, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");

    jobject lp = (*env)->NewObject(env, cFLP, ctorP, -2, -2);
    if (lp && fGrav) (*env)->SetIntField(env, lp, fGrav, 51);
    if (lp && fLM) (*env)->SetIntField(env, lp, fLM, 24);
    if (lp && fTM) (*env)->SetIntField(env, lp, fTM, 150);
    (*env)->CallVoidMethod(env, decor, mAdd, btn, lp);
    drain(env, "v29: addView(button)");
    g_btn = (*env)->NewGlobalRef(env, btn);

    jobject lab = make_button(env, "MP: enter a world, then tap", 10, 8);
    if (lab){
        jobject lp2 = (*env)->NewObject(env, cFLP, ctorP, -2, -2);
        int ltop = 150 + 130;
        if (lp2 && fGrav) (*env)->SetIntField(env, lp2, fGrav, 51);
        if (lp2 && fLM) (*env)->SetIntField(env, lp2, fLM, 24);
        if (lp2 && fTM) (*env)->SetIntField(env, lp2, fTM, ltop);
        (*env)->CallVoidMethod(env, decor, mAdd, lab, lp2);
        drain(env, "v29: addView(label)");
        g_label = (*env)->NewGlobalRef(env, lab);
    }
    jclass cTicker = (*env)->FindClass(env, "com/sfsmp/bridge/Ticker");
    if (cTicker){
        jmethodID ctorT = (*env)->GetMethodID(env, cTicker, "<init>", "()V");
        jobject tk = (*env)->NewObject(env, cTicker, ctorT);
        if (tk && !drain(env, "v29: new Ticker")) g_ticker = (*env)->NewGlobalRef(env, tk);
    } else drain(env, "v29: FindClass Ticker");
    g_mPostDelayed = (*env)->GetMethodID(env, cView, "postDelayed", "(Ljava/lang/Runnable;J)Z");
    drain(env, "v29: get postDelayed");
    g_installed = 1;
    log_app("v29: overlay installed (button + label)\n");
}

JNIEXPORT void JNICALL Java_com_sfsmp_bridge_Bridge_nativeInit(JNIEnv* env, jclass cls, jobject act){
    log_app("v29: nativeInit called\n");
    if (act) g_activity = (*env)->NewGlobalRef(env, act);
    if (cls) g_bridge = (jclass)(*env)->NewGlobalRef(env, cls);
    install_overlay(env);
}

JNIEXPORT void JNICALL Java_com_sfsmp_bridge_Bridge_nativeTick(JNIEnv* env, jclass cls){
    (void)cls;
    g_ticks++;
    if (!g_installed) install_overlay(env);
    if (g_ticks == 2) log_app("v29: ticker alive on UI thread\n");
    if (g_ready && !g_ui_ready){ g_ui_ready = 1; set_label(env, "MP: ready - tap to build button"); }
    schedule(env, 1000);
}

JNIEXPORT void JNICALL Java_com_sfsmp_bridge_Bridge_nativeOnClick(JNIEnv* env, jclass cls, jobject v){
    (void)cls; (void)v;
    g_click++;
    char b[140];
    if (!g_ready){
        snprintf(b,sizeof(b),"v29: tap #%d but IL2CPP not ready yet\n", g_click);
        log_app(b);
        set_label(env, "MP: not ready yet");
        return;
    }
    g_do_clone = 1;
    snprintf(b,sizeof(b),"v29: *** TAP #%d -> clone queued for next main-thread tick ***\n", g_click);
    log_app(b);
    set_label(env, "MP: cloning the game button...");
}

static void* worker(void* arg){
    (void)arg;
    sleep(8);
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v29: no il2cpp\n"); return NULL; }
    fn0 dg=(fn0)dlsym(h,"il2cpp_domain_get");
    fn_ta ta=(fn_ta)dlsym(h,"il2cpp_thread_attach");
    fn_ao ao=(fn_ao)dlsym(h,"il2cpp_domain_assembly_open");
    fn_gi gi=(fn_gi)dlsym(h,"il2cpp_assembly_get_image");
    fn_cf cf=(fn_cf)dlsym(h,"il2cpp_class_from_name");
    fn_gm gm=(fn_gm)dlsym(h,"il2cpp_class_get_method_from_name");
    fn_cgfn cgfn=(fn_cgfn)dlsym(h,"il2cpp_class_get_field_from_name");
    fn_fgt fgt=(fn_fgt)dlsym(h,"il2cpp_field_get_type");
    fn_tgoc tgoc=(fn_tgoc)dlsym(h,"il2cpp_type_get_class_or_element_class");
    fn_tgo tgo=(fn_tgo)dlsym(h,"il2cpp_type_get_object");
    fn_cgt cgt=(fn_cgt)dlsym(h,"il2cpp_class_get_type");
    fn_fgo fgo=(fn_fgo)dlsym(h,"il2cpp_field_get_offset");
    fn_ri ri=(fn_ri)dlsym(h,"il2cpp_runtime_invoke");
    fn_alg alg=(fn_alg)dlsym(h,"il2cpp_array_length");
    fn_unbox unbox=(fn_unbox)dlsym(h,"il2cpp_object_unbox");
    fn_schars schars=(fn_schars)dlsym(h,"il2cpp_string_chars");
    fn_mp mp=(fn_mp)dlsym(dlopen("libc.so",RTLD_NOW),"mprotect");
    if(!dg||!ta||!ao||!gi||!cf||!gm||!cgfn||!fgt||!tgoc||!tgo||!cgt||!fgo||!ri||!alg||!unbox||!schars||!mp){ log_app("v29: missing syms\n"); return NULL; }
    g_ri=ri; g_alg=alg; g_unbox=unbox; g_schars=schars;
    void* d=dg(); ta(d);
    void* a=ao(d,"Assembly-CSharp"); if(!a){ log_app("v29: no asm\n"); return NULL; }
    void* img=gi(a); if(!img){ log_app("v29: no img\n"); return NULL; }
    void* coreA=ao(d,"UnityEngine.CoreModule"); if(!coreA){ log_app("v29: no core\n"); return NULL; }
    void* coreImg=gi(coreA);
    char b[260];

    g_rocketCls=cf(img,"SFS.World","FULMMHJZAEKABZAY");
    if(!g_rocketCls){ log_app("v29: no Rocket\n"); return NULL; }
    void* objCls=cf(coreImg,"UnityEngine","Object");
    objCls_ = objCls;
    g_findMethod=objCls?gm(objCls,"FindObjectsOfType",1):NULL;
    if(!g_findMethod){ log_app("v29: no FindObjectsOfType\n"); return NULL; }
    g_typeObj=tgo(cgt(g_rocketCls));
    void* fName=cgfn(g_rocketCls,"rocketName");
    void* fLoc=cgfn(g_rocketCls,"location");
    if(fName) g_offName=fgo(fName);
    if(fLoc)  g_offLoc=fgo(fLoc);
    g_mGetRot=gm(g_rocketCls,"GetRotation",0);

    void* locCls = fLoc ? tgoc(fgt(fLoc)) : NULL;
    if(!locCls) locCls=cf(img,"SFS.World","FNVQPOCIQHTEDVSZ");
    if(locCls) g_mGetValue = gm(locCls,"get_Value",0);

    void* compCls=cf(coreImg,"UnityEngine","Component");
    void* trCls=cf(coreImg,"UnityEngine","Transform");
    g_mGetTransform = compCls?gm(compCls,"get_transform",0):NULL;
    g_mGetName = objCls?gm(objCls,"get_name",0):NULL;
    g_mSetName = objCls?gm(objCls,"set_name",1):NULL;
    if(trCls){ g_mGetChildCount=gm(trCls,"get_childCount",0); g_mGetChild=gm(trCls,"GetChild",1);
               g_mGetParent=gm(trCls,"get_parent",0); g_mSetSibling=gm(trCls,"SetSiblingIndex",1);
               g_setParent=gm(trCls,"SetParent",2); }
    g_canvasCls=cf(coreImg,"UnityEngine","Canvas");
    if(g_canvasCls) g_typeCanvas=tgo(cgt(g_canvasCls));
    g_rectCls=cf(coreImg,"UnityEngine","RectTransform");
    if(g_rectCls) g_typeRect=tgo(cgt(g_rectCls));
    {
        void* goCls=cf(coreImg,"UnityEngine","GameObject");
        g_mGetGO = compCls?gm(compCls,"get_gameObject",0):NULL;
        g_cms=(fn_cms)dlsym(h,"il2cpp_class_get_methods");
        g_mn=(fn_mn)dlsym(h,"il2cpp_method_get_name");
        g_mpc=(fn_mpc)dlsym(h,"il2cpp_method_get_param_count");
        g_mgp=(fn_mgp)dlsym(h,"il2cpp_method_get_param");
        g_tgt=(fn_tgt)dlsym(h,"il2cpp_type_get_type");
        g_mig=(fn_mig)dlsym(h,"il2cpp_method_is_generic");
        g_tgoc=(fn_tgoc)dlsym(h,"il2cpp_type_get_class_or_element_class");
        g_tn=(fn_tn)dlsym(h,"il2cpp_type_get_name");
        g_sNew=(fn_snew)dlsym(h,"il2cpp_string_new");
        select_instantiate(objCls, trCls);
        g_mGetComponent = goCls?gm(goCls,"GetComponent",1):NULL;
        void* tmpA = ao(d,"Unity.TextMeshPro");
        void* tmpImg = tmpA ? gi(tmpA) : NULL;
        if(tmpImg) g_tmpCls = cf(tmpImg,"TMPro","TMP_Text");
        if(!g_tmpCls && tmpImg) g_tmpCls = cf(tmpImg,"TMPro","TextMeshProUGUI");
        if(g_tmpCls){ g_typeTmp = tgo(cgt(g_tmpCls)); g_mSetText = gm(g_tmpCls,"set_text",1); g_mGetText = gm(g_tmpCls,"get_text",0); }
    }
    snprintf(b,sizeof(b),"v29: ui kind=%d inst=%p setname=%p go=%p getcomp=%p sib=%p tmp=%p/%p settext=%p snew=%p\n",
             g_pick_kind,g_mInstantiate,g_mSetName,g_mGetGO,g_mGetComponent,g_mSetSibling,g_tmpCls,g_typeTmp,g_mSetText,g_sNew);
    log_app(b);

    void* wtime=NULL; const char* ns[]={"SFS.World","SFS",""};
    for(int k=0;k<3&&!wtime;k++) wtime=cf(img,ns[k],"VCQSQMYGGZECNBPE");
    if(!wtime){ log_app("v29: no WorldTime\n"); return NULL; }
    void* upd=gm(wtime,"Update",0);
    if(!upd){ log_app("v29: no Update\n"); return NULL; }
    void** mi=(void**)upd; uintptr_t page=4096;
    mp((void*)((uintptr_t)mi & ~(page-1)), page*2, PROT_READ|PROT_WRITE|PROT_EXEC);
    g_orig_update=mi[0]; mi[0]=(void*)hook_update;
    log_app("v29: armed (tap the MP button after entering a world)\n");
    g_ready=1;
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res){
    (void)res;
    g_vm = vm;
    log_app("v29: JNI_OnLoad\n");
    pthread_t t;
    if(pthread_create(&t,NULL,worker,NULL)==0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
