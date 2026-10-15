// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Reflection;
using UnityEngine;

namespace MultiplayerSFS.Mod;

// 把服务端命令接进游戏原生的 F1 控制台（ModLoader.IO.Console）。
//
// 接口是用 Mono.Cecil 从 Assembly-CSharp.dll 里拆出来的（2026-10-06）：
//   public class ModLoader.IO.Console : SFS.Input.Screen_Menu
//   {
//       public static Console main;
//       public static List<Func<string, bool>> commands;   // 返回 true = 我已处理
//       public void HandleLog(string message, ...);        // 它把 Unity 日志显示在屏幕上
//       public void WriteText(string input);
//   }
// 这是**正式扩展点**，不需要 Harmony：往 commands 里加一个 Func<string,bool> 即可。
// 实测游戏自己的控制台没有任何内置命令（连 help 都是 not found），所以不存在重名冲突。
//
// 为什么用反射而不是直接引用类型：我们同时出 1.6 与 1.5 两个包，
// 而 ModLoader.IO.Console 是 1.6 才有的。直接引用会让 1.5 包编译不过；
// 反射在 1.5 上静默跳过，只打一条日志。
//
// 【日志为什么用英文】控制台的 HandleLog 会把每条 Unity 日志塞进一个 TextMeshProUGUI，
// 而 TMP 没有中文字形 —— 中文会变成方框并刷一堆 "character ... was not found" 警告。
// 所以凡是有可能出现在控制台里的文本，都必须 ASCII。
public static class ServerConsoleBridge
{
    private const string ConsoleTypeName = "ModLoader.IO.Console, Assembly-CSharp";

    private static bool installed;
    private static PropertyInfo mainProperty;   // Console.main（静态属性或字段）
    private static FieldInfo mainField;
    private static MethodInfo writeTextMethod;
    private static bool writeTextReported;

    // 我们接管的命令名，与服务端 execute_command 支持的那一套一致。
    // 另外支持 mp:<命令> 强制转发（万一以后游戏自己加了同名命令）。
    private static readonly string[] OwnedCommands = new string[]
    {
        "help", "status", "players", "network", "traffic",
        "say", "time", "debris", "world", "kick", "stop", "mp"
    };

    public static void Install()
    {
        if (installed) return;
        installed = true;
        try
        {
            Type consoleType = Type.GetType(ConsoleTypeName, throwOnError: false);
            if (consoleType == null)
            {
                Debug.Log("[SFS-MP] no ModLoader.IO.Console in this game version; F1 console hook skipped.");
                return;
            }

            FieldInfo commandsField = consoleType.GetField("commands", BindingFlags.Public | BindingFlags.Static);
            object commands = commandsField == null ? null : commandsField.GetValue(null);
            if (commands == null)
            {
                Debug.LogWarning("[SFS-MP] console commands list not found; F1 console hook skipped.");
                return;
            }

            // 控制台自己的输出接口：用来把服务端回包显示在它的日志区里。
            // Debug.Log 虽然也会被 HandleLog 捕获，但实测"命令吞掉了、回包没显示"，
            // 所以改走它自己的 API。
            writeTextMethod = consoleType.GetMethod("WriteText", new Type[] { typeof(string) });
            mainField = consoleType.GetField("main", BindingFlags.Public | BindingFlags.Static);
            mainProperty = consoleType.GetProperty("main", BindingFlags.Public | BindingFlags.Static);

            MethodInfo add = commands.GetType().GetMethod("Add");
            if (add == null)
            {
                Debug.LogWarning("[SFS-MP] console commands list has no Add; F1 console hook skipped.");
                return;
            }

            // 元素类型是 Func<string,bool>，必须构造同型委托（不能直接传方法组，会类型不匹配）。
            add.Invoke(commands, new object[] { new Func<string, bool>(HandleInput) });
            Debug.Log("[SFS-MP] F1 console hooked. Server commands: " + string.Join(", ", OwnedCommands));
        }
        catch (Exception ex)
        {
            Debug.LogWarning("[SFS-MP] F1 console hook failed: " + ex.Message);
        }
    }

    // 控制台命令处理。返回 true = "我处理了"，控制台就不再报 Command not found。
    // 返回 false = 交还给游戏自己处理。
    private static bool HandleInput(string input)
    {
        if (string.IsNullOrWhiteSpace(input)) return false;
        string text = input.Trim();

        if (text.StartsWith("mp:", StringComparison.OrdinalIgnoreCase))
        {
            text = text.Substring(3).Trim();
        }
        else
        {
            string first = text;
            int space = text.IndexOfAny(new char[] { ' ', '\t' });
            if (space >= 0) first = text.Substring(0, space);
            first = first.ToLowerInvariant();

            bool mine = false;
            for (int i = 0; i < OwnedCommands.Length; i++)
            {
                if (OwnedCommands[i] == first) { mine = true; break; }
            }
            if (!mine) return false;
        }

        if (text.Length == 0) return true;
        if (ClientManager.client == null || !ClientManager.client.Connected)
        {
            Debug.Log("[SFS-MP] not connected; command not sent: " + text);
            return true;
        }

        Debug.Log("[SFS-MP] > " + text);
        ClientManager.RequestServerCommand(text);
        return true;
    }

    // 服务端回包 -> 写进控制台自己的输出区。
    // 先试 WriteText（控制台自己的 API，最可能正是它显示日志用的那条路），
    // 失败再退回 Debug.Log（由 HandleLog 捕获）。两者都试，避免又出现"命令吞了、回包没显示"。
    public static void WriteReply(string output)
    {
        if (string.IsNullOrEmpty(output)) return;
        if (!TryWriteText(output))
        {
            Debug.Log("[SFS-MP] server: " + output);
        }
    }

    private static bool TryWriteText(string line)
    {
        try
        {
            if (writeTextMethod == null) return false;
            object main = null;
            if (mainField != null) main = mainField.GetValue(null);
            else if (mainProperty != null) main = mainProperty.GetValue(null, null);
            if (main == null) return false;
            writeTextMethod.Invoke(main, new object[] { line });
            if (!writeTextReported)
            {
                writeTextReported = true;
                Debug.Log("[SFS-MP] console WriteText path OK.");
            }
            return true;
        }
        catch (Exception ex)
        {
            if (!writeTextReported)
            {
                writeTextReported = true;
                Debug.LogWarning("[SFS-MP] console WriteText failed, falling back to log: " + ex.Message);
            }
            return false;
        }
    }
}
