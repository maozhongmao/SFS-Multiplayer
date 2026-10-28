/* SPDX-License-Identifier: GPL-3.0-or-later */
// v22: M1 via Location.get_Value() (planet + double position/velocity),
//      M5 prep via scene explorer (Canvas -> transform tree names).
#include <jni.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdint.h>

#define MARKER "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt"
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

static void* g_orig_update = NULL;
static void* g_findMethod = NULL, *g_typeObj = NULL, *g_rocketCls = NULL;
static void* g_mGetRot = NULL, *g_mGetTransform = NULL, *g_mGetName = NULL;
static void* g_mGetChildCount = NULL, *g_mGetChild = NULL, *g_mGetValue = NULL;
static void* g_canvasCls = NULL, *g_typeCanvas = NULL;
static void* g_rectCls = NULL, *g_typeRect = NULL, *g_mGetParent = NULL;
static void* g_mInstantiate = NULL, *g_mSetName = NULL, *g_mGetGO = NULL;
static void* g_mGetComponent = NULL, *g_mSetSibling = NULL, *g_mSetText = NULL;
static void* g_tmpCls = NULL, *g_typeTmp = NULL;
typedef void* (*fn_snew)(const char*);
typedef void* (*fn_cms)(void*, void**);
typedef const char* (*fn_mn)(void*);
typedef int32_t (*fn_mpc)(void*);
typedef void* (*fn_mgp)(void*, uint32_t);
typedef int32_t (*fn_tgt)(void*);
typedef int32_t (*fn_mig)(void*);
static fn_snew g_sNew = NULL;
static fn_cms g_cms=NULL; static fn_mn g_mn=NULL; static fn_mpc g_mpc=NULL;
static fn_mgp g_mgp=NULL; static fn_tgt g_tgt=NULL; static fn_mig g_mig=NULL;
static void* g_setParent = NULL;
static fn_tgoc g_tgoc = NULL;
static volatile int g_pick_kind = 0;  /* 3 = (Object,Transform,bool), 1 = (Object) */
static volatile int g_clone_done = 0;
static int g_offName = -1, g_offLoc = -1;
static fn_ri g_ri; static fn_alg g_alg; static fn_unbox g_unbox; static fn_schars g_schars;
static volatile int g_frame = 0;
static volatile int g_ready = 0;
static volatile int g_scene_done = 0;

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

static void print_tree(void* tr, int depth, int* budget){
    if (!tr || !budget || *budget <= 0 || depth > 3) return;
    if (!g_mGetChildCount || !g_mGetChild) return;
    char nm[40]; obj_name(tr, nm, 40);
    void* e=NULL;
    void* c = g_ri(g_mGetChildCount, tr, NULL, &e);
    int cc = (c && !e) ? *(int32_t*)g_unbox(c) : 0;
    char b[160];
    snprintf(b,sizeof(b),"m5: %*s'%s' children=%d\n", depth*2, "", nm, cc);
    log_app(b);
    (*budget)--;
    int lim = cc > 10 ? 10 : cc;
    for (int i=0;i<lim && *budget>0;i++){
        int32_t idx = i;
        void* p[1]; p[0] = &idx;
        void* e2=NULL;
        void* child = g_ri(g_mGetChild, tr, p, &e2);
        if (child && !e2) print_tree(child, depth+1, budget);
    }
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
    int lim = n>45?45:n;
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
    if (exc) { log_app("m1: find threw\n"); return; }
    int n = arr ? (int)g_alg(arr) : 0;
    char b[300];
    snprintf(b,sizeof(b),"m1: rockets=%d\n", n);
    log_app(b);
    int lim = n>2?2:n;
    for (int i=0;i<lim;i++){
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
        snprintf(b,sizeof(b),"m1: [%d] ro=%p name='%s' planet='%s' t=%.2f pos=(%.3f, %.3f) vel=(%.3f, %.3f) rot=%.2f\n",
                 i, ro, nm, pname, t, px, py, vx, vy, rot);
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

static void* pick_instantiate(void* objCls, void* trCls){
    if (!g_cms || !g_mn || !g_mpc || !g_mgp || !g_tgt || !g_mig || !g_tgoc) return NULL;
    void* m3 = NULL; void* m1 = NULL;
    void* it = NULL; void* m;
    while ((m = g_cms(objCls, &it)) != NULL) {
        const char* nm = g_mn(m);
        if (!nm || strcmp(nm, "Instantiate") != 0) continue;
        if (g_mig(m)) continue;
        int pc = g_mpc(m);
        if (pc == 3) {
            void* k0 = g_tgoc(g_mgp(m,0));
            void* k1 = g_tgoc(g_mgp(m,1));
            int   t2 = g_tgt(g_mgp(m,2));
            if (k0 == objCls && k1 == trCls && t2 == 2) { m3 = m; break; }
        } else if (pc == 1) {
            void* k0 = g_tgoc(g_mgp(m,0));
            if (k0 == objCls) m1 = m;
        }
    }
    if (m3) { g_pick_kind = 3; return m3; }
    if (m1) { g_pick_kind = 1; return m1; }
    return NULL;
}

static void ui_clone(void){
    if (g_clone_done) return;
    g_clone_done = 1;
    char b[220];
    log_app("m5: --- clone attempt ---\n");
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
    void* e2=NULL;
    void* clone = NULL;
    if (g_pick_kind == 3) {
        void* a0 = srcGo; void* a1 = par; int32_t a2 = 0;
        void* ip[3]; ip[0]=&a0; ip[1]=&a1; ip[2]=&a2;
        clone = g_ri(g_mInstantiate, NULL, ip, &e2);
    } else if (g_pick_kind == 1) {
        void* a0 = srcGo;
        void* ip[1]; ip[0]=&a0;
        clone = g_ri(g_mInstantiate, NULL, ip, &e2);
        if (clone && g_setParent && par) {
            void* e11=NULL; void* cTr0 = g_ri(g_mGetTransform, clone, NULL, &e11);
            if (cTr0 && !e11) {
                int32_t wp = 0;
                void* sp[2]; sp[0]=&par; sp[1]=&wp;
                g_ri(g_setParent, cTr0, sp, &e11);
                snprintf(b,sizeof(b),"m5: SetParent exc=%p\n", e11); log_app(b);
            }
        }
    }
    snprintf(b,sizeof(b),"m5: Instantiate(kind=%d) -> %p exc=%p\n", g_pick_kind, clone, e2); log_app(b);
    if (!clone) return;
    if (g_mSetName && g_sNew){
        void* sname = g_sNew("MP Button");
        void* np[1]; np[0]=&sname;
        void* e3=NULL;
        g_ri(g_mSetName, clone, np, &e3);
        snprintf(b,sizeof(b),"m5: set_name exc=%p\n", e3); log_app(b);
    }
    if (g_mSetSibling && srcTr && par){
        void* e4=NULL;
        void* e5=NULL;
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
                void* txt = g_sNew("\xE5\xA4\x9A\xE4\xBA\xBA");
                void* tp2[1]; tp2[0]=&txt;
                void* e10=NULL;
                g_ri(g_mSetText, tmp, tp2, &e10);
                snprintf(b,sizeof(b),"m5: TMP set_text child=%d exc=%p\n", i, e10); log_app(b);
                hit = 1;
            }
        }
        if (!hit) log_app("m5: no TMP_Text in direct children\n");
    }
    log_app("m5: *** CLONE DONE ***\n");
}

static void hook_update(void* self){
    int f = __sync_add_and_fetch(&g_frame, 1);
    if (g_ready && f % 60 == 1) dump_rockets();
    if (g_ready && f == 240) dump_scene();
    if (g_ready && f == 320) ui_clone();
    if (g_orig_update) ((void(*)(void*))g_orig_update)(self);
}

static void* worker(void* arg){
    (void)arg;
    sleep(8);
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v22: no il2cpp\n"); return NULL; }
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
    if(!dg||!ta||!ao||!gi||!cf||!gm||!cgfn||!fgt||!tgoc||!tgo||!cgt||!fgo||!ri||!alg||!unbox||!schars||!mp){ log_app("v22: missing syms\n"); return NULL; }
    g_ri=ri; g_alg=alg; g_unbox=unbox; g_schars=schars;
    void* d=dg(); ta(d);
    void* a=ao(d,"Assembly-CSharp"); if(!a){ log_app("v22: no asm\n"); return NULL; }
    void* img=gi(a); if(!img){ log_app("v22: no img\n"); return NULL; }
    void* coreA=ao(d,"UnityEngine.CoreModule"); if(!coreA){ log_app("v22: no core\n"); return NULL; }
    void* coreImg=gi(coreA);
    char b[240];

    g_rocketCls=cf(img,"SFS.World","FULMMHJZAEKABZAY");
    if(!g_rocketCls){ log_app("v22: no Rocket\n"); return NULL; }
    void* objCls=cf(coreImg,"UnityEngine","Object");
    g_findMethod=objCls?gm(objCls,"FindObjectsOfType",1):NULL;
    if(!g_findMethod){ log_app("v22: no FindObjectsOfType\n"); return NULL; }
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
    if(trCls){ g_mGetChildCount=gm(trCls,"get_childCount",0); g_mGetChild=gm(trCls,"GetChild",1); }
    g_canvasCls=cf(coreImg,"UnityEngine","Canvas");
    if(g_canvasCls) g_typeCanvas=tgo(cgt(g_canvasCls));
    g_rectCls=cf(coreImg,"UnityEngine","RectTransform");
    if(g_rectCls) g_typeRect=tgo(cgt(g_rectCls));
    if(trCls){ g_mGetParent=gm(trCls,"get_parent",0); g_mSetSibling=gm(trCls,"SetSiblingIndex",1); }
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
        g_mInstantiate = pick_instantiate(objCls, trCls);
        g_mSetName = objCls?gm(objCls,"set_name",1):NULL;
        g_mGetComponent = goCls?gm(goCls,"GetComponent",1):NULL;
        if(trCls) g_setParent = gm(trCls,"SetParent",2);
        g_sNew = (fn_snew)dlsym(h,"il2cpp_string_new");
        void* tmpA = ao(d,"Unity.TextMeshPro");
        void* tmpImg = tmpA ? gi(tmpA) : NULL;
        if(tmpImg) g_tmpCls = cf(tmpImg,"TMPro","TMP_Text");
        if(!g_tmpCls && tmpImg) g_tmpCls = cf(tmpImg,"TMPro","TextMeshProUGUI");
        if(g_tmpCls){ g_typeTmp = tgo(cgt(g_tmpCls)); g_mSetText = gm(g_tmpCls,"set_text",1); }
    }
    snprintf(b,sizeof(b),"v25: ui kind=%d inst=%p setname=%p go=%p getcomp=%p sib=%p tmp=%p/%p settext=%p snew=%p\n",
             g_pick_kind,g_mInstantiate,g_mSetName,g_mGetGO,g_mGetComponent,g_mSetSibling,g_tmpCls,g_typeTmp,g_mSetText,g_sNew);
    log_app(b);

    snprintf(b,sizeof(b),"v22: ready rocket=%p offname=0x%x offloc=0x%x getValue=%p rot=%p canvas=%p/%p chd=%p/%p name=%p\n",
             g_rocketCls,g_offName,g_offLoc,g_mGetValue,g_mGetRot,g_canvasCls,g_typeCanvas,g_mGetChildCount,g_mGetChild,g_mGetName);
    log_app(b);

    void* wtime=NULL; const char* ns[]={"SFS.World","SFS",""};
    for(int k=0;k<3&&!wtime;k++) wtime=cf(img,ns[k],"VCQSQMYGGZECNBPE");
    if(!wtime){ log_app("v22: no WorldTime\n"); return NULL; }
    void* upd=gm(wtime,"Update",0);
    if(!upd){ log_app("v22: no Update\n"); return NULL; }
    void** mi=(void**)upd; uintptr_t page=4096;
    mp((void*)((uintptr_t)mi & ~(page-1)), page*2, PROT_READ|PROT_WRITE|PROT_EXEC);
    g_orig_update=mi[0]; mi[0]=(void*)hook_update;
    log_app("v22: armed\n");
    g_ready=1;
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res){
    (void)vm;(void)res;
    pthread_t t;
    if(pthread_create(&t,NULL,worker,NULL)==0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
