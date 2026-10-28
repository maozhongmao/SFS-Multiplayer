.class public final Lcom/sfsmp/patch/Probe;
.super Ljava/lang/Object;
.source "Probe.java"

# ===================================================================
# 独立于 Installer 的存活探针。
# 用途：把 Installer 从启动路径里摘掉后，还能证明这个 dex 打得进去、跑得起来。
# 将来做故障隔离时，入口只调 Probe，Probe 再决定要不要调 Installer。
# ===================================================================

.field public static final VERSION:Ljava/lang/String; = "patcher-1"


.method private constructor <init>()V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


.method public static alive()V
    .locals 1

    const-string v0, "probe: alive patcher-1"

    invoke-static {v0}, Lcom/sfsmp/patch/Installer;->write(Ljava/lang/String;)V

    return-void
.end method
