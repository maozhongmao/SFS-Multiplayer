#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SFS Pro (FLDPXIce) 新版 APK 自动体检脚本
=========================================
用法:
    python scan_apk.py <APK路径> [--save-baseline] [--no-net]

功能:
  1. 包体基础信息 + sha256；与服务器 release_apk_size 清单比对（--no-net 跳过）
  2. classes2.dex（授权层）: 与基线对比类清单/关键字符串；检测"心跳红旗"(定时器等)
  3. libstub.so（授权native层）: 哈希/导入表红旗(socket/pthread_create/nanosleep)
     与基线对比字符串; 自动解码 XOR(0x5A) 混淆的 /api 路径数组
  4. 输出 JSON 到 tools/scans/ 目录，可与 --save-baseline 保存的基线对比

判定要点（看完输出就能答）:
  - "心跳红旗" 有 → 疑似新增联网/定时检测
  - libstub 导入表出现 socket/pthread_create/nanosleep → 重点人工分析
  - 体积不在服务器清单 → 不是卖家原包(或已被改)
"""
import sys, os, io, re, json, zipfile, hashlib, struct, time
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
SCANS = os.path.join(HERE, "scans")
BASE_DIR = os.path.join(HERE, "baselines")
SERVER = "http://8.136.134.174:8000"
os.makedirs(SCANS, exist_ok=True)
os.makedirs(BASE_DIR, exist_ok=True)

RED_IMPORTS = {"pthread_create", "nanosleep", "usleep", "sleep", "clock_nanosleep",
               "socket", "connect", "getaddrinfo", "select", "poll", "alarm"}
TIMER_PATTERNS = [r"postDelayed", r"scheduleAtFixedRate", r"schedule\(", r"new Timer",
                  r"TimerTask", r"Thread\.sleep", r"setRepeating", r"Handler\(.*\)\.post",
                  r"countDown", r"倒计时", r"心跳", r"heartbeat"]
LIC_KEYS = ["licenseCached", "s_licensedCached", "licenseFetch", "licenseCheck", "licenseBind",
            "get_key", "get_cek", "bind/submit", "offline/check", "config/salt",
            "SFSPRO-TRIAL", "IntegrityMeta", "libsIntegrityOk", "apkSize", "versionGateDecision",
            "needFullIntegrityCheck", "trialExpiredNow", "BOUND_OTHER", "INTEGRITY_BAD",
            "ALLOW_INSECURE_VERIFY", "detectHook", "ED25519", "libpatch.so.sig"]


def log(msg):
    print(msg, flush=True)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def extract_strings(data, minlen=4):
    out = set()
    for m in re.finditer(rb"[\x20-\x7E]{%d,}" % minlen, data):
        out.add(m.group().decode("ascii", "replace"))
    return out


def decode_xor_arrays(data, key=0x5A):
    """扫描 int32-LE 形式的 XOR 混淆数组，解出 /api 路径"""
    pat = bytes([ord("/") ^ key, 0, 0, 0, ord("a") ^ key, 0, 0, 0, ord("p") ^ key, 0, 0, 0])
    paths = set()
    start = 0
    while True:
        idx = data.find(pat, start)
        if idx < 0:
            break
        start = idx + 4
        # 从 idx 开始尽量向后读
        vals = []
        p = idx
        while p + 4 <= len(data):
            v = struct.unpack_from("<I", data, p)[0]
            c = v ^ key
            if 0x20 <= c <= 0x7E:
                vals.append(c); p += 4
            else:
                break
        if len(vals) >= 8:
            txt = "".join(chr(c) for c in vals)
            # 相邻数组会连读，按 /api/ 逐段切分
            for chunk in txt.split("/api/")[1:]:
                tail = re.split(r"[^A-Za-z0-9_\-/?=&.:]", chunk)[0]
                if tail:
                    paths.add("/api/" + tail)
    return sorted(paths)


def elf_imports(path):
    try:
        from elftools.elf.elffile import ELFFile
    except ImportError:
        return None
    with open(path, "rb") as f:
        elf = ELFFile(f)
        dynsym = elf.get_section_by_name(".dynsym")
        if dynsym is None:
            return []
        return sorted({s.name for s in dynsym.iter_symbols()
                       if s["st_shndx"] == "SHN_UNDEF" and s.name})


def fetch_server_config(no_net=False):
    if no_net:
        return None
    import urllib.request
    try:
        req = urllib.request.Request(SERVER + "/api/config/app",
                                     headers={"User-Agent": "SFS-Android/1.0"})
        with urllib.request.urlopen(req, timeout=10) as r:
            return json.loads(r.read().decode("utf-8"))
    except Exception as e:
        return {"_error": repr(e)}


def scan(apk_path, save_baseline=False, no_net=False):
    name = os.path.splitext(os.path.basename(apk_path))[0]
    result = {"apk": apk_path, "name": name, "time": datetime.now().isoformat()}
    size = os.path.getsize(apk_path)
    result["size"] = size
    result["sha256"] = sha256_file(apk_path)
    log(f"[1] {name}: {size} bytes\n    sha256={result['sha256']}")

    # 服务器比对
    cfg = fetch_server_config(no_net)
    result["server"] = None
    if cfg is not None:
        if "_error" in cfg:
            log(f"[2] 服务器: 无法连接 ({cfg['_error']})")
        else:
            d = cfg.get("data", {})
            sizes = [int(x) for x in str(d.get("release_apk_size", "")).split(",") if x.strip().isdigit()]
            in_list = size in sizes
            result["server"] = {"client_version": d.get("client_version"),
                                "min_build": d.get("min_build"),
                                "in_release_list": in_list,
                                "release_count": len(sizes)}
            log(f"[2] 服务器: client_version={d.get('client_version')} min_build={d.get('min_build')}")
            log(f"    包体是否在官方销售清单({len(sizes)}份): {'✅ 是' if in_list else '❌ 否 (可能是改包!)'}")

    z = zipfile.ZipFile(apk_path)
    names = z.namelist()

    # ---- classes2.dex ----
    if "classes2.dex" in names:
        c2 = z.read("classes2.dex")
        result["classes2_sha256"] = hashlib.sha256(c2).hexdigest()
        result["classes2_size"] = len(c2)
        ss = extract_strings(c2)
        result["has_integrity_meta"] = "com/sfsmod/IntegrityMeta" in ss or "IntegrityMeta" in ss
        lic_hits = sorted(s for s in ss if any(k in s for k in LIC_KEYS))
        result["license_strings"] = lic_hits[:80]
        timer_hits = []
        for p in TIMER_PATTERNS:
            for s in ss:
                if re.search(p, s):
                    timer_hits.append(s)
        timer_hits = sorted(set(timer_hits))[:60]
        result["timer_hits"] = timer_hits
        log(f"[3] classes2.dex {len(c2)} B | 授权相关字符串 {len(lic_hits)} 条 | 定时器线索 {len(timer_hits)} 条")
        for t in timer_hits[:15]:
            log(f"    ⏱ {t[:90]}")
        for s in lic_hits[:15]:
            log(f"    🔑 {s[:90]}")

    # ---- libstub.so ----
    stub = None
    for n in names:
        if n.endswith("libstub.so"):
            stub = z.read(n); stub_name = n
    result["libstub"] = {}
    if stub:
        result["libstub"]["sha256"] = hashlib.sha256(stub).hexdigest()
        result["libstub"]["size"] = len(stub)
        tmp = os.path.join(SCANS, "_tmp_libstub.so")
        open(tmp, "wb").write(stub)
        imps = elf_imports(tmp)
        if imps is not None:
            reds = sorted(set(imps) & RED_IMPORTS)
            result["libstub"]["red_imports"] = reds
            log(f"[4] libstub.so {len(stub)} B | 红旗导入: {reds if reds else '无 ✅'}")
        paths = decode_xor_arrays(stub)
        result["libstub"]["api_paths"] = paths
        for p in paths:
            log(f"    🌐 {p}")

    # ---- 全部 .so 哈希速览 ----
    so_hashes = {}
    for n in names:
        if n.endswith(".so"):
            so_hashes[n.split("/")[-1]] = hashlib.sha256(z.read(n)).hexdigest()[:16]
    result["so_hashes"] = so_hashes

    # ---- 与基线对比 ----
    base_file = os.path.join(BASE_DIR, "baseline_1004.json")
    if save_baseline:
        out = os.path.join(BASE_DIR, f"baseline_{name}.json")
        json.dump(result, open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
        log(f"[5] 基线已保存: {out}")
    elif os.path.exists(base_file):
        base = json.load(open(base_file, encoding="utf-8"))
        log(f"[5] 与基线({base.get('name')})对比:")
        if result.get("classes2_sha256") != base.get("classes2_sha256"):
            log("    classes2.dex 有变化")
            new_c = sorted(set(result.get("license_strings", [])) - set(base.get("license_strings", [])))
            for s in new_c[:20]:
                log(f"      +授权字符串 {s[:80]}")
            new_t = sorted(set(result.get("timer_hits", [])) - set(base.get("timer_hits", [])))
            for s in new_t[:20]:
                log(f"      +定时器线索 {s[:80]} ⚠️")
        else:
            log("    classes2.dex 无变化")
        b_stub = base.get("libstub", {})
        if result.get("libstub", {}).get("sha256") != b_stub.get("sha256"):
            log("    libstub.so 有变化 ⚠️ (重点看红旗导入)")
            new_p = sorted(set(result.get("libstub", {}).get("api_paths", [])) - set(b_stub.get("api_paths", [])))
            for s in new_p:
                log(f"      +新API {s}")
        else:
            log("    libstub.so 无变化")

    out = os.path.join(SCANS, f"scan_{name}.json")
    json.dump(result, open(out, "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    log(f"\n[✓] 详细结果: {out}")
    return result


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = set(a for a in sys.argv[1:] if a.startswith("--"))
    if not args:
        print(__doc__)
        sys.exit(1)
    scan(args[0], save_baseline="--save-baseline" in flags, no_net="--no-net" in flags)
