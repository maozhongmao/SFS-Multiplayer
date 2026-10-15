// example_plugin.js - SFS-Multiplayer 示例插件（JS 侧契约见 src/protocol/PLUGIN_API.md）
// =============================================================================
// 这份文件同时是两种东西：
//
//   1) 真插件源码：用 quickjs-ng 编译成 .qbc 字节码后交给宿主执行。
//      宿主只跑字节码，没有源码求值入口（PLUGIN_API.md 第 1 节）。
//
//   2) 本地调试镜像：src/plugins/demo/demo_js_engine.{hpp,cpp} 的 DemoEngine
//      不认 JavaScript，只执行本文件里带 @demo 标记的注释行（标记本身是 "@demo" 加冒号）。
//      命令逐行对应下面 onEvent() 的真逻辑，所以不开 QuickJS 也能把
//      "装载 -> 每秒投递 -> 调宿主 API -> 卸载" 整条链路跑通。
//      接上 QuickJS 后这些标记行只是普通注释，可以留着也可以删掉。
//
// 编译成字节码（将来接入后）：
//   qjsc -c -o example_plugin.qbc example_plugin.js
// 宿主侧装载：PluginDescriptor{ id="example-plugin", capabilities=kCapLog|...,
//   limits=..., bytecode=<.qbc 字节> }，然后 host.subscribe(id, kEventWorldSnapshot)。
//
// -----------------------------------------------------------------------------
// sfs.* 契约速查（用途 / 限制，全部来自 PLUGIN_API.md 第 2/4 节）
//
//   sfs.log(text)
//     用途：写服务端日志，排查用。这是唯一不需要 HostServices 适配层的出口。
//     限制：需要 log 能力位；text <= 255 字节；非 ASCII 字符会被宿主替换成 '?'。
//     返回：0 成功，-1 无能力，-2 参数非法，-4 本轮 API 预算用尽。
//
//   sfs.readRocket(rocketId)
//     用途：按 id 拉一枚火箭的当前快照（事件里的 rockets[] 已含快照，通常不必再拉）。
//     限制：需要 readRocket 能力位；rocketId 必须是 >= 0 的整数。
//     返回：成功是 RocketView 对象；失败是负的状态码（-1/-2/-3/-5/-6）。
//
//   sfs.writeRocket(view)
//     用途：写回火箭快照（本示例不用；改世界要非常克制）。
//     限制：需要 writeRocket 能力位；rocketId 必须已存在；partCount 0..4096；
//           throttle 0..1；坐标/速度必须是有限数（NaN/Inf 被拒）；每轮最多 64 次。
//     返回：0 成功，-2 参数非法，-3 不存在，-4 预算/写次数用尽。
//
//   sfs.sendMessage(playerId, text)
//     用途：给指定玩家发系统消息。
//     限制：需要 sendMessage 能力位；playerId >= 0；text 不能有控制字符；
//           UTF-8 长度 <= 255 字节。
//     返回：0 成功，-3 玩家不存在，-4 预算用尽。
//
// 明确不存在：文件、网络、进程/线程、模块加载、eval、高精度计时。
// 事件是"每秒一次、批量"的：worldSnapshot 一次带整张世界，不要按帧轮询。
// 同一轮投递的指令预算（默认 20 万条）或未捕获异常会让宿主把插件置为 faulted，
// faulted 是终态，需控制台重新装载。
//
// 能力位常量（描述符 capabilities 里声明）：
//   readRocket=1<<0  writeRocket=1<<1  sendMessage=1<<2  log=1<<3
// =============================================================================

// 超过这个火箭数就给每个在线玩家发一条提醒。
var ROCKET_ALERT_THRESHOLD = 3;

// 宿主每次投递都会调用这个函数；字节码求值结果必须是它。
function onEvent(event) {
  // 离散小事件也会走同一个入口，先按 kind 分流。
  if (event.kind === "playerJoined") {
    sfs.log("player joined id=" + event.player.playerId);
    return;
  }
  if (event.kind !== "worldSnapshot") {
    return;
  }

  var tick = event.tick;
  var rockets = event.rockets.length;
  var players = event.players.length;

  // 每秒一行日志：成本低、最容易确认插件真的在跑。
  sfs.log("tick=" + tick + " rockets=" + rockets + " players=" + players);

  // readRocket 示例：拿第一名火箭的部件数。空世界时不要索引。
  if (rockets > 0) {
    var view = sfs.readRocket(event.rockets[0].rocketId);
    if (typeof view === "object") {
      sfs.log("first rocket parts=" + view.partCount);
    }
  }

  // 超过阈值：给每个在线玩家发一条消息（对应 DemoEngine 的 sendall）。
  if (rockets > ROCKET_ALERT_THRESHOLD) {
    for (var i = 0; i < players; i++) {
      sfs.sendMessage(event.players[i].playerId, "world has " + rockets + " rockets");
    }
  }
}

// -----------------------------------------------------------------------------
// DemoEngine 可执行镜像（命令表 v0，见 demo_js_engine.cpp）：
//   set <name> = <expr>           绝对值/字符串/事件字段/变量
//   log <expr>                    调 sfs.log
//   sendmsg <idExpr> <textExpr>   调 sfs.sendMessage
//   sendall <textExpr>            给快照里每个玩家各发一条
//   getrocket <idExpr> into <var> 调 sfs.readRocket，成功存 RocketView、失败存负状态码
//   setthrottle <var> <expr>      改本地视图的 throttle（配合 writeback 调 sfs.writeRocket）
//   writeback <var>               调 sfs.writeRocket
//   when <kind> <action>          只在该事件类型上执行（any = 任意）
//   if <a> <op> <b> then <action> 条件执行（op: > < >= <= == !=）
// 表达式支持 + - * / 和括号；字符串里 @name / @a.b 会插值成变量或事件字段。
// -----------------------------------------------------------------------------
// @demo: when playerJoined log "player joined id=@player.playerId"
// @demo: when worldSnapshot set tick = event.tick
// @demo: when worldSnapshot set rockets = event.rockets.count
// @demo: when worldSnapshot set players = event.players.count
// @demo: when worldSnapshot log "tick=@tick rockets=@rockets players=@players"
// @demo: when worldSnapshot if rockets > 0 then getrocket event.rockets[0].rocketId into first
// @demo: when worldSnapshot if rockets > 0 then log "first rocket parts=@first.partCount"
// @demo: when worldSnapshot if rockets > 3 then sendall "world has @rockets rockets"
