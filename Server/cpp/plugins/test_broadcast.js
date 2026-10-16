// test_broadcast.js - 新增命令 test：给全体玩家广播消息 "test"；插件启动时给出提示。
// JS 侧契约见 Server/cpp/src/protocol/PLUGIN_API.md
// 能力位：sendMessage=1<<2 (4) | log=1<<3 (8)  =>  12
//
// 编译成字节码（宿主只跑字节码，没有源码求值入口）：
//   qjsc -o test_broadcast.qbc test_broadcast.js
//
// 需要宿主支持的事件种类：command（形如 { kind:"command", command:"test", args:"" }）

var STARTED = false;

// 给快照里每个在线玩家各发一条，返回成功条数。事件是每秒一次的批量投递。
function broadcastToAll(event, text) {
  if (!event.players) return 0;
  var sent = 0;
  for (var i = 0; i < event.players.length; i++) {
    if (sfs.sendMessage(event.players[i].playerId, text) === 0) sent++;
  }
  return sent;
}

// 末尾的 `onEvent;`（见文件底部）让字节码的"完成值"就是入口函数：
// qjsc 出的脚本以函数声明结尾时完成值是 undefined，宿主因此拿不到入口。
function onEvent(event) {
  // 启动提示：宿主装载后投递的第一个事件就提示一次（此后不再重复）。
  if (!STARTED) {
    STARTED = true;
    sfs.log("test_broadcast loaded; command 'test' broadcasts \"test\" to all players");
  }

  // 新命令：test -> 全体广播 "test"
  // 控制台的未知命令由宿主以 chatMessage 投递（event.player.playerId === -1 表示来自控制台）。
  // 控制台命令由宿主以 chatMessage 投递（来源 playerId = -1）；
  // 不强依赖 player 字段，避免宿主实现差异导致命令被静默忽略。
  if (event.kind === "chatMessage") {
    if (event.text === "test") {
      var sent = broadcastToAll(event, "test");
      sfs.log("command test: sent \"test\" to " + sent + " player(s)");
    } else {
      sfs.log("unknown command: " + event.text);
    }
    return;
  }

  if (event.kind === "playerJoined") {
    sfs.log("player joined id=" + event.player.playerId);
  }
}

// 关键：显式把入口挂到 globalThis。
// qjsc -b 出的字节码是按模块作用域编译的，函数声明不会自动成为全局；
// 同时保留完成值，两条路都能被宿主取到。
globalThis.onEvent = onEvent;
onEvent;
