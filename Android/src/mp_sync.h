/* SPDX-License-Identifier: GPL-3.0-or-later */
/* mp_sync.h —— 2.2.0.0 同步内核（对标 PC 端 Interpolator + ClientManager 的整块移植）
 * 设计：
 *   - 一张"远端火箭状态表"（gid -> 权威/位姿/分级/零件销毁/对接状态）
 *   - 所有 33 种包的入口统一走 mpx_dispatch()，未应用的包也会被记账（便于对齐 PC 行为）
 *   - 位姿采样：首包硬对齐 + 自适应延迟插值 + 断流 free-simulation
 */
#ifndef MP_SYNC_H
#define MP_SYNC_H
#include <stdint.h>

#define MPX_MAX        32   /* 同时跟踪的远端火箭上限（PC 端本地表也不大） */
#define MPX_SNAPS      24
#define MPX_DELAY_MIN  100
#define MPX_DELAY_MAX  220

typedef struct {
    double t;          /* 本机单调时钟 ms */
    double x, y, vx, vy;
    float rot, av;
} mpx_snap_t;

typedef struct {
    int   gid;
    int   owner_player;      /* 服务端说的归属玩家（-1 未知） */
    int   authoritative;     /* 这枚归我控制 */
    int   seen;
    unsigned last_rx;        /* mp_now_ms 对时 */
    double x, y, vx, vy;
    float rot, av;
    char  planet[32];
    int   has_primary;
    int   applied_once;      /* 首包硬对齐用 */
    int   stage;             /* 最近一次分级 stage_id */
    int   destroyed_parts;   /* 收到多少次 DestroyPart */
    mpx_snap_t snaps[MPX_SNAPS];
    int   nsnap;
} mpx_rocket_t;

/* 运维接口（mp_net 调用） */
void mpx_reset(void);
void mpx_tick(unsigned now_ms);
void mpx_dispatch(int ptype, const unsigned char* payload, int nbytes, int my_global_id);
void mpx_authority_packet(int count, const int* gids, int my_global_id);
int  mpx_count(void);
mpx_rocket_t* mpx_at(int i);
mpx_rocket_t* mpx_find(int gid);
int  mpx_pick_pose(int gid, double now_ms, double* x, double* y, double* vx, double* vy, float* rot, float* av, int* first);
int  mpx_is_stale(int gid, unsigned now_ms);
int  mpx_authority_of(int gid);
int  mpx_last_stage(int gid);
const char* mpx_planet(int gid);

/* 权威集合应用（Interpolator 的 authority 状态机入口） */
void mpx_authority_apply(const int* gids, int n, int my_global_id);
int  mpx_rocket_is_authoritative(int gid);
/* 权威接管时取最后已知位姿做一次 prime */
int  mpx_last_pose(int gid, double* x, double* y, double* vx, double* vy, float* rot, float* av);

/* 统计（对齐 PC 调试面板） */
void mpx_stats(int* rockets, int* auth, int* primary_rx, int* delay_ms, int* jitter_ms);
#endif
