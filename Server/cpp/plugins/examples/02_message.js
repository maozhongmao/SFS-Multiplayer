// 【类别 2】消息 —— sfs.broadcast(text) / sfs.sendMessage(playerId, text)
// 能力位：sendMessage (1<<2)
//   sfs.broadcast(text)            全体广播
//   sfs.sendMessage(pid, text)     只发给该玩家（pid < 0 视为全体）
// 客户端表现：游戏内聊天窗。
// 编译：qjsc -b -o 02_message.qbc 02_message.js

var players = [];
function onEvent(event) {
  if (event.kind === "worldSnapshot") { players = event.players || []; return; }

  if (event.kind === "playerJoined" && event.player) {
    // 私聊：只给刚进来的那个玩家发一句
    sfs.sendMessage(event.player.playerId, "welcome!");
    return;
  }
  if (event.kind === "command") {
    if (event.command === "hi") {
      sfs.broadcast("hello everyone");            // 全体
    } else if (event.command === "dm" && players.length > 0) {
      sfs.sendMessage(players[0].playerId, "just for you");   // 私聊
    }
  }
}
globalThis.onEvent = onEvent;
onEvent;
