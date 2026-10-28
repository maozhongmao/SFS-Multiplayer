.class public final Lcom/sfsmp/bridge/Ticker;
.super Ljava/lang/Object;
.source "Ticker.java"

# ===================================================================
# UI 线程心跳 —— native 侧 poll_click 靠它每帧驱动
# v64.c:3539 FindClass("com/sfsmp/bridge/Ticker") 硬依赖这个类名
#
# 节奏：一帧 16ms。这个值不是随便定的 —— 游戏 UI 点击采样按帧算，
# 慢了按钮就迟钝，快了纯烧电。
# ===================================================================

.implements Ljava/lang/Runnable;

.field private static final FRAME_MS:J = 0x10L


.method public constructor <init>()V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


.method public run()V
    .locals 2

    :loop
    :try_start_0
    invoke-static {}, Lcom/sfsmp/bridge/Bridge;->nativeTick()V
    :try_end_0
    .catch Ljava/lang/Throwable; {:try_start_0 .. :try_end_0} :catch_0

    :catch_0
    const-wide/16 v0, 0x10

    :try_start_1
    invoke-static {v0, v1}, Ljava/lang/Thread;->sleep(J)V
    :try_end_1
    .catch Ljava/lang/Throwable; {:try_start_1 .. :try_end_1} :catch_1

    :catch_1
    goto :loop
.end method
