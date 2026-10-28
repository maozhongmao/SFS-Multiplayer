.class public final Lcom/sfsmp/patch/Registry;
.super Ljava/lang/Object;
.source "Registry.java"

# ===================================================================
# 补丁点清单 —— 这个文件是「要打什么补丁」的唯一真相源
#
# 加一个补丁 = 这里加一个编号常量 + install 里一行调用 + docs/patcher.md 表格加一行。
# 换 SFS 版本、换注入点时只改这里，不动 Installer。
#
# 约定：
#   - 每个 install 自己 catch，绝不把异常抛出去弄崩游戏
#   - 返回 1 = 装上，0 = 跳过（环境不满足 / 还是占位）
#   - 结果一律走 Installer.write()，不要直接 Log
# ===================================================================

# 补丁点编号（与 docs/patcher.md 一模一样的表）
.field public static final P0_HOOK:I = 0x0
.field public static final P1_LOAD_SO:I = 0x1
.field public static final P2_BRIDGE_INIT:I = 0x2
.field public static final P3_TICKER:I = 0x3

.field private static LOADED:Z


.method private constructor <init>()V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# ------------------------------------------------------------------
# installAll() —— 按编号顺序跑一遍，返回成功装上的个数
# ------------------------------------------------------------------
.method public static installAll()I
    .locals 2

    const/4 v0, 0x0

    invoke-static {}, Lcom/sfsmp/patch/Registry;->installP0()I

    move-result v1

    add-int/2addr v0, v1

    invoke-static {}, Lcom/sfsmp/patch/Registry;->installP1()I

    move-result v1

    add-int/2addr v0, v1

    invoke-static {}, Lcom/sfsmp/patch/Registry;->installP2()I

    move-result v1

    add-int/2addr v0, v1

    invoke-static {}, Lcom/sfsmp/patch/Registry;->installP3()I

    move-result v1

    add-int/2addr v0, v1

    return v0
.end method


# ------------------------------------------------------------------
# P0：证明钩子本身活着。不依赖游戏内部结构、不依赖 so。
# ------------------------------------------------------------------
.method public static installP0()I
    .locals 1

    const-string v0, "P0 hook alive"

    invoke-static {v0}, Lcom/sfsmp/patch/Installer;->write(Ljava/lang/String;)V

    const/4 v0, 0x1

    return v0
.end method


# ------------------------------------------------------------------
# P1：加载 libsfsmp.so。
# so 里 JNI_OnLoad 会自己 FindClass 拿 Bridge.INSTANCE，所以这里只负责 load。
# 失败要分得清：UnsatisfiedLinkError（找不到库）vs 其它（库在里面炸了）。
# ------------------------------------------------------------------
.method public static installP1()I
    .locals 2

    sget-boolean v0, Lcom/sfsmp/patch/Registry;->LOADED:Z

    if-eqz v0, :do_load

    const-string v0, "P1 already loaded"

    invoke-static {v0}, Lcom/sfsmp/patch/Installer;->write(Ljava/lang/String;)V

    const/4 v0, 0x1

    return v0

    :do_load
    :try_start_l
    const-string v0, "sfsmp"

    invoke-static {v0}, Ljava/lang/System;->loadLibrary(Ljava/lang/String;)V

    const/4 v0, 0x1

    sput-boolean v0, Lcom/sfsmp/patch/Registry;->LOADED:Z

    const-string v0, "P1 loadLibrary(sfsmp) ok"

    invoke-static {v0}, Lcom/sfsmp/patch/Installer;->write(Ljava/lang/String;)V

    const/4 v0, 0x1

    return v0
    :try_end_l
    .catch Ljava/lang/UnsatisfiedLinkError; {:try_start_l .. :try_end_l} :catch_unsat
    .catch Ljava/lang/Throwable; {:try_start_l .. :try_end_l} :catch_l

    :catch_unsat
    move-exception v0

    const-string v1, "P1 no lib (UnsatisfiedLinkError)"

    invoke-static {v1, v0}, Lcom/sfsmp/patch/Installer;->report(Ljava/lang/String;Ljava/lang/Throwable;)V

    const/4 v0, 0x0

    return v0

    :catch_l
    move-exception v0

    const-string v1, "P1 load threw"

    invoke-static {v1, v0}, Lcom/sfsmp/patch/Installer;->report(Ljava/lang/String;Ljava/lang/Throwable;)V

    const/4 v0, 0x0

    return v0
.end method


# ------------------------------------------------------------------
# P2：调 Bridge.bridgeInit(activity) 完成 native 侧初始化。
# 现在拿不到 Activity 引用（Installer 是纯静态），先只确认类在不在。
# 后续接 Activity 时把 ctx 传进 apply(Context) 即可，不改这里的分支结构。
# ------------------------------------------------------------------
.method public static installP2()I
    .locals 4

    :try_start_b
    const-class v0, Lcom/sfsmp/patch/Registry;

    invoke-virtual {v0}, Ljava/lang/Class;->getClassLoader()Ljava/lang/ClassLoader;

    move-result-object v0

    const-string v1, "com.sfsmp.bridge.Bridge"

    invoke-virtual {v0, v1}, Ljava/lang/ClassLoader;->loadClass(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v0

    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1}, Ljava/lang/StringBuilder;-><init>()V

    const-string v2, "P2 bridge class present: "

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    invoke-virtual {v0}, Ljava/lang/Class;->getName()Ljava/lang/String;

    move-result-object v2

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v1

    invoke-static {v1}, Lcom/sfsmp/patch/Installer;->write(Ljava/lang/String;)V

    const/4 v1, 0x1

    return v1
    :try_end_b
    .catch Ljava/lang/Throwable; {:try_start_b .. :try_end_b} :catch_b

    :catch_b
    move-exception v0

    const-string v1, "P2 no Bridge class"

    invoke-static {v1, v0}, Lcom/sfsmp/patch/Installer;->report(Ljava/lang/String;Ljava/lang/Throwable;)V

    const/4 v0, 0x0

    return v0
.end method


# ------------------------------------------------------------------
# P3：Ticker —— UI 线程心跳，native 侧 poll_click 靠它驱动。
# 需要 Activity 才能 runOnUiThread，先只认类，接上 Activity 后启用。
# ------------------------------------------------------------------
.method public static installP3()I
    .locals 4

    :try_start_t
    const-class v0, Lcom/sfsmp/patch/Registry;

    invoke-virtual {v0}, Ljava/lang/Class;->getClassLoader()Ljava/lang/ClassLoader;

    move-result-object v0

    const-string v1, "com.sfsmp.bridge.Ticker"

    invoke-virtual {v0, v1}, Ljava/lang/ClassLoader;->loadClass(Ljava/lang/String;)Ljava/lang/Class;

    move-result-object v0

    const-string v1, "P3 ticker class present (not started yet)"

    invoke-static {v1}, Lcom/sfsmp/patch/Installer;->write(Ljava/lang/String;)V

    const/4 v1, 0x0

    return v1
    :try_end_t
    .catch Ljava/lang/Throwable; {:try_start_t .. :try_end_t} :catch_t

    :catch_t
    move-exception v0

    const-string v1, "P3 no Ticker class"

    invoke-static {v1, v0}, Lcom/sfsmp/patch/Installer;->report(Ljava/lang/String;Ljava/lang/Throwable;)V

    const/4 v0, 0x0

    return v0
.end method
