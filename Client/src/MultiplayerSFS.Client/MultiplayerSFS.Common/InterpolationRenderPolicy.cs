// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using UnityEngine;

namespace MultiplayerSFS.Common;

/// <summary>
/// 远端火箭渲染状态契约：
/// 插值（Hermite/Linear）已产生平滑的目标状态，但包间隔边界仍可能有微小不连续；
/// 渲染端对此做一层极轻的指数平滑：平滑常数 = 包间隔 × 0.2，钳制在 0.02~0.12 秒。
/// 恒速稳态滞后 ≈ 速度 × 平滑常数，上限 0.12 秒保证远低于历史重滤波版本（0.45~1.4 秒）；
/// 位置跳变（换星、传送、权威切换）不经过本平滑，由首包硬对齐负责。
/// </summary>
public static class InterpolationRenderPolicy
{
    /// <summary>
    /// 平滑时间常数 = 包间隔 × 该比例。刻意取小（默认 0.2）：
    /// - 完全不平滑会在包边界产生肉眼可见的微跳（一闪一闪）；
    /// - 平滑太重则恒速滞后 = 速度 × 常数，旧版 0.45s 就是这样飘的。
    /// 取包间隔的 20%（200ms 包距 → 40ms 平滑）居中折衷，滞后不可察觉且不闪。
    /// </summary>
    public const double SmoothingFractionOfPacketInterval = 0.2;

    /// <summary>平滑时间常数下限（秒），避免超高频包时常数趋零导致抖动。</summary>
    public const double MinSmoothingSeconds = 0.02;

    /// <summary>平滑时间常数上限（秒），任何网速档下都不得回到旧版 0.45s+ 的重滤波。</summary>
    public const double MaxSmoothingSeconds = 0.12;

    /// <summary>由包间隔推导渲染平滑时间常数（秒）。</summary>
    public static double SmoothingSeconds(double packetIntervalSeconds)
    {
        if (!IsFinite(packetIntervalSeconds) || packetIntervalSeconds <= 0)
            return MinSmoothingSeconds;
        double value = packetIntervalSeconds * SmoothingFractionOfPacketInterval;
        if (value < MinSmoothingSeconds) return MinSmoothingSeconds;
        if (value > MaxSmoothingSeconds) return MaxSmoothingSeconds;
        return value;
    }

    /// <summary>
    /// 单帧指数平滑：alpha 由时间常数与帧时长推导。恒速稳态滞后 ≈ 速度 × smoothingSeconds，
    /// 上限 0.12s 保证滞后远低于旧版（0.45~1.4s），视觉无“飘”。
    /// </summary>
    public static double FrameAlpha(double smoothingSeconds, double unscaledDeltaTime)
    {
        if (!IsFinite(unscaledDeltaTime) || unscaledDeltaTime <= 0) return 1.0;
        return 1.0 - Math.Exp(-unscaledDeltaTime / Math.Max(0.001, smoothingSeconds));
    }

    /// <summary>渲染位置：向插值目标做轻量指数平滑（见 SmoothingSeconds 的折衷说明）。</summary>
    public static (double X, double Y) ResolvePosition(double smoothingSeconds, double unscaledDeltaTime,
        double renderedX, double renderedY, double targetX, double targetY)
    {
        double alpha = FrameAlpha(smoothingSeconds, unscaledDeltaTime);
        return (renderedX + (targetX - renderedX) * alpha, renderedY + (targetY - renderedY) * alpha);
    }

    /// <summary>渲染速度：同样轻量平滑。</summary>
    public static (double Vx, double Vy) ResolveVelocity(double smoothingSeconds, double unscaledDeltaTime,
        double renderedVx, double renderedVy, double targetVx, double targetVy)
    {
        double alpha = FrameAlpha(smoothingSeconds, unscaledDeltaTime);
        return (renderedVx + (targetVx - renderedVx) * alpha, renderedVy + (targetVy - renderedVy) * alpha);
    }

    /// <summary>渲染朝向：轻量角度平滑（最短弧）。</summary>
    public static float ResolveRotation(double smoothingSeconds, double unscaledDeltaTime,
        float renderedRotation, float targetRotation)
    {
        float alpha = (float)FrameAlpha(smoothingSeconds, unscaledDeltaTime);
        return Mathf.LerpAngle(renderedRotation, targetRotation, alpha);
    }

    /// <summary>渲染角速度：轻量平滑。</summary>
    public static float ResolveAngularVelocity(double smoothingSeconds, double unscaledDeltaTime,
        float renderedAngularVelocity, float targetAngularVelocity)
    {
        float alpha = (float)FrameAlpha(smoothingSeconds, unscaledDeltaTime);
        return renderedAngularVelocity + (targetAngularVelocity - renderedAngularVelocity) * alpha;
    }

    private static bool IsFinite(double value)
    {
        return !double.IsNaN(value) && !double.IsInfinity(value);
    }
}
