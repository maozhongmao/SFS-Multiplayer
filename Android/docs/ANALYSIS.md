# SFS Pro (FLDPXIce) 授权系统逆向分析报告

> 分析目标：`com.FLDPXIce.SpaceflightSimulatorPro`（第三方"盗版当正版卖"的魔改包）
> 分析版本：SFS-Pro0930（2026.9.30 / build 20260912）+ SFS-Pro1004（2026.10.04 / build 2026100402）
> 分析日期：2026-10-15
> 工作目录：`D:\SFS-Pro`

---

## 0. 结论速览（TL;DR）

1. **真实授权后端**：`http://8.136.134.174:8000`（阿里云 IP，JSON API）。**不是** www.mini1.cn（那个只是"打开迷你世界"按钮的跳转链接）。
2. **服务器是自研的"SFS MOD注入器"**（Python + FastAPI + MySQL），自述卖点："一机一密 · 限时绑定防倒卖 · **零轮询游戏检测**"。
3. **没有联网心跳**（两个版本都验证过）：
   - 授权层 `libstub.so` 连 socket/sleep/pthread_create 都没有导入；
   - Java 层没有任何授权相关的定时器；
   - 服务器自己都写着"零轮询"。
   - 检查全部发生在**启动时**（+首次激活时），之后靠本地缓存的 `license.dat`。
4. 新版（1004）新增的所谓"检测加强"是：**mod 层（libpatch.so）定期"拉取"本地授权状态来决定功能放不放** —— 纯本地内存读取，不联网。
5. 未来若真加"心跳"，几乎必然表现为：Java 定时器（classes2.dex）或 native 新增 `pthread_create/nanosleep/socket` 导入。已备好自动扫描脚本 `tools/scan_apk.py`。

---

## 1. 包体结构与版本对比

| 项目 | 0930 | 1004 |
|---|---|---|
| 文件大小 | 152,326,126 B | 153,571,414 B（服务器 `release_apk_size` 列表命中）|
| 版本 | 2026.9.30 / build 20260912 | 1.6.00.22.2026.10.04 / build 2026100402 |
| classes.dex | 8,177,040 B（Unity/Google 库）| 同 0930 |
| **classes2.dex** | 177,048 B（91 类，授权+mod 壳）| 179,780 B（119 类，**重构**：UI 拆分 + LicenseApi + IntegrityMeta）|
| classes3.dex | 8,168,648 B（Unity/Google 库）| 同 0930 |
| libstub.so | 368,352 B（逻辑在 Java 一半）| **596,976 B（逻辑全下沉 native）**|
| libs.so | 23,456 B | 28,600 B |
| libpatch.so | 1,830,xxx B（mod 框架）| 2,060,672 B |
| 其余 so | libil2cpp/libunity/libmain/libsfsmod/libbcrypto/libhb/libdatastore 两版全同 | 同 |

**1004 新增类**：`LicenseApi`（全部 native）、`IntegrityMeta`（完整性清单）、`AnaisUi/BmlUi/FnUi/IcUi/InfoUi/KeypadUi/MpChatUi/PdUi/PeUi/SasUi/UiCore/UiPos/UiScene/UiWin`（UI 模块化）。

---

## 2. 授权系统全流程（已验证）

### 2.1 服务器 API（GET/POST，明文 HTTP，JSON）

| 端点 | 用途 | 说明 |
|---|---|---|
| `GET /api/config/salt` | 服务器下发全局盐 | 实测返回 `{"salt":"f89c86a8..."}`（16 字节 hex），**设备指纹用**，首次联网拉取后缓存 |
| `GET /api/config/app` | 配置/版本门槛 | 返回 `client_version`、`min_build`、`legacy_license_policy`、`release_apk_size`（历史各份包尺寸清单！）等 |
| `POST /api/bind/submit` | 提交激活码绑定 | body: `{"temp_code","device_hash","timestamp"}` |
| `POST /api/auth/get_key` | 取设备密钥 d+iv | 返回经 **RSA-PSS 签名**的 `d/iv/nonce/server_ts` |
| `POST /api/auth/get_cek` | 取加密的内容密钥 | 返回 `wrapped_cek`（AES-CBC 包）、`expire_time` 等，同样 RSA-PSS 签名 |
| `GET /api/offline/check?device_hash=&code=` | 离线码吊销检查 | 返回 `data.revoked`、`code` |

**8.136.134.174 就是它全部的授权后端**。迷你世界（mini1.cn / com.minitech.miniworld）只是 mod 界面里一个"打开迷你世界"按钮的兜底跳转（ModUIHelper:538），与授权无关。

### 2.2 加解密流程（客户端侧）

- **激活**：输入 32 位绑定码 → `/api/bind/submit` → `/api/auth/get_key` 拿 `d`+`iv`（RSA-PSS(SHA-256, MGF1, salt=222) 验签，公钥硬编码 `LICENSE_PUBKEY_B64`）→ `/api/auth/get_cek` 拿 `wrapped_cek`（再验签）→ `AES/CBC/PKCS5` 用 d/iv 解出 **32 字节 CEK**。
- **本地授权文件**：`license.dat`，AES-CBC 加密，Key/IV 由设备指纹 + 盐 `|SFS-LICENSE|2026` / `|SFS-LICENSE-IV|2026` 派生；文件头版本 `SFSL2`/`SFSL3`。
- **离线码**：ECDSA（`SHA256withECDSA`，`OFFLINE_PUBKEY_B64`）验签，过期时间内置。
- **试用码**：`SFSPRO-TRIAL` 前缀，`TRIAL_SECONDS = 3600`（1 小时体验卡），本地文件记录剩余（`trialLoad/trialStore/trialRemainingSec`）。
- **设备指纹**：`SHA-256(BOARD|BRAND|DEVICE|HARDWARE|MANUFACTURER|MODEL|PRODUCT|android_id|服务器盐)`，64 hex。**一机一密**——换机要"联系作者解绑"。

### 2.3 完整性/防篡改体系

- **APK 签名校验**（libstub）：签名证书 SHA-256 必须等于 `aff3e95b...` 或 `b51ca1ba...`。
- **libil2cpp.so 校验**：SHA-256 == `32ce44a7...`（0930）
- **1004 增强**（`IntegrityMeta`）：7 个 so 的**尺寸 + SHA-256** 全量校验（libil2cpp/libunity/libmain/libsfsmod/libbcrypto/libhb/libdatastore_shared_counter）。实测两版包内这 7 个库哈希全部吻合。
- **apkSize + 服务器 `release_apk_size` 清单**：每卖一份包体尺寸略有差异（定制水印），尺寸清单在服务器——**重打包后尺寸对不上可能被识别**（新版有"安装包与发布包不符"提示语）。
- **libs.so 用 ED25519 验证 `libpatch.so`**（配套 `libpatch.so.sig`），验不过拒载。
- `ALLOW_INSECURE_VERIFY = false`（两版）——签名校验失败直接锁死，没有"放行"后门。
- 反调试：`detectHook`（检测到"非法环境"禁止激活）。
- 注意：**libstub.so 自身不在 7 库哈希清单里**（未来分析/处理时的重要事实）。

### 2.4 启动检查时序（checkAndStart，native）

1. 读 `license.dat` → 无 → 显示激活界面（输 32 位码）。
2. 有 → 起 Java 线程做：设备指纹比对 → `/api/config/app` 版本门槛（旧版弹"请更新后再进入游戏"）→ 若离线码：过期检查 + `/api/offline/check` 吊销检查 → 否则 `/api/auth/get_key` 在线检查（`code==0` 通过）。
3. 通过 → `setLicensedCached(true)` → 进游戏；失败 → 删授权 + 锁 UI。

**"断网能玩"的原因**：有本地 `license.dat` 且未过期时，网络请求失败走放行分支（`ERR|-1` → startGame）；且服务器承认"零轮询"设计。联网时旧版会被版本门槛拦下（服务器现已推 2026.10）。

---

## 3. "心跳"问题专项结论

### 3.1 两版都没有联网心跳（证据）

| 证据 | 0930 | 1004 |
|---|---|---|
| libstub.so 导入表含 socket/connect/nanosleep/pthread_create？ | ❌ 无 | ❌ 无 |
| libstub.so 含 sleep/timer 相关字符串？ | ❌ 无 | ❌ 无 |
| Java 层授权相关定时器？ | ❌ 无 | ❌ 无（只有 UI/FPS 计时）|
| 服务器自述 | — | "零轮询游戏检测" |

### 3.2 新版"检测加强"的实质（1004 新增）

`libpatch.so`（mod 框架）新增：定期从 **Java 层读缓存状态**：

```
PULL: ModUIHelper.licenseCached() → 读 LicenseApi.s_licensedCached（纯本地静态字段）
GRANT: java reported licensed / NOT licensed
method_interp blocked (not licensed/tampered)   ← 未授权时拦截 mod 方法
store_dll refused (not licensed)                ← 未授权时拒绝挂 dll
```

- 触发方式：拦截到 mod 方法调用时检查 + 日志函数每 4096 次调用采样检查一次（非定时器、非网络）。
- **本质**：把授权判定散布到 mod 层，防止"只换 Java 授权层"的半吊子破解。**不产生任何网络流量。**

### 3.3 如果未来真加心跳，会是什么样（预判 + 检测法）

最容易出现的两种实现：

1. **Java 定时器**：classes2.dex 里出现 `postDelayed/scheduleAtFixedRate/Timer` 且调用 `licenseCheck()` 之类 → 用扫描脚本一眼看出。
2. **Native 线程**：libstub.so（或新库）导入表新增 `pthread_create + nanosleep/usleep`（或 socket）→ 扫描脚本的"红旗导入"检查会报。

**检测脚本已放好**：`D:\SFS-Pro\tools\scan_apk.py`（用法见文件头注释）。拿到新版包先跑它：
```bash
python D:/SFS-Pro/tools/scan_apk.py D:/SFS-Pro/SFS-ProXXXX.apk
```
它会输出：体积/哈希对比、新类、新增字符串、红旗导入、路径表、完整性清单变化、与基线（0930/1004）的差异。

---

## 4. 应对手册（按场景）

### 场景 A：只求"当前能玩、以后不被远程搞死"
- 当前两版**都无心跳**：断网玩即可（记得首次激活需联网）。
- **阻断服务器**（防吊销/防强制升级）：因为服务器是**固定 IP**（`8.136.134.174:8000`），DNS/hosts 拦不住，需要用**按应用防火墙**（如 RethinkDNS/个人DNS过滤器，无需 root 的本地 VPN 类工具）把该 IP 段或该应用出网掐掉，游戏就用缓存的 `license.dat`。
- 如果新版对"连不上服务器"改为**失败即锁**（fail-closed），断网策略会露馅 → 走场景 C。

### 场景 B：验证新版行为（推荐流程）
1. 安装新版 → 正常联网激活/进入一次（`license.dat` 落地）。
2. `adb logcat | grep -i "license\|token\|api"` 记录行为。
3. 断网运行 30 分钟：不锁 → 无强制心跳或 fail-open；锁死 → fail-closed。
4. 抓包（PC 开 mitmproxy/Charles，手机代理指向 PC；数据是**明文 HTTP**，好抓）确认有没有启动后周期性请求 `/api/auth/*`。

### 场景 C：真要动刀（技术路线，供研究）
优先级从低风险到高风险：

1. **本地假服务器**：不行。响应有 RSA-PSS 签名，`ALLOW_INSECURE_VERIFY=false`，伪造响应必被 `verifySig` 拒绝。
2. **Frida hook**（需 root）：hook `licenseCached()` 返回 true、hook `verifySig` 返回 true、hook `startGame 前置检查`——注意 `detectHook` 会查环境（先处理它）。libstub 逻辑全在 native，直接 hook 导出符号（`Java_com_sfsmod_*`）即可，符号没剥。
3. **改包路线**（libstub 不在 7 库哈希清单里，是个口子）：
   - libstub.so 承载全部校验（apk 签名/尺寸/7 库哈希/服务器校验逻辑），把它替换成"全放行版"理论上能过；
   - 但改 APK 会破坏 APK 签名（v1/v2），而重签后 `apkSignatureOk` 的证书比对会失败 → 所以必须同时把 libstub 里的 `apkSignatureOk`、`apkSize`、`libsIntegrityOk` 等一并 patch 掉；
   - `libpatch.so` 有 ED25519 自校验链（libs.so 验它），不动它就没事；
   - 这是纯研究路线，工作量不小，且属于对"加壳商业包"的破解行为——你自己权衡。
4. **等新版包**（最省事）：版本门槛说明新版还在持续发（服务器 `release_apk_size` 列表里 30+ 个尺寸），跟着包走 + 断网玩 + 用脚本体检。

### 场景 D：维权/举报线索
- 卖家的服务器自报门牌：**QQ群 631961524**，"SFS MOD注入器"；每份 APK 有专属尺寸水印（服务器留了全部销售记录）。
- libpatch.so 里泄露了作者构建路径：`D:\2SFS Project\testdll\UIToolsStub\`。
- 如果打算向平台/官方举报，以上 + 本报告 + 服务器响应截图都是证据。

---

## 5. 文件与哈希清单（D:\SFS-Pro）

```
SFS-Pro0930.apk            sha256 78709017458bb2df3b44b644...（152,326,126 B）
SFS-Pro1004.apk            sha256 068a4d75ca954cd11fea346e...（153,571,414 B）
dex/        0930 的三个 dex
dex1004/    1004 的三个 dex
jadx_out/   0930 classes2 反编译（Java 源码）
jadx_out1004/ 1004 classes2 反编译
native/     0930 提取的 so（libstub/libhb/libs/libpatch/libil2cpp）
native1004/ 1004 提取的 so
jadx/       jadx 1.5.6 工具
tools/scan_apk.py   ← 新版本自动体检脚本
ANALYSIS.md ← 本文件
```

关键常量备忘：

```
服务器   http://8.136.134.174:8000        （XOR 0x5A 编码）
指纹盐   /api/config/salt                 （全局盐，缓存）
Key盐    |SFS-LICENSE|2026
IV盐     |SFS-LICENSE-IV|2026
RSA公钥  MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8A...(见 LicenseApi.java)
EC公钥   MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcD...(见 LicenseApi.java)
证书哈希 aff3e95b8d66966c7832c41142ec9df5874d88e2da1db6e24d0f83b984e838d0
         b51ca1ba3e74ccadf68fff1fcef72bc4c805b73cdc4b034eb31590add857cba5
libil2cpp哈希 32ce44a76de27896afd5ea0aa243e1ac40b7ebaa78babf3a23264a398f0cffcc（0930） / eee179ed...（1004）
```

---

## 6. 去壳补丁记录（1004 版，2026-10-15）

**目标**：移除整套授权/激活/校验，直接进游戏（自用）。

**方法**：补丁 `libstub.so`（它承载全部校验逻辑，且不在 7 库哈希清单里），共 7 处、48 字节：

| 符号 | 补丁 | 语义 |
|---|---|---|
| `LicenseActivity.licenseExists` | `mov w0,#1; ret` | 恒"有授权"（全新安装也走直通路径）|
| `LicenseActivity.checkAndStart` | `b startGame` | 跳过验证线程 |
| `LicenseActivity$1.onClick` | `b startGame` | 激活按钮兜底直通 |
| `LicenseApi.licenseCached` | `mov w0,#1; ret` | mod 层 PULL 恒"已授权" |
| `LicenseApi.apkSignatureOk` | `mov w0,#1; ret` | 跳过 APK 签名校验 |
| `LicenseApi.libil2cppOk` | `mov w0,#1; ret` | 跳过 il2cpp 校验 |
| `LicenseActivity.detectHook` | `mov w0,#0; ret` | 跳过环境检测 |

**关键设计点**：
- `onCreate` **不能**直接跳过（必须先 `super.onCreate`，否则 SuperNotCalledException 崩进程）→ 改为"让 licenseExists 恒真"，自然走 `super → readDeviceHash → checkAndStart(→startGame)` 链
- `startGame` 自身只做 `Intent.setClassName(包名, ExtendedUnityActivity) → startActivity → finish()`，无授权依赖，可安全直呼
- 重打包替换 libstub.so（DEFLATE），其余文件字节原样；zipalign + v1/v2/v3 重签名（自签证书）

**产物**：
```
build/signed/SFS-Pro1004-patched-aligned-signed.apk   ← 可安装（自签）
build/SFS-Pro1004-patched-unsigned.apk                 ← 中间物
build/libstub_patched.so                               ← 补丁后内核（sha256 a029b661...）
tools/build_patch.py                                   ← 一键流水线（符号名驱动，新版本可复用）
```

**注意**：自签证书与原包不同 → 必须卸载原版再装；游戏存档在 `Android/media/` 共享目录，卸载不影响。
