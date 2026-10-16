// =============================================================================
// SFS-Multiplayer 示例插件（QuickJS）—— 这份文件本身就是文档
// =============================================================================
//
// 【一分钟上手】
//   1) 插件就是一个函数 onEvent(event)：收到事件 → 判断 → 调 sfs.* API
//   2) 编译（必须 -b，否则 qjsc 出的是 C 源码，服务端会报 engine rejected bytecode）：
//        qjsc -b -o test_broadcast.qbc test_broadcast.js
//   3) 启动服务端（编译期开关 + 运行期指向字节码）：
//        SFS_PLUGINS=1 SFS_PLUGIN_PATH=test_broadcast.qbc ./sfs-server-kcp-arm --port 9806
//   4) 在服务端控制台敲 test 或 who 即见效
//
// 【插件能调用的 API】能力位没声明的调用会被拒绝（返回负数）
//   sfs.log(text)                    写服务端日志（非 ASCII 会变成 ?，≤255 字节）
//   sfs.sendMessage(playerId, text)  发给指定玩家；playerId < 0 表示全体广播
//   sfs.broadcast(text)              等价于 sendMessage(-1, text)，全体广播
//   sfs.players()                    最近一次快照里的在线玩家 [{playerId, name, controlledRocket}]
//   sfs.readRocket(rocketId)         读一枚火箭（服务端侧尚未开放，当前总失败）
//   sfs.writeRocket(view)            写回火箭（服务端侧尚未开放）
//   能力位常量（插件描述符里声明）：readRocket=1<<0 writeRocket=1<<1 sendMessage=1<<2 log=1<<3
//
// 【插件能收到的事件】事件是批量、每秒一次的，不按帧回调
//   "worldSnapshot"   每秒一次：tick / worldTime / rockets[] / players[] / droppedRockets
//   "playerJoined"    玩家加入：player.playerId / player.name
//   "playerLeft"      玩家离开：同上
//   "chatMessage"     玩家聊天：text / player.playerId
//   "command"         服务端控制台命令：command（命令名）/ text
//   注意：单个事件不带全部信息（玩家名单只在 worldSnapshot 里）——
//        常见写法是"快照时记住、命令时使用"，本文件就是这个模式。
//
// 【四个坑（都是踩过的）】
//   1. qjsc 不加 -b 出的是 C 源码，不是字节码。
//   2. -b 的字节码按模块作用域编译，函数声明不会成为全局
//      ⇒ 文件末尾必须写 globalThis.onEvent = onEvent;（再补一句 onEvent; 兼容另一种取法）
//   3. 插件命令名会被服务端内置命令截走（内置优先）：
//      help / status / say / kick / stop / players … 都是内置的，插件要换名字。
//   4. 服务端输出是行缓冲的，插件调试看不到输出时先确认宿主有没有 fflush（宿主已处理）。
//
// 【沙箱边界（有意为之）】没有文件、没有网络、没有进程线程、没有 eval、没有高精度计时，
//   也不能任意改世界（只能通过受限的 writeRocket，且限流 + 拒绝 NaN/Inf）。
//   每个插件一份独立 JSRuntime，超预算或抛异常会被标记 faulted 并停止投递，不影响其他插件与服务端。
// =============================================================================

var STARTED = false;
var LAST_PLAYERS = [];   // worldSnapshot 里最近一次的玩家列表（命令事件里没有玩家名单）

// 全体广播：服务端的 sendMessage 落到按 player_id 分发，
// player_id < 0 表示全体；这里用第一个在线玩家做"发起人"只为拿到一个合法 id。
function broadcastToAll(text) {
  if (LAST_PLAYERS.length === 0) return 0;
  return sfs.sendMessage(LAST_PLAYERS[0].playerId, text) === 0 ? LAST_PLAYERS.length : 0;
}

function onEvent(event) {
  // 启动提示：宿主装载后投递的第一个事件就提示一次（此后不再重复）。
  if (!STARTED) {
    STARTED = true;
    sfs.log("test_broadcast loaded; commands: test, who");
  }

  // 每秒一次的世界快照：把在线玩家记下来（命令事件里没有名单）。
  if (event.kind === "worldSnapshot") {
    LAST_PLAYERS = event.players || [];
    return;
  }

  // 控制台命令：宿主以 kind="command" 投递（与玩家聊天分开）。
  if (event.kind === "command") {
    if (event.command === "test") {
      // 命令 test：给全体玩家广播 "test"
      var sent = broadcastToAll("test");
      sfs.log("command test: broadcast to " + sent + " player(s)");
    } else if (event.command === "who") {
      // 命令 who：播报在线玩家（用 sfs.players() 拿结构化名单，不必自己翻事件）
      var list = sfs.players();
      var names = [];
      for (var i = 0; i < list.length; i++) names.push(list[i].playerId + ":" + (list[i].name || "?"));
      sfs.log("command who: " + list.length + " online [" + names.join(",") + "]");
      sfs.broadcast("online: " + list.length);
    } else {
      sfs.log("unknown command: " + event.command);
    }
    return;
  }
}

// 关键：显式把入口挂到 globalThis（见上方"坑 2"）。
globalThis.onEvent = onEvent;
onEvent;
