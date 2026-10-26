/*
 * mp_net.c — 手机端联机客户端（M1：KCP + 握手）
 *
 * 复用服务器协议：帧 = [int32 bodyLen][u8 kind][int32 seq][int32 payloadBits][payload]
 * 传输 = KCP over UDP，KCP 字节流上再套 4 字节小端长度前缀（与 Server/cpp 的 KcpTransport 一致）
 * 握手 = magic 0x31465353 + version 2 + strings(username,password,solarSystem) + i32 resumeId + resumeToken
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

/* v129: 路径跟随运行时包名（由 v64.c 的 sfsmp_set_package 决定）。共存版换了包名也能写进自己的目录。 */
extern const char* sfsmp_dp(const char* name);
#define MP_MARKER (sfsmp_dp("sfsmp-probe.txt"))
#define MP_SERVER (sfsmp_dp("sfsmp-server.txt"))

static void mp_log(const char* m){
    int fd = open(MP_MARKER, O_WRONLY|O_CREAT|O_APPEND, 0644);
    if(fd >= 0){ write(fd, m, strlen(m)); close(fd); }
}

/* ---------- 位写入器（Lidgren LSB-first） ---------- */
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

/* ---------- 位读取器 ---------- */
typedef struct { const unsigned char* buf; int nbits; int bit; } br_t;
static unsigned long long br_bits(br_t* r, int n){
    unsigned long long v = 0;
    for(int i=0;i<n;i++){
        int byte = r->bit >> 3, off = r->bit & 7;
        if(r->bit < r->nbits && ((r->buf[byte] >> off) & 1u)) v |= (1ull << i);
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

/* 本地位姿（主线程写、网络线程读） */
static double g_lx=0, g_ly=0, g_lvx=0, g_lvy=0;
static float  g_lrot=0;
static float  g_lav=0;   /* v107: 真实角速度，随 primary 一起发 */
static char   g_lplanet[40] = "Earth";
static volatile int g_have_pose = 0;
static volatile int g_global_id = -1;
static volatile int g_create_sent = 0;
static volatile int g_joined = 0;
/* v97: 服务器下发的世界时间（HelloAck 里带；也是 WorldSnapshot 的第一个字段） */
static volatile double g_server_world_time = 0.0;
static volatile int g_have_world_time = 0;
double mp_server_world_time(void){ return g_have_world_time ? g_server_world_time : 0.0; }
int mp_is_joined(void){ return g_joined; }

void mp_set_local_angular(float av);
void mp_set_local_pose(double x, double y, double vx, double vy, const char* planet, float rot){
    g_lx=x; g_ly=y; g_lvx=vx; g_lvy=vy; g_lrot=rot;
    if(planet && planet[0]){ snprintf(g_lplanet, sizeof(g_lplanet), "%s", planet); }
    g_have_pose = 1;
}

/* v107: 真实角速度。对端靠它做外推，网络有延迟时不会卡在旧角度。 */
void mp_set_local_angular(float av){ g_lav=av; }

/* 远端火箭状态缓存（M4 用） */
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

/* ---------- v74: 远端火箭完整结构缓存 ---------- */
static mp_rrocket_t g_rr[MP_RR_MAX];
static int g_rr_n = 0;

/* v110: 满了以后不能直接覆盖最后一格 —— 那样 v64.c 的 g_rrSpawned[]/g_rrRocket[]
 * 还指着旧对象，新结构永远不会被生成，帧循环还会往一个换了 gid 的槽上写位姿。
 * 现在：满了就淘汰"最久没收到包"的那一格，并把它整格清零（连同 nsnap 快照），
 * 让消费端通过 valid/has_structure 自然发现这是一枚新火箭。 */
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

int mp_rr_count(void){ return g_rr_n; }
mp_rrocket_t* mp_rr_at(int i){ return (i>=0 && i<g_rr_n) ? &g_rr[i] : NULL; }
void mp_rr_clear_dirty(int i){ if(i>=0 && i<g_rr_n) g_rr[i].dirty = 0; }

/* CreateRocket 的 RocketState 载荷（= 整棵零件树）。
 * 字段顺序以 Server/cpp/src/protocol/packets.cpp:273 RocketState::serialize 为准：
 *   string rocket_name; NetLocation(f64 x,y,vx,vy + string address); f32 rotation;
 *   f32 angular_velocity; bool throttle_on; f32 throttle_percent; bool rcs;
 *   count+(i32 id, PartState); count+(i32 a, i32 b); count+(i32 stage_id, count+i32 ids)
 * PartState（packets.cpp:167）：string name; f32 x,y; f32 ox,oy,oz; f32 temperature;
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
    if(lid == 1) return;   /* 这是"我们自己的火箭"，不能当远端火箭再生成一枚 */

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
          memset(pt, 0, sizeof(*pt));             /* v93: 先清零，再读 id —— 顺序写反会把刚读到的 id 清成 0 */
          pt->id = br_i32(&r);                    /* part id：joints/stages 用它引用零件，必须保留 */
          br_str(&r, pt->name, sizeof(pt->name));
          pt->x = br_f32(&r); pt->y = br_f32(&r);
          pt->ox = br_f32(&r); pt->oy = br_f32(&r); pt->oz = br_f32(&r);
          pt->temp = br_f32(&r);
          /* 变量字典：本期不落地，但必须按格式跳过，否则偏移错位 */
          int n = br_i32(&r); if(n < 0 || n > 4096) n = 0;
          for(k=0;k<n;k++){ char kb[64]; br_str(&r, kb, sizeof(kb)); (void)br_f64(&r); }
          n = br_i32(&r); if(n < 0 || n > 4096) n = 0;
          for(k=0;k<n;k++){ char kb[64]; br_str(&r, kb, sizeof(kb)); (void)br_bool(&r); }
          n = br_i32(&r); if(n < 0 || n > 4096) n = 0;
          for(k=0;k<n;k++){ char kb[64], vb[64]; br_str(&r, kb, sizeof(kb)); br_str(&r, vb, sizeof(vb)); }
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


static char  g_host[96] = "192.168.1.5";
static int   g_port = 9806;
static int   g_fd = -1;
static ikcpcb* g_kcp = NULL;

static int mp_output(const char* buf, int len, ikcpcb* kcp, void* user){
    (void)kcp; (void)user;
    if(g_fd < 0) return -1;
    return (int)send(g_fd, buf, (size_t)len, 0);
}

static unsigned long long mp_now_ms(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ull + (unsigned long long)(ts.tv_nsec / 1000000);
}

/* ================= v78: 远端位姿插值（移植 PC 端 Interpolator 思路） =================
 * 收到 primary 包时压一个快照（带本机单调时钟时间戳），渲染时取 now-120ms 处的插值，
 * 这样网络抖动不会直接反映到画面上。超出最新快照则用速度做最多 250ms 的短外推。
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

/* ---------- 小端写入 ---------- */
static void w_u32(unsigned char* p, unsigned v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static unsigned r_u32(const unsigned char* p){ return (unsigned)p[0]|((unsigned)p[1]<<8)|((unsigned)p[2]<<16)|((unsigned)p[3]<<24); }
static void w_u64(unsigned char* p, unsigned long long v){ for(int i=0;i<8;i++) p[i]=(unsigned char)(v>>(8*i)); }
static unsigned long long r_u64(const unsigned char* p){ unsigned long long v=0; for(int i=0;i<8;i++) v |= (unsigned long long)p[i] << (8*i); return v; }

/* ---------- 组帧 ---------- */
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
    int need = 8 + 4+strlen(user) + 4+strlen(pass) + 4+strlen(solar) + 4 + 4+strlen(resumeTok);
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
    return (int)(p - out);
}

/* Lidgren NetBuffer.Write(string)：7 位编码长度 + UTF8 */
static int lid_str(unsigned char* out, int cap, const char* s){
    size_t n = strlen(s);
    if(n >= 128 || (int)n + 1 > cap) return -1;
    out[0] = (unsigned char)n;
    memcpy(out+1, s, n);
    return (int)n + 1;
}

/* 业务包 = [u8 type][Lidgren 字段] */
static int build_join_request(unsigned char* out, int cap, const char* user, const char* pass, const char* solar){
    int o = 0, n;
    if(cap < 4) return -1;
    out[o++] = 0;                                  /* PacketType.JoinRequest */
    n = lid_str(out+o, cap-o, user);  if(n<0) return -1; o += n;
    n = lid_str(out+o, cap-o, pass);  if(n<0) return -1; o += n;
    n = lid_str(out+o, cap-o, solar); if(n<0) return -1; o += n;
    return o;
}

/* ---------- 发出：KCP 消息 = 4 字节长度前缀 + 帧 ---------- */
static int send_msg(const unsigned char* frame, int n){
    unsigned char buf[1500];
    if(n + 4 > (int)sizeof(buf)) return -1;
    w_u32(buf, (unsigned)n);
    memcpy(buf+4, frame, (size_t)n);
    { char hx[200]; int k=0; k+=snprintf(hx+k,sizeof(hx)-k,"mp: tx n=%d hex=", n);
      for(int i=0;i<n && i<24 && k<(int)sizeof(hx)-4;i++) k+=snprintf(hx+k,sizeof(hx)-k,"%02X", frame[i]);
      k+=snprintf(hx+k,sizeof(hx)-k,"\n"); mp_log(hx); }
    return ikcp_send(g_kcp, (const char*)buf, n + 4);
}

/* v73: 大消息（完整火箭结构可达几十上百 KB）。KCP 会自己分片，这里只补 4 字节长度前缀。 */
static int send_msg_big(const unsigned char* frame, int n){
    static unsigned char buf[(1 << 20) + 64];
    if(n + 4 > (int)sizeof(buf)) return -1;
    w_u32(buf, (unsigned)n);
    memcpy(buf+4, frame, (size_t)n);
    { char b2[96]; snprintf(b2,sizeof(b2),"mp: tx-big n=%d\n", n); mp_log(b2); }
    return ikcp_send(g_kcp, (const char*)buf, n + 4);
}


/* ================= v73: 完整火箭结构上传 =================
 * 之前 CreateRocket 的 parts/joints/stages 全是 0 个 -> 服务端登记了一枚"空气火箭",
 * PC 端 Instantiate 出来什么都没有, 于是"看不见位置"。这里把本机真实结构发上去。
 * 数据由 v64.c 从 RocketSave 里读出来(游戏自己的保存结构), 这里只负责编码。
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
    /* v111: 名字必须无条件覆盖。老代码只在 name 非空时写，导致 g_st_name 保留上一次的值
     * （第一轮若被写成 'MP-Remote'，之后每次传空名字都还是它），
     * 于是服务端把我们的火箭注册成 'MP-Remote'，PC 端就画出一个挂着这个假名字的火箭。 */
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

/* 大缓冲位写入（Lidgren 位序，LSB first） */
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
/* Lidgren Write(string): 7 位变长字节长度 + UTF8 */
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
    bw_f64(&w, 0.0);                    /* world_time 由服务端补 */
    bw_i32(&w, 1);                      /* local_id */
    bw_i32(&w, -1);                     /* global_id 交给服务端分配 */
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

/* v73: 带真实结构的 CreateRocket。消息可能上百 KB -> 自己的大缓冲 + 大帧缓冲。 */
static int mp_send_create_rocket_full(void){
    static unsigned char pay[1 << 20];
    static unsigned char frm[(1 << 20) + 128];
    bw2_t w;
    int i, j;
    memset(pay, 0, sizeof(pay));
    w.buf = pay; w.cap = (int)sizeof(pay); w.bit = 0;
    bw2_u8(&w, 9);                       /* PacketType.CreateRocket */
    bw2_f64(&w, 0.0);                    /* world_time 服务端补 */
    bw2_i32(&w, 1);                      /* local_id */
    bw2_i32(&w, -1);                     /* global_id */
    bw2_bool(&w, 0);                     /* for_launch */
    bw2_str(&w, g_st_name[0] ? g_st_name : "MP-Android");
    bw2_f64(&w, g_lx); bw2_f64(&w, g_ly); bw2_f64(&w, g_lvx); bw2_f64(&w, g_lvy);
    bw2_str(&w, g_lplanet);
    bw2_f32(&w, g_lrot); bw2_f32(&w, 0.0f);
    bw2_bool(&w, 0); bw2_f32(&w, 0.0f); bw2_bool(&w, 0);

    bw2_i32(&w, g_st_nparts);
    for(i=0;i<g_st_nparts;i++){
        const mp_part_rec* p = &g_st_parts[i];
        bw2_i32(&w, i + 1);              /* part id */
        bw2_str(&w, p->name);
        bw2_f32(&w, p->x); bw2_f32(&w, p->y);
        bw2_f32(&w, p->ox); bw2_f32(&w, p->oy); bw2_f32(&w, p->oz);
        bw2_f32(&w, p->temp);
        /* v126: 变量字典必须跟着走，否则 PC 端建出来的零件全用默认值（尺寸永远=1） */
        { int k;
          bw2_i32(&w, p->nnv);
          for(k=0;k<p->nnv;k++){ bw2_str(&w, p->nv[k].name); bw2_f64(&w, p->nv[k].v); }
          bw2_i32(&w, p->ntv);
          for(k=0;k<p->ntv;k++){ bw2_str(&w, p->tv[k].name); bw2_bool(&w, p->tv[k].v); }
          bw2_i32(&w, p->nxv);
          for(k=0;k<p->nxv;k++){ bw2_str(&w, p->xv[k].name); bw2_str(&w, p->xv[k].val); }
        }
        bw2_bool(&w, 1);                 /* burns 为空 */
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

static int mp_send_primary(void){
    bw_t w; bw_clear(&w);
    bw_u8(&w, 11);                      /* PacketType.UpdateRocketPrimary */
    bw_f64(&w, 0.0);
    bw_i32(&w, g_global_id >= 0 ? g_global_id : 0);
    bw_f64(&w, g_lx); bw_f64(&w, g_ly); bw_f64(&w, g_lvx); bw_f64(&w, g_lvy);
    bw_str(&w, g_lplanet);
    bw_f32(&w, g_lrot); bw_f32(&w, g_lav);   /* v107: angular_velocity 发真实值，不再恒 0 */
    bw_u32(&w, 0);
    unsigned char frame[1200];
    int fn = build_frame_bits(frame, sizeof(frame), 3, 3, w.buf, w.bit);
    if(fn <= 0) return -1;
    /* v72: 每隔 1 秒回显一次真正发出去的内容，用来确认位姿在变 */
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

/* v72: 声明"这枚火箭归我控制"。服务端 has_authority 只认 controlled_rocket_，
 * 不发这条 -> 权威按"最近玩家/轮询"分给别人 -> 我们的 UpdateRocketPrimary 全被丢弃。 */
static int mp_send_control(int rocket_id){
    bw_t w; bw_clear(&w);
    bw_u8(&w, 4);                       /* PacketType.UpdatePlayerControl */
    bw_i32(&w, 0);                      /* player_id：服务端一律用认证身份覆盖 */
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

static void* mp_thread(void* arg){
    (void)arg;
    char host[96]; snprintf(host, sizeof(host), "%s", g_host);
    int port = g_port;
    char b[192];
    snprintf(b,sizeof(b),"mp: connecting %s:%d ...\n", host, port); mp_log(b);

    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
    char ps[16]; snprintf(ps,sizeof(ps),"%d",port);
    if(getaddrinfo(host, ps, &hints, &res) != 0 || !res){
        snprintf(b,sizeof(b),"mp: DNS failed for %s\n", host); mp_log(b); return NULL;
    }
    g_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if(g_fd < 0){ mp_log("mp: socket failed\n"); freeaddrinfo(res); return NULL; }
    if(connect(g_fd, res->ai_addr, res->ai_addrlen) != 0){
        snprintf(b,sizeof(b),"mp: connect failed errno=%d\n", errno); mp_log(b); freeaddrinfo(res); return NULL;
    }
    struct sockaddr_in sa; memcpy(&sa, res->ai_addr, sizeof(sa)); freeaddrinfo(res);
    { char ip[32]; inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
      snprintf(b,sizeof(b),"mp: resolved %s -> %s:%d\n", host, ip, port); mp_log(b); }

    int fl = fcntl(g_fd, F_GETFL, 0); fcntl(g_fd, F_SETFL, fl | O_NONBLOCK);

    g_kcp = ikcp_create((IUINT32)(mp_now_ms() & 0x7fffffff), NULL);
    if(!g_kcp){ mp_log("mp: ikcp_create failed\n"); return NULL; }
    ikcp_setoutput(g_kcp, mp_output);
    ikcp_nodelay(g_kcp, 1, 10, 0, 1);
    ikcp_wndsize(g_kcp, 256, 256);

    unsigned char hello[512];
    int hn = build_hello(hello, sizeof(hello), "android", "", "", -1, "");
    unsigned char frame[600];
    int fn = build_frame(frame, sizeof(frame), 1 /*Hello*/, 2 /*= 握手版本号，不是序号*/, hello, hn);
    send_msg(frame, fn);
    mp_log("mp: hello sent\n");

    unsigned long long last_log = 0;
    int got_ack = 0;
    unsigned char rx[2048];
    static unsigned char kb[65536];   /* v72: 收包缓冲放大到 64KB，之前 4KB 一旦收到大包就永久堵死 */
    for(int iter = 0; ; iter++){                     /* v65: 常驻，不再 90 秒后自己退出 */
        unsigned long long now = mp_now_ms();
        ikcp_update(g_kcp, (IUINT32)now);
        ikcp_flush(g_kcp);

        int rn = (int)recv(g_fd, rx, sizeof(rx), 0);
        if(rn > 0){
            ikcp_input(g_kcp, (const char*)rx, rn);
            while(1){
                int n = ikcp_recv(g_kcp, (char*)kb, (int)sizeof(kb));
                if(n <= 0) break;
                if(n < 4) continue;
                int len = (int)r_u32(kb);
                if(len + 4 > n) continue;
                unsigned char* f = kb + 4;
                int kind = f[4];
                int payloadBits = (int)r_u32(f+9);
                int plen = payloadBits / 8;
                if(!got_ack && kind == 2 /*HelloAck*/){
                    mp_log_ack(f + 13, plen);
                    got_ack = 1;
                    /* 立刻发业务包 JoinRequest(type=0) 完成 auth */
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
                    if(ptype == 9 /*CreateRocket*/){
                        /* v74: 整棵零件树解出来，供 v64 生成远端火箭 */
                        int lid9 = -1, gid9 = -1;
                        rr_parse_create(f + 14, plen - 1, &lid9, &gid9);
                        if(lid9 == 1 && gid9 >= 0 && g_global_id < 0){
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
                        float av = 0.0f;                       /* v78: primary 末尾带角速度 */
                        if(q + 4 <= f + plen){ memcpy(&av,q,4); q+=4; }
                        mp_remote_t* rs = remote_slot(rid);
                        rs->x=px; rs->y=py; rs->vx=vx; rs->vy=vy; rs->rot=rot; rs->valid=1;
                        { mp_rrocket_t* rp = rr_find(rid);   /* v74: 结构已到时同步位置 */
                          if(rp){ rp->x=px; rp->y=py; rp->vx=vx; rp->vy=vy; rp->rot=rot; rp->av=av; rp->valid=1;
                                  if(pl[0]) snprintf(rp->planet, sizeof(rp->planet), "%s", pl);
                                  rr_push_snap(rp, px, py, vx, vy, rot, av); } }
                        snprintf(b,sizeof(b),"mp: ROCKET id=%d planet='%s' pos=(%.1f, %.1f) vel=(%.2f, %.2f) rot=%.1f\n",
                                 rid, pl, px, py, vx, vy, rot);
                        mp_log(b);
                    } else if(ptype == 5 /*UpdatePlayerAuthority*/){
                        /* v72: 服务端告诉我们"这些火箭归你控制"。不含我们的 global_id = 上行必被丢弃。 */
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
                } else if(kind == 5 /*Pong*/){
                    snprintf(b,sizeof(b),"mp: pong n=%d\n", plen); mp_log(b);
                }
            }
        }
        if(!got_ack && now - last_log > 3000){
            last_log = now;
            mp_log("mp: waiting ack...\n");
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
        /* v72: 拿到 global_id 后立刻声明控制权，并每秒重申一次（服务端对相同目标只回执、不刷新） */
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
        usleep(10000);
    }
    mp_log("mp: thread exit\n");
    return NULL;
}

static pthread_t g_mp_thread;
static int g_mp_started = 0;

void mp_net_connect(const char* hostport){
    if(g_mp_started) return;
    g_mp_started = 1;
    if(hostport && hostport[0]){
        snprintf(g_host, sizeof(g_host), "%s", hostport);
        char* c = strchr(g_host, ':');
        if(c){ *c = 0; g_port = atoi(c+1); }
    }
    if(pthread_create(&g_mp_thread, NULL, mp_thread, NULL) == 0) pthread_detach(g_mp_thread);
}
