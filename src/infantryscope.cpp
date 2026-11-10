// A soldier's scoped weapon looked through (src/infantryscope.h).
#include "crew.h"
#include "enemy_control.h"
#include "infantryscope.h"
#include "firstperson.h"
#include "scope_pip.h"
#include "scope_pip_gpu.h"
#include "thermal.h"
#include "layout.h"
#include "memory.h"
#include "edf/patch.h"
#include <atomic>
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

// The picture in the picture's camera (scope_pip.h; docs/zoom-re.md §1-§2, H): CharacterGhostCamera's step, slot 4 of
// vtable 0x1768C10, writes cam+0x24 = pi/4 / cam+0x410 at 0xF8906 on its target branch; the projection and the Umbra
// frustum read +0x24 later. Chained after sightzoom.cpp's (the same slot; that one acts on a vehicle seat, this one on
// foot only). The target: +0x350 / +0x360 (camera-re §3).
constexpr unsigned kCamVtable=0x1768C10,kCamStep=0xF86A0,kFovWrite=0xF8906;
constexpr std::size_t kCamStepSlot=4,kCamTargetRef=0x350,kCamTarget=0x360,kCamFov=0x24,kCamZoom=0x410;
const unsigned char kCamStepCode[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x90};
const unsigned char kFovWriteCode[]={0xF3,0x0F,0x10,0x05,0x12,0xD1,0x66,0x01,0xF3,0x0F,0x5E,0x87,0x10,0x04,0x00,0x00,0xF3,0x0F,0x11,0x47,0x24};
using CamStepFn=void(__fastcall*)(void*,void*);
CamStepFn nextCamStep=nullptr;
bool camHooked=false;
// The lens pass's state (render thread writes, game thread reads): the world's end hooked (thermal.cpp), the pass
// refused this device's surface (until the next mission), the last frame it drew (GetTickCount64).
std::atomic<bool> lensHooked{false},lensRefused{false};
std::atomic<ULONGLONG> lensAt{0};
// Game thread only: the camera's FOV this hook wrote, to retract before the next step (sightzoom.cpp's way).
struct Unzoom { void* camera=nullptr; float before=0.0f,written=0.0f; };
Unzoom unzoom{};

// openedAt: when this soldier's scope was first seen open (the lens's probation, scope_pip.h KeepUnzoomed). frameFov /
// scopeFov: the camera step's last unmagnified frame for this soldier and the stock zoom's FOV it replaced (0: the
// frame is the stock zoom's), at fovAt.
struct Published { ObjRef human; InfantryScopeCue cue; ULONGLONG at; ULONGLONG openedAt; float frameFov,scopeFov; ULONGLONG fovAt; };
Published published{};
SRWLOCK lock=SRWLOCK_INIT;

bool PictureInPicture() noexcept {
    const Config& c=Cfg();
    return c.enabled && scopepip::ModeOf(c.infantryScope)==scopepip::Mode::pictureInPicture;
}
Published Read() noexcept {
    AcquireSRWLockShared(&lock);
    const Published p=published;
    ReleaseSRWLockShared(&lock);
    return p;
}

void Publish(const Published& p) noexcept {
    AcquireSRWLockExclusive(&lock);
    published=p;
    ReleaseSRWLockExclusive(&lock);
}
// The camera step's frame for `human` (its cue still the one published).
void PublishFov(const void* human,float frameFov,float scopeFov) noexcept {
    AcquireSRWLockExclusive(&lock);
    if(published.cue.human==human){published.frameFov=frameFov;published.scopeFov=scopeFov;published.fovAt=GetTickCount64();}
    ReleaseSRWLockExclusive(&lock);
}

// The camera step (see kCamVtable): the stock step runs whole; after it, for the scoped soldier's camera in the picture
// in the picture while the lens can draw (scope_pip.h KeepUnzoomed), the stock zoom's FOV goes back to pi/4. Before
// the next step this hook's own write is retracted (only while the field still holds it): the stock step's no-target
// branch does not write the FOV, so a closed scope, a mode change or a death never leaves the camera unmagnified.
void __fastcall CamStepHook(void* cam,void* step) {
    __try {
        auto c=static_cast<unsigned char*>(cam);
        if(unzoom.camera==cam) {
            if(At<float>(c,kCamFov)==unzoom.written)*reinterpret_cast<float*>(c+kCamFov)=unzoom.before;
            unzoom=Unzoom{};
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    nextCamStep(cam,step);
    __try {
        const Published p=Read();
        const ULONGLONG now=GetTickCount64();
        if(!p.cue.human || now-p.at>kFreshMs || !PictureInPicture())return;
        auto c=static_cast<unsigned char*>(cam);
        if(At<const void*>(c,kCamTargetRef)!=p.cue.human && At<const void*>(c,kCamTarget)!=p.cue.human)return;
        const float before=At<float>(c,kCamFov);
        const float wide=scopepip::KeepUnzoomed(true,lensHooked.load(std::memory_order_acquire),
                                                lensRefused.load(std::memory_order_acquire),now,p.openedAt,
                                                lensAt.load(std::memory_order_acquire)) ?
                         scopepip::Unzoomed(before,At<float>(c,kCamZoom)) : 0.0f;
        if(!(wide>0.0f)){PublishFov(p.cue.human,0.0f,0.0f);return;}
        unzoom=Unzoom{cam,before,wide};
        *reinterpret_cast<float*>(c+kCamFov)=wide;
        PublishFov(p.cue.human,wide,before);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
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
// through a vehicle's sight (hud.cpp HudScopeHoldsCrosshair, scope_hold.h), nor for one whose player controls an enemy
// (the user, 2026-10-21: 「操控怪物的时候没隐藏武器准星」; its weapon is not the one in use, and the enemy's own HUD
// replaces the player's: docs/hud-re.md §9.3.1). The function's own early returns leave exactly as much undone (docs in
// infantryscope.h).
void __fastcall CrossDrawHook(void* self,void* a,void* b) {
    __try {
        if(Readable(self,kCrossHuman+8)) {
            const void* const soldier=At<const void*>(self,kCrossHuman);
            InfantryScopeCue c;
            if(soldier && ((InfantryScopeNow(&c) && soldier==c.human) || HudScopeHoldsCrosshair(soldier)))return;
            if(EnemyControlReplacesPlayerHud(soldier))return;
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
        // The same soldier's scope still open: its probation's start and the camera's last frame carry on.
        {
            const Published was=Read();
            if(was.human.Is(human) && was.cue.human==human) {
                p.openedAt=was.openedAt;p.frameFov=was.frameFov;p.scopeFov=was.scopeFov;p.fovAt=was.fovAt;
            } else p.openedAt=GetTickCount64();
        }
        p.cue=InfantryScopeCue{human,std::isfinite(power) ? power : 1.0f,
                               infantryscope::LookFor(At<std::int32_t>(w,kFireInterval),std::isfinite(blast) ? blast : 0.0f,rocket)};
        p.at=GetTickCount64();
        if(!published.human.Is(human) || published.cue.look!=p.cue.look)
            Log("INFANTRYSCOPE %.1fx, %s",p.cue.zoom,p.cue.look==scope::Look::launcher ? "launcher" : p.cue.look==scope::Look::combat ? "combat optic" : "marksman");
        Publish(p);
    } __except(EXCEPTION_EXECUTE_HANDLER) { Publish(Published{}); }
}

bool InfantryScopeNow(InfantryScopeCue* out) noexcept {
    const Published p=Read();
    const ULONGLONG now=GetTickCount64();
    if(!p.human || !Cfg().enabled || !Cfg().infantryScope || now-p.at>kFreshMs || !(p.cue.zoom>1.01f))return false;
    if(out) {
        *out=p.cue;
        // The lens is what the HUD frames only while it draws, on the camera's unmagnified frame.
        out->lens=PictureInPicture() && p.frameFov>0.0f && p.scopeFov>0.0f && now-p.fovAt<=kFreshMs &&
                  scopepip::LensDrew(now,lensAt.load(std::memory_order_acquire));
        out->lensFov=out->lens ? p.scopeFov : 0.0f;
    }
    return true;
}

namespace {
// Render thread: whether the world view `view` is `human`'s: the camera that submitted it (firstperson.cpp
// LocalViewPlayer), else the first camera slot's (thermal.cpp ViewPlayer's way).
bool ViewOf(const void* view,const void* human) noexcept {
    if(!human)return false;
    __try {
        if(const void* who=LocalViewPlayer(view))return who==human;
        return At<unsigned>(view,0)==0 && At<unsigned>(view,4)==0 && human==PlayerHuman();
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}  // namespace

void InfantryScopeWorldEnd(ID3D11DeviceContext* context,const void* view) noexcept {
    // Render thread only. The pass's device resources go after a while of no lens (kIdleViews world views: a few
    // seconds), not on every other view (split screen: the other player's view has none).
    constexpr unsigned kIdleViews=240;
    static thread_local scopepip::Magnifier magnifier;
    static thread_local unsigned idle=0;
    if(!context || !view)return;
    const Published p=Read();
    const ULONGLONG now=GetTickCount64();
    const bool want=ViewOf(view,p.cue.human) && p.cue.human && now-p.at<=kFreshMs && PictureInPicture() && p.frameFov>0.0f && p.scopeFov>0.0f &&
                    now-p.fovAt<=kFreshMs && !lensRefused.load(std::memory_order_acquire);
    if(!want) {
        if(++idle>=kIdleViews){magnifier.Reset();idle=0;}
        return;
    }
    idle=0;
    const float m=scopepip::Magnification(scopepip::YScale(p.frameFov),p.scopeFov);
    if(!(m>=scopepip::kLeast))return;   // the frame is the stock zoom's already: no lens, nothing refused
    if(magnifier.Composite(context,m)) {
        lensAt.store(now,std::memory_order_release);
        return;
    }
    // The surface is not one the pass draws on (multisampled, a split screen's partial viewport, an unknown format) or
    // the device would not give its resources: the whole-screen scope until the next mission (the camera zooms again).
    magnifier.Reset();
    if(!lensRefused.exchange(true,std::memory_order_acq_rel))
        Log("INFANTRYSCOPE the scope's lens cannot be drawn on this view's surface: the whole screen zooms (InfantryScope=1's picture) this mission");
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
    // The picture in the picture: the camera step chained (its FOV write where docs/zoom-re.md has it), the lens pass's
    // place hooked (thermal.cpp's world end, installed before this). Either missing: InfantryScope=2 shows mode 1's picture.
    if(!Matches(kCamStep,kCamStepCode,sizeof(kCamStepCode)) || !Matches(kFovWrite,kFovWriteCode,sizeof(kFovWriteCode)))
        Log("INFANTRYSCOPE the camera's field of view is not set where docs/zoom-re.md has it: no picture in the picture");
    else if(!edf::ChainVtableSlot(reinterpret_cast<void**>(image+kCamVtable)+kCamStepSlot,reinterpret_cast<void*>(&CamStepHook),
                                  reinterpret_cast<void**>(&nextCamStep)))
        Log("INFANTRYSCOPE the camera's step could not be chained: no picture in the picture");
    else camHooked=true;
    const bool worldEnd=thermal::GetStatus().installed;
    if(camHooked && !worldEnd)Log("INFANTRYSCOPE the world's end is not hooked (THERMAL): no picture in the picture");
    lensHooked.store(camHooked && worldEnd,std::memory_order_release);
    return true;
}

void ResetInfantryScope() noexcept {
    Publish(Published{});
    lensRefused.store(false,std::memory_order_release);
    lensAt.store(0,std::memory_order_release);
}
}  // namespace crew
