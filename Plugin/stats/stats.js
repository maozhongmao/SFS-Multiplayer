// =============================================================================
// 在线时长统计
// =============================================================================
// 能力位: log(1<<3) + sendMessage(1<<2)
//
// 记录每个玩家本次连接的上线时间、在线时长，支持排行榜。
// 状态存在插件内存里（服务端重启即清空）—— 沙箱没有文件系统，无法持久化。
//
// 控制台命令（宿主会丢弃命令参数，所以都是独立命令名）:
//   stats        广播一句话摘要（谁在线、多久）
//   statstop     广播在线时长排行榜（前 5）
//   statsdetail  只写日志，输出每个玩家的精确时长（英文）
//   statsreset   重置计时基准
//
// 注意:
//   - 玩家名字当前服务端不下发（lifecycle.cpp 没填 name），显示为 #id
//   - 掉线重连算新一次会话，累计时长重新开始
//   - 插件没有时钟，只能靠 worldSnapshot 每秒的 tick 累计
// =============================================================================

// ---- 文本工具 ----

// 日志出口只接受 ASCII：sfs.log 会把非 ASCII 变成 '?'，这里提前处理并限长。
function ascii(v, max) {
  var s = String(v == null ? "" : v);
  var out = "";
  for (var i = 0; i < s.length && out.length < max; i++) {
    var c = s.charCodeAt(i);
    out += (c >= 32 && c <= 126) ? s.charAt(i) : "?";
  }
  return out;
}

// 按 UTF-8 字节截断，不切断多字节字符。sendMessage 上限 255 字节。
function clipUtf8(str, maxBytes) {
  var s = String(str == null ? "" : str);
  var bytes = 0, out = "";
  for (var i = 0; i < s.length; i++) {
    var code = s.charCodeAt(i), len;
    if (code < 0x80) len = 1;
    else if (code < 0x800) len = 2;
    else if (code >= 0xd800 && code <= 0xdbff) len = 4;
    else len = 3;
    if (bytes + len > maxBytes) break;
    bytes += len;
    out += s.charAt(i);
    if (len === 4 && i + 1 < s.length) out += s.charAt(++i);
  }
  return out;
}

// 控制字符（换行、制表等）会让 sendMessage 整条被拒，换成空格。
function sanitize(str) {
  var s = String(str == null ? "" : str);
  var out = "";
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    out += (c < 32 || c === 127) ? " " : s.charAt(i);
  }
  return out;
}

function LOG(text) {
  return sfs.log(ascii(text, 200));
}

// 全体广播。只能用 sfs.broadcast：sendMessage 的 playerId<0 会被拒绝。
function SAY(text) {
  var msg = clipUtf8(sanitize(text), 240);
  if (msg.length === 0) return -2;
  return sfs.broadcast(msg);
}

function TELL(playerId, text) {
  if (typeof playerId !== "number" || playerId < 0) return -2;
  var msg = clipUtf8(sanitize(text), 240);
  if (msg.length === 0) return -2;
  return sfs.sendMessage(playerId, msg);
}

// 秒 -> "1h23m45s"
function fmtDuration(seconds) {
  var s = Math.max(0, Math.floor(seconds));
  var h = Math.floor(s / 3600);
  var m = Math.floor((s % 3600) / 60);
  var sec = s % 60;
  if (h > 0) return h + "h" + m + "m" + sec + "s";
  if (m > 0) return m + "m" + sec + "s";
  return sec + "s";
}

// ---- 统计状态 ----

var ST = {
  players: {},   // playerId -> { since, lastSeen }
  total: {},     // playerId -> 累计秒数
  bootTick: -1,
  lastTick: 0,
  started: false
};

function nameOf(p) {
  var n = p && p.name ? String(p.name) : "";
  return n.length > 0 ? n : "#" + (p && typeof p.playerId === "number" ? p.playerId : "?");
}

function trackPlayers(list) {
  var now = ST.lastTick;
  var seen = {};

  for (var i = 0; i < list.length; i++) {
    var p = list[i];
    if (!p || typeof p.playerId !== "number") continue;
    var id = p.playerId;
    seen[id] = true;

    if (!ST.players[id]) {
      ST.players[id] = { since: now, lastSeen: now };
      if (ST.total[id] === undefined) ST.total[id] = 0;
      LOG("join id=" + id + " name=" + nameOf(p));
      TELL(id, "欢迎 " + nameOf(p) + "，本服在线时长会自动统计");
    } else {
      ST.players[id].lastSeen = now;
    }
  }

  for (var key in ST.players) {
    if (!ST.players.hasOwnProperty(key)) continue;
    var pid = parseInt(key, 10);
    if (seen[pid]) continue;
    var r = ST.players[pid];
    var dwell = Math.max(0, r.lastSeen - r.since);
    ST.total[pid] = (ST.total[pid] || 0) + dwell;
    LOG("left id=" + pid + " session=" + fmtDuration(dwell) +
        " total=" + fmtDuration(ST.total[pid]));
    SAY("#" + pid + " 离开了，本次在线 " + fmtDuration(dwell));
    delete ST.players[pid];
  }
}

function ranking() {
  var arr = [];
  for (var key in ST.players) {
    if (!ST.players.hasOwnProperty(key)) continue;
    var pid = parseInt(key, 10);
    var rec = ST.players[pid];
    var live = Math.max(0, ST.lastTick - rec.since);
    arr.push({ id: pid, secs: live, total: (ST.total[pid] || 0) + live });
  }
  arr.sort(function (a, b) { return b.secs - a.secs; });
  return arr;
}

// ---- 命令 ----

function cmdStats() {
  var arr = ranking();
  if (arr.length === 0) { SAY("当前没有人在线"); return; }
  SAY("在线 " + arr.length + " 人 | 最长 #" + arr[0].id + " " + fmtDuration(arr[0].secs));
}

function cmdTop() {
  var arr = ranking();
  if (arr.length === 0) { SAY("当前没有人在线"); return; }
  var n = Math.min(5, arr.length);
  var out = "在线时长 TOP" + n + ": ";
  for (var i = 0; i < n; i++) {
    if (i > 0) out += " | ";
    out += (i + 1) + ".#" + arr[i].id + " " + fmtDuration(arr[i].secs);
  }
  SAY(out);
}

function cmdDetail() {
  var arr = ranking();
  LOG("--- online duration (uptime=" +
      fmtDuration(Math.max(0, ST.lastTick - ST.bootTick)) + ") ---");
  for (var i = 0; i < arr.length; i++) {
    LOG("  #" + arr[i].id + " session=" + arr[i].secs + "s total=" + arr[i].total + "s");
  }
  LOG("--- " + arr.length + " player(s) ---");
}

function cmdReset() {
  ST.bootTick = ST.lastTick;
  for (var k in ST.players) {
    if (ST.players.hasOwnProperty(k)) ST.players[k].since = ST.lastTick;
  }
  LOG("stats reset at tick=" + ST.lastTick);
  SAY("在线计时已重置");
}

function onEvent(event) {
  if (!event || !event.kind) return;

  if (event.kind === "worldSnapshot") {
    if (!ST.started) {
      ST.started = true;
      ST.bootTick = event.tick || 0;
      LOG("plugin_stats loaded; commands: stats, statstop, statsdetail, statsreset");
    }
    ST.lastTick = event.tick || 0;
    trackPlayers(event.players || []);
    return;
  }

  if (event.kind === "playerJoined" && event.player) {
    trackPlayers((sfs.players() || []).concat([event.player]));
    return;
  }

  if (event.kind !== "command") return;

  var cmd = String(event.command || "");
  if (cmd === "stats") cmdStats();
  else if (cmd === "statstop") cmdTop();
  else if (cmd === "statsdetail") cmdDetail();
  else if (cmd === "statsreset") cmdReset();
}

// 关键：-b 的字节码按模块作用域编译，函数声明不会自动成为全局。
globalThis.onEvent = onEvent;
onEvent;
