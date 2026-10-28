/* SPDX-License-Identifier: GPL-3.0-or-later */
// v43: SFS-Pro route — NOJ drawer Open()+Draw* ; v41: probe mode — no overlay button; auto-summon the cloned '多人联机' button when the main menu is ready; click -> login page.
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
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>
#include <setjmp.h>
#include <signal.h>
#include <pthread.h>
#include <sys/mman.h>
#include <stdint.h>
#include <stdlib.h>
#include "mp_types.h"

/* v129: 数据目录运行时化 —— 共存版（改过包名）的 /sdcard/Android/data/<包名>/files 和官方不是同一个，
 * 写死官方包名会导致新包里探针/配置全部静默失效（不同 uid 根本写不进去）。 */
static char g_pkgName[96] = "com.StefMorojna.SpaceflightSimulator";
static char g_dataDir[192] = "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files";
static char g_mediaDir[192] = "/sdcard/Android/media/com.StefMorojna.SpaceflightSimulator";

const char* sfsmp_dp(const char* name){
    static char bufs[4][256]; static int k = 0;
    char* b = bufs[k]; k = (k + 1) & 3;
    snprintf(b, 256, "%s/%s", g_dataDir, name);
    return b;
}
#define dp(x) sfsmp_dp(x)
static const char* pkg(void){ return g_pkgName; }
static const char* mediaDir(void){ return g_mediaDir; }

void sfsmp_set_package(const char* p){
    if (!p || !p[0]) return;
    if (strlen(p) > 90) return;
    snprintf(g_pkgName, sizeof(g_pkgName), "%s", p);
    snprintf(g_dataDir,  sizeof(g_dataDir),  "/sdcard/Android/data/%s/files", p);
    snprintf(g_mediaDir, sizeof(g_mediaDir), "/sdcard/Android/media/%s", p);
}
#define MARKER (dp("sfsmp-probe.txt"))
static volatile int g_mode = 1;
/* v68: 档位机制取消 —— 「多人联机」按钮永久激活（用户 2026-10-23 决定：先给一批测试人员用，
 * 补丁安装器分发包做好之后再正式发布。原来读 sfsmp-mode.txt 的开关全部废弃，恒为 20 = 完整流程：
 * 点按钮 → 原生输入页（服务器/用户名/密码）→ 加入服务器 → 连服务器 → 进世界）。 */
static int read_mode(void){
    return 20;
}
static void log_app(const char* m){ int fd=open(MARKER,O_WRONLY|O_CREAT|O_APPEND,0644); if(fd>=0){write(fd,m,strlen(m));close(fd);} }

/* v30 globals */
static void* (*g_gm)(void*, const char*, int)=NULL;    /* il2cpp_class_get_method_from_name */
static void* (*g_mObjGetClass)(void*)=NULL;            /* il2cpp_object_get_class */
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
static void* g_mSetEuler = NULL; static int g_rotLogged = 0;   /* v152: 朝向修正 */
/* v153: 可玩版位姿跟随 —— 不再每帧瞬移，偏差小就用速度追 */
static double g_pvx = 0.0, g_pvy = 0.0; static int g_snap = 1; static int g_snapN = 0, g_pursueN = 0;
static void* g_mGetChildCount = NULL, *g_mGetChild = NULL, *g_mGetValue = NULL, *g_mSetValue = NULL;
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
static void* g_cloneText=NULL;   /* v91: 克隆按钮上的文字组件，用来做「点下去有反应」的反馈 */
static void* g_mmCls=NULL, *g_mmInst=NULL, *g_mOpenCredits=NULL, *g_mmMainField=NULL;
static void (*g_staticGet)(void*, void*) = NULL;
static void* g_mGetSizeDelta=NULL, *g_mSetSizeDelta=NULL, *g_mGetLocalScale=NULL, *g_mSetLocalScale=NULL;
/* v132: 让克隆按钮和源按钮一样宽 —— anchors / pivot / LayoutElement 三件套 */
static void* g_mAnchorMinGet=NULL, *g_mAnchorMinSet=NULL, *g_mAnchorMaxGet=NULL, *g_mAnchorMaxSet=NULL;
static void* g_mPivotGet=NULL, *g_mPivotSet=NULL;
static void* g_leCls=NULL, *g_typeLE=NULL;
static void* g_mLeGetPrefW=NULL, *g_mLeSetPrefW=NULL, *g_mLeGetPrefH=NULL, *g_mLeSetPrefH=NULL;
static void* g_mAddComponent=NULL;
static void* g_mGetRect=NULL;
static int g_offClick=-1, g_offHold=-1;
static volatile int g_pressed=0;
static void mainmenu_button(void);
static void page_show_login(void);
static void native_text_input(void);
static void* find_text_component(void* go);
static void page_hide_login(void);
static void check_statement_scroll(JNIEnv*);
static void native_window_test(void);
static void native_mods_open(void);
static int drain(JNIEnv* env, const char* tag);
static void poll_click(void);
static void connect_and_enter_world(void);
static void pin_view_to_launchpad(void);
static jmethodID g_mPostDelayed = NULL;
static volatile int g_click = 0;
static int g_tiWasOpen = 0;
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

static void* g_rockCls=NULL, *g_rockType=NULL;

/* v110: 我们自己生成的远端火箭对象表（定义在下面的 rr_* 区块）。
 * publish_local_pose 选"我自己的火箭"时必须排除它们 —— 前置声明放这里。 */
static void* g_rrRocket[MP_RR_MAX];

extern void mp_set_local_pose(double x, double y, double vx, double vy, const char* planet, float rot);
extern void mp_set_local_angular(float av);   /* v107 */
extern void mp_net_connect(const char* hostport);
extern int mp_is_joined(void);
extern unsigned long long mp_last_rx_age_ms(void);
extern double mp_server_world_time(void);   /* v97 */
static void* g_srcBtn=NULL; static void* g_mEvtInvoke=NULL; static int g_autoStarted=0;
/* ---- v64: enter the world through the game's own scene loader ---- */
static void* g_sceneLoaderCls=NULL, *g_typeSceneLoader=NULL, *g_mLoadWorldScene=NULL;
/* v100: 进世界前把视角钉到发射台（PC 端 DivertLoading 同款：WorldView.main.SetViewLocation(Base.planetLoader.spaceCenter.LaunchPadLocation)）
 * WorldView = SFS.World.ZIMVTWPJZBTPPXSR（dump.cs:26023），静态 main @0x0，SetViewLocation(Location) RVA 0x16CC208（dump.cs:26115）
 * Base = SFS.RQOFTSKFXWRDBQCA（dump.cs:40945），planetLoader @0x40（dump.cs:40952）
 * planetLoader = SFS.WorldBase.YXHFAUIATGCKNHYQ（dump.cs:8660），spaceCenter @0x60（dump.cs:8678）
 * SpaceCenterData.LaunchPadLocation getter RVA 0x17AC594（dump.cs:4742/4750） */
static void* g_worldViewCls=NULL, *g_worldViewMainField=NULL, *g_mSetViewLocation=NULL;
static void* g_baseCls=NULL, *g_typeBase=NULL;
static int g_offPlanetLoader=0;
static void* g_plCls=NULL, *g_scDataCls=NULL, *g_mGetLaunchPad=NULL;
static int g_offSpaceCenter=0;
static int g_viewPinned=0;
static int g_pinTries=0;
static int g_dumpDone=0;
static int g_rotEulerLogged=0;
static int g_lastEulerOk=0;      /* v127: euler 是否读成功（0° 是合法值，不能当哨兵） */
static int g_ctrlScanLogged=0;
/* v118: 唯一权威"我是谁" —— SFS.World.PlayerController.main.player.Value */
static void* g_pcCls=NULL, *g_pcMainField=NULL;
static int   g_offPcPlayer=-1, g_offPcHasCtrl=-1, g_offPcCamDist=-1;
static int   player_controller_resolve(void);
/* v109: 找"我自己的那枚火箭"时的诊断计数（只在变化时打日志，避免刷屏） */
static int g_pickLogPrev=-99, g_pickLogN=-1;
/* v70: 官方进世界链路 -> SFS.WorldBase.FAELUUBBVIPEYYXS.EnterWorld(string worldName, Action cb) RVA 0x16B1BD0 */
static void* g_wbCls=NULL, *g_mEnterWorld=NULL;
static volatile int g_enterTried=0, g_enterWait=0, g_enterFallback=0;
static volatile int g_tmpWorldMade=0, g_loadWorldDone=0;   /* v97: 临时沙盒世界 / 已切过场景 */
static void enter_world_fallback(void);
static int enter_world_official(void);
static volatile int g_enterReq = 0, g_enterLogs = 0;
/* v73: 完整结构上传 */
/* v126: 上行结构统一放到 mp_types.h（要和 mp_net.c 的编码器共享，字段必须完全一致） */
typedef mp_up_part_t  v73_part_rec;
typedef mp_up_joint_t v73_joint_rec;
typedef mp_up_stage_t v73_stage_rec;
static void up_fill_vars(v73_part_rec* r, void* pe);
extern void mp_set_structure(const char* name, const v73_part_rec* parts, int nparts,
                             const v73_joint_rec* joints, int njoints,
                             const v73_stage_rec* stages, int nstages);
extern void mp_reset_structure(void);
extern int  mp_structure_ready(void);
static void* g_rsCls=NULL, *g_mRsCtor=NULL;
static volatile int g_stUploaded = 0;
static v73_part_rec  g_up_parts[1024];
static v73_joint_rec g_up_joints[4096];
static v73_stage_rec g_up_stages[64];
static void upload_structure(void* rocket);
static void log_exc(void* exc, const char* tag);
static void ux_poll_conn(void);
static void ux_connect_started(void);

static void dump_rockets(void);
static void publish_local_pose(void){
    if(!g_rockCls || !g_rockType || !g_findMethod || !g_mGetValue || g_offLoc < 0) return;
    void* e=NULL; void* p[1]; p[0]=g_rockType;
    void* arr=g_ri(g_findMethod, NULL, p, &e);
    if(!arr || e) return;
    int n=(int)g_alg(arr);
    if(n <= 0) return;
    /* v109: 只上传"我自己真正操控的那枚火箭"的位置。
     *
     * 历史教训：v108 及以前盲取数组第 0 个 —— 场上只要留着一枚残留的远端火箭
     * （我们自己 rr_try_spawn 生成的 'MP-Remote'，坐标 631xxx ≈ 2 倍地球半径），
     * 就会把**它的地心坐标当成自己的**发出去。这正是"别人看你飞 300 多公里"的真凶。
     *
     * 基类 DXYTHXTABGUDBUFN（dump.cs:21647）：
     *   location 0x38 / mapPlayer 0x40 / isPlayer 0x48 / hasControl 0x50
     * 两者都是 Bool_Local : Obs<bool>，Obs<T> 的字段 value 在 0x0（dump.cs:45593），
     * 加上 Il2Cpp 对象头 0x10 ⇒ 布尔值在 +0x10。
     *
     * 选取优先级：hasControl(1) > isPlayer(2) > 都没有(0，拒绝上报)。
     * 以前还会兜底 data[0]，现在**兜底已删除** —— 宁可这一帧不报，也不能把别人的火箭当成自己。 */
    /* v118: 唯一权威来源 —— PlayerController.main.player.Value。
     * 权威源码 D:/sfs-tools/ref/PCSFS/SFS.World/PlayerController.cs:32-34
     *   public static PlayerController main;  public Player_Local player;
     * Player_Local : Obs_Destroyable<Player> : Obs<Player>（SFS.Variables/Obs_Destroyable.cs:6、Obs.cs:11）
     *   private T value;  ⇒ 火箭对象在 obs 对象 +0x10。
     *
     * 为什么废掉 v109/v111 的"猜 hasControl / 猜 isPlayer / 猜名字"：
     * 实测（artifacts/probe114.txt:201-207）同一帧里
     *   真正的玩家火箭 rot=89.98，而我们读到的对象 rot=0.00 —— 差 90°，
     * 说明选中的是残留副本。残留副本带 hasControl=true、已脱离 g_rrRocket[]、
     * 名字也不是 'MP-Remote'，两道过滤全漏。猜是猜不出来的，只能问游戏"你是谁"。
     *
     * 注意：这里**不加也不减任何角度**。v115 的 rot-=90 是在错对象上打的补丁，已撤。 */
    void* ro=NULL;
    int pickHow=0;
    {
        if(g_pcCls && g_pcMainField){
            void* pc=NULL; g_staticGet(g_pcMainField, &pc);
            if(pc && g_offPcPlayer >= 0){
                void* PL=*(void**)((char*)pc + g_offPcPlayer);
                if(PL){
                    void* pv=*(void**)((char*)PL + 0x10);
                    if(pv){
                        void* cl = g_mObjGetClass ? g_mObjGetClass(pv) : NULL;
                        if(cl && cl == g_rockCls){ ro=pv; pickHow=1; }
                        else pickHow=-1;   /* 玩家当前不是火箭（EVA/建筑）⇒ 本帧不上报 */
                    }
                }
            }
        }
        if(!ro && !pickHow){
            /* 旧路径兜底：PlayerController 没解析到时才退回去猜 hasControl（带排除） */
            void** data=(void**)((char*)arr+0x20);
            for(int i=0;i<n;i++){
                void* cand=data[i];
                if(!cand) continue;
                { int isRemote=0;
                  for(int k=0;k<MP_RR_MAX;k++){ if(g_rrRocket[k] && g_rrRocket[k]==cand){ isRemote=1; break; } }
                  if(!isRemote && g_offName >= 0){
                      char nn[48] = "";
                      void* ns = *(void**)((char*)cand + g_offName);
                      if(ns) u16_to_ascii(ns, nn, sizeof(nn));
                      if(nn[0] && strcmp(nn, "MP-Remote") == 0) isRemote = 1;
                  }
                  if(isRemote) continue; }
                void* ob=*(void**)((char*)cand + 0x50);
                if(ob && *(unsigned char*)((char*)ob + 0x10)){ ro=cand; pickHow=2; break; }
            }
        }
        if(!g_ctrlScanLogged || g_pickLogPrev!=pickHow){
            g_ctrlScanLogged=1; g_pickLogPrev=pickHow;
            char sb[220];
            snprintf(sb,sizeof(sb),"v118: rockets=%d pickHow=%d (1=PlayerController.player 2=hasControlFallback 0=NONE -1=player-not-rocket) pc=%p offPcPlayer=%d\n",
                     n, pickHow, g_pcCls, g_offPcPlayer);
            log_app(sb);
        }
    }
    if(!ro) return;   /* 没有"我的火箭"就一帧都不报，绝不用别人的坐标顶上 */
    void* loc=*(void**)((char*)ro + g_offLoc);
    if(!loc) return;
    void* lv=g_ri(g_mGetValue, loc, NULL, &e);
    if(!lv || e) return;
    double t=*(double*)((char*)lv+0x10);
    double px=*(double*)((char*)lv+0x20), py=*(double*)((char*)lv+0x28);
    double vx=*(double*)((char*)lv+0x30), vy=*(double*)((char*)lv+0x38);
    void* pl=*(void**)((char*)lv+0x18);
    char pname[40]="Earth"; if(pl) obj_name(pl, pname, sizeof(pname));
    /* v107: 角度必须用和 PC 端完全相同的定义。
     * 权威：D:/sfs-tools/ref/PCSFS/SFS.World/RocketSave.cs:43
     *   rotation = rocket.rb2d.transform.eulerAngles.z;
     * D:/SFS-Multiplayer/Client/src/MultiplayerSFS.Client/MultiplayerSFS.Mod/LocalManager.cs:1227 同款
     *
     * v103 发的是 Rocket.GetRotation() —— 那是**地图图标朝向**（SFS.World/Rocket.cs:267 硬 +90f），
     * 不是物体朝向 -> 对端恒定偏 90°。
     * v107 改用 Rigidbody2D.get_rotation（无参、返回 float，Unity 里与 transform.eulerAngles.z 恒等），
     * 不用 get_eulerAngles + Vector3.z —— 那条路返回结构体走隐藏指针，偏移极易写错（v106 就写错了）。
     * 同时取真实 angularVelocity，让对端能外推，不再"一漂就错位"。 */
    float rot=0.0f, av=0.0f;
    {
        void* rb = *(void**)((char*)ro + 0x60);
        if(rb && g_mObjGetClass && g_gm){
            void* rbCls = g_mObjGetClass(rb);
            void* mr = rbCls ? g_gm(rbCls, "get_rotation", 0) : NULL;
            void* ma = rbCls ? g_gm(rbCls, "get_angularVelocity", 0) : NULL;
            if(mr){
                void* e5=NULL; void* r = g_ri(mr, rb, NULL, &e5);
                if(r && !e5) rot = *(float*)g_unbox(r);
            }
            if(ma){
                void* e6=NULL; void* v = g_ri(ma, rb, NULL, &e6);
                if(v && !e6) av = *(float*)g_unbox(v);
            }
            /* v114: PC 端写回的字段是 rocket.rb2d.transform.eulerAngles.z
             * （Client/.../LocalManager.cs:1294 和 :1735 两处都是 new Vector3(0f,0f,state.rotation)），
             * 不是 rb2d.rotation。两者只在 transform 没有旋转父级时相等。
             * 所以这里同时取 transform.eulerAngles.z 做对照：
             *  - eulerOk=1 就用 euler 值（和 PC 完全同源）；
             *  - eulerOk=0 才退回 rb2d.rotation。
             * Vector3.z 在 0x08（不是 0x18，v106 就是错在这）；返回值按指针处理更稳。 */
            int eulerOk = 0; float eulerZ = 0.0f;
            {
                void* mt = rbCls ? g_gm(rbCls, "get_transform", 0) : NULL;
                if(mt){
                    void* et=NULL; void* tr = g_ri(mt, rb, NULL, &et);
                    if(tr && !et){
                        void* trCls = g_mObjGetClass(tr);
                        void* me = trCls ? g_gm(trCls, "get_eulerAngles", 0) : NULL;
                        if(me){
                            void* e7=NULL; void* v3 = g_ri(me, tr, NULL, &e7);
                            if(v3 && !e7){ eulerZ = *(float*)((char*)v3 + 0x08); eulerOk = 1; }
                        }
                    }
                }
            }
            if(!g_rotEulerLogged){ g_rotEulerLogged=1;
                char rb2[260]; snprintf(rb2,sizeof(rb2),"v114: rot rb.rotation=%.2f tr.eulerZ=%.2f eulerOk=%d angVel=%.3f -> using=%s\n",
                    rot, eulerZ, eulerOk, av, "rb.rotation"); log_app(rb2); }
            g_lastEulerOk = eulerOk;
            /* v155: 实测（v114 日志）手机端 rb2d.rotation=45.07 而 transform.eulerAngles.z=0.00
             * —— 这版 SFS 里刚体的旋转**不会**镜像到 transform，所以 euler 恒为 0。
             * 之前"eulerOk 就用 euler"把真实角度 45° 换成了 0°，这就是"火箭永远朝上"的根因。
             * 现在一律用 rb2d.rotation（真值），euler 只作日志参考。 */
            (void)eulerZ;
            /* v118: 撤销 v115 的 rot -= 90。
             * bug-hunter 第二轮报告（docs/bug-rotation-90-round2.md）证明：
             * 90° 偏差不是角度本身错，而是选错了火箭（残留副本 vs 玩家真火箭，
             * 证据 artifacts/probe114.txt:201-207 同帧 rot=89.98 vs 读到的 0.00）。
             * 在错误来源上再减 90 只是把错误换个方向。现在来源已换成
             * PlayerController.main.player.Value，**不发任何补偿**。 */
            float r2 = rot;
            while(r2 < 0.0f) r2 += 360.0f;
            while(r2 >= 360.0f) r2 -= 360.0f;
            rot = r2;
        }
        /* v127: 0° 是合法角度（火箭直立停在发射台就是 0），不能当"读失败"哨兵。
         * bug-hunter 实测：手机端上行恒定发 rot=90.00，就是因为 euler 读到 0.00 后
         * 被这个哨兵判定成失败、回退到 GetRotation()（atan2 推力矢量 = 90°）。
         * PC 端上行下行都用 transform.eulerAngles.z（LocalManager.cs:1227/1294/1735），
         * 所以只要这里不再回退，两端就同源。 */
        if(!g_lastEulerOk && g_mGetRot){ void* e4=NULL; void* r=g_ri(g_mGetRot, ro, NULL, &e4);
            if(r && !e4) rot = *(float*)g_unbox(r) - 90.0f; }
    }
    (void)t;
    mp_set_local_pose(px, py, vx, vy, pname, rot);
    mp_set_local_angular(av);
    if(!g_stUploaded) upload_structure(ro);      /* v73: 第一次拿到本机火箭就把结构发上去 */
}

/* v73: 用游戏自己的 RocketSave 序列化当前火箭，把 parts/joints/stages 抄进 mp_net。
 * RocketSave ctor(Rocket) RVA 0x1852640；字段 parts 0x38 / joints 0x40 / stages 0x48。
 * PartSave: name 0x10 / position(Vector2) 0x18 / orientation(Orientation*) 0x20 / temperature 0x28
 * JointSave: partIndex_A 0x10 / partIndex_B 0x14 ; StageSave: stageId 0x10 / partIndexes(int[]) 0x18
 */
static void upload_structure(void* rocket){
    char b[220];
    if(!rocket) return;
    if(!g_rsCls || !g_mRsCtor || !g_mNewObj){ log_app("v73: RocketSave class missing\n"); return; }
    void* obj = g_mNewObj(g_rsCls);
    if(!obj){ log_app("v73: RocketSave alloc failed\n"); return; }
    void* a1[1]; a1[0] = rocket; void* e = NULL;
    g_ri(g_mRsCtor, obj, a1, &e);
    if(e){ log_app("v73: RocketSave ctor threw\n"); log_exc(e, "v73"); return; }
    void* pa = *(void**)((char*)obj + 0x38);
    void* ja = *(void**)((char*)obj + 0x40);
    void* sa = *(void**)((char*)obj + 0x48);
    int np = pa ? (int)g_alg(pa) : 0;
    int nj = ja ? (int)g_alg(ja) : 0;
    int ns = sa ? (int)g_alg(sa) : 0;
    if(np < 0) np = 0; if(np > 1024) np = 1024;
    if(nj < 0) nj = 0; if(nj > 4096) nj = 4096;
    if(ns < 0) ns = 0; if(ns > 64)   ns = 64;
    int i, k;
    for(i=0;i<np;i++){
        void* pe = ((void**)((char*)pa + 0x20))[i];
        v73_part_rec* r = &g_up_parts[i];
        memset(r, 0, sizeof(*r));
        if(!pe) continue;
        u16_to_ascii(*(void**)((char*)pe + 0x10), r->name, 48);
        r->x = *(float*)((char*)pe + 0x18);
        r->y = *(float*)((char*)pe + 0x1c);
        void* ori = *(void**)((char*)pe + 0x20);
        if(ori){ r->ox = *(float*)((char*)ori + 0x10); r->oy = *(float*)((char*)ori + 0x14); r->oz = *(float*)((char*)ori + 0x18); }
        r->temp = *(float*)((char*)pe + 0x28);
        up_fill_vars(r, pe);            /* v126: 变量字典（尺寸/开关/文本） */
    }
    { int ui, tot=0, tvt=0, txt=0;
      for(ui=0;ui<np;ui++){ tot+=g_up_parts[ui].nnv; tvt+=g_up_parts[ui].ntv; txt+=g_up_parts[ui].nxv; }
      { char vb[220];
        snprintf(vb,sizeof(vb),"v126b: vars total nv=%d tv=%d xv=%d over %d parts\n", tot, tvt, txt, np);
        log_app(vb); }
      for(ui=0; ui<np && ui<6; ui++){
        char vb[240]; v73_part_rec* rr0=&g_up_parts[ui];
        snprintf(vb,sizeof(vb),"v126b: p%d '%s' nv=%d tv=%d xv=%d v0='%s'=%.3f\n",
                 ui, rr0->name, rr0->nnv, rr0->ntv, rr0->nxv,
                 rr0->nnv>0?rr0->nv[0].name:"-", rr0->nnv>0?rr0->nv[0].v:0.0);
        log_app(vb);
      } }
    int nj2 = 0;
    for(i=0;i<nj;i++){
        void* je = ((void**)((char*)ja + 0x20))[i];
        int ia, ib;
        if(!je) continue;
        ia = *(int*)((char*)je + 0x10); ib = *(int*)((char*)je + 0x14);
        if(ia < 0 || ib < 0 || ia >= np || ib >= np) continue;
        g_up_joints[nj2].a = ia + 1; g_up_joints[nj2].b = ib + 1;
        nj2++;
    }
    int ns2 = 0;
    for(i=0;i<ns;i++){
        void* se = ((void**)((char*)sa + 0x20))[i];
        v73_stage_rec* r;
        if(!se) continue;
        r = &g_up_stages[ns2];
        memset(r, 0, sizeof(*r));
        r->stage_id = *(int*)((char*)se + 0x10);
        { void* ids = *(void**)((char*)se + 0x18);
          int cnt = ids ? (int)g_alg(ids) : 0;
          if(cnt < 0) cnt = 0; if(cnt > 256) cnt = 256;
          r->n = cnt;
          for(k=0;k<cnt;k++) r->ids[k] = ((int*)((char*)ids + 0x20))[k] + 1; }
        ns2++;
    }
    { char nm[64] = "MP-Android";
      void* nms = *(void**)((char*)obj + 0x10);
      if(nms) u16_to_ascii(nms, nm, sizeof(nm));
      mp_set_structure(nm, g_up_parts, np, g_up_joints, nj2, g_up_stages, ns2);
      g_stUploaded = 1;
      snprintf(b, sizeof(b), "v73: uploaded structure parts=%d joints=%d stages=%d name='%s'\n", np, nj2, ns2, nm);
      log_app(b); }
}

static void dump_rockets(void){
    void* params[1]; params[0] = g_typeObj;
    void* exc = NULL;
    void* arr = g_ri(g_findMethod, NULL, params, &exc);
    if (exc || !arr) { log_app("m1: FindObjects failed\n"); return; }
    int n = (int)g_alg(arr);
    int shown = 0;
    char b[320];
    for (int i=0;i<n && shown<4;i++){
        void* ro = ((void**)((char*)arr + 0x20))[i];
        if (!ro) continue;
        char nm[48]; nm[0]=0;
        if (g_offName>=0) u16_to_ascii(*(void**)((char*)ro + g_offName), nm, 48);
        /* 暂时不过滤名字：先看清这 6 个 Rocket 类实例到底叫什么 */
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
        /* v120: 同一对象三口径对照 —— 这是分辨"选错对象"与"口径不同"的唯一可靠办法。
         * 曾经把 m1 的 rot（=Rocket.GetRotation，含 +90f）当成"真值"去比 publish_local_pose
         * 的 eulerAngles.z，得出"选错火箭"的错误结论。这里把三者放在同一行。 */
        float rbRot=0.0f, euZ=0.0f; int euOk=0;
        { void* rb2 = *(void**)((char*)ro + 0x60);
          if(rb2 && g_mObjGetClass && g_gm){
              void* c2 = g_mObjGetClass(rb2);
              void* m1x = c2 ? g_gm(c2,"get_rotation",0) : NULL;
              if(m1x){ void* q=NULL; void* rv=g_ri(m1x,rb2,NULL,&q); if(rv&&!q) rbRot=*(float*)g_unbox(rv); }
              void* mt2 = c2 ? g_gm(c2,"get_transform",0) : NULL;
              if(mt2){ void* q2=NULL; void* tr2=g_ri(mt2,rb2,NULL,&q2);
                  if(tr2 && !q2){ void* c3=g_mObjGetClass(tr2); void* me2=c3?g_gm(c3,"get_eulerAngles",0):NULL;
                      if(me2){ void* q3=NULL; void* vv=g_ri(me2,tr2,NULL,&q3); if(vv&&!q3){ euZ=*(float*)((char*)vv+0x08); euOk=1; } } } }
          } }
        snprintf(b,sizeof(b),"m1: [%d] name='%s' planet='%s' t=%.2f pos=(%.3f, %.3f) vel=(%.3f, %.3f) rot=%.2f rbRot=%.2f eulZ=%.2f euOk=%d d1=%.2f d2=%.2f\n",
                 shown, nm, pname, t, px, py, vx, vy, rot, rbRot, euZ, euOk,
                 rot-rbRot, rbRot-euZ);
        log_app(b);
        shown++;
    }
    snprintf(b,sizeof(b),"m1: scanned=%d rockets=%d\n", n, shown); log_app(b);
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



/* ---------- v44: clone "- World Creation -/Holder" as native login window ---------- */
static void* g_goCls2=NULL, *g_mSetActive2=NULL, *g_loginWin=NULL, *g_trType2=NULL;
static void* g_popCls=NULL, *g_popType=NULL, *g_mPopOpen=NULL;
static void* g_mSetAsLast=NULL, *g_mGetSiblingIdx2=NULL, *g_mSetSiblingIdx2=NULL, *g_mSetParent2=NULL;

static void* tr_find_named(const char* want){
    void* exc=NULL; void* params[1]; void* arr; int n;
    if(!g_mFindAll || !g_trType2) return NULL;
    params[0]=g_trType2;
    arr=g_ri(g_mFindAll, NULL, params, &exc);
    n=(arr && !exc) ? (int)g_alg(arr) : -1;
    if(n<=0){ char b[120]; snprintf(b,sizeof(b),"v44: transforms=%d exc=%p\n",n,exc); log_app(b); return NULL; }
    for(int i=0;i<n;i++){
        void* t=((void**)((char*)arr+0x20))[i];
        void* go2; char nm[80];
        if(!t) continue;
        go2 = g_mGetGO ? g_ri(g_mGetGO, t, NULL, &exc) : NULL;
        if(!go2) continue;
        obj_name(go2, nm, 80);
        if(strcmp(nm, want)==0) return t;
    }
    return NULL;
}

static void log_tree(void* tr, const char* tag){
    char b[200]; void* e=NULL; int cc=0;
    if(!tr) return;
    { void* r=g_ri(g_mGetChildCount, tr, NULL, &e); if(r && !e) cc=*(int32_t*)g_unbox(r); }
    snprintf(b,sizeof(b),"v44: %s childCount=%d\n", tag, cc); log_app(b);
    for(int i=0;i<cc && i<24;i++){
        int32_t ci=i; void* p1[1]={&ci}; void* ch; char nm[72];
        ch=g_ri(g_mGetChild, tr, p1, &e); if(!ch||e) continue;
        void* cgo=g_ri(g_mGetGO, ch, NULL, &e); if(!cgo) continue;
        obj_name(cgo, nm, 72);
        snprintf(b,sizeof(b),"v44:   [%d] '%s'\n", i, nm); log_app(b);
    }
}


/* ---------- v44b: native text-input dialog (DEOTNULUBFZWSSUA.textInput) ---------- */
static void* (*g_mgp2)(void*, uint32_t);
static void* (*g_cfit)(void*);
static void* make_del(void*, void*);
static void u16_to_ascii(void*, char*, int);
static void* g_mSetContentType=NULL, *g_mActivateInput=NULL, *g_boxCls=NULL, *g_mBoxOpen=NULL;
static void* g_evtSysCls=NULL, *g_mSetSel=NULL, *g_mCurEvtSys=NULL;
static void* g_mGoActive=NULL;
static void* g_mW2S=NULL, *g_mCamMain=NULL, *g_v3Cls=NULL, *g_mGetPos2=NULL;
static float g_clonePos[3] = {0,0,0};
static int   g_clonePosOk = 0;
static void* g_cloneTr=NULL;
static void* g_sceneMgrCls=NULL, *g_mGetActiveScene=NULL, *g_sceneCls=NULL, *g_mSceneGetName=NULL;
static char g_sceneName[64] = "";

/* 读当前场景名（只读字符串，不做对象遍历） */
static void scene_poll(void){
    if(!g_mGetActiveScene || !g_mSceneGetName) return;
    void* e=NULL; void* sc=g_ri(g_mGetActiveScene, NULL, NULL, &e);
    if(!sc || e){ static int once=0; if(!once){ once=1; char b[96]; snprintf(b,sizeof(b),"v48: GetActiveScene sc=%p exc=%p\n", sc, e); log_app(b);} return; }
    void* nm=g_ri(g_mSceneGetName, sc, NULL, &e);
    if(!nm){ void* e2=NULL; nm=g_ri(g_mSceneGetName, (char*)sc+0x10, NULL, &e2);   /* 值类型 this 未装箱试探 */
        static int o3=0; if(!o3){ o3=1; char b[96]; snprintf(b,sizeof(b),"v48: get_name retry nm=%p exc=%p\n", nm, e2); log_app(b);} }
    if(!nm || e){ static int once2=0; if(!once2){ once2=1; char b[96]; snprintf(b,sizeof(b),"v48: Scene.get_name nm=%p exc=%p sc=%p\n", nm, e, sc); log_app(b);} return; }
    char tmp[64]=""; u16_to_ascii(nm, tmp, sizeof(tmp));
    if(strcmp(tmp, g_sceneName)!=0){
        strncpy(g_sceneName, tmp, 63); g_sceneName[63]=0;
        char b[120]; snprintf(b,sizeof(b),"v48: scene='%s'\n", g_sceneName); log_app(b);
        if(!strstr(g_sceneName, "World_Mobile")){ g_stUploaded = 0; mp_reset_structure(); }  /* v73: 离开世界就作废旧结构 */
    }
}

static int in_menu_scene(void){
    /* SFS 安卓场景：Home_Mobile(主菜单) / Hub_Mobile / Build_Mobile / World_Mobile(飞行) */
    return (strstr(g_sceneName, "Home_Mobile") != NULL) || (strstr(g_sceneName, "Hub_Mobile") != NULL);
}
static void* g_mGetPosition=NULL;
static void* g_tiInst=NULL, *g_toastCls=NULL, *g_toastField=NULL, *g_mToast=NULL;
static char  g_tiVal[3][96];

static void toast(const char* msg){
    void* e=NULL; void* inst=NULL; void* a[1];
    if(!g_toastField || !g_mToast || !g_staticGet || !g_sNew) return;
    g_staticGet(g_toastField, &inst);
    if(!inst) return;
    a[0]=g_sNew(msg);
    g_ri(g_mToast, inst, a, &e);
}

static int g_focus_tries = 0;
static void focus_first_field(void){
    if(!g_tiInst || g_focus_tries >= 4) return;
    g_focus_tries++;
    void* arr = *(void**)((char*)g_tiInst + 0x60);
    if(!arr) return;
    int n = *(int*)((char*)arr + 0x18);
    if(n <= 0) return;
    void* el = ((void**)((char*)arr+0x20))[0];
    void* box = el ? *(void**)((char*)el + 0x30) : NULL;
    void* tmp = box ? *(void**)((char*)box + 0x28) : NULL;
    void* e=NULL;
    if(box && g_mBoxOpen) g_ri(g_mBoxOpen, box, NULL, &e);
    if(tmp && g_evtSysCls && g_mSetSel && g_mCurEvtSys){
        void* es=g_ri(g_mCurEvtSys, NULL, NULL, &e);
        void* tgo=g_mGetGO?g_ri(g_mGetGO, tmp, NULL, &e):NULL;
        if(es && tgo){ void* pp[1]={tgo}; g_ri(g_mSetSel, es, pp, &e); }
    }
    if(tmp && g_mActivateInput) g_ri(g_mActivateInput, tmp, NULL, &e);
    char fb[96]; snprintf(fb,sizeof(fb),"v63: focus try %d exc=%p\n", g_focus_tries, e); log_app(fb);
}

static void ti_poll(void){
    if(!g_tiInst) return;
    focus_first_field();
    void* arr = *(void**)((char*)g_tiInst + 0x60);
    if(!arr) return;
    int n = *(int*)((char*)arr + 0x18);
    for(int i=0;i<n && i<3;i++){
        void* el = ((void**)((char*)arr+0x20))[i];
        void* box = el ? *(void**)((char*)el + 0x30) : NULL;
        void* tmp = box ? *(void**)((char*)box + 0x28) : NULL;
        void* str = tmp ? *(void**)((char*)tmp + 0x210) : NULL;
        char t[96]=""; if(str) u16_to_ascii(str, t, sizeof(t));
        if(strcmp(t, g_tiVal[i]) != 0){
            strncpy(g_tiVal[i], t, 95); g_tiVal[i][95]=0;
            char b[160]; snprintf(b,sizeof(b),"v45: input[%d]='%s'\n", i, t); log_app(b);
            if(i==0 && t[0]){
                char ep[120]; snprintf(ep,sizeof(ep),"%s",t);
                if(!strchr(ep,':')){ size_t L=strlen(ep); snprintf(ep+L, sizeof(ep)-L, ":9806"); }
                char tb[160]; snprintf(tb,sizeof(tb),"服务器: %s   客户端 V1.4 (handshake 2)   Ping: --   Players: --", ep);
                toast(tb);
            }
        }
    }
}
static void* g_tiCls=NULL, *g_elemCls=NULL, *g_mTiOpen=NULL, *g_mElement2=NULL, *g_tiField=NULL;
/* ---- v65: dialog callback (fake Action<string[]>) + the game's own mobile keyboard ---- */
static void* g_tiActCls=NULL, *g_mTiActInvoke=NULL, *g_boxClsKB=NULL, *g_mKBOpen=NULL;
/* v83/v97: EnterWorld(string, Action) 的第 2 参必须是真委托，传 NULL 会在 SetResult() 抛 NRE。
 * v97 起这个回调还必须「切场景」—— 对应 WorldsMenu.OpenWorld() 里的
 *   delegate { Base.sceneLoader.LoadWorldScene(); }
 * 时机：LoadSolarSystem 完成 → insideWorld=true → 这里被调 → 我们立刻调 LoadWorldScene(false)。
 * 注意延迟：不要在回调里同步嵌套调场景加载（v64 的死法），先记 flag，下一帧 hook_update 里做。 */
static void* g_mActionInvoke=NULL;   /* v83: System.Action::Invoke 的 MethodInfo */
static volatile int g_enterCbHits = 0;
static volatile int g_wantLoadWorld = 0;   /* v97: 回调里置 1，hook_update 下一帧执行 LoadWorldScene */
static volatile int g_loadWorldTries = 0;
__attribute__((noinline, used)) static void enter_done_cb(void* self, void* mth){
    (void)self; (void)mth;
    int n = __sync_add_and_fetch(&g_enterCbHits, 1);
    g_wantLoadWorld = 1;
    char b[150]; snprintf(b,sizeof(b),"v97: *** EnterWorld callback #%d -> LoadWorldScene queued ***\n", n); log_app(b);
}
static void* make_enter_action(void){
    int oMp = (g_doffMethodPtr >= 0) ? g_doffMethodPtr : 0x10;
    int oIi = (g_doffInvokeImpl >= 0) ? g_doffInvokeImpl : 0x18;
    int oMe = (g_doffMethod >= 0) ? g_doffMethod : 0x28;
    void* del; char b[170];
    if (!g_mNewObj || !g_actionCls) { log_app("v83: no System.Action class\n"); return NULL; }
    del = g_mNewObj(g_actionCls);
    if (!del) { log_app("v83: object_new(Action) FAILED\n"); return NULL; }
    *(void**)((char*)del + oMp) = (void*)enter_done_cb;
    *(void**)((char*)del + oIi) = (void*)enter_done_cb;
    *(void**)((char*)del + oMe) = g_mActionInvoke;
    snprintf(b,sizeof(b),"v83: enter action=%p mp@0x%x ii@0x%x method@0x%x=%p\n",
             del, (unsigned)oMp, (unsigned)oIi, (unsigned)oMe, g_mActionInvoke);
    log_app(b);
    return del;
}
static volatile int g_dialogOpen=0, g_clickCool=0;   /* v66: 弹窗打开时禁止再判定按钮点击（否则点一下输入法就把页面重开、内容被清空） */
/* ---- v67: 延迟进世界 ---------------------------------------------------------------
 * 实测：在 Confirm() 的 Action 回调里直接 runtime_invoke(LoadWorldScene)，场景确实切到 World_Mobile，
 * 但 Confirm() 自己「关弹窗 / 恢复时间流动」的收尾代码被它抛在身后，结果世界是冻结的
 * （HUD 数字两秒不动、左上角菜单点不动）。所以回调里只记下地址并返回，等游戏把手尾做完再进世界。 */
static volatile int g_pendingEnter=0;
static char g_pendingEp[96]="";
static void* g_mTimeScaleSet=NULL, *g_mTimeScaleGet=NULL;
static void force_timescale1(const char* why){
    void* exc=NULL; char b[128];
    if (g_mTimeScaleGet){ void* r=g_ri(g_mTimeScaleGet,NULL,NULL,&exc);
        if(r && !exc){ float* f=(float*)g_unbox(r); snprintf(b,sizeof(b),"v67: timeScale=%.2f (%s)\n", f[0], why); log_app(b); } }
    if (g_mTimeScaleSet){ void* exc2=NULL; float v=1.0f; void* p[1]; p[0]=&v;
        g_ri(g_mTimeScaleSet,NULL,p,&exc2);
        snprintf(b,sizeof(b),"v67: set timeScale=1 exc=%p (%s)\n", exc2, why); log_app(b); }
}
static void connect_and_enter_world(void);
static void pin_view_to_launchpad(void);
static void* make_ti_action(void);
static void* (*g_arrayNew)(void*, uintptr_t)=NULL;

/* ================= v126: 上行变量字典 =================
 * 零件尺寸/开关/文本都住在 PartSave 的三个 Dictionary 里（PartSave.cs:28-30）。
 * PC 端建零件时走 PartsLoader.cs:137 doubleVariables.LoadDictionary(partSave.NUMBER_VARIABLES, ...)，
 * 字典为空 = 全用默认值（所以尺寸永远停在 1）。这里把字典枚举出来，交给 mp_net.c 编码上行。
 * 枚举方式：Dictionary.get_Count → get_Keys → KeyCollection.CopyTo(string[], 0) → get_Item(key)。
 */
static void* up_str_cls(void){
    static void* sCls = NULL;
    if(!sCls && g_sNew && g_mObjGetClass){ void* s = g_sNew("x"); if(s) sCls = g_mObjGetClass(s); }
    return sCls;
}

static int up_dic_keys(void* d, void** outArr, int* outN){
    void* e=NULL; void* cls; void* mCount; void* rc; int cnt;
    *outArr=NULL; *outN=0;
    if(!d || !g_ri || !g_gm || !g_arrayNew || !g_mObjGetClass) return 0;
    cls = g_mObjGetClass(d);
    if(!cls) return 0;
    mCount = g_gm(cls, "get_Count", 0);
    if(!mCount) return 0;
    rc = g_ri(mCount, d, NULL, &e);
    if(!rc || e) return 0;
    cnt = *(int*)g_unbox(rc);
    if(cnt <= 0) return 0;
    { void* sCls = up_str_cls(); void* arr; void* mk; void* kc; void* kcCls; void* mCopy;
      int32_t zero = 0; void* a[2];
      if(!sCls) return 0;
      arr = g_arrayNew(sCls, (uintptr_t)cnt);
      if(!arr) return 0;
      mk = g_gm(cls, "get_Keys", 0);
      if(!mk) return 0;
      e=NULL; kc = g_ri(mk, d, NULL, &e);
      if(!kc || e) return 0;
      kcCls = g_mObjGetClass(kc);
      mCopy = kcCls ? g_gm(kcCls, "CopyTo", 2) : NULL;
      if(!mCopy) return 0;
      a[0]=arr; a[1]=&zero;
      e=NULL; g_ri(mCopy, kc, a, &e);
      if(e) return 0;
      *outArr = arr; *outN = cnt; }
    return 1;
}

static void up_fill_vars(v73_part_rec* r, void* pe){
    void* arr=NULL; int n=0, i;
    if(!r || !pe) return;
    r->nnv = r->ntv = r->nxv = 0;
    /* NUMBER_VARIABLES @0x30 */
    if(up_dic_keys(*(void**)((char*)pe + 0x30), &arr, &n)){
        void* d = *(void**)((char*)pe + 0x30);
        void* cls = g_mObjGetClass(d);
        void* mGet = cls ? g_gm(cls, "get_Item", 1) : NULL;
        if(mGet){
            int cap = (n > MP_UP_PART_NV) ? MP_UP_PART_NV : n;
            for(i=0;i<cap;i++){
                void* key = ((void**)((char*)arr + 0x20))[i];
                void* a1[1]; void* e=NULL; void* rv;
                a1[0]=key;
                rv = g_ri(mGet, d, a1, &e);
                if(!rv || e) continue;
                u16_to_ascii(key, r->nv[r->nnv].name, (int)sizeof(r->nv[0].name));
                r->nv[r->nnv].v = *(double*)g_unbox(rv);
                r->nnv++;
            }
        }
    }
    /* TOGGLE_VARIABLES @0x38 */
    if(up_dic_keys(*(void**)((char*)pe + 0x38), &arr, &n)){
        void* d = *(void**)((char*)pe + 0x38);
        void* cls = g_mObjGetClass(d);
        void* mGet = cls ? g_gm(cls, "get_Item", 1) : NULL;
        if(mGet){
            int cap = (n > MP_UP_PART_TV) ? MP_UP_PART_TV : n;
            for(i=0;i<cap;i++){
                void* key = ((void**)((char*)arr + 0x20))[i];
                void* a1[1]; void* e=NULL; void* rv;
                a1[0]=key;
                rv = g_ri(mGet, d, a1, &e);
                if(!rv || e) continue;
                u16_to_ascii(key, r->tv[r->ntv].name, (int)sizeof(r->tv[0].name));
                r->tv[r->ntv].v = (*(unsigned char*)g_unbox(rv)) ? 1 : 0;
                r->ntv++;
            }
        }
    }
    /* TEXT_VARIABLES @0x40 */
    if(up_dic_keys(*(void**)((char*)pe + 0x40), &arr, &n)){
        void* d = *(void**)((char*)pe + 0x40);
        void* cls = g_mObjGetClass(d);
        void* mGet = cls ? g_gm(cls, "get_Item", 1) : NULL;
        if(mGet){
            int cap = (n > MP_UP_PART_XV) ? MP_UP_PART_XV : n;
            for(i=0;i<cap;i++){
                void* key = ((void**)((char*)arr + 0x20))[i];
                void* a1[1]; void* e=NULL; void* rv;
                a1[0]=key;
                rv = g_ri(mGet, d, a1, &e);
                if(!rv || e) continue;
                u16_to_ascii(key, r->xv[r->nxv].name, (int)sizeof(r->xv[0].name));
                u16_to_ascii(rv, r->xv[r->nxv].val, (int)sizeof(r->xv[0].val));
                r->nxv++;
            }
        }
    }
}

static void stub_ti_cb(void* a, void* b, void* c, void* d){
    char lb[200];
    snprintf(lb,sizeof(lb),"v44: TI cb a=%p b=%p c=%p d=%p\n", a,b,c,d); log_app(lb);
}

/* ---------- v46: M0 spike — read rocket pose in world ---------- */
static void m0_spike(void){
    char b[200]; void* e=NULL; void* arr; void* p[1];
    if(!g_rockCls || !g_rockType || !g_mFindAll){ log_app("m0: rocket symbols missing\n"); return; }
    p[0]=g_rockType;
    arr = g_findMethod ? g_ri(g_findMethod, NULL, p, &e) : NULL;   /* 仅场景激活对象 */
    if(!arr || e){ e=NULL; arr=g_ri(g_mFindAll, NULL, p, &e); }
    int n=(arr && !e) ? (int)g_alg(arr) : -1;
    snprintf(b,sizeof(b),"m0: rockets=%d exc=%p\n", n, e); log_app(b);
    if(n<=0) return;
    for(int i=0;i<n && i<4;i++){
        void* r=((void**)((char*)arr+0x20))[i];
        if(!r) continue;
        void* go2=g_ri(g_mGetGO, r, NULL, &e);
        char nm[64]=""; if(go2) obj_name(go2, nm, sizeof(nm));
        void* tr = go2 && g_mGoTransform ? g_ri(g_mGoTransform, go2, NULL, &e) : NULL;
        float x=0,y=0,z=0;
        if(tr && g_mGetPosition){
            void* v=g_ri(g_mGetPosition, tr, NULL, &e);
            if(v && !e){ float* f=(float*)((char*)v+0x10); x=f[0]; y=f[1]; z=f[2]; }
        }
        snprintf(b,sizeof(b),"m0: [%d] '%s' go=%p tr=%p pos=(%.1f, %.1f, %.1f)\n", i, nm, go2, tr, x, y, z); log_app(b);
    }
}

static void native_text_input(void){
    char b[240]; void* e=NULL; void* inst=NULL; void* arr=NULL; void* del=NULL; void* k=NULL;
    int mode = 1;
    if(!g_tiCls || !g_mTiOpen || !g_mElement2 || !g_elemCls || !g_arrayNew){ log_app("v44: TI plumbing missing\n"); return; }
    if(g_tiField && g_staticGet) g_staticGet(g_tiField, &inst);
    snprintf(b,sizeof(b),"v44: textInput inst=%p\n", inst); log_app(b);
    if(!inst){ log_app("v44: textInput instance NULL\n"); return; }
    arr = g_arrayNew(g_elemCls, 3);
    if(!arr){ log_app("v44: element array failed\n"); return; }
    const char* names[3] = {"服务器 (IP:端口 或域名)","用户名","密码 (可留空)"};
    const char* hints[3] = {"","",""};   /* 第二参数是初始值, 必须留空 */
    for(int i=0;i<3;i++){
        void* a1[2]; void* r; void* el=NULL;
        a1[0]=g_sNew?g_sNew(names[i]):NULL; a1[1]=g_sNew?g_sNew(hints[i]):NULL;
        r=g_ri(g_mElement2, NULL, a1, &e);
        if(r && !e) ((void**)((char*)arr+0x20))[i]=r;
        snprintf(b,sizeof(b),"v44: elem[%d]=%p exc=%p\n", i, r, e); log_app(b); e=NULL;
        if(r && i==2 && g_mSetContentType){    /* Password: contentType=7 -> 圆点遮蔽 */
            void* box = *(void**)((char*)r + 0x30);        /* TUXXAURTZSIFPXTZ */
            void* tmp = box ? *(void**)((char*)box + 0x28) : NULL;  /* TMP_InputField */
            if(tmp){ int32_t v=7; void* p1[1]={&v}; void* e2=NULL;
                g_ri(g_mSetContentType, tmp, p1, &e2);
                snprintf(b,sizeof(b),"v44: pwd contentType=7 tmp=%p exc=%p\n", tmp, e2); log_app(b); }
            else log_app("v44: pwd tmp NULL (wrapper not resolved)\n");
        }
    }
    { void* t2 = g_mgp2 ? g_mgp2(g_mTiOpen,2) : NULL;
      k = (t2 && g_cfit) ? g_cfit(t2) : NULL; }
    if (k && !g_tiActCls) { g_tiActCls = k;
        g_mTiActInvoke = g_gm ? g_gm(k,"Invoke",1) : NULL;
        snprintf(b,sizeof(b),"v65: Action<string[]> cls=%p InvokeMI=%p\n", k, g_mTiActInvoke); log_app(b); }
    del = make_ti_action();   /* v65: 真回调 → 点「加入服务器」能收到 3 行输入 */
    snprintf(b,sizeof(b),"v44: cbArgsCls=%p del=%p\n", k, del); log_app(b);
    { void* p5[5];
      p5[0]=g_sNew?g_sNew("返回"):NULL;
      p5[1]=g_sNew?g_sNew("加入服务器"):NULL;
      p5[2]=del; p5[3]=&mode; p5[4]=arr;
      void* r=g_ri(g_mTiOpen, inst, p5, &e);
      snprintf(b,sizeof(b),"v44: TI Open ret=%p exc=%p\n", r, e); log_app(b); }
    g_tiInst = inst;
    if(g_mActivateInput){     /* 主动给第一行焦点，否则键盘弹了字符进不去 */
        void* arr0 = *(void**)((char*)inst + 0x60);
        if(arr0){ int n0=*(int*)((char*)arr0+0x18); if(n0>0){
            void* el0=((void**)((char*)arr0+0x20))[0];
            void* box=el0?*(void**)((char*)el0+0x30):NULL;
            void* tmp=box?*(void**)((char*)box+0x28):NULL;
            if(g_mBoxOpen && box){ void* eB=NULL; g_ri(g_mBoxOpen, box, NULL, &eB);
                snprintf(b,sizeof(b),"v55: box.Open exc=%p\n", eB); log_app(b); }
            void* eJ=NULL;
            if(tmp && g_mSetSel && g_mCurEvtSys){
                void* es = g_ri(g_mCurEvtSys, NULL, NULL, &eJ);
                void* tgo = g_mGetGO ? g_ri(g_mGetGO, tmp, NULL, &eJ) : NULL;
                if(es && tgo){ void* pp[1]={tgo}; g_ri(g_mSetSel, es, pp, &eJ);
                    char sb[96]; snprintf(sb,sizeof(sb),"v62: SetSelectedGameObject exc=%p\n", eJ); log_app(sb); }
            }
            if(tmp && g_mActivateInput){ void* eA=NULL; g_ri(g_mActivateInput, tmp, NULL, &eA);
                snprintf(b,sizeof(b),"v62: ActivateInputField exc=%p\n", eA); log_app(b); }
        } }
    }
    toast("服务器未连接   客户端 V1.4 (handshake 2)   Ping: --   Players: --");
    /* v117: 把弹窗里所有按钮的屏幕坐标打出来。
     * 这个弹窗是 Unity 自己画的，uiautomator 抓不到（根节点只有 unitySurfaceView），
     * 所以只能从 il2cpp 侧拿 RectTransform + WorldToScreenPoint 自己算，
     * 否则自动/手动点击都只能靠猜坐标。 */
    if(g_typeBtn && g_findMethod && g_mW2S && g_mCamMain && g_v3Cls && g_mNewObj && g_mGetGO && g_mGoTransform && g_mGetPos2){
        void* params[1]; params[0]=g_typeBtn; void* eB=NULL;
        void* barr = g_ri(g_findMethod, NULL, params, &eB);
        if(barr && !eB){
            void* cam = g_ri(g_mCamMain, NULL, NULL, &eB);
            int nb = (int)g_alg(barr);
            char gb[200];
            snprintf(gb,sizeof(gb),"v117: dialog buttons=%d\n", nb); log_app(gb);
            for(int i=0;i<nb && i<24;i++){
                void* be = ((void**)((char*)barr+0x20))[i];
                if(!be) continue;
                /* v121: 用组件自己的 transform.position 而不是 GameObject.transform。
                 * v117 用 g_mGetGO -> g_mGoTransform 拿到的永远是同一个（大概是画布根）位置，
                 * 所以 6 个按钮全打成 (540,1188)。组件自身 transform 才是按钮的真实位置。 */
                void* tr2 = NULL; void* eb2=NULL;
                if(g_mGetTransform && g_mObjGetClass && g_gm){
                    void* bcls = g_mObjGetClass(be);
                    void* mt = bcls ? g_gm(bcls,"get_transform",0) : NULL;
                    if(mt){ void* q=NULL; tr2 = g_ri(mt, be, NULL, &q); if(q) tr2=NULL; }
                }
                if(!tr2) continue;
                void* wp = g_ri(g_mGetPos2, tr2, NULL, &eb2);
                if(!wp || eb2) continue;
                /* v121: 屏幕尺寸取真实分辨率（2283），不是固定假设 */
                float sw = 1080.0f, sh = 2283.0f;
                void* v3 = g_mNewObj(g_v3Cls);
                if(!v3) continue;
                *(float*)((char*)v3+0x10) = *(float*)((char*)wp+0x10);
                *(float*)((char*)v3+0x14) = *(float*)((char*)wp+0x14);
                *(float*)((char*)v3+0x18) = *(float*)((char*)wp+0x18);
                if(!cam) continue;
                void* pa[2]; pa[0]=cam; pa[1]=v3; void* e3=NULL;
                void* sp = g_ri(g_mW2S, NULL, pa, &e3);
                if(!sp || e3) continue;
                float sx = *(float*)((char*)sp+0x10);
                float sy = *(float*)((char*)sp+0x14);
                /* v123: find_text_component / obj_name 的入参必须是 GameObject，不是组件！
                 * v121 我在这里直接传了 Button 组件 be 进去，内部按 GameObject 解引用 ⇒
                 * SIGSEGV（tombstone_02: find_text_component+144 ← hook_update+28568）。
                 * 用户看到的是"IP 页面没了"，其实是进程在弹窗刚打开时崩了。 */
                void* bgo = NULL; { void* eb9=NULL; if(g_mGetGO) bgo = g_ri(g_mGetGO, be, NULL, &eb9); if(eb9) bgo=NULL; }
                char nmb[48]=""; if(bgo) obj_name(bgo, nmb, sizeof(nmb));
                char tb[64]="";
                { void* tc = bgo ? find_text_component(bgo) : NULL;
                  if(tc && g_mGetTxt){ void* e4=NULL; void* ss=g_ri(g_mGetTxt, tc, NULL, &e4);
                      if(ss && !e4) u16_to_ascii(ss, tb, sizeof(tb)); } }
                snprintf(gb,sizeof(gb),"v121: dlgbtn[%d] '%s' text='%s' screen=(%.0f,%.0f) tap=(%.0f,%.0f)\n",
                         i, nmb, tb, sx, sy, sx, sh - sy);
                log_app(gb);
            }
        } else log_app("v117: dialog button scan failed\n");
    }
}

static void native_login_window(void){
    char b[240]; void* e=NULL; void* root; void* go; void* parent; void* clone; void* ctl;
    if(g_loginWin){ if(g_mSetActive2){ int32_t t=1; void* p[1]={&t}; g_ri(g_mSetActive2, g_loginWin, p, &e); }
                    log_app("v44: popup re-shown\n"); return; }
    if(!g_mInst3 || !g_mGetGO || !g_mSetActive2){ log_app("v44: plumbing missing\n"); return; }
    root = tr_find_named("- Update Popup -");
    snprintf(b,sizeof(b),"v44: popup root=%p\n", root); log_app(b);
    if(!root){ log_app("v44: popup NOT found\n"); return; }
    log_tree(root, "PopupRoot");
    go = g_ri(g_mGetGO, root, NULL, &e);
    parent = g_ri(g_mGetParent, root, NULL, &e);
    if(!go || !parent){ log_app("v44: popup go/parent missing\n"); return; }
    { void* ip[3]; int32_t w=0; ip[0]=go; ip[1]=parent; ip[2]=&w;
      clone = g_ri(g_mInst3, NULL, ip, &e); }
    snprintf(b,sizeof(b),"v44: popup clone=%p exc=%p\n", clone, e); log_app(b);
    if(!clone) return;
    { int32_t t=1; void* p[1]={&t}; g_ri(g_mSetActive2, clone, p, &e);
      snprintf(b,sizeof(b),"v44: SetActive exc=%p\n", e); log_app(b); }
    if(g_popType && g_mGetComponent){
        void* tp[1]; tp[0]=g_popType;
        ctl = g_ri(g_mGetComponent, clone, tp, &e);
        snprintf(b,sizeof(b),"v44: ctl=%p exc=%p (OnOpen skipped)\n", ctl, e); log_app(b);
        if(ctl && g_mSetEnabled){ int32_t z=0; void* p1[1]={&z}; void* e9=NULL;
            g_ri(g_mSetEnabled, ctl, p1, &e9);
            snprintf(b,sizeof(b),"v44: ctl disabled exc=%p\n", e9); log_app(b); }
    }
    { void* tr2 = g_mGoTransform ? g_ri(g_mGoTransform, clone, NULL, &e) : NULL;
      if(tr2){ int32_t z=0; void* p1[1]={&z}; void* c1=g_ri(g_mGetChild, tr2, p1, &e);
        if(c1) log_tree(c1, "PopupHolder"); } }
    g_loginWin = clone;
    log_app("v44: *** POPUP CLONE SHOWN ***\n");
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

/* v91: 改克隆按钮上的文字，用来告诉玩家「点下去发生了事」。
   不碰场景、不碰世界、不碰游戏原生事件，只改我们自己克隆出来的那个 TMP 文本。 */
static void clone_label(const char* utf8)
{
    void* s = g_sNew ? g_sNew(utf8) : NULL;
    if (!g_cloneText || !g_mSetTxt || !s) return;
    { void* p[1]; p[0] = s; void* e = NULL; g_ri(g_mSetTxt, g_cloneText, p, &e); }
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
            if (!src && bpar) { src = btn; g_srcBtn = btn; }
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
    if (ctr && g_mGetParent && g_mForceRebuild) {
        void* par = g_ri(g_mGetParent, ctr, NULL, &exc);
        if (par) { void* p2[1]; p2[0]=par; g_ri(g_mForceRebuild, NULL, p2, &exc); log_app("v52: layout rebuild\n"); }
    }
    { void* e13=NULL; void* wp=g_mGetPos2 ? g_ri(g_mGetPos2, ctr, NULL, &e13) : NULL;
      if (wp && !e13) { float* f=(float*)((char*)wp+0x10);
        g_clonePos[0]=f[0]; g_clonePos[1]=f[1]; g_clonePos[2]=f[2]; g_clonePosOk=1;
        char pb[140]; snprintf(pb,sizeof(pb),"v52: clone pos=(%.0f,%.0f,%.0f)\n", f[0],f[1],f[2]); log_app(pb); } }
    { void* tp[1]; tp[0]=g_typeBtn; cbtn = (g_mGetComponent && g_typeBtn) ? g_ri(g_mGetComponent, clone, tp, &exc) : NULL; }
    if (cbtn && g_mSetEnabled) {
        /* v94: 必须禁用克隆按钮的 Button 组件。
           Instantiate 复制来的 m_OnClick 就是游戏「开始游戏」的处理器，一旦启用，点我们的按钮
           就等于点开始游戏 -> 直接跳世界列表/新世界（v90 事故）。点击一律由 poll_click() 命中测试处理。 */
        int32_t z0=0; void* p0[1]={&z0}; void* e9=NULL;
        g_ri(g_mSetEnabled, cbtn, p0, &e9);
        snprintf(b,sizeof(b),"v94: clone button DISABLED (native onClick blocked) exc=%p\n", e9); log_app(b);
    }
    if (g_trCls && g_trType && g_mGetComponent && g_mSetEnabled) {
        void* tp[1]; void* e2 = NULL; void* tc;
        tp[0] = g_trType;
        tc = g_ri(g_mGetComponent, clone, tp, &e2);
        if (tc && !e2) { void* p[1]; int32_t z = 0; p[0]=&z; g_ri(g_mSetEnabled, tc, p, &e2); log_app("v32: translation disabled\n"); }
    }
    ct = find_text_component(clone);
    g_cloneText = ct;   /* v91 */
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
    /* v132: anchors / pivot 必须一起抄。只改 sizeDelta 的话，VerticalLayoutGroup
       会按源锚点重排，把宽度又算回去 —— 这就是「按钮长度怎么改都不对」的根因。 */
    if (ctr && srcTr) {
        if (g_mAnchorMinGet && g_mAnchorMinSet) {
            void* r = g_ri(g_mAnchorMinGet, srcTr, NULL, &exc);
            if (r && !exc) { float* v = (float*)g_unbox(r); float a[2] = { v[0], v[1] };
                void* p[1]; p[0] = a; g_ri(g_mAnchorMinSet, ctr, p, &exc);
                snprintf(b,sizeof(b),"v132: anchorMin=%.2f,%.2f\n", a[0], a[1]); log_app(b); }
        }
        if (g_mAnchorMaxGet && g_mAnchorMaxSet) {
            void* r = g_ri(g_mAnchorMaxGet, srcTr, NULL, &exc);
            if (r && !exc) { float* v = (float*)g_unbox(r); float a[2] = { v[0], v[1] };
                void* p[1]; p[0] = a; g_ri(g_mAnchorMaxSet, ctr, p, &exc);
                snprintf(b,sizeof(b),"v132: anchorMax=%.2f,%.2f\n", a[0], a[1]); log_app(b); }
        }
        if (g_mPivotGet && g_mPivotSet) {
            void* r = g_ri(g_mPivotGet, srcTr, NULL, &exc);
            if (r && !exc) { float* v = (float*)g_unbox(r); float a[2] = { v[0], v[1] };
                void* p[1]; p[0] = a; g_ri(g_mPivotSet, ctr, p, &exc);
                snprintf(b,sizeof(b),"v132: pivot=%.2f,%.2f\n", a[0], a[1]); log_app(b); }
        }
    }
    /* v132: 布局组真正用的是 LayoutElement.preferredWidth，不是 sizeDelta */
    if (g_leCls && g_mGetComponent && g_typeLE && g_mLeGetPrefW && g_mLeSetPrefW) {
        void* tp[1]; void* e = NULL;
        tp[0] = g_typeLE;
        void* leSrc = g_ri(g_mGetComponent, srcGo, tp, &e);
        void* leCln = (e || !leSrc) ? NULL : g_ri(g_mGetComponent, clone, tp, &e);
        if (leSrc && !leCln && g_mAddComponent) { e = NULL; leCln = g_ri(g_mAddComponent, clone, tp, &e); }
        if (leSrc && leCln) {
            float pw = 0.0f, ph = 0.0f;
            { void* r = g_ri(g_mLeGetPrefW, leSrc, NULL, &e); if (r && !e) pw = *(float*)g_unbox(r); }
            if (g_mLeGetPrefH && g_mLeSetPrefH) { void* r = g_ri(g_mLeGetPrefH, leSrc, NULL, &e); if (r && !e) ph = *(float*)g_unbox(r); }
            { float v = pw; void* p[1]; p[0]=&v; g_ri(g_mLeSetPrefW, leCln, p, &e); }
            if (g_mLeSetPrefH) { float v = ph; void* p[1]; p[0]=&v; g_ri(g_mLeSetPrefH, leCln, p, &e); }
            snprintf(b,sizeof(b),"v132: LayoutElement prefW=%.1f prefH=%.1f copied\n", pw, ph); log_app(b);
        } else {
            snprintf(b,sizeof(b),"v132: LayoutElement src=%p clone=%p (skip)\n", leSrc, leCln); log_app(b);
        }
    }
    if (g_mForceRebuild && parent) { void* p[1]; p[0]=parent; g_ri(g_mForceRebuild, NULL, p, &exc); log_app("v35: layout rebuilt\n"); }
    /* v132: rebuild 之后再量一次，宽度应当与源按钮一致（差值≈0 才算修好） */
    if (g_mGetRect && srcTr && ctr) {
        void* e20 = NULL;
        void* r1 = g_ri(g_mGetRect, srcTr, NULL, &e20);
        void* r2 = g_ri(g_mGetRect, ctr, NULL, &e20);
        if (r1 && r2 && !e20) { float* a=(float*)g_unbox(r1); float* c=(float*)g_unbox(r2);
            snprintf(b,sizeof(b),"v132: WIDTH src=%.1f clone=%.1f delta=%.1f\n", a[2], c[2], c[2]-a[2]); log_app(b); }
    }
    if (g_mmCls && g_mmMainField && g_staticGet) { void* v=NULL; g_staticGet(g_mmMainField, &v); g_mmInst=v; }
    { char mb[120]; snprintf(mb,sizeof(mb),"v33: mainmenu inst=%p openCredits=%p\n",g_mmInst,g_mOpenCredits); log_app(mb); }
    g_cloneGo = clone; g_cloneBtn = cbtn; g_cloneRect = ctr;
    log_app("v32: *** MAIN MENU BUTTON READY ***\n");
}

/* ================= v37: login page (Android overlay) ================= */
static jobject g_login_page = NULL, g_login_back = NULL, g_login_join = NULL, g_login_dialog = NULL;
static jobject g_login_stmt_title = NULL, g_login_scroll = NULL, g_login_status = NULL;
static int g_stmt_accepted = 0;

static const char* STMT_TEXT =
"STCH Studio Multiplayer Mod and Relay Server Statement\n\nUpdated: August 16, 2026\n\n"
"STCH Studio, independently established and operated by the developer known as maozhongmao / yangchengtong (\"STCH Studio\" or \"the Developer\"), hereby states:\n\n"
"This multiplayer mod and relay server (the \"Service\") are independently developed by the Developer as a non-official public-interest tool intended to support multiplayer play for the Spaceflight Simulator (SFS) community. Please read and comply with this Statement before use.\n\n"
"1. Non-Official Status\n\nThe Service is independently developed by STCH Studio. It has no affiliation, authorization, partnership or endorsement relationship with SFS, its official team, or its rights holders.\n\n"
"2. Legitimate Game Ownership\n\nBefore using the Service, you must have lawfully purchased and own a legitimate copy of SFS. The Service does not provide or bundle any game files.\n\n"
"3. Free and Non-Commercial Use\n\nThe Service is permanently free. No person or organization may charge fees or conduct commercial activity through the Service.\n\n"
"4. Technical Scope\n\nThe Service only relays real-time player spacecraft synchronization data. It does not reverse engineer or modify the core game program.\n\n"
"5. User Responsibility\n\nEach user is solely responsible for all content and interactions created during multiplayer sessions.\n\n"
"6. Service Availability Disclaimer\n\nThe Service is provided \"as is\". Availability and security are not guaranteed.\n\n"
"7. Data Collection and Privacy\n\nOnly player game IDs, IP addresses and spacecraft synchronization data are recorded.\n\n"
"8. Prohibited Conduct\n\nUsers must not distribute malicious code, attack the server, impersonate others, or commit fraud.\n\n"
"9. Changes and Termination\n\nThe Developer may update, suspend or terminate the Service at any time.\n\n"
"10. Governing Law and Disputes\n\nDisputes should first be resolved through friendly negotiation.\n\n"
"11. Support and Feedback\n\nQQ Group 2: 679991439\nEmail: maozhongmao@qq.com / yangchengtong@stch.de5.net\n\n"
"12. Open-Source Notice\n\nMIT License. Source: https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer\n\n"
"STCH Studio (Developer: maozhongmao / yangchengtong)\n";

static float g_density = 3.0f;
static int dpi(int v){ return (int)((float)v * g_density + 0.5f); }

static jobject v37_rounded(JNIEnv* e, jint color, float radius_dp)
{
    jclass c = (*e)->FindClass(e, "android/graphics/drawable/GradientDrawable");
    jobject d;
    jmethodID ctor, sc, sr;
    if (!c) return NULL;
    ctor = (*e)->GetMethodID(e, c, "<init>", "()V");
    d = (*e)->NewObject(e, c, ctor);
    if (!d) return NULL;
    sc = (*e)->GetMethodID(e, c, "setColor", "(I)V");
    (*e)->CallVoidMethod(e, d, sc, color);
    sr = (*e)->GetMethodID(e, c, "setCornerRadius", "(F)V");
    (*e)->CallVoidMethod(e, d, sr, (jfloat)dpi((int)radius_dp));
    return d;
}

static jobject v37_text(JNIEnv* e, const char* text, float sp, jint color, int bold)
{
    jclass c = (*e)->FindClass(e, "android/widget/TextView");
    jmethodID ctor = (*e)->GetMethodID(e, c, "<init>", "(Landroid/content/Context;)V");
    jobject v = (*e)->NewObject(e, c, ctor, g_activity);
    jmethodID setText, setSize, setColor;
    jstring js;
    if (!v) return NULL;
    setText = (*e)->GetMethodID(e, c, "setText", "(Ljava/lang/CharSequence;)V");
    js = (*e)->NewStringUTF(e, text ? text : "");
    (*e)->CallVoidMethod(e, v, setText, js);
    (*e)->DeleteLocalRef(e, js);
    setSize = (*e)->GetMethodID(e, c, "setTextSize", "(F)V");
    (*e)->CallVoidMethod(e, v, setSize, (jfloat)sp);
    setColor = (*e)->GetMethodID(e, c, "setTextColor", "(I)V");
    (*e)->CallVoidMethod(e, v, setColor, color);
    if (bold) {
        jclass ctf = (*e)->FindClass(e, "android/graphics/Typeface");
        jfieldID fb = (*e)->GetStaticFieldID(e, ctf, "DEFAULT_BOLD", "Landroid/graphics/Typeface;");
        jobject tf = (*e)->GetStaticObjectField(e, ctf, fb);
        jmethodID stf = (*e)->GetMethodID(e, c, "setTypeface", "(Landroid/graphics/Typeface;)V");
        (*e)->CallVoidMethod(e, v, stf, tf);
    }
    return v;
}

static jobject v37_edit(JNIEnv* e, const char* value, int inputType)
{
    jclass c = (*e)->FindClass(e, "android/widget/EditText");
    jmethodID ctor = (*e)->GetMethodID(e, c, "<init>", "(Landroid/content/Context;)V");
    jobject v = (*e)->NewObject(e, c, ctor, g_activity);
    jmethodID setText, setSize, setColor, setType, setSingle, setBg, setPad;
    jclass cView;
    jstring js;
    if (!v) return NULL;
    setText = (*e)->GetMethodID(e, c, "setText", "(Ljava/lang/CharSequence;)V");
    js = (*e)->NewStringUTF(e, value ? value : "");
    (*e)->CallVoidMethod(e, v, setText, js);
    (*e)->DeleteLocalRef(e, js);
    setSize = (*e)->GetMethodID(e, c, "setTextSize", "(F)V");
    (*e)->CallVoidMethod(e, v, setSize, (jfloat)16);
    setColor = (*e)->GetMethodID(e, c, "setTextColor", "(I)V");
    (*e)->CallVoidMethod(e, v, setColor, (jint)0xFFFFFFFF);
    setType = (*e)->GetMethodID(e, c, "setInputType", "(I)V");
    (*e)->CallVoidMethod(e, v, setType, (jint)inputType);
    setSingle = (*e)->GetMethodID(e, c, "setSingleLine", "(Z)V");
    (*e)->CallVoidMethod(e, v, setSingle, (jboolean)1);
    cView = (*e)->FindClass(e, "android/view/View");
    setBg = (*e)->GetMethodID(e, cView, "setBackground", "(Landroid/graphics/drawable/Drawable;)V");
    { jobject bg = v37_rounded(e, (jint)0xFF3A3E42, 8); if (bg) (*e)->CallVoidMethod(e, v, setBg, bg); }
    setPad = (*e)->GetMethodID(e, cView, "setPadding", "(IIII)V");
    (*e)->CallVoidMethod(e, v, setPad, dpi(12), dpi(8), dpi(12), dpi(8));
    return v;
}

static jobject v37_lin(JNIEnv* e, int orientation)
{
    jclass c = (*e)->FindClass(e, "android/widget/LinearLayout");
    jmethodID ctor = (*e)->GetMethodID(e, c, "<init>", "(Landroid/content/Context;)V");
    jobject v = (*e)->NewObject(e, c, ctor, g_activity);
    jmethodID so;
    if (!v) return NULL;
    so = (*e)->GetMethodID(e, c, "setOrientation", "(I)V");
    (*e)->CallVoidMethod(e, v, so, (jint)orientation);
    return v;
}

static void v37_bg(JNIEnv* e, jobject v, jint color, float r)
{
    jclass cV = (*e)->FindClass(e, "android/view/View");
    jmethodID sb = (*e)->GetMethodID(e, cV, "setBackground", "(Landroid/graphics/drawable/Drawable;)V");
    jobject d = v37_rounded(e, color, r);
    if (d) (*e)->CallVoidMethod(e, v, sb, d);
}

static void v37_pad(JNIEnv* e, jobject v, int l, int t, int r, int b)
{
    jclass cV = (*e)->FindClass(e, "android/view/View");
    jmethodID sp = (*e)->GetMethodID(e, cV, "setPadding", "(IIII)V");
    (*e)->CallVoidMethod(e, v, sp, dpi(l), dpi(t), dpi(r), dpi(b));
}

static void v37_addvw(JNIEnv* e, jobject parent, jobject child, int w, int h, int weight, int marginH, int marginV)
{
    jclass cVG = (*e)->FindClass(e, "android/view/ViewGroup");
    jmethodID mAdd = (*e)->GetMethodID(e, cVG, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
    jclass cLP = (*e)->FindClass(e, "android/widget/LinearLayout$LayoutParams");
    jmethodID ctorLP = (*e)->GetMethodID(e, cLP, "<init>", "(II)V");
    jobject lp = (*e)->NewObject(e, cLP, ctorLP, w, h);
    if (!lp) return;
    if (weight > 0) { jfieldID fw = (*e)->GetFieldID(e, cLP, "weight", "F"); (*e)->SetFloatField(e, lp, fw, (jfloat)weight); }
    if (marginH > 0 || marginV > 0) {
        jclass cMLP = (*e)->FindClass(e, "android/view/ViewGroup$MarginLayoutParams");
        jmethodID sm = (*e)->GetMethodID(e, cMLP, "setMargins", "(IIII)V");
        (*e)->CallVoidMethod(e, lp, sm, dpi(marginH), dpi(marginV), dpi(marginH), dpi(marginV));
    }
    (*e)->CallVoidMethod(e, parent, mAdd, child, lp);
}

static jobject v37_button(JNIEnv* e, const char* text, jint bgColor)
{
    jobject v = v37_text(e, text, 16, (jint)0xFFFFFFFF, 1);
    jclass cTV, cView;
    jmethodID gv, setClickable, mSetClick;
    jfieldID fInst;
    jobject listener;
    if (!v) return NULL;
    v37_bg(e, v, bgColor, 8);
    cTV = (*e)->FindClass(e, "android/widget/TextView");
    gv = (*e)->GetMethodID(e, cTV, "setGravity", "(I)V");
    (*e)->CallVoidMethod(e, v, gv, 17);
    cView = (*e)->FindClass(e, "android/view/View");
    setClickable = (*e)->GetMethodID(e, cView, "setClickable", "(Z)V");
    (*e)->CallVoidMethod(e, v, setClickable, (jboolean)1);
    fInst = (*e)->GetStaticFieldID(e, g_bridge, "INSTANCE", "Lcom/sfsmp/bridge/Bridge;");
    listener = fInst ? (*e)->GetStaticObjectField(e, g_bridge, fInst) : NULL;
    mSetClick = (*e)->GetMethodID(e, cView, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V");
    if (listener && mSetClick) (*e)->CallVoidMethod(e, v, mSetClick, listener);
    return v;
}

static void check_statement_scroll(JNIEnv* e)
{
    jobject sv = g_login_scroll;
    jclass cSV, cV;
    jmethodID getY, getChild, getH;
    jint y, ch, vh;
    jobject child;
    if (!sv || g_stmt_accepted) return;
    cSV = (*e)->GetObjectClass(e, sv);
    getY = (*e)->GetMethodID(e, cSV, "getScrollY", "()I");
    getChild = (*e)->GetMethodID(e, cSV, "getChildAt", "(I)Landroid/view/View;");
    getH = (*e)->GetMethodID(e, cSV, "getHeight", "()I");
    if (!getY || !getChild || !getH) return;
    y = (*e)->CallIntMethod(e, sv, getY);
    child = (*e)->CallObjectMethod(e, sv, getChild, 0);
    if (!child) return;
    cV = (*e)->GetObjectClass(e, child);
    { jmethodID ch2 = (*e)->GetMethodID(e, cV, "getHeight", "()I"); ch = (*e)->CallIntMethod(e, child, ch2); }
    vh = (*e)->CallIntMethod(e, sv, getH);
    if (ch - vh - y <= dpi(12)) {
        g_stmt_accepted = 1;
        if (g_login_join) {
            jclass cTV = (*e)->FindClass(e, "android/widget/TextView");
            jmethodID setText = (*e)->GetMethodID(e, cTV, "setText", "(Ljava/lang/CharSequence;)V");
            jstring js = (*e)->NewStringUTF(e, "Join Server");
            (*e)->CallVoidMethod(e, g_login_join, setText, js); (*e)->DeleteLocalRef(e, js);
        }
        if (g_login_stmt_title) {
            jclass cTV = (*e)->FindClass(e, "android/widget/TextView");
            jmethodID setText = (*e)->GetMethodID(e, cTV, "setText", "(Ljava/lang/CharSequence;)V");
            jstring js = (*e)->NewStringUTF(e, "User Statement (accepted)");
            (*e)->CallVoidMethod(e, g_login_stmt_title, setText, js); (*e)->DeleteLocalRef(e, js);
        }
        log_app("v37: statement accepted (scrolled to end)\n");
    }
}

static void page_hide_login(void)
{
    JNIEnv* env = NULL;
    if (!g_vm || !g_decor || !g_login_page) return;
    if ((*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK || !env) return;
    if (g_login_dialog) {
        jclass cD = (*env)->FindClass(env, "android/app/Dialog");
        jmethodID dismiss = (*env)->GetMethodID(env, cD, "dismiss", "()V");
        if (dismiss) (*env)->CallVoidMethod(env, g_login_dialog, dismiss);
        (*env)->DeleteGlobalRef(env, g_login_dialog); g_login_dialog = NULL;
    }
    if (g_login_page) { (*env)->DeleteGlobalRef(env, g_login_page); g_login_page = NULL; }
    if (g_login_back){ (*env)->DeleteGlobalRef(env, g_login_back); g_login_back = NULL; }
    if (g_login_join){ (*env)->DeleteGlobalRef(env, g_login_join); g_login_join = NULL; }
    if (g_login_stmt_title){ (*env)->DeleteGlobalRef(env, g_login_stmt_title); g_login_stmt_title = NULL; }
    if (g_login_scroll){ (*env)->DeleteGlobalRef(env, g_login_scroll); g_login_scroll = NULL; }
    if (g_login_status){ (*env)->DeleteGlobalRef(env, g_login_status); g_login_status = NULL; }
    g_stmt_accepted = 0;
    drain(env, "v37: login remove");
    log_app("v37: login page closed\n");
}

static void page_show_login(void)
{
    JNIEnv* e = NULL;
    jobject page, card, title, stmtBox, stmtTitle, scroll, stmtText, status, btnRow, back, join, row;
    int i;
    if (!g_vm || !g_activity || !g_decor) return;
    if ((*g_vm)->GetEnv(g_vm, (void**)&e, JNI_VERSION_1_6) != JNI_OK || !e) return;
    if (g_login_page) { log_app("v45: login page exists -> rebuild\n"); page_hide_login(); }

    { jclass cFrame = (*e)->FindClass(e, "android/widget/FrameLayout");
      jmethodID ctor = (*e)->GetMethodID(e, cFrame, "<init>", "(Landroid/content/Context;)V");
      page = (*e)->NewObject(e, cFrame, ctor, g_activity); }
    if (!page) return;
    v37_bg(e, page, (jint)0xFF0E1114, 0);

    card = v37_lin(e, 1);
    v37_bg(e, card, (jint)0xFF2B2E32, 16);
    v37_pad(e, card, 20, 18, 20, 20);

    title = v37_text(e, "Multiplayer SFS - Join Menu", 20, (jint)0xFFFFFFFF, 1);
    { jclass cTV = (*e)->FindClass(e, "android/widget/TextView");
      jmethodID gv = (*e)->GetMethodID(e, cTV, "setGravity", "(I)V");
      (*e)->CallVoidMethod(e, title, gv, 17); }
    v37_addvw(e, card, title, -1, -2, 0, 0, 10);

    for (i = 0; i < 3; ++i) {
        const char* lbl = i == 0 ? "Server" : (i == 1 ? "Username" : "Password");
        const char* val = i == 0 ? "192.168.1.5" : (i == 1 ? "yangchengtong" : "");
        int itype = i == 2 ? 0x81 : 1;
        jobject L, E;
        row = v37_lin(e, 0);
        L = v37_text(e, lbl, 17, (jint)0xFFFFFFFF, 1);
        E = v37_edit(e, val, itype);
        v37_addvw(e, row, L, dpi(96), -2, 0, 0, 0);
        v37_addvw(e, row, E, 0, dpi(44), 1, 0, 0);
        v37_addvw(e, card, row, -1, -2, 0, 0, 7);
    }

    stmtBox = v37_lin(e, 1);
    v37_bg(e, stmtBox, (jint)0xFF222529, 12);
    v37_pad(e, stmtBox, 12, 10, 12, 10);
    stmtTitle = v37_text(e, "User Statement (read to the end)", 16, (jint)0xFFFFFFFF, 1);
    { jclass cTV = (*e)->FindClass(e, "android/widget/TextView");
      jmethodID gv = (*e)->GetMethodID(e, cTV, "setGravity", "(I)V");
      (*e)->CallVoidMethod(e, stmtTitle, gv, 17); }
    v37_addvw(e, stmtBox, stmtTitle, -1, -2, 0, 0, 5);

    { jclass cSV = (*e)->FindClass(e, "android/widget/ScrollView");
      jmethodID ctor = (*e)->GetMethodID(e, cSV, "<init>", "(Landroid/content/Context;)V");
      scroll = (*e)->NewObject(e, cSV, ctor, g_activity); }
    stmtText = v37_text(e, STMT_TEXT, 11, (jint)0xFFB9C0C8, 0);
    { jclass cVG = (*e)->FindClass(e, "android/view/ViewGroup");
      jmethodID mAdd = (*e)->GetMethodID(e, cVG, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
      jclass cLP = (*e)->FindClass(e, "android/widget/LinearLayout$LayoutParams");
      jmethodID ctorLP = (*e)->GetMethodID(e, cLP, "<init>", "(II)V");
      jobject lp = (*e)->NewObject(e, cLP, ctorLP, -1, -2);
      (*e)->CallVoidMethod(e, scroll, mAdd, stmtText, lp); }
    v37_addvw(e, stmtBox, scroll, -1, dpi(180), 0, 0, 4);
    v37_addvw(e, card, stmtBox, -1, -2, 0, 0, 8);

    status = v37_text(e, "Server: unreachable   Client V1.4 (handshake 2)   Ping: --   Players: --", 11, (jint)0xFF9AA4B2, 0);
    v37_addvw(e, card, status, -1, -2, 0, 0, 7);

    btnRow = v37_lin(e, 0);
    back = v37_button(e, "Back", (jint)0xFF3A3E42);
    join = v37_button(e, "Read to the end to continue", (jint)0xFF3A3E42);
    v37_addvw(e, btnRow, back, 0, dpi(46), 1, 5, 0);
    v37_addvw(e, btnRow, join, 0, dpi(46), 1, 5, 0);
    v37_addvw(e, card, btnRow, -1, -2, 0, 0, 8);

    { jclass cVG = (*e)->FindClass(e, "android/view/ViewGroup");
      jmethodID mAdd = (*e)->GetMethodID(e, cVG, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
      jclass cFLP = (*e)->FindClass(e, "android/widget/FrameLayout$LayoutParams");
      jmethodID ctorLP = (*e)->GetMethodID(e, cFLP, "<init>", "(II)V");
      jobject lp = (*e)->NewObject(e, cFLP, ctorLP, -1, -2);
      jfieldID fg = (*e)->GetFieldID(e, cFLP, "gravity", "I");
      (*e)->SetIntField(e, lp, fg, 17);
      jclass cMLP = (*e)->FindClass(e, "android/view/ViewGroup$MarginLayoutParams");
      jmethodID sm = (*e)->GetMethodID(e, cMLP, "setMargins", "(IIII)V");
      (*e)->CallVoidMethod(e, lp, sm, dpi(16), dpi(16), dpi(16), dpi(16));
      (*e)->CallVoidMethod(e, page, mAdd, card, lp); }

    {
        jclass cD = (*e)->FindClass(e, "android/app/Dialog");
        jmethodID ctorD = (*e)->GetMethodID(e, cD, "<init>", "(Landroid/content/Context;)V");
        jmethodID setCV = (*e)->GetMethodID(e, cD, "setContentView", "(Landroid/view/View;)V");
        jmethodID showD = (*e)->GetMethodID(e, cD, "show", "()V");
        jmethodID getWin = (*e)->GetMethodID(e, cD, "getWindow", "()Landroid/view/Window;");
        jobject d = (*e)->NewObject(e, cD, ctorD, g_activity);
        jobject win;
        if (!d || drain(e, "v38: dialog")) return;
        (*e)->CallVoidMethod(e, d, setCV, page);
        (*e)->CallVoidMethod(e, d, showD);
        win = (*e)->CallObjectMethod(e, d, getWin);
        if (win) {
            jclass cW = (*e)->FindClass(e, "android/view/Window");
            jmethodID sl = (*e)->GetMethodID(e, cW, "setLayout", "(II)V");
            jmethodID sb = (*e)->GetMethodID(e, cW, "setBackgroundDrawable", "(Landroid/graphics/drawable/Drawable;)V");
            jclass cCD = (*e)->FindClass(e, "android/graphics/drawable/ColorDrawable");
            jmethodID ctorC = (*e)->GetMethodID(e, cCD, "<init>", "(I)V");
            jobject cd;
            (*e)->CallVoidMethod(e, win, sl, -1, -1);
            cd = (*e)->NewObject(e, cCD, ctorC, (jint)0);
            if (cd) (*e)->CallVoidMethod(e, win, sb, cd);
        }
        g_login_dialog = (*e)->NewGlobalRef(e, d);
    }

    g_login_page = (*e)->NewGlobalRef(e, page);
    g_login_back = (*e)->NewGlobalRef(e, back);
    g_login_join = (*e)->NewGlobalRef(e, join);
    g_login_stmt_title = (*e)->NewGlobalRef(e, stmtTitle);
    g_login_scroll = (*e)->NewGlobalRef(e, scroll);
    g_login_status = (*e)->NewGlobalRef(e, status);
    log_app("v37: *** LOGIN PAGE SHOWN ***\n");
}

/* ================= v39: native window via the game's factory ================= */
static void* g_mOpen=NULL, *g_mDrawTitle=NULL, *g_mDrawText2=NULL;
static void* g_modsCls=NULL, *g_modsOpen=NULL, *g_modsMainField=NULL;
static void* g_delFuncBool=NULL, *g_delActionPanel=NULL, *g_delAction=NULL;
static void* (*g_mClassParent)(void*)=NULL;
static const char* (*g_mClassGetName)(void*)=NULL;

static int32_t v39_tramp_bool(void* a, void* b, void* c, void* d){ (void)a;(void)b;(void)c;(void)d; return 1; }
static void v39_tramp_action(void* a, void* b, void* c, void* d){ (void)a;(void)b;(void)c;(void)d; log_app("v39: action trampoline\n"); }

static void v39_draw_title(void* p, const char* s)
{
    void* exc = NULL; void* a0 = g_sNew ? g_sNew(s) : NULL;
    void* ip[1]; ip[0] = a0;
    if (g_mDrawTitle && a0) g_ri(g_mDrawTitle, p, ip, &exc);
    { char b[160]; snprintf(b,sizeof(b),"v39: DrawTitle('%s') exc=%p\n", s, exc); log_app(b); }
}

static void v39_draw_text(void* p, int slot, const char* s)
{
    void* exc = NULL; int32_t i0 = slot; void* a1 = g_sNew ? g_sNew(s) : NULL;
    void* ip[2]; ip[0] = &i0; ip[1] = a1;
    if (g_mDrawText2 && a1) g_ri(g_mDrawText2, p, ip, &exc);
    { char b[200]; snprintf(b,sizeof(b),"v39: DrawText(%d) exc=%p\n", slot, exc); log_app(b); }
}

static void v39_draw_button(void* p, int slot, const char* s)
{
    void* exc = NULL; int32_t i0 = slot; void* a1 = g_sNew ? g_sNew(s) : NULL;
    void* a2 = g_delAction; int32_t a3 = 1;
    void* ip[4]; ip[0] = &i0; ip[1] = a1; ip[2] = a2; ip[3] = &a3;
    if (g_mDrawFull && a1) g_ri(g_mDrawFull, p, ip, &exc);
    { char b[200]; snprintf(b,sizeof(b),"v39: DrawFullButton(%d,'%s') exc=%p\n", slot, s, exc); log_app(b); }
}

static void v39_build(void* panel)
{
    char b[200];
    snprintf(b,sizeof(b),"v39: BUILDER panel=%p\n", panel); log_app(b);
    if (!panel) { log_app("v39: builder got null panel\n"); return; }
    v39_draw_title(panel, "Multiplayer SFS - Join Menu");
    v39_draw_text(panel, 0, "Native window test - built by libsfsmp");
    v39_draw_button(panel, 1, "Join Server");
    v39_draw_button(panel, 2, "Back");
    log_app("v39: builder done\n");
}

static void v39_tramp_builder(void* a, void* b, void* c, void* d)
{
    void* cand[2]; int i;
    (void)c; (void)d;
    cand[0] = a; cand[1] = b;
    for (i = 0; i < 2; ++i) {
        void* o = cand[i];
        void* k;
        const char* kn;
        if (!o) continue;
        k = g_mObjGetClass ? g_mObjGetClass(o) : NULL;
        kn = (k && g_mClassGetName) ? g_mClassGetName(k) : NULL;
        if (kn && strstr(kn, "NOJZIZCFSEXSUEEX")) { v39_build(o); return; }
    }
    v39_build(a ? a : b);
}

static void* v39_make_delegate(void* delClass, void* fn)
{
    void* d;
    if (!g_mNewObj || !delClass) return NULL;
    d = g_mNewObj(delClass);
    if (!d) return NULL;
    *(void**)((char*)d + 0x10) = fn;
    *(void**)((char*)d + 0x18) = fn;
    if (g_doffMethodCode >= 0) *(void**)((char*)d + 0x40) = fn;
    return d;
}


static void native_mods_open(void)
{
    char b[220]; void* inst = NULL; void* exc = NULL;
    log_app("v40: ===== native mods window test =====\n");
    if (!g_modsCls) { log_app("v40: mods class missing\n"); return; }
    if (g_modsMainField && g_staticGet) g_staticGet(g_modsMainField, &inst);
    if (!inst) { log_app("v40: mods main is NULL\n"); return; }
    snprintf(b,sizeof(b),"v40: mods main=%p Open=%p\n", inst, g_modsOpen); log_app(b);
    if (!g_modsOpen) { log_app("v40: Open missing\n"); return; }
    log_app("v40: calling Open now...\n");
    g_ri(g_modsOpen, inst, NULL, &exc);
    snprintf(b,sizeof(b),"v40: Open returned exc=%p\n", exc); log_app(b);
}

static void native_window_test(void)
{
    char b[300]; void* exc = NULL; void* arr; void* params[1]; int n, i;
    void* panel = NULL;
    log_app("v39: ===== native window test =====\n");
    if (!g_panelCls || !g_typePanel) { log_app("v39: no panel class\n"); return; }
    g_mOpen = g_gm ? g_gm(g_panelCls, "Open", 5) : NULL;
    g_mDrawTitle = g_gm ? g_gm(g_panelCls, "DrawTitle", 1) : NULL;
    g_mDrawText2 = g_gm ? g_gm(g_panelCls, "DrawText", 2) : NULL;
    if (!g_mOpen) { log_app("v39: Open not found\n"); return; }
    snprintf(b,sizeof(b),"v39: Open=%p Title=%p Text=%p DrawFull=%p\n", g_mOpen, g_mDrawTitle, g_mDrawText2, g_mDrawFull);
    log_app(b);

    params[0] = g_typePanel;
    arr = g_ri(g_mFindAll, NULL, params, &exc);
    n = (arr && !exc) ? (int)g_alg(arr) : 0;
    for (i = 0; i < n; ++i) {
        void* o = ((void**)((char*)arr + 0x20))[i];
        if (!o) continue;
        panel = o;
        { char nm[40]; char_name(o, nm, 40);
          snprintf(b,sizeof(b),"v39: panel[%d]=%p '%s'\n", i, o, nm); log_app(b); }
        if (panel) break;
    }
    if (!panel) { log_app("v39: no panel instance\n"); return; }

    if (!g_delFuncBool) {
        void* t0 = g_mgp ? g_mgp(g_mOpen, 0) : NULL;
        void* t1 = g_mgp ? g_mgp(g_mOpen, 1) : NULL;
        void* t3 = g_mgp ? g_mgp(g_mOpen, 3) : NULL;
        void* c0 = t0 ? g_tgoc(t0) : NULL;
        void* c1 = t1 ? g_tgoc(t1) : NULL;
        void* c3 = t3 ? g_tgoc(t3) : NULL;
        g_delFuncBool = v39_make_delegate(c0, (void*)v39_tramp_bool);
        g_delActionPanel = v39_make_delegate(c1, (void*)v39_tramp_builder);
        g_delAction = v39_make_delegate(c3, (void*)v39_tramp_action);
        snprintf(b,sizeof(b),"v39: delegates fb=%p ap=%p a=%p\n", g_delFuncBool, g_delActionPanel, g_delAction);
        log_app(b);
    }
    if (!g_delFuncBool || !g_delActionPanel) { log_app("v39: delegate build failed\n"); return; }

    { int32_t one = 1;
      void* ip[5]; ip[0] = g_delFuncBool; ip[1] = g_delActionPanel; ip[2] = &one;
      ip[3] = g_delAction; ip[4] = g_delAction;
      log_app("v39: calling Open now...\n");
      g_ri(g_mOpen, panel, ip, &exc);
      snprintf(b,sizeof(b),"v39: Open returned exc=%p\n", exc); log_app(b); }
}

/* ================= v64: "多人联机" button == connect + enter the world =================
 * 用户硬要求（m01348）：入口必须是我们自己的按钮，原版「开始游戏」只能用于单人，不算。
 * 所以点按钮时我们做两件事：
 *   1) 连服务器（地址优先取 sfsmp-connect.txt，其次弹窗里读到的值，最后默认 192.168.1.5:9806）
 *   2) 直接请游戏自己的场景加载器加载世界场景：
 *      SFS.USPVHOMQUKUGZPLU.LoadWorldScene(bool)  RVA 0x16EE994  (dump.cs:28962-29037)
 *      同一个类里还有 LoadHomeScene / LoadHubScene / LoadBuildScene，说明它就是场景切换总入口，
 *      世界卡片上的「开始游戏」背后调的就是它。调用走 il2cpp_runtime_invoke(实例, 参数)，
 *      不碰 methodPointer，也不手搓委托。
 */
static const char* mp_pick_endpoint(char* buf, int cap){
    /* v116: 唯一来源就是 IP 页面里玩家自己填的地址。
     * 删掉两条隐形回退：
     *   a) 读 sfsmp-connect.txt（我本机测试的残留文件会让玩家"自己连上某个服务器"）；
     *   b) 硬编码 192.168.1.5:9806（玩家没填也被连到我家的服务器）。
     * 玩家没填就不连 —— 这是产品行为，不是测试开关。 */
    (void)buf; (void)cap;
    if (g_tiVal[0][0]) return g_tiVal[0];
    return NULL;
}

/* ---- v69: 世界卡片链路 ----------------------------------------------------------------
 * 为什么不再直接调 LoadWorldScene：那只切了场景，行星/存档都没装载，结果
 * SFS.World.Location.get_Height() 每帧抛 NullReference（同一进程 logcat 里 5800+ 条），
 * 世界冻结、左上角菜单点不动。世界卡片上的「开始游戏」走的是游戏自己的完整链路
 * （读存档 + 载星系 + LoadWorldScene），所以改成模拟那两次点击：
 *   ① 主菜单「开始游戏」（Play Button 的 clickEvent 0x48）→ 世界列表
 *   ② 世界卡片上的「开始游戏」→ 真正进世界
 * 只动 clickEvent(0x48=ClickUnityEvent)，不碰 onClick(0x68)。 */
static volatile int g_wcStep = 0, g_wcTimer = 0, g_autoEnterTried = 0;
static volatile int g_openListStep = 0, g_openListTimer = 0;   /* v89: 连接后替玩家点游戏原生 UI（Play Button -> 世界卡） */
static volatile int g_uStep = 0, g_uTimer = 0;   /* v86: 标准 UnityEvent 点按钮的两步状态机 */
/* v110: 远端火箭渲染重新开启，但加了"熔断"保险。
 * 历史：v87-v102 因为 rr_sync_pose 直接往 Location 组件写 double（覆盖 planet/Obs 指针）
 *       导致 UnityMain SIGSEGV，整块功被关掉（g_rrEnabled=0）。
 * v88 已把写法改成官方 Location.set_Value，写坏指针的根因消除，但开关一直没再打开。
 * v110 重开的策略：
 *   1) 运行时可切换 —— 读 sfsmp-rr.txt（内容 "1" 开 / "0" 关），不用重新编译就能回退；
 *   2) 熔断 —— g_rrStrikes 累计到 3 次异常/无效状态就自动关闭，避免反复崩；
 *   3) 先删掉 rr_diag_space 里对结构体返回值做 g_unbox 强解的代码（那是复开后的头号炸弹）。 */
static volatile int g_rrEnabled = 1;   /* 默认开；文件里写 0 可关 */
static volatile int g_rrStrikes = 0;   /* 熔断计数：异常一次 +1，达到 3 自动关 */
static volatile int g_rrFileChecked = 0;

/* v80: 依次触发 clickEvent(0x48) 与 onClick(0x68)。v79 实测只发 0x48 没能打开世界列表，
 * 所以两个都发；末尾打印 Play Button 的屏幕坐标，便于用 adb input tap 兜底。 */
static int wc_fire_click(void* btn, const char* tag){
    char b[220]; void* e = NULL; void* ev;
    if (!btn) { log_app("v81: no button\n"); return 0; }
    ev = *(void**)((char*)btn + 0x48);
    if (!g_mEvtInvoke && ev && g_mObjGetClass) {
        void* ec = g_mObjGetClass(ev);
        if (ec && g_gm) g_mEvtInvoke = g_gm(ec, "Invoke", 0);
        snprintf(b,sizeof(b),"v81: late-bind Invoke cls=%p m=%p\n", ec, g_mEvtInvoke); log_app(b);
    }
    if (!g_mEvtInvoke) { log_app("v81: still no Invoke method\n"); return 0; }
    ev = *(void**)((char*)btn + 0x48);
    g_ri(g_mEvtInvoke, ev, NULL, &e);
    snprintf(b,sizeof(b),"v80: fired %s click48 ev=%p exc=%p\n", tag, ev, e); log_app(b);
    e = NULL;
    ev = *(void**)((char*)btn + 0x68);
    g_ri(g_mEvtInvoke, ev, NULL, &e);
    snprintf(b,sizeof(b),"v80: fired %s click68 ev=%p exc=%p\n", tag, ev, e); log_app(b);
    return 1;
}

/* v86: 走 Unity 标准 UI 路径 —— Button.m_OnClick（字段 0x100）是 ButtonClickedEvent : UnityEvent，
 * 而 UnityEvent::Invoke() 是普通 0 参方法（SFS 自定义的 ClickUnityEvent 上没有 Invoke，v81 已实测证明）。
 * 这是唯一能真正“点”到游戏原生按钮的办法，点下去走的是游戏自己的加载链路。 */
static void* g_mStdEvtInvoke=NULL;
static int unity_click(void* btnComp, const char* tag){
    char b[240]; void* e = NULL;
    if(!btnComp){ log_app("v86: unity-click: null button\n"); return 0; }
    void* ev = *(void**)((char*)btnComp + 0x100);
    if(!ev){ snprintf(b,sizeof(b),"v86: unity-click %s: m_OnClick=null\n", tag); log_app(b); return 0; }
    if(!g_mStdEvtInvoke && g_mObjGetClass && g_gm){
        void* ec = g_mObjGetClass(ev);
        if(ec) g_mStdEvtInvoke = g_gm(ec, "Invoke", 0);
        snprintf(b,sizeof(b),"v86: UnityEvent cls=%p Invoke=%p\n", ec, g_mStdEvtInvoke); log_app(b);
    }
    if(!g_mStdEvtInvoke){ log_app("v86: no UnityEvent::Invoke\n"); return 0; }
    g_ri(g_mStdEvtInvoke, ev, NULL, &e);
    snprintf(b,sizeof(b),"v86: unity-click %s ev=%p exc=%p\n", tag, ev, e); log_app(b);
    return e==NULL;
}

/* v80: 把 Play Button 的屏幕坐标打到日志，供 adb input tap 兜底（Unity 的 y 轴向上）。 */
static void wc_log_target(void){
    void* e=NULL, *go, *tr, *wp, *v3, *sp2, *cam=NULL; char b[220];
    if (!g_srcBtn) { log_app("v81: target: no srcBtn\n"); return; }
    go = g_ri(g_mGetGO, g_srcBtn, NULL, &e);
    if (!go || e) { snprintf(b,sizeof(b),"v81: target: srcGo=%p exc=%p\n", go, e); log_app(b); return; }
    tr = g_mGoTransform ? g_ri(g_mGoTransform, go, NULL, &e) : NULL;
    if (!tr || e) { snprintf(b,sizeof(b),"v81: target: tr=%p exc=%p\n", tr, e); log_app(b); return; }
    wp = g_mGetPos2 ? g_ri(g_mGetPos2, tr, NULL, &e) : NULL;
    if (!wp || e) { snprintf(b,sizeof(b),"v81: target: wp=%p exc=%p\n", wp, e); log_app(b); return; }
    if (g_mCamMain) { void* ec=NULL; void* co=g_ri(g_mCamMain, NULL, NULL, &ec); if (co && !ec) cam=co; }
    if (!cam || !g_mW2S || !g_v3Cls || !g_mNewObj) { snprintf(b,sizeof(b),"v81: target: cam=%p w2s=%p v3=%p new=%p\n", cam,g_mW2S,g_v3Cls,g_mNewObj); log_app(b); return; }
    v3 = g_mNewObj(g_v3Cls); if (!v3) return;
    { float* wf=(float*)((char*)wp+0x10); float* vf=(float*)((char*)v3+0x10);
      vf[0]=wf[0]; vf[1]=wf[1]; vf[2]=wf[2]; }
    { void* a2[2]; a2[0]=cam; a2[1]=v3;
      sp2 = g_ri(g_mW2S, NULL, a2, &e);
      if (sp2 && !e) { float* sf=(float*)((char*)sp2+0x10);
          snprintf(b,sizeof(b),"v80: playbtn screen=(%.0f,%.0f) tapY=%.0f\n", sf[0], sf[1], 2376.0f-sf[1]); log_app(b); } }
}

/* v86: 列出当前所有 Button 并“点”那个 开始游戏/继续游戏 的世界卡（走 UnityEvent::Invoke）。 */
static void u_pick_card(void){
    char b[240], t[96] = "", nm[64] = "";
    void* exc = NULL; void* params[1]; void* arr; int n, i, pickIdx = -1; void* pick = NULL;
    if (!g_typeBtn || !g_findMethod) { log_app("v86: card: button plumbing missing\n"); return; }
    params[0] = g_typeBtn;
    arr = g_ri(g_findMethod, NULL, params, &exc);
    n = (arr && !exc) ? (int)g_alg(arr) : -1;
    snprintf(b,sizeof(b),"v86: world buttons=%d exc=%p\n", n, exc); log_app(b);
    if (n <= 0) return;
    for (i = 0; i < n && i < 32; ++i) {
        void* btn = ((void**)((char*)arr + 0x20))[i]; void* go; void* tc; void* s;
        if (!btn) continue;
        go = g_ri(g_mGetGO, btn, NULL, &exc);
        if (!go) continue;
        obj_name(go, nm, sizeof(nm));
        t[0] = 0; tc = find_text_component(go);
        if (tc && g_mGetTxt) { s = g_ri(g_mGetTxt, tc, NULL, &exc); if (s && !exc) u16_to_ascii(s, t, sizeof(t)); }
        snprintf(b,sizeof(b),"v86: wbtn[%d] '%s' text='%s'\n", i, nm, t); log_app(b);
        if (pick) continue;
        if (!strcmp(nm, "Play Button") || !strcmp(nm, "Play Button(Clone)")) continue;
        if (strstr(t, "\xE5\x88\xA0\xE9\x99\xA4")) continue;                              /* 删除 */
        if (strstr(t, "\xE9\x87\x8D\xE5\x91\xBD\xE5\x90\x8D")) continue;              /* 重命名 */
        if (strstr(t, "\xE6\x96\xB0\xE5\xBB\xBA\xE4\xB8\x96\xE7\x95\x8C")) continue; /* 新建世界 */
        if (strstr(t, "\xE5\xBC\x80\xE5\xA7\x8B\xE6\xB8\xB8\xE6\x88\x8F") ||           /* 开始游戏 */
            strstr(t, "\xE7\xBB\xA7\xE7\xBB\xAD\xE6\xB8\xB8\xE6\x88\x8F") ||           /* 继续游戏 */
            strstr(t, "Play") || strstr(t, "Continue")) { pick = btn; pickIdx = i; }
    }
    if (!pick) { log_app("v86: no world card button in list\n"); return; }
    snprintf(b,sizeof(b),"v86: picking world card button [%d]\n", pickIdx); log_app(b);
    unity_click(pick, "world-card");
}

static void wc_log_btn(void* go, int i){
    char b[240], t[96] = "", nm[64] = "";
    void* exc = NULL; void* tc; void* s;
    obj_name(go, nm, sizeof(nm));
    tc = find_text_component(go);
    if (tc && g_mGetTxt) { s = g_ri(g_mGetTxt, tc, NULL, &exc); if (s && !exc) u16_to_ascii(s, t, sizeof(t)); }
    snprintf(b,sizeof(b),"v69: world-btn[%d] '%s' text='%s'\n", i, nm, t); log_app(b);
}

static void wc_open_list(void){
    log_app("v69: opening world list (play button clickEvent)\n");
    if (!g_srcBtn) { log_app("v69: no play button captured\n"); return; }
    wc_fire_click(g_srcBtn, "play-button -> world list");
}

static void wc_pick_card(void){
    char b[200], t[96] = "", nm[64] = "";
    void* exc = NULL; void* params[1]; void* arr; int n, i, pickIdx = -1; void* pick = NULL;
    if (!g_typeBtn || !g_findMethod) { log_app("v69: button plumbing missing\n"); return; }
    params[0] = g_typeBtn;
    arr = g_ri(g_findMethod, NULL, params, &exc);
    n = (arr && !exc) ? (int)g_alg(arr) : -1;
    snprintf(b,sizeof(b),"v69: world-list buttons(active)=%d exc=%p\n", n, exc); log_app(b);
    if (n <= 0) return;
    for (i = 0; i < n; ++i) {
        void* btn = ((void**)((char*)arr + 0x20))[i]; void* go; void* tc; void* s;
        if (!btn) continue;
        go = g_ri(g_mGetGO, btn, NULL, &exc);
        if (!go) continue;
        wc_log_btn(go, i);
        if (pick) continue;
        obj_name(go, nm, sizeof(nm));
        if (!strcmp(nm, "Play Button") || !strcmp(nm, "Play Button(Clone)")) continue;
        t[0] = 0; tc = find_text_component(go);
        if (tc && g_mGetTxt) { s = g_ri(g_mGetTxt, tc, NULL, &exc); if (s && !exc) u16_to_ascii(s, t, sizeof(t)); }
        if (strstr(t, "\xE5\x88\xA0\xE9\x99\xA4")) continue;                              /* 删除 */
        if (strstr(t, "\xE9\x87\x8D\xE5\x91\xBD\xE5\x90\x8D")) continue;              /* 重命名 */
        if (strstr(t, "\xE6\x96\xB0\xE5\xBB\xBA\xE4\xB8\x96\xE7\x95\x8C")) continue; /* 新建世界 */
        if (strstr(t, "\xE5\xBC\x80\xE5\xA7\x8B\xE6\xB8\xB8\xE6\x88\x8F") ||           /* 开始游戏 */
            strstr(t, "\xE7\xBB\xA7\xE7\xBB\xAD\xE6\xB8\xB8\xE6\x88\x8F") ||           /* 继续游戏 */
            strstr(t, "Play") || strstr(t, "Continue")) { pick = btn; pickIdx = i; }
    }
    if (!pick) { log_app("v69: no world card play button found in list\n"); return; }
    snprintf(b,sizeof(b),"v69: picking world card button [%d]\n", pickIdx); log_app(b);
    if (wc_fire_click(pick, "world-card play")) {
        g_enterReq = 1;
        log_app("v69: world card clicked, waiting for World_Mobile\n");
    }
}

static void enter_world_fallback(void){
    char b[240];
    if (!g_mLoadWorldScene || !g_typeSceneLoader || !g_ri || !g_findMethod) {
        log_app("v64: LoadWorldScene plumbing missing\n"); return;
    }
    { void* params[1]; params[0] = g_typeSceneLoader; void* exc = NULL;
      void* arr = g_ri(g_findMethod, NULL, params, &exc);
      int n = (arr && !exc) ? (int)g_alg(arr) : -1;
      snprintf(b,sizeof(b),"v64: sceneLoader instances=%d exc=%p\n", n, exc); log_app(b);
      if (!arr || exc || n <= 0) {
          log_app("v64: scene loader instance NOT found (list page only?) -> fallback to world list\n");
          if (g_srcBtn && g_mEvtInvoke) { void* ev = *(void**)((char*)g_srcBtn + 0x68);
              if (ev) { void* e8 = NULL; g_ri(g_mEvtInvoke, ev, NULL, &e8);
                  snprintf(b,sizeof(b),"v64: fallback play-event exc=%p\n", e8); log_app(b); } }
          return;
      }
      void* inst = ((void**)((char*)arr + 0x20))[0];
      int32_t zero = 0; void* p1[1]; p1[0] = &zero; void* e2 = NULL;
      log_app("v64: calling LoadWorldScene(false) now...\n");
      void* r = g_ri(g_mLoadWorldScene, inst, p1, &e2);
      snprintf(b,sizeof(b),"v64: LoadWorldScene ret=%p exc=%p inst=%p\n", r, e2, inst); log_app(b);
      g_enterReq = 1;
      if (e2 && g_srcBtn && g_mEvtInvoke) {
          void* ev = *(void**)((char*)g_srcBtn + 0x68);
          if (ev) { void* e8 = NULL; g_ri(g_mEvtInvoke, ev, NULL, &e8);
              snprintf(b,sizeof(b),"v64: fallback play-event exc=%p\n", e8); log_app(b); }
      }
    }
}

static void connect_and_enter_world(void){
    char b[240]; char epbuf[96]; epbuf[0] = 0;
    /* v117: 地址来源按优先级取，全部来自玩家在 IP 页里的输入：
     *   1) g_pendingEp —— 弹窗回调里就已经定好的地址（回调写入 g_tiVal 的同时也写这里）；
     *   2) g_tiVal[0] —— 弹窗回调直接回填的字符串；
     * 两者都没有才是"玩家什么都没填"。
     * v116 只看了 mp_pick_endpoint(=g_tiVal)，结果回调明明回了 '192.168.1.5'
     * 却被判成"没填"，于是既不连也不进世界。 */
    const char* ep = (g_pendingEp[0]) ? g_pendingEp : mp_pick_endpoint(epbuf, sizeof(epbuf));
    if (!ep || !ep[0]) {
        /* v116: 玩家没填服务器地址 —— 不连，也不进世界。
         * 宁可什么都不做，也不能用隐藏默认值把人连到别人的服务器上。 */
        log_app("v116: no endpoint entered -> skip connect\n");
        return;
    }
    snprintf(b,sizeof(b),"v97: MP click -> connect '%s'\n", ep); log_app(b);
    ux_connect_started();
    mp_net_connect(ep);
    g_enterWait = 0;
    g_enterLogs = 0;
    /* ---- v97: 连上之后按官方链路进世界 ----------------------------------------
     * 链路（照抄 WorldsMenu.cs:70-79）：
     *   enter_world_official() -> WorldBaseManager.EnterWorld("sfsmp_tmp", cb)
     *     -> 游戏 LoadSolarSystem -> 回调 enter_done_cb -> g_wantLoadWorld=1
     *       -> hook_update 下一帧 Base.sceneLoader.LoadWorldScene(false)
     * 临时沙盒世界（sfsmp_tmp）由 ensure_tmp_world() 现造，退出删。
     * 之前 v85 把这段清空了，所以「连上但进不去世界」。 */
    if (!g_enterTried) {
        log_app("v97: entering world via official EnterWorld chain\n");
        enter_world_official();
    } else {
        log_app("v97: enter already tried once, skip\n");
    }
    log_app("v97: connect requested (world entry handled by EnterWorld chain)\n");
}

/* ---- v65: real callback for the input dialog ------------------------------------------
 * 背景：v45 里第 3 参（Action<string[]>）传 NULL ⇒ 点「加入服务器」没有任何回调，世界进不去。
 * 现在造一个真能用 Action<string[]>：il2cpp_object_new(Action<string[]>) 后
 *   method_ptr(0x10) / invoke_impl(0x18) = 本文件的 native 回调
 *   method(0x28) = Action<string[]>::Invoke 的真实 MethodInfo（这一格是 NULL 就崩，SFS-Pro 报告有记载）
 * 游戏点「加入服务器」时就会进到这里：拿到 3 行输入 → 连服务器 → 进世界。
 */
static volatile int g_tiCbHits = 0;
__attribute__((noinline, used)) static void ti_callback(void* self, void* args, void* mth){
    (void)self; (void)mth;
    int n = __sync_add_and_fetch(&g_tiCbHits, 1);
    char b[200]; snprintf(b,sizeof(b),"v65: *** DIALOG CALLBACK HIT #%d args=%p ***\n", n, args); log_app(b);
    if (args) {
        int len = *(int*)((char*)args + 0x18);
        snprintf(b,sizeof(b),"v65: callback strings=%d\n", len); log_app(b);
        for (int i = 0; i < len && i < 3; i++) {
            void* s = ((void**)((char*)args + 0x20))[i];
            char t[96] = ""; if (s) u16_to_ascii(s, t, sizeof(t));
            snprintf(b,sizeof(b),"v65: cb[%d]='%s'\n", i, t); log_app(b);
            if (t[0]) { strncpy(g_tiVal[i], t, 95); g_tiVal[i][95] = 0; }
        }
    }
    /* v67: 不在回调里直接切场景 —— 先让 Confirm()/OnClose() 把弹窗收尾做完 */
    strncpy(g_pendingEp, g_tiVal[0][0] ? g_tiVal[0] : "192.168.1.5:9806", 95);
    g_pendingEnter = 45;
    { char pb[160]; snprintf(pb,sizeof(pb),"v67: enter world scheduled in 45 frames (ep='%s')\n", g_pendingEp); log_app(pb); }
}

static void* make_ti_action(void){
    if (!g_mNewObj || !g_tiActCls) { log_app("v65: cannot build dialog callback (no object_new/Action<T> class)\n"); return NULL; }
    void* del = g_mNewObj(g_tiActCls);
    if (!del) { log_app("v65: object_new(Action<string[]>) FAILED\n"); return NULL; }
    int oMp = (g_doffMethodPtr >= 0) ? g_doffMethodPtr : 0x10;
    int oIi = (g_doffInvokeImpl >= 0) ? g_doffInvokeImpl : 0x18;
    int oMe = (g_doffMethod >= 0) ? g_doffMethod : 0x28;
    *(void**)((char*)del + oMp) = (void*)ti_callback;
    *(void**)((char*)del + oIi) = (void*)ti_callback;
    *(void**)((char*)del + oMe) = g_mTiActInvoke;   /* 真实 MethodInfo, 不能是 NULL */
    char b[200]; snprintf(b,sizeof(b),"v65: dialog callback=%p mp@0x%x ii@0x%x method@0x%x=%p\n",
                          del, (unsigned)oMp, (unsigned)oIi, (unsigned)oMe, g_mTiActInvoke);
    log_app(b);
    return del;
}

static void poll_click(void)
{
    void* e = NULL; void* r; int down = 0, up = 0, inside = 0; float mx = 0, my = 0;
    if (!g_cloneGo || !g_cloneRect || !g_rcs) return;
    if (g_clickCool > 0) g_clickCool--;
    if (g_dialogOpen || g_clickCool > 0) { g_pressed = 0; return; }   /* v66 */
    if (g_mdDown) { int32_t z = 0; void* p[1]; p[0]=&z; r = g_ri(g_mdDown, NULL, p, &e); if (r && !e) down = *(unsigned char*)g_unbox(r) ? 1 : 0; }
    if (g_mdUp)   { int32_t z = 0; void* p[1]; p[0]=&z; r = g_ri(g_mdUp,   NULL, p, &e); if (r && !e) up   = *(unsigned char*)g_unbox(r) ? 1 : 0; }
    if (g_mpos)   { r = g_ri(g_mpos, NULL, NULL, &e); if (r && !e) { float* v = (float*)g_unbox(r); mx = v[0]; my = v[1]; } }
    { float sp[2]; void* p[3];
      sp[0] = mx; sp[1] = my;
      void* camObj = NULL;
      if (g_mCamMain) { void* ec=NULL; void* co=g_ri(g_mCamMain, NULL, NULL, &ec); if(co && !ec) camObj=co; }
      p[0] = g_cloneRect; p[1] = sp; p[2] = camObj;
      r = g_ri(g_rcs, NULL, p, &e);
      if (r && !e) inside = *(unsigned char*)g_unbox(r) ? 1 : 0;
      if (!inside && g_clonePosOk) {
          float dx = mx - g_clonePos[0], dy = my - g_clonePos[1];
          if ((dx<0?-dx:dx) < 108.0f && (dy<0?-dy:dy) < 45.0f) inside = 1;   /* v66: 收紧到按钮实际大小 */
      }
      if (!inside && g_mW2S && g_mGetPos2 && g_v3Cls && g_mNewObj) {
          void* e9=NULL; void* wp=g_ri(g_mGetPos2, g_cloneRect, NULL, &e9);
          if(wp && !e9 && camObj){
              void* v3=g_mNewObj(g_v3Cls);
              if(v3){ float* wf=(float*)((char*)wp+0x10); float* vf=(float*)((char*)v3+0x10);
                  vf[0]=wf[0]; vf[1]=wf[1]; vf[2]=wf[2];
                  void* a2[2]; a2[0]=camObj; a2[1]=v3; void* sp2=NULL; void* e10=NULL;
                  sp2=g_ri(g_mW2S, NULL, a2, &e10);
                  if(sp2 && !e10){ float* sf=(float*)((char*)sp2+0x10);
                      float dx=mx-sf[0], dy=my-sf[1];
                      if((dx<0?-dx:dx) < 108.0f && (dy<0?-dy:dy) < 45.0f) inside = 1;   /* v66 */
                      if((g_frame % 300) == 0){ char gb[200];
                          snprintf(gb,sizeof(gb),"v97: MP_BTN screen=(%.0f,%.0f) size=~216x90 tap=(%.0f,%.0f)\n",
                                   sf[0], sf[1], sf[0], 2376.0f - sf[1]); log_app(gb); }
                  }
              }
          }
      } }
    if ((g_frame % 120) == 0 && g_mGetRect && g_cloneRect) {
        void* e6=NULL; void* rr=g_ri(g_mGetRect, g_cloneRect, NULL, &e6);
        if(rr && !e6){ float* f=(float*)((char*)rr+0x10);
            if(0){ char rb[160]; snprintf(rb,sizeof(rb),"v51: rect x=%.0f y=%.0f w=%.0f h=%.0f mouse=(%.0f,%.0f)\n", f[0],f[1],f[2],f[3],mx,my); log_app(rb); }
        }
    }
    if (down) { char b[140]; snprintf(b,sizeof(b),"v44: DOWN m=(%.0f,%.0f) inside=%d pressed=%d\n", mx, my, inside, g_pressed); log_app(b); }
    if (down && inside) g_pressed = 1;
    if (up) { char b[140]; snprintf(b,sizeof(b),"v44: UP m=(%.0f,%.0f) inside=%d pressed=%d\n", mx, my, inside, g_pressed); log_app(b); }
    if (up && g_pressed && inside) {
        log_app("v37: *** CLICKED -> login page ***\n");
        int _m = read_mode();
        if (_m == 10) native_login_window();
        else if (_m == 20) {        /* v116: 无条件弹 IP 输入页面。
            * 历史教训：v102 起这里有一条"有 sfsmp-connect.txt 就静默连接"的测试捷径，
            * 结果每次我本机测试写上这个文件，用户拿到的包就"IP 页消失了"，
            * 被反复质疑是故意把页面去掉（用户原话三次）。
            * 玩家必须永远看到 IP 页 —— 这条路径整条删掉，不留开关。 */
            log_app("v116: *** CLICKED -> IP input page (always) ***\n");
            /* v117: 清掉上一轮残留的地址，保证"这次不填就不连"，
             * 也保证想改地址的人不会被上一轮的旧值偷偷顶替。 */
            g_pendingEp[0] = 0; g_tiVal[0][0] = 0; g_tiVal[1][0] = 0; g_tiVal[2][0] = 0;
            native_text_input();
        }
        else native_text_input();   /* 其它模式仍弹原生输入框（服务器/用户名/密码） */
        g_clickCool = 90;           /* v66: 2 秒内不再响应，避免连点/误触把输入页重开 */
    }
    if (up) g_pressed = 0;
}


/* ================= v43: NOJ drawer (SFS Pro route) ================= */
static void* g_nojCls2=NULL, *g_nojType2=NULL, *g_mOpenNoj=NULL, *g_mDrawTitle2=NULL, *g_mDrawText3=NULL, *g_mDrawFullBtn=NULL;
static void* (*g_mgp2)(void*, uint32_t)=NULL;
static void* (*g_cfit)(void*)=NULL;
static void* (*g_ghn)(void*, int)=NULL;
static int   g_noj_busy=0;

static void* noj_pick(void* a, void* b){
    void* cand[2] = { b, a };
    for (int i=0;i<2;i++){
        void* p = cand[i];
        if (!p) continue;
        if ((uintptr_t)p < 0x1000 || ((uintptr_t)p >> 48) || ((uintptr_t)p & 7)) continue;
        void* k = *(void**)p;
        if ((uintptr_t)k < 0x1000 || ((uintptr_t)k >> 48)) continue;
        const char* kn = g_mClassGetName ? g_mClassGetName(k) : NULL;
        if (kn && strstr(kn, "NOJZIZCFSEXSUEEX")) return p;
        if (!kn && g_nojCls2 && k == g_nojCls2) return p;
    }
    return NULL;
}

static void noj_draw_all(void* panel){
    char b[240]; void* e=NULL; int idx = 0x7fffffff; int one = 1; void* s1;
    if(!panel || g_noj_busy) return;
    g_noj_busy = 1;
    log_app("v43: drawing panel content...\n");
    s1 = g_sNew ? g_sNew("Multiplayer SFS") : NULL;
    if(g_mDrawTitle2 && s1){ void* p[1]={s1}; void* r=g_ri(g_mDrawTitle2,panel,p,&e); snprintf(b,sizeof(b),"v43: DrawTitle ret=%p exc=%p\n",r,e); log_app(b); e=NULL; }
    s1 = g_sNew ? g_sNew("Server: 192.168.1.5   Client V1.4") : NULL;
    if(g_mDrawText3 && s1){ void* p[2]={&idx,s1}; void* r=g_ri(g_mDrawText3,panel,p,&e); snprintf(b,sizeof(b),"v43: DrawText ret=%p exc=%p\n",r,e); log_app(b); e=NULL; }
    s1 = g_sNew ? g_sNew("Join Server") : NULL;
    if(g_mDrawFullBtn && s1){ void* p[4]={&idx,s1,NULL,&one}; void* r=g_ri(g_mDrawFullBtn,panel,p,&e); snprintf(b,sizeof(b),"v43: DrawFullButton ret=%p exc=%p\n",r,e); log_app(b); e=NULL; }
    g_noj_busy = 0;
}

static void stub_noj_build(void* a, void* b, void* c, void* d){
    (void)c; (void)d;
    log_app("v43: BUILDER entered\n");
    void* panel = noj_pick(a,b);
    if(!panel){ log_app("v43: BUILDER no panel\n"); return; }
    char b2[120]; snprintf(b2,sizeof(b2),"v43: BUILDER panel=%p\n", panel); log_app(b2);
    noj_draw_all(panel);
}

static int stub_true(void){ return 1; }

static void* make_del(void* klass, void* fn){
    void* d = g_mNewObj ? g_mNewObj(klass) : NULL;
    if(!d) return NULL;
    *(void**)((char*)d + 0x10) = fn;
    *(void**)((char*)d + 0x18) = fn;
    *(void**)((char*)d + 0x28) = NULL;
    *(void**)((char*)d + 0x40) = NULL;
    if(g_ghn) g_ghn(d, 0);
    return d;
}

static void noj_open(int with_delegates){
    char b[260]; void* e=NULL; int z=0, one=1;
    log_app(with_delegates ? "v43: ==== NOJ open (delegates) ====\n" : "v43: ==== NOJ probe (NULL delegates) ====\n");
    if(!g_nojCls2 || !g_nojType2 || !g_mOpenNoj || !g_mFindAll){ log_app("v43: missing symbols\n"); return; }
    void* args[1]={g_nojType2};
    void* arr=g_ri(g_mFindAll,NULL,args,&e);
    int n=(arr&&!e)?(int)g_alg(arr):-1;
    if(n<=0) return;
    snprintf(b,sizeof(b),"v43: NOJ instances=%d exc=%p\n",n,e); log_app(b);
    void* inst=NULL;
    for(int i=0;i<n && i<8;i++){
        void* o=((void**)((char*)arr+0x20))[i];
        if(!o) continue;
        char nm[60]; obj_name(o,nm,60);
        snprintf(b,sizeof(b),"v43: inst[%d]=%p '%s'\n",i,o,nm); log_app(b);
        if(!inst) inst=o;
    }
    if(!inst) return;
    void* fb=NULL, *an=NULL;
    if(1){
        void* t0 = g_mgp2 ? g_mgp2(g_mOpenNoj,0) : NULL;
        void* t1 = g_mgp2 ? g_mgp2(g_mOpenNoj,1) : NULL;
        void* k0 = (t0 && g_cfit) ? g_cfit(t0) : NULL;
        void* k1 = (t1 && g_cfit) ? g_cfit(t1) : NULL;
        fb = k0 ? make_del(k0,(void*)stub_true) : NULL;
        an = k1 ? make_del(k1,(void*)stub_noj_build) : NULL;
        snprintf(b,sizeof(b),"v43: delegates fb=%p an=%p (k0=%p k1=%p)\n",fb,an,k0,k1); log_app(b);
        if(!fb || !an){ log_app("v43: delegate build failed\n"); return; }
    }
    void* p[5]={fb, an, &z, NULL, NULL}; (void)one;
    log_app("v43: calling Open now...\n");
    void* r=g_ri(g_mOpenNoj, inst, p, &e);
    snprintf(b,sizeof(b),"v43: Open returned %p exc=%p\n", r, e); log_app(b);
}

static void* g_orig_wt_update=NULL;
static void hook_worldtime(void* self){
    /* 不在世界里自动探测：加载期间调用 FindObjectsOfType 会把主线程卡死 */
    if (g_orig_wt_update) ((void(*)(void*))g_orig_wt_update)(self);
}

static void probe_now_check(void){
    const char* flag = dp("sfsmp-probe-now.txt");
    int fd = open(flag, O_RDONLY);
    if(fd < 0) return;
    char b[8]={0}; int n=(int)read(fd,b,7); close(fd);
    if(n<=0 || b[0]!='1') return;
    fd = open(flag, O_WRONLY|O_TRUNC); if(fd>=0){ write(fd,"0",1); close(fd); }
    log_app("m1: --- manual probe ---\n");
    dump_rockets();
}

/* ---- v70: 让游戏自己去装「真世界」 ------------------------------------------
 * 硬调 LoadWorldScene 只切场景，行星/存档没装 ⇒ 每帧 NullReferenceException（Location.get_Height），
 * 时间停住、汉堡菜单点不动。改用 SFS.WorldBase.FAELUUBBVIPEYYXS.EnterWorld(worldName, cb)：
 * 它就是世界卡片「开始游戏」背后的成品链路（读 Saving/Worlds/<名> 的存档 + 星系）。
 */
/* ---- v97: 存档根目录动态探测 -------------------------------------------------
 * 实测（FileLocations.cs:139-166）：Android 11+ 上游戏把整个 Saving 目录迁到了
 *   /sdcard/Android/media/<pkg>/Saving     <-- 现在真正生效的位置
 * 老路径 /sdcard/Android/data/<pkg>/files/Saving 在迁移后就不存在了。
 * 这里两个都试，日志打出最终选中的那个（v97: saveRoot=...），以后不管游戏怎么迁都不会再错。
 */
static const char* g_saveRoot = NULL;
static const char* save_root(void){
    if (g_saveRoot) return g_saveRoot;
    static char cand[2][256];
    static char chosen[256];
    snprintf(cand[0], sizeof(cand[0]), "%s/Saving", mediaDir());
    snprintf(cand[1], sizeof(cand[1]), "%s/Saving", g_dataDir);
    char b[300];
    for (int i = 0; i < 2; i++){
        DIR* d = opendir(cand[i]);
        snprintf(b,sizeof(b),"v97: probe saveRoot '%s' -> %s\n", cand[i], d ? "OK" : "no");
        log_app(b);
        if (d){ closedir(d); snprintf(chosen,sizeof(chosen),"%s",cand[i]); g_saveRoot = chosen; return g_saveRoot; }
    }
    log_app("v97: no save root at all\n");
    return NULL;
}

static const char* pick_world_name(char* out, int cap){
    const char* root = save_root();
    char base[300];
    if (!root) return NULL;
    snprintf(base,sizeof(base),"%s/Worlds", root);
    DIR* d = opendir(base);
    if (!d) return NULL;
    struct dirent* e; const char* got = NULL;
    while ((e = readdir(d)) != NULL){
        if (e->d_name[0] == '.') continue;
        snprintf(out, cap, "%s", e->d_name); got = out; break;
    }
    closedir(d);
    return got;
}

/* ---- v97: 造一个临时沙盒世界 -------------------------------------------------
 * 依据 WorldReference.cs:42-61 的目录布局 + 实测 WorldSettings.txt 的真实 JSON：
 *   {"solarSystem":{"name":""},"mode":{"mode":0,"allowQuicksaves":true},
 *    "difficulty":{"difficulty":0},"playtime":{"lastPlayedTime_Ticks":<ticks>,"totalPlayTime_Seconds":0.0},
 *    "cheats":{...全 false}}
 * WorldsMenu.OpenWorld() 要求 worldPersistentPath（= Persistent/）先存在，否则不调 EnterWorld，
 * 所以这里把 Persistent/ 一起建出来。退出时删掉整个目录。
 */
static int ensure_tmp_world(char* out, int cap){
    const char* root = save_root();
    if (!root) return 0;
    char wdir[300], sub[340], txt[420];
    snprintf(wdir,sizeof(wdir),"%s/Worlds/sfsmp_tmp", root);
    snprintf(sub,sizeof(sub),"%s/Persistent", wdir);
    snprintf(txt,sizeof(txt),"%s/WorldSettings.txt", wdir);
    mkdir(wdir, 0770);
    mkdir(sub, 0770);
    long long ticks = (long long)(time(NULL)) * 10000000LL + 621355968000000000LL;  /* unix -> .NET ticks */
    char json[512];
    snprintf(json,sizeof(json),
        "{\"solarSystem\":{\"name\":\"\"},\"mode\":{\"mode\":0,\"allowQuicksaves\":true},"
        "\"difficulty\":{\"difficulty\":0},"
        "\"playtime\":{\"lastPlayedTime_Ticks\":%lld,\"totalPlayTime_Seconds\":0.0},"
        "\"cheats\":{\"infiniteFuel\":false,\"noAtmosphericDrag\":false,\"unbreakableParts\":false,"
        "\"noGravity\":false,\"noHeatDamage\":false,\"noBurnMarks\":false,"
        "\"infiniteBuildArea\":false,\"partClipping\":false}}",
        ticks);
    int fd = open(txt, O_WRONLY|O_CREAT|O_TRUNC, 0660);
    if (fd < 0){ char b[200]; snprintf(b,sizeof(b),"v97: open WorldSettings FAILED path=%s\n", txt); log_app(b); return 0; }
    ssize_t w = write(fd, json, (size_t)strlen(json)); close(fd);
    char b[520]; snprintf(b,sizeof(b),"v97: tmp world '%s' written=%d json=%s\n", wdir, (int)w, json); log_app(b);
    snprintf(out, cap, "%s", "sfsmp_tmp");
    return 1;
}

static void log_exc(void* exc, const char* tag){
    if (!exc) return;
    char b[700];
    void* cls = g_mObjGetClass ? g_mObjGetClass(exc) : NULL;
    void* m = NULL; void* c = cls;
    for (int i = 0; i < 8 && c && g_gm; i++){ m = g_gm(c,"get_Message",0); if (m) break; c = g_mClassParent ? g_mClassParent(c) : NULL; }
    snprintf(b,sizeof(b),"%s exc=%p cls=%p getMsg=%p\n", tag, exc, cls, m); log_app(b);
    if (m && g_ri){ void* e = NULL; void* s = g_ri(m, exc, NULL, &e);
        if (s && !e){ char t[280] = ""; u16_to_ascii(s, t, sizeof(t));
            snprintf(b,sizeof(b),"%s msg='%s'\n", tag, t); log_app(b); } }
    { void* m2 = NULL; void* c2 = cls;
      for (int i = 0; i < 8 && c2 && g_gm; i++){ m2 = g_gm(c2,"get_StackTrace",0); if (m2) break; c2 = g_mClassParent ? g_mClassParent(c2) : NULL; }
      if (m2 && g_ri){ void* e = NULL; void* s = g_ri(m2, exc, NULL, &e);
          if (s && !e){ char t[600] = ""; u16_to_ascii(s, t, sizeof(t));
              snprintf(b,sizeof(b),"%s stack='%s'\n", tag, t); log_app(b); } } }
}

static int enter_world_official(void){
    char b[256];
    if (!g_mEnterWorld || !g_sNew || !g_ri){
        log_app("v97: EnterWorld plumbing missing\n"); return 0;
    }
    /* v97: 优先用我们自己的临时沙盒世界（不碰玩家存档、不会串味）；造不出来才回退到玩家已有世界 */
    char wn[128]; wn[0] = 0;
    if (ensure_tmp_world(wn, sizeof(wn))) {
        g_tmpWorldMade = 1;
        snprintf(b,sizeof(b),"v97: using TMP sandbox world '%s'\n", wn); log_app(b);
    } else if (pick_world_name(wn, sizeof(wn))) {
        snprintf(b,sizeof(b),"v97: tmp world failed, fallback to existing '%s'\n", wn); log_app(b);
    } else {
        log_app("v97: no world at all -> abort enter\n"); return 0;
    }
    void* s = g_sNew(wn);
    snprintf(b,sizeof(b),"v97: EnterWorld('%s') str=%p\n", wn, s); log_app(b);
    /* 关键：WorldsMenu.OpenWorld() 的写法是
     *   EnterWorld(name, delegate { Base.sceneLoader.LoadWorldScene(); })
     * 即「切场景」必须发生在 callback 里面。v83 的回调只打日志不切场景，
     * 所以场景永远不动 —— 这就是 v83 失败的唯一原因，v97 把它补上。 */
    void* act = make_enter_action();               /* v97: 真 Action，回调里切场景 */
    void* params[2]; params[0] = s; params[1] = act;
    void* exc = NULL;
    void* r = g_ri(g_mEnterWorld, NULL, params, &exc);
    snprintf(b,sizeof(b),"v97: EnterWorld ret=%p exc=%p act=%p\n", r, exc, act); log_app(b);
    if (exc) { log_exc(exc, "v97:"); }
    g_enterTried = 1;
    g_enterWait = 0;                 /* v97: 不再走 fallback（直调 LoadWorldScene 会冻结世界） */
    g_enterFallback = 0;
    return 1;
}

/* ================= v74: 在手机端生成远端火箭 =================
 * 数据来源：mp_net.c 解出的 CreateRocket 整棵零件树（mp_types.h: mp_rrocket_t）。
 * 生成方式：手工填一个 RocketSave，再调游戏自己的
 *   SFS.World.WQPAXLGGCOAUZEOA.LoadRocket(RocketSave, out bool)   RVA 0x18F31D8
 * 让游戏自己建零件/关节/分级，避免我们手工拼 Part 组件。
 * 偏移依据 dump.cs: RocketSave 0x10/0x18/0x20/0x24/0x28/0x2C/0x30/0x38/0x40/0x48、
 * PartSave 0x10..0x48、JointSave 0x10/0x14、StageSave 0x10/0x18、LocationData 0x10/0x18/0x28。
 */
static void* g_spawnerCls=NULL, *g_mLoadRocket=NULL;
static void* g_locDataCls=NULL, *g_partSaveCls=NULL, *g_jointSaveCls=NULL, *g_stageSaveCls=NULL;
static void* g_int32Cls=NULL, *g_oriCls=NULL;
static int g_selfTested = 0;
static void* g_mLocDataCtor=NULL, *g_mPartSaveCtor=NULL, *g_mJointSaveCtor=NULL, *g_mStageSaveCtor=NULL;
static void* g_mRsCtor0=NULL, *g_mOriCtor=NULL;
static void* (*g_elemClsOf)(void*)=NULL;
static int   g_rrSpawned[MP_RR_MAX];
/* v110: g_rrRocket[] 已前置声明在 publish_local_pose 之前，这里不再重复定义 */
static int   g_rrTries[MP_RR_MAX];
static int   g_rrSimOff[MP_RR_MAX];   /* v78: 已关掉物理模拟（远端火箭纯视觉） */
/* v77: 远端火箭位姿跟随需要的 Rigidbody2D 方法（运行时从对象自身类里解析） */
static void* g_mRbGetPos=NULL, *g_mRbSetPos=NULL, *g_mRbSetVel=NULL, *g_mRbSetRot=NULL, *g_mRbSetAng=NULL, *g_mRbSetSim=NULL;
static int   g_rrDiagDone = 0;
static int   g_rrSyncN = 0;
static int   g_rrSetValLogged = 0;   /* v88: 首次成功 set_Value 的日志开关 */

static int rr_snapshot(void** out, int cap){
    if(!out || cap<=0) return 0;
    if(!g_findMethod || !g_typeObj || !g_alg) return 0;
    void* params[1]; params[0]=g_typeObj; void* e=NULL;
    void* arr=g_ri(g_findMethod,NULL,params,&e);
    if(!arr || e) return 0;
    /* v116: 数组元素个数来自 il2cpp_array_length；把它和 cap 夹取后，
     * 还要保证 +0x20 数据区本身不会越界读 —— 之前 out[] 是调用方栈上的定长数组，
     * 一旦 n 算错就直接把栈写穿，正是 hook_update 里 SIGSEGV(x19 被破坏) 的典型来源。 */
    int n=(int)g_alg(arr);
    if(n<0) n=0; if(n>cap) n=cap;
    void** d=(void**)((char*)arr+0x20);
    for(int i=0;i<n;i++){
        void* v=d[i];
        /* 只有真的是 il2cpp 对象（非 0 且对齐）才收；垃圾值一律丢弃。 */
        if(!v || (((uintptr_t)v) & 7)) continue;
        if(g_mObjGetClass && !g_mObjGetClass(v)) continue;
        out[i]=v;
    }
    return n;
}

static void* rr_find_added(void** snap, int nsnap){
    void* cur[64]; int n=rr_snapshot(cur,64);
    if(nsnap<0) nsnap=0; if(nsnap>64) nsnap=64;
    for(int i=0;i<n;i++){
        if(!cur[i]) continue;                 /* v116: rr_snapshot 可能留下空槽 */
        int seen=0;
        for(int k=0;k<nsnap;k++){ if(snap[k]==cur[i]){ seen=1; break; } }
        if(!seen) return cur[i];
    }
    return NULL;
}

static void* rr_mk_part(mp_rpart_t* pt){
    if(!g_partSaveCls || !g_mNewObj || !g_sNew) return NULL;
    void* ps=g_mNewObj(g_partSaveCls);
    if(!ps) return NULL;
    if(g_mPartSaveCtor){ void* e=NULL; g_ri(g_mPartSaveCtor, ps, NULL, &e); if(e){ log_exc(e,"v74: PartSave ctor"); } }
    *(void**)((char*)ps+0x10)=g_sNew(pt->name);
    *(float*)((char*)ps+0x18)=pt->x;
    *(float*)((char*)ps+0x1c)=pt->y;
    *(float*)((char*)ps+0x28)=pt->temp;
    if(g_oriCls){
        void* ori=g_mNewObj(g_oriCls);
        if(ori){
            if(g_mOriCtor){ void* e2=NULL; g_ri(g_mOriCtor, ori, NULL, &e2); if(e2){ log_exc(e2,"v74: Orientation ctor"); } }
            *(float*)((char*)ori+0x10)=pt->ox;
            *(float*)((char*)ori+0x14)=pt->oy;
            *(float*)((char*)ori+0x18)=pt->oz;
            *(void**)((char*)ps+0x20)=ori;
        }
    }
    return ps;
}

/* 对端 part id 是"确定性哈希"(IDExtensions.DeterministicPartID)，不是数组下标 —— 必须映射 */
static int rr_idx_of(mp_rrocket_t* rr, int pid){
    int i; if(pid < 0) return -1;
    for(i=0;i<rr->nparts;i++) if(rr->parts[i].id == pid) return i;
    return -1;
}

/* 服务端转发可能丢 part id（全是 0），也可能把 1 基下标当 id 发下来。
 * 不能只凭「有没有 0」猜，改成**按匹配数投票**：id 查表 / 1 基下标 / 0 基下标，
 * 哪种解释能对上最多关节就用哪种，减少「零件散落」。 */
#define RR_MAP_ID 0
#define RR_MAP_1B 1
#define RR_MAP_0B 2
static int rr_map_mode(mp_rrocket_t* rr){
    int m_id = 0, m_1 = 0, m_0 = 0, i;
    if (rr->nparts <= 0) return RR_MAP_1B;
    for (i = 0; i < rr->njoints; i++) {
        int a = rr->joints[i].a, b = rr->joints[i].b;
        if (rr_idx_of(rr, a) >= 0 && rr_idx_of(rr, b) >= 0) m_id++;
        if (a >= 1 && a <= rr->nparts && b >= 1 && b <= rr->nparts) m_1++;
        if (a >= 0 && a < rr->nparts && b >= 0 && b < rr->nparts) m_0++;
    }
    if (m_id > 0 && m_id >= m_1 && m_id >= m_0) return RR_MAP_ID;
    return (m_1 >= m_0) ? RR_MAP_1B : RR_MAP_0B;
}
static int rr_jidx(mp_rrocket_t* rr, int v, int mode){
    if (mode == RR_MAP_ID) return rr_idx_of(rr, v);
    if (mode == RR_MAP_1B) return (v >= 1 && v <= rr->nparts) ? (v - 1) : -1;
    return (v >= 0 && v < rr->nparts) ? v : -1;
}

static void* rr_build_save(mp_rrocket_t* rr){
    char b2[200];
    if(!g_rsCls || !g_mNewObj || !g_sNew || !g_arrayNew) return NULL;
    void* rs=g_mNewObj(g_rsCls);
    if(!rs) return NULL;
    if(g_mRsCtor0){ void* e=NULL; g_ri(g_mRsCtor0, rs, NULL, &e); if(e){ log_exc(e,"v74: RocketSave ctor"); return NULL; } }
    *(void**)((char*)rs+0x10)=g_sNew(rr->name[0] ? rr->name : "MP-Remote");
    if(g_locDataCls){
        void* ld=g_mNewObj(g_locDataCls);
        if(ld){
            if(g_mLocDataCtor){ void* e=NULL; g_ri(g_mLocDataCtor, ld, NULL, &e); if(e){ log_exc(e,"v74: LocationData ctor"); } }
            *(void**)((char*)ld+0x10)=g_sNew(rr->planet[0] ? rr->planet : "Earth");
            *(double*)((char*)ld+0x18)=rr->x;  *(double*)((char*)ld+0x20)=rr->y;
            *(double*)((char*)ld+0x28)=rr->vx; *(double*)((char*)ld+0x30)=rr->vy;
            *(void**)((char*)rs+0x18)=ld;
        }
    }
    *(float*)((char*)rs+0x20)=rr->rot;
    *(float*)((char*)rs+0x24)=rr->av;
    *(unsigned char*)((char*)rs+0x28)=(unsigned char)(rr->throttle_on ? 1 : 0);
    *(float*)((char*)rs+0x2C)=rr->throttle;
    *(unsigned char*)((char*)rs+0x30)=(unsigned char)(rr->rcs ? 1 : 0);

    /* 注意：游戏侧对 parts/joints/stages 用 LINQ/foreach，**null 会抛
     * ArgumentNullException 'Value cannot be null. Parameter name: source'** ——
     * 即使数量为 0 也必须给空数组。 */
    if(g_partSaveCls){
        void* pa=g_arrayNew(g_partSaveCls,(uintptr_t)rr->nparts);
        if(pa){ void** d=(void**)((char*)pa+0x20); int i;
            for(i=0;i<rr->nparts;i++) d[i]=rr_mk_part(&rr->parts[i]);
            *(void**)((char*)rs+0x38)=pa; }
    }
    if(g_jointSaveCls && rr->njoints<=0){
        /* v82: 空关节也必须给数组，null 会让游戏的 foreach 抛异常 */
        void* ja0=g_arrayNew(g_jointSaveCls,0);
        if(ja0) *(void**)((char*)rs+0x40)=ja0;
    }
    if(g_jointSaveCls && rr->njoints>0){
        int mode=rr_map_mode(rr), valid=0, i;
        for(i=0;i<rr->njoints;i++)
            if(rr_jidx(rr,rr->joints[i].a,mode)>=0 && rr_jidx(rr,rr->joints[i].b,mode)>=0) valid++;
        snprintf(b2,sizeof(b2),"v130: joints pid=(%d,%d) mode=%d mapped=%d/%d nparts=%d id0=%d\n",
                 rr->joints[0].a, rr->joints[0].b, mode, valid, rr->njoints, rr->nparts,
                 rr->nparts>0?rr->parts[0].id:0);
        log_app(b2);
        { void* ja=g_arrayNew(g_jointSaveCls,(uintptr_t)valid);
          if(ja){ void** d=(void**)((char*)ja+0x20); int k=0;
            for(i=0;i<rr->njoints && k<valid;i++){
                int ia=rr_jidx(rr,rr->joints[i].a,mode), ib=rr_jidx(rr,rr->joints[i].b,mode);
                if(ia<0||ib<0) continue;
                void* je=g_mNewObj(g_jointSaveCls);
                if(!je) continue;
                if(g_mJointSaveCtor){ void* e=NULL; g_ri(g_mJointSaveCtor, je, NULL, &e); (void)e; }
                *(int*)((char*)je+0x10)=ia;
                *(int*)((char*)je+0x14)=ib;
                d[k++]=je;
            }
            *(void**)((char*)rs+0x40)=ja; }
        }
    }
    if(g_stageSaveCls){
        void* sa=g_arrayNew(g_stageSaveCls,(uintptr_t)rr->nstages);
        if(sa){ void** d=(void**)((char*)sa+0x20); int i;
            for(i=0;i<rr->nstages;i++){
                void* se=g_mNewObj(g_stageSaveCls);
                if(!se){ d[i]=NULL; continue; }
                if(g_mStageSaveCtor){ void* e=NULL; g_ri(g_mStageSaveCtor, se, NULL, &e); (void)e; }
                *(int*)((char*)se+0x10)=rr->stages[i].stage_id;
                if(g_int32Cls && g_arrayNew){
                    int mode2=rr_map_mode(rr), n=0, k;
                    for(k=0;k<rr->stages[i].n;k++) if(rr_jidx(rr,rr->stages[i].ids[k],mode2)>=0) n++;
                    void* ia=g_arrayNew(g_int32Cls,(uintptr_t)n);
                    if(ia){ int* p=(int*)((char*)ia+0x20); int w=0;
                        for(k=0;k<rr->stages[i].n;k++){ int ix=rr_jidx(rr,rr->stages[i].ids[k],mode2); if(ix>=0) p[w++]=ix; }
                        *(void**)((char*)se+0x18)=ia; }
                }
                d[i]=se;
            }
            *(void**)((char*)rs+0x48)=sa; }
    }
    return rs;
}

static void rr_sysstr(void* s, char* out, int cap){
    out[0]=0; if(!s) return;
    int n=*(int*)((char*)s+0x10);
    if(n<0) n=0; if(n>=cap) n=cap-1;
    { unsigned short* p=(unsigned short*)((char*)s+0x14); int i;
      for(i=0;i<n;i++) out[i]=(char)(p[i]&0xFF); }
    out[n]=0;
}

/* v75 自检：用游戏自己产出的 RocketSave 调一次 LoadRocket，验证"调用约定"本身是否正确 */
static void rr_selftest(void){
    char b[240], tmp[64];
    void* snap[8]; int n=rr_snapshot(snap,8);
    snprintf(b,sizeof(b),"v74: SELFTEST rockets=%d rsCls=%p ctor1=%p\n", n, g_rsCls, g_mRsCtor); log_app(b);
    if(n<=0 || !g_rsCls || !g_mRsCtor || !g_mNewObj) return;
    void* rs=g_mNewObj(g_rsCls); if(!rs) return;
    void* a1[1]; a1[0]=snap[0]; void* e=NULL;
    g_ri(g_mRsCtor, rs, a1, &e);
    snprintf(b,sizeof(b),"v74: SELFTEST save=%p ctor1exc=%p\n", rs, e); log_app(b);
    if(e){ log_exc(e,"v74: selftest ctor1"); return; }
    { void* parts=*(void**)((char*)rs+0x38); void* joints=*(void**)((char*)rs+0x40);
      void* stages=*(void**)((char*)rs+0x48); void* loc=*(void**)((char*)rs+0x18);
      snprintf(b,sizeof(b),"v74: SELFTEST s.parts=%p s.joints=%p s.stages=%p s.loc=%p\n", parts,joints,stages,loc); log_app(b);
      if(parts){ snprintf(b,sizeof(b),"v74: SELFTEST nparts=%d\n", *(int*)((char*)parts+0x18)); log_app(b); }
      if(joints){ snprintf(b,sizeof(b),"v74: SELFTEST njoints=%d\n", *(int*)((char*)joints+0x18)); log_app(b); }
      if(stages){ snprintf(b,sizeof(b),"v74: SELFTEST nstages=%d\n", *(int*)((char*)stages+0x18)); log_app(b); }
      if(loc){ rr_sysstr(*(void**)((char*)loc+0x10), tmp, sizeof(tmp));
               snprintf(b,sizeof(b),"v74: SELFTEST addr='%s'\n", tmp); log_app(b); }
    }
    { unsigned char ok=0; void* a2[2]; a2[0]=rs; a2[1]=&ok; void* e2=NULL;
      g_ri(g_mLoadRocket, NULL, a2, &e2);
      snprintf(b,sizeof(b),"v74: SELFTEST LoadRocket ok=%d exc=%p\n",(int)ok,e2); log_app(b);
      if(e2) log_exc(e2,"v74: selftest LoadRocket"); }
}

/* v124: 诊断 —— 远端火箭建出来之后到底发生了什么（位置？激活？被销毁？） */
static int g_rrDbgSteps[MP_RR_MAX];
static int g_nosimChecked = 0, g_skipSimOff = 0;
/* v125: 远端火箭必须以 physicsMode=true 落点，否则 Physics.Update 每帧用轨迹把 location 覆写回去
 * （实测生成后的 location.y 稳定停在 ~2×地球半径 = 630026，永远在地表 315 km 高处 → 玩家看不见）。 */
static void* g_mSetLocAndState = NULL;
static int g_physLogged = 0;

static void rr_bind_phys(void* ph){
    if(!ph || g_mSetLocAndState) return;
    void* cls = g_mObjGetClass ? g_mObjGetClass(ph) : NULL;
    if(!cls || !g_gm) return;
    g_mSetLocAndState = g_gm(cls, "SetLocationAndState", 2);
    { char b[180]; snprintf(b,sizeof(b),"v125: phys cls=%p SetLocAndState=%p\n", cls, g_mSetLocAndState); log_app(b); }
}

static void rr_dump_rocket(const char* tag, int i, void* ro){
    char b[330], pn[32]="?";
    double px=0,py=0,vx=0,vy=0;
    int act=-1, nact=-1, nall=-1;
    if(ro){
        void* loc = (g_offLoc>=0) ? *(void**)((char*)ro+g_offLoc) : NULL;
        if(loc && g_mGetValue){ void* e=NULL; void* lv=g_ri(g_mGetValue, loc, NULL, &e);
            if(lv && !e){
                void* pl=*(void**)((char*)lv+0x18);
                if(pl) obj_name(pl, pn, sizeof(pn));
                px=*(double*)((char*)lv+0x20); py=*(double*)((char*)lv+0x28);
                vx=*(double*)((char*)lv+0x30); vy=*(double*)((char*)lv+0x38);
            } }
        if(g_mGetGO && g_mGoActive){ void* e2=NULL; void* go=g_ri(g_mGetGO, ro, NULL, &e2);
            if(go && !e2){ void* a=g_ri(g_mGoActive, go, NULL, &e2);
                if(a && !e2) act=(int)*(unsigned char*)((char*)a+0x10); } }
    }
    { void* snap[64]; nact = rr_snapshot(snap, 64); }
    if(g_mFindAll && g_typeObj){ void* e3=NULL; void* p3[1]; p3[0]=g_typeObj;
        void* a3=g_ri(g_mFindAll, NULL, p3, &e3); if(a3 && !e3) nall=(int)g_alg(a3); }
    snprintf(b,sizeof(b),"%s[%d] ro=%p act=%d planet='%s' pos=(%.2f,%.2f) vel=(%.2f,%.2f) nAct=%d nAll=%d\n",
             tag, i, ro, act, pn, px, py, vx, vy, nact, nall);
    log_app(b);
}

static void rr_try_spawn(int i){
    char b[220];
    mp_rrocket_t* rr=mp_rr_at(i);
    if(!rr || !rr->has_structure) return;
    if(g_rrSpawned[i]) return;
    if(g_rrTries[i] >= 3) return;
    if(!strstr(g_sceneName,"World_Mobile")) return;
    if(!g_mLoadRocket || !g_rsCls){ log_app("v74: LoadRocket not resolved\n"); return; }
    g_rrTries[i]++;
    void* snap[64]; int nsnap=rr_snapshot(snap,64);
    void* rs=rr_build_save(rr);
    if(!rs){ log_app("v74: RocketSave build failed\n"); return; }
    if(rr->nparts>0){
        snprintf(b,sizeof(b),"v74: part0 id=%d name='%s' pos=(%.2f,%.2f)\n",
                 rr->parts[0].id, rr->parts[0].name, rr->parts[0].x, rr->parts[0].y); log_app(b);
        if(rr->nparts>1){ snprintf(b,sizeof(b),"v74: part1 id=%d name='%s'\n", rr->parts[1].id, rr->parts[1].name); log_app(b); }
    }
    unsigned char ok=0; void* a[2]; a[0]=rs; a[1]=&ok; void* e=NULL;
    g_ri(g_mLoadRocket,NULL,a,&e);
    snprintf(b,sizeof(b),"v74: LoadRocket gid=%d parts=%d joints=%d stages=%d ok=%d exc=%p\n",
             rr->gid, rr->nparts, rr->njoints, rr->nstages, (int)ok, e);
    log_app(b);
    if(e){ log_exc(e,"v74: LoadRocket"); return; }
    /* v122: 以前这里先把 g_rrSpawned 置 1，再拿 rr_find_added 的返回值。
     * rr_find_added 找不到时返回 NULL —— 于是"生成了但没抓到手柄"，
     * rr_sync_pose 对 NULL 静默跳过、rr_check_alive 因为 !obj 也跳过，
     * 结果就是火箭建出来了却永远不动，且不会重试。用户描述"之前有过一次、复现不出来"
     * 正是这种概率性抓不到手柄的表现。
     * 现在：抓不到就当作没生成成功（不置 spawned），下一轮重试；超过 3 次打日志放弃。 */
    { void* spawned = rr_find_added(snap, nsnap);
      if(!spawned){
          snprintf(b,sizeof(b),"v122: LoadRocket ran but handle NOT found (existing=%d tries=%d) -> retry next round\n",
                   nsnap, g_rrTries[i]);
          log_app(b);
          return;   /* g_rrSpawned 保持 0，下轮重试 */
      }
      g_rrSpawned[i]=1;
      g_rrRocket[i]=spawned;
      g_rrDbgSteps[i]=8;
    }
    mp_rr_clear_dirty(i);
    snprintf(b,sizeof(b),"v122: spawned rocket=%p (existing=%d)\n", g_rrRocket[i], nsnap);
    log_app(b);
    { char tb[96]; snprintf(tb,sizeof(tb),"联机：远端火箭已同步（%d 零件）", rr->nparts); toast(tb); }
}

/* v131: 连接状态提示（toast），只做「最容易注意到」的三件事：
 * 连上 / 断开 / 连接超时。进入非世界场景时复位，方便下次重连再提示。 */
static int g_uxJoined = 0, g_uxStale = 0, g_uxNoJoin = 0, g_uxTried = 0;
static unsigned long long g_uxTryMs = 0;
static void ux_poll_conn(void){
    if(!g_ready) return;
    if(strstr(g_sceneName, "World_Mobile")){
        if(mp_is_joined() && !g_uxJoined){ g_uxJoined = 1; g_uxNoJoin = 1; toast("联机：已连接服务器"); }
        if(!mp_is_joined() && g_uxTried && !g_uxNoJoin && mp_last_rx_age_ms() > 6000){
            g_uxNoJoin = 1; toast("联机：连接超时，检查服务器地址");
        }
        if(g_uxJoined && !g_uxStale && mp_last_rx_age_ms() > 8000){
            g_uxStale = 1; toast("联机：与服务器断开");
        }
    } else {
        g_uxJoined = 0; g_uxStale = 0; g_uxNoJoin = 0;
    }
}
static void ux_connect_started(void){ g_uxTried = 1; g_uxTryMs = 1; g_uxNoJoin = 0; toast("联机：正在连接服务器…"); }

/* ================= v77: 让远端火箭跟着位姿包动 =================
 * 生成时只定了一次位置；此后每帧把 mp_net 解出的位姿写回去。
 * 写两处：
 *   1) Rocket.location（SFS.World.Location：0x10 time / 0x18 planet / 0x20 Double2 pos / 0x30 Double2 vel）
 *      —— 游戏自己的逻辑/存档/HUD 读这个；
 *   2) Rocket.rb2d（字段 0x60）的 position/velocity/rotation/angularVelocity
 *      —— Unity 渲染与物理读这个。
 * Rigidbody2D 的方法按对象自己的类解析，避免依赖 UnityEngine.CoreModule 的类名。
 */
static void rr_bind_rb(void* rb){
    if(!rb || g_mRbSetPos) return;
    void* cls = g_mObjGetClass ? g_mObjGetClass(rb) : NULL;
    if(!cls || !g_gm){ char b[120]; snprintf(b,sizeof(b),"v77: rb bind fail cls=%p\n", cls); log_app(b); return; }
    g_mRbGetPos = g_gm(cls,"get_position",0);
    g_mRbSetPos = g_gm(cls,"set_position",1);
    /* v113: 这一版 Unity 把 Rigidbody2D.set_velocity 改名成了 set_linearVelocity
     * （dump.cs:584133 set_linearVelocity(Vector2)），老的 set_velocity 在 2D 类上不存在，
     * 所以一直绑到 NULL —— 速度写不进去，远端火箭只会瞬移不会"飞"。
     * 先试新名，再回退旧名。 */
    g_mRbSetVel = g_gm(cls,"set_linearVelocity",1);
    if(!g_mRbSetVel) g_mRbSetVel = g_gm(cls,"set_velocity",1);
    g_mRbSetRot = g_gm(cls,"set_rotation",1);
    g_mRbSetAng = g_gm(cls,"set_angularVelocity",1);
    g_mRbSetSim = g_gm(cls,"set_simulated",1);
    { char b[260]; snprintf(b,sizeof(b),"v78: rb cls=%p getPos=%p setPos=%p setVel=%p setRot=%p setAng=%p setSim=%p\n",
        cls, g_mRbGetPos, g_mRbSetPos, g_mRbSetVel, g_mRbSetRot, g_mRbSetAng, g_mRbSetSim); log_app(b); }
}

static void rr_diag_space(void){
    void* snap[8]; int n=rr_snapshot(snap,8); char b[300];
    snprintf(b,sizeof(b),"v77: diag rockets=%d\n", n); log_app(b);
    for(int i=0;i<n && i<4;i++){
        void* ro=snap[i]; if(!ro) continue;
        void* rb=*(void**)((char*)ro+0x60);
        void* loc = (g_offLoc>=0) ? *(void**)((char*)ro+g_offLoc) : NULL;
        double lx=0,ly=0;
        if(loc && g_mGetValue){ void* e0=NULL; void* lv0=g_ri(g_mGetValue, loc, NULL, &e0);
            if(lv0 && !e0){ lx=*(double*)((char*)lv0+0x20); ly=*(double*)((char*)lv0+0x28); } }
        /* v110: 删掉 Rigidbody2D.get_position 的诊断分支。
         * 原因：get_position 返回 Vector2，il2cpp 下这类"小结构体返回值"可能走隐藏指针参数，
         * 我们拿到的东西不一定是装箱对象，g_unbox 之后按 float[0]/float[1] 强解会读写错地址。
         * 这是复开渲染后的头号炸弹（而且 f>700 一进世界就会跑到），直接去掉；
         * rb 侧的位置由 rr_sync_pose 里 set_position 的调用保证，不需要在这里读。 */
        (void)rb;
        snprintf(b,sizeof(b),"v77: diag[%d] ro=%p loc=(%.2f,%.2f)\n", i, ro, lx, ly); log_app(b);
    }
}

/* v78: 远端火箭改成"纯视觉"：
 *  1) 关掉 Rigidbody2D 的物理模拟（simulated=false），否则游戏自己的重力和关节每帧把火箭拽走，
 *     表现就是"位置全错 / 火箭躺地上"；
 *  2) 位姿不再直接写最近一个包，而是写 now-120ms 的插值（mp_rr_sample），网络抖动不直接反映到画面。
 */
static void rr_sync_pose(int i){
    if(i<0 || i>=MP_RR_MAX) return;
    mp_rrocket_t* rr=mp_rr_at(i);
    if(!rr || !rr->has_structure || !rr->valid) return;
    if(!g_rrSpawned[i]) return;
    void* ro=g_rrRocket[i];
    double ix, iy; float irot;
    if(!ro){ g_rrSpawned[i]=0; g_rrTries[i]=0; g_rrSimOff[i]=0; return; }
    /* v82: 每帧写回前先确认对象还是火箭类；被游戏销毁后内存可能已被复用，直接写会 SIGSEGV */
    if(g_mObjGetClass && g_rocketCls){
        void* rc = g_mObjGetClass(ro);
        if(rc != g_rocketCls){ g_rrSpawned[i]=0; g_rrRocket[i]=NULL; g_rrTries[i]=0; g_rrSimOff[i]=0;
            { char b[150]; snprintf(b,sizeof(b),"v82: rocket[%d] dead (cls=%p) -> skip\n", i, rc); log_app(b); }
            return; }
    }
    if(!mp_rr_sample(i, &ix, &iy, &irot)){ ix=rr->x; iy=rr->y; irot=rr->rot; }
    /* v154: 角度归一化到 (-180,180]，否则插值跨 0/360 会多转一整圈 */
    if(irot > 180.0f) irot -= 360.0f; else if(irot <= -180.0f) irot += 360.0f;
    /* v88: 绝对不要直接往 Location 组件本体写 double。
     * Location 组件布局：planet 0x20 / position(Double2_Local=Obs 引用) 0x28 / velocity 0x30，
     * 旧代码写 loc+0x20/0x28 就是拿位置 double 覆盖 planet 与 Obs 指针，
     * 游戏随后解引用该指针 -> SIGSEGV fault addr 0x0076523140000010（0x4076523140000000 = double 357.1）。
     * 正确做法：走官方 set_Value(Location)，只改 Location DTO 里的 position/velocity（纯值字段）。 */
    if(g_offLoc>=0){
        void* loc=*(void**)((char*)ro+g_offLoc);
        /* v110: loc 拿不到就是"这个对象已经不是我认识的那枚火箭了"（内存被复用/被销毁），
         * 立刻停手并记一次 strike，绝不硬写下去 —— 这是历史 SIGSEGV 的同类路径。 */
        if(!loc || !g_mGetValue || !g_mSetValue){
            if(g_rrStrikes < 3){ g_rrStrikes++;
                { char b4[170]; snprintf(b4,sizeof(b4),"v110: strike %d/3 loc=%p get=%p set=%p\n", g_rrStrikes, loc, g_mGetValue, g_mSetValue); log_app(b4); } }
            g_rrSpawned[i]=0; g_rrRocket[i]=NULL; g_rrSimOff[i]=0;
            return;
        }
        {
            void* e5=NULL;
            void* lv=g_ri(g_mGetValue, loc, NULL, &e5);
            if(lv && !e5){
                {   /* v153: 先读游戏当前的权威位置，再决定"瞬移对齐"还是"速度追" */
                    double cx=*(double*)((char*)lv+0x20), cy=*(double*)((char*)lv+0x28);
                    double dx2=ix-cx, dy2=iy-cy, err2=dx2*dx2+dy2*dy2;
                    g_pvx = rr->vx + dx2*25.0;
                    g_pvy = rr->vy + dy2*25.0;
                    g_snap = (err2 > 4.0) ? 1 : 0;      /* 偏差 > 2 m 才整帧对齐 */
                    if(g_snap) g_snapN++; else g_pursueN++;
                }
                *(double*)((char*)lv+0x20)=ix;      *(double*)((char*)lv+0x28)=iy;
                *(double*)((char*)lv+0x30)=rr->vx;  *(double*)((char*)lv+0x38)=rr->vy;
                void* av[1]; av[0]=lv; e5=NULL;
                g_ri(g_mSetValue, loc, av, &e5);
                if(!g_rrSetValLogged){ g_rrSetValLogged=1;
                    char b3[200]; snprintf(b3,sizeof(b3),"v88: set_Value ok loc=%p lv=%p pos=(%.2f,%.2f) exc=%p\n", loc, lv, ix, iy, e5); log_app(b3); }
            }
        }
    }
    /* v125: 关键 —— 用游戏自己的 Physics.SetLocationAndState(loc, physicsMode:true) 落点。
     * physicsMode=false 时 Physics.Update() 每帧执行 location.Value = trajectory.GetLocation(worldTime)，
     * 把我们写进去的位姿整帧丢掉，并且重建出来的位置在 y≈2×R；physicsMode=true 后
     * 游戏反过来从 rb2d 读位置，我们写的 rb2d pos/vel 才是权威。 */
    { void* ph = *(void**)((char*)ro + 0xF8);
      if(ph){
          rr_bind_phys(ph);
          if(g_mSetLocAndState && g_offLoc >= 0){
              void* loc2 = *(void**)((char*)ro + g_offLoc);
              void* e6 = NULL;
              void* lv2 = (loc2 && g_mGetValue) ? g_ri(g_mGetValue, loc2, NULL, &e6) : NULL;
              if(lv2 && !e6){
                  *(double*)((char*)lv2+0x20)=ix;     *(double*)((char*)lv2+0x28)=iy;
                  *(double*)((char*)lv2+0x30)=rr->vx; *(double*)((char*)lv2+0x38)=rr->vy;
                  int32_t one = 1; void* a2[2]; a2[0]=lv2; a2[1]=&one; void* e7=NULL;
                  g_ri(g_mSetLocAndState, ph, a2, &e7);
                  if(!g_physLogged){ g_physLogged = 1; char b5[220];
                      snprintf(b5,sizeof(b5),"v125: SetLocationAndState(loc,true) pos=(%.2f,%.2f) vel=(%.2f,%.2f) exc=%p\n",
                               ix, iy, rr->vx, rr->vy, e7);
                      log_app(b5); }
              }
          }
      } }
    void* rb=*(void**)((char*)ro+0x60);
    if(!rb) return;
    rr_bind_rb(rb);
    void* e=NULL;
    /* v127: 不再调用 set_simulated(false)。
     * bug-hunter 证据（logcat 三次 NullReference at Physics.set_PhysicsMode ← Obs<PhysicsMode> ← Start，
     * 均紧随 REMOTE CREATE）：SetLocationAndState(...,true) 之后再把 rb2d.simulated 关掉，会让
     * Physics.PhysicsMode 的 getter 回 false，而 trajectory 已经被置 null（Physics.cs:75），
     * 随后 set_PhysicsMode / trajectory.CheckEncounters（Physics.cs:50/194）就炸。
     * 位置和速度我们每帧自己写，本来就不需要关物理。 */
    (void)g_mRbSetSim; (void)g_skipSimOff;
    if(g_mRbSetPos && g_snap){ float p[2]; p[0]=(float)ix; p[1]=(float)iy; void* a[1]; a[0]=p; g_ri(g_mRbSetPos,rb,a,&e); }
    if(g_mRbSetVel){ float v[2]; v[0]=(float)g_pvx; v[1]=(float)g_pvy; void* a[1]; a[0]=v; g_ri(g_mRbSetVel,rb,a,&e); }
    if(g_mRbSetRot){ float rd=irot; void* a[1]; a[0]=&rd; g_ri(g_mRbSetRot,rb,a,&e); }
    if(g_mRbSetAng){ float av=rr->av; void* a[1]; a[0]=&av; g_ri(g_mRbSetAng,rb,a,&e); }
    /* v152: 朝向修正。rb2d.rotation 只影响物理体；地图图标(mapIcon)与子物体读的是
     * transform.eulerAngles，所以远端火箭"永远朝上"、地图图标也跟着朝上。
     * 这里把同一个角度同时写进 transform.eulerAngles.z。 */
    { void* tr = (g_mGetTransform && ro) ? g_ri(g_mGetTransform, ro, NULL, &e) : NULL;
      if(tr){
          if(!g_mSetEuler){
              void* tcls = g_mObjGetClass ? g_mObjGetClass(tr) : NULL;
              if(tcls && g_gm) g_mSetEuler = g_gm(tcls, "set_eulerAngles", 1);
          }
          if(g_mSetEuler){ float eu[3]; eu[0]=0.0f; eu[1]=0.0f; eu[2]=irot;
              void* a3[1]; a3[0]=eu; void* e3=NULL; g_ri(g_mSetEuler, tr, a3, &e3);
              if(!g_rotLogged){ g_rotLogged=1; char b6[210];
                  snprintf(b6,sizeof(b6),"v152: rot in=%.2f setEuler z=%.2f exc=%p\n", irot, eu[2], e3); log_app(b6); } }
      } }
    if(g_rrSyncN < 3){ g_rrSyncN++; char b[220];
        snprintf(b,sizeof(b),"v154: sync[%d] gid=%d raw=(%.2f,%.2f) interp=(%.2f,%.2f) rot=%.1f snaps=%d pursue=%d\n",
                 i, rr->gid, rr->x, rr->y, ix, iy, irot, g_snapN, g_pursueN); log_app(b); }
}

/* v110: 运行时开关 —— 每 120 帧看一次 sfsmp-rr.txt，内容首字符 '0' 就关掉远端渲染。
 * 好处：真机上一旦发现崩/卡，不用重新编译打包，直接 adb 写个文件＋重启应用就能回退。 */
static void rr_check_toggle(void){
    const char* f = dp("sfsmp-rr.txt");
    int fd;
    if (g_rrStrikes >= 3 && g_rrEnabled) {
        g_rrEnabled = 0;
        log_app("v110: *** CIRCUIT BREAKER TRIPPED -> remote render OFF ***\n");
        return;
    }
    if (g_rrFileChecked) return;
    g_rrFileChecked = 1;
    fd = open(f, O_RDONLY);
    if (fd < 0) { log_app("v110: rr toggle file absent -> remote render ON (default)\n"); return; }
    { char c = 0; int n = (int)read(fd, &c, 1); close(fd);
      if (n > 0 && c == '0') { g_rrEnabled = 0; log_app("v110: rr toggle file says 0 -> remote render OFF\n"); }
      else { log_app("v110: rr toggle file says ON -> remote render ON\n"); } }
}

/* v110: 判断"我生成的这枚远端火箭还在不在"。
 * 老实现只用 snap[64] 抓当前火箭列表，场上超过 64 枚时我们的目标会被截掉，
 * 于是被误判成 "gone"，随后 rr_try_spawn 又重新生成一枚 —— 场上越堆越多。
 * 新实现：直接对目标的类指针做一次有效性判断 + 只在小规模时用列表比对。 */
static void rr_check_alive(void){
    int passed=0, gone=0;
    for(int i=0;i<MP_RR_MAX;i++){
        void* obj=g_rrRocket[i];
        if(!g_rrSpawned[i] || !obj) continue;
        /* 1) 类指针校验：Unity 里被销毁的 UnityEngine.Object 保留托管壳，
         *    但 il2cpp 侧的原生对象已经回收，取类指针仍可读；配合下面 2) 一起判断。 */
        void* cls = (g_mObjGetClass) ? g_mObjGetClass(obj) : NULL;
        if(!cls || !g_rocketCls || cls != g_rocketCls){
            char b[140]; snprintf(b,sizeof(b),"v110: rocket[%d] dead (cls=%p) -> respawn allowed\n", i, cls); log_app(b);
            g_rrSpawned[i]=0; g_rrRocket[i]=NULL; g_rrTries[i]=0; g_rrSimOff[i]=0;
            gone++; continue;
        }
        passed++;
    }
    /* 只在数目很小时才用完整列表比对兜底，避免 >64 时的误判 */
    if(passed > 0){
        void* snap[64]; int n=rr_snapshot(snap,64);
        if(n < 64){
            for(int i=0;i<MP_RR_MAX;i++){
                if(!g_rrSpawned[i] || !g_rrRocket[i]) continue;
                int found=0; for(int k=0;k<n;k++) if(snap[k]==g_rrRocket[i]){ found=1; break; }
                if(!found){ char b[140]; snprintf(b,sizeof(b),"v110: rocket[%d] not in list -> respawn allowed\n", i); log_app(b);
                    g_rrSpawned[i]=0; g_rrRocket[i]=NULL; g_rrTries[i]=0; g_rrSimOff[i]=0; }
            }
        } else {
            static int logged=0;
            if(!logged){ logged=1; char b[150]; snprintf(b,sizeof(b),"v110: rockets=%d >= 64, skip list check\n", n); log_app(b); }
        }
    }
}

/* ---- v97: 把服务器快照里的世界时间写进游戏 -----------------------------------
 * 对应 GameManager / WorldTime（dump.cs: SFS.World.WorldTime）的 worldTime 字段。
 * 类名混淆，用「类型名里含 WorldTime 且字段名叫 worldTime」来定位，找不到就跳过（不致命）。 */
static void* g_wtMainInst = NULL;
static void* g_wtMainField = NULL;
static int   g_wtResolved = 0;
static int   g_snapApplied = 0;

static void* g_wtCls = NULL, *g_wtType = NULL, *g_mWtGetMain = NULL, *g_offWt = NULL;
/* v97: 全类型扫描需要的 il2cpp 入口（在 worker 里解析、这里取出使用） */
static void* g_cachedImg = NULL;
static size_t (*g_imgClassCount)(void*) = NULL;
static void* (*g_imgClassGet)(void*, int) = NULL;
static void* (*g_cgfn)(void*, const char*) = NULL;
static int   (*g_fgo)(void*) = NULL;
static int world_time_resolve(void){
    if (g_wtResolved) return g_wtCls ? 1 : 0;
    g_wtResolved = 1;
    /* 反编译源码 D:\sfs-tools\ref\PCSFS\SFS.World\WorldTime.cs 里类型是 SFS.World.WorldTime，
       混淆后的类名未知 —— 这里用「静态字段 main + 实例字段 worldTime」两个特征去全类扫一遍。 */
    void* img = g_cachedImg;
    if (!img || !g_imgClassCount || !g_imgClassGet || !g_cgfn) { log_app("v97: wt: no image scan plumbing\n"); return 0; }
    size_t n = g_imgClassCount(img);
    for (size_t i = 0; i < n; i++) {
        void* c = g_imgClassGet(img, (int)i);
        if (!c) continue;
        void* fMain = g_cgfn(c, "main");
        void* fWt   = g_cgfn(c, "worldTime");
        if (fMain && fWt) {
            g_wtCls = c; g_offWt = (void*)(intptr_t)g_fgo(fWt);
            char b[200]; snprintf(b,sizeof(b),"v97: wt class found %p off=0x%x\n", c, (unsigned)(intptr_t)g_offWt); log_app(b);
            return 1;
        }
    }
    log_app("v97: wt class NOT found\n");
    return 0;
}

/* v119: PlayerController 在混淆包里**名字被改了**（dump.cs 里搜 'PlayerController' 零命中），
 * 只能按结构特征扫描：同时具备 静态 main + 实例 player + 实例 hasControl 的类即它是。
 * 权威源码 D:/sfs-tools/ref/PCSFS/SFS.World/PlayerController.cs:32-36
 *   public static PlayerController main;  public Player_Local player;  public Bool_Local hasControl; */
static int player_controller_resolve(void){
    if(g_pcCls) return 1;
    void* img = g_cachedImg;
    char b[300];
    if (!img || !g_imgClassCount || !g_imgClassGet || !g_cgfn) { log_app("v119: pc: no image scan plumbing\n"); return 0; }
    size_t n = g_imgClassCount(img);
    int scanned = 0;
    for (size_t i = 0; i < n; i++) {
        void* c = g_imgClassGet(img, (int)i);
        if (!c) continue;
        void* fMain = g_cgfn(c, "main");
        if (!fMain) continue;
        void* fPlayer = g_cgfn(c, "player");
        if (!fPlayer) continue;
        void* fCtrl = g_cgfn(c, "hasControl");
        if (!fCtrl) continue;
        void* fCam = g_cgfn(c, "cameraDistance");
        g_pcCls = c;
        g_pcMainField = fMain;
        g_offPcPlayer = (int)g_fgo(fPlayer);
        g_offPcHasCtrl = (int)g_fgo(fCtrl);
        g_offPcCamDist = fCam ? (int)g_fgo(fCam) : -1;
        snprintf(b,sizeof(b),"v119: pc class found %p main=%p offPlayer=0x%x offHasCtrl=0x%x offCamDist=0x%x (scan idx %d)\n",
                 c, fMain, g_offPcPlayer, g_offPcHasCtrl, g_offPcCamDist, (int)i);
        log_app(b);
        return 1;
    }
    (void)scanned;
    if (n > 0) { snprintf(b,sizeof(b),"v119: pc class NOT found (%d types scanned)\n", (int)n); log_app(b); }
    return 0;
}

static int world_time_push(double wt){
    char b[240];
    if (!world_time_resolve() || !g_offWt) {
        snprintf(b,sizeof(b),"v97: world time unsupported, skip (server wt=%.2f)\n", wt); log_app(b);
        return 0;
    }
    if (!g_wtMainField) g_wtMainField = g_cgfn(g_wtCls, "main");
    if (!g_wtMainInst && g_wtMainField && g_staticGet) { void* v = NULL; g_staticGet(g_wtMainField, &v); g_wtMainInst = v; }
    if (!g_wtMainInst) {
        snprintf(b,sizeof(b),"v97: wt main instance null, skip (wt=%.2f)\n", wt); log_app(b); return 0;
    }
    { double* p = (double*)((char*)g_wtMainInst + (uintptr_t)g_offWt);
      double old = *p; *p = wt;
      snprintf(b,sizeof(b),"v97: worldTime %.2f -> %.2f ptr=%p\n", old, wt, (void*)p); log_app(b); }
    return 1;
}

/* v100: 把镜头/出生点钉到发射台。失败只打日志，不崩、不阻断进世界。
 * 从 Base（MonoBehaviour -> FindObjectsOfType）拿 planetLoader(0x40) -> spaceCenter(0x60)
 * -> get_LaunchPadLocation() -> WorldView.main.SetViewLocation(loc)。 */
static void pin_view_to_launchpad(void){
    char b[260];
    if(!g_ri || !g_findMethod || !g_typeBase || !g_mGetLaunchPad || !g_mSetViewLocation || !g_worldViewMainField){
        snprintf(b,sizeof(b),"v100: pin skip (plumbing base=%p find=%p pad=%p set=%p wvF=%p)\n",
                 g_typeBase, g_findMethod, g_mGetLaunchPad, g_mSetViewLocation, g_worldViewMainField);
        log_app(b); return;
    }
    /* Base 实例 */
    void* p1[1]; p1[0] = g_typeBase; void* e = NULL;
    void* arr = g_ri(g_findMethod, NULL, p1, &e);
    int n = (arr && !e) ? (int)g_alg(arr) : -1;
    snprintf(b,sizeof(b),"v100: base instances=%d exc=%p\n", n, e); log_app(b);
    if(!arr || e || n <= 0) { log_app("v100: pin aborted (no Base)\n"); return; }
    void* base = ((void**)((char*)arr + 0x20))[0];
    /* planetLoader -> spaceCenter */
    void* pl = (g_offPlanetLoader >= 0) ? *(void**)((char*)base + g_offPlanetLoader) : NULL;
    snprintf(b,sizeof(b),"v100: base=%p planetLoader=%p offPL=%d\n", base, pl, (int)g_offPlanetLoader); log_app(b);
    if(!pl) { log_app("v100: pin aborted (no planetLoader)\n"); return; }
    void* sc = (g_offSpaceCenter >= 0) ? *(void**)((char*)pl + g_offSpaceCenter) : NULL;
    snprintf(b,sizeof(b),"v100: spaceCenter=%p offSC=%d\n", sc, (int)g_offSpaceCenter); log_app(b);
    if(!sc) { log_app("v100: pin aborted (no spaceCenter)\n"); return; }
    /* get_LaunchPadLocation() */
    void* e2 = NULL; void* loc = g_ri(g_mGetLaunchPad, sc, NULL, &e2);
    snprintf(b,sizeof(b),"v100: LaunchPadLocation=%p exc=%p\n", loc, e2); log_app(b);
    if(!loc || e2) { log_app("v100: pin aborted (no launchpad loc)\n"); return; }
    if(e2) log_exc(e2, "v100:");
    /* WorldView.main 静态实例 */
    void* wv = NULL; g_staticGet(g_worldViewMainField, &wv);
    snprintf(b,sizeof(b),"v100: WorldView.main=%p\n", wv); log_app(b);
    if(!wv) { log_app("v100: pin aborted (no WorldView.main)\n"); return; }
    void* av[1]; av[0] = loc; void* e3 = NULL;
    void* r = g_ri(g_mSetViewLocation, wv, av, &e3);
    snprintf(b,sizeof(b),"v100: SetViewLocation ret=%p exc=%p\n", r, e3); log_app(b);
    if(e3) log_exc(e3, "v100:");
    else { g_viewPinned = 1; log_app("v100: *** view pinned to launch pad ***\n"); }
}

static void hook_update(void* self){
    int f = __sync_add_and_fetch(&g_frame, 1);
    if (g_ready && g_do_clone){ g_do_clone = 0; if (read_mode() == 5) native_window_test(); else if (read_mode() == 6) native_mods_open(); else if (read_mode() == 9) noj_open(1); else if (read_mode() == 11) native_text_input();  else mainmenu_button(); }
    if (g_ready && !g_cloneGo && (f % 3) == 0) {   /* v149: 90 帧(1.5s) 太慢，菜单出来后才补按钮 */
        if (g_mmInst == NULL && g_mmCls && g_mmMainField && g_staticGet) { void* v = NULL; g_staticGet(g_mmMainField, &v); g_mmInst = v; }
        int _rm = read_mode(); if (g_mmInst && (_rm == 3 || _rm == 9 || _rm == 10 || _rm == 11 || _rm == 12 || _rm == 20)) { log_app("v41: auto-summon (main menu ready)\n"); mainmenu_button(); }
    }
    if (g_ready && (f % 120) == 0) { int md = read_mode(); if (md == 9) noj_open(1); }
    if ((f % 300) == 0 && g_mmInst && g_mGoActive && g_mGetGO){
        void* e5=NULL; void* g5=g_ri(g_mGetGO, g_mmInst, NULL, &e5);
        if(g5){ void* a5=g_ri(g_mGoActive, g5, NULL, &e5);
            int val = (a5 && !e5) ? (int)*(unsigned char*)((char*)a5 + 0x10) : -1;
            char ab[120]; snprintf(ab,sizeof(ab),"v48: menuGO active=%d exc=%p\n", val, e5); log_app(ab); }
    }
    if ((f % 300) == 0) { char hb[160]; snprintf(hb,sizeof(hb),"v44: hb f=%d mmInst=%p mmField=%p sg=%p mode=%d\n", f, g_mmInst, g_mmMainField, (void*)g_staticGet, read_mode()); log_app(hb); }
    if (g_ready && (f % 3) == 0) scene_poll();
    if (g_ready && (f % 15) == 0 && !g_autoStarted && f > 480 && g_srcBtn && g_mEvtInvoke
        && read_mode() != 20 && strstr(g_sceneName, "Home_Mobile")) {
        void* ev = *(void**)((char*)g_srcBtn + 0x68);
        if(ev){ void* e8=NULL; g_ri(g_mEvtInvoke, ev, NULL, &e8);
            char ab[96]; snprintf(ab,sizeof(ab),"v59: auto-enter world exc=%p\n", e8); log_app(ab);
            g_autoStarted = 1; }
    }
    if (g_ready && (f % 15) == 0 && g_tiInst && g_mGoActive && g_mGetGO) {
        void* e7=NULL; void* g7=g_ri(g_mGetGO, g_tiInst, NULL, &e7);
        if(g7){ void* a7=g_ri(g_mGoActive, g7, NULL, &e7);
            int open = (a7 && !e7) ? (int)*(unsigned char*)((char*)a7+0x10) : 0;
            if(g_tiWasOpen && !open){
                const char* ep = g_tiVal[0][0] ? g_tiVal[0] : "192.168.1.5:9806";
                char cb[128]; snprintf(cb,sizeof(cb),"v55: dialog closed -> connect '%s'\n", ep); log_app(cb);
                if (read_mode() == 20) { log_app("v64: dialog closed -> join + enter world (mode 20)\n"); connect_and_enter_world(); }
                else { ux_connect_started(); mp_net_connect(ep); }
            }
            g_dialogOpen = open;
            if (open != g_tiWasOpen) { char db[120]; snprintf(db,sizeof(db),"v64: dialog open=%d (was %d)\n", open, g_tiWasOpen); log_app(db); }
            g_tiWasOpen = open;
        }
    }
    if (g_ready && g_enterReq && (f % 60) == 0 && g_enterLogs < 5) {
        char eb[120]; snprintf(eb,sizeof(eb),"v64: after-enter scene='%s'\n", g_sceneName); log_app(eb);
        if (++g_enterLogs >= 5) force_timescale1("after-enter");
    }
    /* v103: 进世界后打一次火箭清单，确认本机火箭对象在哪、位置是多少 */
    if (g_ready && !g_dumpDone && strstr(g_sceneName, "World_Mobile") && (f % 30) == 0 && g_rockCls) {
        g_dumpDone = 1; dump_rockets();
    }
    if (g_ready && (f % 30) == 0 && strstr(g_sceneName, "World_Mobile") && g_rockCls) publish_local_pose();
    if ((f % 15) == 0) ux_poll_conn();
    /* ---- v97: 刚进世界时把服务器快照的「世界时间」推给游戏 -------------------
     * 对应你的要求「服务器下发的那包当模板」：WorldSnapshot 里有 world_time / difficulty，
     * 火箭内容走各自 CreateRocket（一直在收）。这里只补世界时间，让两边时钟对齐。 */
    if (g_ready && strstr(g_sceneName, "World_Mobile") && !g_snapApplied && (f % 10) == 0) {
        double wt = mp_server_world_time();
        if (wt > 0.0) {
            g_snapApplied = 1;
            char sb[160]; snprintf(sb,sizeof(sb),"v97: server world_time=%.2f incoming, pushing to game\n", wt); log_app(sb);
            world_time_push(wt);
        }
    }
    /* v87: 远端火箭的“生成 / 每帧写回 / 存活检查”整块关掉。
       根因：崩溃栈是 UnityMain SIGSEGV，fault addr=0x0076523140000010，
       而 x8=0x4076523140000000 正是位置 double 的位模式 —— 位置被写进了一个被当作指针解引用的字段。
       关掉后：连接照常、自己的位姿照常上传，只有“显示别人”暂时停用。 */
    if (g_ready && (f % 120) == 0) rr_check_toggle();
    /* v124: 远端火箭存活/位置诊断；顺带读 sfsmp-nosim.txt（存在就不关 rb 物理模拟） */
    if (g_ready && (f % 120) == 0 && !g_nosimChecked){
        g_nosimChecked = 1;
        int fd0 = open(dp("sfsmp-nosim.txt"), O_RDONLY);
        if(fd0 >= 0){ close(fd0); g_skipSimOff = 1; log_app("v124: nosim file present -> keep rb simulated\n"); }
        else log_app("v124: nosim file absent -> rb simulated off (default)\n");
    }
    if (g_ready && strstr(g_sceneName, "World_Mobile") && (f % 10) == 0){
        for (int i = 0; i < MP_RR_MAX; i++){
            if (g_rrDbgSteps[i] > 0){ g_rrDbgSteps[i]--; rr_dump_rocket("v124: alive", i, g_rrRocket[i]); }
        }
    }
    if (g_rrEnabled) {
        /* v74: 收到远端火箭结构就在世界里生成它（每 20 帧扫一次，失败最多 3 次） */
        if (g_ready && !g_selfTested && strstr(g_sceneName,"World_Mobile") && f > 900){ g_selfTested = 1; rr_selftest(); }
        if (g_ready && (f % 20) == 0){ int rn = mp_rr_count(); for (int i = 0; i < rn; i++) rr_try_spawn(i); }
        /* v77: 每 2 帧把远端位姿写回已生成的火箭；定期检查它是否被游戏销毁 */
        if (g_ready && strstr(g_sceneName, "World_Mobile")) {
            if ((f % 2) == 0) { int _rn = mp_rr_count(); for (int i = 0; i < _rn; i++) rr_sync_pose(i); }
            if ((f % 90) == 0) rr_check_alive();
            if (!g_rrDiagDone && f > 700) { g_rrDiagDone = 1; rr_diag_space(); }
        }
    }
    /* v69: 世界卡片两步走（打开世界列表 → 点卡片上的开始游戏/继续游戏） */
    /* v86: 两步走标准 UI 点击（Play Button → 世界卡），失败也不会冻结菜单 */
    if (g_uStep == 1) {
        if (--g_uTimer <= 0) { unity_click(g_srcBtn, "Play Button -> world list"); g_uStep = 2; g_uTimer = 120; }
    } else if (g_uStep == 2) {
        if (--g_uTimer <= 0) { u_pick_card(); g_uStep = 0; }
    }
    if (g_wcStep == 1) {
        if (--g_wcTimer <= 0) { wc_open_list(); g_wcStep = 2; g_wcTimer = 75; }
    } else if (g_wcStep == 2) {
        if (--g_wcTimer <= 0) { wc_pick_card(); g_wcStep = 3; g_wcTimer = 120; }
    } else if (g_wcStep == 3) {
        if (--g_wcTimer <= 0) { g_wcStep = 0; log_app("v69: world-list route finished\n"); }
    }
    /* v79 test hook: /sdcard/.../files/sfsmp-enter.txt 存在时，在主菜单自动走一次世界卡片链路
     * （免触摸测试用；和第 3 方文件 sfsmp-connect.txt 一样，都是调试通道） */
    if (g_ready && (f % 120) == 0 && strstr(g_sceneName, "Home_Mobile")) wc_log_target();
    if (!g_autoEnterTried && g_ready && f > 600 && (f % 30) == 0 && strstr(g_sceneName, "Home_Mobile")) {
        if (access(dp("sfsmp-enter.txt"), F_OK) == 0) {
            g_autoEnterTried = 1; g_wcStep = 1; g_wcTimer = 20;
            log_app("v79: enter-file seen -> world-card route\n");
        }
    }
    if (g_enterFallback && g_enterWait > 0) {
        if (--g_enterWait == 0) {
            if (!strstr(g_sceneName, "World_Mobile")) {
                log_app("v70: EnterWorld did not switch scene -> fallback LoadWorldScene\n");
                g_enterFallback = 0; enter_world_fallback();
            } else { g_enterFallback = 0; log_app("v70: world scene reached via EnterWorld\n"); }
        }
    }
    if (g_pendingEnter > 0) {
        g_pendingEnter--;
        if (g_pendingEnter == 30) force_timescale1("before-enter");
        if (g_pendingEnter == 0) {
            if (g_tiWasOpen) force_timescale1("dialog-still-open");
            connect_and_enter_world();
        }
    }
    /* ---- v97: EnterWorld 回调置位后，由主线程这一帧去切场景 ------------------
     * 对应 WorldsMenu.OpenWorld(): delegate { Base.sceneLoader.LoadWorldScene(); }
     * 放在 hook_update（WorldTime.Update，UnityMain 线程）里调，和游戏自己的调用点同线程同阶段。 */
    if (g_wantLoadWorld && !g_loadWorldDone) {
        g_loadWorldTries++;
        if (g_loadWorldTries >= 2) {          /* 回调那一帧先让游戏把 insideWorld 收尾做完 */
            g_wantLoadWorld = 0; g_loadWorldDone = 1;
            char b[220];
            snprintf(b,sizeof(b),"v97: calling LoadWorldScene(false) after EnterWorld cb (tries=%d)\n", g_loadWorldTries); log_app(b);
            if (g_mLoadWorldScene && g_typeSceneLoader && g_ri && g_findMethod) {
                void* p1[1]; p1[0] = g_typeSceneLoader; void* e2 = NULL;
                void* arr = g_ri(g_findMethod, NULL, p1, &e2);
                int n = (arr && !e2) ? (int)g_alg(arr) : -1;
                snprintf(b,sizeof(b),"v97: sceneLoader instances=%d exc=%p\n", n, e2); log_app(b);
                if (arr && !e2 && n > 0) {
                    void* inst = ((void**)((char*)arr + 0x20))[0];
                    int32_t zero = 0; void* pa[1]; pa[0] = &zero; void* e3 = NULL;
                    void* r = g_ri(g_mLoadWorldScene, inst, pa, &e3);
                    snprintf(b,sizeof(b),"v97: LoadWorldScene ret=%p exc=%p inst=%p\n", r, e3, inst); log_app(b);
                    if (e3) log_exc(e3, "v97:");
                }
            } else log_app("v97: LoadWorldScene plumbing missing\n");
        }
    }
    /* ---- v100: 世界加载完成后把视角钉到发射台（此时 WorldView.main 才存在）----
     * 空沙盒世界没有起点，不钉视角火箭会生成在原点/太空。每 30 帧重试，最多 10 次。 */
    /* v102: 视角钉发射台暂时停用 —— WorldView.main 在空沙盒世界里拿到的对象会随场景切换失效，
     * SetViewLocation 会 SIGSEGV（fault addr 0x128, hook_update+18692）。等找到安全时机再开。 */
    (void)g_pinTries;

    if (g_cloneGo) poll_click();
    if (g_tiInst && (f % 30) == 0) ti_poll();
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
    if (g_ticker && g_mPostDelayed){
        jobject target = g_btn ? g_btn : g_decor;
        if (target) (*env)->CallBooleanMethod(env, target, g_mPostDelayed, g_ticker, delay);
        drain(env, "v41: postDelayed");
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

    log_app("v41: overlay UI disabled (probe mode)\n");
    jclass cTicker = (*env)->FindClass(env, "com/sfsmp/bridge/Ticker");
    if (cTicker){
        jmethodID ctorT = (*env)->GetMethodID(env, cTicker, "<init>", "()V");
        jobject tk = (*env)->NewObject(env, cTicker, ctorT);
        if (tk && !drain(env, "v29: new Ticker")) g_ticker = (*env)->NewGlobalRef(env, tk);
    } else drain(env, "v29: FindClass Ticker");
    { jclass cView2 = (*env)->FindClass(env, "android/view/View");
      g_mPostDelayed = (*env)->GetMethodID(env, cView2, "postDelayed", "(Ljava/lang/Runnable;J)Z"); }
    drain(env, "v29: get postDelayed");
    g_installed = 1;
    log_app("v29: overlay installed (button + label)\n");
}

JNIEXPORT void JNICALL Java_com_sfsmp_bridge_Bridge_nativeInit(JNIEnv* env, jclass cls, jobject act){
    log_app("v29: nativeInit called\n");
    if (act) {
        jclass ac = (*env)->GetObjectClass(env, act);
        if (ac) {
            jmethodID gp = (*env)->GetMethodID(env, ac, "getPackageName", "()Ljava/lang/String;");
            if (gp) {
                jstring js = (jstring)(*env)->CallObjectMethod(env, act, gp);
                if (js) {
                    const char* cs = (*env)->GetStringUTFChars(env, js, NULL);
                    if (cs) { sfsmp_set_package(cs); (*env)->ReleaseStringUTFChars(env, js, cs);
                              /* BISECT3: 去掉 mkdir（怀疑在 onCreate 里做 FUSE 操作会卡住 Unity 启动） */ }
                    (*env)->DeleteLocalRef(env, js);
                }
            }
            (*env)->DeleteLocalRef(env, ac);
        }
        g_activity = (*env)->NewGlobalRef(env, act);
    }
    if (cls) g_bridge = (jclass)(*env)->NewGlobalRef(env, cls);
    /* v139: 千万不要在 Activity.onCreate 里碰 getWindow()/getDecorView() ——
       这时 UnityPlayer 还没构造，提前把 DecorView 拉出来会让 Unity 建不出 surface（黑屏）。
       改由 nativeTick（Unity 起来之后）再装。 */
}

JNIEXPORT void JNICALL Java_com_sfsmp_bridge_Bridge_nativeTick(JNIEnv* env, jclass cls){
    g_ticks++;
    if (g_ticks == 1) {
        if (g_dataDir[0]) {
            mkdir(g_dataDir, 0770);
            { char sd[224]; snprintf(sd, sizeof(sd), "%s/Saving", g_dataDir); mkdir(sd, 0770); }
        }
        if (!g_decor && g_activity) {          /* v144: Unity 起来后再取 decor 作为 postDelayed 的宿主 */
            jclass cAct = (*env)->GetObjectClass(env, g_activity);
            jmethodID mWin = cAct ? (*env)->GetMethodID(env, cAct, "getWindow", "()Landroid/view/Window;") : NULL;
            jobject win = mWin ? (*env)->CallObjectMethod(env, g_activity, mWin) : NULL;
            if (win) {
                jclass cWin = (*env)->GetObjectClass(env, win);
                jmethodID mDec = cWin ? (*env)->GetMethodID(env, cWin, "getDecorView", "()Landroid/view/View;") : NULL;
                jobject dec = mDec ? (*env)->CallObjectMethod(env, win, mDec) : NULL;
                if (dec) g_decor = (*env)->NewGlobalRef(env, dec);
                (*env)->DeleteLocalRef(env, win);
            }
        }
        /* v147: ticker 只在第一次 tick 跑一次，必须有 Runnable + postDelayed 才能续命。
           g_mPostDelayed 原来只在 overlay 里解析，overlay 不装就永远是 NULL -> ticker 只跑一次。 */
        if (!g_ticker) {
            jclass cTick = (*env)->FindClass(env, "com/sfsmp/bridge/Ticker");
            if (cTick) {
                jmethodID c0 = (*env)->GetMethodID(env, cTick, "<init>", "()V");
                if (c0) { jobject t = (*env)->NewObject(env, cTick, c0);
                          if (t) g_ticker = (*env)->NewGlobalRef(env, t); }
                (*env)->DeleteLocalRef(env, cTick);
            }
            drain(env, "v147: ticker create");
        }
        if (!g_mPostDelayed && g_decor) {
            jclass cV = (*env)->GetObjectClass(env, g_decor);
            if (cV) g_mPostDelayed = (*env)->GetMethodID(env, cV, "postDelayed", "(Ljava/lang/Runnable;J)Z");
            drain(env, "v147: postDelayed lookup");
        }
        log_app("v144: dirs+decor ready\n");
    }
    if (g_ticks == 3) log_app("v144: ticker alive\n");
    (void)cls;
    schedule(env, 1000);
}

JNIEXPORT void JNICALL Java_com_sfsmp_bridge_Bridge_nativeOnClick(JNIEnv* env, jclass cls, jobject v){
    (void)cls;
    if (g_login_back && (*env)->IsSameObject(env, v, g_login_back)) { log_app("v37: back pressed\n"); page_hide_login(); return; }
    if (g_login_join && (*env)->IsSameObject(env, v, g_login_join)) { log_app("v37: JOIN pressed\n"); return; }
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

static sigjmp_buf g_jb;
static pthread_t g_probe_thr;
static void segv_probe(int sig){
    /* v136: 这是进程级 handler。只有探针线程自己的 fault 才能 longjmp，
       别的线程（尤其 Unity 主线程）的 fault 必须还原默认处理，否则会跨线程跳栈，
       把 Unity 的主线程搅死（实测表现为永久黑屏、il2cpp 永不初始化）。 */
    if (!g_probe_thr || !pthread_equal(pthread_self(), g_probe_thr)) {
        signal(sig, SIG_DFL); raise(sig); return;
    }
    siglongjmp(g_jb, 1);
}

static void* worker(void* arg){
    /* v137 诊断：完全不动 il2cpp，验证「游戏本身能否正常启动」。
       Unity 起不来的黑屏如果在这一版消失，就证明是我们早期进 il2cpp 把初始化搅死了。 */
    log_app("v144: worker start\n");
    (void)arg;
    { int w = 0; while (g_ticks < 1 && w < 120) { usleep(500000); w++; }
      if (g_ticks < 1) { log_app("v147: no ticker, abort\n"); return NULL; } }
    /* v145: ticker 在 onCreate 就跑起来了（1 秒一次），但 il2cpp 要等主菜单才好（实测 10–20 s）。
       光等 tick 不够 —— 必须真等够时间，否则 dg() 在 libil2cpp 内部 SIGSEGV（worker+840）。 */
    log_app("v146: waiting for unity (30s)\n");
    /* v150: 等待时长改为可调（sfsmp-wait.txt，秒），用来摸清「最早多久能安全进 il2cpp」。
       目标：比主菜单出现更早就绪，这样克隆能跟菜单同一帧完成，用户看不到「先菜单后按钮」。 */
    { int secs = 2; const char* wp = sfsmp_dp("sfsmp-wait.txt");
      int fd = wp ? open(wp, O_RDONLY) : -1;
      if (fd >= 0) { char wb[16] = {0}; int n = (int)read(fd, wb, 15); close(fd);
                     if (n > 0) { int v = atoi(wb); if (v >= 1 && v <= 60) secs = v; } }
      { char wl[80]; snprintf(wl, sizeof(wl), "v150: wait=%ds\n", secs); log_app(wl); }
      sleep(secs); }
    log_app("v146: entering il2cpp\n");
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
    /* v138: 一次到位，不做探针（探针会在 il2cpp 持锁时 longjmp，把 Unity 初始化线程锁死） */
    void* d = dg();
    { char db[110]; snprintf(db, sizeof(db), "v138: domain=%p\n", d); log_app(db); }
    if (!d) { log_app("v138: no domain\n"); return NULL; }
    ta(d);
    void* a = ao(d, "Assembly-CSharp");
    if (!a) { log_app("v138: no asm\n"); return NULL; }
    void* img = gi(a);
    if (!img) { log_app("v138: no img\n"); return NULL; }
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
    if(locCls){ g_mGetValue = gm(locCls,"get_Value",0); g_mSetValue = gm(locCls,"set_Value",1); }
    { snprintf(b,sizeof(b),"v88: locCls=%p get_Value=%p set_Value=%p locOff=%d\n", locCls, g_mGetValue, g_mSetValue, g_offLoc); log_app(b); }

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
        if(g_rectCls){ g_mGetSizeDelta=gm(g_rectCls,"get_sizeDelta",0); g_mSetSizeDelta=gm(g_rectCls,"set_sizeDelta",1); g_mGetRect=gm(g_rectCls,"get_rect",0);
            g_mAnchorMinGet=gm(g_rectCls,"get_anchorMin",0); g_mAnchorMinSet=gm(g_rectCls,"set_anchorMin",1);
            g_mAnchorMaxGet=gm(g_rectCls,"get_anchorMax",0); g_mAnchorMaxSet=gm(g_rectCls,"set_anchorMax",1);
            g_mPivotGet=gm(g_rectCls,"get_pivot",0);         g_mPivotSet=gm(g_rectCls,"set_pivot",1); }
        /* v132: LayoutElement（布局组真正读取的首选尺寸） */
        g_leCls = cf(coreImg,"UnityEngine.UI","LayoutElement");
        if(!g_leCls) g_leCls = cf(coreImg,"UnityEngine","LayoutElement");
        if(g_leCls){
            g_typeLE=tgo(cgt(g_leCls));
            g_mLeGetPrefW=gm(g_leCls,"get_preferredWidth",0);  g_mLeSetPrefW=gm(g_leCls,"set_preferredWidth",1);
            g_mLeGetPrefH=gm(g_leCls,"get_preferredHeight",0); g_mLeSetPrefH=gm(g_leCls,"set_preferredHeight",1);
        }
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
        if(!g_actionCls) g_actionCls=cf(img,"System","Action");
        if(g_actionCls && g_gm && !g_mActionInvoke) g_mActionInvoke=g_gm(g_actionCls,"Invoke",0);
        { char b5[170]; snprintf(b5,sizeof(b5),"v83: Action cls=%p Invoke=%p\n", g_actionCls, g_mActionInvoke); log_app(b5); }
        g_clickEvtCls=cf(img,"SFS.UI","ClickUnityEvent");
        g_holdEvtCls =cf(img,"SFS.UI","HoldUnityEvent");
        if(g_clickEvtCls){ g_mClickCtor=gm(g_clickEvtCls,".ctor",0); g_mEvtInvoke=gm(g_clickEvtCls,"Invoke",0); }
        if(g_holdEvtCls)  g_mHoldCtor =gm(g_holdEvtCls ,".ctor",0);
        g_mmCls=cf(img,"SFS.UI","JNFOGGJGNMZRAZYH");
        if(!g_mmCls) g_mmCls=cf(img,"SFS","JNFOGGJGNMZRAZYH");
        if(!g_mmCls) g_mmCls=cf(img,"","JNFOGGJGNMZRAZYH");
        if(g_mmCls){ g_mOpenCredits=gm(g_mmCls,"OpenCredits",0); g_mmMainField=cgfn(g_mmCls,"main"); }
        g_staticGet=(void(*)(void*,void*))dlsym(h,"il2cpp_field_static_get_value");
        g_gm=(void*(*)(void*,const char*,int))gm;
        { void* trc2 = cf(coreImg,"UnityEngine","Transform"); if(trc2){ g_trType2 = tgo(cgt(trc2)); }
          g_goCls2 = cf(coreImg,"UnityEngine","GameObject");
        if(g_goCls2){ g_mAddComponent = g_gm ? g_gm(g_goCls2,"AddComponent",1) : gm(g_goCls2,"AddComponent",1); }
        { char lb[240]; snprintf(lb,sizeof(lb),"v132: anchors=%p/%p pivot=%p LE cls=%p type=%p prefW=%p addComp=%p\n",
                  (void*)g_mAnchorMinGet,(void*)g_mAnchorMinSet,(void*)g_mPivotGet,g_leCls,g_typeLE,(void*)g_mLeGetPrefW,(void*)g_mAddComponent); log_app(lb); }
          { { void* smc = cf(coreImg,"UnityEngine.SceneManagement","SceneManager");
              void* scc = cf(coreImg,"UnityEngine.SceneManagement","Scene");
              if(smc) g_mGetActiveScene = gm(smc,"GetActiveScene",0);
              if(scc) g_mSceneGetName  = gm(scc,"get_name",0);
              snprintf(b,sizeof(b),"v48: sceneMgr=%p GetActive=%p SceneCls=%p getName=%p\n",
                       smc,(void*)g_mGetActiveScene,scc,(void*)g_mSceneGetName); log_app(b); } }
          if(trc2){
              g_mSetAsLast      = gm(trc2,"SetAsLastSibling",0);
              g_mGetSiblingIdx2 = gm(trc2,"get_siblingIndex",0);
              g_mSetSiblingIdx2 = gm(trc2,"SetSiblingIndex",1);
              g_mSetParent2     = gm(trc2,"SetParent",2);
          }
          if(g_goCls2){ g_mSetActive2 = g_gm(g_goCls2,"SetActive",1); g_mGoActive = g_gm(g_goCls2,"get_activeInHierarchy",0); }
          snprintf(b,sizeof(b),"v44: trType=%p goCls=%p setActive=%p\n", g_trType2, g_goCls2, (void*)g_mSetActive2); log_app(b); }
        g_tiCls  = cf(img,"SFS.UI","VCJMEXUSBHCVBJHA");
        g_elemCls= cf(img,"SFS.UI","XPYDSZFDOLBQODKZ");
        g_arrayNew=(void*(*)(void*, uintptr_t))dlsym(h,"il2cpp_array_new");
        if(g_tiCls){ g_mTiOpen=gm(g_tiCls,"Open",5); g_mElement2=gm(g_tiCls,"Element",2); }
        if(g_tmpCls){ g_mSetContentType=gm(g_tmpCls,"set_contentType",1); g_mActivateInput=gm(g_tmpCls,"ActivateInputField",0); }
        g_boxCls = cf(img,"SFS.UI","TUXXAURTZSIFPXTZ");
        if(g_boxCls) g_mBoxOpen = gm(g_boxCls,"Open",0);
        g_evtSysCls = cf(coreImg,"UnityEngine.EventSystems","EventSystem");
        if(g_evtSysCls){ g_mSetSel = gm(g_evtSysCls,"SetSelectedGameObject",1);
                         g_mCurEvtSys = gm(g_evtSysCls,"get_current",0); }
        g_rockCls = cf(img,"SFS.World","FULMMHJZAEKABZAY");
        if(g_rockCls){ g_rockType=tgo(cgt(g_rockCls)); }
        /* ---- v73: RocketSave (ctor(Rocket) RVA 0x1852640) 取完整结构 ---- */
        g_rsCls = cf(img,"SFS.World","RocketSave");
        if(!g_rsCls) g_rsCls = cf(img,"","RocketSave");
        if(g_rsCls){
            g_mRsCtor = g_gm ? g_gm(g_rsCls,".ctor",1) : NULL;
            snprintf(b,sizeof(b),"v73: RocketSave cls=%p ctor=%p\n", g_rsCls, g_mRsCtor); log_app(b);
        } else log_app("v73: RocketSave class NOT found\n");
        /* ---- v74: 远端火箭生成（LoadRocket + RocketSave 配套类） ---- */
        g_spawnerCls = cf(img,"SFS.World","WQPAXLGGCOAUZEOA");
        if(!g_spawnerCls) g_spawnerCls = cf(img,"","WQPAXLGGCOAUZEOA");
        if(g_spawnerCls){
            g_mLoadRocket = gm(g_spawnerCls,"LoadRocket",2);
            snprintf(b,sizeof(b),"v74: spawner cls=%p LoadRocket=%p\n", g_spawnerCls, g_mLoadRocket); log_app(b);
        } else log_app("v74: spawner class NOT found\n");
        g_elemClsOf = (void*(*)(void*))dlsym(h,"il2cpp_class_get_element_class");
        if(g_rsCls){
            void* fParts=cgfn(g_rsCls,"parts");
            void* fJoints=cgfn(g_rsCls,"joints");
            void* fStages=cgfn(g_rsCls,"stages");
            void* fLoc=cgfn(g_rsCls,"location");
            if(fLoc) g_locDataCls = tgoc(fgt(fLoc));
            if(fParts && g_elemClsOf)  g_partSaveCls  = g_elemClsOf(tgoc(fgt(fParts)));
            if(fJoints && g_elemClsOf) g_jointSaveCls = g_elemClsOf(tgoc(fgt(fJoints)));
            if(fStages && g_elemClsOf) g_stageSaveCls = g_elemClsOf(tgoc(fgt(fStages)));
            if(g_partSaveCls){ void* fo=cgfn(g_partSaveCls,"orientation"); if(fo) g_oriCls=tgoc(fgt(fo)); }
            /* Int32 类：coreImg 未必是 corlib，用 StageSave.partIndexes 的 int[] 元素类反推 */
            g_int32Cls = cf(coreImg,"System","Int32");
            if(!g_int32Cls && g_stageSaveCls && g_elemClsOf){
                void* fi=cgfn(g_stageSaveCls,"partIndexes");
                if(fi && tgoc(fgt(fi))) g_int32Cls = g_elemClsOf(tgoc(fgt(fi)));
                snprintf(b,sizeof(b),"v74: int32 field=%p type=%p cls=%p\n", fi, fi?fgt(fi):NULL, g_int32Cls); log_app(b);
            }
            g_mRsCtor0      = g_gm ? g_gm(g_rsCls,".ctor",0) : NULL;
            if(g_locDataCls)  g_mLocDataCtor  = g_gm ? g_gm(g_locDataCls,".ctor",0) : NULL;
            if(g_partSaveCls) g_mPartSaveCtor = g_gm ? g_gm(g_partSaveCls,".ctor",0) : NULL;
            if(g_jointSaveCls)g_mJointSaveCtor= g_gm ? g_gm(g_jointSaveCls,".ctor",0) : NULL;
            if(g_stageSaveCls)g_mStageSaveCtor= g_gm ? g_gm(g_stageSaveCls,".ctor",0) : NULL;
            if(g_oriCls)      g_mOriCtor      = g_gm ? g_gm(g_oriCls,".ctor",0) : NULL;
            snprintf(b,sizeof(b),"v74: rs=%p loc=%p part=%p joint=%p stage=%p ori=%p int32=%p ctor0=%p\n",
                     g_rsCls,g_locDataCls,g_partSaveCls,g_jointSaveCls,g_stageSaveCls,g_oriCls,g_int32Cls,g_mRsCtor0);
            log_app(b);
        } else log_app("v74: RocketSave missing for LoadRocket\n");

        /* ---- v97: 世界时间类扫描（WorldTime 混淆名未知，用 main+worldTime 双字段做特征） ---- */
        { g_cachedImg = img;
          g_imgClassCount = (size_t(*)(void*))dlsym(h,"il2cpp_image_get_class_count");
          g_imgClassGet   = (void*(*)(void*,int))dlsym(h,"il2cpp_image_get_class");
          g_cgfn          = (void*(*)(void*,const char*))dlsym(h,"il2cpp_class_get_field_from_name");
          g_fgo           = (int(*)(void*))dlsym(h,"il2cpp_field_get_offset");
          snprintf(b,sizeof(b),"v97: scan plumbing img=%p cnt=%p get=%p cgfn=%p fgo=%p\n",
                   img,(void*)g_imgClassCount,(void*)g_imgClassGet,(void*)g_cgfn,(void*)g_fgo); log_app(b);
          world_time_resolve();
        }
        /* ---- v67: UnityEngine.Time (进出世界前后强制恢复时间流动) ---- */
        { void* tCls = cf(coreImg,"UnityEngine","Time");
          if(!tCls) tCls = cf(img,"UnityEngine","Time");
          if(tCls){ g_mTimeScaleGet = gm(tCls,"get_timeScale",0); g_mTimeScaleSet = gm(tCls,"set_timeScale",1);
                    snprintf(b,sizeof(b),"v67: Time get=%.p set=%p\n", (void*)g_mTimeScaleGet, (void*)g_mTimeScaleSet); log_app(b); } }
        /* ---- v64: the game's own scene loader (world entry point) ---- */
        g_sceneLoaderCls = cf(img,"SFS","USPVHOMQUKUGZPLU");
        if(!g_sceneLoaderCls) g_sceneLoaderCls = cf(img,"","USPVHOMQUKUGZPLU");
        if(g_sceneLoaderCls){
            g_typeSceneLoader = tgo(cgt(g_sceneLoaderCls));
    /* v100: WorldView / Base / planetLoader / SpaceCenterData 解析 */
    g_worldViewCls = cf(img,"SFS.World","ZIMVTWPJZBTPPXSR");
    if(g_worldViewCls){
        void* fs = cgfn(g_worldViewCls,"main");
        if(fs){ g_worldViewMainField = fs; }
        g_mSetViewLocation = gm(g_worldViewCls,"SetViewLocation",1);
    }
    g_baseCls = cf(img,"SFS","RQOFTSKFXWRDBQCA");
    if(g_baseCls){
        g_typeBase = cgt ? tgo(cgt(g_baseCls)) : NULL;
        void* fp = cgfn(g_baseCls,"planetLoader");
        if(fp) g_offPlanetLoader = (int)fgo(fp);
    }
    g_plCls = cf(img,"SFS.WorldBase","YXHFAUIATGCKNHYQ");
    if(g_plCls){
        void* fs2 = cgfn(g_plCls,"spaceCenter");
        if(fs2) g_offSpaceCenter = (int)fgo(fs2);
    }
    g_scDataCls = cf(img,"SFS.World","SpaceCenterData");
    if(g_scDataCls) g_mGetLaunchPad = gm(g_scDataCls,"get_LaunchPadLocation",0);
    /* v118: 唯一权威"我是谁" —— SFS.World.PlayerController（未混淆名，静态 main + 实例 player）
     * 权威源码 D:/sfs-tools/ref/PCSFS/SFS.World/PlayerController.cs:32-34
     * Player_Local : Obs_Destroyable<Player> : Obs<Player>，Obs 里 private T value 在 +0x10。 */
    player_controller_resolve();
    { char vb[300]; snprintf(vb,sizeof(vb),"v100: wvCls=%p wvMain=%p setView=%p baseCls=%p baseType=%p offPL=%d plCls=%p offSC=%d scCls=%p getPad=%p\n",
        g_worldViewCls,g_worldViewMainField,g_mSetViewLocation,g_baseCls,g_typeBase,(int)g_offPlanetLoader,g_plCls,(int)g_offSpaceCenter,g_scDataCls,g_mGetLaunchPad); log_app(vb); }
            g_mLoadWorldScene = gm(g_sceneLoaderCls,"LoadWorldScene",1);
            if(!g_mLoadWorldScene) g_mLoadWorldScene = gm(g_sceneLoaderCls,"LoadWorldScene",0);
            snprintf(b,sizeof(b),"v64: sceneLoader cls=%p type=%p LoadWorldScene=%p\n",
                     g_sceneLoaderCls,g_typeSceneLoader,g_mLoadWorldScene);
            log_app(b);
        } else log_app("v64: scene loader class NOT found\n");
        /* ---- v70: 官方进世界链路 EnterWorld(string, Action) ---- */
        g_wbCls = cf(img,"SFS.WorldBase","FAELUUBBVIPEYYXS");
        if(!g_wbCls) g_wbCls = cf(img,"SFS","FAELUUBBVIPEYYXS");
        if(!g_wbCls) g_wbCls = cf(img,"","FAELUUBBVIPEYYXS");
        if(g_wbCls){
            g_mEnterWorld = gm(g_wbCls,"EnterWorld",2);
            if(!g_mEnterWorld) g_mEnterWorld = gm(g_wbCls,"EnterWorld",1);
            snprintf(b,sizeof(b),"v70: worldBase cls=%p EnterWorld=%p\n", g_wbCls, g_mEnterWorld); log_app(b);
        } else log_app("v70: world base class NOT found\n");
        { void* tc2=cf(coreImg,"UnityEngine","Transform"); if(tc2) g_mGetPosition=gm(tc2,"get_position",0); }
        snprintf(b,sizeof(b),"v46: rocket cls=%p type=%p getpos=%p\n", g_rockCls,g_rockType,(void*)g_mGetPosition); log_app(b);
        g_toastCls = cf(img,"SFS.UI","EJIHSVFRLSOQRBJE");
        if(g_toastCls){ g_toastField=cgfn(g_toastCls,"main"); g_mToast=gm(g_toastCls,"Log",1); }
        snprintf(b,sizeof(b),"v45: toast cls=%p field=%p log=%p\n", g_toastCls,g_toastField,g_mToast); log_app(b);
        { void* cr=cf(img,"SFS.UI","DEOTNULUBFZWSSUA"); if(cr) g_tiField=cgfn(cr,"textInput"); }
        snprintf(b,sizeof(b),"v44: TI cls=%p open=%p elem=%p field=%p anew=%p\n", g_tiCls,g_mTiOpen,g_mElement2,g_tiField,(void*)g_arrayNew); log_app(b);
        g_popCls = cf(img,"","PKVTCUBZSQHIVNVO");
        if(!g_popCls) g_popCls = cf(img,"SFS.UI","PKVTCUBZSQHIVNVO");
        if(g_popCls){ g_popType = tgo(cgt(g_popCls)); g_mPopOpen = gm(g_popCls,"OnOpen",0);
            snprintf(b,sizeof(b),"v44: popupCls=%p type=%p OnOpen=%p\n", g_popCls, g_popType, g_mPopOpen); log_app(b); }
        g_nojCls2 = cf(img,"","NOJZIZCFSEXSUEEX");
        if(!g_nojCls2) g_nojCls2 = cf(img,"SFS.UI","NOJZIZCFSEXSUEEX");
        if(g_nojCls2){ g_nojType2 = tgo(cgt(g_nojCls2));
            g_mOpenNoj    = g_gm(g_nojCls2,"Open",5);
            g_mDrawTitle2 = g_gm(g_nojCls2,"DrawTitle",1);
            g_mDrawText3  = g_gm(g_nojCls2,"DrawText",2);
            g_mDrawFullBtn= g_gm(g_nojCls2,"DrawFullButton",4);
            g_mgp2 = (void*(*)(void*,uint32_t))dlsym(h,"il2cpp_method_get_param");
            g_cfit = (void*(*)(void*))dlsym(h,"il2cpp_class_from_il2cpp_type");
            g_ghn  = (void*(*)(void*,int))dlsym(h,"il2cpp_gchandle_new");
            snprintf(b,sizeof(b),"v43: noj cls=%p type=%p open=%p title=%p text=%p btn=%p\n", g_nojCls2,g_nojType2,g_mOpenNoj,g_mDrawTitle2,g_mDrawText3,g_mDrawFullBtn);
            log_app(b);
        } else log_app("v43: noj class NOT found\n");
        g_modsCls=cf(img,"ModLoader.UI","NANBAJRHGSVEQBTQ");
        if(!g_modsCls) g_modsCls=cf(img,"ModLoader","NANBAJRHGSVEQBTQ");
        if(g_modsCls){
            void* (*par)(void*) = (void*(*)(void*))dlsym(h,"il2cpp_class_get_parent");
            void* bc=par?par(g_modsCls):NULL;
            g_modsMainField=cgfn(g_modsCls,"main");
            if(bc) g_modsOpen=gm(bc,"Open",0);
            if(!g_modsOpen) g_modsOpen=gm(g_modsCls,"Open",0);
        }
        { char mb[160]; snprintf(mb,sizeof(mb),"v40: mods cls=%p open=%p mainF=%p\n",g_modsCls,g_modsOpen,g_modsMainField); log_app(mb); }
        g_mObjGetClass=(void*(*)(void*))dlsym(h,"il2cpp_object_get_class");
        g_mClassGetName=(const char*(*)(void*))dlsym(h,"il2cpp_class_get_name");
        g_mClassParent=(void*(*)(void*))dlsym(h,"il2cpp_class_get_parent");
        snprintf(b,sizeof(b),"v33: mainmenu cls=%p openCredits=%p mainField=%p\n",g_mmCls,g_mOpenCredits,g_mmMainField);
        log_app(b);
        g_trCls=cf(img,"SFS.Translations","WRNLOFEPQTZANWYN");
        if(g_trCls) g_trType=tgo(cgt(g_trCls));
        if(uiI){ void* lb=cf(uiI,"UnityEngine.UI","LayoutRebuilder");
                 g_mForceRebuild=lb?gm(lb,"ForceRebuildLayoutImmediate",1):NULL; }
        if(uimI){ void* rtu=cf(uimI,"UnityEngine","RectTransformUtility");
                  if(rtu){ g_rcs=gm(rtu,"RectangleContainsScreenPoint",3); g_mW2S=gm(rtu,"WorldToScreenPoint",2); }
        { void* cam=cf(coreImg,"UnityEngine","Camera"); if(cam) g_mCamMain=gm(cam,"get_main",0);
          g_v3Cls = cf(coreImg,"UnityEngine","Vector3");
          { void* trc=cf(coreImg,"UnityEngine","Transform"); if(trc) g_mGetPos2=gm(trc,"get_position",0); } } }
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
        { void* wt = cf(img,"SFS.World","VCQSQMYGGZECNBPE");
          if(wt){ void* upd2 = gm(wt,"Update",0);
            if(upd2){ void** mi2=(void**)upd2;
              mp((void*)((uintptr_t)mi2 & ~(page-1)), page*2, PROT_READ|PROT_WRITE|PROT_EXEC);
              g_orig_wt_update = mi2[0]; mi2[0]=(void*)hook_worldtime;
              snprintf(b,sizeof(b),"v46: WorldTime hook armed (was %p)\n", (void*)g_orig_wt_update); log_app(b);
            } else log_app("v46: WorldTime.Update not found\n");
          } else log_app("v46: WorldTime class not found\n"); }
        g_ready=1;
        /* v116: 删除开屏"读 sfsmp-connect.txt 自动连接"。
         * 这个文件一旦存在，玩家（和我自己）就会在没有点多人联机按钮、没有看到 IP 页的情况下被连上服务器，
         * 属于隐形行为，已三次导致"IP 页被删"的误会。连接只能由用户点按钮触发。 */
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
