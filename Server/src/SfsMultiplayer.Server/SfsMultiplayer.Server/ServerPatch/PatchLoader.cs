// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.Loader;

namespace SfsMultiplayer.Server.ServerPatch;

// 补丁加载器：启动时扫描 plugins/ 目录加载 DLL 补丁，并支持 reload 热重载。
// 每个 plugins/*.dll 在独立的 AssemblyLoadContext 中加载，便于热重载时整体卸载，
// 避免旧补丁程序集残留在默认 ALC 造成类型冲突/内存泄漏（参考 MC 插件隔离思想）。
internal sealed class PatchLoader : IServerContext, IDisposable
{
    private readonly TcpMultiplayerServer _server;
    private readonly string _pluginsDir;
    private readonly List<LoadedPatch> _patches = new();
    private readonly object _lock = new();

    public PatchLoader(TcpMultiplayerServer server)
    {
        _server = server ?? throw new ArgumentNullException(nameof(server));
        _pluginsDir = Path.Combine(AppContext.BaseDirectory, "plugins");
    }

    public TcpMultiplayerServer Server => _server;
    public ServerSettings Settings => _server.Settings;
    public void Log(string message) => Console.WriteLine($"[补丁] {message}");
    public void Log(string format, params object[] args) => Console.WriteLine($"[补丁] {format}", args);

    // 启动加载：扫描 plugins 目录所有 DLL，逐个实例化 IServerPatch 并 OnLoad。
    // 仅当实验性功能 experimental_patches 开启时才生效；否则不创建目录、不生成文件、不加载。
	public void LoadAll()
	{
		lock (_lock) LoadAllCore();
	}
	private void LoadAllCore()
    {
        if (!_server.Settings.ExperimentalPatches)
        {
            Log("实验性功能未开启（server.yml 的 experimental_patches: false），跳过 DLL 补丁加载。");
            return;
        }

        Directory.CreateDirectory(_pluginsDir);
        WritePluginsReadme(_pluginsDir);

        var dlls = Directory.GetFiles(_pluginsDir, "*.dll", SearchOption.TopDirectoryOnly)
            .OrderBy(p => p, StringComparer.OrdinalIgnoreCase)
            .ToArray();
        if (dlls.Length == 0)
        {
            Log("未找到补丁（plugins/ 目录为空），服务器以原生功能运行。");
            return;
        }

		foreach (var dll in dlls)
		{
			try
			{
				LoadOne(dll);
			}
			catch (Exception ex)
			{
				Log($"加载失败: {Path.GetFileName(dll)} - {ex.Message}");
				if (Environment.GetEnvironmentVariable("SFS_SERVER_DEBUG") == "1")
					Console.Error.WriteLine(ex);
			}
		}
	        Log($"已加载 {_patches.Count}/{dlls.Length} 个补丁。");
	}

    // 热重载：卸载全部旧补丁（OnUnload + 卸载 ALC），重新扫描加载。
    // 旧补丁的 OnUnload 必须释放自身资源，否则其 ALC 卸载时仍可能残留托管引用。
	public void Reload()
	{
		lock (_lock)
		{
			UnloadAllCore();
			Log("已卸载旧补丁，开始重新扫描 plugins/。");
			LoadAllCore();
		}
	}

    private void LoadOne(string dllPath)
    {
        var alc = new PatchLoadContext(Path.GetFileName(dllPath));
        var assembly = alc.LoadFromAssemblyPath(dllPath);
        // 跨 AssemblyLoadContext 时 typeof(IServerPatch).IsAssignableFrom 会因类型身份不同而失效，
        // 改用接口全名匹配，保证补丁 DLL 在独立 ALC 中也能被识别为实现 IServerPatch。
        const string interfaceFullName = "SfsMultiplayer.Server.ServerPatch.IServerPatch";
        var patchTypes = assembly.GetTypes()
            .Where(t => !t.IsAbstract && t.IsClass &&
                        t.GetInterfaces().Any(i => i.FullName == interfaceFullName))
            .ToArray();
        if (patchTypes.Length == 0)
        {
            Log($"跳过（无 IServerPatch 实现）: {Path.GetFileName(dllPath)}");
            alc.Unload();
            return;
        }

        foreach (var type in patchTypes)
        {
            IServerPatch? patch = null;
            try
            {
                patch = (IServerPatch?)Activator.CreateInstance(type);
                if (patch is null) continue;
                patch.OnLoad(this);
                _patches.Add(new LoadedPatch(patch, alc, dllPath));
                Log($"已加载: {patch.Name} v{patch.Version}（{Path.GetFileName(dllPath)}）");
            }
            catch (Exception ex)
            {
                Log($"实例化/加载失败: {type.FullName} - {ex.Message}");
                // 该类型失败不影响同 DLL 其他类型；若整个 DLL 都没成功则卸载 ALC 由调用方处理。
                if (Environment.GetEnvironmentVariable("SFS_SERVER_DEBUG") == "1")
                    Console.Error.WriteLine(ex);
            }
        }
		if (!_patches.Any(p => p.Source == dllPath))
		{
			alc.Unload(); // 本 DLL 所有类型均加载失败，释放 ALC 避免跨 reload 泄漏
		}
    }

	private void UnloadAll()
	{
		lock (_lock) UnloadAllCore();
	}
	private void UnloadAllCore()
    {
        foreach (var entry in _patches)
        {
            try
            {
                entry.Patch.OnUnload();
            }
            catch (Exception ex)
            {
                Log($"卸载回调异常: {entry.Patch.Name} - {ex.Message}");
            }
            try
            {
                entry.Context.Unload();
                GC.Collect(); GC.WaitForPendingFinalizers(); // 立即回收 collectible ALC，释放 DLL 文件句柄以便覆盖部署
            }
            catch (Exception ex)
            {
                Log($"ALC 卸载异常: {Path.GetFileName(entry.Source)} - {ex.Message}");
            }
        }
        _patches.Clear();
	}

    // 实验性功能开启时，在 plugins/ 目录生成接入说明 README（关闭时不生成，目录也不存在）。
    private static void WritePluginsReadme(string pluginsDir)
    {
        try
        {
            var readme = Path.Combine(pluginsDir, "README.md");
            if (File.Exists(readme)) return; // 已有则不覆盖，避免冲掉用户改动
            File.WriteAllText(readme, PluginsReadmeContent);
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[补丁] 生成 plugins/README.md 失败: {ex.Message}");
        }
    }

    private const string PluginsReadmeContent = @"# 服务器补丁（插件）系统 — DLL 热加载

服务器内置 DLL 补丁加载器，对标同行插件系统。补丁用于在不改服务器源码的前提下扩展服务端行为。

## 使用方式
1. 把编译好的补丁 DLL 放进本目录（plugins/）。
2. 启动服务器会自动加载；运行中输入控制台命令 reload 可热重载（不重启服务器）。
3. 补丁必须实现接口 SfsMultiplayer.Server.ServerPatch.IServerPatch：

```csharp
public interface IServerPatch
{
    string Name { get; }
    string Version { get; }
    void OnLoad(IServerContext context);   // 加载时调用，拿到服务器上下文
    void OnUnload();                        // 热重载/退出前调用，释放资源
}

public interface IServerContext
{
    TcpMultiplayerServer Server { get; }   // 读在线玩家、世界时间，向客户端广播
    ServerSettings Settings { get; }        // 只读配置
    void Log(string message);               // 补丁日志（前缀 [补丁]）
}
```

## 隔离与依赖
- 每个补丁 DLL 在独立的 AssemblyLoadContext 中加载，热重载时整体卸载。
- 接口/协议所在的服务端程序集（SfsMultiplayer.Server、SfsMultiplayer.Protocol）为共享程序集，
  补丁 DLL 只需引用它们（Private=false），不要复制进 plugins/。
- 补丁自身携带的其他依赖 DLL 放进 plugins/ 即可被解析。

## 示例
参考源码 src/SamplePatch/（编译为 SamplePatch.dll 丢进 plugins/ 即可见加载日志与心跳）。

注意：补丁 OnUnload 必须释放自己持有的定时器/回调/资源，否则 ALC 卸载后仍可能残留托管引用。
";

    public void Dispose()
    {
        lock (_lock)
        {
            try { UnloadAll(); } catch { /* 退出时忽略卸载异常 */ }
        }
    }

    private sealed class LoadedPatch(IServerPatch patch, AssemblyLoadContext context, string source)
    {
        public IServerPatch Patch { get; } = patch;
        public AssemblyLoadContext Context { get; } = context;
        public string Source { get; } = source;
    }

    // 每个补丁 DLL 独立 ALC，但接口/协议所在的服务端程序集必须共享（不隔离），
    // 否则 IServerPatch 在默认 ALC 与补丁 ALC 中成为两个不同 Type，无法互相 cast。
    // 策略：补丁自身携带的依赖（不在下列共享名单内）才从 plugins 目录加载；
    // 服务端主程序集与协议程序集回退到默认加载（由主机已加载的同一份）。
    private static readonly HashSet<string> SharedAssemblies = new(StringComparer.OrdinalIgnoreCase)
    {
        "SfsMultiplayer.Server",
        "SfsMultiplayer.Protocol",
        "SFS-Multiplayer-Server",
    };

    private sealed class PatchLoadContext(string name) : AssemblyLoadContext(name, isCollectible: true)
    {
        protected override Assembly? Load(AssemblyName assemblyName)
        {
            if (SharedAssemblies.Contains(assemblyName.Name ?? string.Empty))
                return null; // 交给默认 ALC，保证接口类型身份一致

            var pluginsDir = Path.Combine(AppContext.BaseDirectory, "plugins");
            var inPlugins = Path.Combine(pluginsDir, assemblyName.Name + ".dll");
            if (File.Exists(inPlugins)) return LoadFromAssemblyPath(inPlugins);

            var inBase = Path.Combine(AppContext.BaseDirectory, assemblyName.Name + ".dll");
            if (File.Exists(inBase)) return LoadFromAssemblyPath(inBase);

            return null; // 交给默认解析（共享框架/已加载程序集）
        }
    }
}
