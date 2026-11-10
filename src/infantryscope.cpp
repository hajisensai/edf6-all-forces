// A soldier's scoped weapon looked through (src/infantryscope.h).
#include "crew.h"
#include "enemy_control.h"
#include "infantryscope.h"
#include "firstperson.h"
#include "scope_pip.h"
#include "scope_pip_gpu.h"
#include "scope_lens.h"
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
// The lens pass's state (render thread writes, game thread reads): the world's end hooked (thermal.cpp) and the lens view
// installed (scope_lens.cpp), the pass refused this device's surface (until the next mission), the last frame it drew
// (GetTickCount64).
std::atomic<bool> lensHooked{false},lensRefused{false};
std::atomic<ULONGLONG> lensAt{0};
// The scoped soldier's last world view: whether the lens drew in it, and the scope's FOV it drew (the HUD's reticle):
// the HUD frames the lens only over a world the lens is in.
std::atomic<bool> lensShown{false};
std::atomic<float> lensShownFov{0.0f};
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
// The lens refused this mission: its surface (the render thread) or its view (scope_lens.cpp: a split screen, viewport 3
// in use).
bool LensRefused() noexcept {
    return lensRefused.load(std::memory_order_acquire) || scopelens::Refused();
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
// in the picture while the lens can draw (scope_pip.h KeepUnzoomed), the stock zoom's FOV goes back to pi/4 and the lens
// view is asked for at the stock zoom's FOV (scope_lens.cpp renders it in the next recording). Before the next step this
// hook's own write is retracted (only while the field still holds it): the stock step's no-target branch does not write
// the FOV, so a closed scope, a mode change or a death never leaves the camera unmagnified.
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
        const float wide=scopepip::KeepUnzoomed(true,lensHooked.load(std::memory_order_acquire),LensRefused(),now,p.openedAt,
                                                lensAt.load(std::memory_order_acquire)) ?
                         scopepip::Unzoomed(before,At<float>(c,kCamZoom)) : 0.0f;
        if(!(wide>0.0f)){PublishFov(p.cue.human,0.0f,0.0f);return;}
        unzoom=Unzoom{cam,before,wide};
        *reinterpret_cast<float*>(c+kCamFov)=wide;
        PublishFov(p.cue.human,wide,before);
        scopelens::Want(cam,before);
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
        // The lens is what the HUD frames only while it draws: in the soldier's last world view, lately.
        const float fov=lensShownFov.load(std::memory_order_acquire);
        out->lens=PictureInPicture() && !LensRefused() && lensShown.load(std::memory_order_acquire) && fov>0.0f &&
                  scopepip::LensDrew(now,lensAt.load(std::memory_order_acquire));
        out->lensFov=out->lens ? fov : 0.0f;
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
// The world view's own projection's vertical scale (view+0x60, row-major, [5]; docs/fp-render-re.md 2026-10-17, read the
// same way by firstperson.cpp DrawFrustum): the field the frame was really rendered with. The render thread runs 0-3
// game steps behind the camera step (fp-render-re §17.2), so the camera's latest FOV is not this frame's. 0: unreadable.
float ViewYScale(const void* view) noexcept {
    __try {
        if(!Readable(view,0xA0))return 0.0f;
        const float y=std::fabs(At<float>(view,0x60+5*sizeof(float)));
        return std::isfinite(y) && y>0.05f ? y : 0.0f;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0.0f; }
}
}  // namespace

namespace {
// Render thread only: the lens's compositor (scope_pip_gpu.h), the world views since the lens last wanted it. Any
// thread: whether this mission's first drawn lens / a lens view that could not be held was logged.
scopepip::LensCompositor compositor;
unsigned idleViews=0;
std::atomic<bool> drawnLogged{false},lensSizeLogged{false};
// The world view's viewport (view+0) and query (view+4: 0 the main view, 1 the distant view, 2 a shadow cascade).
bool ViewIndex(const void* view,unsigned* index,unsigned* type) noexcept {
    __try { *index=At<unsigned>(view,0);*type=At<unsigned>(view,4);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}  // namespace

void InfantryScopeWorldEnd(ID3D11DeviceContext* context,const void* view) noexcept {
    // Render thread only. The pass's device resources go after a while of no lens (kIdleViews world views: a few
    // seconds), not on every other view (split screen: the other player's view has none).
    constexpr unsigned kIdleViews=240;
    unsigned index=0,type=0;
    if(!context || !view || !ViewIndex(view,&index,&type) || type!=0)return;
    if(index==scopepip::kLensViewport) {
        // The lens view's world (scope_lens.cpp): held for the lens command after it, when the main view's was.
        if(!compositor.HoldsMain())return;
        if(!compositor.CaptureLens(context,scopelens::Side(),ViewYScale(view))) {
            compositor.Release();
            if(!lensSizeLogged.exchange(true,std::memory_order_acq_rel))
                Log("INFANTRYSCOPE the lens view's world target is not a %dx%d one the lens can sample: no lens this frame",
                    scopelens::Side(),scopelens::Side());
        }
        return;
    }
    const Published p=Read();
    const ULONGLONG now=GetTickCount64();
    const bool mine=ViewOf(view,p.cue.human);
    const bool want=mine && now-p.at<=kFreshMs && PictureInPicture() && !LensRefused() && scopelens::Installed();
    if(!want) {
        if(index==0)compositor.Release();
        if(mine)lensShown.store(false,std::memory_order_release);
        if(++idleViews>=kIdleViews){compositor.Reset();idleViews=0;}
        return;
    }
    idleViews=0;
    // The main view's world colour, held for the lens command (recorded after the lens view's world, before this view's
    // post stage).
    if(compositor.CaptureMain(context,ViewYScale(view)))return;
    lensShown.store(false,std::memory_order_release);
    // The surface is not one the pass draws on (multisampled, a split screen's partial viewport, an unknown format) or
    // the device would not give its resources: the whole-screen scope until the next mission (the camera zooms again).
    compositor.Reset();
    if(!lensRefused.exchange(true,std::memory_order_acq_rel))
        Log("INFANTRYSCOPE the scope's lens cannot be drawn on this view's surface: the whole screen zooms (InfantryScope=1's picture) this mission");
}

void InfantryScopeLensCommand(ID3D11DeviceContext* context) noexcept {
    // Render thread, after the lens view's world: the lens drawn into the main view's world colour (both released).
    float h=0.0f,r=0.0f,lensYScale=0.0f;
    const bool heldMain=compositor.HoldsMain(),heldLens=compositor.HoldsLens();
    if(!context || !compositor.Composite(context,&h,&r,&lensYScale)) {
        lensShown.store(false,std::memory_order_release);
        // docs/scope-pip-re.md §4.5 (M): the lens view's world end is the one with view+0 == 3. Not seen: say so once.
        if((!heldMain || !heldLens) && !lensSizeLogged.exchange(true,std::memory_order_acq_rel))
            Log("INFANTRYSCOPE the lens command found no %s world colour held: no lens this frame",heldMain ? "lens view's" : "main view's");
        return;
    }
    // The reticle's FOV: the lens view's own projection as rendered (it may be a step behind the camera).
    const float fov=scopepip::LensShownFov(h,r,lensYScale);
    lensShownFov.store(fov,std::memory_order_release);
    lensShown.store(fov>0.0f,std::memory_order_release);
    lensAt.store(GetTickCount64(),std::memory_order_release);
    if(!drawnLogged.exchange(true,std::memory_order_acq_rel))
        Log("INFANTRYSCOPE lens drawn: main height %.0f, lens radius %.0f, lens view %dx%d, scope fov %.4f",h,r,
            scopelens::Side(),scopelens::Side(),fov);
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
    // The lens view (scope_lens.cpp): its own checks, its own log line when it is not there.
    lensHooked.store(camHooked && worldEnd && scopelens::Install(),std::memory_order_release);
    return true;
}

void ResetInfantryScope() noexcept {
    Publish(Published{});
    scopelens::Reset();
    drawnLogged.store(false,std::memory_order_release);
    lensSizeLogged.store(false,std::memory_order_release);
    lensRefused.store(false,std::memory_order_release);
    lensAt.store(0,std::memory_order_release);
    lensShown.store(false,std::memory_order_release);
}
}  // namespace crew
