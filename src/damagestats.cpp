// The damage statistics (damagestats.h; README 伤害统计). Every HP change the damage pipeline makes ends in one function,
// 0x547C30(obj, GameDamageInfo*) (docs/damage-stats-re.md §5; the only call 0x54A586): hooked at its head, it reads the
// object's HP and death byte before and after and books the difference against its source and target (damage_stats.h).
//  - The target: the object; an enemy by its SGO name (obj+0x08's resource key; the RTTI class when it has none), the
//    player's side by group (me, my vehicle, the other players, allied soldiers, allied vehicles).
//  - The source: the GDI's attacker (+0x10/+0x18 weak, its team +0x24). A weapon's round carries no weapon (§2.1), so each
//    round a weapon fires is tagged where the fire creates it (0x69799F, the one round spawn of 0x696FD0: its InitParam
//    is the weapon's +0x800 or +0x9E0) with the weapon's own display name (weapon+0x1B0, the game's language) and who fired
//    it (for a vehicle's gun: the seat holding it, so the player's seat counts as theirs). The settling of a round's hits
//    is synchronous (§1.4): its direct hits (0x543920 from 0x232702 / 0x23426F, the queue at core+0x6E0, its GDI at
//    core+0x730) and its blasts (0x542860 from 0x23250E / 0x5425D6 / 0x5427BF, the GDI in rcx) push that GDI on a
//    thread's source stack for their length; 0x547C30 under it finds the round (GDI - 0x870) and its tag, the tag
//    checked against the round's class and owner. No tag (melee, a ram, a derived round, a script's blast, an enemy's
//    attack): the attacker itself names the source (an enemy's kind is its weapon).
// 0x54A586 itself is left alone: subcarrier.cpp checks that its call still reaches 0x547C30 (DamageCallReaches).
#include "crew.h"
#include "edf/host.h"
#include "edf/patch.h"
#include "hudtext.h"
#include "layout.h"
#include "memory.h"
#include "stores.h"
#include <cwchar>

namespace crew {
namespace {
using hudtext::Tr;
using hudtext::Tx;

// EDF.dll (TimeDateStamp 0x678CCB46, docs/damage-stats-re.md).
constexpr unsigned kDamage=0x547C30,kSpawn=0x1194280,kSpawnCall=0x69799F,kSettle=0x543920,kArea=0x542860;
constexpr unsigned kSettleCalls[]={0x232702,0x23426F},kAreaCalls[]={0x23250E,0x5425D6,0x5427BF};
// 0x547C30's first 14 bytes (mov rax,rsp; mov [rax+18h],rbx; push rbp/rsi/rdi/r12/r13): whole instructions, nothing
// rip-relative, so they run as they are from the trampoline.
const unsigned char kDamageHead[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55};
constexpr std::size_t kGdiAttacker=0x10,kGdiAttackerCtrl=0x18,kGdiTeam=0x24;
constexpr std::size_t kQueueGdi=0x50;                       // a round's hit queue (core+0x6E0) to its GDI (core+0x730)
constexpr std::size_t kRoundGdi=0x870;                      // a round to its GDI (core = round+0x140)
constexpr std::size_t kRoundOwner=0xAE8,kRoundOwnerCtrl=0xAF0;   // core+0x9A8 / +0x9B0
constexpr std::size_t kParamOfWeapon[]={0x800,0x9E0};       // the InitParam the fire hands the spawn, in the weapon
constexpr std::size_t kWeaponName=0x1B0;                    // const wchar_t*: the weapon's name in the game's language
// RTTI: the vtable's complete object locator at -8; its type descriptor (+0xC) and class hierarchy (+0x10) as image
// offsets; the hierarchy's base count (+8) and base array (+0xC), each base descriptor's type descriptor at +0.
constexpr std::size_t kColType=0xC,kColHierarchy=0x10,kHierBases=0x8,kHierArray=0xC,kTypeName=0x10;
constexpr std::size_t kTeams=0x20B2978,kTeamRows=0x38,kTeamStride=0x38,kTeamRelation=0x18;   // heli.cpp Relations
constexpr std::int32_t kEnemyRelation=2,kMostTeams=64;

// --- Names ---
// The class name of `o` (".?AV<name>@@" from its vtable's locator), or nullptr.
const char* ClassOf(const void* o) noexcept {
    const auto vt=At<const unsigned char*>(o,0);
    if(!Readable(o,8) || vt<image+8 || vt>=image+edf::kImageSize || !Readable(vt-8,8))return nullptr;
    const auto col=*reinterpret_cast<const unsigned char* const*>(vt-8);
    if(!Readable(col,kColHierarchy+4))return nullptr;
    const std::uint32_t td=At<std::uint32_t>(col,kColType);
    if(!td || td+kTypeName+4>=edf::kImageSize)return nullptr;
    return reinterpret_cast<const char*>(image+td+kTypeName);
}
// Whether `o`'s class is or derives from `rtti` (".?AV<name>@@"): its locator's hierarchy, every base.
bool DerivesFrom(const void* o,const char* rtti) noexcept {
    if(!ClassOf(o))return false;
    const auto col=*reinterpret_cast<const unsigned char* const*>(At<const unsigned char*>(o,0)-8);
    const std::uint32_t hd=At<std::uint32_t>(col,kColHierarchy);
    if(!hd || hd+kHierArray+4>=edf::kImageSize)return false;
    const std::uint32_t bases=At<std::uint32_t>(image+hd,kHierBases),array=At<std::uint32_t>(image+hd,kHierArray);
    if(bases>64 || array+bases*4>=edf::kImageSize)return false;
    for(std::uint32_t i=0;i<bases;++i) {
        const std::uint32_t bcd=At<std::uint32_t>(image+array,4*i);
        if(!bcd || bcd+4>=edf::kImageSize)continue;
        const std::uint32_t td=At<std::uint32_t>(image+bcd,0);
        if(td && td+kTypeName+64<edf::kImageSize && std::strcmp(reinterpret_cast<const char*>(image+td+kTypeName),rtti)==0)return true;
    }
    return false;
}
// `o`'s kind as the page names it: its SGO file without ".SGO" (E501_ANT_RED), else its class (GiantAnt), else "?".
void KindName(const unsigned char* o,wchar_t* out,std::size_t size) noexcept {
    std::size_t n=0;
    const wchar_t* file=WeaponFile(o,&n);   // any scene object's resource key at +0x08, not only a weapon's
    if(file && n>4 && _wcsnicmp(file+n-4,L".SGO",4)==0)n-=4;
    if(file && n){swprintf_s(out,size,L"%.*ls",static_cast<int>(n<size ? n : size-1),file);return;}
    const char* cls=ClassOf(o);
    if(cls && std::strncmp(cls,".?AV",4)==0)cls+=4;
    std::size_t i=0;
    for(;cls && cls[i] && !(cls[i]=='@' && cls[i+1]=='@') && i+1<size;++i)out[i]=static_cast<wchar_t>(static_cast<unsigned char>(cls[i]));
    out[i]=0;
    if(!i)swprintf_s(out,size,L"?");
}
// A weapon's name: its display name (weapon+0x1B0), else its SGO file.
void WeaponName(const unsigned char* w,wchar_t* out,std::size_t size) noexcept {
    const auto name=At<const wchar_t*>(w,kWeaponName);
    if(name && Readable(name,2*sizeof(wchar_t)) && name[0]) {
        std::size_t i=0;
        for(;i+1<size && Readable(name+i,sizeof(wchar_t)) && name[i];++i)out[i]=name[i];
        out[i]=0;
        return;
    }
    KindName(w,out,size);
}

// --- Sides ---
const std::int32_t* Relations(std::int32_t team) noexcept {
    if(team<0 || team>=kMostTeams)return nullptr;
    const auto manager=At<const unsigned char*>(image,kTeams);
    if(!Readable(manager,kTeamRows+8))return nullptr;
    const auto rows=At<const unsigned char*>(manager,kTeamRows);
    if(!Readable(rows+team*kTeamStride,kTeamStride))return nullptr;
    const auto relation=At<const std::int32_t*>(rows+team*kTeamStride,kTeamRelation);
    return Readable(relation,kMostTeams*4) ? relation : nullptr;
}
// Whether team `t` is the player's enemy (the player's team as last seen; the friendly team before that). Unreadable
// relations: any team but the player's.
bool EnemyTeam(std::int32_t t) noexcept {
    const std::int32_t mine=player.at ? player.team : kTeamFriend;
    if(t==mine)return false;
    const std::int32_t* rel=Relations(mine);
    return rel ? t>=0 && t<kMostTeams && rel[t]==kEnemyRelation : true;
}
bool Soldier(const unsigned char* o) noexcept { return DerivesFrom(o,".?AVSoldierBase@@"); }
// Who in vehicle `v` holds weapon `w` (the seat whose holders have it); for no weapon, anyone aboard: a player of this
// machine (the side "my vehicle"), another machine's (another player's), or nobody's (an NPC vehicle).
dmgstat::Side VehicleSide(unsigned char* v,const unsigned char* w) noexcept {
    const unsigned seats=edf::SeatCount(v);
    bool anyMine=false,anyPlayer=false;
    for(unsigned i=0;i<seats;++i) {
        const unsigned char* seat=edf::SeatAt(v,i);
        const bool mine=edf::SeatRider(image,seat)==edf::Rider::player,someone=mine || edf::AnyPlayerIn(image,seat);
        anyMine=anyMine || mine;anyPlayer=anyPlayer || someone;
        const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
        const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
        if(!w || count>16 || !Readable(holders,count*8))continue;
        for(std::uint64_t k=0;k<count;++k) {
            if(!Readable(holders[k],kHolderWeapon+8) || At<const unsigned char*>(holders[k],kHolderWeapon)!=w)continue;
            return mine ? dmgstat::Side::myVehicle : someone ? dmgstat::Side::teammate : dmgstat::Side::support;
        }
    }
    return anyMine ? dmgstat::Side::myVehicle : anyPlayer ? dmgstat::Side::teammate : dmgstat::Side::support;
}
// The friendly side `o` (an attacker or a round's owner, not an enemy) stands for, `w` the weapon it fired if known.
dmgstat::Side SideOf(unsigned char* o,const unsigned char* w) noexcept {
    if(o==PlayerHuman())return dmgstat::Side::me;
    if(VehicleClassOf(o)>=0)return VehicleSide(o,w);
    if(!Soldier(o))return dmgstat::Side::ally;
    if(edf::IsPlayer(o))return dmgstat::Side::me;      // a second local player's too: this machine's
    return edf::IsAnyPlayer(o) ? dmgstat::Side::teammate : dmgstat::Side::squad;
}
// The group the target `o` (of team `team`) is in; `name` what the page calls it.
dmgstat::Group GroupOf(unsigned char* o,std::int32_t team,wchar_t* name,std::size_t size) noexcept {
    using dmgstat::Group;
    Group g=Group::ally;
    if(EnemyTeam(team))g=Group::enemy;
    else if(o==PlayerHuman() || (Soldier(o) && edf::IsPlayer(o)))g=Group::me;
    else if(VehicleClassOf(o)>=0)g=VehicleSide(o,nullptr)==dmgstat::Side::myVehicle ? Group::myVehicle : Group::vehicle;
    else if(Soldier(o) && edf::IsAnyPlayer(o))g=Group::teammate;
    static const Tx kName[]={Tx::statsGroupMe,Tx::statsGroupMyVehicle,Tx::statsGroupTeammate,Tx::statsGroupAlly};
    if(g==Group::enemy || g==Group::vehicle)KindName(o,name,size);   // an enemy's kind, an allied vehicle's model
    else swprintf_s(name,size,L"%ls",Tr(kName[static_cast<int>(g)]));
    return g;
}

// --- The rounds' tags (the game thread's; under tagLock all the same, should a fire run elsewhere) ---
struct Tag { const unsigned char* round; const void* vtable; const void* owner; dmgstat::Side side; wchar_t name[dmgstat::kNameLen]; };
constexpr int kTags=4096;   // two ways per hash: a round lives a few seconds at most, a few hundred in flight
Tag tags[kTags];
SRWLOCK tagLock=SRWLOCK_INIT;
int TagHome(const void* round) noexcept {
    const std::uint64_t h=(reinterpret_cast<std::uintptr_t>(round)>>4)*0x9E3779B97F4A7C15ull;
    return static_cast<int>(h>>52)&(kTags-2);   // an even slot: its pair is it and the next
}
const unsigned char* WeaponOfParam(const unsigned char* param) noexcept {
    for(const std::size_t at:kParamOfWeapon) {
        const unsigned char* w=param-at;
        const char* cls=ClassOf(w);
        if(cls && std::strncmp(cls,".?AVWeapon",10)==0)return w;
    }
    return nullptr;
}
void TagRound(unsigned char* round,const unsigned char* param) noexcept {
    const unsigned char* w=WeaponOfParam(param);
    if(!w || !Readable(round,kRoundOwnerCtrl+8))return;
    Tag t{round,At<const void*>(round,0),At<const void*>(round,kRoundOwner),dmgstat::Side::ally,{}};
    const auto owner=At<unsigned char*>(round,kRoundOwner);
    if(owner && Readable(owner,kTeam+4))t.side=EnemyTeam(At<std::int32_t>(owner,kTeam)) ? dmgstat::Side::enemy : SideOf(owner,w);
    WeaponName(w,t.name,dmgstat::kNameLen);
    AcquireSRWLockExclusive(&tagLock);
    const int home=TagHome(round);
    Tag& slot=tags[home].round==round || !tags[home].round ? tags[home] : tags[home+1];
    slot=t;
    ReleaseSRWLockExclusive(&tagLock);
}
// The tag of the round whose GDI is `gdi`, checked against the round as it is now and the hit's attacker.
bool TagOf(const unsigned char* gdi,const void* attacker,Tag* out) noexcept {
    const unsigned char* round=gdi-kRoundGdi;
    if(!Readable(round,kRoundOwnerCtrl+8))return false;
    bool found=false;
    AcquireSRWLockShared(&tagLock);
    const int home=TagHome(round);
    for(int i=home;i<home+2 && !found;++i) {
        const Tag& t=tags[i];
        found=t.round==round && t.vtable==At<const void*>(round,0) && t.owner==At<const void*>(round,kRoundOwner) && t.owner==attacker;
        if(found)*out=t;
    }
    ReleaseSRWLockShared(&tagLock);
    return found;
}

// --- The settling's source stack (per thread; nested: a kill's blast settles inside the hit that made it) ---
thread_local const unsigned char* sourceStack[8];
thread_local int sourceDepth=0;
struct Settling {
    explicit Settling(const unsigned char* gdi) noexcept { if(sourceDepth<8)sourceStack[sourceDepth]=gdi;++sourceDepth; }
    ~Settling() { --sourceDepth; }
};
const unsigned char* Source() noexcept { return sourceDepth>0 && sourceDepth<=8 ? sourceStack[sourceDepth-1] : nullptr; }

// --- The book and the page (under lock: the game thread books and clicks, the draw thread reads and lays out) ---
SRWLOCK lock=SRWLOCK_INIT;
dmgstat::Book book;
dmgstat::View view;
ULONGLONG missionAt=0;
constexpr int kTargets=200;
float uiRect[kTargets*4];
int uiCode[kTargets],uiCount=0,uiRows=0,uiVisible=1;
// How the hits were named (under lock): by a round's tag, by a friendly attacker (no tag), by an enemy; the last summary.
std::uint32_t namedByTag=0,namedByAttacker=0,namedByEnemy=0;
ULONGLONG summaryAt=0;

// One log line of the book (under lock): its tables, the totals, the three weapons that dealt the most, how hits were named.
void Summary(const char* why) noexcept {
    float dealt=0.0f,ff=0.0f,healed=0.0f,taken=0.0f;
    for(int s=0;s<dmgstat::kSides;++s){dealt+=book.dealt[s];ff+=book.friendlyFire[s];healed+=book.healGiven[s];}
    for(const float d:book.taken)taken+=d;
    int top[3]={-1,-1,-1};
    for(int s=0;s<book.sources;++s) {
        if(!dmgstat::Friendly(book.source[s].side))continue;
        for(int k=0;k<3;++k) {
            if(top[k]>=0 && book.source[top[k]].damage>=book.source[s].damage)continue;
            for(int m=2;m>k;--m)top[m]=top[m-1];
            top[k]=s;break;
        }
    }
    char names[3][3*dmgstat::kNameLen]{};
    for(int k=0;k<3;++k)
        if(top[k]>=0)WideCharToMultiByte(CP_UTF8,0,book.source[top[k]].name.text,-1,names[k],sizeof(names[k]),nullptr,nullptr);
    Log("DAMAGESTATS %s: %d sources, %d targets; dealt %.0f, friendly fire %.0f, taken %.0f, healed %.0f; top [%s] %.0f, [%s] %.0f, "
        "[%s] %.0f; named by tag %u, by attacker %u, enemy %u",why,book.sources,book.targets,dealt,ff,taken,healed,names[0],
        top[0]>=0 ? book.source[top[0]].damage : 0.0f,names[1],top[1]>=0 ? book.source[top[1]].damage : 0.0f,names[2],
        top[2]>=0 ? book.source[top[2]].damage : 0.0f,namedByTag,namedByAttacker,namedByEnemy);
}

void Record(unsigned char* obj,const unsigned char* gdi,float hpBefore,bool deadBefore) noexcept {
    __try {
        if(!Readable(obj,kHp+4) || !Readable(gdi,kGdiTeam+4))return;
        const float hp=At<float>(obj,kHp);
        const float change=hpBefore-hp;
        if(!std::isfinite(change) || change==0.0f)return;
        // The source: the attacker (alive), the round's tag when it came from a weapon's round.
        const auto ctrl=At<const unsigned char*>(gdi,kGdiAttackerCtrl);
        unsigned char* attacker=ctrl && Readable(ctrl,0x10) && At<std::int32_t>(ctrl,0x8)>0 ? At<unsigned char*>(gdi,kGdiAttacker) : nullptr;
        const std::int32_t team=At<std::int32_t>(gdi,kGdiTeam);
        wchar_t source[dmgstat::kNameLen],target[dmgstat::kNameLen];
        dmgstat::Side side;
        Tag tag;
        int named=0;   // 0 tag, 1 a friendly attacker, 2 an enemy
        const unsigned char* settling=Source();
        if(settling && attacker && TagOf(settling,attacker,&tag)){side=tag.side;wcscpy_s(source,tag.name);}
        else if(EnemyTeam(team)) {
            named=2;
            side=dmgstat::Side::enemy;
            if(attacker)KindName(attacker,source,_countof(source));
            else swprintf_s(source,L"%ls",Tr(Tx::statsUnknownEnemy));
        } else {
            named=1;
            side=attacker ? SideOf(attacker,nullptr) : dmgstat::Side::ally;
            // No weapon to name: a vehicle by its model (a ram, a beam), anything else as unidentified.
            if(attacker && VehicleClassOf(attacker)>=0)KindName(attacker,source,_countof(source));
            else swprintf_s(source,L"%ls",Tr(Tx::statsUnknownWeapon));
        }
        const dmgstat::Group group=GroupOf(obj,At<std::int32_t>(obj,kTeam),target,_countof(target));
        const dmgstat::Hit h{side,source,group,target,change>0.0f ? change : -change,change<0.0f,!deadBefore && obj[kDead]!=0,
                             static_cast<std::uint32_t>(GameMs()-missionAt)};
        AcquireSRWLockExclusive(&lock);
        dmgstat::Add(book,h,Tr(Tx::statsOther));
        ++(named==0 ? namedByTag : named==1 ? namedByAttacker : namedByEnemy);
        const ULONGLONG now=GetTickCount64();
        if(Cfg().debug && now-summaryAt>=10000){summaryAt=now;Summary("so far");}
        ReleaseSRWLockExclusive(&lock);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// --- The hooks ---
using DamageFn=std::uintptr_t(__fastcall*)(unsigned char*,unsigned char*,std::uintptr_t,std::uintptr_t);
using SpawnFn=unsigned char*(__fastcall*)(void*,void*,void*,unsigned char*);
using SettleFn=std::uintptr_t(__fastcall*)(unsigned char*,void*,void*,std::uintptr_t);
using AreaFn=void*(__fastcall*)(unsigned char*,void*,void*,float,std::uint32_t,void*,void*);
DamageFn damageNext=nullptr;
SpawnFn spawnNext=nullptr;
SettleFn settleNext=nullptr;
AreaFn areaNext=nullptr;

struct Before { float hp; bool dead; bool ok; };
Before Read(const unsigned char* obj) noexcept {
    __try {
        if(Readable(obj,kHp+4))return Before{At<float>(obj,kHp),obj[kDead]!=0,true};
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return Before{0.0f,false,false};
}
std::uintptr_t __fastcall DamageHook(unsigned char* obj,unsigned char* gdi,std::uintptr_t r8,std::uintptr_t r9) noexcept {
    const bool on=Cfg().damageStats;
    const Before b=on ? Read(obj) : Before{};
    const std::uintptr_t r=damageNext(obj,gdi,r8,r9);
    if(on && b.ok)Record(obj,gdi,b.hp,b.dead);
    return r;
}
unsigned char* __fastcall SpawnHook(void* mgr,void* matrix,void* factory,unsigned char* param) noexcept {
    unsigned char* const round=spawnNext(mgr,matrix,factory,param);
    if(round && param && Cfg().damageStats) {
        __try { TagRound(round,param); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    return round;
}
std::uintptr_t __fastcall SettleHook(unsigned char* queue,void* flag,void* hitSet,std::uintptr_t r9) noexcept {
    const Settling s(queue+kQueueGdi);
    return settleNext(queue,flag,hitSet,r9);
}
void* __fastcall AreaHook(unsigned char* gdi,void* flag,void* hitSet,float radius,std::uint32_t frame,void* ignore,void* list) noexcept {
    const Settling s(gdi);
    return areaNext(gdi,flag,hitSet,radius,frame,ignore,list);
}

// 0x547C30's head: a 14-byte absolute jump to the hook; its bytes and a jump back in the trampoline (heli.cpp's way).
bool HookDamage() noexcept {
    if(!Matches(kDamage,kDamageHead,sizeof(kDamageHead)))return false;
    unsigned char trampoline[sizeof(kDamageHead)+14];
    std::memcpy(trampoline,kDamageHead,sizeof(kDamageHead));
    const unsigned char jump[6]={0xFF,0x25,0,0,0,0};
    std::memcpy(trampoline+sizeof(kDamageHead),jump,6);
    const auto back=reinterpret_cast<std::uintptr_t>(image+kDamage+sizeof(kDamageHead));
    std::memcpy(trampoline+sizeof(kDamageHead)+6,&back,8);
    void* const code=edf::AllocateNearCode(image+kDamage,trampoline,sizeof(trampoline));
    if(!code)return false;
    unsigned char patch[sizeof(kDamageHead)];
    std::memcpy(patch,jump,6);
    const auto hook=reinterpret_cast<std::uintptr_t>(&DamageHook);
    std::memcpy(patch+6,&hook,8);
    damageNext=reinterpret_cast<DamageFn>(code);
    if(edf::PatchCode(image+kDamage,kDamageHead,patch,sizeof(patch)))return true;
    damageNext=nullptr;VirtualFree(code,0,MEM_RELEASE);
    return false;
}
// The call at `site` (to `target`) through `hook`; `next` the stock target. Counts the sites done.
template<class Fn> int Redirect(unsigned site,unsigned target,void* hook,Fn* next) noexcept {
    bool changed=false;
    *next=reinterpret_cast<Fn>(image+target);
    return RedirectCall(image+site,image+target,hook,changed) ? 1 : 0;
}
}  // namespace

bool InstallDamageStats() noexcept {
    __try {
        if(!HookDamage()){Log("HOOK damageStats: 0x547C30's head is not the stock one: the statistics are off");return false;}
        const int spawn=Redirect(kSpawnCall,kSpawn,reinterpret_cast<void*>(&SpawnHook),&spawnNext);
        int settle=0,area=0;
        for(const unsigned site:kSettleCalls)settle+=Redirect(site,kSettle,reinterpret_cast<void*>(&SettleHook),&settleNext);
        for(const unsigned site:kAreaCalls)area+=Redirect(site,kArea,reinterpret_cast<void*>(&AreaHook),&areaNext);
        Log("HOOK damageStats damage=1 roundTags=%d settle=%d/2 blast=%d/3 (no tag: a hit is named by its attacker)",spawn,settle,area);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void ResetDamageStats() noexcept {
    AcquireSRWLockExclusive(&tagLock);
    std::memset(tags,0,sizeof(tags));
    ReleaseSRWLockExclusive(&tagLock);
    AcquireSRWLockExclusive(&lock);
    if(book.sources || book.targets)Summary("the mission before");
    namedByTag=namedByAttacker=namedByEnemy=0;
    const std::uint32_t serial=book.serial;
    dmgstat::Clear(book);
    book.serial=serial+1;   // never back to a number the draw's copy may hold: the new book is copied
    view.open=false;view.pick=-1;view.scroll=0;   // the tab and the scope stay as the player left them
    missionAt=GameMs();
    uiCount=0;
    ReleaseSRWLockExclusive(&lock);
}

bool DamageStatsRead(dmgstat::Book* out,bool* fresh,dmgstat::View* v,std::uint32_t* missionMs) noexcept {
    if(!Cfg().damageStats)return false;
    AcquireSRWLockShared(&lock);
    *fresh=out->serial!=book.serial;
    if(*fresh)std::memcpy(out,&book,sizeof(book));
    *v=view;
    *missionMs=static_cast<std::uint32_t>(GameMs()-missionAt);
    ReleaseSRWLockShared(&lock);
    return true;
}

void DamageStatsUi(const float* rects,const int* codes,int n,int rows,int visible) noexcept {
    n=rects && codes ? (n<0 ? 0 : n>kTargets ? kTargets : n) : 0;
    AcquireSRWLockExclusive(&lock);
    std::memcpy(uiRect,rects ? rects : uiRect,sizeof(float)*4*static_cast<std::size_t>(n));
    std::memcpy(uiCode,codes ? codes : uiCode,sizeof(int)*static_cast<std::size_t>(n));
    uiCount=n;uiRows=rows;uiVisible=visible>0 ? visible : 1;
    ReleaseSRWLockExclusive(&lock);
}

int DamageStatsUiAt(float x,float y) noexcept {
    if(!Cfg().damageStats)return 0;
    int code=0;
    AcquireSRWLockShared(&lock);
    for(int i=uiCount-1;i>=0 && !code;--i) {   // the last drawn on top
        const float* r=uiRect+4*i;
        if(x>=r[0] && x<r[2] && y>=r[1] && y<r[3])code=uiCode[i];
    }
    ReleaseSRWLockShared(&lock);
    return code;
}

void DamageStatsClick(int code) noexcept {
    AcquireSRWLockExclusive(&lock);
    dmgstat::Click(view,code,uiRows,uiVisible);
    ReleaseSRWLockExclusive(&lock);
}

bool DamageStatsShown() noexcept {
    if(!Cfg().damageStats)return false;
    AcquireSRWLockShared(&lock);
    const bool shown=view.open;
    ReleaseSRWLockShared(&lock);
    return shown;
}

void DamageStatsWheel(int notches) noexcept {
    AcquireSRWLockExclusive(&lock);
    dmgstat::Scroll(view,notches,uiRows,uiVisible);
    ReleaseSRWLockExclusive(&lock);
}

void DamageStatsShow(bool shown) noexcept {
    AcquireSRWLockExclusive(&lock);
    if(shown && !view.open)Summary("page opened");
    view.open=shown;
    if(!shown)uiCount=0;
    ReleaseSRWLockExclusive(&lock);
}
}  // namespace crew
