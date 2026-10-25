// =============================================================================
// 管理命令集
// =============================================================================
// 能力位: log(1<<3) + sendMessage(1<<2) + admin(1<<4)
//
// 把常用管理操作做成短命令。宿主会丢弃命令参数，所以每个操作都是独立命令名。
//
// 命令:
//   ainfo      服务器概况
//   aonline    在线玩家列表
//   aclean     清理残骸（6 部件以下）
//   acleanbig  清理残骸（20 部件以下）
//   aslow      时间流速 x0.5
//   anormal    时间流速 x1.0
//   afast      时间流速 x2.0
//   aturbo     时间流速 x5.0
//   aboot      踢掉第一个在线玩家
//   ahelp      命令列表
//
// 不能和内置命令重名：help status say kick players stop save time world
// network traffic sync auto debris kicked off
// =============================================================================

// ---- 文本工具 ----

function ascii(v, max) {
  var s = String(v == null ? "" : v);
  var out = "";
  for (var i = 0; i < s.length && out.length < max; i++) {
    var c = s.charCodeAt(i);
    out += (c >= 32 && c <= 126) ? s.charAt(i) : "?";
  }
  return out;
}

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

function SAY(text) {
  var msg = clipUtf8(sanitize(text), 240);
  if (msg.length === 0) return -2;
  return sfs.broadcast(msg);
}

function fmtDuration(seconds) {
  var s = Math.max(0, Math.floor(seconds));
  var h = Math.floor(s / 3600);
  var m = Math.floor((s % 3600) / 60);
  var sec = s % 60;
  if (h > 0) return h + "h" + m + "m" + sec + "s";
  if (m > 0) return m + "m" + sec + "s";
  return sec + "s";
}

// ---- 状态 ----

var ST = { started: false, tick: 0 };

function onlineCount() {
  var list = sfs.players();
  return (list && list.length) ? list.length : 0;
}

function worldTime() {
  var t = sfs.getWorldTime();
  return (typeof t === "number" && t >= 0) ? t : -1;
}

// ---- 命令 ----

function cmdInfo() {
  var n = onlineCount();
  var wt = worldTime();
  var info = sfs.serverInfo();

  LOG("--- server info ---");
  LOG("  tick        : " + ST.tick);
  LOG("  online      : " + n);
  LOG("  world time  : " + (wt >= 0 ? fmtDuration(wt) : "n/a"));
  LOG("  serverInfo  : " + (typeof info === "string" ? info : "rc=" + info));
  LOG("--- end ---");

  SAY("在线 " + n + " 人 | 世界时间 " + (wt >= 0 ? fmtDuration(wt) : "未知"));
}

function cmdOnline() {
  var list = sfs.players() || [];
  LOG("--- online players (" + list.length + ") ---");
  for (var i = 0; i < list.length; i++) {
    var p = list[i] || {};
    var nm = p.name ? String(p.name) : "#" + p.playerId;
    LOG("  [" + i + "] id=" + p.playerId + " name=" + ascii(nm, 20) +
        " rocket=" + (typeof p.controlledRocket === "number" ? p.controlledRocket : "-"));
  }
  LOG("--- end ---");

  if (list.length === 0) { SAY("当前没有玩家在线"); return; }
  var out = "在线 " + list.length + " 人: ";
  for (var j = 0; j < list.length && j < 8; j++) {
    if (j > 0) out += ", ";
    out += (list[j].name ? String(list[j].name) : "#" + list[j].playerId);
  }
  if (list.length > 8) out += " …";
  SAY(out);
}

function cmdClean(maxParts) {
  var rc = sfs.clearDebris(maxParts);
  if (rc === 0) {
    LOG("clearDebris(" + maxParts + ") ok");
    SAY("已清理残骸（" + maxParts + " 部件以下）");
  } else {
    LOG("clearDebris(" + maxParts + ") failed rc=" + rc);
    SAY("清理失败，返回码 " + rc);
  }
}

function cmdTimeScale(x, label) {
  var rc = sfs.setTimeScale(x);
  if (rc === 0) {
    LOG("setTimeScale(" + x + ") ok");
    SAY("时间流速已设为 " + label);
  } else {
    LOG("setTimeScale(" + x + ") failed rc=" + rc);
    SAY("设置失败，返回码 " + rc);
  }
}

function cmdBoot() {
  var list = sfs.players() || [];
  if (list.length === 0) {
    SAY("没有玩家可踢");
    LOG("aboot: no players online");
    return;
  }
  var target = list[0];
  var rc = sfs.kick(target.playerId, "admin boot");
  var nm = target.name ? String(target.name) : "#" + target.playerId;
  if (rc === 0) {
    LOG("aboot: kicked id=" + target.playerId);
    SAY("已踢出 " + nm);
  } else {
    LOG("aboot: kick failed rc=" + rc);
    SAY("踢出失败，返回码 " + rc);
  }
}

function cmdHelp() {
  LOG("--- plugin_admin commands ---");
  LOG("  ainfo      server overview");
  LOG("  aonline    list online players");
  LOG("  aclean     clear debris (<=6 parts)");
  LOG("  acleanbig  clear debris (<=20 parts)");
  LOG("  aslow      time scale 0.5");
  LOG("  anormal    time scale 1.0");
  LOG("  afast      time scale 2.0");
  LOG("  aturbo     time scale 5.0");
  LOG("  aboot      kick first online player");
  LOG("  ahelp      this list");
  LOG("--- end ---");
  SAY("管理命令：ainfo aonline aclean acleanbig aslow anormal afast aturbo ahelp");
}

function onEvent(event) {
  if (!event || !event.kind) return;

  if (event.kind === "worldSnapshot") {
    if (!ST.started) {
      ST.started = true;
      LOG("plugin_admin loaded; type ahelp");
    }
    ST.tick = event.tick || 0;
    return;
  }

  if (event.kind !== "command") return;

  var cmd = String(event.command || "");
  if (cmd === "ainfo") cmdInfo();
  else if (cmd === "aonline") cmdOnline();
  else if (cmd === "aclean") cmdClean(6);
  else if (cmd === "acleanbig") cmdClean(20);
  else if (cmd === "aslow") cmdTimeScale(0.5, "0.5x");
  else if (cmd === "anormal") cmdTimeScale(1.0, "1x");
  else if (cmd === "afast") cmdTimeScale(2.0, "2x");
  else if (cmd === "aturbo") cmdTimeScale(5.0, "5x");
  else if (cmd === "aboot") cmdBoot();
  else if (cmd === "ahelp") cmdHelp();
}

globalThis.onEvent = onEvent;
onEvent;
