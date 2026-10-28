/* SPDX-License-Identifier: GPL-3.0-or-later */
// v18 (M1): from the proven main-thread tick, find live Rocket instances and read
// their location/position/velocity/rotation -> the data source for multiplayer sync.
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
typedef void  (*fn_fgv)(void*, void*, void*);
typedef void* (*fn_fgt)(void*);
typedef void* (*fn_tgoc)(void*);
typedef void* (*fn_tgo)(void*);
typedef void* (*fn_cgt)(void*);
typedef void* (*fn_ri)(void*, void*, void**, void**);
typedef size_t (*fn_alg)(void*);
typedef void* (*fn_unbox)(void*);
typedef uint16_t* (*fn_schars)(void*);
typedef int (*fn_mp)(void*, size_t, int);

static void* g_orig_update = NULL;
static void* g_findMethod = NULL, *g_typeObj = NULL, *g_rocketCls = NULL;
static void* g_fName = NULL, *g_fLoc = NULL, *g_fPos = NULL, *g_fVel = NULL, *g_mGetRot = NULL;
static fn_ri g_ri; static fn_alg g_alg; static fn_fgv g_fgv; static fn_unbox g_unbox; static fn_schars g_schars;
static volatile int g_frame = 0;
static volatile int g_ready = 0;

static void u16_to_ascii(void* s, char* out, int cap){
    out[0]=0; if(!s) return;
    int len = *(int*)((char*)s + 0x10);
    uint16_t* ch = g_schars(s);
    if(!ch) return;
    int k=0;
    for(int j=0;j<len && k<cap-1;j++){
        unsigned c = ch[j];
        out[k++] = (c<128) ? (char)c : (char)'?';
    }
    out[k]=0;
}

static void dump_rockets(void){
    void* params[1]; params[0] = g_typeObj;
    void* exc = NULL;
    void* arr = g_ri(g_findMethod, NULL, params, &exc);
    if (exc) { log_app("v18: FindObjectsOfType threw\n"); return; }
    int n = arr ? (int)g_alg(arr) : 0;
    char b[260];
    snprintf(b,sizeof(b),"v18: rockets=%d\n", n);
    log_app(b);
    int lim = n>3?3:n;
    for (int i=0;i<lim;i++){
        void* ro = ((void**)((char*)arr + 0x20))[i];
        if (!ro) continue;
        char nm[48]; nm[0]=0;
        void* s = NULL;
        if (g_fName) { g_fgv(g_fName, ro, &s); u16_to_ascii(s, nm, 48); }
        double pv[2] = {0,0}, vv[2] = {0,0};
        void* loc = NULL;
        if (g_fLoc) { g_fgv(g_fLoc, ro, &loc); }
        if (loc) {
            if (g_fPos) g_fgv(g_fPos, loc, pv);
            if (g_fVel) g_fgv(g_fVel, loc, vv);
        }
        float rot = 0.0f;
        if (g_mGetRot) {
            void* exc2 = NULL;
            void* r = g_ri(g_mGetRot, ro, NULL, &exc2);
            if (r && !exc2) rot = *(float*)g_unbox(r);
        }
        snprintf(b,sizeof(b),"v18: [%d] name='%s' pos=(%.4f, %.4f) vel=(%.4f, %.4f) rot=%.2f\n",
                 i, nm, pv[0], pv[1], vv[0], vv[1], rot);
        log_app(b);
    }
}

static void hook_update(void* self){
    int f = __sync_add_and_fetch(&g_frame, 1);
    if (g_ready && f % 30 == 1) dump_rockets();
    if (g_orig_update) ((void(*)(void*))g_orig_update)(self);
}

static void* worker(void* arg){
    (void)arg;
    sleep(8);
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v18: no il2cpp\n"); return NULL; }
    fn0 dg = (fn0)dlsym(h,"il2cpp_domain_get");
    fn_ta ta = (fn_ta)dlsym(h,"il2cpp_thread_attach");
    fn_ao ao = (fn_ao)dlsym(h,"il2cpp_domain_assembly_open");
    fn_gi gi = (fn_gi)dlsym(h,"il2cpp_assembly_get_image");
    fn_cf cf = (fn_cf)dlsym(h,"il2cpp_class_from_name");
    fn_gm gm = (fn_gm)dlsym(h,"il2cpp_class_get_method_from_name");
    fn_cgfn cgfn = (fn_cgfn)dlsym(h,"il2cpp_class_get_field_from_name");
    fn_fgv fgv = (fn_fgv)dlsym(h,"il2cpp_field_get_value");
    fn_fgt fgt = (fn_fgt)dlsym(h,"il2cpp_field_get_type");
    fn_tgoc tgoc = (fn_tgoc)dlsym(h,"il2cpp_type_get_class_or_element_class");
    fn_tgo tgo = (fn_tgo)dlsym(h,"il2cpp_type_get_object");
    fn_cgt cgt = (fn_cgt)dlsym(h,"il2cpp_class_get_type");
    fn_ri ri = (fn_ri)dlsym(h,"il2cpp_runtime_invoke");
    fn_alg alg = (fn_alg)dlsym(h,"il2cpp_array_length");
    fn_unbox unbox = (fn_unbox)dlsym(h,"il2cpp_object_unbox");
    fn_schars schars = (fn_schars)dlsym(h,"il2cpp_string_chars");
    fn_mp mp = (fn_mp)dlsym(dlopen("libc.so",RTLD_NOW),"mprotect");
    if(!dg||!ta||!ao||!gi||!cf||!gm||!cgfn||!fgv||!fgt||!tgoc||!tgo||!cgt||!ri||!alg||!unbox||!schars||!mp){ log_app("v18: missing syms\n"); return NULL; }
    g_ri=ri; g_alg=alg; g_fgv=fgv; g_unbox=unbox; g_schars=schars;
    void* d = dg(); ta(d);
    void* a = ao(d,"Assembly-CSharp"); if(!a){ log_app("v18: no asm\n"); return NULL; }
    void* img = gi(a); if(!img){ log_app("v18: no img\n"); return NULL; }
    void* coreA = ao(d,"UnityEngine.CoreModule"); if(!coreA){ log_app("v18: no core\n"); return NULL; }
    void* coreImg = gi(coreA);
    char b[220];

    g_rocketCls = cf(img,"SFS.World","FULMMHJZAEKABZAY");
    if(!g_rocketCls){ log_app("v18: no Rocket class\n"); return NULL; }
    void* objCls = cf(coreImg,"UnityEngine","Object");
    if(!objCls){ log_app("v18: no Object class\n"); return NULL; }
    g_findMethod = gm(objCls,"FindObjectsOfType",1);
    if(!g_findMethod){ log_app("v18: no FindObjectsOfType\n"); return NULL; }
    g_typeObj = tgo(cgt(g_rocketCls));
    if(!g_typeObj){ log_app("v18: no Type obj\n"); return NULL; }
    g_fName = cgfn(g_rocketCls,"rocketName");
    g_fLoc  = cgfn(g_rocketCls,"location");
    if(!g_fName || !g_fLoc){ log_app("v18: no rocket fields\n"); return NULL; }
    void* locCls = tgoc(fgt(g_fLoc));
    void* locCls2 = cf(img,"SFS.World","FNVQPOCIQHTEDVSZ");
    if(!locCls) locCls = locCls2;
    if(locCls){
        g_fPos = cgfn(locCls,"position");
        g_fVel = cgfn(locCls,"velocity");
    }
    g_mGetRot = gm(g_rocketCls,"GetRotation",0);
    snprintf(b,sizeof(b),"v18: ready rocket=%p fname=%p floc=%p loccls=%p fpos=%p fvel=%p rot=%p type=%p\n",
             g_rocketCls,g_fName,g_fLoc,locCls,g_fPos,g_fVel,g_mGetRot,g_typeObj);
    log_app(b);

    void* wtime = NULL;
    const char* ns[] = {"SFS.World","SFS",""};
    for(int k=0;k<3 && !wtime;k++) wtime = cf(img,ns[k],"VCQSQMYGGZECNBPE");
    if(!wtime){ log_app("v18: no WorldTime class\n"); return NULL; }
    void* upd = gm(wtime,"Update",0);
    if(!upd){ log_app("v18: no WorldTime.Update\n"); return NULL; }
    void** mi = (void**)upd;
    uintptr_t page = 4096;
    mp((void*)((uintptr_t)mi & ~(page-1)), page*2, PROT_READ|PROT_WRITE|PROT_EXEC);
    g_orig_update = mi[0];
    mi[0] = (void*)hook_update;
    log_app("v18: hooked WorldTime.Update -> main thread probe armed\n");
    g_ready = 1;
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res){
    (void)vm;(void)res;
    pthread_t t;
    if(pthread_create(&t,NULL,worker,NULL)==0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
