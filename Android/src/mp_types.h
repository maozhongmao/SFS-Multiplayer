/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MP_TYPES_H
#define MP_TYPES_H

/* v74: 远端火箭的完整结构（手机端渲染用）。
 * 由 mp_net.c 从 CreateRocket(type=9) 解出，v64.c 消费后构造 RocketSave 调 LoadRocket。 */

#define MP_RR_MAX        8      /* 同时缓存的远端火箭数 */
#define MP_RR_PARTS      512    /* 单枚火箭最多零件数 */
#define MP_RR_JOINTS     1024
#define MP_RR_STAGES     32
#define MP_RR_STAGE_IDS  128
#define MP_RR_SNAPS      24     /* v78: 每枚远端火箭保留的位姿快照数（20Hz -> 1.2s 历史） */

typedef struct { unsigned long long t; double x, y, vx, vy; float rot, av; } mp_snap_t;

typedef struct {
    int   id;                   /* 服务端/对端给的 part id（joints/stages 用它引用零件） */
    char  name[40];
    float x, y;                 /* position (Vector2) */
    float ox, oy, oz;           /* orientation */
    float temp;                 /* temperature */
} mp_rpart_t;

typedef struct { int a, b; } mp_rjoint_t;

typedef struct { int stage_id; int n; int ids[MP_RR_STAGE_IDS]; } mp_rstage_t;

typedef struct {
    int gid;
    int valid;          /* 有位姿 */
    int has_structure;  /* 有完整零件树 */
    int dirty;          /* 结构自上次被消费后更新过 */
    char name[48];
    char planet[24];
    double x, y, vx, vy;    /* planet-relative */
    float rot, av, throttle;
    int throttle_on, rcs;
    int nparts, njoints, nstages;
    /* v78: 位姿快照环形缓冲（按收到时间排序，[nsnap-1] 最新） */
    mp_snap_t snaps[MP_RR_SNAPS];
    int nsnap;
    unsigned long long last_rx;
    mp_rpart_t  parts[MP_RR_PARTS];
    mp_rjoint_t joints[MP_RR_JOINTS];
    mp_rstage_t stages[MP_RR_STAGES];
} mp_rrocket_t;

/* ---- 上行（手机 → 服务器）的火箭结构：v64.c 填，mp_net.c 编码 ----
 * 变量字典必须一起传：PC 端 PartsLoader.cs:137 用 partSave.NUMBER_VARIABLES 建零件，
 * 字典为空 = 用默认值，于是尺寸类属性永远停在 1（m00202 用户报的问题）。 */
#define MP_UP_MAX_PARTS  1024
#define MP_UP_MAX_JOINTS 4096
#define MP_UP_MAX_STAGES 64
#define MP_UP_STAGE_IDS  256
#define MP_UP_PART_NV    10
#define MP_UP_PART_TV    6
#define MP_UP_PART_XV    3

typedef struct { char name[28]; double v; } mp_nvar_t;
typedef struct { char name[28]; int v; }    mp_tvar_t;
typedef struct { char name[28]; char val[56]; } mp_xvar_t;

typedef struct {
    char  name[48];
    float x, y, ox, oy, oz, temp;
    int   nnv, ntv, nxv;
    mp_nvar_t nv[MP_UP_PART_NV];
    mp_tvar_t tv[MP_UP_PART_TV];
    mp_xvar_t xv[MP_UP_PART_XV];
} mp_up_part_t;

typedef struct { int a, b; } mp_up_joint_t;
typedef struct { int stage_id; int n; int ids[MP_UP_STAGE_IDS]; } mp_up_stage_t;

/* mp_net.c 实现，v64.c 调用 */
int          mp_rr_count(void);
mp_rrocket_t* mp_rr_at(int i);
void         mp_rr_clear_dirty(int i);
/* v78: 取渲染时刻（现在 - 120ms）的插值位姿；返回 0 表示没有可用快照 */
int          mp_rr_sample(int i, double* x, double* y, float* rot);

#endif
