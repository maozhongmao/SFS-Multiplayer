// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Globalization;
using System.Text;
using SfsMultiplayer.Protocol;
using SfsMultiplayer.Server;

Console.OutputEncoding = Encoding.UTF8;
return await ServerProgram.RunAsync(args);

internal static class ServerProgram
{
    public static async Task<int> RunAsync(string[] args)
    {
        try
        {
            var options = CommandLine.Parse(args);
            if (options.Help)
            {
                PrintHelp();
                return 0;
            }

            var baseDirectory = AppContext.BaseDirectory;
            var defaultConfigPath = Path.Combine(baseDirectory, "server.yml");
            var configPath = options.ConfigPath is null
                ? ServerSettings.EnsureDefaultYaml(defaultConfigPath)
                : Path.GetFullPath(options.ConfigPath);
            var settings = ServerSettings.Load(configPath);
            var pathBase = Path.GetDirectoryName(configPath) ?? baseDirectory;
            if (string.IsNullOrWhiteSpace(settings.StatePath))
                settings.StatePath = Path.Combine(pathBase, "data", "server-state.json");

            ApplyOverrides(settings, options);
            settings.WorldPath = ResolveOptionalPath(pathBase, settings.WorldPath);
            settings.StatePath = ResolveOptionalPath(pathBase, settings.StatePath);
            var password = Environment.GetEnvironmentVariable("SFS_SERVER_PASSWORD");
            if (password is not null) settings.Password = password;
            settings.Validate();

            var (world, source) = LoadWorld(settings);
            Console.WriteLine($"[世界] 来源={source} 时间={world.WorldTime:F3} 难度={world.Difficulty} 火箭={world.Rockets.Count} 部件={world.Rockets.Values.Sum(r => r.Parts.Count)}");
            Console.WriteLine($"[状态] {settings.StatePath}");

            if (options.Check)
            {
                Console.WriteLine("CHECK_OK 配置、世界和状态均可读取。");
                return 0;
            }

            using var cancellation = new CancellationTokenSource();
            ConsoleCancelEventHandler cancelKeyPress = (_, eventArgs) =>
            {
                eventArgs.Cancel = true;
                cancellation.Cancel();
            };
            EventHandler processExit = (_, _) => cancellation.Cancel();
            Console.CancelKeyPress += cancelKeyPress;
            AppDomain.CurrentDomain.ProcessExit += processExit;
            try
            {
                await using (var server = new TcpMultiplayerServer(settings, world))
                {
                    server.Start();
                    Console.WriteLine($"[启动] TCP+UDP P2P Network V1.2.0-preview {settings.BindAddress}:{server.Port}，最多 {settings.MaxConnections} 人。");
                    if (settings.Debug) Console.WriteLine("[调试] 已开启。");
                    Console.WriteLine("[指令] 输入 help 查看服务端命令，输入 stop 安全保存并退出。");
                    // 启动加载 plugins/ 下的 DLL 补丁（不阻塞网络启动）。
                    server.LoadPatches();
                    StartConsoleCommandThread(server, cancellation);
                    await server.RunAsync(cancellation.Token);
                }
                Console.WriteLine("[停止] 状态已保存，网络线程已停止，服务端已退出。");
                return 0;
            }
            finally
            {
                Console.CancelKeyPress -= cancelKeyPress;
                AppDomain.CurrentDomain.ProcessExit -= processExit;
            }
        }
        catch (CommandLineException ex)
        {
            Console.Error.WriteLine("参数错误：" + ex.Message);
            Console.Error.WriteLine("运行 --help 查看用法。");
            return 2;
        }
        catch (Exception ex)
        {
            Console.Error.WriteLine("启动失败：" + ex.Message);
            if (Environment.GetEnvironmentVariable("SFS_SERVER_DEBUG") == "1")
                Console.Error.WriteLine(ex);
            return 1;
        }
    }

    private static void StartConsoleCommandThread(TcpMultiplayerServer server, CancellationTokenSource cancellation)
    {
        var thread = new Thread(() => RunConsoleCommands(server, cancellation))
        {
            IsBackground = true,
            Name = "SFS Server Console Commands"
        };
        thread.Start();
    }

    private static void RunConsoleCommands(TcpMultiplayerServer server, CancellationTokenSource cancellation)
    {
        while (!cancellation.IsCancellationRequested)
        {
            string? line;
            try
            {
                line = Console.ReadLine();
            }
            catch (IOException)
            {
                return;
            }
            if (line is null) return;
            var result = server.ExecuteCommand(line);
            if (!string.IsNullOrWhiteSpace(result.Message)) Console.WriteLine("[指令] " + result.Message);
            if (result.RequestShutdown)
            {
                cancellation.Cancel();
                return;
            }
        }
    }

    private static (WorldSnapshot World, string Source) LoadWorld(ServerSettings settings)
    {
        if (ServerStateStore.TryLoad(settings.StatePath, out var saved))
            return (saved!, "服务端状态");
        if (!string.IsNullOrWhiteSpace(settings.WorldPath))
            return (SfsWorldLoader.Load(settings.WorldPath), "SFS 存档（只读导入）");
        return (new WorldSnapshot { WorldTime = 1_000_000, Difficulty = DifficultyType.Normal }, "新空世界");
    }

    private static void ApplyOverrides(ServerSettings settings, CommandLine options)
    {
        if (options.WorldPath is not null) settings.WorldPath = options.WorldPath;
        if (options.StatePath is not null) settings.StatePath = options.StatePath;
        if (options.Port is not null) settings.Port = options.Port.Value;
        if (options.MaxConnections is not null) settings.MaxConnections = options.MaxConnections.Value;
        if (options.AutoSaveSeconds is not null) settings.AutoSaveSeconds = options.AutoSaveSeconds.Value;
        if (options.Debug) settings.Debug = true;
    }

    private static string ResolveOptionalPath(string basePath, string value)
    {
        if (string.IsNullOrWhiteSpace(value)) return string.Empty;
        return Path.GetFullPath(Path.IsPathRooted(value) ? value : Path.Combine(basePath, value));
    }

    private static void PrintHelp()
    {
        Console.WriteLine("SFS Multiplayer Server（独立实时联机服务端）");
        Console.WriteLine();
        Console.WriteLine("用法:");
        Console.WriteLine("  SFS-Multiplayer-Server.exe [选项]");
        Console.WriteLine();
        Console.WriteLine("选项:");
        Console.WriteLine("  --config <文件>       读取 JSON 配置；相对路径以配置目录为基准");
        Console.WriteLine("  --world <世界目录>    首次只读导入 SFS 世界目录");
        Console.WriteLine("  --state <文件>        服务端自有状态文件");
        Console.WriteLine("  --port <1-65535>      TCP 监听端口，默认 9806");
        Console.WriteLine("  --max-players <1-256> 最大连接数，默认 16");
        Console.WriteLine("  --autosave <秒>       自动保存间隔，0 禁用，默认 30");
        Console.WriteLine("  --debug               开启网络调试汇总（RTT/抖动/流量/队列）");
        Console.WriteLine("  --check               只校验配置/世界/状态，不启动网络");
        Console.WriteLine("  --help                显示帮助");
        Console.WriteLine();
        Console.WriteLine("环境变量:");
        Console.WriteLine("  SFS_SERVER_PASSWORD   连接密码；优先于配置文件");
        Console.WriteLine("  SFS_SERVER_DEBUG=1    启动失败时打印完整异常");
    }
}

internal sealed record CommandLine(
    string? ConfigPath,
    string? WorldPath,
    string? StatePath,
    int? Port,
    int? MaxConnections,
    int? AutoSaveSeconds,
    bool Debug,
    bool Check,
    bool Help)
{
    public static CommandLine Parse(IReadOnlyList<string> args)
    {
        string? config = null, world = null, state = null;
        int? port = null, max = null, autosave = null;
        var check = false;
        var debug = false;
        var help = false;
        for (var i = 0; i < args.Count; i++)
        {
            switch (args[i])
            {
                case "--config": config = Value(args, ref i, "--config"); break;
                case "--world": world = Value(args, ref i, "--world"); break;
                case "--state": state = Value(args, ref i, "--state"); break;
                case "--port": port = Integer(args, ref i, "--port"); break;
                case "--max-players": max = Integer(args, ref i, "--max-players"); break;
                case "--autosave": autosave = Integer(args, ref i, "--autosave"); break;
                case "--debug": debug = true; break;
                case "--check": check = true; break;
                case "--help":
                case "-h": help = true; break;
                default: throw new CommandLineException($"未知参数：{args[i]}");
            }
        }
        return new CommandLine(config, world, state, port, max, autosave, debug, check, help);
    }

    private static string Value(IReadOnlyList<string> args, ref int index, string option)
    {
        if (++index >= args.Count || args[index].StartsWith("--", StringComparison.Ordinal))
            throw new CommandLineException($"{option} 缺少值。");
        return args[index];
    }

    private static int Integer(IReadOnlyList<string> args, ref int index, string option)
    {
        var value = Value(args, ref index, option);
        if (!int.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var number))
            throw new CommandLineException($"{option} 必须是整数：{value}");
        return number;
    }
}

internal sealed class CommandLineException(string message) : Exception(message);
