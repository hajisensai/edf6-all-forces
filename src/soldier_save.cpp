// The new classes' save (the layout and the rules: src/soldier_save_logic.h; docs/plan-2026-11-28-new-classes-impl.md
// 模块 D, docs/newclass-re/inject.md §4, architecture.md §3.3).
//
// Save: MAIN.GST's save request 0x7BCD40 (only caller 0x7C2F66, the block GS+0x6E68) builds its job with 0x7BD910,
// which memcpys the block into the job's own vector and points +0x90 / +0x98 at it (0x7BD9F4..0x7BDA1B), then queues
// the job: 0x7BCFAF call 0x7B5660(queue, &{control, job}, &flags, done). From the queue on, the save thread may run
// it (checksum 0x7C1270 over +0x90 / +0x98, then the write), so the copy is changed right before the queueing call,
// on the requesting thread: the stand-in into the copy, the game status never touched. (0x7BCE7C, right after the
// copy, is outfit41.cpp's; weapon_records.cpp's SaveHook wraps the whole request, its job already queued on return.)
// Load: the completion's copy into the block (0x7BE970, wrapped by weapon_records.cpp ResultCopyHook) calls back
// here (SetSaveBlockLoaded): the real class back while the class is still the stand-in, and only a class this
// session's SoldierInit has (0xE34D0's count: the game status's constructor 0xD62C0 calls SetMode 0xDC460 at
// 0xD6722, so SoldierInit is read before any load).
#include "soldier_save.h"
#include "soldier_save_logic.h"
#include "weapon_records.h"
#include "edf_layout.h"
#include "memory.h"
#include "plugin_log.h"
#include <atomic>
#include <cstdint>

namespace crew {
namespace {
using namespace soldier_save;

constexpr std::size_t kBlockAt=0x6E68;                     // GS: the save block (weapon_records_logic.h kBlock)
constexpr std::size_t kModeAt=0x130,kModeSoldierInit=0x20; // GS+0x130 the mode; +0x20 SoldierInit's node
constexpr std::size_t kNodeCount=4;                        // an SGO array node: +4 its count (0xE34D0)
constexpr unsigned kQueueCall=0x7BCFAF,kQueue=0x7B5660;
constexpr std::size_t kRefJob=8,kJobData=0x90,kJobSize=0x98;
constexpr unsigned kClassCount=0xE34D0;

// 0x7BCF9E: mov r9,[rbp-0x60]; lea r8,[rsp+0x50]; lea rdx,[rsp+0x78]; mov rcx,rbx (then the call at 0x7BCFAF):
// rdx = {control, job} (0x7BCF74..0x7BCF80: [rsp+0x78] = control, [rsp+0x80] = its +8, the job; zero when none).
const unsigned char kQueueArgs[]={0x4C,0x8B,0x4D,0xA0,0x4C,0x8D,0x44,0x24,0x50,0x48,0x8D,0x54,0x24,0x78,0x48,0x8B,0xCB};
constexpr unsigned kQueueArgsAt=0x7BCF9E;
const unsigned char kRefBuilt[]={0x48,0x8B,0x06,0x48,0x89,0x44,0x24,0x78,0x48,0x8B,0x40,0x08,0x48,0x89,0x45,0x80};
constexpr unsigned kRefBuiltAt=0x7BCF74;
// 0x7BCE6D: mov r9d,0xDCE8 (the job's size: 0x7BCD40 saves the block only)
const unsigned char kJobSizeArg[]={0x41,0xB9,0xE8,0xDC,0x00,0x00};
constexpr unsigned kJobSizeArgAt=0x7BCE6D;
// 0x7BDA06: the job's +0x90 = its vector's data, +0x98 = the size
const unsigned char kJobFields[]={0x48,0x8B,0x86,0xC0,0x00,0x00,0x00,0x48,0x89,0x86,0x90,0x00,0x00,0x00,0x48,0x89,0xBE,0x98,0x00,0x00,0x00};
constexpr unsigned kJobFieldsAt=0x7BDA06;
// 0xE34D0: mov rax,[rcx+0x20]; mov eax,[rax+4]; ret (the class count read here the same way)
const unsigned char kClassCountCode[]={0x48,0x8B,0x41,0x20,0x8B,0x40,0x04,0xC3};
static_assert(kModeSoldierInit==0x20 && kNodeCount==4,"as 0xE34D0 reads them");

std::atomic<bool> active{false};
using QueueFn=void*(__fastcall*)(void*,void*,void*,void*);

const char* LoadedName(Loaded l) noexcept {
    switch(l) {
    case Loaded::kNone: return "no record";
    case Loaded::kRestored: return "restored";
    case Loaded::kChanged: return "changed in a stock game";
    case Loaded::kUnavailable: return "not a class this session";
    }
    return "?";
}

// The job's copy, or nullptr (no job, not the block's size, not readable).
unsigned char* JobCopy(void* ref) noexcept {
    __try {
        if(!ref || !Readable(ref,kRefJob+8))return nullptr;
        auto* const job=At<unsigned char*>(ref,kRefJob);
        if(!job || !Readable(job,kJobSize+8))return nullptr;
        auto* const data=At<unsigned char*>(job,kJobData);
        if(At<std::size_t>(job,kJobSize)!=kBlockSize || !data || !Readable(data,kBlockSize,true))return nullptr;
        return data;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void* __fastcall QueueHook(void* queue,void* ref,void* flags,void* done) {
    if(active.load(std::memory_order_acquire)) {
        if(unsigned char* const copy=JobCopy(ref)) {
            SaveResult out[kLocalPlayers];
            if(PrepareSaveCopy(copy,out))
                for(std::size_t p=0;p<kLocalPlayers;++p)
                    if(out[p].now.standIn)Log("SOLDIERSAVE save: player %zu class %d written as %d (record %08X in class %d slot %d)",
                                              p+1,out[p].was,out[p].now.cls,out[p].now.record,kRecordClass,kRecordSlot);
        } else Log("SOLDIERSAVE save: the job's copy of the save block unreadable: saved as the game made it");
    }
    return reinterpret_cast<QueueFn>(image+kQueue)(queue,ref,flags,done);
}

// This session's class count (0xE34D0's read), 0 when SoldierInit is not there.
int ClassCount(unsigned char* gs) noexcept {
    __try {
        if(!Readable(gs+kModeAt+kModeSoldierInit,8))return 0;
        auto* const node=At<unsigned char*>(gs+kModeAt,kModeSoldierInit);
        if(!node || !Readable(node+kNodeCount,4))return 0;
        const int n=At<int>(node,kNodeCount);
        return n>0 && n<=kReservedClasses ? n : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void BlockLoaded(unsigned char* block) noexcept {
    if(!active.load(std::memory_order_acquire) || !block)return;
    unsigned char* const gs=block-kBlockAt;
    const int classes=ClassCount(gs);
    LoadResult out[kLocalPlayers];
    RestoreLoaded(block,classes,out);
    for(std::size_t p=0;p<kLocalPlayers;++p)
        if(out[p].now.what!=Loaded::kNone)
            Log("SOLDIERSAVE load: player %zu class %d in the file, record class %d: %s, class %d (%d classes)",p+1,out[p].was,
                out[p].now.real,LoadedName(out[p].now.what),out[p].now.cls,classes);
}
}  // namespace

bool SoldierSaveActive() noexcept { return active.load(std::memory_order_acquire); }

bool InstallSoldierSave() noexcept {
    if(!WeaponRecordsActive()) {
        Log("SOLDIERSAVE REFUSED: the weapon records are not relocated, their load completion hook is not there: "
            "no class past the stock four may be saved");
        return false;
    }
    if(!Matches(kQueueArgsAt,kQueueArgs,sizeof(kQueueArgs)) || !Matches(kRefBuiltAt,kRefBuilt,sizeof(kRefBuilt)) ||
       !Matches(kJobSizeArgAt,kJobSizeArg,sizeof(kJobSizeArg)) || !Matches(kJobFieldsAt,kJobFields,sizeof(kJobFields)) ||
       !Matches(kClassCount,kClassCountCode,sizeof(kClassCountCode))) {
        Log("SOLDIERSAVE REFUSED: the save request's job queueing or the class count is not the expected code");
        return false;
    }
    SetSaveBlockLoaded(&BlockLoaded);
    bool changed=false;
    if(!RedirectCall(image+kQueueCall,image+kQueue,reinterpret_cast<void*>(&QueueHook),changed)) {
        SetSaveBlockLoaded(nullptr);
        Log("SOLDIERSAVE REFUSED: the call at EDF+0x%X (to 0x%X) is not the game's (another hook?)%s",kQueueCall,kQueue,
            changed ? ", half written" : "");
        return false;
    }
    active.store(true,std::memory_order_release);
    Log("SOLDIERSAVE installed: MAIN.GST's class past %d written as %d, the real one in class %d slot %d (GS+0x%zX / +0x%zX)",
        kStockClasses-1,kStandIn,kRecordClass,kRecordSlot,kBlockAt+RecordOffset(0),kBlockAt+RecordOffset(1));
    return true;
}
}  // namespace crew
