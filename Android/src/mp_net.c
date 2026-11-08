/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * mp_net.c 鈥?鎵嬫満绔仈鏈哄鎴风锛圡1锛欿CP + 鎻℃墜锛?
 *
 * 澶嶇敤鏈嶅姟鍣ㄥ崗璁細甯?= [int32 bodyLen][u8 kind][int32 seq][int32 payloadBits][payload]
 * 浼犺緭 = KCP over UDP锛孠CP 瀛楄妭娴佷笂鍐嶅 4 瀛楄妭灏忕闀垮害鍓嶇紑锛堜笌 Server/cpp 鐨?KcpTransport 涓€鑷达級
 * 鎻℃墜 = magic 0x31465353 + version 2 + strings(username,password,solarSystem) + i32 resumeId + resumeToken
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ikcp.h"
#include "mp_types.h"
#include "mp_destroy_part_wire.h"

/* v129: 璺緞璺熼殢杩愯鏃跺寘鍚嶏紙鐢?v64.c 鐨?sfsmp_set_package 鍐冲畾锛夈€傚叡瀛樼増鎹簡鍖呭悕涔熻兘鍐欒繘鑷繁鐨勭洰褰曘€?*/
extern const char* sfsmp_dp(const char* name);
/* v130: 鍏抽敭浜嬩欢鍚屾椂鍐欏埌 /sdcard/Android/media/<鍖呭悕>/log.txt锛堢敱 v64.c 瀹炵幇骞跺仛閫愬抚琛岃繃婊わ級 */
extern void sfsmp_media_log(const char* m);
#define MP_MARKER (sfsmp_dp("sfsmp-probe.txt"))
#define MP_SERVER (sfsmp_dp("sfsmp-server.txt"))

static void mp_log(const char* m){
    int fd = open(MP_MARKER, O_WRONLY|O_CREAT|O_APPEND, 0644);
    if(fd >= 0){ write(fd, m, strlen(m)); close(fd); }
    sfsmp_media_log(m);
}

/* ---------- 浣嶅啓鍏ュ櫒锛圠idgren LSB-first锛?---------- */
typedef struct { unsigned char buf[1024]; int bit; } bw_t;
static void bw_clear(bw_t* w){ memset(w->buf,0,sizeof(w->buf)); w->bit=0; }
static void bw_bits(bw_t* w, unsigned long long v, int n){
    for(int i=0;i<n;i++){
        int byte = w->bit >> 3, off = w->bit & 7;
        if((v >> i) & 1ull) w->buf[byte] |= (unsigned char)(1u << off);
        else                w->buf[byte] &= (unsigned char)~(1u << off);
        w->bit++;
    }
}
static void bw_u8 (bw_t* w, unsigned v){ bw_bits(w, v & 0xffu, 8); }
static void bw_u32(bw_t* w, unsigned v){ bw_bits(w, v, 32); }
static void bw_i32(bw_t* w, int v){ bw_bits(w, (unsigned)v, 32); }
static void bw_u64(bw_t* w, unsigned long long v){ bw_bits(w, v, 64); }
static void bw_f64(bw_t* w, double v){ unsigned long long b; memcpy(&b,&v,8); bw_bits(w, b, 64); }
static void bw_f32(bw_t* w, float v){ unsigned b; memcpy(&b,&v,4); bw_bits(w, b, 32); }
static void bw_bool(bw_t* w, int v){ bw_bits(w, v?1ull:0ull, 1); }
static void bw_str(bw_t* w, const char* s){
    size_t n = strlen(s);
    unsigned v = (unsigned)n;
    while(v >= 0x80){ bw_u8(w, (v & 0x7f) | 0x80); v >>= 7; }
    bw_u8(w, v);
    for(size_t i=0;i<n;i++) bw_u8(w, (unsigned char)s[i]);
}

/* ---------- 浣嶈鍙栧櫒 ---------- */
typedef struct { const unsigned char* buf; int nbits; int bit; int failed; } br_t;
static unsigned long long br_bits(br_t* r, int n){
    unsigned long long v = 0;
    for(int i=0;i<n;i++){
        int byte = r->bit >> 3, off = r->bit & 7;
        if(r->bit < r->nbits) { if((r->buf[byte] >> off) & 1u) v |= (1ull << i); }
        else r->failed = 1;
        r->bit++;
    }
    return v;
}
static unsigned br_u8(br_t* r){ return (unsigned)br_bits(r,8); }
static int br_i32(br_t* r){ return (int)(unsigned)br_bits(r,32); }
static double br_f64(br_t* r){ unsigned long long b=br_bits(r,64); double d; memcpy(&d,&b,8); return d; }
static float br_f32(br_t* r){ unsigned b=(unsigned)br_bits(r,32); float f; memcpy(&f,&b,4); return f; }
static int br_bool(br_t* r){ return (int)br_bits(r,1); }
static void br_str(br_t* r, char* out, int cap){
    unsigned v=0; int shift=0;
    for(;;){ unsigned b=br_u8(r); v |= (b & 0x7fu) << shift; if(!(b & 0x80u)) break; shift += 7; if(shift>28) break; }
    int n=(int)v; if(n<0) n=0; if(n>cap-1) n=cap-1;
    for(int i=0;i<n;i++) out[i]=(char)br_u8(r);
    out[n]=0;
}

/* 鏈湴浣嶅Э锛堜富绾跨▼鍐欍€佺綉缁滅嚎绋嬭锛?*/
static double g_lx=0, g_ly=0, g_lvx=0, g_lvy=0;
static float  g_lrot=0;
static float  g_lav=0;   /* v107: 鐪熷疄瑙掗€熷害锛岄殢 primary 涓€璧峰彂 */
static char   g_lplanet[40] = "Earth";
static volatile int g_have_pose = 0;
static volatile int g_global_id = -1;
static volatile int g_create_sent = 0;
static volatile int g_local_id = 1;              /* per-session CreateRocket local id, must change when local rocket changes */
static volatile int g_destroy_old_gid = -1;      /* old rocket to destroy before creating the replacement */
static volatile int g_rx_primary = 0;
/* v166: 灞忓箷璇婃柇锛堜富鍔涙満娌?adb锛屽彧鑳借 mod 鑷繁鎶婄粨璁烘樉绀哄嚭鏉ワ級 */
static char g_dg_addr[80] = "";
static int  g_dg_af = 0, g_dg_cand = 0, g_dg_sendok = 0, g_dg_sendfail = 0, g_dg_errno = 0;                          /* v163: 涓嬭 primary 璁℃暟锛堥噺棰戠巼锛?*/
int mp_rx_primary_count(void){ return g_rx_primary; }
static volatile int g_joined = 0;
/* v97: 鏈嶅姟鍣ㄤ笅鍙戠殑涓栫晫鏃堕棿锛圚elloAck 閲屽甫锛涗篃鏄?WorldSnapshot 鐨勭涓€涓瓧娈碉級 */
static volatile double g_server_world_time = 0.0;
static volatile int g_have_world_time = 0;
double mp_server_world_time(void){ return g_have_world_time ? g_server_world_time : 0.0; }
int mp_is_joined(void){ return g_joined; }

static unsigned long long mp_now_ms(void);
/* v131: 缁欎笂灞傚仛銆岃繛鎺ョ姸鎬?HUD銆嶇敤 鈥斺€?鏈€杩戜竴娆℃敹鍒版湇鍔″櫒鍖呯殑鏃堕棿鎴?*/
static volatile unsigned long long g_last_rx_ms = 0;
unsigned long long mp_last_rx_age_ms(void){
    unsigned long long t = g_last_rx_ms;
    if(!t) return 0xffffffffull;
    return mp_now_ms() - t;
}

void mp_set_local_angular(float av);
void mp_set_local_pose(double x, double y, double vx, double vy, const char* planet, float rot){
    g_lx=x; g_ly=y; g_lvx=vx; g_lvy=vy; g_lrot=rot;
    if(planet && planet[0]){ snprintf(g_lplanet, sizeof(g_lplanet), "%s", planet); }
    g_have_pose = 1;
}

/* v107: 鐪熷疄瑙掗€熷害銆傚绔潬瀹冨仛澶栨帹锛岀綉缁滄湁寤惰繜鏃朵笉浼氬崱鍦ㄦ棫瑙掑害銆?*/
void mp_set_local_angular(float av){ g_lav=av; }

/* 杩滅鐏鐘舵€佺紦瀛橈紙M4 鐢級 */
typedef struct { int id; double x, y, vx, vy; float rot; int valid; } mp_remote_t;
static mp_remote_t g_remote[16];
static int g_remote_count = 0;

static mp_remote_t* remote_slot(int id){
    for(int i=0;i<g_remote_count;i++) if(g_remote[i].id == id) return &g_remote[i];
    if(g_remote_count >= 16) return &g_remote[g_remote_count-1];
    mp_remote_t* r = &g_remote[g_remote_count++];
    memset(r, 0, sizeof(*r)); r->id = id; return r;
}

int mp_remote_get(int i, int* id, double* x, double* y, float* rot){
    if(i < 0 || i >= g_remote_count) return 0;
    if(!g_remote[i].valid) return 0;
    *id = g_remote[i].id; *x = g_remote[i].x; *y = g_remote[i].y; *rot = g_remote[i].rot;
    return 1;
}

/* ---------- v74: 杩滅鐏瀹屾暣缁撴瀯缂撳瓨 ---------- */
static mp_rrocket_t g_rr[MP_RR_MAX];
static int g_rr_n = 0;

/* v110: 婊′簡浠ュ悗涓嶈兘鐩存帴瑕嗙洊鏈€鍚庝竴鏍?鈥斺€?閭ｆ牱 v64.c 鐨?g_rrSpawned[]/g_rrRocket[]
 * 杩樻寚鐫€鏃у璞★紝鏂扮粨鏋勬案杩滀笉浼氳鐢熸垚锛屽抚寰幆杩樹細寰€涓€涓崲浜?gid 鐨勬Ы涓婂啓浣嶅Э銆?
 * 鐜板湪锛氭弧浜嗗氨娣樻卑"鏈€涔呮病鏀跺埌鍖?鐨勯偅涓€鏍硷紝骞舵妸瀹冩暣鏍兼竻闆讹紙杩炲悓 nsnap 蹇収锛夛紝
 * 璁╂秷璐圭閫氳繃 valid/has_structure 鑷劧鍙戠幇杩欐槸涓€鏋氭柊鐏銆?*/
static mp_rrocket_t* rr_slot(int gid){
    int i, victim;
    for(i=0;i<g_rr_n;i++) if(g_rr[i].gid == gid) return &g_rr[i];
    if(g_rr_n < MP_RR_MAX){
        mp_rrocket_t* r = &g_rr[g_rr_n++];
        memset(r, 0, sizeof(*r));
        r->gid = gid;
        return r;
    }
    victim = 0;
    for(i=1;i<g_rr_n;i++) if(g_rr[i].last_rx < g_rr[victim].last_rx) victim = i;
    memset(&g_rr[victim], 0, sizeof(g_rr[victim]));
    g_rr[victim].gid = gid;
    { char b[140]; snprintf(b,sizeof(b),"mp: rr slot full -> evict idx=%d for gid=%d\n", victim, gid); mp_log(b); }
    return &g_rr[victim];
}

static mp_rrocket_t* rr_find(int gid){
    for(int i=0;i<g_rr_n;i++) if(g_rr[i].gid == gid) return &g_rr[i];
    return NULL;
}

static mp_rpart_t* rr_part(int gid, int pid){
    mp_rrocket_t* rr = rr_find(gid);
    if(!rr) return NULL;
    for(int i=0;i<rr->nparts;i++) if(rr->parts[i].id == pid) return &rr->parts[i];
    return NULL;
}

static void rr_set_toggle(mp_rrocket_t* rr, int pid, const char* key, int val){
    mp_rpart_t* p = rr_part(rr ? rr->gid : -1, pid); if(!p) return;
    for(int i=0;i<p->ntv;i++) if(strncmp(p->tv[i].name,key,27)==0){ p->tv[i].v=val; rr->dirty=1; return; }
    if(p->ntv < MP_RV_TOG){ snprintf(p->tv[p->ntv].name,28,"%s",key); p->tv[p->ntv].v=val; p->ntv++; rr->dirty=1; }
}

static void rr_set_num(mp_rrocket_t* rr, int pid, const char* key, double val){
    mp_rpart_t* p = rr_part(rr ? rr->gid : -1, pid); if(!p) return;
    for(int i=0;i<p->nnv;i++) if(strncmp(p->nv[i].name,key,27)==0){ p->nv[i].v=val; rr->dirty=1; return; }
    if(p->nnv < MP_RV_NUM){ snprintf(p->nv[p->nnv].name,28,"%s",key); p->nv[p->nnv].v=val; p->nnv++; rr->dirty=1; }
}

int mp_rr_count(void){ return g_rr_n; }
mp_rrocket_t* mp_rr_at(int i){ return (i>=0 && i<g_rr_n) ? &g_rr[i] : NULL; }
void mp_rr_clear_dirty(int i){ if(i>=0 && i<g_rr_n) g_rr[i].dirty = 0; }

int mp_rr_remove_part(int rocket_id, int part_id){
    mp_rrocket_t* rr = rr_find(rocket_id);
    if(!rr || part_id < 0) return 0;
    int at = -1;
    for(int i = 0; i < rr->nparts; ++i) if(rr->parts[i].id == part_id){ at = i; break; }
    if(at < 0) return 0;
    memmove(&rr->parts[at], &rr->parts[at + 1], sizeof(rr->parts[0]) * (size_t)(rr->nparts - at - 1));
    --rr->nparts;
    rr->has_structure = rr->nparts > 0;
    for(int i = 0; i < rr->njoints;){
        if(rr->joints[i].a == part_id || rr->joints[i].b == part_id){
            memmove(&rr->joints[i], &rr->joints[i + 1], sizeof(rr->joints[0]) * (size_t)(rr->njoints - i - 1));
            --rr->njoints;
        } else ++i;
    }
    for(int i = 0; i < rr->nstages; ++i){
        mp_rstage_t* stage = &rr->stages[i];
        for(int j = 0; j < stage->n;){
            if(stage->ids[j] == part_id){
                memmove(&stage->ids[j], &stage->ids[j + 1], sizeof(stage->ids[0]) * (size_t)(stage->n - j - 1));
                --stage->n;
            } else ++j;
        }
    }
    rr->dirty = 1;
    return 1;
}


/* CreateRocket 鐨?RocketState 杞借嵎锛? 鏁存５闆朵欢鏍戯級銆?
 * 瀛楁椤哄簭浠?Server/cpp/src/protocol/packets.cpp:273 RocketState::serialize 涓哄噯锛?
 *   string rocket_name; NetLocation(f64 x,y,vx,vy + string address); f32 rotation;
 *   f32 angular_velocity; bool throttle_on; f32 throttle_percent; bool rcs;
 *   count+(i32 id, PartState); count+(i32 a, i32 b); count+(i32 stage_id, count+i32 ids)
 * PartState锛坧ackets.cpp:167锛夛細string name; f32 x,y; f32 ox,oy,oz; f32 temperature;
 *   count+(string,f64); count+(string,bool); count+(string,string); bool burns_null; [f32 x3 + string x2]
 */
static void rr_parse_create(const unsigned char* p, int nbytes, int* out_lid, int* out_gid){
    br_t r = { p, nbytes * 8, 0 };
    char b[220];
    double wt = br_f64(&r);
    int lid = br_i32(&r);
    int gid = br_i32(&r);
    int for_launch = br_bool(&r);
    (void)wt; (void)for_launch;
    *out_lid = lid; *out_gid = gid;
    if(gid < 0) return;
    if(lid >= 0 && lid == g_local_id) return;   /* 杩欐槸"鎴戜滑鑷繁鐨勭伀绠?锛屼笉鑳藉綋杩滅鐏鍐嶇敓鎴愪竴鏋?*/

    mp_rrocket_t* rr = rr_slot(gid);
    rr->valid = 1;
    rr->has_structure = 0;
    rr->nparts = rr->njoints = rr->nstages = 0;
    br_str(&r, rr->name, sizeof(rr->name));
    rr->x  = br_f64(&r); rr->y  = br_f64(&r);
    rr->vx = br_f64(&r); rr->vy = br_f64(&r);
    { char addr[24]; br_str(&r, addr, sizeof(addr));
      if(addr[0]) snprintf(rr->planet, sizeof(rr->planet), "%s", addr); }
    rr->rot      = br_f32(&r);
    rr->av       = br_f32(&r);
    rr->throttle_on = br_bool(&r);
    rr->throttle = br_f32(&r);
    rr->rcs      = br_bool(&r);

    { int np = br_i32(&r); if(np < 0) np = 0; if(np > MP_RR_PARTS) np = MP_RR_PARTS;
      int i, k;
      for(i=0;i<np;i++){
          mp_rpart_t* pt = &rr->parts[i];
          memset(pt, 0, sizeof(*pt));             /* v93: 鍏堟竻闆讹紝鍐嶈 id 鈥斺€?椤哄簭鍐欏弽浼氭妸鍒氳鍒扮殑 id 娓呮垚 0 */
          pt->id = br_i32(&r);                    /* part id锛歫oints/stages 鐢ㄥ畠寮曠敤闆朵欢锛屽繀椤讳繚鐣?*/
          br_str(&r, pt->name, sizeof(pt->name));
          pt->x = br_f32(&r); pt->y = br_f32(&r);
          pt->ox = br_f32(&r); pt->oy = br_f32(&r); pt->oz = br_f32(&r);
          pt->temp = br_f32(&r);
          /* v161: 鍙橀噺瀛楀吀蹇呴』鐪熸钀藉湴锛堢噧鏂?鐢甸噺/甯?鐫€闄嗚吙鐘舵€佸叏闈犲畠锛夛紝
           * 瓒呭嚭鏈湴瀹归噺鏃朵粛鎸夋牸寮忚瀹屼涪寮冿紝閬垮厤鍋忕Щ閿欎綅銆?*/
          int n = br_i32(&r); if(n < 0 || n > 4096) n = 0;
          for(k=0;k<n;k++){
              char kb[64]; br_str(&r, kb, sizeof(kb)); double dv = br_f64(&r);
              if(k < MP_RV_NUM){ snprintf(pt->nv[k].name,sizeof(pt->nv[k].name),"%s",kb); pt->nv[k].v = dv; }
              pt->nnv = (k < MP_RV_NUM) ? (k+1) : pt->nnv;
          }
          n = br_i32(&r); if(n < 0 || n > 4096) n = 0;
          for(k=0;k<n;k++){
              char kb[64]; br_str(&r, kb, sizeof(kb)); int bv = br_bool(&r);
              if(k < MP_RV_TOG){ snprintf(pt->tv[k].name,sizeof(pt->tv[k].name),"%s",kb); pt->tv[k].v = bv; }
              pt->ntv = (k < MP_RV_TOG) ? (k+1) : pt->ntv;
          }
          n = br_i32(&r); if(n < 0 || n > 4096) n = 0;
          for(k=0;k<n;k++){
              char kb[64], vb[64]; br_str(&r, kb, sizeof(kb)); br_str(&r, vb, sizeof(vb));
              if(k < MP_RV_TXT){ snprintf(pt->xv[k].name,sizeof(pt->xv[k].name),"%s",kb); snprintf(pt->xv[k].val,sizeof(pt->xv[k].val),"%s",vb); }
              pt->nxv = (k < MP_RV_TXT) ? (k+1) : pt->nxv;
          }
          if(!br_bool(&r)){                        /* burns */
              (void)br_f32(&r); (void)br_f32(&r); (void)br_f32(&r);
              { char tb[64]; br_str(&r, tb, sizeof(tb)); br_str(&r, tb, sizeof(tb)); }
          }
          rr->nparts = i + 1;
      }
    }
    { int nj = br_i32(&r); if(nj < 0) nj = 0; if(nj > MP_RR_JOINTS) nj = MP_RR_JOINTS;
      int i;
      for(i=0;i<nj;i++){ rr->joints[i].a = br_i32(&r); rr->joints[i].b = br_i32(&r); rr->njoints = i + 1; } }
    { int ns = br_i32(&r); if(ns < 0) ns = 0; if(ns > MP_RR_STAGES) ns = MP_RR_STAGES;
      int i, k;
      for(i=0;i<ns;i++){
          mp_rstage_t* st = &rr->stages[i];
          st->stage_id = br_i32(&r);
          { int c = br_i32(&r); if(c < 0) c = 0; if(c > MP_RR_STAGE_IDS) c = MP_RR_STAGE_IDS;
            st->n = c;
            for(k=0;k<c;k++) st->ids[k] = br_i32(&r); }
          rr->nstages = i + 1;
      }
    }
    rr->has_structure = (rr->nparts > 0) ? 1 : 0;
    rr->dirty = 1;
    snprintf(b,sizeof(b),"mp: REMOTE CREATE gid=%d lid=%d name='%s' planet='%s' parts=%d joints=%d stages=%d pos=(%.2f,%.2f)\n",
             gid, lid, rr->name, rr->planet, rr->nparts, rr->njoints, rr->nstages, rr->x, rr->y);
    mp_log(b);
    if(rr->nparts > 0){
        snprintf(b,sizeof(b),"mp:   part0 id=%d name='%s' pos=(%.2f,%.2f) o=(%.3f,%.3f,%.3f)\n",
                 rr->parts[0].id, rr->parts[0].name, rr->parts[0].x, rr->parts[0].y,
                 rr->parts[0].ox, rr->parts[0].oy, rr->parts[0].oz);
        mp_log(b);
    }
    if(rr->njoints > 0){
        snprintf(b,sizeof(b),"mp:   joint0 a=%d b=%d\n", rr->joints[0].a, rr->joints[0].b);
        mp_log(b);
    }
}


static char  g_host[96] = "";   /* v187: 娌℃湁榛樿鏈嶅姟鍣ㄥ湴鍧€锛屽繀椤荤敱 UI 浼犲叆 */
static int   g_port = 9806;
static int   g_fd = -1;
static ikcpcb* g_kcp = NULL;
static IUINT32 g_kcp_next = 0;   /* v186: 涓嬩竴娆?ikcp_update 鏃跺埢锛坢s锛宨kcp_check 杩斿洖锛?*/

static int mp_output(const char* buf, int len, ikcpcb* kcp, void* user){
    (void)kcp; (void)user;
    if(g_fd < 0) return -1;
    { int n = (int)send(g_fd, buf, (size_t)len, 0);
      /* v165: UDP 鐨?connect() 涓嶉獙璇佸彲杈炬€э紝send 鎵嶆槸鐪熸牸 鈥斺€?澶辫触蹇呴』鐣欒瘉 */
      if(n < 0){ g_dg_sendfail++; g_dg_errno = errno; static int fails = 0;
          if(fails < 6){ fails++; char eb[110];
              snprintf(eb,sizeof(eb),"mp: send failed errno=%d len=%d (fails=%d)\n", errno, len, fails); mp_log(eb); } }
      else { g_dg_sendok++; static int oks = 0; if(oks < 3){ oks++; char ob[90];
              snprintf(ob,sizeof(ob),"mp: send ok n=%d len=%d\n", n, len); mp_log(ob); } }
      return n; }
}

void mp_diag(char* out, int cap){
    if(!out || cap < 32) return;
    snprintf(out, (size_t)cap, "鑱旀満 %s af=%d 鍙?ok=%d fail=%d e=%d rx=%llums",
             g_dg_addr[0] ? g_dg_addr : "(鏈В鏋?", g_dg_af, g_dg_sendok, g_dg_sendfail,
             g_dg_errno, (unsigned long long)mp_last_rx_age_ms());
}

static unsigned long long mp_now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)(ts.tv_nsec / 1000000);
}

/* ================= v78: 杩滅浣嶅Э鎻掑€硷紙绉绘 PC 绔?Interpolator 鎬濊矾锛?=================
 * 鏀跺埌 primary 鍖呮椂鍘嬩竴涓揩鐓э紙甯︽湰鏈哄崟璋冩椂閽熸椂闂存埑锛夛紝娓叉煋鏃跺彇 now-120ms 澶勭殑鎻掑€硷紝
 * 杩欐牱缃戠粶鎶栧姩涓嶄細鐩存帴鍙嶆槧鍒扮敾闈笂銆傝秴鍑烘渶鏂板揩鐓у垯鐢ㄩ€熷害鍋氭渶澶?250ms 鐨勭煭澶栨帹銆?
 */
#define RR_DELAY_MS      120
#define RR_EXTRAP_MAX_MS 250

static void rr_push_snap(mp_rrocket_t* rr, double x, double y, double vx, double vy, float rot, float av){
    mp_snap_t* s;
    if(!rr) return;
    if(rr->nsnap >= MP_RR_SNAPS){
        memmove(&rr->snaps[0], &rr->snaps[1], sizeof(mp_snap_t) * (MP_RR_SNAPS - 1));
        rr->nsnap = MP_RR_SNAPS - 1;
    }
    s = &rr->snaps[rr->nsnap++];
    s->t = mp_now_ms(); s->x = x; s->y = y; s->vx = vx; s->vy = vy; s->rot = rot; s->av = av;
    rr->last_rx = s->t;
}

static double rr_ang_lerp(double a, double b, double t){
    double d = b - a;
    while(d > 180.0) d -= 360.0;
    while(d <= -180.0) d += 360.0;
    return a + d * t;
}

int mp_rr_sample(int i, double* ox, double* oy, float* orot){
    double rt; mp_snap_t* s; int n, k;
    if(i < 0 || i >= g_rr_n) return 0;
    { mp_rrocket_t* rr = &g_rr[i];
      if(!rr->valid || rr->nsnap <= 0) return 0;
      rt = (double)mp_now_ms() - (double)RR_DELAY_MS;
      s = rr->snaps; n = rr->nsnap; }
    if(n == 1){ *ox = s[0].x; *oy = s[0].y; *orot = s[0].rot; return 1; }
    if(rt <= (double)s[0].t){ *ox = s[0].x; *oy = s[0].y; *orot = s[0].rot; return 1; }
    if(rt >= (double)s[n-1].t){
        double dt = (rt - (double)s[n-1].t) / 1000.0;
        if(dt > (double)RR_EXTRAP_MAX_MS / 1000.0) dt = (double)RR_EXTRAP_MAX_MS / 1000.0;
        *ox = s[n-1].x + s[n-1].vx * dt;
        *oy = s[n-1].y + s[n-1].vy * dt;
        *orot = (float)((double)s[n-1].rot + (double)s[n-1].av * dt);
        return 1;
    }
    for(k = 1; k < n; k++){
        if(rt < (double)s[k].t){
            double span = (double)(s[k].t - s[k-1].t) / 1000.0;
            double t = (span > 0.001) ? (((rt - (double)s[k-1].t) / 1000.0) / span) : 1.0;
            if(t < 0.0) t = 0.0;
            if(t > 1.0) t = 1.0;
            *ox = s[k-1].x + (s[k].x - s[k-1].x) * t;
            *oy = s[k-1].y + (s[k].y - s[k-1].y) * t;
            *orot = (float)rr_ang_lerp((double)s[k-1].rot, (double)s[k].rot, t);
            return 1;
        }
    }
    *ox = s[n-1].x; *oy = s[n-1].y; *orot = s[n-1].rot;
    return 1;
}

/* ---------- 灏忕鍐欏叆 ---------- */
static void w_u32(unsigned char* p, unsigned v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static unsigned r_u32(const unsigned char* p){ return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24); }
static void w_u64(unsigned char* p, unsigned long long v){ for(int i=0;i<8;i++) p[i]=(unsigned char)(v>>(8*i)); }
static unsigned long long r_u64(const unsigned char* p){ unsigned long long v=0; for(int i=0;i<8;i++) v |= (unsigned long long)p[i] << (8*i); return v; }

/* ---------- 缁勫抚 ---------- */
static int build_frame_bits(unsigned char* out, int cap, int kind, int seq, const unsigned char* payload, int nbits){
    int nbytes = (nbits + 7) / 8;
    int body = 9 + nbytes;
    if(body + 4 > cap) return -1;
    w_u32(out, (unsigned)body);
    out[4] = (unsigned char)kind;
    w_u32(out+5, (unsigned)seq);
    w_u32(out+9, (unsigned)nbits);
    if(nbytes) memcpy(out+13, payload, (size_t)nbytes);
    return body + 4;
}

static int build_frame(unsigned char* out, int cap, int kind, int seq, const unsigned char* payload, int n){
    int body = 9 + n;
    if(body + 4 > cap) return -1;
    w_u32(out, (unsigned)body);
    out[4] = (unsigned char)kind;
    w_u32(out+5, (unsigned)seq);
    w_u32(out+9, (unsigned)(n*8));
    if(n) memcpy(out+13, payload, (size_t)n);
    return body + 4;
}

static int build_hello(unsigned char* out, int cap, const char* user, const char* pass, const char* solar, int resumeId, const char* resumeTok){
    /* v157: 骞冲彴瀛楄妭鏀逛负銆屾湁寮€鍏虫枃浠舵墠鍙戙€嶏紝榛樿涓嶅彂 鈥斺€?杩欐牱鏈増鑳借繛鑰佺殑銆佹病鏈夊钩鍙板瓧娈电殑鏈嶅姟绔€?
     * 鎵撳紑鏂瑰紡锛氬線 files 鐩綍鏀句竴涓?sfsmp-platform.txt锛堝唴瀹归殢鎰忥級銆?*/
    int pf = (access(sfsmp_dp("sfsmp-platform.txt"), F_OK) == 0) ? 1 : 0;
    int need = 8 + 4+strlen(user) + 4+strlen(pass) + 4+strlen(solar) + 4 + 4+strlen(resumeTok) + pf;
    if(need > cap) return -1;
    unsigned char* p = out;
    w_u32(p, 0x31465353u); p += 4;
    w_u32(p, 2u);          p += 4;
    unsigned v;
    v=(unsigned)strlen(user);  w_u32(p,v); p+=4; memcpy(p,user,v);  p+=v;
    v=(unsigned)strlen(pass);  w_u32(p,v); p+=4; memcpy(p,pass,v);  p+=v;
    v=(unsigned)strlen(solar); w_u32(p,v); p+=4; memcpy(p,solar,v); p+=v;
    w_u32(p,(unsigned)resumeId); p+=4;
    v=(unsigned)strlen(resumeTok); w_u32(p,v); p+=4; memcpy(p,resumeTok,v); p+=v;
    if(pf) *p++ = 1;   /* platform=1(Android)锛氭湇鍔＄鎹璇嗗埆鎵嬫満绔€佺鐢?P2P銆佺粺璁′汉鏁?*/
    return (int)(p - out);
}

/* Lidgren NetBuffer.Write(string)锛? 浣嶇紪鐮侀暱搴?+ UTF8 */
static int lid_str(unsigned char* out, int cap, const char* s){
    size_t n = strlen(s);
    if(n >= 128 || (int)n + 1 > cap) return -1;
    out[0] = (unsigned char)n;
    memcpy(out+1, s, n);
    return (int)n + 1;
}

/* 涓氬姟鍖?= [u8 type][Lidgren 瀛楁] */
static int build_join_request(unsigned char* out, int cap, const char* user, const char* pass, const char* solar){
    int o = 0, n;
    if(cap < 4) return -1;
    out[o++] = 0;                                  /* PacketType.JoinRequest */
    n = lid_str(out+o, cap-o, user);  if(n<0) return -1; o += n;
    n = lid_str(out+o, cap-o, pass);  if(n<0) return -1; o += n;
    n = lid_str(out+o, cap-o, solar); if(n<0) return -1; o += n;
    return o;
}

/* ---------- 鍙戝嚭锛欿CP 娑堟伅 = 4 瀛楄妭闀垮害鍓嶇紑 + 甯?---------- */
static int send_msg(const unsigned char* frame, int n){
    unsigned char buf[1500];
    if(n + 4 > (int)sizeof(buf)) return -1;
    w_u32(buf, (unsigned)n);
    memcpy(buf+4, frame, (size_t)n);
    { char hx[200]; int k=0; k+=snprintf(hx+k,sizeof(hx)-k,"mp: tx n=%d hex=", n);
      for(int i=0;i<n && i<24 && k<(int)sizeof(hx)-4;i++) k+=snprintf(hx+k,sizeof(hx)-k,"%02X", frame[i]);
      k+=snprintf(hx+k,sizeof(hx)-k,"\n"); mp_log(hx); }
    { int r = ikcp_send(g_kcp, (const char*)buf, n + 4); if (r >= 0) ikcp_flush(g_kcp); return r; }
}

/* v73: 澶ф秷鎭紙瀹屾暣鐏缁撴瀯鍙揪鍑犲崄涓婄櫨 KB锛夈€侹CP 浼氳嚜宸卞垎鐗囷紝杩欓噷鍙ˉ 4 瀛楄妭闀垮害鍓嶇紑銆?*/
static int send_msg_big(const unsigned char* frame, int n){
    static unsigned char buf[(1 << 20) + 64];
    if(n + 4 > (int)sizeof(buf)) return -1;
    w_u32(buf, (unsigned)n);
    memcpy(buf+4, frame, (size_t)n);
    { char b2[96]; snprintf(b2,sizeof(b2),"mp: tx-big n=%d\n", n); mp_log(b2); }
    { int r = ikcp_send(g_kcp, (const char*)buf, n + 4); if (r >= 0) ikcp_flush(g_kcp); return r; }
}


/* ================= v73: 瀹屾暣鐏缁撴瀯涓婁紶 =================
 * 涔嬪墠 CreateRocket 鐨?parts/joints/stages 鍏ㄦ槸 0 涓?-> 鏈嶅姟绔櫥璁颁簡涓€鏋?绌烘皵鐏",
 * PC 绔?Instantiate 鍑烘潵浠€涔堥兘娌℃湁, 浜庢槸"鐪嬩笉瑙佷綅缃?銆傝繖閲屾妸鏈満鐪熷疄缁撴瀯鍙戜笂鍘汇€?
 * 鏁版嵁鐢?v64.c 浠?RocketSave 閲岃鍑烘潵(娓告垙鑷繁鐨勪繚瀛樼粨鏋?, 杩欓噷鍙礋璐ｇ紪鐮併€?
 */
#define MP_MAX_PARTS  1024
#define MP_MAX_JOINTS 4096
#define MP_MAX_STAGES 64
#define MP_MAX_STAGE_IDS 256

typedef mp_up_part_t  mp_part_rec;
typedef mp_up_joint_t mp_joint_rec;
typedef mp_up_stage_t mp_stage_rec;

static char g_st_name[64] = "";
static mp_part_rec g_st_parts[MP_MAX_PARTS];
static int g_st_nparts = 0;
static mp_joint_rec g_st_joints[MP_MAX_JOINTS];
static int g_st_njoints = 0;
static mp_stage_rec g_st_stages[MP_MAX_STAGES];
static int g_st_nstages = 0;

void mp_reset_structure(void){ g_st_nparts = 0; g_st_njoints = 0; g_st_nstages = 0; g_st_name[0] = 0; }
int  mp_structure_ready(void){ return g_st_nparts > 0; }

void mp_set_structure(const char* name, const mp_part_rec* parts, int nparts,
                      const mp_joint_rec* joints, int njoints,
                      const mp_stage_rec* stages, int nstages){
    int i;
    mp_reset_structure();
    /* v111: 鍚嶅瓧蹇呴』鏃犳潯浠惰鐩栥€傝€佷唬鐮佸彧鍦?name 闈炵┖鏃跺啓锛屽鑷?g_st_name 淇濈暀涓婁竴娆＄殑鍊?
     * 锛堢涓€杞嫢琚啓鎴?'MP-Remote'锛屼箣鍚庢瘡娆′紶绌哄悕瀛楅兘杩樻槸瀹冿級锛?
     * 浜庢槸鏈嶅姟绔妸鎴戜滑鐨勭伀绠敞鍐屾垚 'MP-Remote'锛孭C 绔氨鐢诲嚭涓€涓寕鐫€杩欎釜鍋囧悕瀛楃殑鐏銆?*/
    if(name && name[0]) snprintf(g_st_name, sizeof(g_st_name), "%s", name);
    else g_st_name[0] = 0;
    if(nparts > MP_MAX_PARTS) nparts = MP_MAX_PARTS;
    for(i=0;i<nparts;i++) g_st_parts[i] = parts[i];
    g_st_nparts = nparts;
    if(njoints > MP_MAX_JOINTS) njoints = MP_MAX_JOINTS;
    for(i=0;i<njoints;i++) g_st_joints[i] = joints[i];
    g_st_njoints = njoints;
    if(nstages > MP_MAX_STAGES) nstages = MP_MAX_STAGES;
    for(i=0;i<nstages;i++) g_st_stages[i] = stages[i];
    g_st_nstages = nstages;
    { char b2[160]; snprintf(b2,sizeof(b2),"mp: structure set parts=%d joints=%d stages=%d name='%s'\n",
                             g_st_nparts, g_st_njoints, g_st_nstages, g_st_name); mp_log(b2); }
}

/* 澶х紦鍐蹭綅鍐欏叆锛圠idgren 浣嶅簭锛孡SB first锛?*/
typedef struct { unsigned char* buf; int cap; int bit; } bw2_t;
static int bw2_bits(bw2_t* w, unsigned long long v, int n){
    int i;
    for(i=0;i<n;i++){
        int byte = w->bit >> 3, off = w->bit & 7;
        if(byte >= w->cap) return -1;
        if((v >> i) & 1ull) w->buf[byte] |= (unsigned char)(1u << off);
        else                w->buf[byte] &= (unsigned char)~(1u << off);
        w->bit++;
    }
    return 0;
}
static void bw2_u8(bw2_t* w, unsigned v){ bw2_bits(w, v & 0xffu, 8); }
static void bw2_i32(bw2_t* w, int v){ bw2_bits(w, (unsigned long long)(unsigned)v, 32); }
static void bw2_f32(bw2_t* w, float v){ unsigned b; memcpy(&b,&v,4); bw2_bits(w, b, 32); }
static void bw2_f64(bw2_t* w, double v){ unsigned long long b; memcpy(&b,&v,8); bw2_bits(w, b, 64); }
static void bw2_bool(bw2_t* w, int v){ bw2_bits(w, v?1ull:0ull, 1); }
/* Lidgren Write(string): 7 浣嶅彉闀垮瓧鑺傞暱搴?+ UTF8 */
static void bw2_str(bw2_t* w, const char* s){
    size_t n = s ? strlen(s) : 0;
    unsigned long long len = (unsigned long long)n;
    int i;
    for(;;){
        unsigned v = (unsigned)(len & 0x7fu);
        len >>= 7;
        bw2_bits(w, v | (len ? 0x80u : 0u), 8);
        if(!len) break;
    }
    for(i=0;i<(int)n;i++) bw2_bits(w, (unsigned char)s[i], 8);
}

static int mp_send_create_rocket_minimal(void){
    bw_t w; bw_clear(&w);
    bw_u8(&w, 9);                       /* PacketType.CreateRocket */
    bw_f64(&w, 0.0);                    /* world_time 鐢辨湇鍔＄琛?*/
    bw_i32(&w, g_local_id);             /* local_id */
    bw_i32(&w, -1);                     /* global_id 浜ょ粰鏈嶅姟绔垎閰?*/
    bw_bool(&w, 0);                     /* for_launch */
    bw_str(&w, "MP-Android");           /* rocket_name */
    bw_f64(&w, g_lx); bw_f64(&w, g_ly); bw_f64(&w, g_lvx); bw_f64(&w, g_lvy);
    bw_str(&w, g_lplanet);
    bw_f32(&w, g_lrot); bw_f32(&w, 0.0f);
    bw_bool(&w, 0); bw_f32(&w, 0.0f); bw_bool(&w, 0);   /* throttle_on, throttle, rcs */
    bw_i32(&w, 0); bw_i32(&w, 0); bw_i32(&w, 0);        /* parts, joints, stages */
    unsigned char frame[1200];
    int fn = build_frame_bits(frame, sizeof(frame), 3 /*Packet*/, 2, w.buf, w.bit);
    if(fn <= 0) return -1;
    { char hx[96]; snprintf(hx,sizeof(hx),"mp: CreateRocket minimal (no structure)\n"); mp_log(hx); }
    return send_msg(frame, fn);
}

/* v73: 甯︾湡瀹炵粨鏋勭殑 CreateRocket銆傛秷鎭彲鑳戒笂鐧?KB -> 鑷繁鐨勫ぇ缂撳啿 + 澶у抚缂撳啿銆?*/
static int mp_send_create_rocket_full(void){
    static unsigned char pay[1 << 20];
    static unsigned char frm[(1 << 20) + 128];
    bw2_t w;
    int i, j;
    memset(pay, 0, sizeof(pay));
    w.buf = pay; w.cap = (int)sizeof(pay); w.bit = 0;
    bw2_u8(&w, 9);                       /* PacketType.CreateRocket */
    bw2_f64(&w, 0.0);                    /* world_time 鏈嶅姟绔ˉ */
    bw2_i32(&w, g_local_id);             /* local_id: changes when local rocket changes */
    bw2_i32(&w, g_global_id);            /* existing gid -> structure overwrite; -1 -> new rocket */
    bw2_bool(&w, 0);                     /* for_launch */
    bw2_str(&w, g_st_name[0] ? g_st_name : "MP-Android");
    bw2_f64(&w, g_lx); bw2_f64(&w, g_ly); bw2_f64(&w, g_lvx); bw2_f64(&w, g_lvy);
    bw2_str(&w, g_lplanet);
    bw2_f32(&w, g_lrot); bw2_f32(&w, 0.0f);
    bw2_bool(&w, 0); bw2_f32(&w, 0.0f); bw2_bool(&w, 0);

    bw2_i32(&w, g_st_nparts);
    for(i=0;i<g_st_nparts;i++){
        const mp_part_rec* p = &g_st_parts[i];
        bw2_i32(&w, p->id ? p->id : (i + 1));   /* deterministic part id (PC-compatible) */
        bw2_str(&w, p->name);
        bw2_f32(&w, p->x); bw2_f32(&w, p->y);
        bw2_f32(&w, p->ox); bw2_f32(&w, p->oy); bw2_f32(&w, p->oz);
        bw2_f32(&w, p->temp);
        /* v126: 鍙橀噺瀛楀吀蹇呴』璺熺潃璧帮紝鍚﹀垯 PC 绔缓鍑烘潵鐨勯浂浠跺叏鐢ㄩ粯璁ゅ€硷紙灏哄姘歌繙=1锛?*/
        { int k;
          bw2_i32(&w, p->nnv);
          for(k=0;k<p->nnv;k++){ bw2_str(&w, p->nv[k].name); bw2_f64(&w, p->nv[k].v); }
          bw2_i32(&w, p->ntv);
          for(k=0;k<p->ntv;k++){ bw2_str(&w, p->tv[k].name); bw2_bool(&w, p->tv[k].v); }
          bw2_i32(&w, p->nxv);
          for(k=0;k<p->nxv;k++){ bw2_str(&w, p->xv[k].name); bw2_str(&w, p->xv[k].val); }
        }
        bw2_bool(&w, 1);                 /* burns 涓虹┖ */
    }
    bw2_i32(&w, g_st_njoints);
    for(i=0;i<g_st_njoints;i++){
        bw2_i32(&w, g_st_joints[i].a);
        bw2_i32(&w, g_st_joints[i].b);
    }
    bw2_i32(&w, g_st_nstages);
    for(i=0;i<g_st_nstages;i++){
        const mp_stage_rec* s = &g_st_stages[i];
        bw2_i32(&w, s->stage_id);
        bw2_i32(&w, s->n);
        for(j=0;j<s->n;j++) bw2_i32(&w, s->ids[j]);
    }
    {
        int nbytes = (w.bit + 7) / 8;
        int body = 9 + nbytes;
        if(body + 4 > (int)sizeof(frm)) { mp_log("mp: CreateRocket too big for frame buffer\n"); return -1; }
        w_u32(frm, (unsigned)body);
        frm[4] = 3;                      /* kind = Packet */
        w_u32(frm + 5, 2);
        w_u32(frm + 9, (unsigned)w.bit);
        memcpy(frm + 13, pay, (size_t)nbytes);
        { char b2[160]; snprintf(b2,sizeof(b2),"mp: CreateRocket full tx n=%d parts=%d joints=%d stages=%d\n",
                                 body + 4, g_st_nparts, g_st_njoints, g_st_nstages); mp_log(b2); }
        return send_msg_big(frm, body + 4);
    }
}

static int mp_send_create_rocket(void){
    if(g_st_nparts > 0) return mp_send_create_rocket_full();
    return mp_send_create_rocket_minimal();
}

/* Outbound DestroyPart events are produced by the Unity main thread and sent only by the network thread. */
#define MP_TX_DP_MAX 256
static pthread_mutex_t g_tx_dp_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { int part_id; int create_explosion; unsigned reason; } g_tx_dp[MP_TX_DP_MAX];
static int g_tx_dp_head = 0, g_tx_dp_count = 0;

void mp_queue_destroy_part(int part_id, int create_explosion, unsigned reason){
    if(part_id < 0) return;
    if(g_global_id < 0){ mp_log("mp: DestroyPart dropped before global id\n"); return; }
    pthread_mutex_lock(&g_tx_dp_lock);
    for(int i = 0; i < g_tx_dp_count; ++i){
        int at = (g_tx_dp_head + i) % MP_TX_DP_MAX;
        if(g_tx_dp[at].part_id == part_id){ pthread_mutex_unlock(&g_tx_dp_lock); return; }
    }
    if(g_tx_dp_count == MP_TX_DP_MAX){
        g_tx_dp_head = (g_tx_dp_head + 1) % MP_TX_DP_MAX;
        --g_tx_dp_count;
    }
    int tail = (g_tx_dp_head + g_tx_dp_count) % MP_TX_DP_MAX;
    g_tx_dp[tail].part_id = part_id;
    g_tx_dp[tail].create_explosion = create_explosion;
    g_tx_dp[tail].reason = reason;
    ++g_tx_dp_count;
    pthread_mutex_unlock(&g_tx_dp_lock);
}

static int mp_send_destroy_part_now(int part_id, int create_explosion, unsigned reason){
    bw_t w; bw_clear(&w);
    bw_u8(&w, 13);                       /* PacketType.DestroyPart */
    bw_f64(&w, 0.0);
    bw_i32(&w, g_global_id);
    bw_i32(&w, part_id);
    bw_bool(&w, create_explosion);
    bw_u8(&w, reason);
    unsigned char frame[64];
    int fn = build_frame_bits(frame, sizeof(frame), 3, 6, w.buf, w.bit);
    return fn > 0 ? send_msg(frame, fn) : -1;
}

static void mp_drain_destroy_parts(void){
    if(g_global_id < 0) return;
    for(int guard = 0; guard < MP_TX_DP_MAX; ++guard){
        int part_id, create_explosion;
        unsigned reason;
        pthread_mutex_lock(&g_tx_dp_lock);
        if(g_tx_dp_count == 0){ pthread_mutex_unlock(&g_tx_dp_lock); return; }
        part_id = g_tx_dp[g_tx_dp_head].part_id;
        create_explosion = g_tx_dp[g_tx_dp_head].create_explosion;
        reason = g_tx_dp[g_tx_dp_head].reason;
        g_tx_dp_head = (g_tx_dp_head + 1) % MP_TX_DP_MAX;
        --g_tx_dp_count;
        pthread_mutex_unlock(&g_tx_dp_lock);
        int sent = mp_send_destroy_part_now(part_id, create_explosion, reason);
        char b[128]; snprintf(b,sizeof(b),"mp: tx DESTROY PART id=%d part=%d ret=%d\n", g_global_id, part_id, sent); mp_log(b);
    }
}

static int mp_send_destroy_rocket_now(int rocket_id){
    bw_t w; bw_clear(&w);
    bw_u8(&w, 10);                      /* PacketType.DestroyRocket */
    bw_f64(&w, 0.0);
    bw_i32(&w, rocket_id);
    bw_u8(&w, 0);                       /* Intentional */
    unsigned char frame[64];
    int fn = build_frame_bits(frame, sizeof(frame), 3, 7, w.buf, w.bit);
    return fn > 0 ? send_msg(frame, fn) : -1;
}

#define MP_TX_DR_MAX 16
static pthread_mutex_t g_tx_dr_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_tx_dr[MP_TX_DR_MAX];
static int g_tx_dr_n = 0;

void mp_queue_destroy_rocket(int rocket_id){
    if(rocket_id < 0) return;
    pthread_mutex_lock(&g_tx_dr_lock);
    for(int i=0;i<g_tx_dr_n;i++) if(g_tx_dr[i] == rocket_id){ pthread_mutex_unlock(&g_tx_dr_lock); return; }
    if(g_tx_dr_n < MP_TX_DR_MAX) g_tx_dr[g_tx_dr_n++] = rocket_id;
    pthread_mutex_unlock(&g_tx_dr_lock);
}

static void mp_drain_destroy_rockets(void){
    for(int guard=0; guard<MP_TX_DR_MAX; ++guard){
        int gid;
        pthread_mutex_lock(&g_tx_dr_lock);
        if(g_tx_dr_n == 0){ pthread_mutex_unlock(&g_tx_dr_lock); return; }
        gid = g_tx_dr[0];
        for(int i=1;i<g_tx_dr_n;i++) g_tx_dr[i-1]=g_tx_dr[i];
        --g_tx_dr_n;
        pthread_mutex_unlock(&g_tx_dr_lock);

        int ret = mp_send_destroy_rocket_now(gid);
        { mp_rrocket_t* rr = rr_slot(gid);
          if(rr){ rr->valid=0; rr->has_structure=0; rr->dirty=1; } }
        { char b[128]; snprintf(b,sizeof(b),"mp: clear pad rocket gid=%d ret=%d\n", gid, ret); mp_log(b); }
    }
}

void mp_on_new_local_rocket(void){
    int old = g_global_id;
    if(old >= 0) g_destroy_old_gid = old;
    g_global_id = -1;
    g_create_sent = 0;
    mp_reset_structure();
    pthread_mutex_lock(&g_tx_dp_lock);
    g_tx_dp_head = 0; g_tx_dp_count = 0;
    pthread_mutex_unlock(&g_tx_dp_lock);
    ++g_local_id;
    if(g_local_id <= 0) g_local_id = 1;
    { char b[128]; snprintf(b,sizeof(b),"mp: new local rocket local_id=%d old_gid=%d\n", g_local_id, old); mp_log(b); }
}

void mp_reupload_structure(void){
    /* 鏈湴鐏瀵硅薄鍙樺寲锛堝垎绾?鎷嗗垎/鎺у埗鍒囨崲锛夛細淇濈暀褰撳墠 gid锛屽彧閲嶄紶缁撴瀯瑕嗙洊锛?
       涓嶅啀 DestroyRocket 鏃?gid + CreateRocket 鏂?gid锛岄伩鍏?PC 绔伀绠秷澶?閲嶅缓鍜岀鎾炵垎鐐搞€?*/
    g_create_sent = 0;
    { char b[128]; snprintf(b,sizeof(b),"mp: reupload structure on gid=%d local_id=%d\n", g_global_id, g_local_id); mp_log(b); }
}

static int mp_send_primary(void){
    bw_t w; bw_clear(&w);
    bw_u8(&w, 11);                      /* PacketType.UpdateRocketPrimary */
    bw_f64(&w, 0.0);
    bw_i32(&w, g_global_id >= 0 ? g_global_id : 0);
    bw_f64(&w, g_lx); bw_f64(&w, g_ly); bw_f64(&w, g_lvx); bw_f64(&w, g_lvy);
    bw_str(&w, g_lplanet);
    bw_f32(&w, g_lrot); bw_f32(&w, g_lav);   /* v107: angular_velocity 鍙戠湡瀹炲€硷紝涓嶅啀鎭?0 */
    bw_u32(&w, 0);
    unsigned char frame[1200];
    int fn = build_frame_bits(frame, sizeof(frame), 3, 3, w.buf, w.bit);
    if(fn <= 0) return -1;
    /* v72: 姣忛殧 1 绉掑洖鏄句竴娆＄湡姝ｅ彂鍑哄幓鐨勫唴瀹癸紝鐢ㄦ潵纭浣嶅Э鍦ㄥ彉 */
    static unsigned long long last_tx_log = 0;
    unsigned long long t = mp_now_ms();
    if(t - last_tx_log >= 1000){
        last_tx_log = t;
        char tb[160];
        snprintf(tb,sizeof(tb),"mp: tx primary id=%d planet='%s' pos=(%.2f, %.2f) vel=(%.2f, %.2f) rot=%.2f\n",
                 g_global_id, g_lplanet, g_lx, g_ly, g_lvx, g_lvy, g_lrot);
        mp_log(tb);
    }
    return send_msg(frame, fn);
}

/* v72: 澹版槑"杩欐灇鐏褰掓垜鎺у埗"銆傛湇鍔＄ has_authority 鍙 controlled_rocket_锛?
 * 涓嶅彂杩欐潯 -> 鏉冨▉鎸?鏈€杩戠帺瀹?杞"鍒嗙粰鍒汉 -> 鎴戜滑鐨?UpdateRocketPrimary 鍏ㄨ涓㈠純銆?*/
static int mp_send_control(int rocket_id){
    bw_t w; bw_clear(&w);
    bw_u8(&w, 4);                       /* PacketType.UpdatePlayerControl */
    bw_i32(&w, 0);                      /* player_id锛氭湇鍔＄涓€寰嬬敤璁よ瘉韬唤瑕嗙洊 */
    bw_i32(&w, rocket_id);              /* rocket_id */
    unsigned char frame[64];
    int fn = build_frame_bits(frame, sizeof(frame), 3, 4, w.buf, w.bit);
    if(fn <= 0) return -1;
    return send_msg(frame, fn);
}

static void mp_log_ack(const unsigned char* ack, int n){
    char b[256];
    if(n < 8) return;
    const unsigned char* p = ack;
    unsigned magic = r_u32(p); p += 4;
    unsigned ver   = r_u32(p); p += 4;
    int pid = (int)r_u32(p); p += 4;
    double period = 0, chatcd = 0, wtime = 0, stime = 0;
    if(p + 8 <= ack + n){ period = (double)0; unsigned long long bits=r_u64(p); memcpy(&period,&bits,8); p += 8; }
    if(p + 8 <= ack + n){ unsigned long long bits=r_u64(p); memcpy(&chatcd,&bits,8); p += 8; }
    if(p + 8 <= ack + n){ unsigned long long bits=r_u64(p); memcpy(&wtime,&bits,8); p += 8; }
    if(p + 8 <= ack + n){ unsigned long long bits=r_u64(p); memcpy(&stime,&bits,8); p += 8; }
    if (wtime > 0.0) { g_server_world_time = wtime; g_have_world_time = 1; }   /* v97 */
    snprintf(b,sizeof(b),"mp: HANDSHAKE OK magic=%08x ver=%u playerId=%d worldTime=%.2f period=%.3f\n",
             magic, ver, pid, wtime, period);
    mp_log(b);
}

static int g_mp_started;   /* v164: 鍓嶇疆澹版槑锛堝畾涔夊湪鍚庨潰锛夛紝澶辫触鏃惰鑳藉浣?*/

static void* mp_thread(void* arg){
    (void)arg;
    char host[96]; snprintf(host, sizeof(host), "%s", g_host);
    int port = g_port;
    char b[192];
    snprintf(b,sizeof(b),"mp: connecting %s:%d ...\n", host, port); mp_log(b);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_DGRAM;   /* v162: 鍏佽 IPv6 */
    char ps[16]; snprintf(ps,sizeof(ps),"%d",port);
    { int gi = getaddrinfo(host, ps, &hints, &res);
      if(gi != 0 || !res){
        snprintf(b,sizeof(b),"mp: DNS failed for %s (gai=%d)\n", host, gi); mp_log(b);
        g_mp_started = 0; return NULL;
      } }
    /* v164: 鐓ф妱绔父鐨勮繛鎺ユ満鍒?鈥斺€?閫愪釜鍊欓€夊湴鍧€璇曪紙鍩熷悕甯稿悓鏃剁粰 A 鍜?AAAA锛夛紝
     * 澶辫触涔熻澶嶄綅 g_mp_started锛屽惁鍒欎竴娆″け璐ヤ箣鍚庢墍鏈夐噸璇曢兘琚尅鍦ㄩ棬澶栥€?*/
    { struct addrinfo* ai = NULL; int cand = 0;
      for(ai = res; ai; ai = ai->ai_next){
          cand++;
          int fd = socket(ai->ai_family, SOCK_DGRAM, 0);
          if(fd < 0) continue;
          if(connect(fd, ai->ai_addr, ai->ai_addrlen) == 0){
              char ip[64] = "?"; void* src = NULL; int fam = ai->ai_family;
              g_fd = fd;
              if(fam == AF_INET6) src = &((struct sockaddr_in6*)ai->ai_addr)->sin6_addr;
              else if(fam == AF_INET) src = &((struct sockaddr_in*)ai->ai_addr)->sin_addr;
              if(src) inet_ntop(fam, src, ip, sizeof(ip));
              snprintf(b,sizeof(b),"mp: resolved %s -> [%s]:%d af=%d cand=%d\n", host, ip, port, fam, cand); mp_log(b);
              snprintf(g_dg_addr,sizeof(g_dg_addr),"%s",ip); g_dg_af = fam; g_dg_cand = cand;
              break;
          }
          close(fd);
      }
      freeaddrinfo(res); }
    if(g_fd < 0){
        snprintf(b,sizeof(b),"mp: connect failed errno=%d (all candidates tried)\n", errno); mp_log(b);
        g_mp_started = 0; return NULL;
    }

    int fl = fcntl(g_fd, F_GETFL, 0); fcntl(g_fd, F_SETFL, fl | O_NONBLOCK);

    g_kcp = ikcp_create((IUINT32)(mp_now_ms() & 0x7fffffff), NULL);
    if(!g_kcp){ mp_log("mp: ikcp_create failed\n"); g_mp_started = 0; return NULL; }
    ikcp_setoutput(g_kcp, mp_output);
    /* v186锛欿CP 鍙傛暟鍏ㄩ潰瀵归綈鏈嶅姟绔紙Server/cpp/src/net/kcp_transport.hpp + ADR-001锛夈€?
     * 涔嬪墠鍙榻愪簡 nodelay锛屾紡浜?MTU 鏄惧紡璁剧疆鍜屾帴鏀剁獥鍙ｏ紙鏈嶅姟绔?256/512锛屾垜浠竴鐩存槸 256/256锛?
     * 蹇収/缁撴瀯鍖呬竴澶氬氨鍘嬩笉浣忥紝鍥炲寘寤惰繜琚斁澶э級銆?*/
    ikcp_nodelay(g_kcp, 1, 10, 0, 1);   /* kNodelay=1, kIntervalMs=10, kFastResend=0, kNoCongestion=1 */
    ikcp_setmtu(g_kcp, 1400);           /* kMtu = 1400 */
    ikcp_wndsize(g_kcp, 256, 512);      /* 涓庢湇鍔＄ ikcp_wndsize(256, 512) 涓€鑷?*/

    unsigned char hello[512];
    int hn = build_hello(hello, sizeof(hello), "android", "", "", -1, "");
    unsigned char frame[600];
    int fn = build_frame(frame, sizeof(frame), 1 /*Hello*/, 2 /*= 鎻℃墜鐗堟湰鍙凤紝涓嶆槸搴忓彿*/, hello, hn);
    send_msg(frame, fn);
    mp_log("mp: hello sent\n");

    unsigned long long last_log = 0;
    int got_ack = 0;
    unsigned char rx[2048];
    static unsigned char kb[65536];   /* v72: 鏀跺寘缂撳啿鏀惧ぇ鍒?64KB锛屼箣鍓?4KB 涓€鏃︽敹鍒板ぇ鍖呭氨姘镐箙鍫垫 */
    for(int iter = 0; ; iter++){                     /* v65: 甯搁┗锛屼笉鍐?90 绉掑悗鑷繁閫€鍑?*/
        unsigned long long now = mp_now_ms();
        /* v186锛氭寜 ikcp_check 鐨勮繑鍥炲€艰皟搴?update锛堟湇鍔＄ kcp_transport.cpp:455-462 鍚屾绛栫暐锛夛紝
         * 涓嶅啀姣忚疆鏃犳潯浠?update+flush 鈥斺€?鍘熸潵姣?10ms 閮藉己鍒?flush锛岀┖杞敜閱掑銆丆PU 鎶栧姩澶с€?*/
        if (g_kcp_next == 0 || (IUINT32)now >= g_kcp_next) {
            ikcp_update(g_kcp, (IUINT32)now);
            g_kcp_next = ikcp_check(g_kcp, (IUINT32)now);
        }

        int rn = (int)recv(g_fd, rx, sizeof(rx), 0);
        if(rn > 0){
            ikcp_input(g_kcp, (const char*)rx, rn);
            while(1){
                int n = ikcp_recv(g_kcp, (char*)kb, (int)sizeof(kb));
                if(n <= 0) break;
                if(n < 4) continue;
                int len = (int)r_u32(kb);
                if(len < 9 || len > n - 4) continue;
                unsigned char* f = kb + 4;
                int kind = f[4];
                int payloadBits = (int)r_u32(f+9);
                if(payloadBits < 0 || payloadBits > (len - 9) * 8) continue;
                int plen = (payloadBits + 7) / 8;
                g_last_rx_ms = mp_now_ms();
                if(!got_ack && kind == 2 /*HelloAck*/){
                    mp_log_ack(f + 13, plen);
                    got_ack = 1;
                    /* 绔嬪埢鍙戜笟鍔″寘 JoinRequest(type=0) 瀹屾垚 auth */
                    unsigned char jp[256]; int jn = build_join_request(jp, sizeof(jp), "android", "", "");
                    if(jn > 0){
                        unsigned char jf[300]; int jfn = build_frame(jf, sizeof(jf), 3 /*Packet*/, 1, jp, jn);
                        send_msg(jf, jfn);
                        mp_log("mp: join request sent\n");
                    } else mp_log("mp: build join failed\n");
                } else if(kind == 3 /*Packet*/ && plen > 0){
                    int ptype = f[13];
                    if(ptype == 1 /*JoinResponse*/){
                        const unsigned char* q = f + 14;
                        int pid = (int)r_u32(q); q += 4;
                        double v[4]; for(int i=0;i<4;i++){ unsigned long long bits=r_u64(q); memcpy(&v[i],&bits,8); q += 8; }
                        unsigned diff = *q++;
                        char solar[64]=""; int l = *q++; if(l>0 && l<64){ memcpy(solar,q,l); solar[l]=0; q+=l; }
                        snprintf(b,sizeof(b),"mp: JOIN OK playerId=%d worldTime=%.2f diff=%u solar='%s'\n", pid, v[2], diff, solar);
                        mp_log(b);
                    }
                    g_joined = 1;
                    if(ptype == 6 /*UpdateWorldTime*/){
                        if(plen >= 9){
                            double wt = 0.0; memcpy(&wt, f + 14, 8);
                            g_server_world_time = wt; g_have_world_time = 1;
                            snprintf(b,sizeof(b),"mp: WORLD TIME %.2f\n", wt); mp_log(b);
                        }
                    } else if(ptype == 2 /*PlayerConnected*/){
                        br_t pr = { f + 14, payloadBits, 0, 0 };
                        int pid = br_i32(&pr);
                        char un[40] = ""; br_str(&pr, un, sizeof(un));
                        (void)br_f32(&pr); (void)br_f32(&pr); (void)br_f32(&pr);
                        int print = br_bool(&pr);
                        snprintf(b,sizeof(b),"mp: PLAYER CONNECTED id=%d name='%s' print=%d\n", pid, un, print); mp_log(b);
                    } else if(ptype == 3 /*PlayerDisconnected*/){
                        br_t pr = { f + 14, payloadBits, 0, 0 };
                        int pid = br_i32(&pr);
                        snprintf(b,sizeof(b),"mp: PLAYER DISCONNECTED id=%d\n", pid); mp_log(b);
                    } else if(ptype == 9 /*CreateRocket*/){
                        /* v74: 鏁存５闆朵欢鏍戣В鍑烘潵锛屼緵 v64 鐢熸垚杩滅鐏 */
                        int lid9 = -1, gid9 = -1;
                        rr_parse_create(f + 14, plen - 1, &lid9, &gid9);
                        if(lid9 == g_local_id && gid9 >= 0 && g_global_id < 0){
                            g_global_id = gid9;
                            snprintf(b,sizeof(b),"mp: OUR ROCKET registered global_id=%d\n", gid9); mp_log(b);
                        }
                    } else if(ptype == 11 /*UpdateRocketPrimary*/){
                        const unsigned char* q = f + 14;
                        unsigned long long bits; double wt; int rid;
                        memcpy(&wt, q, 8); q += 8;
                        rid = (int)r_u32(q); q += 4;
                        double px,py,vx,vy;
                        memcpy(&px,q,8); q+=8; memcpy(&py,q,8); q+=8;
                        memcpy(&vx,q,8); q+=8; memcpy(&vy,q,8); q+=8;
                        int alen = *q++; char pl[40]=""; if(alen>0 && alen<40){ memcpy(pl,q,alen); pl[alen]=0; q+=alen; }
                        float rot; memcpy(&rot,q,4); q+=4;
                        float av = 0.0f;                       /* v78: primary 鏈熬甯﹁閫熷害 */
                        if(q + 4 <= f + plen){ memcpy(&av,q,4); q+=4; }
                        g_rx_primary++;
                        mp_remote_t* rs = remote_slot(rid);
                        rs->x=px; rs->y=py; rs->vx=vx; rs->vy=vy; rs->rot=rot; rs->valid=1;
                        { mp_rrocket_t* rp = rr_find(rid);   /* v74: 缁撴瀯宸插埌鏃跺悓姝ヤ綅缃?*/
                          if(rp){ rp->x=px; rp->y=py; rp->vx=vx; rp->vy=vy; rp->rot=rot; rp->av=av; rp->valid=1;
                                  if(pl[0]) snprintf(rp->planet, sizeof(rp->planet), "%s", pl);
                                  rr_push_snap(rp, px, py, vx, vy, rot, av); } }
                        snprintf(b,sizeof(b),"mp: ROCKET id=%d planet='%s' pos=(%.1f, %.1f) vel=(%.2f, %.2f) rot=%.1f\n",
                                 rid, pl, px, py, vx, vy, rot);
                        mp_log(b);
                    } else if(ptype == 12 /*UpdateRocketSecondary*/){
                        br_t sr = { f + 14, payloadBits, 0, 0 };
                        (void)br_f64(&sr);
                        int rid = br_i32(&sr);
                        float turn = br_f32(&sr);
                        float rx = br_f32(&sr), ry = br_f32(&sr);
                        float hx = br_f32(&sr), hy = br_f32(&sr);
                        float vx = br_f32(&sr), vy = br_f32(&sr);
                        float thr = br_f32(&sr);
                        int ton = br_bool(&sr), rcs = br_bool(&sr);
                        mp_rrocket_t* srp = rr_find(rid);
                        if(srp){
                            srp->sec_turn=turn; srp->sec_raw_x=rx; srp->sec_raw_y=ry;
                            srp->sec_horiz_x=hx; srp->sec_horiz_y=hy; srp->sec_vert_x=vx; srp->sec_vert_y=vy;
                            srp->sec_throttle_on=ton; srp->sec_rcs=rcs;
                        }
                        snprintf(b,sizeof(b),"mp: SECONDARY gid=%d turn=%.2f throttle=%.2f on=%d rcs=%d\n", rid, turn, thr, ton, rcs); mp_log(b);
                    } else if(ptype == 15 /*EngineModule*/){
                        br_t mr = { f + 14, payloadBits, 0, 0 };
                        (void)br_f64(&mr); int rid = br_i32(&mr); int pid = br_i32(&mr); int on = br_bool(&mr);
                        mp_rrocket_t* rp = rr_find(rid);
                        if(rp){ for(int i=0;i<rp->nparts;i++) if(rp->parts[i].id==pid){ rp->parts[i].engine_on=on; break; } rr_set_toggle(rp,pid,"engine_on",on); }
                        snprintf(b,sizeof(b),"mp: ENGINE gid=%d part=%d on=%d\n", rid, pid, on); mp_log(b);
                    } else if(ptype == 16 /*WheelModule*/){
                        br_t mr = { f + 14, payloadBits, 0, 0 };
                        (void)br_f64(&mr); int rid = br_i32(&mr); int pid = br_i32(&mr); int on = br_bool(&mr);
                        mp_rrocket_t* rp = rr_find(rid);
                        if(rp){ for(int i=0;i<rp->nparts;i++) if(rp->parts[i].id==pid){ rp->parts[i].wheel_on=on; break; } rr_set_toggle(rp,pid,"wheel_on",on); }
                        snprintf(b,sizeof(b),"mp: WHEEL gid=%d part=%d on=%d\n", rid, pid, on); mp_log(b);
                    } else if(ptype == 17 /*BoosterModule*/){
                        br_t mr = { f + 14, payloadBits, 0, 0 };
                        (void)br_f64(&mr); int rid = br_i32(&mr); int pid = br_i32(&mr);
                        int primed = br_bool(&mr); float th = br_f32(&mr); float fuel = br_f32(&mr);
                        mp_rrocket_t* rp = rr_find(rid);
                        if(rp){ for(int i=0;i<rp->nparts;i++) if(rp->parts[i].id==pid){ rp->parts[i].booster_primed=primed; rp->parts[i].booster_throttle=th; rp->parts[i].booster_fuel=fuel; break; } rr_set_toggle(rp,pid,"booster_primed",primed); rr_set_num(rp,pid,"fuel_percent",fuel); }
                        snprintf(b,sizeof(b),"mp: BOOSTER gid=%d part=%d primed=%d th=%.2f fuel=%.2f\n", rid, pid, primed, th, fuel); mp_log(b);
                    } else if(ptype == 18 /*ParachuteModule*/){
                        br_t mr = { f + 14, payloadBits, 0, 0 };
                        (void)br_f64(&mr); int rid = br_i32(&mr); int pid = br_i32(&mr);
                        float st = br_f32(&mr); float tg = br_f32(&mr);
                        mp_rrocket_t* rp = rr_find(rid);
                        if(rp){ for(int i=0;i<rp->nparts;i++) if(rp->parts[i].id==pid){ rp->parts[i].chute_state=st; rp->parts[i].chute_target=tg; break; } rr_set_num(rp,pid,"animation_state",st); rr_set_num(rp,pid,"deploy_state",tg); }
                        snprintf(b,sizeof(b),"mp: CHUTE gid=%d part=%d state=%.2f target=%.2f\n", rid, pid, st, tg); mp_log(b);
                    } else if(ptype == 19 /*MoveModule*/){
                        br_t mr = { f + 14, payloadBits, 0, 0 };
                        (void)br_f64(&mr); int rid = br_i32(&mr); int pid = br_i32(&mr);
                        float tm = br_f32(&mr); float tt = br_f32(&mr);
                        mp_rrocket_t* rp = rr_find(rid);
                        if(rp){ for(int i=0;i<rp->nparts;i++) if(rp->parts[i].id==pid){ rp->parts[i].move_time=tm; rp->parts[i].move_target=tt; break; } rr_set_num(rp,pid,"state",tm); rr_set_num(rp,pid,"state_target",tt); }
                        snprintf(b,sizeof(b),"mp: MOVE gid=%d part=%d time=%.2f target=%.2f\n", rid, pid, tm, tt); mp_log(b);
                    } else if(ptype == 20 /*ResourceModule*/){
                        br_t mr = { f + 14, payloadBits, 0, 0 };
                        (void)br_f64(&mr); int rid = br_i32(&mr);
                        double pct = br_f64(&mr); int cnt = br_i32(&mr);
                        if(cnt < 0) cnt = 0; if(cnt > 512) cnt = 512;
                        mp_rrocket_t* rp = rr_find(rid);
                        for(int i=0;i<cnt;i++){
                            int pid = br_i32(&mr);
                            if(rp) for(int j=0;j<rp->nparts;j++) if(rp->parts[j].id==pid){ rp->parts[j].resource_percent=pct; break; }
                        }
                        snprintf(b,sizeof(b),"mp: RESOURCE gid=%d pct=%.3f n=%d\n", rid, pct, cnt); mp_log(b);
                    } else if(ptype == 27 /*RenameRocket*/){
                        br_t nr = { f + 14, payloadBits, 0, 0 };
                        int rid = br_i32(&nr);
                        char nm[48] = ""; br_str(&nr, nm, sizeof(nm));
                        mp_rrocket_t* nrp = rr_find(rid);
                        if(nrp){ snprintf(nrp->name, sizeof(nrp->name), "%s", nm); nrp->dirty = 1; }
                        snprintf(b,sizeof(b),"mp: RENAME gid=%d name='%s'\n", rid, nm); mp_log(b);
                    } else if(ptype == 28 /*UpdatePartTemperature*/){
                        br_t tr = { f + 14, payloadBits, 0, 0 };
                        int rid = br_i32(&tr);
                        int pid = br_i32(&tr);
                        float temp = br_f32(&tr);
                        mp_rrocket_t* trp = rr_find(rid);
                        if(trp){
                            for(int i=0;i<trp->nparts;i++) if(trp->parts[i].id == pid){ trp->parts[i].temp = temp; break; }
                        }
                        snprintf(b,sizeof(b),"mp: PART TEMP gid=%d part=%d temp=%.1f\n", rid, pid, temp); mp_log(b);
                    } else if(ptype == 10 /*DestroyRocket*/){
                        if(plen >= 14){
                            int gid = 0;
                            memcpy(&gid, f + 22, 4);
                            mp_rrocket_t* rr = rr_slot(gid);
                            if(rr){
                                rr->valid = 0;
                                rr->has_structure = 0;
                                rr->dirty = 1;
                            }
                            { char db[128]; snprintf(db,sizeof(db),"mp: REMOTE DESTROY gid=%d -> remove ghost\n", gid); mp_log(db); }
                        }
                    } else if(ptype == 13 /*DestroyPart*/){
                        mp_destroy_part_t event;
                        if(mp_parse_destroy_part(f + 13, payloadBits, &event)){
                            if(mp_rr_remove_part(event.rocket_id, event.part_id))
                                snprintf(b,sizeof(b),"mp: DESTROY PART applied gid=%d part=%d reason=%u explosion=%d\n", event.rocket_id, event.part_id, event.reason, event.create_explosion);
                            else
                                snprintf(b,sizeof(b),"mp: DESTROY PART missing gid=%d part=%d\n", event.rocket_id, event.part_id);
                        } else snprintf(b,sizeof(b),"mp: DESTROY PART malformed bits=%d\n", payloadBits);
                        mp_log(b);
                    } else if(ptype == 5 /*UpdatePlayerAuthority*/){
                        /* v72: 鏈嶅姟绔憡璇夋垜浠?杩欎簺鐏褰掍綘鎺у埗"銆備笉鍚垜浠殑 global_id = 涓婅蹇呰涓㈠純銆?*/
                        const unsigned char* q = f + 14;
                        int cnt = (plen >= 5) ? (int)r_u32(q) : 0; q += 4;
                        int ids[8]; int ni = 0;
                        while(ni < cnt && ni < 8 && q + 4 <= f + plen){
                            ids[ni++] = (int)r_u32(q); q += 4;
                        }
                        int mine = 0;
                        for(int i = 0; i < ni; i++) if(ids[i] == g_global_id) mine = 1;
                        snprintf(b,sizeof(b),"mp: AUTHORITY count=%d ids=[%s%d,%d,%d] mine=%d global=%d\n",
                                 cnt, cnt > 0 ? "" : "", cnt > 0 ? ids[0] : 0, cnt > 1 ? ids[1] : 0, cnt > 2 ? ids[2] : 0,
                                 mine, g_global_id);
                        mp_log(b);
                    } else {
                        snprintf(b,sizeof(b),"mp: packet type=%d len=%d\n", ptype, plen); mp_log(b);
                    }
                } else if(kind == 4 /*Ping 鈥斺€?v174: 蹇呴』鍘熸牱鍥?Pong锛屽惁鍒欐湇鍔＄ 30s 鍒ゆ*/){
                    unsigned char pf[600];
                    int pn2 = build_frame_bits(pf, sizeof(pf), 5, (int)r_u32(f+5), f+13, payloadBits);
                    if(pn2 > 0) send_msg(pf, pn2);
                    { static int pings = 0; if(pings < 3){ pings++; snprintf(b,sizeof(b),"mp: ping -> pong seq=%u bits=%d n=%d\n", r_u32(f+5), payloadBits, pn2); mp_log(b);} }
                } else if(kind == 5 /*Pong*/){
                    snprintf(b,sizeof(b),"mp: pong n=%d\n", plen); mp_log(b);
                }
            }
        }
        if(!got_ack && now - last_log > 3000){
            last_log = now;
            mp_log("mp: waiting ack...\n");
        }
        /* v174: 8 绉掓敹涓嶅埌浠讳綍鍖?-> 閫€鍑哄惊鐜浣?g_mp_started锛岀帺瀹跺啀鐐逛竴娆″嵆鍙噸杩烇紙鍘熸潵寰幆姘镐笉閫€鍑猴紝琚涪鍚庢案涔呭崱姝伙級 */
        unsigned long long rx_age = (now > g_last_rx_ms) ? (now - g_last_rx_ms) : 0;   /* now 鏄湰杞紑濮嬫椂鍒伙紝鍙兘灏忎簬鍒氭洿鏂扮殑 rx 鏃跺埢锛屽繀椤婚槻鏃犵鍙峰洖缁?*/
        if(got_ack && g_last_rx_ms && rx_age > 8000){
            snprintf(b,sizeof(b),"mp: rx timeout %llums -> allow reconnect\n", rx_age);
            mp_log(b);
            break;
        }
        if(got_ack && g_have_pose && mp_structure_ready() && !g_create_sent){
            if(mp_send_create_rocket() > 0){
                g_create_sent = 1;
                mp_log("mp: CreateRocket sent\n");
            }
        } else if(got_ack && g_have_pose && g_create_sent && g_global_id >= 0){
            static unsigned long long last_primary = 0;
            if(now - last_primary >= 50){ last_primary = now; mp_send_primary(); }
        }
        /* 鏂扮伀绠洖鎵у凡鍒帮紙g_global_id 鏈夋晥锛夊悗鍐嶉攢姣佹棫 gid锛孭C 鍏堢湅鍒版柊鐏鍐嶇湅鍒版棫鐏娑堝け锛屼笉浼氬嚭鐜扮┖妗ｃ€?*/
        if(got_ack && g_create_sent && g_global_id >= 0){
            int old_gid = __sync_lock_test_and_set(&g_destroy_old_gid, -1);
            if(old_gid >= 0){
                int dr = mp_send_destroy_rocket_now(old_gid);
                char db[128]; snprintf(db,sizeof(db),"mp: DestroyRocket old gid=%d ret=%d\n", old_gid, dr); mp_log(db);
            }
        }
        /* v72: 鎷垮埌 global_id 鍚庣珛鍒诲０鏄庢帶鍒舵潈锛屽苟姣忕閲嶇敵涓€娆★紙鏈嶅姟绔鐩稿悓鐩爣鍙洖鎵с€佷笉鍒锋柊锛?*/
        if(got_ack && g_global_id >= 0){
            static unsigned long long last_ctl = 0;
            static int ctl_logs = 0;
            if(last_ctl == 0 || now - last_ctl >= 1000){
                last_ctl = now;
                int cr = mp_send_control(g_global_id);
                if(ctl_logs < 3 || (ctl_logs % 30) == 0){
                    char cb2[110]; snprintf(cb2,sizeof(cb2),"mp: control claim rocket=%d ret=%d (#%d)\n", g_global_id, cr, ctl_logs);
                    mp_log(cb2);
                }
                ctl_logs++;
            }
        }
        if(got_ack) mp_drain_destroy_rockets();
        if(got_ack && g_global_id >= 0) mp_drain_destroy_parts();
        /* v186锛氱潯鍒般€屼笅涓€娆?KCP update銆嶄负姝紙1鈥?0ms锛夛紝姣斿浐瀹?10ms 鍞ら啋鏇寸渷鐢典篃鏇村噯鏃?*/
        { IUINT32 n2 = (IUINT32)mp_now_ms();
          int wait = (int)(g_kcp_next - n2);
          if (wait < 1) wait = 1;
          if (wait > 10) wait = 10;
          usleep((useconds_t)wait * 1000u); }
    }
    mp_log("mp: thread exit\n");
    g_mp_started = 0;   /* v164: 鍏佽鍐嶆杩炴帴 */
    return NULL;
}

static pthread_t g_mp_thread;
static int g_mp_started = 0;

void mp_net_connect(const char* hostport){
    if(g_mp_started) return;
    /* v187: 娌℃湁榛樿鏈嶅姟鍣ㄥ湴鍧€ 鈥斺€?鐜╁娌″～灏变粈涔堥兘涓嶅仛锛岀敱涓婂眰鎻愮ず鍘诲～ */
    if(!hostport || !hostport[0]){
        if(!g_host[0]){ mp_log("mp: no endpoint configured -> skip connect\n"); return; }
    }
    g_mp_started = 1;
    if(hostport && hostport[0]){
        snprintf(g_host, sizeof(g_host), "%s", hostport);
        /* v162: IPv6 鈥斺€?鏀寔 [2001:db8::1]:9806銆?001:db8::1锛堥粯璁ょ鍙ｏ級銆乭ost:port銆佺函 IPv4 */
        if(g_host[0] == '['){
            char* rb = strchr(g_host, ']');
            if(rb){
                *rb = 0;
                memmove(g_host, g_host+1, strlen(g_host));
                if(rb[1] == ':') g_port = atoi(rb+2);
            }
        } else {
            int colons = 0; const char* q = g_host;
            while(*q){ if(*q == ':') colons++; q++; }
            if(colons == 1){ char* c = strchr(g_host, ':'); if(c){ *c = 0; g_port = atoi(c+1); } }
        }
    }
    if(pthread_create(&g_mp_thread, NULL, mp_thread, NULL) == 0) pthread_detach(g_mp_thread);
}

