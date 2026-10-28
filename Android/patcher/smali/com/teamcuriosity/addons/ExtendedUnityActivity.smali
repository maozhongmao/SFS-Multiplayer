.class public Lcom/teamcuriosity/addons/ExtendedUnityActivity;
.super Lcom/unity3d/player/UnityPlayerActivity;
.source "ExtendedUnityActivity.java"

# instance fields
.field private sListeners:Ljava/util/Map;


# direct methods
.method public constructor <init>()V
    .locals 1

    invoke-direct {p0}, Lcom/unity3d/player/UnityPlayerActivity;-><init>()V

    new-instance v0, Ljava/util/concurrent/ConcurrentHashMap;

    invoke-direct {v0}, Ljava/util/concurrent/ConcurrentHashMap;-><init>()V

    iput-object v0, p0, Lcom/teamcuriosity/addons/ExtendedUnityActivity;->sListeners:Ljava/util/Map;

    return-void
.end method


# virtual methods
.method protected onCreate(Landroid/os/Bundle;)V
    .locals 0

    invoke-super {p0, p1}, Lcom/unity3d/player/UnityPlayerActivity;->onCreate(Landroid/os/Bundle;)V

    invoke-static {}, Lcom/sfsmp/patch/Installer;->apply()V

    return-void
.end method


.method protected onResume()V
    .locals 0

    invoke-super {p0}, Lcom/unity3d/player/UnityPlayerActivity;->onResume()V

    invoke-static {}, Lcom/sfsmp/patch/Installer;->apply()V

    return-void
.end method


.method public registerActivityResultListener(ILcom/teamcuriosity/addons/ExtendedUnityActivity$ActivityResultListener;)V
    .locals 1

    if-nez p2, :cond_0

    iget-object p2, p0, Lcom/teamcuriosity/addons/ExtendedUnityActivity;->sListeners:Ljava/util/Map;

    invoke-static {p1}, Ljava/lang/Integer;->valueOf(I)Ljava/lang/Integer;

    move-result-object p1

    invoke-interface {p2, p1}, Ljava/util/Map;->remove(Ljava/lang/Object;)Ljava/lang/Object;

    goto :goto_0

    :cond_0
    iget-object v0, p0, Lcom/teamcuriosity/addons/ExtendedUnityActivity;->sListeners:Ljava/util/Map;

    invoke-static {p1}, Ljava/lang/Integer;->valueOf(I)Ljava/lang/Integer;

    move-result-object p1

    invoke-interface {v0, p1, p2}, Ljava/util/Map;->put(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;

    :goto_0
    return-void
.end method


.method protected onActivityResult(IILandroid/content/Intent;)V
    .locals 3

    invoke-super {p0, p1, p2, p3}, Lcom/unity3d/player/UnityPlayerActivity;->onActivityResult(IILandroid/content/Intent;)V

    :try_start_0
    iget-object v0, p0, Lcom/teamcuriosity/addons/ExtendedUnityActivity;->sListeners:Ljava/util/Map;

    if-nez v0, :try_done

    invoke-static {p1}, Ljava/lang/Integer;->valueOf(I)Ljava/lang/Integer;

    move-result-object v1

    invoke-interface {v0, v1}, Ljava/util/Map;->get(Ljava/lang/Object;)Ljava/lang/Object;

    move-result-object v0

    check-cast v0, Lcom/teamcuriosity/addons/ExtendedUnityActivity$ActivityResultListener;

    if-eqz v0, :try_done

    invoke-interface {v0, p1, p2, p3}, Lcom/teamcuriosity/addons/ExtendedUnityActivity$ActivityResultListener;->onActivityResult(IILandroid/content/Intent;)V

    :try_done
    :try_end_0
    .catch Ljava/lang/Throwable; {:try_start_0 .. :try_end_0} :catch_0

    return-void

    :catch_0
    return-void
.end method

