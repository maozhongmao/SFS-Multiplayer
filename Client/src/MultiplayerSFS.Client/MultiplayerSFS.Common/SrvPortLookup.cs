// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Concurrent;
using System.Linq;
using System.Net;
using System.Threading.Tasks;
using DnsClient;
using DnsClient.Protocol;

namespace MultiplayerSFS.Common;

// 玩家只填域名、没填端口时，用 SRV 记录把端口问出来。
//
// 为什么需要它：服务端默认端口是 9806，但内网穿透的公网端口只能 ≥10000，
// 而 DNS 的 A/CNAME 记录压根不带端口 —— 所以只能靠 SRV（_sfs._tcp.<主机>）
// 把"这个域名的服务在哪个端口"告诉客户端。Minecraft 服务器列表用的是同一套机制。
//
// 这里只取端口，不取 SRV 的 Target 主机：玩家填的域名才是权威目标，
// Target 只是给多机分流用的，取端口已经够，少一次解析就少一种失败方式。
// 玩家显式写了端口时本类不会被调用（显式端口永远优先）。
public static class SrvPortLookup
{
	// 传输是 KCP over UDP，所以服务前缀用 _udp（与 DNS 记录 _sfs._udp.<host> 必须一致，
	// 两边任一边改名都会让"免端口"静默失效、回退到默认端口）。
	private const string ServicePrefix = "_sfs._udp.";

	// 只查一次、失败不重试：解析发生在输入框每次改动和点 Join 时，卡太久会拖住界面。
	// 查不到就回 null，调用方回退到默认端口（玩家仍可自己补上端口）。
	private static readonly LookupClientOptions Options = new LookupClientOptions
	{
		Timeout = TimeSpan.FromMilliseconds(1200),
		Retries = 0,
		EnableAuditTrail = false,
	};

	// 缓存 Task 而不是结果：同一个主机名被并发问到时也只发一次 DNS 查询。
	private static readonly ConcurrentDictionary<string, Task<int?>> Cache =
		new ConcurrentDictionary<string, Task<int?>>();

	public static Task<int?> QueryAsync(string host)
	{
		string key = host == null ? string.Empty : host.Trim().TrimEnd('.').ToLowerInvariant();
		// IP 字面量没有 SRV 可查，省掉一次必然失败的查询
		if (key.Length == 0 || IPAddress.TryParse(key, out _)) return Task.FromResult<int?>(null);
		return Cache.GetOrAdd(key, QueryUncachedAsync);
	}

	private static async Task<int?> QueryUncachedAsync(string host)
	{
		try
		{
			var client = new LookupClient(Options);
			var response = await client.QueryAsync(ServicePrefix + host, QueryType.SRV).ConfigureAwait(false);
			if (response == null || response.HasError) return null;

			var records = response.Answers.OfType<SrvRecord>().ToList();
			if (records.Count == 0) return null;

			// 按 RFC 2782 先比优先级（越小越优先）；同优先级本应按权重随机，
			// 但我们一个服务只挂一个目标，取端口即可。
			ushort lowestPriority = records.Min(record => record.Priority);
			return records.Where(record => record.Priority == lowestPriority).Max(record => (int)record.Port);
		}
		catch
		{
			// 运营商 DNS 屏蔽 SRV、超时、格式错误等一律当"没有 SRV"处理
			return null;
		}
	}
}
