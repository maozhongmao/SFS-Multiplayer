// 【类别 4】管理 —— sfs.kick(id, reason) / sfs.clearDebris(maxParts) / sfs.setTimeScale(x)
// 能力位：admin (1<<4)  ← 管理类都需要它（危险操作要显式声明）
//   返回 0 = 成功；负数 = 拒绝（-1 无能力 / -2 参数非法 / -3 不存在 / -4 预算用尽）
// 编译：qjsc -b -o 04_admin.qbc 04_admin.js

var players = [];
function onEvent(event) {
  if (event.kind === "worldSnapshot") { players = event.players || []; return; }
  if (event.kind !== "command") return;

  if (event.command === "clean") {
    sfs.log("clearDebris -> " + sfs.clearDebris(6));
  } else if (event.command === "speed2") {
    sfs.log("setTimeScale(2) -> " + sfs.setTimeScale(2.0));
  } else if (event.command === "boot" && players.length > 0) {
    sfs.log("kick -> " + sfs.kick(players[0].playerId, "bye"));
  }
}
globalThis.onEvent = onEvent;
onEvent;
