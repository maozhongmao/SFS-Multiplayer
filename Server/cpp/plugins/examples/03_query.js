// 【类别 3】查询 —— sfs.players() / sfs.getWorldTime() / sfs.serverInfo()
// 不需要能力位（数据本来就随快照/服务端自带）。
//   sfs.players()        最近快照里的在线玩家 [{playerId, name, controlledRocket}]
//   sfs.getWorldTime()   当前世界时间（秒）；失败返回负数
//   sfs.serverInfo()     在线人数等（成功返回字符串，失败返回负数）
// 编译：qjsc -b -o 03_query.qbc 03_query.js

function onEvent(event) {
  if (event.kind !== "command" || event.command !== "status") return;

  var list = sfs.players();
  var names = [];
  for (var i = 0; i < list.length; i++) names.push(list[i].playerId + ":" + (list[i].name || "?"));
  sfs.log("players=" + list.length + " [" + names.join(",") + "]");

  var t = sfs.getWorldTime();
  var info = sfs.serverInfo();
  sfs.log("worldTime=" + t + " server=" + info);
}
globalThis.onEvent = onEvent;
onEvent;
