/* SPDX-License-Identifier: GPL-3.0-or-later */
/* mp_sync.c —— 同步内核：整块移植 PC 端 Interpolator + ClientManager 的核心语义。 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "mp_sync.h"

typedef struct { const unsigned char* p; int bits; int pos; } mpx_br_t;
static unsigned mpx_bits(mpx_br_t* r, int n){
    unsigned v = 0; int i;
    for (i = 0; i < n; i++){
        int byte = r->pos >> 3, bit = r->pos & 7;
        if (byte >= r->bits / 8) { r->pos += (n - i); return v; }
        v |= (unsigned)((r->p[byte] >> bit) & 1u) << i;
        r->pos++;
    }
    return v;
}
static double mpx_f64(mpx_br_t* r){ unsigned long long b = mpx_bits(r,64); double d; memcpy(&d,&b,8); return d; }
static float  mpx_f32(mpx_br_t* r){ unsigned b = mpx_bits(r,32); float f; memcpy(&f,&b,4); return f; }
static int    mpx_i32(mpx_br_t* r){ return (int)(unsigned)mpx_bits(r,32); }
static unsigned mpx_u8(mpx_br_t* r){ return mpx_bits(r,8); }
static int    mpx_bool(mpx_br_t* r){ return (int)mpx_bits(r,1); }
static void   mpx_str(mpx_br_t* r, char* out, int cap){
    unsigned len = mpx_bits(r,8); int i;
    if (cap < 1) return;
    if (len > (unsigned)(cap - 1)) len = (unsigned)(cap - 1);
    for (i = 0; i < (int)len; i++) out[i] = (char)mpx_bits(r,8);
    out[len] = 0;
}

static double mpx_now(void){
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return (double)ts.tv_sec*1000.0 + (double)ts.tv_nsec/1.0e6;
}
static unsigned g_mpx_jit = 0;
static unsigned g_mpx_delay = MPX_DELAY_MIN;
static double   g_mpx_last_primary = 0;
static unsigned g_mpx_primary_rx = 0;

static mpx_rocket_t g_mpx[MPX_MAX];
static int g_mpx_n = 0;
static int g_mpx_unknown_ptype[64];

extern void mp_log(const char* m);

void mpx_reset(void){
    memset(g_mpx, 0, sizeof(g_mpx));
    g_mpx_n = 0;
    memset(g_mpx_unknown_ptype, 0, sizeof(g_mpx_unknown_ptype));
    g_mpx_jit = 0; g_mpx_delay = MPX_DELAY_MIN; g_mpx_primary_rx = 0; g_mpx_last_primary = 0;
}

int mpx_count(void){ return g_mpx_n; }
mpx_rocket_t* mpx_at(int i){ return (i>=0 && i<g_mpx_n) ? &g_mpx[i] : NULL; }

mpx_rocket_t* mpx_find(int gid){
    int i;
    for (i = 0; i < g_mpx_n; i++) if (g_mpx[i].gid == gid) return &g_mpx[i];
    return NULL;
}
static mpx_rocket_t* mpx_get(int gid){
    mpx_rocket_t* r = mpx_find(gid);
    int i;
    if (r) return r;
    if (g_mpx_n < MPX_MAX) { r = &g_mpx[g_mpx_n++]; }
    else {
        int victim = 0;
        for (i = 1; i < g_mpx_n; i++) if (g_mpx[i].last_rx < g_mpx[victim].last_rx) victim = i;
        r = &g_mpx[victim];
    }
    memset(r, 0, sizeof(*r));
    r->gid = gid; r->owner_player = -1; r->stage = -1;
    return r;
}

static void mpx_push_snap(mpx_rocket_t* r){
    mpx_snap_t* s;
    if (!r) return;
    if (r->nsnap >= MPX_SNAPS) { memmove(&r->snaps[0], &r->snaps[1], sizeof(mpx_snap_t)*(MPX_SNAPS-1)); r->nsnap = MPX_SNAPS-1; }
    s = &r->snaps[r->nsnap++];
    s->t = mpx_now();
    s->x = r->x; s->y = r->y; s->vx = r->vx; s->vy = r->vy; s->rot = r->rot; s->av = r->av;
}

void mpx_tick(unsigned now_ms){
    (void)now_ms;
    int d = (int)MPX_DELAY_MIN + 2*(int)g_mpx_jit;
    if (d < MPX_DELAY_MIN) d = MPX_DELAY_MIN;
    if (d > MPX_DELAY_MAX) d = MPX_DELAY_MAX;
    g_mpx_delay = (unsigned)d;
}

int mpx_pick_pose(int gid, double now_ms, double* x, double* y, double* vx, double* vy, float* rot, float* av, int* first){
    mpx_rocket_t* r = mpx_find(gid);
    double rt;
    mpx_snap_t* s; int n;
    if (!r || !r->has_primary || r->nsnap <= 0) return 0;
    if (first) *first = !r->applied_once;
    r->applied_once = 1;
    rt = now_ms - (double)g_mpx_delay;
    s = r->snaps; n = r->nsnap;
    if (n == 1 || rt <= s[0].t) { *x=s[0].x; *y=s[0].y; *vx=s[0].vx; *vy=s[0].vy; *rot=s[0].rot; *av=s[0].av; return 1; }
    if (rt >= s[n-1].t) {
        double dt = (rt - s[n-1].t)/1000.0; if (dt > 0.25) dt = 0.25;
        *x = s[n-1].x + s[n-1].vx*dt; *y = s[n-1].y + s[n-1].vy*dt;
        *vx = s[n-1].vx; *vy = s[n-1].vy;
        *rot = (float)(s[n-1].rot + s[n-1].av*dt); *av = s[n-1].av;
        return 1;
    }
    { int k;
      for (k = 1; k < n; k++) if (rt <= s[k].t) break;
      if (k >= n) k = n-1;
      { double t0=s[k-1].t, t1=s[k].t; double a = (t1>t0)?((rt-t0)/(t1-t0)):0.0; if(a<0)a=0; if(a>1)a=1;
        *x = s[k-1].x + (s[k].x-s[k-1].x)*a;
        *y = s[k-1].y + (s[k].y-s[k-1].y)*a;
        *vx = s[k-1].vx + (s[k].vx-s[k-1].vx)*a;
        *vy = s[k-1].vy + (s[k].vy-s[k-1].vy)*a;
        *rot = (float)(s[k-1].rot + (s[k].rot-s[k-1].rot)*a);
        *av = s[k-1].av + (s[k].av-s[k-1].av)*a; } }
    return 1;
}

int mpx_is_stale(int gid, unsigned now_ms){
    mpx_rocket_t* r = mpx_find(gid);
    if (!r || !r->seen) return 1;
    return (now_ms > r->last_rx + 1500u);
}
int mpx_authority_of(int gid){ mpx_rocket_t* r = mpx_find(gid); return r ? r->authoritative : 0; }

void mpx_authority_packet(int count, const int* gids, int my_global_id){
    if(count < 0 || count > 256 || (count > 0 && !gids)) return;
    mpx_authority_apply(gids, count, my_global_id);
}

int mpx_last_stage(int gid){ mpx_rocket_t* r = mpx_find(gid); return r ? r->stage : -1; }
const char* mpx_planet(int gid){ mpx_rocket_t* r = mpx_find(gid); return r ? r->planet : ""; }

/* Interpolator：服务端下发"这些火箭归你控制" —— 记录归属，驱动 Dynamic/Kinematic 切换 */
/* Interpolator: 权威变更时先 prime 一次状态（把最后已知位姿灌回去），再交还物理引擎，
 * 否则刚接过控制权的那一瞬间会用旧物理状态闪一下。 */
void mpx_authority_apply(const int* gids, int n, int my_global_id){
    int i;
    char b[160];
    for (i = 0; i < g_mpx_n; i++) g_mpx[i].authoritative = 0;
    for (i = 0; i < n; i++) {
        mpx_rocket_t* q = mpx_get(gids[i]);
        if (q) q->authoritative = 1;
    }
    snprintf(b,sizeof(b),"mpx: authority applied n=%d (mine gid=%d)\n", n, my_global_id);
    mp_log(b);
}
/* 取某枚火箭"最后一次已知位姿"，用于权威接管时的 prime */
int mpx_last_pose(int gid, double* x, double* y, double* vx, double* vy, float* rot, float* av){
    mpx_rocket_t* q = mpx_find(gid);
    if (!q || !q->has_primary) return 0;
    *x=q->x; *y=q->y; *vx=q->vx; *vy=q->vy; *rot=q->rot; *av=q->av;
    return 1;
}
int mpx_rocket_is_authoritative(int gid){
    mpx_rocket_t* q = mpx_find(gid);
    return q ? q->authoritative : 0;
}

void mpx_stats(int* rockets, int* auth, int* primary_rx, int* delay_ms, int* jitter_ms){
    int i, a = 0;
    for (i = 0; i < g_mpx_n; i++) if (g_mpx[i].authoritative) a++;
    if (rockets) *rockets = g_mpx_n;
    if (auth) *auth = a;
    if (primary_rx) *primary_rx = (int)g_mpx_primary_rx;
    if (delay_ms) *delay_ms = (int)g_mpx_delay;
    if (jitter_ms) *jitter_ms = (int)g_mpx_jit;
}

void mpx_dispatch(int ptype, const unsigned char* payload, int nbytes, int my_global_id){
    mpx_br_t r = { payload, nbytes * 8, 0 };
    char b[200];
    (void)my_global_id;

    switch (ptype) {
    case 2: case 3: {
        int pid = mpx_i32(&r);
        snprintf(b,sizeof(b),"mpx: player %s id=%d\n", ptype==2?"connected":"disconnected", pid);
        mp_log(b); break; }
    case 4: {
        int pid = mpx_i32(&r); int rid = mpx_i32(&r);
        snprintf(b,sizeof(b),"mpx: player %d controls rocket %d\n", pid, rid); mp_log(b); break; }
    case 5: {
        int count = mpx_i32(&r);
        snprintf(b,sizeof(b),"mpx: authority payload observed n=%d bytes=%d\n", count, nbytes);
        mp_log(b);
        break; }
    case 6: break;
    case 7: { int pid = mpx_i32(&r); unsigned c1=mpx_u8(&r), c2=mpx_u8(&r), c3=mpx_u8(&r);
        snprintf(b,sizeof(b),"mpx: player %d color=(%u,%u,%u)\n", pid,c1,c2,c3); mp_log(b); break; }
    case 8: { int pid = mpx_i32(&r); char txt[160]; mpx_str(&r, txt, sizeof(txt));
        snprintf(b,sizeof(b),"mpx: chat[%d]: %s\n", pid, txt); mp_log(b); break; }
    case 9: break;
    case 10: { int gid = mpx_i32(&r); mpx_rocket_t* q = mpx_find(gid);
        if (q) { memset(q, 0, sizeof(*q)); q->gid = gid; q->owner_player = -1; q->stage = -1; }
        snprintf(b,sizeof(b),"mpx: destroy rocket %d\n", gid); mp_log(b); break; }
    case 11: {
        double wt = mpx_f64(&r); int gid = mpx_i32(&r);
        double x = mpx_f64(&r), y = mpx_f64(&r), vx = mpx_f64(&r), vy = mpx_f64(&r);
        char pl[40]; mpx_str(&r, pl, sizeof(pl));
        float rot = mpx_f32(&r), av = mpx_f32(&r);
        mpx_rocket_t* q = mpx_get(gid);
        (void)wt;
        if (q) {
            double now = mpx_now();
            q->x=x; q->y=y; q->vx=vx; q->vy=vy; q->rot=rot; q->av=av;
            if (pl[0]) snprintf(q->planet, sizeof(q->planet), "%s", pl);
            q->has_primary = 1; q->seen = 1; q->last_rx = (unsigned)now;
            mpx_push_snap(q);
            if (g_mpx_last_primary > 0) {
                int iv = (int)(now - g_mpx_last_primary);
                int dev = iv - 50; if (dev < 0) dev = -dev; if (dev > 200) dev = 200;
                g_mpx_jit = g_mpx_jit ? (unsigned)((g_mpx_jit*7 + (unsigned)dev)/8) : (unsigned)dev;
            }
            g_mpx_last_primary = now;
            g_mpx_primary_rx++;
        }
        break; }
    case 12: { double wt = mpx_f64(&r); int gid = mpx_i32(&r); float it = mpx_f32(&r);
        (void)wt; (void)it; { mpx_rocket_t* q = mpx_find(gid); if (q) q->last_rx = (unsigned)mpx_now(); } break; }
    case 13: { double wt = mpx_f64(&r); int gid = mpx_i32(&r); int part = mpx_i32(&r);
        int exp = mpx_bool(&r); unsigned rsn = mpx_u8(&r);
        mpx_rocket_t* q = mpx_find(gid);
        (void)wt; (void)exp; (void)rsn;
        if (q) q->destroyed_parts++;
        snprintf(b,sizeof(b),"mpx: destroy part gid=%d part=%d\n", gid, part); mp_log(b); break; }
    case 14: { double wt = mpx_f64(&r); int gid = mpx_i32(&r);
        int n = mpx_i32(&r), i;
        (void)wt;
        { mpx_rocket_t* q = mpx_find(gid); if (q) q->last_rx = (unsigned)mpx_now(); }
        for (i = 0; i < n && i < 32; i++) {
            int sid = mpx_i32(&r); int cnt = mpx_i32(&r); int j;
            for (j = 0; j < cnt && j < 256; j++) (void)mpx_i32(&r);
            if (i == 0) { mpx_rocket_t* q = mpx_find(gid); if (q) q->stage = sid; }
        }
        snprintf(b,sizeof(b),"mpx: staging gid=%d stages=%d\n", gid, n); mp_log(b); break; }
    case 15: case 16: case 17: case 18: case 19: case 20: {
        double wt = mpx_f64(&r); int gid = mpx_i32(&r); int part = mpx_i32(&r);
        (void)wt; (void)part;
        { mpx_rocket_t* q = mpx_find(gid); if (q) q->last_rx = (unsigned)mpx_now(); }
        snprintf(b,sizeof(b),"mpx: part module type=%d gid=%d part=%d\n", ptype, gid, part); mp_log(b); break; }
    case 21: { char txt[160]; mpx_str(&r, txt, sizeof(txt));
        snprintf(b,sizeof(b),"mpx: toast: %s\n", txt); mp_log(b); break; }
    case 22: { int en = mpx_bool(&r); unsigned mask = (unsigned)mpx_bits(&r, 16);
        snprintf(b,sizeof(b),"mpx: cheat enabled=%d mask=0x%04x\n", en, mask); mp_log(b); break; }
    case 23: { double wt = mpx_f64(&r); int a = mpx_i32(&r); int bb = mpx_i32(&r); unsigned op = mpx_u8(&r);
        (void)wt; snprintf(b,sizeof(b),"mpx: dock op=%u a=%d b=%d\n", op, a, bb); mp_log(b); break; }
    case 24: { double wt = mpx_f64(&r); unsigned op = mpx_u8(&r); (void)wt;
        snprintf(b,sizeof(b),"mpx: timewarp op=%u\n", op); mp_log(b); break; }
    case 25: { int en = mpx_bool(&r); snprintf(b,sizeof(b),"mpx: experimental=%d\n", en); mp_log(b); break; }
    case 26: case 32: { char s2[120]; mpx_str(&r, s2, sizeof(s2));
        snprintf(b,sizeof(b),"mpx: p2p type=%d '%s'\n", ptype, s2); mp_log(b); break; }
    case 27: { int gid = mpx_i32(&r); char nm[64]; mpx_str(&r, nm, sizeof(nm));
        snprintf(b,sizeof(b),"mpx: rename gid=%d -> %s\n", gid, nm); mp_log(b); break; }
    case 28: { double wt = mpx_f64(&r); int gid = mpx_i32(&r); int part = mpx_i32(&r); float t2 = mpx_f32(&r);
        (void)wt; (void)part; (void)t2; { mpx_rocket_t* q = mpx_find(gid); if (q) q->last_rx = (unsigned)mpx_now(); } break; }
    case 29: { int gid = mpx_i32(&r); unsigned rsn = mpx_u8(&r);
        snprintf(b,sizeof(b),"mpx: destruct reason gid=%d reason=%u\n", gid, rsn); mp_log(b); break; }
    case 30: { char s2[160]; mpx_str(&r, s2, sizeof(s2));
        snprintf(b,sizeof(b),"mpx: server cmd: %s\n", s2); mp_log(b); break; }
    case 31: break;
    default:
        if (ptype >= 0 && ptype < 64 && !g_mpx_unknown_ptype[ptype]) {
            g_mpx_unknown_ptype[ptype] = 1;
            snprintf(b,sizeof(b),"mpx: unhandled packet type=%d (logged once)\n", ptype); mp_log(b);
        }
        break;
    }
}
