// hknp 物理步进多线程（docs/havok-mt-re.md）的站点、期望字节与每帧决策，纯数据，不碰游戏：
// havok_mt.cpp 安装，tests/havok_mt_test.cpp、tests/havok_mt_audit.py、tests/havok_mt_sim.py 校验。
//
// 原版：物理持有者 System@havok@xgs（全局指针 0x20B30A0）建好了 hkCpuThreadPool（[holder+8]，按核数建了 worker，
// Steam 版 8 核 16 线程上是 15 个）和 hkDefaultTaskQueue（[holder+0x20]），但从来没让 pool 处理这个队列：
// worker 一直睡着，hknp 的任务图全由游戏线程在 processGraph 0x9EDDB0 里一个个跑完。
// 打开：System::update 在步进前 0x119932F 调用的 0x118F270 是空函数（ret 0，全 DLL 只此一处调用），
// 改成调插件的 BeforeStep：每帧在游戏线程、步进前核对 holder / pool / queue 的 vtable，pool 还没接这个队列就照
// setNumThreads 0x130FF70 末尾的做法调 pool vt 槽 4 processTaskQueue 0x130FE20(pool, queue) 接上。
// 关闭：复位 0x118F0A0（调用者只有 0x70E38E、0x70FEC6）直接释放 pool 和 queue，不先 close。接上以后 worker
// 停在队列里等任务，pool 析构 0x130F670 会等 worker 退出，永远等不到：死锁。两个调用点改成先调插件的
// BeforeReset：照抄 0x130FF70 的顺序 close（队列子对象 vt+0x68 = 0xA15050）→ pool vt+0x40 waitForCompletion
// 0x1310630（等每个 worker 退出 process 并清 +0x140 / +0x154 / +0x155），再调原来的 0x118F0A0。
// 正常析构 0x118EC70 自己在 0x118ECB5 先 close 再释放，不用改。
#pragma once
#include <cstddef>
#include <cstdint>

namespace crew::havokmt {
// 持有者与两个对象（RVA / 偏移）。
constexpr std::uint32_t kHolderGlobal=0x20B30A0;
constexpr std::size_t kHolderPool=0x08,kHolderQueue=0x20;
constexpr std::uint32_t kPoolVtable=0x1AFED80;      // .?AVhkCpuThreadPool@@
constexpr std::uint32_t kQueueVtable=0x182EC50;     // .?AVhkDefaultTaskQueue@@
constexpr std::uint32_t kQueueSubVtable=0x182EC80;  // 队列 +0x18 处的子对象（close / reopen / 处理循环都以它为 this）
constexpr std::size_t kQueueSub=0x18;
// pool 字段（0x130FE20 写，0x1310630 清）
constexpr std::size_t kPoolThreads=0x20;            // int，worker 个数
constexpr std::size_t kPoolQueue=0x140;             // 正在处理的队列（完整指针，不是 +0x18 子对象）
constexpr std::size_t kPoolProcessing=0x155;        // byte，1 = worker 正在处理队列
// 队列字段
constexpr std::size_t kQueueClosed=kQueueSub+0x348; // byte，close 0xA15050 置 1，worker 在 0xA0FBFB 看到就退出
constexpr std::size_t kQueueThreads=0x380;          // int，processTaskQueue 写 worker 数
// vtable 槽（下标）与它们应指向的函数
constexpr std::size_t kPoolProcessSlot=4;           // vt+0x20 processTaskQueue(pool, queue)
constexpr std::size_t kPoolWaitSlot=8;              // vt+0x40 waitForCompletion(pool)
constexpr std::size_t kQueueCloseSlot=13;           // 子对象 vt+0x68 close(sub)
constexpr std::uint32_t kPoolProcess=0x130FE20,kPoolWait=0x1310630,kQueueClose=0xA15050;
constexpr std::uint32_t kWorkerJob=0x13102A0;       // processTaskQueue 写进 [pool+0x128] 的 worker 函数
constexpr std::uint32_t kSetNumThreads=0x130FF70;   // pool vt 槽 10：close → wait → reopen → processTaskQueue 的出处
constexpr std::size_t kPoolSetThreadsSlot=10;

// 改写的三个调用点：E8 rel32，前面都是 mov rcx,[rip→0x20B30A0]（rcx = holder）。
struct CallSite { std::uint32_t context; std::uint32_t call; std::uint32_t target; std::uint8_t stock[12]; };
constexpr std::uint32_t kStepNop=0x118F270;         // ret 0（C2 00 00）
constexpr std::uint32_t kReset=0x118F0A0;
constexpr CallSite kStepSite={0x1199328,0x119932F,kStepNop,
    {0x48,0x8B,0x0D,0x71,0x9D,0xF1,0x00,0xE8,0x3C,0x5F,0xFF,0xFF}};
constexpr CallSite kResetSites[]={
    {0x70E387,0x70E38E,kReset,{0x48,0x8B,0x0D,0x12,0x4D,0x9A,0x01,0xE8,0x0D,0x0D,0xA8,0x00}},
    {0x70FEBF,0x70FEC6,kReset,{0x48,0x8B,0x0D,0xDA,0x31,0x9A,0x01,0xE8,0xD5,0xF1,0xA7,0x00}},
};
constexpr std::size_t kResetSiteCount=sizeof(kResetSites)/sizeof(kResetSites[0]);

// 只核对、不写的指纹：被调函数的开头与复位 / 析构里决定顺序的那几条指令。
struct Fingerprint { std::uint32_t rva; std::uint8_t size; std::uint8_t bytes[80]; };
constexpr Fingerprint kFingerprints[]={
    {kStepNop,3,{0xC2,0x00,0x00}},
    // processTaskQueue：[pool+0x140]=queue，[queue+0x380]=[pool+0x20]，[pool+0x128]=0x13102A0，[pool+0x155]=1，
    // [pool+0x130]=queue，然后逐个唤醒 worker
    {kPoolProcess,70,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0x41,0x20,0x48,0x8B,0xF9,0x48,0x89,0x91,0x40,
                      0x01,0x00,0x00,0x89,0x82,0x80,0x03,0x00,0x00,0x48,0x8D,0x05,0x5C,0x04,0x00,0x00,0x48,0x89,0x81,0x28,
                      0x01,0x00,0x00,0x8B,0x41,0x20,0x83,0xE8,0x01,0xC6,0x81,0x55,0x01,0x00,0x00,0x01,0x48,0x63,0xD8,0x48,
                      0x89,0x91,0x30,0x01,0x00,0x00,0x78,0x28,0x0F,0x1F}},
    // waitForCompletion：[pool+0x155] 时对 +0x148 等 [pool+0x20] 次，清 word +0x154，再清 +0x138 / [queue+0x380] / +0x140
    {kPoolWait,80,{0x40,0x53,0x48,0x83,0xEC,0x20,0x80,0xB9,0x55,0x01,0x00,0x00,0x00,0x48,0x8B,0xD9,0x74,0x37,0x48,0x89,
                   0x7C,0x24,0x38,0x33,0xFF,0x39,0x79,0x20,0x7E,0x1D,0x48,0x89,0x74,0x24,0x30,0x48,0x8D,0x8B,0x48,0x01,
                   0x00,0x00,0xE8,0x01,0xA8,0x6D,0xFF,0xFF,0xC7,0x3B,0x7B,0x20,0x7C,0xED,0x48,0x8B,0x74,0x24,0x30,0x48,
                   0x8B,0x7C,0x24,0x38,0x66,0xC7,0x83,0x54,0x01,0x00,0x00,0x00,0x00,0x48,0x8B,0x83,0x40,0x01,0x00,0x00}},
    // close：加锁后看 +0x348，没关就置 1 并唤醒等待者
    {kQueueClose,26,{0x40,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9,0x48,0x8B,0x49,0x08,0x48,0x8B,0x01,0xFF,0x50,0x08,0x80,
                     0xBF,0x48,0x03,0x00,0x00,0x00}},
    // 复位 0x118F110..0x118F13D：先释放 pool（0x118F121），再释放 queue（0x118F138），之前没有 close
    {0x118F110,46,{0x4C,0x89,0x7D,0x77,0x48,0x8B,0x4F,0x08,0x4C,0x89,0x7F,0x08,0x48,0x85,0xC9,0x74,0x06,0xE8,0x8A,0x9E,
                   0x7E,0xFF,0x90,0x4C,0x89,0x7D,0x7F,0x48,0x8B,0x4F,0x20,0x4C,0x89,0x7F,0x20,0x48,0x85,0xC9,0x74,0x06,
                   0xE8,0x73,0x9E,0x7E,0xFF,0x90}},
    // 正常析构 0x118ECAD：mov rcx,[rbx+0x20]; add rcx,0x18; call [vt+0x68]（自带 close，不用改）
    {0x118ECAD,14,{0x48,0x8B,0x4B,0x20,0x48,0x83,0xC1,0x18,0x48,0x8B,0x01,0xFF,0x50,0x68}},
};
constexpr std::size_t kFingerprintCount=sizeof(kFingerprints)/sizeof(kFingerprints[0]);

// E8 rel32 指向 target 的 5 个字节。
constexpr bool CallBytes(std::uint64_t site,std::uint64_t target,std::uint8_t out[5]) noexcept {
    const std::int64_t delta=static_cast<std::int64_t>(target)-static_cast<std::int64_t>(site+5);
    if(delta<INT32_MIN || delta>INT32_MAX)return false;
    const auto rel=static_cast<std::uint32_t>(static_cast<std::int32_t>(delta));
    out[0]=0xE8;
    for(int i=0;i<4;++i)out[1+i]=static_cast<std::uint8_t>(rel>>(8*i));
    return true;
}
// 调用点原有 E8 rel32 指向的地址（RVA）。
constexpr std::uint32_t CallTarget(std::uint32_t site,const std::uint8_t call[5]) noexcept {
    std::uint32_t rel=0;
    for(int i=0;i<4;++i)rel|=std::uint32_t{call[1+i]}<<(8*i);
    return static_cast<std::uint32_t>(site+5+static_cast<std::int32_t>(rel));
}

// 每帧步进前看到的状态（havok_mt.cpp 从游戏内存读出，测试里直接构造）。
struct StepView {
    bool holder=false;           // [0x20B30A0] 非空
    bool pool=false,queue=false; // [holder+8] / [holder+0x20] 非空
    bool vtables=false;          // pool / queue / 队列子对象三个 vtable 都对
    std::uintptr_t queueAddr=0;  // [holder+0x20]
    std::uintptr_t attached=0;   // [pool+0x140]
    bool processing=false;       // [pool+0x155]
    int threads=0;               // [pool+0x20]
    bool closed=false;           // [queue+0x360]
};
enum class StepAction { none, attach, foreign, badVtable, noWorkers, closedQueue };
constexpr StepAction DecideStep(const StepView& v) noexcept {
    if(!v.holder || !v.pool || !v.queue)return StepAction::none;              // 物理系统还没建或正在拆
    if(!v.vtables)return StepAction::badVtable;                               // 不是认得的对象：不碰
    if(v.attached==v.queueAddr && v.processing)return StepAction::none;       // 已接上
    if(v.attached!=0)return StepAction::foreign;                              // 接着别的队列：不是插件接的，不碰
    if(v.threads<=0)return StepAction::noWorkers;                             // 单核机器：pool 没建 worker
    if(v.closed)return StepAction::closedQueue;                               // 队列已关（正在析构）
    return StepAction::attach;
}
// 复位前：pool 接着队列（[pool+0x140] 非空）才需要 close + 等待；没接（关掉开关的原版、或已被 waitForCompletion 清过）不动。
constexpr bool ResetNeedsDetach(bool vtables,std::uintptr_t attached) noexcept { return vtables && attached!=0; }
}  // namespace crew::havokmt
