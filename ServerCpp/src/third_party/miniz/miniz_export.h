// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// 说明：miniz 上游用 CMake 生成 miniz_export.h（负责定义 MINIZ_EXPORT 这个链接宏）。
// 本项目以源码方式静态编入（见 build.sh），不需要导出符号，因此这里就是把宏展开为空。
// 只加这一个文件、不改动 miniz 自身的任何源码。

#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif
