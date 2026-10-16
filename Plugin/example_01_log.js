// 【类别 1】日志 —— sfs.log(text)
// 能力位：log (1<<3)
// 用途：写服务端日志（非 ASCII 会被替换成 '?'；上限 255 字节）。
// 编译：qjsc -b -o 01_log.qbc 01_log.js
// 运行：SFS_PLUGINS=1 SFS_PLUGIN_PATH=01_log.qbc ./sfs-server-kcp-arm --port 9806

function onEvent(event) {
  if (event.kind === "worldSnapshot" && event.tick % 30 === 0) {   // 每 30 秒一行，别刷屏
    sfs.log("tick=" + event.tick + " rockets=" + event.rockets.length);
  }
  if (event.kind === "playerJoined" && event.player) {
    sfs.log("player joined: " + event.player.playerId);
  }
}
globalThis.onEvent = onEvent;   // 必须：-b 的字节码按模块作用域编译
onEvent;
