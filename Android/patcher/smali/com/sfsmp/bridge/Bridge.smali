.class public final Lcom/sfsmp/bridge/Bridge;
.super Ljava/lang/Object;
.source "Bridge.java"

# ===================================================================
# JNI 妗ユ帴绫?鈥斺€?蹇呴』涓?libsfsmp.so 浜岃繘鍒跺吋瀹癸紙瀛楁鍚?绛惧悕涓€涓兘涓嶈兘鏀癸級
#
# native 渚э紙v64.c:1573 / v64.c:3488锛夎繖鏍峰彇锛?#   GetStaticFieldID(env, g_bridge, "INSTANCE", "Lcom/sfsmp/bridge/Bridge;")
#   GetStaticMethodID(env, ..., "nativeInit", "(Landroid/app/Activity;)V")
#   GetStaticMethodID(env, ..., "nativeOnClick", "(Landroid/view/View;)V")
#   GetStaticMethodID(env, ..., "nativeTick", "()V")
# v64.c:3539 杩樹細 FindClass("com/sfsmp/bridge/Ticker")
# so 閲岀殑 JNI_OnLoad 鍦?loadLibrary 閭ｄ竴鍒诲氨浼氭壘杩欎簺锛屾壘涓嶅埌鐩存帴鎶?鈫?娓告垙宕┿€?# 鎵€浠?Bridge 鍜?Ticker 蹇呴』涓€璧疯繘鍖咃紝杩欐槸纭粦瀹氥€?# ===================================================================

# 鍗曚緥锛宯ative 闈犲畠鎷垮疄渚?.field public static final INSTANCE:Lcom/sfsmp/bridge/Bridge;


# direct methods
.method static constructor <clinit>()V
    .locals 1

    new-instance v0, Lcom/sfsmp/bridge/Bridge;

    invoke-direct {v0}, Lcom/sfsmp/bridge/Bridge;-><init>()V

    sput-object v0, Lcom/sfsmp/bridge/Bridge;->INSTANCE:Lcom/sfsmp/bridge/Bridge;

    return-void
.end method


.method private constructor <init>()V
    .locals 0

    invoke-direct {p0}, Ljava/lang/Object;-><init>()V

    return-void
.end method


# ------------------------------------------------------------------
# 涓変釜 native 鏂规硶锛氬０鏄庡嵆鍙紝瀹炵幇鍦?libsfsmp.so
# ------------------------------------------------------------------
.method public static native nativeInit(Landroid/app/Activity;)V
.end method


.method public static native nativeOnClick(Landroid/view/View;)V
.end method


.method public static native nativeTick()V
.end method


# ------------------------------------------------------------------
# bridgeInit 鈥斺€?Java 渚у叆鍙ｏ紝nativeInit 涔嬪悗鍐嶈捣 UI 蹇冭烦
# ------------------------------------------------------------------
.method public static bridgeInit(Landroid/app/Activity;)V
    .locals 2

    :try_start_0
    invoke-static {p0}, Lcom/sfsmp/bridge/Bridge;->nativeInit(Landroid/app/Activity;)V
    :try_end_0
    .catch Ljava/lang/Throwable; {:try_start_0 .. :try_end_0} :catch_0

    :try_start_1
    new-instance v0, Lcom/sfsmp/bridge/Ticker;

    invoke-direct {v0}, Lcom/sfsmp/bridge/Ticker;-><init>()V

    invoke-virtual {p0, v0}, Landroid/app/Activity;->runOnUiThread(Ljava/lang/Runnable;)V
    :try_end_1
    .catch Ljava/lang/Throwable; {:try_start_1 .. :try_end_1} :catch_0

    return-void

    :catch_0
    return-void
.end method


# ------------------------------------------------------------------
# onClick 鈥斺€?缁?native 寰€ View 涓婃寕 listener 鐢?# ------------------------------------------------------------------
.method public onClick(Landroid/view/View;)V
    .locals 1

    :try_start_2
    invoke-static {p1}, Lcom/sfsmp/bridge/Bridge;->nativeOnClick(Landroid/view/View;)V
    :try_end_2
    .catch Ljava/lang/Throwable; {:try_start_2 .. :try_end_2} :catch_2

    :catch_2
    return-void
.end method
