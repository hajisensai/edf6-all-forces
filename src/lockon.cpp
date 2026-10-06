// Every lock-on weapon's lock (src/lockon.h). Docs: docs/stores-re.md §7, docs/lockon-re.md. All addresses are RVAs into
// EDF.dll TimeDateStamp 0x678CCB46.
//  - The weapon's lock: the list of locks (MSVC std::list: head at +0xC60, its count +0xC68; a node +0 next, +0x10 the
//    entry, +0x18 its control block, +0x20 its hold timer), the lock in progress (+0xC70 entry, +0xC78 control block,
//    +0xC80 progress against LockonTime +0x6D4, +0xCA0 its frames out of the cone), the most locks it takes +0x6DC (the
//    game's: min(AmmoCount, FireBurstCount), or FireCount); an entry +0x10 is its target's lock point. ClearLock
//    0x68FF60(weapon); 0x68FEE0(weapon +0xC70) drops the lock in progress (entry, control block, progress).
//  - The search (0x6963A0, only with no lock in progress and the list not full) hands every candidate in the weapon's
//    cone to the keeper 0x691310(&{entry,ctrl}, result, ctx: +0 weapon), which turns down an entry already in the
//    list (unless the weapon or the target allows repeats) and keeps the least result +0x10 (the depth along the
//    weapon's aim: the nearest in the cone). The detour hands it a copy scored by the plugin's order instead: the
//    angle off the player's view (CameraRay) squared for a weapon the player holds (PlayerLockByView; the jets'
//    stores PlayerJetLockByView), else for a store (the NPC jets) the angle off its nose (result +0 |yaw|, +4 |pitch|);
//    plus kRepeatScore for each time the candidate already is in the list, plus kSkipScore for a target cycled away
//    from. Every other weapon (an NPC's) keeps the stock order. The detour moves its first 17 bytes (mov [rsp+18],r8;
//    mov [rsp+10],rdx; push rbx; push r15; sub rsp,48: no rip-relative) into a trampoline.
//  - Who holds it: weapon +0x120 is its owner: a soldier (one of the four classes' vtables) whose own weapon it is
//    (IsPlayer), or a vehicle, one of whose seats has it among its holders (seat +0xC8, count +0xD8, the weapon at
//    holder +0x10) with the player in it (docs/lockon-re.md §5).
#include "lockon.h"
#include "crew.h"
#include "memory.h"
#include "stores.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kLockList=0xC60,kLockCount=0xC68,kLocking=0xC70,kLockingCtrl=0xC78,kLockProgress=0xC80;
constexpr std::size_t kLockFailed=0xCA0,kLockTime=0x6D4,kLockMost=0x6DC,kWeaponOwner=0x120;
constexpr std::size_t kNodeEntry=0x10,kNodeCtrl=0x18,kEntryPoint=0x10;
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr unsigned kSoldierVts[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};   // Ranger, Wing Diver, Fencer, Air Raider
constexpr std::size_t kClearLock=0x68FF60,kDropLocking=0x68FEE0;
const unsigned char kClearLockSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20};
const unsigned char kDropLockingSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x33,0xFF,0x48,0x8B,0xD9,0x48};
constexpr std::size_t kPick=0x691310,kPickCopied=17;
const unsigned char kPickSig[]={0x4C,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x53,0x41,0x57,0x48,0x83,0xEC,0x48};
constexpr float kSkipScore=100.0f,kRepeatScore=20.0f;   // past any angle squared (pi^2 < 10); a skip past repeats
constexpr ULONGLONG kSkipMs=4000;
constexpr int kMostSkips=16,kMostNodes=64;
using PickFn=std::uint64_t(__fastcall*)(void**,unsigned char*,unsigned char*);
using EntryFn=void(__fastcall*)(void*);
PickFn pickNext=nullptr;
bool clearOk=false,dropOk=false;
// The targets the player cycled away from on one weapon, last in its order until `until`.
struct Skip { const unsigned char* weapon; const void* entries[kMostSkips]; int count; ULONGLONG until; } skip{};

bool Live(const unsigned char* ctrl) noexcept { return ctrl && Readable(ctrl,0x10) && At<std::int32_t>(ctrl,8)>0; }

// Calls `visit(node)` for each node of `w`'s lock list (at most kMostNodes).
template<class F> void EachLock(const unsigned char* w,F visit) noexcept {
    if(!Readable(w+kLockList,0x10))return;
    const auto head=At<const unsigned char*>(w,kLockList);
    const auto count=At<std::uint64_t>(w,kLockCount);
    const unsigned char* node=Readable(head,8) ? At<const unsigned char*>(head,0) : nullptr;
    for(std::uint64_t i=0;i<count && i<kMostNodes && node && node!=head && Readable(node,kNodeCtrl+8);++i) {
        visit(node);
        node=At<const unsigned char*>(node,0);
    }
}

bool IsSoldier(const unsigned char* o) noexcept {
    if(!Readable(o,8))return false;
    const auto vt=At<const unsigned char*>(o,0);
    for(const unsigned v:kSoldierVts)if(vt==image+v)return true;
    return false;
}

bool SeatHolds(unsigned char* seat,const unsigned char* w) noexcept {
    if(!Readable(seat,kSeatWeaponCount+8))return false;
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>16 || !Readable(holders,count*8))return false;
    for(std::uint64_t i=0;i<count;++i)
        if(Readable(holders[i],kHolderWeapon+8) && At<const unsigned char*>(holders[i],kHolderWeapon)==w)return true;
    return false;
}

// The camera's view ray for this frame's picks (CameraRay inverts the view-projection: once a frame, not per candidate).
bool PickView(float* eye,float* dir) noexcept {
    static ULONGLONG at=~0ull;
    static bool ok=false;
    static float e[3],d[3];
    const ULONGLONG ms=GameMs();
    if(ms!=at){at=ms;ok=CameraRay(e,d);}
    if(ok){std::memcpy(eye,e,12);std::memcpy(dir,d,12);}
    return ok;
}

// The angle (rad) of the candidate `ref` (its entry's lock point) off the player's view, or -1: no view, unread.
float ViewAngle(void** ref) noexcept {
    const auto entry=ref ? static_cast<const unsigned char*>(ref[0]) : nullptr;
    float eye[3],dir[3];
    if(!Readable(entry,kEntryPoint+12) || !PickView(eye,dir))return -1.0f;
    const float* p=reinterpret_cast<const float*>(entry+kEntryPoint);
    const float d[3]={p[0]-eye[0],p[1]-eye[1],p[2]-eye[2]};
    const float l=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
    if(!(l>1e-3f))return -1.0f;
    const float c=(d[0]*dir[0]+d[1]*dir[1]+d[2]*dir[2])/l;
    return std::acos(c<-1.0f ? -1.0f : c>1.0f ? 1.0f : c);
}

// Candidate `ref` of weapon `w` (stock result `result`) scored by the plugin's order; false: the stock order.
bool Score(void** ref,const unsigned char* result,const unsigned char* w,float* score) noexcept {
    const bool store=IsStoreWeapon(w),held=PlayerHolds(w);
    const bool byView=held && (store ? Cfg().playerJetLockByView : Cfg().playerLockByView);
    const float view=byView ? ViewAngle(ref) : -1.0f;
    if(view>=0.0f)*score=view*view;
    else if(store){const float yaw=At<float>(result,0),pitch=At<float>(result,4);*score=yaw*yaw+pitch*pitch;}
    else return false;
    const void* const entry=ref ? ref[0] : nullptr;
    EachLock(w,[&](const unsigned char* node){if(At<const void*>(node,kNodeEntry)==entry)*score+=kRepeatScore;});
    if(skip.weapon==w && GameMs()<skip.until)
        for(int i=0;i<skip.count;++i)if(skip.entries[i]==entry){*score+=kSkipScore;break;}
    return std::isfinite(*score);
}

std::uint64_t __fastcall PickHook(void** ref,unsigned char* result,unsigned char* ctx) {
    __try {
        const auto w=Readable(ctx,8) ? At<const unsigned char*>(ctx,0) : nullptr;
        float score=0.0f;
        if(w && Readable(result,0x40) && Score(ref,result,w,&score)) {
            alignas(16) unsigned char copy[0x40];
            std::memcpy(copy,result,sizeof(copy));
            std::memcpy(copy+0x10,&score,4);
            return pickNext(ref,copy,ctx);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return pickNext(ref,result,ctx);
}

// The pick's trampoline: its copied first bytes, then jmp [rip] back past them.
bool HookPick() noexcept {
    unsigned char tramp[kPickCopied+14];
    std::memcpy(tramp,image+kPick,kPickCopied);
    const unsigned char jmp[6]={0xFF,0x25,0,0,0,0};
    std::memcpy(tramp+kPickCopied,jmp,6);
    const auto back=reinterpret_cast<std::uintptr_t>(image+kPick+kPickCopied);
    std::memcpy(tramp+kPickCopied+6,&back,8);
    void* const t=edf::AllocateNearCode(image+kPick,tramp,sizeof(tramp));
    if(!t)return false;
    pickNext=reinterpret_cast<PickFn>(t);
    unsigned char patch[kPickCopied];
    std::memset(patch,0x90,sizeof(patch));
    std::memcpy(patch,jmp,6);
    const auto hook=reinterpret_cast<std::uintptr_t>(&PickHook);
    std::memcpy(patch+6,&hook,8);
    if(edf::PatchCode(image+kPick,kPickSig,patch,sizeof(patch)))return true;
    VirtualFree(t,0,MEM_RELEASE);pickNext=nullptr;
    return false;
}
}  // namespace

bool PlayerHolds(const unsigned char* w) noexcept {
    if(!w || !Readable(w+kWeaponOwner,8))return false;
    const auto owner=At<unsigned char*>(w,kWeaponOwner);
    if(!owner)return false;
    if(IsSoldier(owner))return IsPlayer(owner);
    if(!Readable(owner,kSeats+0x18))return false;
    const unsigned seats=SeatCount(owner);
    for(unsigned i=0;i<seats;++i) {
        unsigned char* const seat=SeatAt(owner,i);
        if(SeatHolds(seat,w))return SeatRider(seat)==Rider::player;
    }
    return false;
}

int WeaponLock(const unsigned char* w,float* point,float* progress) noexcept {
    if(!w || !Readable(w+kLockList,0x48))return 0;
    int got=0;
    EachLock(w,[&](const unsigned char* node) {
        if(got || !Live(At<const unsigned char*>(node,kNodeCtrl)))return;
        const auto entry=At<const unsigned char*>(node,kNodeEntry);
        if(Readable(entry,kEntryPoint+12)){std::memcpy(point,entry+kEntryPoint,12);*progress=1.0f;got=2;}
    });
    if(got)return got;
    const auto entry=At<const unsigned char*>(w,kLocking);
    if(entry && Live(At<const unsigned char*>(w,kLockingCtrl)) && Readable(entry,kEntryPoint+12)) {
        const float t=At<float>(w,kLockTime),p=At<float>(w,kLockProgress);
        std::memcpy(point,entry+kEntryPoint,12);
        *progress=t>0.0f && std::isfinite(p) ? (p/t<1.0f ? (p/t>0.0f ? p/t : 0.0f) : 1.0f) : 0.0f;
        return 1;
    }
    return 0;
}

void ClearWeaponLock(unsigned char* w) noexcept {
    if(clearOk && w)reinterpret_cast<EntryFn>(image+kClearLock)(w);
}

void NextLockTarget(unsigned char* w) noexcept {
    if(!w || !Readable(w+kLockList,0x48))return;
    Skip s{w,{},0,GameMs()+kSkipMs};
    const auto locking=At<const void*>(w,kLocking);
    if(locking && dropOk) {   // a lock in progress: that one dropped, the locks made kept (a multi-lock keeps its count)
        s.entries[s.count++]=locking;
        skip=s;
        reinterpret_cast<EntryFn>(image+kDropLocking)(w+kLocking);
        Put<std::uint32_t>(w,kLockFailed,0);
        return;
    }
    EachLock(w,[&](const unsigned char* node){if(s.count<kMostSkips)s.entries[s.count++]=At<const void*>(node,kNodeEntry);});
    if(locking && s.count<kMostSkips)s.entries[s.count++]=locking;
    skip=s;
    ClearWeaponLock(w);
}

bool InstallLockon() noexcept {
    __try {
        clearOk=Matches(kClearLock,kClearLockSig,sizeof(kClearLockSig));
        dropOk=Matches(kDropLocking,kDropLockingSig,sizeof(kDropLockingSig));
        const bool pickOk=Matches(kPick,kPickSig,sizeof(kPickSig)) && HookPick();
        Log("HOOK lockon pick=%d clearLock=%d dropLocking=%d (the player's weapons lock nearest the view first)",pickOk,
            clearOk,dropOk);
        return pickOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
