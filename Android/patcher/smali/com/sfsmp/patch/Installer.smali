.class public final Lcom/sfsmp/patch/Installer;
.super Ljava/lang/Object;
.source "Installer.java"

# ===================================================================
# SFSMP 补丁安装器 —— 入口壳子
#
# 这个类只做三件事，别往里加业务：
#   1. 一个静态单例 PROBE
#   2. apply()：幂等入口，被 Activity 生命周期调用
#   3. 把结果写到一个文件（这台 vivo 的 logcat 抓不到应用日志）
#
# 具体要打什么补丁，全部放在 com.sfsmp.patch.Registry 的补丁点清单里。
# 这个文件在换版本时**不需要改**。
# ===================================================================

# static fields
.field public static final TAG:Ljava/lang/String; = "SFSMPPatch"

.field public static final MARK_OK:Ljava/lang/String; = "sfsmp-patch:"

.field public static final MARK_FAIL:Ljava/lang/String; = "sfsmp-patch-FAIL:"

.field private static final OUT_PATH:Ljava/lang/String;

.field private static VISITED:I

.field private static INSTALLED:Z


# direct methods
.method static constructor <clinit>()V
    .locals 2

    const-string v0, "/sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-patch.txt"

    sput-object v0, Lcom/sfsmp/patch/Installer;->OUT_PATH:Ljava/lang/String;

    const/4 v1, 0x0

    sput v1, Lcom/sfsmp/patch/Installer;->VISITED:I

    const/4 v1, 0x0

    sput-boolean v1, Lcom/sfsmp/patch/Installer;->INSTALLED:Z

    return-void
.end method

.method private constructor <init>()V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# ------------------------------------------------------------------
# apply() —— 唯一对外入口。Activity.onCreate / onResume 都调它。
# 幂等：装过一次就只记一次，不重复执行。
# ------------------------------------------------------------------
.method public static apply()V
    .locals 5

    sget v0, Lcom/sfsmp/patch/Installer;->VISITED:I

    add-int/lit8 v1, v0, 0x1

    sput v1, Lcom/sfsmp/patch/Installer;->VISITED:I

    if-nez v0, :already

    const-string v0, "hook-visible"

    invoke-static {v0}, Lcom/sfsmp/patch/Installer;->write(Ljava/lang/String;)V

    :already
    sget-boolean v0, Lcom/sfsmp/patch/Installer;->INSTALLED:Z

    if-eqz v0, :done

    const/4 v0, 0x1

    sput-boolean v0, Lcom/sfsmp/patch/Installer;->INSTALLED:Z

    :try_start_0
    invoke-static {}, Lcom/sfsmp/patch/Installer;->run()V
    :try_end_0
    .catch Ljava/lang/Throwable; {:try_start_0 .. :try_end_0} :catch_0

    return-void

    :catch_0
    move-exception v0

    sget-object v1, Lcom/sfsmp/patch/Installer;->MARK_FAIL:Ljava/lang/String;

    invoke-static {v1, v0}, Lcom/sfsmp/patch/Installer;->report(Ljava/lang/String;Ljava/lang/Throwable;)V

    :done
    return-void
.end method


.method private static run()V
    .locals 3

    const-string v0, "run-start"

    invoke-static {v0}, Lcom/sfsmp/patch/Installer;->write(Ljava/lang/String;)V

    :try_start_1
    invoke-static {}, Lcom/sfsmp/patch/Registry;->installAll()I
    :try_end_1
    .catch Ljava/lang/Throwable; {:try_start_1 .. :try_end_1} :catch_1

    move-result v0

    new-instance v1, Ljava/lang/StringBuilder;

    invoke-direct {v1}, Ljava/lang/StringBuilder;-><init>()V

    sget-object v2, Lcom/sfsmp/patch/Installer;->MARK_OK:Ljava/lang/String;

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/String;

    const-string v2, "patches="

    invoke-virtual {v1, v2}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/String;

    invoke-virtual {v1, v0}, Ljava/lang/StringBuilder;->append(I)Ljava/lang/StringBuilder;

    invoke-virtual {v1}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v0

    invoke-static {v0}, Lcom/sfsmp/patch/Installer;->write(Ljava/lang/String;)V

    :catch_1
    move-exception v0

    sget-object v1, Lcom/sfsmp/patch/Installer;->MARK_FAIL:Ljava/lang/String;

    invoke-static {v1, v0}, Lcom/sfsmp/patch/Installer;->report(Ljava/lang/String;Ljava/lang/Throwable;)V

    return-void
.end method


.method public static report(Ljava/lang/String;Ljava/lang/Throwable;)V
    .locals 2

    if-eqz p1, :no_throw

    new-instance v0, Ljava/lang/StringBuilder;

    invoke-direct {v0}, Ljava/lang/StringBuilder;-><init>()V

    invoke-virtual {v0, p1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    const-string v1, " "

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    if-eqz p2, :null_t

    invoke-virtual {p2}, Ljava/lang/Throwable;->toString()Ljava/lang/String;

    move-result-object v1

    invoke-virtual {v0, v1}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    :null_t
    invoke-virtual {v0}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object p1

    :no_throw
    invoke-static {p1}, Lcom/sfsmp/patch/Installer;->write(Ljava/lang/String;)V

    return-void
.end method


# ------------------------------------------------------------------
# write() —— 双通道：logcat + 文件
# vivo 上 logcat 抓不到应用日志，文件才是可靠通道。
# 单条写失败不影响其它条，全部 catch 住。
# ------------------------------------------------------------------
.method public static write(Ljava/lang/String;)V
    .locals 5

    sget-object v0, Lcom/sfsmp/patch/Installer;->TAG:Ljava/lang/String;

    :try_start_l
    invoke-static {v0, p0}, Landroid/util/Log;->i(Ljava/lang/String;Ljava/lang/String;)I
    :try_end_l
    .catch Ljava/lang/Throwable; {:try_start_l .. :try_end_l} :catch_l

    :catch_l
    :try_start_w
    new-instance v0, Ljava/io/File;

    sget-object v1, Lcom/sfsmp/patch/Installer;->OUT_PATH:Ljava/lang/String;

    invoke-direct {v0, v1}, Ljava/io/File;-><init>(Ljava/lang/String;)V

    invoke-virtual {v0}, Ljava/io/File;->getParentFile()Ljava/io/File;

    move-result-object v1

    if-eqz v1, :skip_mk

    invoke-virtual {v1}, Ljava/io/File;->exists()Z

    move-result v2

    if-nez v2, :skip_mk

    invoke-virtual {v1}, Ljava/io/File;->mkdirs()Z

    :skip_mk
    new-instance v1, Ljava/io/FileOutputStream;

    const/4 v2, 0x1

    invoke-direct {v1, v0, v2}, Ljava/io/FileOutputStream;-><init>(Ljava/io/File;Z)V

    new-instance v2, Ljava/lang/StringBuilder;

    invoke-direct {v2}, Ljava/lang/StringBuilder;-><init>()V

    invoke-virtual {v2, p0}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    const-string v3, "\n"

    invoke-virtual {v2, v3}, Ljava/lang/StringBuilder;->append(Ljava/lang/String;)Ljava/lang/StringBuilder;

    invoke-virtual {v2}, Ljava/lang/StringBuilder;->toString()Ljava/lang/String;

    move-result-object v2

    const-string v3, "UTF-8"

    invoke-virtual {v2, v3}, Ljava/lang/String;->getBytes(Ljava/lang/String;)[B

    move-result-object v2

    invoke-virtual {v1, v2}, Ljava/io/FileOutputStream;->write([B)V

    invoke-virtual {v1}, Ljava/io/FileOutputStream;->close()V
    :try_end_w
    .catch Ljava/lang/Throwable; {:try_start_w .. :try_end_w} :catch_w

    :catch_w
    return-void
.end method
