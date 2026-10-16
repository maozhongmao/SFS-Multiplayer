// test_broadcast.js - 演示插件：新增命令 test（全体广播）与 players（播报在线玩家）
// 契约见 Server/cpp/src/protocol/PLUGIN_API.md；编译必须用 -b：
//   qjsc -b -o test_broadcast.qbc test_broadcast.js

var STARTED = false;
var LAST_PLAYERS = [];

function broadcastToAll(text) {
  if (LAST_PLAYERS.length === 0) return 0;
  return sfs.sendMessage(LAST_PLAYERS[0].playerId, text) === 0 ? LAST_PLAYERS.length : 0;
}

function onEvent(event) {
  if (!STARTED) {
    STARTED = true;
    sfs.log("test_broadcast loaded; commands: test, who");
  }

  if (event.kind === "worldSnapshot") {
    LAST_PLAYERS = event.players || [];
    return;
  }

  if (event.kind === "command") {
    if (event.command === "test") {
      var sent = broadcastToAll("test");
      sfs.log("command test: broadcast to " + sent + " player(s)");
    } else if (event.command === "who") {   // 注意：不能用 players —— 那是服务端内置命令，插件的同名命令会被内置的截走
      // 新 API：sfs.players() 直接给结构化名单，不用自己从事件里翻
      var list = sfs.players();
      var names = [];
      for (var i = 0; i < list.length; i++) names.push(list[i].playerId + ":" + (list[i].name || "?"));
      sfs.log("command who: " + list.length + " online [" + names.join(",") + "]");
      sfs.broadcast("online: " + list.length);   // 新 API：sfs.broadcast
    } else {
      sfs.log("unknown command: " + event.command);
    }
    return;
  }
}

globalThis.onEvent = onEvent;
onEvent;
