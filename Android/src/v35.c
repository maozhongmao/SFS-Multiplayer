/* SPDX-License-Identifier: GPL-3.0-or-later */
// v35: v34 + copy the source button's sizeDelta/localScale onto the clone so the width matches the other menu buttons.
//   * the direct UnityEngine.Object.Instantiate call is DEAD (v28/v29: it dies inside libunity
//     because an internal lookup returns NULL) - so let the game's own managed code do it.
//   * panel class = obfuscated NOJZIZCFSEXSUEEX (dump.cs:21261) = the SFS.UI.ModGUI window class;
//     methods keep their names: DrawFullButton(int,string,Action,bool) RVA 0x18DDCF8.
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

/* v30 globals */
static void* g_panelCls=NULL, *g_typePanel=NULL, *g_mDrawFull=NULL, *g_actionCls=NULL;
static void* (*g_mNewObj)(void*)=NULL;
static int g_offList=-1, g_offMenuHolder=-1;
static int g_doffMethodPtr=-1, g_doffInvokeImpl=-1, g_doffMethodCode=-1;
static int g_doffTarget=-1, g_doffMethod=-1, g_doffTramp=-1;
static void* g_panelInst=NULL;
static void panel_button(void);
static void* make_action(void);
static void page_show(void);
static void page_hide(void);

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
static jobject g_decor = NULL;
static jobject g_page = NULL;
static jobject g_page_back = NULL;
static void* g_mFindAll = NULL;
/* ---- v32 main-menu clone ---- */
static void* g_btnCls=NULL, *g_typeBtn=NULL, *g_txtCls=NULL, *g_typeTxt=NULL;
static void* g_clickEvtCls=NULL, *g_holdEvtCls=NULL;
static void* g_mClickCtor=NULL, *g_mHoldCtor=NULL;
static void* g_trCls=NULL, *g_trType=NULL;
static void* g_clickField=NULL, *g_holdField=NULL;
static void* g_mSetTxt=NULL, *g_mGetTxt=NULL, *g_mGoTransform=NULL;
static void* g_mSetEnabled=NULL, *g_mGetSiblingIndex=NULL, *g_mForceRebuild=NULL, *g_rcs=NULL;
static void* g_mdDown=NULL, *g_mdUp=NULL, *g_mpos=NULL;
static void* g_cloneGo=NULL, *g_cloneBtn=NULL, *g_cloneRect=NULL;
static void* g_mmCls=NULL, *g_mmInst=NULL, *g_mOpenCredits=NULL, *g_mmMainField=NULL;
static void (*g_staticGet)(void*, void*) = NULL;
static void* g_mGetSizeDelta=NULL, *g_mSetSizeDelta=NULL, *g_mGetLocalScale=NULL, *g_mSetLocalScale=NULL;
static int g_offClick=-1, g_offHold=-1;
static volatile int g_pressed=0;
static void mainmenu_button(void);
static void poll_click(void);
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


/* ---------- v32: main-menu button route ---------- */
static void* find_text_component(void* go)
{
    char b[160]; void* e = NULL; void* tr; int cc = 0, i;
    if (!go || !g_mGetTransform || !g_mGetChildCount || !g_mGetChild || !g_mGetGO || !g_typeTxt || !g_mGetComponent)
        return NULL;
    tr = g_mGoTransform ? g_ri(g_mGoTransform, go, NULL, &e) : NULL;
    if (!tr || e) return NULL;
    { void* r = g_ri(g_mGetChildCount, tr, NULL, &e); if (!r || e) return NULL; cc = *(int32_t*)g_unbox(r); }
    for (i = 0; i < cc; ++i) {
        void* gp[1]; int32_t ci = i; void* e2 = NULL;
        void* child; void* cgo;
        gp[0] = &ci;
        child = g_ri(g_mGetChild, tr, gp, &e2);
        if (!child || e2) continue;
        cgo = g_ri(g_mGetGO, child, NULL, &e2);
        if (!cgo) continue;
        { void* tp[1]; void* e3 = NULL; void* t;
          tp[0] = g_typeTxt;
          t = g_ri(g_mGetComponent, cgo, tp, &e3);
          if (t && !e3) return t; }
    }
    (void)b;
    return NULL;
}

static void mainmenu_button(void)
{
    char b[320]; int mode = read_mode(); void* exc = NULL;
    void* params[1]; void* arr; int n, i;
    void* src = NULL; void* srcGo; void* srcTr; void* parent; int32_t idx = -1;
    void* clone; void* ctr; void* cbtn; void* ct;

    snprintf(b,sizeof(b),"v32: ===== mainmenu route (mode=%d) =====\n",mode); log_app(b);
    if (!g_typeBtn || !g_mFindAll) { log_app("v32: button plumbing missing\n"); return; }
    params[0] = g_typeBtn;
    arr = g_findMethod ? g_ri(g_findMethod, NULL, params, &exc) : NULL;
    n = (arr && !exc) ? (int)g_alg(arr) : -1;
    snprintf(b,sizeof(b),"v34: buttons(active)=%d exc=%p\n",n,exc); log_app(b);
    if (n <= 0) {
        exc = NULL;
        arr = g_ri(g_mFindAll, NULL, params, &exc);
        n = (arr && !exc) ? (int)g_alg(arr) : -1;
        snprintf(b,sizeof(b),"v34: buttons(all)=%d exc=%p\n",n,exc); log_app(b);
    }
    if (n <= 0) { log_app("v32: no button instance in this scene\n"); return; }

    for (i = 0; i < n; ++i) {
        void* btn = ((void**)((char*)arr + 0x20))[i];
        void* go; char nm[48];
        if (!btn) continue;
        go = g_ri(g_mGetGO, btn, NULL, &exc);
        if (!go) continue;
        obj_name(go, nm, 48);
        if (strcmp(nm, "Play Button") == 0) {
            void* btr = g_ri(g_mGetTransform, btn, NULL, &exc);
            void* bpar = (btr && !exc) ? g_ri(g_mGetParent, btr, NULL, &exc) : NULL;
            snprintf(b,sizeof(b),"v32: btn[%d] '%s' parent=%p\n",i,nm,bpar); log_app(b);
            if (!src && bpar) src = btn;
        }
    }
    if (!src) { log_app("v33: 'Play Button' not found\n"); return; }
    srcGo = g_ri(g_mGetGO, src, NULL, &exc);
    srcTr = g_ri(g_mGetTransform, src, NULL, &exc);
    parent = (srcTr) ? g_ri(g_mGetParent, srcTr, NULL, &exc) : NULL;
    if (srcTr && g_mGetSiblingIndex) { void* r = g_ri(g_mGetSiblingIndex, srcTr, NULL, &exc); if (r && !exc) idx = *(int32_t*)g_unbox(r); }
    snprintf(b,sizeof(b),"v32: src=%p go=%p tr=%p parent=%p idx=%d\n",src,srcGo,srcTr,parent,idx); log_app(b);
    if (mode == 1) { log_app("v32: mode1 probe only\n"); return; }
    if (!g_mInst3 || !srcGo) { log_app("v32: no Instantiate(Object,Transform,bool)\n"); return; }

    log_app("v32: *** calling Instantiate on the game button now ***\n");
    { void* ip[3]; int32_t w = 0; ip[0]=srcGo; ip[1]=parent; ip[2]=&w;
      clone = g_ri(g_mInst3, NULL, ip, &exc); }
    snprintf(b,sizeof(b),"v32: clone=%p exc=%p\n",clone,exc); log_app(b);
    if (!clone) { log_app("v32: clone failed\n"); return; }
    if (mode == 2) { log_app("v32: mode2 clone-only OK\n"); return; }

    log_app("v32: decorating clone\n");
    ctr = g_mGoTransform ? g_ri(g_mGoTransform, clone, NULL, &exc) : NULL;
    if (ctr && g_mSetSibling && idx >= 0) { void* p1[1]; int32_t ni = idx + 1; p1[0]=&ni; g_ri(g_mSetSibling, ctr, p1, &exc); }
    { void* tp[1]; tp[0]=g_typeBtn; cbtn = (g_mGetComponent && g_typeBtn) ? g_ri(g_mGetComponent, clone, tp, &exc) : NULL; }
    if (cbtn && g_mNewObj) {
        if (g_clickEvtCls && g_offClick >= 0) {
            void* ce = g_mNewObj(g_clickEvtCls);
            if (ce) { if (g_mClickCtor) g_ri(g_mClickCtor, ce, NULL, &exc);
                      *(void**)((char*)cbtn + g_offClick) = ce; log_app("v32: clickEvent cleared\n"); }
        }
        if (g_holdEvtCls && g_offHold >= 0) {
            void* he = g_mNewObj(g_holdEvtCls);
            if (he) { if (g_mHoldCtor) g_ri(g_mHoldCtor, he, NULL, &exc);
                      *(void**)((char*)cbtn + g_offHold) = he; log_app("v32: holdEvent cleared\n"); }
        }
    }
    if (g_trCls && g_trType && g_mGetComponent && g_mSetEnabled) {
        void* tp[1]; void* e2 = NULL; void* tc;
        tp[0] = g_trType;
        tc = g_ri(g_mGetComponent, clone, tp, &e2);
        if (tc && !e2) { void* p[1]; int32_t z = 0; p[0]=&z; g_ri(g_mSetEnabled, tc, p, &e2); log_app("v32: translation disabled\n"); }
    }
    ct = find_text_component(clone);
    if (ct && g_mSetTxt) {
        void* str = g_sNew ? g_sNew("\xE5\xA4\x9A\xE4\xBA\xBA\xE8\x81\x94\xE6\x9C\xBA") : NULL;
        if (str) { void* p[1]; p[0]=str; g_ri(g_mSetTxt, ct, p, &exc); log_app("v32: label set\n"); }
    }
    if (ctr && srcTr && g_mGetSizeDelta && g_mSetSizeDelta) {
        void* r = g_ri(g_mGetSizeDelta, srcTr, NULL, &exc);
        if (r && !exc) {
            float* v = (float*)g_unbox(r); float sz[2] = { v[0], v[1] };
            void* p[1]; p[0] = sz;
            g_ri(g_mSetSizeDelta, ctr, p, &exc);
            snprintf(b,sizeof(b),"v35: src sizeDelta=%.1f,%.1f -> clone\n", v[0], v[1]); log_app(b);
        }
    }
    if (ctr && srcTr && g_mGetLocalScale && g_mSetLocalScale) {
        void* r = g_ri(g_mGetLocalScale, srcTr, NULL, &exc);
        if (r && !exc) {
            float* v = (float*)g_unbox(r); float sc[3] = { v[0], v[1], v[2] };
            void* p[1]; p[0] = sc;
            g_ri(g_mSetLocalScale, ctr, p, &exc);
        }
    }
    if (g_mForceRebuild && parent) { void* p[1]; p[0]=parent; g_ri(g_mForceRebuild, NULL, p, &exc); log_app("v35: layout rebuilt\n"); }
    if (g_mmCls && g_mmMainField && g_staticGet) { void* v=NULL; g_staticGet(g_mmMainField, &v); g_mmInst=v; }
    { char mb[120]; snprintf(mb,sizeof(mb),"v33: mainmenu inst=%p openCredits=%p\n",g_mmInst,g_mOpenCredits); log_app(mb); }
    g_cloneGo = clone; g_cloneBtn = cbtn; g_cloneRect = ctr;
    log_app("v32: *** MAIN MENU BUTTON READY ***\n");
}

static void poll_click(void)
{
    void* e = NULL; void* r; int down = 0, up = 0, inside = 0; float mx = 0, my = 0;
    if (!g_cloneGo || !g_cloneRect || !g_rcs) return;
    if (g_mdDown) { int32_t z = 0; void* p[1]; p[0]=&z; r = g_ri(g_mdDown, NULL, p, &e); if (r && !e) down = *(unsigned char*)g_unbox(r) ? 1 : 0; }
    if (g_mdUp)   { int32_t z = 0; void* p[1]; p[0]=&z; r = g_ri(g_mdUp,   NULL, p, &e); if (r && !e) up   = *(unsigned char*)g_unbox(r) ? 1 : 0; }
    if (g_mpos)   { r = g_ri(g_mpos, NULL, NULL, &e); if (r && !e) { float* v = (float*)g_unbox(r); mx = v[0]; my = v[1]; } }
    { float sp[2]; void* p[3];
      sp[0] = mx; sp[1] = my;
      p[0] = g_cloneRect; p[1] = sp; p[2] = NULL;
      r = g_ri(g_rcs, NULL, p, &e);
      if (r && !e) inside = *(unsigned char*)g_unbox(r) ? 1 : 0; }
    if (down && inside) g_pressed = 1;
    if (up) {
        if (g_pressed && inside) {
            if (g_mmInst && g_mOpenCredits) {
                void* e2 = NULL;
                g_ri(g_mOpenCredits, g_mmInst, NULL, &e2);
                log_app(e2 ? "v33: OpenCredits EXCEPTION\n" : "v33: *** OpenCredits called ***\n");
            } else {
                log_app("v33: no main-menu instance -> fallback page\n");
                page_show();
            }
        }
        g_pressed = 0;
    }
}

static void hook_update(void* self){
    (void)__sync_add_and_fetch(&g_frame, 1);
    if (g_ready && g_do_clone){ g_do_clone = 0; mainmenu_button(); }
    if (g_cloneGo) poll_click();
    if (g_orig_update) ((void(*)(void*))g_orig_update)(self);
}

/* ================= v30: hand the callback to the game's own button factory =================
 * Evidence (dump.cs:21261-21362): class NOJZIZCFSEXSUEEX : MonoBehaviour, fields menuHolder 0x20,
 * fullButtonPrefab 0x90, buttonPrefab 0x88, List<...> 0x98; methods DrawFullButton(int,string,Action,bool)
 * RVA 0x18DDCF8, DrawButton 0x18DD1D0, DrawTitle 0x18DD5D4, DrawText 0x18DDC08, Open 0x18DD724,
 * Close 0x18DC824.  PC mods reach the same class through SFS.UI.ModGUI (absent on mobile: 0 hits).
 * The Action argument must be faked: il2cpp_object_new(System.Action) + method_ptr/invoke_impl
 * set to a native trampoline (same trick as the SFS-Pro seller report, section 2.6 "native trampoline").
 */
static volatile int g_tramp_hits = 0;
__attribute__((noinline, used)) static void action_trampoline(void* a, void* b, void* c, void* d){
    (void)a;(void)b;(void)c;(void)d;
    int n = __sync_add_and_fetch(&g_tramp_hits, 1);
    char t[96]; snprintf(t,sizeof(t),"m5: *** ACTION TRAMPOLINE HIT n=%d ***\n", n); log_app(t);
    page_show();
}

static void* make_action(void){
    if (!g_mNewObj || !g_actionCls){ log_app("m5: cannot fake an Action (no object_new/Action class)\n"); return NULL; }
    void* del = g_mNewObj(g_actionCls);
    if (!del){ log_app("m5: object_new(System.Action) FAILED\n"); return NULL; }
    int oMp=g_doffMethodPtr, oIi=g_doffInvokeImpl, oMc=g_doffMethodCode;
    if (oMp<0) oMp=0x10;   /* documented Il2CppDelegate layout: method_ptr @0x10, invoke_impl @0x18 */
    if (oIi<0) oIi=0x18;
    *(void**)((char*)del+oMp) = (void*)action_trampoline;
    *(void**)((char*)del+oIi) = (void*)action_trampoline;
    if (oMc>=0) *(void**)((char*)del+oMc) = (void*)action_trampoline;
    char t[200];
    snprintf(t,sizeof(t),"m5: action=%p tramp=%p offs mp=%d ii=%d mc=%d (tgt=%d mth=%d tr=%d)\n",
             del,(void*)action_trampoline,oMp,oIi,oMc,g_doffTarget,g_doffMethod,g_doffTramp);
    log_app(t);
    return del;
}

static int list_size(void* o){
    if (g_offList<0 || !o) return -1;
    void* lst = *(void**)((char*)o+g_offList);
    if (!lst) return -1;
    return *(int*)((char*)lst+0x18);   /* List<T>._size */
}

static void panel_button(void){
    char b[300];
    int mode = read_mode();
    snprintf(b,sizeof(b),"m5: ===== panel route (mode=%d) =====\n", mode); log_app(b);
    if (!g_panelCls || !g_mDrawFull || !g_typePanel || !g_findMethod){ log_app("m5: panel plumbing missing\n"); return; }
    void* params[1]; params[0]=g_typePanel; void* exc=NULL;
    void* arr = g_ri(g_findMethod, NULL, params, &exc);
    if (exc || !arr){ log_app("m5: panel FindObjectsOfType failed\n"); return; }
    int n=(int)g_alg(arr);
    snprintf(b,sizeof(b),"m5: panel instances=%d\n",n); log_app(b);
    if (n==0 && g_mFindAll){
        void* eall=NULL; void* arr2=g_ri(g_mFindAll,NULL,params,&eall);
        int n2=(arr2&&!eall)?(int)g_alg(arr2):-1;
        snprintf(b,sizeof(b),"m5: panel FindObjectsOfTypeAll=%d\n",n2); log_app(b);
        if(n2>0){ arr=arr2; n=n2; }
    }
    void* best=NULL; int bestN=-2;
    for(int i=0;i<n;i++){
        void* o=((void**)((char*)arr+0x20))[i];
        if(!o) continue;
        char nm[40]; char_name(o,nm,40);
        int ls=list_size(o);
        void* mh=(g_offMenuHolder>=0)?*(void**)((char*)o+g_offMenuHolder):NULL;
        snprintf(b,sizeof(b),"m5: panel[%d] o=%p name='%s' list=%d menuHolder=%p\n",i,o,nm,ls,mh);
        log_app(b);
        if (ls>bestN){ bestN=ls; best=o; }
    }
    if(!best){ log_app("m5: no panel instance in the scene (open a game menu first)\n"); return; }
    g_panelInst=best;
    if (mode==1){ log_app("m5: mode1 = resolve/dump only, nothing is called\n"); return; }
    int slot = (bestN>0)? bestN : 0;
    void* label = g_sNew ? g_sNew("\xE5\xA4\x9A\xE4\xBA\xBA\xE8\x81\x94\xE6\x9C\xBA") : NULL;
    void* act = NULL;
    if (mode==3){ act = make_action(); if(!act) return; }
    else log_app("m5: mode2 = Action=NULL\n");
    int32_t a0=slot; void* a1=label; void* a2=act; int32_t a3=1;
    void* ip[4]; ip[0]=&a0; ip[1]=&a1; ip[2]=&a2; ip[3]=&a3;
    snprintf(b,sizeof(b),"m5: calling DrawFullButton(slot=%d label=%p act=%p enabled=1) on %p\n",slot,label,act,best);
    log_app(b);
    void* e2=NULL;
    g_ri(g_mDrawFull, best, ip, &e2);
    snprintf(b,sizeof(b),"m5: -> returned exc=%p list now=%d\n", e2, list_size(best));
    log_app(b);
    log_app("m5: *** PANEL ROUTE DONE ***\n");
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


/* ---------- v31: full-screen empty page (native Android overlay) ---------- */
static void page_hide(void){
    JNIEnv* env = NULL;
    if (!g_vm || !g_decor || !g_page) return;
    if ((*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) return;
    jclass cVG = (*env)->FindClass(env, "android/view/ViewGroup");
    if (cVG){
        jmethodID mRemove = (*env)->GetMethodID(env, cVG, "removeView", "(Landroid/view/View;)V");
        if (mRemove) (*env)->CallVoidMethod(env, g_decor, mRemove, g_page);
    }
    drain(env, "v31: page remove");
    (*env)->DeleteGlobalRef(env, g_page); g_page = NULL;
    if (g_page_back){ (*env)->DeleteGlobalRef(env, g_page_back); g_page_back = NULL; }
    log_app("v31: page closed\n");
}

static void page_show(void){
    JNIEnv* env = NULL;
    if (!g_vm || !g_activity || !g_decor) return;
    if ((*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) return;
    if (g_page) return;
    jclass cFrame = (*env)->FindClass(env, "android/widget/FrameLayout");
    if (!cFrame || drain(env, "v31: page FrameLayout")) return;
    jmethodID ctorF = (*env)->GetMethodID(env, cFrame, "<init>", "(Landroid/content/Context;)V");
    jobject frame = (*env)->NewObject(env, cFrame, ctorF, g_activity);
    if (!frame || drain(env, "v31: page new")) return;
    jmethodID setBg = (*env)->GetMethodID(env, cFrame, "setBackgroundColor", "(I)V");
    (*env)->CallVoidMethod(env, frame, setBg, (jint)0xFF101820);

    jclass cTV = (*env)->FindClass(env, "android/widget/TextView");
    jmethodID ctorTV = (*env)->GetMethodID(env, cTV, "<init>", "(Landroid/content/Context;)V");
    jmethodID setText = (*env)->GetMethodID(env, cTV, "setText", "(Ljava/lang/CharSequence;)V");
    jmethodID setSize = (*env)->GetMethodID(env, cTV, "setTextSize", "(F)V");
    jmethodID setColor = (*env)->GetMethodID(env, cTV, "setTextColor", "(I)V");
    jmethodID setGrav = (*env)->GetMethodID(env, cTV, "setGravity", "(I)V");

    jobject title = (*env)->NewObject(env, cTV, ctorTV, g_activity);
    jstring s1 = (*env)->NewStringUTF(env, "\xE5\xA4\x9A\xE4\xBA\xBA\xE8\x81\x94\xE6\x9C\xBA");
    (*env)->CallVoidMethod(env, title, setText, s1);
    (*env)->CallVoidMethod(env, title, setSize, (jfloat)28);
    (*env)->CallVoidMethod(env, title, setColor, (jint)0xFFFFFFFF);
    (*env)->CallVoidMethod(env, title, setGrav, 17);

    jobject sub = (*env)->NewObject(env, cTV, ctorTV, g_activity);
    jstring s2 = (*env)->NewStringUTF(env, "\xE7\xA9\xBA\xE9\xA1\xB5\xE9\x9D\xA2");
    (*env)->CallVoidMethod(env, sub, setText, s2);
    (*env)->CallVoidMethod(env, sub, setSize, (jfloat)16);
    (*env)->CallVoidMethod(env, sub, setColor, (jint)0xFF9AA4B2);
    (*env)->CallVoidMethod(env, sub, setGrav, 17);

    jobject back = make_button(env, "\xE8\xBF\x94\xE5\x9B\x9E", 16, 24);
    if (!back){ drain(env, "v31: page back"); return; }
    jfieldID fInst = (*env)->GetStaticFieldID(env, g_bridge, "INSTANCE", "Lcom/sfsmp/bridge/Bridge;");
    jobject listener = fInst ? (*env)->GetStaticObjectField(env, g_bridge, fInst) : NULL;
    jclass cView = (*env)->FindClass(env, "android/view/View");
    jmethodID mSetClick = (*env)->GetMethodID(env, cView, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V");
    if (listener && mSetClick) (*env)->CallVoidMethod(env, back, mSetClick, listener);

    jclass cFLP = (*env)->FindClass(env, "android/widget/FrameLayout$LayoutParams");
    jmethodID ctorP = (*env)->GetMethodID(env, cFLP, "<init>", "(II)V");
    jfieldID fGrav = (*env)->GetFieldID(env, cFLP, "gravity", "I");
    jfieldID fTM = (*env)->GetFieldID(env, cFLP, "topMargin", "I");
    jmethodID mAdd = (*env)->GetMethodID(env, cFrame, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");

    jobject lp1 = (*env)->NewObject(env, cFLP, ctorP, -2, -2);
    if (lp1 && fGrav) (*env)->SetIntField(env, lp1, fGrav, 17);
    if (lp1 && fTM) (*env)->SetIntField(env, lp1, fTM, -120);
    (*env)->CallVoidMethod(env, frame, mAdd, title, lp1);

    jobject lp2 = (*env)->NewObject(env, cFLP, ctorP, -2, -2);
    if (lp2 && fGrav) (*env)->SetIntField(env, lp2, fGrav, 17);
    if (lp2 && fTM) (*env)->SetIntField(env, lp2, fTM, 20);
    (*env)->CallVoidMethod(env, frame, mAdd, sub, lp2);

    jobject lp3 = (*env)->NewObject(env, cFLP, ctorP, -2, -2);
    if (lp3 && fGrav) (*env)->SetIntField(env, lp3, fGrav, 17);
    if (lp3 && fTM) (*env)->SetIntField(env, lp3, fTM, 180);
    (*env)->CallVoidMethod(env, frame, mAdd, back, lp3);

    jobject lpF = (*env)->NewObject(env, cFLP, ctorP, -1, -1);
    (*env)->CallVoidMethod(env, g_decor, mAdd, frame, lpF);
    if (drain(env, "v31: page attach")) return;
    g_page = (*env)->NewGlobalRef(env, frame);
    g_page_back = (*env)->NewGlobalRef(env, back);
    (*env)->DeleteLocalRef(env, title); (*env)->DeleteLocalRef(env, sub);
    (*env)->DeleteLocalRef(env, back); (*env)->DeleteLocalRef(env, frame);
    log_app("v31: *** EMPTY PAGE SHOWN ***\n");
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
    if (g_decor) (*env)->DeleteGlobalRef(env, g_decor);
    g_decor = (*env)->NewGlobalRef(env, decor);

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
    if (g_ready && g_do_clone){ g_do_clone = 0; mainmenu_button(); }
    schedule(env, 1000);
}

JNIEXPORT void JNICALL Java_com_sfsmp_bridge_Bridge_nativeOnClick(JNIEnv* env, jclass cls, jobject v){
    (void)cls;
    if (g_page_back && (*env)->IsSameObject(env, v, g_page_back)) { page_hide(); set_label(env, "MP: page closed"); return; }
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
        /* ---- v30: resolve the game's own panel factory (the SFS.UI.ModGUI window class) ---- */
        {
            void* pcls = cf(img, "", "NOJZIZCFSEXSUEEX");
            if(!pcls) pcls = cf(img, "SFS.UI", "NOJZIZCFSEXSUEEX");
            if(!pcls) pcls = cf(img, "SFS.UI.ModGUI", "NOJZIZCFSEXSUEEX");
            size_t (*icnt)(void*) = (size_t(*)(void*))dlsym(h,"il2cpp_image_get_class_count");
            void* (*iget)(void*,int) = (void*(*)(void*,int))dlsym(h,"il2cpp_image_get_class");
            if(!pcls && icnt && iget){
                size_t c = icnt(img);
                for(size_t i=0;i<c && !pcls;i++){
                    void* k = iget(img,(int)i);
                    if(k && gm(k,"DrawFullButton",4)) pcls=k;
                }
                if(pcls) log_app("v30: panel class found by REVERSE LOOKUP (DrawFullButton/4)\n");
            }
            g_panelCls = pcls;
            if(pcls){
                g_typePanel = tgo(cgt(pcls));
                g_mDrawFull = gm(pcls,"DrawFullButton",4);
                void* f = cgfn(pcls,"CHNLEMLKDNN");
                if(f) g_offList = fgo(f);
                f = cgfn(pcls,"menuHolder");
                if(f) g_offMenuHolder = fgo(f);
                if(g_mDrawFull){
                    void* pt = g_mgp(g_mDrawFull,2);
                    g_actionCls = pt?tgoc(pt):NULL;
                    int t0 = g_tgt?g_tgt(g_mgp(g_mDrawFull,0)):-1;
                    int t1 = g_tgt?g_tgt(g_mgp(g_mDrawFull,1)):-1;
                    int t3 = g_tgt?g_tgt(g_mgp(g_mDrawFull,3)):-1;
                    snprintf(b,sizeof(b),"v30: DrawFullButton pc=%d types=%d/%d/%d (want 8/14/2) rva=0x%llx\n",
                             g_mpc(g_mDrawFull),t0,t1,t3,
                             (unsigned long long)((uintptr_t)(*(void**)g_mDrawFull)-(uintptr_t)g_base));
                    log_app(b);
                }
                if(g_actionCls){
                    void* (*par)(void*) = (void*(*)(void*))dlsym(h,"il2cpp_class_get_parent");
                    const char* fn[7]={"method_ptr","invoke_impl","m_target","method","delegate_trampoline","extra_arg","method_code"};
                    int got[7]={-1,-1,-1,-1,-1,-1,-1};
                    void* cls=g_actionCls;
                    for(int depth=0; cls && depth<6; depth++){
                        for(int i=0;i<7;i++) if(got[i]<0){ void* ff=cgfn(cls,fn[i]); if(ff) got[i]=fgo(ff); }
                        cls = par?par(cls):NULL;
                    }
                    g_doffMethodPtr=got[0]; g_doffInvokeImpl=got[1]; g_doffTarget=got[2];
                    g_doffMethod=got[3]; g_doffTramp=got[4]; g_doffMethodCode=got[6];
                    snprintf(b,sizeof(b),"v30: Action offs mp=0x%x ii=0x%x tgt=0x%x mth=0x%x tr=0x%x mc=0x%x\n",
                             (unsigned)got[0],(unsigned)got[1],(unsigned)got[2],(unsigned)got[3],(unsigned)got[4],(unsigned)got[6]);
                    log_app(b);
                }
                { void* rc = cf(coreImg,"UnityEngine","Resources"); if(rc) g_mFindAll=gm(rc,"FindObjectsOfTypeAll",1); }
                g_mNewObj = (void*(*)(void*))dlsym(h,"il2cpp_object_new");
                snprintf(b,sizeof(b),"v30: panel=%p type=%p DrawFull=%p actionCls=%p offList=0x%x offMenu=0x%x new=%p\n",
                         pcls,g_typePanel,g_mDrawFull,g_actionCls,(unsigned)g_offList,(unsigned)g_offMenuHolder,g_mNewObj);
                log_app(b);
            } else log_app("v30: panel class NOT found\n");
        }
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

    /* ---------- v32 resolve: main-menu button + input + layout ---------- */
    {
        void* uiA = ao(d,"UnityEngine.UI");                void* uiI = uiA?gi(uiA):NULL;
        void* uimA= ao(d,"UnityEngine.UIModule");          void* uimI= uimA?gi(uimA):NULL;
        void* inpA= ao(d,"UnityEngine.InputLegacyModule"); void* inpI= inpA?gi(inpA):NULL;
        void* goCls2 = cf(coreImg,"UnityEngine","GameObject");
        void* behCls = cf(coreImg,"UnityEngine","Behaviour");
        if(goCls2){ g_mGoTransform=gm(goCls2,"get_transform",0); }
        if(g_rectCls){ g_mGetSizeDelta=gm(g_rectCls,"get_sizeDelta",0); g_mSetSizeDelta=gm(g_rectCls,"set_sizeDelta",1); }
        if(trCls){ g_mGetLocalScale=gm(trCls,"get_localScale",0); g_mSetLocalScale=gm(trCls,"set_localScale",1); }
        if(behCls) g_mSetEnabled=gm(behCls,"set_enabled",1);
        if(trCls)  g_mGetSiblingIndex=gm(trCls,"GetSiblingIndex",0);
        g_btnCls=cf(img,"SFS.UI","HKAZNXTEGNSSTQLF");
        if(g_btnCls){ g_typeBtn=tgo(cgt(g_btnCls));
            { void* f=cgfn(g_btnCls,"clickEvent"); if(f) g_offClick=fgo(f); }
            { void* f=cgfn(g_btnCls,"holdEvent");  if(f) g_offHold =fgo(f); }
            g_clickField=cgfn(g_btnCls,"clickEvent"); g_holdField=cgfn(g_btnCls,"holdEvent"); }
        g_txtCls=cf(img,"SFS.UI","NDJCKINKYJCMZNTI");
        if(g_txtCls){ g_typeTxt=tgo(cgt(g_txtCls)); g_mSetTxt=gm(g_txtCls,"set_Text",1); g_mGetTxt=gm(g_txtCls,"get_Text",0); }
        g_clickEvtCls=cf(img,"SFS.UI","ClickUnityEvent");
        g_holdEvtCls =cf(img,"SFS.UI","HoldUnityEvent");
        if(g_clickEvtCls) g_mClickCtor=gm(g_clickEvtCls,".ctor",0);
        if(g_holdEvtCls)  g_mHoldCtor =gm(g_holdEvtCls ,".ctor",0);
        g_mmCls=cf(img,"SFS.UI","JNFOGGJGNMZRAZYH");
        if(!g_mmCls) g_mmCls=cf(img,"SFS","JNFOGGJGNMZRAZYH");
        if(!g_mmCls) g_mmCls=cf(img,"","JNFOGGJGNMZRAZYH");
        if(g_mmCls){ g_mOpenCredits=gm(g_mmCls,"OpenCredits",0); g_mmMainField=cgfn(g_mmCls,"main"); }
        g_staticGet=(void(*)(void*,void*))dlsym(h,"il2cpp_field_static_get_value");
        snprintf(b,sizeof(b),"v33: mainmenu cls=%p openCredits=%p mainField=%p\n",g_mmCls,g_mOpenCredits,g_mmMainField);
        log_app(b);
        g_trCls=cf(img,"SFS.Translations","WRNLOFEPQTZANWYN");
        if(g_trCls) g_trType=tgo(cgt(g_trCls));
        if(uiI){ void* lb=cf(uiI,"UnityEngine.UI","LayoutRebuilder");
                 g_mForceRebuild=lb?gm(lb,"ForceRebuildLayoutImmediate",1):NULL; }
        if(uimI){ void* rtu=cf(uimI,"UnityEngine","RectTransformUtility");
                  if(rtu) g_rcs=gm(rtu,"RectangleContainsScreenPoint",3); }
        if(inpI){ void* inp=cf(inpI,"UnityEngine","Input");
                  if(inp){ g_mdDown=gm(inp,"GetMouseButtonDown",1); g_mdUp=gm(inp,"GetMouseButtonUp",1); g_mpos=gm(inp,"get_mousePosition",0); } }
        snprintf(b,sizeof(b),"v32: btn=%p txt=%p clickE=%p holdE=%p tr=%p rcs=%p input=%p/%p/%p\n",
                 g_btnCls,g_txtCls,g_clickEvtCls,g_holdEvtCls,g_trCls,g_rcs,g_mdDown,g_mdUp,g_mpos);
        log_app(b);
    }
    {
        void* smCls=NULL; const char* ns2[]={"SFS.Input","SFS.UI","SFS",""};
        for(int k=0;k<3&&!smCls;k++) smCls=cf(img,ns2[k],"VNDAREHWRIQNAAKC");
        if(!smCls){ log_app("v32: no screen manager\n"); return NULL; }
        void* upd=gm(smCls,"Update",0);
        if(!upd){ log_app("v32: no screen Update\n"); return NULL; }
        void** mi=(void**)upd; uintptr_t page=4096;
        mp((void*)((uintptr_t)mi & ~(page-1)), page*2, PROT_READ|PROT_WRITE|PROT_EXEC);
        g_orig_update=mi[0]; mi[0]=(void*)hook_update;
        log_app("v32: armed (screen stack hook)\n");
        g_ready=1;
    }
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
