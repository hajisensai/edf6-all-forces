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
#include "soldier_factory.h"
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "scene.h"
#include "hud.h"
#include "memory.h"
#include "salvo_volley.h"
#include "stores.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <initializer_list>

namespace crew {
namespace {
constexpr std::size_t kLockList=0xC60,kLockCount=0xC68,kLocking=0xC70,kLockingCtrl=0xC78,kLockProgress=0xC80;
constexpr std::size_t kLockFailed=0xCA0,kLockTime=0x6D4,kLockMost=0x6DC,kWeaponOwner=0x120,kWeaponNode=0x08;
constexpr std::size_t kLockSoundTimer=0xCB8;
constexpr std::size_t kNodeEntry=0x10,kNodeCtrl=0x18,kEntryPoint=0x10;
// The entry is the lock-target registry's record (heli.cpp kTargetObject / kTargetAim: object +0x8, aim point +0x10).
constexpr std::size_t kEntryObject=0x8;
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr unsigned kSoldierVts[]={0x17CDF28,0x17D0FF8,0x17CF5B8,0x17CF100};   // Ranger, Wing Diver, Fencer, Air Raider
constexpr std::size_t kClearLock=0x68FF60,kDropLocking=0x68FEE0,kCancelLocking=0x695150;
const unsigned char kClearLockSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20};
const unsigned char kDropLockingSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x33,0xFF,0x48,0x8B,0xD9,0x48};
// Sends the stock type-6 cancellation (the timeout path calls it at 0x6964E6, before dropping the local entry).
const unsigned char kCancelLockingSig[]={0x40,0x53,0x48,0x81,0xEC,0x40,0x06,0x00,0x00,0x48,0x8B,0x05,0xF8,0x9E,0x95,0x01};
constexpr std::size_t kPick=0x691310,kPickCopied=17;
const unsigned char kPickSig[]={0x4C,0x89,0x44,0x24,0x18,0x48,0x89,0x54,0x24,0x10,0x53,0x41,0x57,0x48,0x83,0xEC,0x48};
constexpr float kSkipScore=100.0f,kRepeatScore=20.0f;   // past any angle squared (pi^2 < 10); a skip past repeats
constexpr ULONGLONG kSkipMs=4000;
constexpr int kMostSkips=16,kMostNodes=64;
using PickFn=std::uint64_t(__fastcall*)(void**,unsigned char*,unsigned char*);
using EntryFn=void(__fastcall*)(void*);
PickFn pickNext=nullptr;
bool clearOk=false,dropOk=false;
// The shot calls (call [rax+0x80]: the weapon's slot 16, Weapon_VehicleShoot's 0x6B3640) of the fire start (0x690BB0)
// and of the burst step (0x6940BC), each checked with the instructions that set up its arguments.
constexpr std::size_t kStartShot=0x690C7B,kStartShotCall=15,kBurstShot=0x6940DF,kBurstShotCall=22,kShotSlot=0x80/8;
const unsigned char kStartShotSig[]={0x8B,0x93,0x48,0x02,0x00,0x00,0x48,0x8B,0x03,0x2B,0x93,0xE8,0x0B,0x00,0x00,
                                     0xFF,0x90,0x80,0x00,0x00,0x00};
const unsigned char kBurstShotSig[]={0xFF,0xC8,0x89,0x86,0x18,0x0E,0x00,0x00,0x48,0x8B,0x06,0x8B,0x96,0x48,0x02,0x00,
                                     0x00,0x2B,0xD1,0x48,0x8B,0xCE,0xFF,0x90,0x80,0x00,0x00,0x00};
const unsigned char kShotCall[]={0xFF,0x90,0x80,0x00,0x00,0x00};
constexpr std::size_t kAmmoCount=0x248,kRoundsLeft=0xBE8,kBurstLeft=0xE18,kLockCursor=0xDC0,kLockShare=0x6B8;
constexpr std::size_t kLockKept=0x6BC,kShotBlocked=0x144;
static_assert(salvo::kRounds==static_cast<int>(kSalvoLockLimit),"the salvo locks at most one target a round");
// The targets the player cycled away from on one weapon, last in its order until `until`.
struct Skip { const unsigned char* weapon; const void* entries[kMostSkips]; int count; ULONGLONG until; } skip{};

bool Live(const unsigned char* ctrl) noexcept { return ctrl && Readable(ctrl,0x10) && At<std::int32_t>(ctrl,8)>0; }

// Calls `visit(node)` for each node of `w`'s lock list (at most kMostNodes).
template<class F> void EachLock(const unsigned char* w,F visit,unsigned limit=kMostNodes) noexcept {
    if(!Readable(w+kLockList,0x10))return;
    const auto head=At<const unsigned char*>(w,kLockList);
    const auto count=At<std::uint64_t>(w,kLockCount);
    const unsigned char* node=Readable(head,8) ? At<const unsigned char*>(head,0) : nullptr;
    for(std::uint64_t i=0;i<count && i<limit && node && node!=head && Readable(node,kNodeCtrl+8);++i) {
        visit(node);
        node=At<const unsigned char*>(node,0);
    }
}

const void* LockEnemy(const unsigned char* entry) noexcept {
    if(!Readable(entry,kEntryObject+8))return entry;
    if(const auto enemy=At<const void*>(entry,kEntryObject))return enemy;
    return entry;
}

struct SalvoAllocation {
    struct Enemy { const void* object; unsigned rounds; };
    Enemy enemies[kSalvoLockLimit]{};
    int count=0;

    unsigned RoundsFor(const void* object) const noexcept {
        for(int i=0;i<count;++i)if(enemies[i].object==object)return enemies[i].rounds;
        return 0;
    }
};

SalvoAllocation AllocatedLocks(const unsigned char* weapon) noexcept {
    SalvoAllocation locks;
    EachLock(weapon,[&](const unsigned char* node) {
        if(!Live(At<const unsigned char*>(node,kNodeCtrl)))return;
        const void* const enemy=LockEnemy(At<const unsigned char*>(node,kNodeEntry));
        if(!enemy)return;
        for(int i=0;i<locks.count;++i)if(locks.enemies[i].object==enemy){++locks.enemies[i].rounds;return;}
        locks.enemies[locks.count++]={enemy,1};
    },kSalvoLockLimit);
    return locks;
}

bool IsSoldier(const unsigned char* o) noexcept {
    if(!Readable(o,8))return false;
    const auto vt=At<const unsigned char*>(o,0);
    for(const unsigned v:kSoldierVts)if(vt==image+v)return true;
    return soldier::IsZwVtable(vt);   // classes 4 / 5 (their own vtables, src/soldier_factory.h)
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

// What Score asks of the weapon, not of the candidate: whether it is a store (its SGO) and the player holds it (its
// owner, the seat's rider). The pick runs Score once a candidate (hundreds in a cone), so they are read once a weapon a
// frame. Keyed by the weapon's resource node and owner too: another weapon at a freed one's address, or the weapon
// changing hands, in the same frame reads afresh. The pick runs on the game thread only (the weapon's update).
struct WeaponFacts {
    const unsigned char* weapon;
    const void* node;
    const void* owner;
    ULONGLONG frame;
    bool store,held,salvo;
    SalvoAllocation locks;
};
WeaponFacts facts{nullptr,nullptr,nullptr,~0ull,false,false,false,{}};
const WeaponFacts& FactsOf(const unsigned char* w) noexcept {
    if(!Readable(w,kWeaponOwner+8)) {
        facts={nullptr,nullptr,nullptr,~0ull,false,false,false,{}};
        return facts;
    }
    const ULONGLONG frame=GameFrame();
    const auto node=At<const void*>(w,kWeaponNode),owner=At<const void*>(w,kWeaponOwner);
    if(facts.weapon!=w || facts.node!=node || facts.owner!=owner || facts.frame!=frame) {
        const auto* const spec=StoreOf(w);
        const bool salvo=IsSalvoStore(spec);
        facts={w,node,owner,frame,spec!=nullptr,PlayerHolds(w),salvo,{}};
        if(salvo)facts.locks=AllocatedLocks(w);
    }
    return facts;
}

// Candidate `ref` of weapon `w` (stock result `result`) scored by the plugin's order; false: the stock order.
bool Score(void** ref,const unsigned char* result,const unsigned char* w,float* score) noexcept {
    const WeaponFacts& f=FactsOf(w);
    if(!f.store && !f.held)return false;
    const bool byView=f.held && (f.store ? Cfg().playerJetLockByView : Cfg().playerLockByView);
    const float view=byView ? ViewAngle(ref) : -1.0f;
    if(view>=0.0f)*score=view*view;
    else if(f.store){const float yaw=At<float>(result,0),pitch=At<float>(result,4);*score=yaw*yaw+pitch*pitch;}
    else return false;
    const auto* const entry=ref ? static_cast<const unsigned char*>(ref[0]) : nullptr;
    if(f.salvo && std::isfinite(*score)) {
        // All native in-cone candidates receive their first round before a repeat. Group the lock points of one
        // enemy together, and use the aim only to break ties inside the same round count.
        *score=static_cast<float>(f.locks.RoundsFor(LockEnemy(entry)))+*score/(1.0f+*score);
        return true;
    }
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

// The salvo's volleys (salvo_volley.h). Every weapon of the base class shoots through the two patched calls, so the
// test is cheap first: the salvo's magazine, distribution 0 (a node a shot) with the nodes kept (AutoTimeOut 0: the
// cursor +0xDC0 picks the node), then its SGO.
using ShotFn=void(__fastcall*)(unsigned char*,unsigned);
bool IsSalvoWeapon(const unsigned char* w) noexcept {
    __try {
        return Readable(w+kLockShare,8) && At<std::int32_t>(w,kAmmoCount)==salvo::kRounds && At<std::int32_t>(w,kLockShare)==0 &&
               At<unsigned char>(w,kLockKept)!=0 && IsSalvoStore(StoreOf(w));
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// The live lock nodes numbered by enemy (salvo::NumberEnemies), read once a volley: the shots never take nodes off a
// list whose nodes are kept, and the search only runs in the weapon's update. -1 enemies: unread.
struct VolleyTargets { int enemyOf[kSalvoLockLimit]; int nodes,enemies,count; };
void ReadTargets(const unsigned char* w,VolleyTargets* t) noexcept {
    t->nodes=t->enemies=0;t->count=-1;
    __try {
        const void* objects[kSalvoLockLimit];
        EachLock(w,[&](const unsigned char* node) {
            objects[t->nodes++]=Live(At<const unsigned char*>(node,kNodeCtrl)) ? LockEnemy(At<const unsigned char*>(node,kNodeEntry)) : nullptr;
        },kSalvoLockLimit);
        t->enemies=salvo::NumberEnemies(objects,t->nodes,t->enemyOf);
        t->count=static_cast<int>(At<std::uint64_t>(w,kLockCount));
    } __except(EXCEPTION_EXECUTE_HANDLER){t->nodes=t->enemies=0;t->count=-1;}
}

// Up to kVolley rounds now, each through the weapon's own shot (whatever its slot 16 holds, sends included). Before
// each: the burst count left after it (the shot times the next by it: FireBurstInterval while rounds are left, else
// FireInterval) and the cursor at its node (the list's end, past the last node: an unguided round, when no live node).
// After: the burst runs on until the magazine is empty, however many locks it was started with.
void SalvoVolley(unsigned char* w,unsigned index,ShotFn shot) noexcept {
    VolleyTargets t;
    ReadTargets(w,&t);
    const int capacity=At<std::int32_t>(w,kAmmoCount);
    for(int n=0;n<salvo::kVolley;++n) {
        const int left=At<std::int32_t>(w,kRoundsLeft);
        if(left<=0 || (n>0 && At<std::int32_t>(w,kShotBlocked)!=0))break;
        const int round=n ? capacity-left : static_cast<int>(index);   // the stock's own: capacity - left too
        Put<std::int32_t>(w,kBurstLeft,left-1);
        if(t.count>=0) {
            const int node=salvo::SalvoNode(round,t.enemyOf,t.nodes,t.enemies);
            Put<std::int32_t>(w,kLockCursor,node>=0 ? node : t.count);
        }
        shot(w,static_cast<unsigned>(round));
        if(At<std::int32_t>(w,kRoundsLeft)>=left)break;   // not taken
    }
    const int left=At<std::int32_t>(w,kRoundsLeft);
    Put<std::int32_t>(w,kBurstLeft,left>0 ? left : 0);
}

void __fastcall ShotHook(unsigned char* w,unsigned index) {
    const auto shot=reinterpret_cast<ShotFn>(At<void* const*>(w,0)[kShotSlot]);
    if(IsSalvoWeapon(w))SalvoVolley(w,index,shot);
    else shot(w,index);
}

// Both calls become call [rip+disp] (FF 15, the same 6 bytes) through one pointer near them.
bool HookVolleys() noexcept {
    if(!Matches(kStartShot,kStartShotSig,sizeof(kStartShotSig)) || !Matches(kBurstShot,kBurstShotSig,sizeof(kBurstShotSig)))
        return false;
    const auto hook=reinterpret_cast<std::uintptr_t>(&ShotHook);
    unsigned char* const cell=static_cast<unsigned char*>(
        edf::AllocateNearCode(image+kStartShot,reinterpret_cast<const unsigned char*>(&hook),sizeof(hook)));
    if(!cell)return false;
    bool ok=true;
    for(unsigned char* const at:{image+kStartShot+kStartShotCall,image+kBurstShot+kBurstShotCall}) {
        const std::int64_t disp=cell-(at+sizeof(kShotCall));
        if(disp<INT32_MIN || disp>INT32_MAX){ok=false;break;}
        const std::int32_t rel=static_cast<std::int32_t>(disp);
        unsigned char call[sizeof(kShotCall)]={0xFF,0x15};
        std::memcpy(call+2,&rel,4);
        ok=edf::PatchCode(at,kShotCall,call,sizeof(call)) && ok;
    }
    return ok;   // the cell stays: a site already patched calls through it
}
}  // namespace

void LogSalvoLocks(const unsigned char* weapon) noexcept {
    const auto locks=AllocatedLocks(weapon);
    if(!locks.count)return;
    unsigned total=0,fewest=kSalvoLockLimit,most=0;
    for(int i=0;i<locks.count;++i) {
        const unsigned rounds=locks.enemies[i].rounds;
        total+=rounds;
        if(rounds<fewest)fewest=rounds;
        if(rounds>most)most=rounds;
    }
    Log("JET salvo allocation: %u missiles across %d enemies, %u-%u per enemy",total,locks.count,fewest,most);
}

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

const void* WeaponLockObject(const unsigned char* w) noexcept {
    if(!w || !Readable(w+kLockList,0x48))return nullptr;
    const void* got=nullptr;
    EachLock(w,[&](const unsigned char* node) {
        if(got || !Live(At<const unsigned char*>(node,kNodeCtrl)))return;
        const auto entry=At<const unsigned char*>(node,kNodeEntry);
        if(Readable(entry,kEntryObject+8))got=At<const void*>(entry,kEntryObject);
    });
    return got;
}

const void* WeaponLockingObject(const unsigned char* w) noexcept {
    if(!w || !Readable(w+kLockList,0x48))return nullptr;
    const auto entry=At<const unsigned char*>(w,kLocking);
    if(entry && Live(At<const unsigned char*>(w,kLockingCtrl)) && Readable(entry,kEntryObject+8))return At<const void*>(entry,kEntryObject);
    return nullptr;
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
        // The remote copy keeps its own in-progress entry at +0xC88. Dropping only +0xC70 locally leaves that copy
        // reporting a lock on the old target when no replacement is found. Follow the stock timeout's order.
        reinterpret_cast<EntryFn>(image+kCancelLocking)(w);
        reinterpret_cast<EntryFn>(image+kDropLocking)(w+kLocking);
        Put<std::uint32_t>(w,kLockFailed,0);
        Put<std::uint32_t>(w,kLockSoundTimer,0);
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
        // Partial cancellation is available only with both halves of the stock cancellation path verified.
        dropOk=Matches(kDropLocking,kDropLockingSig,sizeof(kDropLockingSig)) &&
               Matches(kCancelLocking,kCancelLockingSig,sizeof(kCancelLockingSig));
        const bool pickOk=Matches(kPick,kPickSig,sizeof(kPickSig)) && HookPick();
        const bool volleyOk=HookVolleys();
        Log("HOOK lockon pick=%d clearLock=%d dropLocking=%d volleys=%d (the player's weapons lock nearest the view first; "
            "the salvo fires %d rounds every %d frames)",pickOk,clearOk,dropOk,volleyOk,salvo::kVolley,salvo::kVolleyGap);
        return pickOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
