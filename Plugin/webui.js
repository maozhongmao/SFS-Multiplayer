#!/usr/bin/env node
// webui.mjs — SFS 服务端 WebUI 单文件版
//
// 它做三件事：
//   1. 从本文件内嵌的插件源码抽取 webui.js → qjsc 编译成 .qbc（除非 --qbc 指定已有字节码）
//   2. spawn 服务端（注入 SFS_PLUGINS / SFS_PLUGIN_PATH），或 --log 模式 tail 已有日志
//   3. 起 HTTP + SSE，把插件吐出的 @W|@E 行推给内嵌网页；网页按钮写回服务端 stdin
//
// 用法：
//   node webui.mjs                                     # 拉起 ./sfs-server-kcp-arm --port 9806
//   node webui.mjs --bin ./sfs-server-kcp-arm --server-args "--port 9806" --port 8080
//   node webui.mjs --log /var/log/sfs/server.out       # 服务端已托管，只 tail（命令按钮不可用）
//   node webui.mjs --qbc webui.qbc                     # 用已有字节码，跳过编译
//   node webui.mjs --qjsc ./qjsc --keep                # 指定编译器；保留临时文件
//
// 沙箱约束提醒：插件无网络/文件/进程，所以必须靠 stdout 管道桥接；这是唯一不改进服务端内核的路子。

import { spawn } from "node:child_process";
import http from "node:http";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

/* ============================ 0. 参数 ============================ */
const SELF = fileURLToPath(import.meta.url);
const SELF_SRC = fs.readFileSync(SELF, "utf8");

const argv = process.argv.slice(2);
const arg = (k, d) => { const i = argv.indexOf(k); return i >= 0 ? argv[i + 1] : d; };
const has = (k) => argv.includes(k);

const OPT = {
  httpPort: Number(arg("--port", "8080")),
  bin: arg("--bin", "./sfs-server-kcp-arm"),
  serverArgs: arg("--server-args", "--port 9806").split(/\s+/).filter(Boolean),
  logFile: arg("--log", ""),
  qjsc: arg("--qjsc", "qjsc"),
  qbc: arg("--qbc", ""),
  keep: has("--keep"),
};

/* ============================ 1. 内嵌资源提取 ============================ */
function extract(beginMark, endMark) {
  const i = SELF_SRC.indexOf(beginMark);
  const j = SELF_SRC.indexOf(endMark, i + beginMark.length);
  if (i < 0 || j < 0) throw new Error(`内嵌资源标记缺失: ${beginMark}`);
  let start = SELF_SRC.indexOf("\n", i);
  start = start < 0 ? i + beginMark.length : start + 1;
  let stop = SELF_SRC.lastIndexOf("\n", j);
  if (stop < start) stop = j;
  return SELF_SRC.slice(start, stop);
}

const PLUGIN_SRC = extract("@@PLUGIN_BEGIN@@", "@@PLUGIN_END@@");
const HTML = extract("@@HTML_BEGIN@@", "@@HTML_END@@");

/* ============================ 2. 编译插件 ============================ */
let TMP_DIR = "";
let QBC_PATH = OPT.qbc;

function compilePlugin() {
  return new Promise((resolve, reject) => {
    if (QBC_PATH) { console.log(`[webui] 使用已有字节码 ${QBC_PATH}`); return resolve(); }

    TMP_DIR = fs.mkdtempSync(path.join(os.tmpdir(), "sfs-webui-"));
    const srcPath = path.join(TMP_DIR, "webui.js");
    const outPath = path.join(TMP_DIR, "webui.qbc");
    fs.writeFileSync(srcPath, PLUGIN_SRC + "\n", "utf8");

    console.log(`[webui] 编译插件: ${OPT.qjsc} -b -o ${outPath} ${srcPath}`);
    const p = spawn(OPT.qjsc, ["-b", "-o", outPath, srcPath], { stdio: ["ignore", "inherit", "inherit"] });
    p.on("error", (e) => reject(new Error(`无法启动 qjsc（${OPT.qjsc}）: ${e.message}`)));
    p.on("exit", (code) => {
      if (code !== 0) return reject(new Error(`qjsc 退出码 ${code}`));
      QBC_PATH = outPath;
      console.log(`[webui] 字节码就绪 ${outPath}`);
      resolve();
    });
  });
}

/* ============================ 3. 采集服务端输出 ============================ */
const latest = { at: 0, data: null };
const pending = new Map();          // tag -> { total, parts, got }
const clients = new Set();          // SSE 连接
let serverProc = null;

function push(obj) {
  const s = `data: ${JSON.stringify(obj)}\n\n`;
  for (const res of clients) res.write(s);
}

function feed(line) {
  line = line.trimEnd();
  if (!line) return;

  if (line.startsWith("@E|")) {
    const p = line.slice(3).split("|");
    push({ type: "event", kind: p[0], args: p.slice(1), at: Date.now() });
    return;
  }
  if (!line.startsWith("@W|")) return;

  let rest = line.slice(3);
  const i1 = rest.indexOf("|");
  if (i1 < 0) return;
  const tag = rest.slice(0, i1);
  rest = rest.slice(i1 + 1);
  const i2 = rest.indexOf("|");
  if (i2 < 0) return;
  const [idxS, totS] = rest.slice(0, i2).split("/");
  const part = rest.slice(i2 + 1);
  const idx = Number(idxS), total = Number(totS);
  if (!Number.isFinite(idx) || !Number.isFinite(total) || total <= 0) return;

  let rec = pending.get(tag);
  if (!rec || rec.total !== total) { rec = { total, parts: new Array(total), got: 0 }; pending.set(tag, rec); }
  if (rec.parts[idx] == null) { rec.parts[idx] = part; rec.got++; }

  if (rec.got === total) {
    pending.delete(tag);
    if (pending.size > 64) pending.clear();
    try {
      latest.data = JSON.parse(rec.parts.join(""));
      latest.at = Date.now();
      push({ type: "state", ...latest.data, at: latest.at });
    } catch { /* 坏包丢弃 */ }
  }
}

function feedChunk(buf) {
  return buf.toString("utf8").split("\n").forEach(feed);
}

function startCollector() {
  if (OPT.logFile) {
    let pos = 0;
    console.log(`[webui] tail 模式: ${OPT.logFile}（命令下发不可用）`);
    setInterval(() => {
      let st;
      try { st = fs.statSync(OPT.logFile); } catch { return; }
      if (st.size < pos) pos = 0;
      if (st.size === pos) return;
      const fd = fs.openSync(OPT.logFile, "r");
      const buf = Buffer.alloc(st.size - pos);
      fs.readSync(fd, buf, 0, buf.length, pos);
      fs.closeSync(fd);
      pos = st.size;
      feedChunk(buf);
    }, 200);
    return;
  }

  serverProc = spawn(OPT.bin, OPT.serverArgs, {
    stdio: ["pipe", "pipe", "inherit"],
    env: { ...process.env, SFS_PLUGINS: "1", SFS_PLUGIN_PATH: QBC_PATH },
  });
  let buf = "";
  serverProc.stdout.setEncoding("utf8");
  serverProc.stdout.on("data", (c) => {
    buf += c;
    let nl;
    while ((nl = buf.indexOf("\n")) >= 0) { feed(buf.slice(0, nl)); buf = buf.slice(nl + 1); }
  });
  serverProc.on("error", (e) => { console.error(`[webui] 服务端启动失败: ${e.message}`); process.exit(1); });
  serverProc.on("exit", (code) => {
    push({ type: "serverExit", code });
    console.error(`[webui] 服务端退出 code=${code}`);
    shutdown(code ?? 0);
  });
  console.log(`[webui] 已拉起 ${OPT.bin} ${OPT.serverArgs.join(" ")} (plugin=${QBC_PATH})`);
}

/* ============================ 4. 命令下发 ============================ */
const ALLOW = new Set([
  "webui_status", "webui_on", "webui_off", "webui_interval_5", "webui_interval_30",
  "players", "help",
]);

function sendCommand(cmd) {
  if (!ALLOW.has(cmd)) return { ok: false, reason: "command not allowed" };
  if (!serverProc || !serverProc.stdin.writable) return { ok: false, reason: "no stdin (tail mode?)" };
  serverProc.stdin.write(cmd + "\n");
  return { ok: true };
}

/* ============================ 5. HTTP / SSE ============================ */
function json(res, code, obj) {
  res.writeHead(code, { "content-type": "application/json; charset=utf-8" });
  res.end(JSON.stringify(obj));
}

const httpServer = http.createServer((req, res) => {
  const u = new URL(req.url, "http://localhost");

  if (u.pathname === "/events") {
    res.writeHead(200, {
      "content-type": "text/event-stream",
      "cache-control": "no-cache",
      connection: "keep-alive",
      "x-accel-buffering": "no",
    });
    res.write(": connected\n\n");
    if (latest.data) res.write(`data: ${JSON.stringify({ type: "state", ...latest.data, at: latest.at })}\n\n`);
    clients.add(res);
    const hb = setInterval(() => res.write(": hb\n\n"), 15000);
    req.on("close", () => { clearInterval(hb); clients.delete(res); });
    return;
  }

  if (u.pathname === "/state") return json(res, 200, { data: latest.data, at: latest.at });

  if (u.pathname === "/cmd" && req.method === "POST") {
    let body = "";
    req.on("data", (c) => { body += c; if (body.length > 4096) req.destroy(); });
    req.on("end", () => {
      let cmd = "";
      try { cmd = String(JSON.parse(body || "{}").cmd || ""); } catch {}
      const r = sendCommand(cmd);
      json(res, r.ok ? 200 : 400, { ...r, cmd });
    });
    return;
  }

  if (u.pathname === "/" || u.pathname === "/index.html") {
    res.writeHead(200, { "content-type": "text/html; charset=utf-8" });
    res.end(HTML);
    return;
  }

  json(res, 404, { error: "not found" });
});

/* ============================ 6. 生命周期 ============================ */
let shuttingDown = false;
function shutdown(code = 0) {
  if (shuttingDown) return;
  shuttingDown = true;
  try { httpServer.close(); } catch {}
  try { serverProc?.kill("SIGTERM"); } catch {}
  if (TMP_DIR && !OPT.keep) { try { fs.rmSync(TMP_DIR, { recursive: true, force: true }); } catch {} }
  else if (TMP_DIR && OPT.keep) console.log(`[webui] 临时文件保留在 ${TMP_DIR}`);
  process.exit(code);
}
for (const sig of ["SIGINT", "SIGTERM"]) process.on(sig, () => shutdown(0));

/* ============================ 7. main ============================ */
(async () => {
  try {
    await compilePlugin();
    startCollector();
    httpServer.listen(OPT.httpPort, () => {
      console.log(`[webui] 打开 http://localhost:${OPT.httpPort}`);
      if (!OPT.logFile) console.log("[webui] 命令按钮会写入服务端 stdin（控制台命令）");
    });
  } catch (e) {
    console.error(`[webui] 启动失败: ${e.message}`);
    shutdown(1);
  }
})();

/*==================================================================
  内嵌插件源码（运行时被抽取 → qjsc 编译）
  能力位：log (1<<3)。不发聊天，避免刷玩家。
==================================================================*/
/*@@PLUGIN_BEGIN@@
// webui.js — 把服务端状态以分片日志行输出：@W|<tick>|<i>/<n>|<json>，事件：@E|kind|args...
// sfs.log 上限 255 字节且非 ASCII 变 '?'，所以这里先转义再分片。
var CFG = { enabled: true, intervalSec: 5, chunkSize: 190 };
var st = { tick: 0, players: [], rockets: [], lastSentTick: -1 };

function safeStr(v, max) {
  var s = String(v == null ? "" : v), out = "";
  for (var i = 0; i < s.length && out.length < max; i++) {
    var c = s.charCodeAt(i);
    out += (c >= 32 && c <= 126) ? s.charAt(i) : "?";
  }
  return out;
}

function emitSnapshot() {
  var wt = sfs.getWorldTime();
  var info = sfs.serverInfo();
  var pl = [], rl = [];
  for (var i = 0; i < st.players.length && i < 24; i++) {
    var p = st.players[i] || {};
    pl.push({ id: p.playerId, name: safeStr(p.name || "", 24), rocket: p.controlledRocket });
  }
  for (var j = 0; j < st.rockets.length && j < 24; j++) {
    var r = st.rockets[j] || {};
    rl.push({ id: (r.rocketId !== undefined ? r.rocketId : r.id), owner: (r.ownerPlayer !== undefined ? r.ownerPlayer : r.owner) });
  }
  var json = JSON.stringify({
    t: st.tick,
    wt: (typeof wt === "number" && wt >= 0) ? Math.round(wt) : -1,
    info: safeStr(info, 80),
    np: st.players.length, pl: pl,
    nr: st.rockets.length, rl: rl
  });
  var tag = String(st.tick);
  var n = Math.max(1, Math.ceil(json.length / CFG.chunkSize));
  for (var k = 0; k < n; k++) {
    sfs.log("@W|" + tag + "|" + k + "/" + n + "|" + json.substring(k * CFG.chunkSize, (k + 1) * CFG.chunkSize));
  }
}

function onEvent(event) {
  if (!event || !event.kind) return;

  if (event.kind === "worldSnapshot") {
    st.tick = event.tick || 0;
    st.players = event.players || [];
    st.rockets = event.rockets || [];
    if (CFG.enabled && st.tick - st.lastSentTick >= CFG.intervalSec) {
      st.lastSentTick = st.tick;
      emitSnapshot();
    }
    return;
  }
  if (event.kind === "playerJoined" && event.player) { sfs.log("@E|join|" + safeStr(event.player.playerId, 32)); return; }
  if (event.kind === "playerLeft") { sfs.log("@E|left|" + safeStr(event.playerId || "?", 32)); return; }
  if (event.kind === "chatMessage") { sfs.log("@E|chat|" + safeStr(event.playerId || "?", 20) + "|" + safeStr(event.text || "", 60)); return; }
  if (event.kind !== "command") return;

  var cmd = String(event.command || "");
  if (cmd === "webui_status") emitSnapshot();
  else if (cmd === "webui_on") { CFG.enabled = true; sfs.log("@E|cfg|on"); }
  else if (cmd === "webui_off") { CFG.enabled = false; sfs.log("@E|cfg|off"); }
  else if (cmd === "webui_interval_5") { CFG.intervalSec = 5; sfs.log("@E|cfg|int=5"); }
  else if (cmd === "webui_interval_30") { CFG.intervalSec = 30; sfs.log("@E|cfg|int=30"); }
}

globalThis.onEvent = onEvent;
onEvent;
@@PLUGIN_END@@*/

/*==================================================================
  内嵌网页（运行时被抽取，GET / 直接返回）
==================================================================*/
/*@@HTML_BEGIN@@
<!doctype html>
<html lang="zh"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SFS WebUI</title>
<style>
  :root { color-scheme: dark; }
  body { margin:0; font:14px/1.5 system-ui,sans-serif; background:#0f1115; color:#dfe3ea; }
  header { padding:12px 16px; background:#161a22; display:flex; gap:12px; align-items:center; border-bottom:1px solid #232a36; }
  .dot { width:8px; height:8px; border-radius:50%; background:#e5484d; transition:background .2s; }
  .dot.on { background:#30a46c; }
  main { padding:16px; display:grid; gap:16px; grid-template-columns:repeat(auto-fit,minmax(320px,1fr)); }
  section { background:#161a22; border:1px solid #232a36; border-radius:10px; padding:12px; }
  h2 { margin:0 0 8px; font-size:13px; color:#8b94a7; letter-spacing:.08em; text-transform:uppercase; }
  table { width:100%; border-collapse:collapse; } td,th { text-align:left; padding:4px 6px; border-bottom:1px solid #1e2430; }
  th { color:#8b94a7; font-weight:500; } .k { color:#8b94a7; } .v { font-variant-numeric:tabular-nums; }
  button { background:#222a36; color:#dfe3ea; border:1px solid #2f3a4a; border-radius:6px; padding:5px 10px; cursor:pointer; margin:2px; }
  button:hover { background:#2c3644; }
  #log { height:180px; overflow:auto; font:12px/1.5 ui-monospace,monospace; color:#9aa4b8; white-space:pre-wrap; }
  #wait { display:none; color:#e5a14d; margin-top:8px; }
</style></head>
<body>
<header><span id="dot" class="dot"></span><b>SFS WebUI</b><span id="meta" class="k">连接中…</span></header>
<main>
  <section><h2>服务器</h2><table>
    <tr><td class="k">tick</td><td id="tick" class="v">-</td></tr>
    <tr><td class="k">世界时间</td><td id="wt" class="v">-</td></tr>
    <tr><td class="k">在线</td><td id="np" class="v">-</td></tr>
    <tr><td class="k">火箭</td><td id="nr" class="v">-</td></tr>
    <tr><td class="k">serverInfo</td><td id="info" class="v">-</td></tr>
  </table><div id="wait">已连接，但还没收到插件数据 —— 检查插件是否加载、worldSnapshot 是否在走。</div></section>

  <section><h2>玩家</h2><table><thead><tr><th>ID</th><th>名字</th><th>火箭</th></tr></thead><tbody id="players"></tbody></table></section>

  <section><h2>火箭</h2><table><thead><tr><th>ID</th><th>Owner</th></tr></thead><tbody id="rockets"></tbody></table></section>

  <section><h2>控制台</h2>
    <div>
      <button data-cmd="webui_status">刷新</button>
      <button data-cmd="webui_interval_5">5s</button>
      <button data-cmd="webui_interval_30">30s</button>
      <button data-cmd="webui_on">开</button>
      <button data-cmd="webui_off">关</button>
      <button data-cmd="players">players</button>
      <button data-cmd="help">help</button>
    </div>
    <div id="log"></div>
  </section>
</main>
<script>
const $ = (id) => document.getElementById(id);
const log = (s) => { const d = $("log"); d.textContent += s + "\n"; d.scrollTop = d.scrollHeight; };
const waitTimer = setTimeout(() => { if (!window.__gotState) $("wait").style.display = "block"; }, 8000);

function render(s) {
  window.__gotState = true; $("wait").style.display = "none";
  $("tick").textContent = s.t;
  $("wt").textContent   = s.wt >= 0 ? s.wt + " s" : "n/a";
  $("np").textContent   = s.np;
  $("nr").textContent   = s.nr;
  $("info").textContent = s.info || "-";
  $("meta").textContent = "更新于 " + new Date(s.at || Date.now()).toLocaleTimeString();

  $("players").innerHTML = (s.pl || []).map(p =>
    `<tr><td class="v">${p.id}</td><td>${p.name || "?"}</td><td class="v">${p.rocket ?? "-"}</td></tr>`).join("")
    || `<tr><td colspan="3" class="k">无</td></tr>`;

  $("rockets").innerHTML = (s.rl || []).map(r =>
    `<tr><td class="v">${r.id}</td><td class="v">${r.owner ?? "-"}</td></tr>`).join("")
    || `<tr><td colspan="2" class="k">无</td></tr>`;
}

function connect() {
  const es = new EventSource("/events");
  es.onopen  = () => { $("dot").classList.add("on"); log("[sse] connected"); };
  es.onerror = () => { $("dot").classList.remove("on"); log("[sse] disconnected"); };
  es.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.type === "state") render(m);
    else if (m.type === "event") log(`[${m.kind}] ${m.args.join(" ")}`);
    else if (m.type === "serverExit") log("[server] exited " + m.code);
  };
}

document.querySelectorAll("button[data-cmd]").forEach(b => b.onclick = async () => {
  const cmd = b.dataset.cmd;
  try {
    const r = await fetch("/cmd", { method: "POST", headers: { "content-type": "application/json" }, body: JSON.stringify({ cmd }) });
    const j = await r.json();
    log(`> ${cmd} ${j.ok ? "ok" : "rejected: " + (j.reason || "")}`);
  } catch (e) { log(`> ${cmd} 请求失败: ${e.message}`); }
});

connect();
</script>
</body></html>
@@HTML_END@@*/