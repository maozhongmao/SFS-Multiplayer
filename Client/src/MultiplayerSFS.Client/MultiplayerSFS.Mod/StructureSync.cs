// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System.Collections.Generic;
using MultiplayerSFS.Common;
using SFS.Parts;
using UnityEngine;

namespace MultiplayerSFS.Mod;

/// <summary>
/// 结构对账：把"活体零件 ID 集合"压成一个哈希，随状态流每秒带过去。
///
/// 为什么这么做（而不是继续找是谁漏了事件）：结构分叉的修复动作是整枚重建，
/// 成本高（75 部件 Instantiate + 插值器重置 = 抖），所以历史上只能靠"撞掉零件"偶发触发。
/// 有了哈希对账：观测方一旦发现不一致就主动要一次权威快照 —— 无论哪个路径漏了什么，
/// 最多 1~2 秒收敛，而且只在真不一致时才重建（不会周期性弹跳）。
/// </summary>
public static class StructureSync
{
    private static readonly Dictionary<int, uint> cachedHash = new Dictionary<int, uint>();
    private static readonly Dictionary<int, int> cachedCount = new Dictionary<int, int>();
    private static readonly Dictionary<int, float> cachedTime = new Dictionary<int, float>();
    private static readonly Dictionary<int, float> nextRequestTime = new Dictionary<int, float>();

    /// <summary>重建请求限流：每枚火箭 5 秒最多一次（服务端本身也有 1 次/秒 的节流）。</summary>
    public const float RequestIntervalSeconds = 5f;

    public static uint Compute(Dictionary<int, Part> parts)
    {
        if (parts == null || parts.Count == 0) return 0u;
        List<int> ids = new List<int>(parts.Count);
        foreach (KeyValuePair<int, Part> kv in parts)
        {
            if (kv.Value != null) ids.Add(kv.Key);   // 只算活体：已摧毁条目留成 null，不能算进去
        }
        if (ids.Count == 0) return 0u;
        ids.Sort();
        uint hash = 2166136261u;                     // FNV-1a
        for (int i = 0; i < ids.Count; i++)
        {
            hash ^= (uint)ids[i];
            hash *= 16777619u;
        }
        return hash == 0u ? 1u : hash;                // 0 保留给"旧版本/未知"
    }

    /// <summary>权威模型（线格式）：销毁即从字典移除，所以键全都是有效的，直接哈希键集合。</summary>
    public static uint Compute(Dictionary<int, PartState> parts)
    {
        if (parts == null || parts.Count == 0) return 0u;
        List<int> ids = new List<int>(parts.Keys);
        ids.Sort();
        uint hash = 2166136261u;
        for (int i = 0; i < ids.Count; i++)
        {
            hash ^= (uint)ids[i];
            hash *= 16777619u;
        }
        return hash == 0u ? 1u : hash;
    }

    /// <summary>权威方发送用（权威模型）：带 0.5 秒缓存。</summary>
    public static uint HashFor(int rocketId, Dictionary<int, PartState> parts)
    {
        if (parts == null) return 0u;
        return HashFor(rocketId, parts.Count, () => Compute(parts));
    }

    /// <summary>权威方发送用：带 0.5 秒缓存（零件很少变，不必每包重算）。</summary>
    public static uint HashFor(int rocketId, Dictionary<int, Part> parts)
    {
        if (parts == null) return 0u;
        int live = 0;
        foreach (KeyValuePair<int, Part> kv in parts)
        {
            if (kv.Value != null) live++;
        }
        float now = Time.unscaledTime;
        if (cachedHash.TryGetValue(rocketId, out uint cached)
            && cachedCount.TryGetValue(rocketId, out int count)
            && count == live
            && cachedTime.TryGetValue(rocketId, out float at)
            && now - at < 0.5f)
        {
            return cached;
        }
        return HashFor(rocketId, live, () => Compute(parts));
    }

    private static uint HashFor(int rocketId, int liveCount, System.Func<uint> compute)
    {
        float now = Time.unscaledTime;
        if (cachedHash.TryGetValue(rocketId, out uint cached)
            && cachedCount.TryGetValue(rocketId, out int count)
            && count == liveCount
            && cachedTime.TryGetValue(rocketId, out float at)
            && now - at < 0.5f)
        {
            return cached;
        }
        uint value = compute();
        cachedHash[rocketId] = value;
        cachedCount[rocketId] = liveCount;
        cachedTime[rocketId] = now;
        return value;
    }

    /// <summary>
    /// 结构哈希 —— 只基于【线格式里的零件编号】（权威模型 world.rockets[id].parts 的键）。
    ///
    /// 之前的教训：我用 new PartSave(part) 从活体零件重算 id，结果两端各自稳定、但**永远不相等**
    /// （日志：local=450397565 remote=1019789884 反复出现）-> 对账永远报不一致、永远修不好。
    /// 用活体零件重算身份本质上不收敛；只有"已经传输过的那套编号"才两端天然一致。
    /// </summary>
    public static uint HashForWire(int rocketId)
    {
        if (ClientManager.world == null) return 0u;
        if (!ClientManager.world.rockets.TryGetValue(rocketId, out RocketState state)) return 0u;
        if (state == null || state.parts == null || state.parts.Count == 0) return 0u;
        List<int> ids = new List<int>(state.parts.Keys);
        ids.Sort();
        uint hash = 2166136261u;
        for (int i = 0; i < ids.Count; i++)
        {
            hash ^= (uint)ids[i];
            hash *= 16777619u;
        }
        return hash == 0u ? 1u : hash;
    }

    /// <summary>旧签名保留（内部转调线格式版本）。</summary>
    public static uint HashForRocket(SFS.World.Rocket rocket)
    {
        if (rocket == null || rocket.partHolder == null) return 0u;
        List<int> ids = new List<int>();
        foreach (Part part in rocket.partHolder.partsSet)
        {
            if (part != null) ids.Add(IDExtensions.DeterministicPartID(new PartSave(part)));
        }
        if (ids.Count == 0) return 0u;
        ids.Sort();
        uint hash = 2166136261u;
        for (int i = 0; i < ids.Count; i++)
        {
            hash ^= (uint)ids[i];
            hash *= 16777619u;
        }
        return hash == 0u ? 1u : hash;
    }

    /// <summary>观测方收到状态包时调用：不一致就请求一次权威快照（限流）。返回 true 表示已请求。</summary>
    public static bool CheckRemote(int rocketId, uint remoteHash, Dictionary<int, Part> localParts)
    {
        if (remoteHash == 0u) return false;           // 对方旧版本：不做判断
        uint local = HashFor(rocketId, localParts);
        if (local == remoteHash) return false;

        float now = Time.unscaledTime;
        if (nextRequestTime.TryGetValue(rocketId, out float next) && now < next) return false;
        nextRequestTime[rocketId] = now + RequestIntervalSeconds;

        uint localId = local;
        Debug.LogWarning("[SFS-MP][structure] hash mismatch on rocket " + rocketId
            + ": local=" + localId + " remote=" + remoteHash + "; requesting authoritative snapshot");
        ClientManager.RequestRocketSnapshot(rocketId);
        return true;
    }
}
