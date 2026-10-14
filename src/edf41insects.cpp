// EDF4.1's DLC insects on EDF6's classes of the same name (tools/make_edf41_objects.py DLC_INSECT_*): what EDF6's class
// computes differently from 4.1's and no SGO key reaches. Only an object whose SGO carries the marker key is touched;
// EDF6's own bees and spiders go through unchanged. EDF.dll TimeDateStamp 0x678CCB46; EDF41.exe addresses are 4.1's.
//
//  - The queen bee's needle speed (`edf41_needle_base`, GiantBeeQueen02). EDF6's GiantBee is 4.1's class moved by
//    +0x810, with one constant changed: the needle setup (EDF+0x422830, called from the constructor 0x41B66B and the
//    level setup 0x41EBA0 with the level in xmm1) writes the speed [+0x1764] = (L / 400 + 0.5) x 3 (EDF+0x422950
//    divss 400 at 0x1765F64, 0x422958 addss 0.5 at 0x1C36990), x 1.5 for the queen (0x422A2E); 4.1's same setup adds 0.4
//    (EDF41 0x20E85A, 0xAB6550). At the DLC queens' levels that is 15-23% faster and farther-reaching needles. Both call
//    sites go through NeedleHook: after the stock setup, a marked object's speed is scaled by (L / 400 + base) /
//    (L / 400 + 0.5), base the key's value (the converter writes 4.1's 0.4). The rest of the setup (count, life,
//    damage, spread base) is the same in both games.
//  - The Nephila's thread scatter (`edf41_random_speed`, Nephila02L / 03L). SpiderStringBullet01's base constructor
//    (EDF+0x27C1B0) seeds the thread's ten nodes with random velocities scaled by random_speed (0x27C4AF). 4.1 reads it
//    by position, bullet_custom_param[0] (EDF41 0x1A1B90..0x1A1CC7): the DLC's 0.01 flies a tight, straight thread.
//    EDF6 looks it up by name in the variant the bullet holds, and the Nephila always hands it the classic SGO node
//    (EDF+0x4E2818, tag 1), whose by-name lookup always misses (EDF+0x238DF0): every Nephila gets the default 0.5
//    (EDF+0x27C362). The Nephila's bullet spawn (EDF+0x4E3805, call 0x1194280 with the shot parameters at
//    nephila+0x10D0, 0x4E3782) goes through SpawnHook, which hands a marked Nephila's value to this thread for the
//    bullet's constructor; the default load at 0x27C362 jumps to a stub that asks RandomSpeed for the value (the stock
//    0.5 when no marked Nephila is firing). The constructor is shared with SpiderStringBullet02 / 03 (EDF+0x2815C3,
//    0x282617), which the value never reaches.
//
// The keys are read the way the classes read their own (classic SGO): 0x528A0(&object+0xA0, name) gives the 12-byte node
// {type (0 array, 2 float), count, offset of the first child / the float's bits} or null.
#include "crew.h"
#include "memory.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kKeyNode=0x528A0;
constexpr std::size_t kSgoAt=0xA0;

// --- the queen's needle
constexpr std::size_t kNeedleSetup=0x422830;             // (bee, float level): the needle parameters
constexpr std::size_t kNeedleCalls[]={0x41B66B,0x41EBA0};
constexpr std::size_t kNeedleSpeed=0x1764;               // float: the needle speed it writes
constexpr std::size_t kSpeedTerms=0x422950;              // divss xmm0,[400]; addss xmm0,[0.5]
constexpr unsigned char kSpeedTermsBytes[]={0xF3,0x0F,0x5E,0x05,0x0C,0x36,0x34,0x01,0xF3,0x0F,0x58,0x05,0x30,0x40,0x81,0x01};
constexpr std::size_t kSpeedDivisor=0x1765F64;           // 400.0f
constexpr std::size_t kSpeedBase=0x1C36990;              // 0.5f (also the Nephila's random_speed default)

// --- the Nephila's thread
constexpr std::size_t kSpawnCall=0x4E3805;               // call 0x1194280(manager, out, factory, shot parameters)
constexpr std::size_t kSpawn=0x1194280;
constexpr std::size_t kShotAt=0x10D0;                    // the shot parameters in the Nephila (0x4E3782 lea rdi,[rsi+0x10D0])
constexpr unsigned char kShotLea[]={0x48,0x8D,0xBE,0xD0,0x10,0x00,0x00};
constexpr std::size_t kShotLeaAt=0x4E3782;
constexpr unsigned char kShotArg[]={0x4C,0x8B,0xCF};     // 0x4E37EC mov r9,rdi
constexpr std::size_t kShotArgAt=0x4E37EC;
constexpr std::size_t kDefaultLoad=0x27C362;             // movss xmm6,[0.5]; jmp 0x27C391 (both misses: tag 0xFFFF)
constexpr unsigned char kDefaultBytes[]={0xF3,0x0F,0x10,0x35,0x26,0xA6,0x9B,0x01,0xEB,0x25};
constexpr std::size_t kAfterDefault=0x27C3AF;            // where 0x27C391 goes for tag 0xFFFF (no variant to release)
constexpr unsigned char kAfterDefaultBytes[]={0x48,0x8B,0x87,0xF0,0x0A,0x00,0x00};

struct Node {
    std::int32_t type;
    std::int32_t count;
    std::int32_t value;
};
using KeyNodeFn=const Node*(__fastcall*)(void*,const wchar_t*);
using NeedleSetupFn=void(__fastcall*)(void*,float);
using SpawnFn=void*(__fastcall*)(void*,void*,void*,void*);

std::atomic<int> logged{0};
thread_local float pendingRandomSpeed=NAN;   // set while a marked Nephila's bullet is being spawned

void LogLimited(const char* what,const void* self,double value) noexcept {
    if(logged.fetch_add(1)<64)Log("EDF41INSECTS %p %s (%.3f)",self,what,value);
}

// The key `name` of `object` as one float (a one-entry array of a float): true with `out` set; false when it has none.
// A key present in any other shape is logged and treated as absent (the converter writes it checked).
bool KeyFloat(void* object,const wchar_t* name,float& out) noexcept {
    const Node* key=reinterpret_cast<KeyNodeFn>(image+kKeyNode)(static_cast<char*>(object)+kSgoAt,name);
    if(!key)return false;
    const Node* first=key->type==0 && key->count==1
        ? reinterpret_cast<const Node*>(reinterpret_cast<const char*>(key)+key->value) : nullptr;
    if(!first || first->type!=2) {
        LogLimited("marker key malformed: left as EDF6 computes it",object,key->type);
        return false;
    }
    std::memcpy(&out,&first->value,sizeof(out));
    return std::isfinite(out);
}

void __fastcall NeedleHook(void* bee,float level) {
    reinterpret_cast<NeedleSetupFn>(image+kNeedleSetup)(bee,level);
    float base=0.0f;
    if(!KeyFloat(bee,L"edf41_needle_base",base))return;
    const float divisor=*reinterpret_cast<const float*>(image+kSpeedDivisor);
    const float edf6=level/divisor+*reinterpret_cast<const float*>(image+kSpeedBase);
    if(!(edf6>0.0f))return;
    float& speed=*reinterpret_cast<float*>(static_cast<char*>(bee)+kNeedleSpeed);
    const float was=speed;
    speed=was*((level/divisor+base)/edf6);
    LogLimited("needle speed as 4.1's (edf41_needle_base)",bee,speed);
}

void* __fastcall SpawnHook(void* manager,void* out,void* factory,void* shot) {
    float value=0.0f;
    char* const nephila=static_cast<char*>(shot)-kShotAt;
    const bool marked=KeyFloat(nephila,L"edf41_random_speed",value);
    if(marked)pendingRandomSpeed=value;
    void* const bullet=reinterpret_cast<SpawnFn>(image+kSpawn)(manager,out,factory,shot);
    pendingRandomSpeed=NAN;
    return bullet;
}

// Called by the stub at the constructor's default load (rcx = the bullet): the value for xmm6.
float __fastcall RandomSpeed(void* bullet) {
    const float value=pendingRandomSpeed;
    if(std::isnan(value))return *reinterpret_cast<const float*>(image+kSpeedBase);
    LogLimited("thread random_speed as 4.1's (edf41_random_speed)",bullet,value);
    return value;
}

bool HookNeedle() noexcept {
    if(!Matches(kSpeedTerms,kSpeedTermsBytes,sizeof(kSpeedTermsBytes)) ||
       *reinterpret_cast<const float*>(image+kSpeedDivisor)!=400.0f || *reinterpret_cast<const float*>(image+kSpeedBase)!=0.5f) {
        Log("EDF41INSECTS EDF+%zX is not GiantBee's needle speed (L / 400 + 0.5): the queen's needle not hooked",kSpeedTerms);
        return false;
    }
    int done=0;
    for(const std::size_t site:kNeedleCalls) {
        bool changed=false;
        done+=RedirectCall(image+site,image+kNeedleSetup,reinterpret_cast<void*>(&NeedleHook),changed) ? 1 : 0;
    }
    return done==2;
}

bool HookThread() noexcept {
    if(!Matches(kDefaultLoad,kDefaultBytes,sizeof(kDefaultBytes)) || !Matches(kAfterDefault,kAfterDefaultBytes,sizeof(kAfterDefaultBytes)) ||
       !Matches(kShotLeaAt,kShotLea,sizeof(kShotLea)) || !Matches(kShotArgAt,kShotArg,sizeof(kShotArg)) ||
       *reinterpret_cast<const float*>(image+kSpeedBase)!=0.5f) {
        Log("EDF41INSECTS EDF+%zX / +%zX are not the thread bullet's random_speed default and the Nephila's shot: not hooked",
            kDefaultLoad,kSpawnCall);
        return false;
    }
    // mov rcx,rdi; mov rax,RandomSpeed; call rax; movss xmm6,xmm0; jmp [rip+0] -> 0x27C3AF. At 0x27C362 rsp is 16-aligned
    // (7 pushes, sub rsp,0x260): the call sees the ABI's alignment and its home space is [rsp, rsp+0x20), below the
    // constructor's locals at [rsp+0x21]. rax, rcx, rdx, r8..r11 and xmm0..xmm5 are reloaded before use past 0x27C3AF.
    unsigned char stub[3+10+2+4+14];
    unsigned char* p=stub;
    const unsigned char movRcx[]={0x48,0x89,0xF9};
    std::memcpy(p,movRcx,3);p+=3;
    *p++=0x48;*p++=0xB8;
    const auto fn=reinterpret_cast<std::uintptr_t>(&RandomSpeed);
    std::memcpy(p,&fn,8);p+=8;
    *p++=0xFF;*p++=0xD0;
    const unsigned char movss[]={0xF3,0x0F,0x10,0xF0};
    std::memcpy(p,movss,4);p+=4;
    const unsigned char jump[]={0xFF,0x25,0,0,0,0};
    std::memcpy(p,jump,6);p+=6;
    const auto back=reinterpret_cast<std::uintptr_t>(image+kAfterDefault);
    std::memcpy(p,&back,8);
    void* const code=edf::AllocateNearCode(image+kDefaultLoad,stub,sizeof(stub));
    if(!code)return false;
    unsigned char patch[sizeof(kDefaultBytes)];
    const std::int64_t rel=static_cast<char*>(code)-reinterpret_cast<char*>(image+kDefaultLoad+5);
    if(rel<INT32_MIN || rel>INT32_MAX){VirtualFree(code,0,MEM_RELEASE);return false;}
    const std::int32_t rel32=static_cast<std::int32_t>(rel);
    patch[0]=0xE9;
    std::memcpy(patch+1,&rel32,4);
    std::memset(patch+5,0xCC,sizeof(patch)-5);
    bool changed=false;
    if(!RedirectCall(image+kSpawnCall,image+kSpawn,reinterpret_cast<void*>(&SpawnHook),changed)) {
        VirtualFree(code,0,MEM_RELEASE);
        return false;
    }
    return edf::PatchCode(image+kDefaultLoad,kDefaultBytes,patch,sizeof(patch));
}
}  // namespace

bool InstallEdf41Insects() noexcept {
    if(!Cfg().edf41Ai) {
        Log("EDF41INSECTS off (ini Edf41Ai=0)");
        return false;
    }
    __try {
        const bool needle=HookNeedle();
        const bool thread=HookThread();
        Log("EDF41INSECTS hooked: queen needle speed=%d, Nephila thread random_speed=%d",needle ? 1 : 0,thread ? 1 : 0);
        return needle && thread;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("EDF41INSECTS fault while hooking: not hooked");
        return false;
    }
}
}  // namespace crew
