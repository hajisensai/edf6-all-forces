#pragma once

namespace crew {
// 插件启动时调用（ini ObjectUpdateMT，默认 1；docs/object-update-mt-re.md）：System::update 的 slot5 循环里，
// InsectBase / HumanBase 对 0x6ED800（骨骼 -> 碰撞代理）的调用改成登记，纯计算交给工作线程，游戏线程在
// 需要结果时（帧内射线 / 盒体 / 刚体查询可能碰到代理层、布娃娃切换、刚体改位置、删对象、再次直接调用、
// 循环结束）按登记顺序串行提交。全有或全无：任何字节或函数指纹不符就一处不改，记一行 OBJMT refused。
bool InstallObjectUpdateMultithread() noexcept;
// 插件自己要直接读写代理之前调用（游戏线程；别的线程或未安装时什么都不做）：把已登记的全部提交。
void FlushObjectUpdates() noexcept;
// PerfProbe：本周期的 PERF OBJMT 行（登记数、按原因的 flush、并行计算 / 串行提交 / 等待耗时），然后清零。
void ReportObjectUpdateMT(unsigned frames) noexcept;
}  // namespace crew
