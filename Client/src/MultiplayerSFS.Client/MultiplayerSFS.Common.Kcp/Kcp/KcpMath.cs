// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。

using System;

namespace MultiplayerSFS.Common.Transport.Kcp
{
    /// <summary>
    /// 通用数学工具（netstandard2.0 缺少 Math.Clamp）
    /// </summary>
    internal static class KcpMath
    {
        public static T Clamp<T>(T value, T min, T max) where T : IComparable<T>
        {
            if (value.CompareTo(min) < 0) return min;
            if (value.CompareTo(max) > 0) return max;
            return value;
        }

        public static T Max<T>(T a, T b) where T : IComparable<T>
        {
            return a.CompareTo(b) > 0 ? a : b;
        }

        public static T Min<T>(T a, T b) where T : IComparable<T>
        {
            return a.CompareTo(b) < 0 ? a : b;
        }

        public static uint MaxUInt(uint a, uint b) => a > b ? a : b;
        public static uint MinUInt(uint a, uint b) => a < b ? a : b;
        public static int MaxInt(int a, int b) => a > b ? a : b;
        public static int MinInt(int a, int b) => a < b ? a : b;
    }
}