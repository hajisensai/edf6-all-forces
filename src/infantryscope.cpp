// A soldier's scoped weapon looked through (src/infantryscope.h).
#include "crew.h"
#include "infantryscope.h"
#include "layout.h"
#include "memory.h"
#include "edf/patch.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
// The soldier's weapon sets (docs/zoom-re.md §1; npcai.cpp): +0x1970 an array of kSetStride, +0x1980 its count, the
// set's weapon **(set +0x40). The soldier's own zoom +0x1D0 (the largest open scope's power), the special-view flag
// +0x224 (a class gadget's own view: then +0x290 is its zoom, not a scope's).
constexpr std::size_t kSets=0x1970,kSetCount=0x1980,kSetStride=0x150,kSetWeapon=0x40;
constexpr std::size_t kHumanZoom=0x1D0,kHumanSpecialView=0x224;
// The weapon: +0x690 SecondaryFire_Type (1 a scope), +0x780 the scope open, +0x784 its power, +0x36C FireInterval,
// +0x8B0 AmmoExplosion.
constexpr std::size_t kScopeType=0x690,kScopeOpen=0x780,kScopePower=0x784,kFireInterval=0x36C,kBlast=0x8B0;
constexpr ULONGLONG kFreshMs=200;   // wall: a cue older than this is gone (the frame stopped publishing)
constexpr int kMostSets=8;

// The stock crosshair (HudPlayer_CrossHair): its draw, slot 3 of its vtable; +0x580 the soldier it is for.
constexpr unsigned kCrossVtable=0x17F6B98,kCrossDraw=0x8039F0;
constexpr std::size_t kCrossDrawSlot=3,kCrossHuman=0x580;
const unsigned char kCrossDrawCode[]={0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xA0};
using CrossDrawFn=void(__fastcall*)(void*,void*,void*);
CrossDrawFn nextCrossDraw=nullptr;
bool installed=false;

struct Published { ObjRef human; InfantryScopeCue cue; ULONGLONG at; };
Published published{};
SRWLOCK lock=SRWLOCK_INIT;

void Publish(const Published& p) noexcept {
    AcquireSRWLockExclusive(&lock);
    published=p;
    ReleaseSRWLockExclusive(&lock);
}

// The weapon whose scope `human` has open, else nullptr.
const unsigned char* OpenScope(const unsigned char* human) noexcept {
    const auto sets=At<const unsigned char*>(human,kSets);
    const auto count=At<std::uint64_t>(human,kSetCount);
    if(!count || count>kMostSets || !Readable(sets,count*kSetStride))return nullptr;
    for(std::uint64_t k=0;k<count;++k) {
        const unsigned char* set=sets+k*kSetStride;
        const auto entry=At<unsigned char* const*>(set,kSetWeapon);
        if(!Readable(entry,8))continue;
        const unsigned char* w=*entry;
        if(!Readable(w,kBlast+4))continue;
        if(At<std::int32_t>(w,kScopeType)==1 && w[kScopeOpen] && At<float>(w,kScopePower)>1.01f)return w;
    }
    return nullptr;
}

// Slot 3 of the stock crosshair: not drawn for the soldier whose scope this plugin draws, on foot (InfantryScopeNow) or
// through a vehicle's sight (hud.cpp HudScopeHoldsCrosshair, scope_hold.h) (the function's own early returns leave
// exactly as much undone: docs in infantryscope.h).
void __fastcall CrossDrawHook(void* self,void* a,void* b) {
    __try {
        if(Readable(self,kCrossHuman+8)) {
            const void* const soldier=At<const void*>(self,kCrossHuman);
            InfantryScopeCue c;
            if(soldier && ((InfantryScopeNow(&c) && soldier==c.human) || HudScopeHoldsCrosshair(soldier)))return;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    nextCrossDraw(self,a,b);
}
}  // namespace

void InfantryScopeFrame(unsigned char* human) noexcept {
    const Config& c=Cfg();
    __try {
        if(!installed || !c.enabled || !c.infantryScope || !human || human!=PlayerHuman() || human[kDead] || !HumanOnFoot(human) ||
           !Readable(human,kSets+0x20) || At<std::int32_t>(human,kHumanSpecialView)==1 || !(At<float>(human,kHumanZoom)>1.01f)) {
            if(published.human)Publish(Published{});
            return;
        }
        const unsigned char* const w=OpenScope(human);
        if(!w){if(published.human)Publish(Published{});return;}
        RoundModel round{};
        const bool rocket=ReadRound(w,&round) && (round.kind==RoundKind::rocket || round.kind==RoundKind::homing);
        const float blast=At<float>(w,kBlast);
        const float power=At<float>(w,kScopePower);
        Published p{};
        p.human=ObjRef::Of(human);
        p.cue=InfantryScopeCue{human,std::isfinite(power) ? power : 1.0f,
                               infantryscope::LookFor(At<std::int32_t>(w,kFireInterval),std::isfinite(blast) ? blast : 0.0f,rocket)};
        p.at=GetTickCount64();
        if(!published.human.Is(human) || published.cue.look!=p.cue.look)
            Log("INFANTRYSCOPE %.1fx, %s",p.cue.zoom,p.cue.look==scope::Look::launcher ? "launcher" : p.cue.look==scope::Look::combat ? "combat optic" : "marksman");
        Publish(p);
    } __except(EXCEPTION_EXECUTE_HANDLER) { Publish(Published{}); }
}

bool InfantryScopeNow(InfantryScopeCue* out) noexcept {
    AcquireSRWLockShared(&lock);
    const Published p=published;
    ReleaseSRWLockShared(&lock);
    if(!p.human || !Cfg().enabled || !Cfg().infantryScope || GetTickCount64()-p.at>kFreshMs || !(p.cue.zoom>1.01f))return false;
    if(out)*out=p.cue;
    return true;
}

bool InstallInfantryScope() noexcept {
    if(installed)return true;
    if(!Matches(kCrossDraw,kCrossDrawCode,sizeof(kCrossDrawCode))) {
        Log("INFANTRYSCOPE the stock crosshair's draw is not where infantryscope.h has it: the stock scope stays");
        return false;
    }
    auto slot=reinterpret_cast<void**>(image+kCrossVtable)+kCrossDrawSlot;
    if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&CrossDrawHook),reinterpret_cast<void**>(&nextCrossDraw))) {
        Log("INFANTRYSCOPE the crosshair's draw could not be chained: the stock scope stays");
        return false;
    }
    installed=true;
    return true;
}

void ResetInfantryScope() noexcept { Publish(Published{}); }
}  // namespace crew
