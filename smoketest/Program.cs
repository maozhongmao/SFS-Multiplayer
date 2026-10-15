using System;
using System.Collections.Generic;
using System.Linq;

class Sim {
    // 复刻修复后的 OnPacket_CreateRocket 行为
    static Dictionary<int,string> synced = new();
    static Dictionary<int,string> pending = new();
    static Func<int,string> Spawn; // 返回 null 表示 prefab 未就绪

    static void OnCreate(int id, string rocket) {
        var r = Spawn(id);
        if (r == null) { pending[id] = rocket; Console.WriteLine($"  CreateRocket#{id} prefab未就绪 -> 缓存pending"); return; }
        synced[id] = r; pending.Remove(id);
        Console.WriteLine($"  CreateRocket#{id} 成功建出 LocalRocket");
    }
    static void Update() {
        if (pending.Count==0) return;
        var snap = pending.Values.ToList(); pending.Clear();
        foreach (var r in snap) { /* 重新触发 */ }
        // 简化：直接重放
        foreach (var kv in snap.Select((v,i)=>new{i,v}).ToDictionary(x=>x.i,x=>x.v))
            OnCreate(int.Parse(kv.Value.Split('#')[1]), kv.Value);
    }

    static void Main() {
        // 场景：prefab 第1帧为null，第2帧就绪（=联机进服时 prefab 延迟）
        int frame = 0;
        Spawn = (id) => frame >= 2 ? $"Rocket{id}" : null;

        Console.WriteLine("=== 修复前（抛异常，对方火箭永远建不出）===");
        try { var r = Spawn(1); if (r==null) throw new Exception("Instantiate(null)"); }
        catch (Exception e) { Console.WriteLine($"  抛异常: {e.Message} -> 冻结"); }

        Console.WriteLine("=== 修复后（缓存pending，等prefab就绪重试）===");
        OnCreate(1, "Rocket#1"); // frame0: null -> pending
        frame = 2; // prefab 就绪
        Update(); // 重试
        Console.WriteLine(synced.ContainsKey(1) ? "SMOKE_PASS: 对方火箭建出，不再冻结" : "SMOKE_FAIL");
    }
}
