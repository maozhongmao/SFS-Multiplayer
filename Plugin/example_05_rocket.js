// 【类别 5】火箭 —— sfs.readRocket(rocketId) / sfs.writeRocket(view)
// 能力位：readRocket (1<<0) / writeRocket (1<<1)
// ⚠️ 状态：**服务端侧尚未开放** —— 当前调用一律返回失败（-5 不可用）。
//    接口形状已冻结；打开后它的用法就是下面这样：
//
//    var v = sfs.readRocket(rocketId);
//    // 成功：{ rocketId, ownerPlayer, controlledBy, partCount,
//    //         posX/Y/Z, velX/Y/Z, throttle, hasUpdateAuthority }
//    var rc = sfs.writeRocket({ rocketId: rocketId, throttle: 1.0 });
//    // 限制：id 必须存在；throttle 0..1；坐标/速度必须是有限数；每轮写次数有上限
//
// 编译：qjsc -b -o 05_rocket.qbc 05_rocket.js

function onEvent(event) {
  if (event.kind !== "worldSnapshot" || event.rockets.length === 0) return;

  var first = sfs.readRocket(event.rockets[0].rocketId);
  if (typeof first === "object") {
    sfs.log("rocket " + first.rocketId + " parts=" + first.partCount + " throttle=" + first.throttle);
  } else {
    sfs.log("readRocket unavailable yet (rc=" + first + ")");
  }
}
globalThis.onEvent = onEvent;
onEvent;
