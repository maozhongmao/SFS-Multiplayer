// webui.js — 服务端插件：给 WebUI/控制台提供轻量状态快照
// 能力位：log (1<<3) | sendMessage (1<<2)
// 说明：QuickJS 沙箱没有 HTTP/文件/网络；本插件只产出状态文本。
//       真正 WebUI 页面/HTTP 服务需要外部进程消费日志或聊天广播。
//
// 编译：qjsc -b -o webui.qbc webui.js
// 运行：SFS_PLUGINS=1 SFS_PLUGIN_PATH=webui.qbc ./sfs-server-kcp-arm --port 9806
//
// 控制台命令（避开内置命令名）：
//   webui_status        立即输出一行状态
//   webui_json          输出 JSON 状态
//   webui_on            开启定时广播
//   webui_off           关闭定时广播
//   webui_interval_5    每 5 秒广播一次
//   webui_interval_30   每 30 秒广播一次

var ui = {
  enabled: true,
  intervalTicks: 30,      // worldSnapshot 每秒一次，30 即 30 秒
  lastTick: -999999,
  tick: 0,
  players: [],
  rockets: []
};

function ascii(s) {
  s = String(s == null ? "" : s);
  var out = "";
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    if (c >= 32 && c <= 126) out += s.charAt(i);
    else out += "?";
  }
  return out;
}

function clampText(s, max) {
  s = ascii(s);
  if (s.length <= max) return s;
  return s.substring(0, max - 3) + "...";
}

function playerSummary() {
  var arr = [];
  var n = ui.players.length;
  for (var i = 0; i < n && i < 16; i++) {
    var p = ui.players[i] || {};
    arr.push(String(p.playerId) + ":" + ascii(p.name || "?"));
  }
  if (n > 16) arr.push("+" + (n - 16));
  return arr.join(",");
}

function statusText() {
  var wt = sfs.getWorldTime();
  var info = sfs.serverInfo();
  return "WEBUI tick=" + ui.tick +
    " players=" + ui.players.length +
    " [" + playerSummary() + "]" +
    " rockets=" + ui.rockets.length +
    " worldTime=" + wt +
    " server=" + ascii(info);
}

function statusJson() {
  var wt = sfs.getWorldTime();
  var info = sfs.serverInfo();
  var obj = {
    tick: ui.tick,
    players: ui.players.length,
    playerList: [],
    rockets: ui.rockets.length,
    worldTime: (typeof wt === "number" && wt >= 0) ? wt : -1,
    serverInfo: ascii(info)
  };

  for (var i = 0; i < ui.players.length && i < 32; i++) {
    var p = ui.players[i] || {};
    obj.playerList.push({
      playerId: p.playerId,
      name: ascii(p.name || "?")
    });
  }
  return JSON.stringify(obj);
}

function emit(text) {
  text = clampText(text, 240);
  sfs.log(text);
  sfs.broadcast(text);
}

function onEvent(event) {
  if (!event || !event.kind) return;

  if (event.kind === "worldSnapshot") {
    ui.tick = event.tick || 0;
    ui.players = event.players || [];
    ui.rockets = event.rockets || [];

    if (ui.enabled && ui.tick - ui.lastTick >= ui.intervalTicks) {
      ui.lastTick = ui.tick;
      emit(statusText());
    }
    return;
  }

  if (event.kind === "playerJoined" && event.player) {
    sfs.log("webui: player joined " + ascii(event.player.playerId));
    return;
  }

  if (event.kind === "playerLeft") {
    sfs.log("webui: player left " + ascii(event.playerId || "?"));
    return;
  }

  if (event.kind !== "command") return;

  var cmd = String(event.command || "");

  if (cmd === "webui_status") {
    emit(statusText());
  } else if (cmd === "webui_json") {
    emit(statusJson());
  } else if (cmd === "webui_on") {
    ui.enabled = true;
    ui.lastTick = -999999;
    emit("WEBUI enabled interval=" + ui.intervalTicks + "s");
  } else if (cmd === "webui_off") {
    ui.enabled = false;
    emit("WEBUI disabled");
  } else if (cmd === "webui_interval_5") {
    ui.intervalTicks = 5;
    emit("WEBUI interval=5s");
  } else if (cmd === "webui_interval_30") {
    ui.intervalTicks = 30;
    emit("WEBUI interval=30s");
  }
}

// 必须：-b 字节码按模块作用域编译，函数声明不是全局
globalThis.onEvent = onEvent;
onEvent;