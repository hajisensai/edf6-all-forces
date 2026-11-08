#pragma once

namespace crew {
// 插件启动时调用（ini HavokMultithread=1，默认关，实机测完再定）：让游戏已建好的 hkCpuThreadPool 处理 hkDefaultTaskQueue，
// hknp 物理步进的任务图在多核上并行（havok_mt_logic.h、docs/havok-mt-re.md）。改写三个调用点：步进前的空函数
// 0x119932F 换成每帧接队列，复位 0x118F0A0 的两个调用点先 close 队列并等 worker 退出。全有或全无：任何字节、
// vtable 槽或函数指纹不符就一处不改，记一行 HAVOKMT refused。ini 为 0 时什么都不改。
bool InstallHavokMultithread() noexcept;
}  // namespace crew
