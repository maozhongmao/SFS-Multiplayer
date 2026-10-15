// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.Text;
using MultiplayerSFS.Common;
using UnityEngine;

namespace MultiplayerSFS.Mod;

/// <summary>
/// 超级调试模式：把协议层看穿。
///
/// 设计原则（吃过教训）：
///  1) **默认聚合**：每秒一行汇总，而不是逐包打印 —— 33ms x 17 枚火箭状态流每秒几百包，逐包必然刷屏且拖慢游戏。
///     需要逐包时在 F8 填过滤器（包类型名或火箭 id），只对命中的包输出明细。
///  2) **两个层级**：帧层（TcpFrameKind，来自 Common 的 TraceSink）与包层（PacketType，收/发各一）。
///  3) 关掉时零开销：只在收发热路径上多一次 bool 判断。
/// </summary>
public static class DebugTrace
{
    public const string PrefsKey = "multiplayersfs.debug.trace";
    public const string FilterPrefsKey = "multiplayersfs.debug.filter";

    private static bool enabled;
    private static bool loaded;
    private static string filter = string.Empty;
    private static bool filterLoaded;

    public static bool Enabled
    {
        get
        {
            if (!loaded)
            {
                loaded = true;
                // 诊断构建默认打开（每秒一行，成本可忽略）；F8 可随时关掉。
try { enabled = PlayerPrefs.GetInt(PrefsKey, 1) != 0; } catch { enabled = true; }
            }
            return enabled;
        }
        set
        {
            loaded = true;
            enabled = value;
            try { PlayerPrefs.SetInt(PrefsKey, value ? 1 : 0); } catch { }
            if (value) Note("trace enabled");
        }
    }

    /// <summary>逐包明细的过滤器：空 = 只出聚合；非空则只输出包含该子串的行（包类型名 / 火箭 id / 帧类型）。</summary>
    public static string Filter
    {
        get
        {
            if (!filterLoaded)
            {
                filterLoaded = true;
                try { filter = PlayerPrefs.GetString(FilterPrefsKey, string.Empty) ?? string.Empty; } catch { filter = string.Empty; }
            }
            return filter;
        }
        set
        {
            filterLoaded = true;
            filter = value ?? string.Empty;
            try { PlayerPrefs.SetString(FilterPrefsKey, filter); } catch { }
        }
    }

    private static readonly Dictionary<string, int> inbound = new Dictionary<string, int>();
    private static readonly Dictionary<string, int> outbound = new Dictionary<string, int>();
    private static readonly Dictionary<string, int> frames = new Dictionary<string, int>();
    private static long inboundBytes;
    private static long outboundBytes;
    private static float nextDumpTime;
    private static float nextNoteTime;
    private static string lastSummary = "-";

    public static string LastSummary { get { return lastSummary; } }

    public static void Frame(string kind, int payloadBits, int payloadBytes)
    {
        if (!Enabled) return;
        Add(frames, kind);
        if (Matches(kind)) Debug.Log("[SFS-MP][trace] frame kind=" + kind + " bits=" + payloadBits + " bytes=" + payloadBytes);
    }

    public static void Inbound(string packetType, int bytes)
    {
        if (!Enabled) return;
        Add(inbound, packetType);
        inboundBytes += bytes;
        if (Matches(packetType)) Debug.Log("[SFS-MP][trace] in  " + packetType + " bytes=" + bytes);
    }

    public static void Outbound(string packetType, int bytes)
    {
        if (!Enabled) return;
        Add(outbound, packetType);
        outboundBytes += bytes;
        if (Matches(packetType)) Debug.Log("[SFS-MP][trace] out " + packetType + " bytes=" + bytes);
    }

    /// <summary>状态迁移 / 异常：立即输出，但每秒最多一条，避免异常路径刷屏。</summary>
    public static void Note(string message)
    {
        if (!Enabled) return;
        float now = Time.unscaledTime;
        if (now < nextNoteTime) return;
        nextNoteTime = now + 1f;
        Debug.Log("[SFS-MP][trace] " + message);
    }

    /// <summary>由 LocalManager.Update 每帧驱动：每秒把聚合结果打成一行。</summary>
    public static void Tick()
    {
        if (!Enabled) return;
        float now = Time.unscaledTime;
        if (now < nextDumpTime) return;
        nextDumpTime = now + 1f;
        if (inbound.Count == 0 && outbound.Count == 0 && frames.Count == 0) return;

        StringBuilder sb = new StringBuilder(256);
        sb.Append("[SFS-MP][trace] 1s frames=").Append(Format(frames));
        sb.Append(" in=").Append(Format(inbound)).Append(" (").Append(inboundBytes).Append("B)");
        sb.Append(" out=").Append(Format(outbound)).Append(" (").Append(outboundBytes).Append("B)");
        if (Filter.Length > 0) sb.Append(" filter=").Append(Filter);
        lastSummary = sb.ToString();
        Debug.Log(lastSummary);

        frames.Clear();
        inbound.Clear();
        outbound.Clear();
        inboundBytes = 0;
        outboundBytes = 0;
    }

    private static void Add(Dictionary<string, int> counts, string key)
    {
        if (key == null) key = "?";
        if (counts.TryGetValue(key, out int value)) counts[key] = value + 1;
        else counts[key] = 1;
    }

    private static bool Matches(string text)
    {
        string f = Filter;
        if (f.Length == 0) return false;
        return text != null && text.IndexOf(f, StringComparison.OrdinalIgnoreCase) >= 0;
    }

    private static string Format(Dictionary<string, int> counts)
    {
        if (counts.Count == 0) return "{}";
        StringBuilder sb = new StringBuilder("{");
        bool first = true;
        foreach (KeyValuePair<string, int> kv in counts)
        {
            if (!first) sb.Append(',');
            first = false;
            sb.Append(kv.Key).Append(':').Append(kv.Value);
        }
        return sb.Append('}').ToString();
    }
}
