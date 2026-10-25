// =============================================================================
// 聊天管理
// =============================================================================
// 能力位: log(1<<3) + sendMessage(1<<2) + admin(1<<4)
//
// 功能:
//   1. 刷屏拦截：窗口内发太多条 -> 警告 -> 超出阈值踢出
//   2. 重复内容拦截：连续发同一句 -> 提醒（不占用刷屏计数）
//   3. 敏感词过滤：命中即记录
//   4. 进/退服提示
//   5. 聊天日志：全部落 stdout（英文，管理员可回溯）
//
// 控制台命令:
//   chatstat    输出聊天统计
//   chatreset   清空统计
//
// 注意: 插件只能在收到 chatMessage 事件后做事，无法撤回已发出的消息。
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

function TELL(playerId, text) {
  if (typeof playerId !== "number" || playerId < 0) return -2;
  var msg = clipUtf8(sanitize(text), 240);
  if (msg.length === 0) return -2;
  return sfs.sendMessage(playerId, msg);
}

// ---- 配置 ----

var CFG = {
  windowSec: 10,      // 刷屏统计窗口（秒）
  warnAt: 4,          // 窗口内超过这么多条 -> 警告
  kickAt: 8,          // 窗口内超过这么多条 -> 踢
  repeatWarn: 3,      // 连续相同内容达到这个次数 -> 提醒
  mutedSec: 30        // 警告后的静默期
};

// 敏感词（小写匹配）
var BANNED = ["fuck", "shit", "bitch", "外挂", "代练", "私服"];

// ---- 状态 ----

var ST = {
  users: {},        // playerId -> { hits: [], lastText, repeats, warnedAt, kicks }
  tick: 0,
  totalMsg: 0,
  blocked: 0,
  started: false
};

function lower(s) {
  var out = "";
  for (var i = 0; i < s.length; i++) {
    var c = s.charCodeAt(i);
    out += (c >= 65 && c <= 90) ? String.fromCharCode(c + 32) : s.charAt(i);
  }
  return out;
}

function containsBanned(text) {
  var low = lower(text);
  for (var i = 0; i < BANNED.length; i++) {
    if (low.indexOf(BANNED[i]) >= 0) return BANNED[i];
  }
  return "";
}

function userOf(id) {
  if (!ST.users[id]) {
    ST.users[id] = { hits: [], lastText: "", repeats: 0, warnedAt: -1, kicks: 0 };
  }
  return ST.users[id];
}

function pruneHits(u) {
  var keep = [];
  for (var i = 0; i < u.hits.length; i++) {
    if (ST.tick - u.hits[i] <= CFG.windowSec) keep.push(u.hits[i]);
  }
  u.hits = keep;
}

// ---- 主逻辑 ----

function handleChat(event) {
  var pid = (event.player && typeof event.player.playerId === "number")
    ? event.player.playerId : -1;
  var text = String(event.text || "");
  if (text.length === 0) return;

  ST.totalMsg++;

  // 控制台来源（playerId = -1）不受限制
  if (pid < 0) {
    LOG("chat [console]: " + text);
    return;
  }

  var u = userOf(pid);
  pruneHits(u);

  // 敏感词：命中就不计入刷屏窗口
  var hit = containsBanned(text);
  if (hit.length > 0) {
    ST.blocked++;
    LOG("chat BLOCK id=" + pid + " word=" + ascii(hit, 32));
    TELL(pid, "消息包含违规内容，已被记录");
    return;
  }

  // 重复内容：不占用刷屏计数，否则 repeatWarn 和 warnAt 会互相误伤
  if (text === u.lastText) {
    u.repeats++;
    if (u.repeats === CFG.repeatWarn) {
      TELL(pid, "请不要重复发送相同内容");
      LOG("chat REPEAT id=" + pid + " x" + u.repeats);
    }
    LOG("chat id=" + pid + " (repeat x" + u.repeats + ")");
    return;
  }

  u.lastText = text;
  u.repeats = 1;
  u.hits.push(ST.tick);

  // 刷屏：只统计内容互不相同的消息
  var n = u.hits.length;
  if (n >= CFG.kickAt) {
    u.kicks++;
    ST.blocked++;
    LOG("chat FLOOD KICK id=" + pid + " msgs=" + n + " in " + CFG.windowSec + "s");
    sfs.kick(pid, "flooding");
    u.hits = [];
    return;
  }
  if (n >= CFG.warnAt) {
    if (u.warnedAt < 0 || ST.tick - u.warnedAt > CFG.mutedSec) {
      u.warnedAt = ST.tick;
      TELL(pid, "发言过快，请放缓节奏");
      LOG("chat FLOOD WARN id=" + pid + " msgs=" + n);
    }
  }

  LOG("chat id=" + pid + " len=" + text.length + " text=" + ascii(text, 120));
}

function cmdChatStat() {
  var online = sfs.players() || [];
  var tracked = 0;
  for (var k in ST.users) { if (ST.users.hasOwnProperty(k)) tracked++; }

  LOG("--- chat stats ---");
  LOG("  total messages : " + ST.totalMsg);
  LOG("  blocked        : " + ST.blocked);
  LOG("  tracked users  : " + tracked);
  LOG("  online         : " + online.length);
  LOG("  window         : " + CFG.windowSec + "s warn=" + CFG.warnAt + " kick=" + CFG.kickAt);
  for (var key in ST.users) {
    if (!ST.users.hasOwnProperty(key)) continue;
    var u = ST.users[key];
    if (u.kicks > 0 || u.hits.length > 0) {
      LOG("  #" + key + " recent=" + u.hits.length + " kicks=" + u.kicks +
          " repeats=" + u.repeats);
    }
  }
  LOG("--- end ---");
}

function cmdChatReset() {
  ST.users = {};
  ST.totalMsg = 0;
  ST.blocked = 0;
  LOG("chat stats reset at tick=" + ST.tick);
  SAY("聊天统计已重置");
}

function onEvent(event) {
  if (!event || !event.kind) return;

  if (event.kind === "worldSnapshot") {
    if (!ST.started) {
      ST.started = true;
      LOG("plugin_chat loaded; commands: chatstat, chatreset");
      LOG("flood: " + CFG.warnAt + " msg/" + CFG.windowSec + "s -> warn, " +
          CFG.kickAt + " -> kick");
    }
    ST.tick = event.tick || 0;
    return;
  }

  if (event.kind === "chatMessage") { handleChat(event); return; }

  if (event.kind === "playerJoined" && event.player) {
    var pid = event.player.playerId;
    var nm = event.player.name ? String(event.player.name) : "#" + pid;
    SAY(nm + " 加入了服务器");
    LOG("join id=" + pid);
    return;
  }

  if (event.kind === "playerLeft" && event.player) {
    LOG("left id=" + event.player.playerId);
    delete ST.users[event.player.playerId];
    return;
  }

  if (event.kind === "command") {
    var cmd = String(event.command || "");
    if (cmd === "chatstat") cmdChatStat();
    else if (cmd === "chatreset") cmdChatReset();
  }
}

globalThis.onEvent = onEvent;
onEvent;
