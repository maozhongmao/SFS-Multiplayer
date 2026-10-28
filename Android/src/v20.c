/* SPDX-License-Identifier: GPL-3.0-or-later */
// v20 (M1 hardening): figure out WHERE the real position lives.
// logs: object class, raw field pointers, Location.position/velocity,
// and Unity transform position/rotation (property getters via runtime_invoke).
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
static void* g_findMethod = NULL, *g_typeObj = NULL, *g_rocketCls = NULL, *g_mGetRot = NULL;
static void* g_mGetTransform = NULL, *g_mGetPosition = NULL, *g_mGetRotation = NULL;
static int g_offName = -1, g_offLoc = -1, g_offPos = -1, g_offVel = -1;
static fn_ri g_ri; static fn_alg g_alg; static fn_unbox g_unbox; static fn_schars g_schars;
static fn_ogc g_ogc; static fn_cgn g_cgn;
static volatile int g_frame = 0;
static volatile int g_ready = 0;

static void u16_to_ascii(uint16_t* ch, int len, char* out, int cap){
    out[0]=0; if(!ch||len<=0) return;
    int k=0;
    for(int j=0;j<len && k<cap-1;j++){ unsigned c=ch[j]; out[k++]=(c<128)?(char)c:(char)'?'; }
    out[k]=0;
}

static void dump_rockets(void){
    void* params[1]; params[0] = g_typeObj;
    void* exc = NULL;
    void* arr = g_ri(g_findMethod, NULL, params, &exc);
    if (exc) { log_app("v20: FindObjectsOfType threw\n"); return; }
    int n = arr ? (int)g_alg(arr) : 0;
    int lim = n>2?2:n;
    char b[420];
    for (int i=0;i<lim;i++){
        void* ro = ((void**)((char*)arr + 0x20))[i];
        if (!ro) continue;
        void* kls = g_ogc(ro);
        const char* kname = kls ? g_cgn(kls) : "?";
        char nm[48]; nm[0]=0;
        void* s = (g_offName>=0) ? *(void**)((char*)ro + g_offName) : NULL;
        if (s) u16_to_ascii(g_schars(s), *(int*)((char*)s + 0x10), nm, 48);
        void* loc = (g_offLoc>=0) ? *(void**)((char*)ro + g_offLoc) : NULL;
        void* rb2d = *(void**)((char*)ro + 0x60);
        double px=0,py=0,vx=0,vy=0;
        if (loc) {
            if (g_offPos>=0) { px = *(double*)((char*)loc + g_offPos); py = *(double*)((char*)loc + g_offPos + 8); }
            if (g_offVel>=0) { vx = *(double*)((char*)loc + g_offVel); vy = *(double*)((char*)loc + g_offVel + 8); }
        }
        float rot = 0.0f;
        if (g_mGetRot) { void* e2=NULL; void* r=g_ri(g_mGetRot,ro,NULL,&e2); if(r&&!e2) rot=*(float*)g_unbox(r); }
        snprintf(b,sizeof(b),"v20: [%d] ro=%p cls=%s name='%s' loc=%p rb2d=%p locpos=(%.3f,%.3f) locvel=(%.3f,%.3f) rot=%.2f\n",
                 i, ro, kname, nm, loc, rb2d, px, py, vx, vy, rot);
        log_app(b);
        if (g_mGetTransform && g_mGetPosition) {
            void* e3=NULL;
            void* tr = g_ri(g_mGetTransform, ro, NULL, &e3);
            if (tr && !e3) {
                void* e4=NULL;
                void* pv = g_ri(g_mGetPosition, tr, NULL, &e4);
                if (pv && !e4) {
                    float* f = (float*)g_unbox(pv);
                    snprintf(b,sizeof(b),"v20: [%d] transform.pos=(%.3f, %.3f, %.3f)\n", i, f[0], f[1], f[2]);
                    log_app(b);
                }
                if (g_mGetRotation) {
                    void* e5=NULL;
                    void* q = g_ri(g_mGetRotation, tr, NULL, &e5);
                    if (q && !e5) {
                        float* f = (float*)g_unbox(q);
                        snprintf(b,sizeof(b),"v20: [%d] transform.quat=(%.3f, %.3f, %.3f, %.3f)\n", i, f[0], f[1], f[2], f[3]);
                        log_app(b);
                    }
                }
            }
        }
    }
    snprintf(b,sizeof(b),"v20: rockets=%d (above)\n", n);
    log_app(b);
}

static void hook_update(void* self){
    int f = __sync_add_and_fetch(&g_frame, 1);
    if (g_ready && f % 60 == 1) dump_rockets();
    if (g_orig_update) ((void(*)(void*))g_orig_update)(self);
}

static void* worker(void* arg){
    (void)arg;
    sleep(8);
    void* h = dlopen("libil2cpp.so", RTLD_NOW);
    if (!h) { log_app("v20: no il2cpp\n"); return NULL; }
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
    fn_ogc ogc=(fn_ogc)dlsym(h,"il2cpp_object_get_class");
    fn_cgn cgn=(fn_cgn)dlsym(h,"il2cpp_class_get_name");
    fn_ri ri=(fn_ri)dlsym(h,"il2cpp_runtime_invoke");
    fn_alg alg=(fn_alg)dlsym(h,"il2cpp_array_length");
    fn_unbox unbox=(fn_unbox)dlsym(h,"il2cpp_object_unbox");
    fn_schars schars=(fn_schars)dlsym(h,"il2cpp_string_chars");
    fn_mp mp=(fn_mp)dlsym(dlopen("libc.so",RTLD_NOW),"mprotect");
    if(!dg||!ta||!ao||!gi||!cf||!gm||!cgfn||!fgt||!tgoc||!tgo||!cgt||!fgo||!ogc||!cgn||!ri||!alg||!unbox||!schars||!mp){ log_app("v20: missing syms\n"); return NULL; }
    g_ri=ri; g_alg=alg; g_unbox=unbox; g_schars=schars; g_ogc=ogc; g_cgn=cgn;
    void* d=dg(); ta(d);
    void* a=ao(d,"Assembly-CSharp"); if(!a){ log_app("v20: no asm\n"); return NULL; }
    void* img=gi(a); if(!img){ log_app("v20: no img\n"); return NULL; }
    void* coreA=ao(d,"UnityEngine.CoreModule"); if(!coreA){ log_app("v20: no core\n"); return NULL; }
    void* coreImg=gi(coreA);
    char b[260];

    g_rocketCls=cf(img,"SFS.World","FULMMHJZAEKABZAY");
    if(!g_rocketCls){ log_app("v20: no Rocket class\n"); return NULL; }
    void* objCls=cf(coreImg,"UnityEngine","Object");
    g_findMethod=objCls?gm(objCls,"FindObjectsOfType",1):NULL;
    if(!g_findMethod){ log_app("v20: no FindObjectsOfType\n"); return NULL; }
    g_typeObj=tgo(cgt(g_rocketCls));
    void* fName=cgfn(g_rocketCls,"rocketName");
    void* fLoc=cgfn(g_rocketCls,"location");
    if(fName) g_offName=fgo(fName);
    if(fLoc)  g_offLoc=fgo(fLoc);
    void* locCls = fLoc ? tgoc(fgt(fLoc)) : NULL;
    if(!locCls) locCls=cf(img,"SFS.World","FNVQPOCIQHTEDVSZ");
    if(locCls){
        void* fp=cgfn(locCls,"position"); void* fv=cgfn(locCls,"velocity");
        if(fp) g_offPos=fgo(fp);
        if(fv) g_offVel=fgo(fv);
    }
    g_mGetRot=gm(g_rocketCls,"GetRotation",0);
    void* compCls=cf(coreImg,"UnityEngine","Component");
    void* trCls=cf(coreImg,"UnityEngine","Transform");
    g_mGetTransform = compCls?gm(compCls,"get_transform",0):NULL;
    if(trCls){ g_mGetPosition=gm(trCls,"get_position",0); g_mGetRotation=gm(trCls,"get_rotation",0); }
    snprintf(b,sizeof(b),"v20: offs name=0x%x loc=0x%x pos=0x%x vel=0x%x tr=%p pos=%p rot=%p\n",
             g_offName,g_offLoc,g_offPos,g_offVel,g_mGetTransform,g_mGetPosition,g_mGetRotation);
    log_app(b);

    void* wtime=NULL; const char* ns[]={"SFS.World","SFS",""};
    for(int k=0;k<3&&!wtime;k++) wtime=cf(img,ns[k],"VCQSQMYGGZECNBPE");
    if(!wtime){ log_app("v20: no WorldTime\n"); return NULL; }
    void* upd=gm(wtime,"Update",0);
    if(!upd){ log_app("v20: no Update\n"); return NULL; }
    void** mi=(void**)upd; uintptr_t page=4096;
    mp((void*)((uintptr_t)mi & ~(page-1)), page*2, PROT_READ|PROT_WRITE|PROT_EXEC);
    g_orig_update=mi[0]; mi[0]=(void*)hook_update;
    log_app("v20: armed\n");
    g_ready=1;
    return NULL;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* res){
    (void)vm;(void)res;
    pthread_t t;
    if(pthread_create(&t,NULL,worker,NULL)==0) pthread_detach(t);
    return JNI_VERSION_1_6;
}
