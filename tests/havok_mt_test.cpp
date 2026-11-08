// hknp 物理多线程的站点、补丁字节与每帧决策（src/havok_mt_logic.h）离线校验，不需要 EDF.dll：
// 三个调用点都是 mov rcx,[rip→0x20B30A0]; call rel32，原 rel32 指向表里的目标；CallBytes 写出的 E8 rel32 解得回目标
// （含 rel32 边界）；指纹里决定顺序的字段（processTaskQueue 写的 +0x140 / +0x380 / +0x128 / +0x155，waitForCompletion
// 清的 +0x154，复位先释放 pool 再释放 queue，析构的 vt+0x68 close）与常量一致；DecideStep / ResetNeedsDetach 的每个分支。
// cmake --build build --target havok_mt_test && build\havok_mt_test.exe
#include "../src/havok_mt_logic.h"
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace {
using namespace crew::havokmt;
int failures=0,checks=0;
bool Expect(bool ok,const char* what,std::uint32_t rva=0) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %#x %s\n",rva,what);}
    return ok;
}
std::uint32_t Rd32(const std::uint8_t* b) {
    return static_cast<std::uint32_t>(b[0])|static_cast<std::uint32_t>(b[1])<<8|static_cast<std::uint32_t>(b[2])<<16|
           static_cast<std::uint32_t>(b[3])<<24;
}
const Fingerprint* Find(std::uint32_t rva) {
    for(const Fingerprint& f:kFingerprints)if(f.rva==rva)return &f;
    return nullptr;
}
// bytes 里第一次出现 pattern 的偏移，没有则 -1。
int Seek(const Fingerprint& f,std::initializer_list<std::uint8_t> pattern) {
    const auto* p=pattern.begin();
    const int n=static_cast<int>(pattern.size());
    for(int i=0;i+n<=f.size;++i)if(std::memcmp(f.bytes+i,p,n)==0)return i;
    return -1;
}

void CallSites() {
    const CallSite* sites[]={&kStepSite,&kResetSites[0],&kResetSites[1]};
    for(const CallSite* s:sites) {
        Expect(s->call==s->context+7,"the call follows the 7-byte mov rcx,[rip+x]",s->call);
        Expect(s->stock[0]==0x48 && s->stock[1]==0x8B && s->stock[2]==0x0D,"mov rcx,[rip+disp32]",s->context);
        const std::uint32_t global=s->context+7+Rd32(s->stock+3);
        Expect(global==kHolderGlobal,"rcx = [0x20B30A0], the physics holder",s->context);
        Expect(s->stock[7]==0xE8,"E8 rel32",s->call);
        Expect(CallTarget(s->call,s->stock+7)==s->target,"the stock rel32 reaches the table's target",s->call);
    }
    Expect(kStepSite.target==kStepNop && kResetSites[0].target==kReset && kResetSites[1].target==kReset,"targets");
    Expect(kResetSites[0].call!=kResetSites[1].call,"two different reset callers");
    // CallBytes 往返：近处、远处、rel32 边界。
    const std::uint64_t base=0x180000000ull;
    for(const CallSite* s:sites) {
        std::uint8_t out[5]{};
        Expect(CallBytes(base+s->call,base+s->target,out),"stock target in reach",s->call);
        Expect(std::memcmp(out,s->stock+7,5)==0,"CallBytes rewrites the stock bytes",s->call);
        const std::uint64_t thunk=base+s->call-0x7FFF0000ull;   // RedirectCall 的近处 thunk 可在 ±2 GB 内任何地方
        Expect(CallBytes(base+s->call,thunk,out) && CallTarget(s->call,out)==static_cast<std::uint32_t>(thunk-base),"backward thunk",s->call);
    }
    std::uint8_t out[5]{};
    Expect(CallBytes(0x1000,0x1005+0x7FFFFFFFull,out) && Rd32(out+1)==0x7FFFFFFFu,"rel32 max");
    Expect(!CallBytes(0x1000,0x1005+0x80000000ull,out),"rel32 max+1 refused");
    Expect(CallBytes(0x90000000ull,0x90000005ull-0x80000000ull,out) && Rd32(out+1)==0x80000000u,"rel32 min");
    Expect(!CallBytes(0x90000000ull,0x90000005ull-0x80000001ull,out),"rel32 min-1 refused");
}

void Fingerprints() {
    for(std::size_t i=0;i<kFingerprintCount;++i) {
        Expect(kFingerprints[i].size>0 && kFingerprints[i].size<=sizeof(kFingerprints[i].bytes),"fingerprint size",kFingerprints[i].rva);
        for(std::size_t j=i+1;j<kFingerprintCount;++j)Expect(kFingerprints[i].rva!=kFingerprints[j].rva,"fingerprint once",kFingerprints[i].rva);
    }
    const Fingerprint* nop=Find(kStepNop);
    Expect(nop && nop->size==3 && nop->bytes[0]==0xC2 && nop->bytes[1]==0 && nop->bytes[2]==0,"0x118F270 is ret 0",kStepNop);
    if(const Fingerprint* p=Find(kPoolProcess)) {
        // mov [rcx+0x140],rdx; mov [rdx+0x380],eax; lea rax,[rip+0x45C]; mov [rcx+0x128],rax; mov byte [rcx+0x155],1
        const int q=Seek(*p,{0x48,0x89,0x91,0x40,0x01,0x00,0x00});
        Expect(q>=0 && kPoolQueue==0x140,"processTaskQueue writes [pool+0x140]=queue",kPoolProcess);
        Expect(Seek(*p,{0x89,0x82,0x80,0x03,0x00,0x00})>=0 && kQueueThreads==0x380,"[queue+0x380]=threads: the full queue pointer",kPoolProcess);
        const int lea=Seek(*p,{0x48,0x8D,0x05});
        Expect(lea>=0 && kPoolProcess+lea+7+Rd32(p->bytes+lea+3)==kWorkerJob,"[pool+0x128]=0x13102A0",kPoolProcess);
        Expect(Seek(*p,{0xC6,0x81,0x55,0x01,0x00,0x00,0x01})>=0 && kPoolProcessing==0x155,"[pool+0x155]=1",kPoolProcess);
        Expect(Seek(*p,{0x8B,0x41,0x20})>=0 && kPoolThreads==0x20,"threads read from [pool+0x20]",kPoolProcess);
    } else Expect(false,"processTaskQueue fingerprint");
    if(const Fingerprint* w=Find(kPoolWait)) {
        Expect(Seek(*w,{0x80,0xB9,0x55,0x01,0x00,0x00,0x00})==6,"waitForCompletion tests [pool+0x155] first",kPoolWait);
        Expect(Seek(*w,{0x48,0x8D,0x8B,0x48,0x01,0x00,0x00})>=0,"waits on the pool's +0x148 semaphore",kPoolWait);
        Expect(Seek(*w,{0x66,0xC7,0x83,0x54,0x01,0x00,0x00,0x00,0x00})>=0,"clears word +0x154 (+0x155 with it)",kPoolWait);
        Expect(Seek(*w,{0x48,0x8B,0x83,0x40,0x01,0x00,0x00})>=0,"then reads +0x140 to clear it",kPoolWait);
    } else Expect(false,"waitForCompletion fingerprint");
    if(const Fingerprint* c=Find(kQueueClose))
        Expect(Seek(*c,{0x80,0xBF,0x48,0x03,0x00,0x00,0x00})>=0 && kQueueClosed==kQueueSub+0x348,"close tests sub+0x348",kQueueClose);
    else Expect(false,"close fingerprint");
    if(const Fingerprint* r=Find(0x118F110)) {
        const int pool=Seek(*r,{0x48,0x8B,0x4F,0x08});
        const int queue=Seek(*r,{0x48,0x8B,0x4F,0x20});
        Expect(pool>=0 && queue>pool && kHolderPool==8 && kHolderQueue==0x20,"reset frees [holder+8] then [holder+0x20]",0x118F110);
        int calls=0;
        for(int i=0;i+5<=r->size;++i)
            if(r->bytes[i]==0xE8 && 0x118F110+i+5+Rd32(r->bytes+i+1)==0x978FB0){++calls;i+=4;}
        Expect(calls==2,"two hkReferencedObject releases (0x978FB0), no close in between",0x118F110);
    } else Expect(false,"reset fingerprint");
    if(const Fingerprint* d=Find(0x118ECAD))
        Expect(Seek(*d,{0x48,0x83,0xC1,0x18})>=0 && Seek(*d,{0xFF,0x50,0x68})>=0 && kQueueCloseSlot*8==0x68,"destructor closes sub vt+0x68",0x118ECAD);
    else Expect(false,"destructor fingerprint");
    Expect(kPoolProcessSlot*8==0x20 && kPoolWaitSlot*8==0x40 && kPoolSetThreadsSlot*8==0x50,"slot offsets");
}

void Decisions() {
    StepView v;
    Expect(DecideStep(v)==StepAction::none,"no holder");
    v.holder=true;
    Expect(DecideStep(v)==StepAction::none,"no pool / queue");
    v.pool=true;v.queue=true;
    Expect(DecideStep(v)==StepAction::badVtable,"objects not recognized");
    v.vtables=true;v.queueAddr=0x5000;v.threads=15;
    Expect(DecideStep(v)==StepAction::attach,"fresh pool: attach");
    v.attached=0x5000;v.processing=true;
    Expect(DecideStep(v)==StepAction::none,"attached: nothing");
    v.attached=0x6000;
    Expect(DecideStep(v)==StepAction::foreign,"another queue: not touched");
    v.attached=0x5000;v.processing=false;
    Expect(DecideStep(v)==StepAction::foreign,"queue set, processing clear: not a state the plugin makes, not touched");
    v.attached=0;v.threads=0;
    Expect(DecideStep(v)==StepAction::noWorkers,"no workers");
    v.threads=15;v.closed=true;
    Expect(DecideStep(v)==StepAction::closedQueue,"closed queue");
    v.closed=false;v.vtables=false;
    Expect(DecideStep(v)==StepAction::badVtable,"vtable checked before anything else");
    Expect(ResetNeedsDetach(true,0x5000),"attached: close + wait before the reset");
    Expect(!ResetNeedsDetach(true,0),"not attached (stock, or already waited): the reset as stock");
    Expect(!ResetNeedsDetach(false,0x5000),"unrecognized objects: not touched");
}
}  // namespace

int main() {
    CallSites();
    Fingerprints();
    Decisions();
    std::printf("havok_mt: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
