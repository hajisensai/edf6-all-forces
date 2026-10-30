// The vehicle sight's magnification (src/sightzoom.h; docs/zoom-re.md): the toggle, kept per seat the player sits at
// (SightZoomFrame, from the seat's own frame: a stock vehicle's input, the gunship gunner's GunnerFrame), and the
// camera: the player's CharacterGhostCamera step is chained after the native view. A model-authored optic bone
// supplies the actual eye, the selected weapon supplies its bore direction, and native LookTo builds cam+0x220.
// Its FOV and world matrix writes are retracted before the next step only while they remain our own values.
//  - Only the player's own camera (its target the player's soldier, as map.cpp tells it), only while they ride (the
//    soldier's vehicle, +0x1550), never while the map or the Tempest's TV holds the view; the magnification published
//    by the same live seat owner: when the player gets out, the vehicle is wrecked or gone, SightZoom or the plugin
//    is switched off, the view is restored on the next camera step, including its no-target branch.
//  - A new seat or vehicle starts at 1x; a new mission too (ResetSightZoom).
//  - Shared pad bindings belong to the high view or the Sazabi's hard lock; zoom remains available on a separate binding.
//  - The range drum is laid on the gun (sightzoom.h LayStep): SightLayStep, from every seat aim's step after the stock
//    step and the stabilizer (stab.cpp StepHook), turns the player's sighted seat's own pitch axis by the change of the
//    superelevation at a gun drive's pace (stab.cpp RemapAimAxis maps it onto the bones and moves the stabilizer's held
//    line with it); the camera lowers the bore by what the gun carries, so the reticle stays on the target as the
//    barrel climbs.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "seat_switch_logic.h"
#include "vehicle_look.h"
#include "layout.h"
#include "sightzoom.h"
#include "optic_mount.h"
#include "turretaim.h"
#include "edf/patch.h"
#include "edf/weapon.h"
#include "firstperson.h"
#include "memory.h"

namespace crew {
namespace {
constexpr unsigned kCamVtable=0x1768C10,kCamStep=0xF86A0;
constexpr std::size_t kCamStepSlot=4,kCamTargetRef=0x350,kCamTarget=0x360,kCamFov=0x24;
// The stock field of view's write in the step (movss xmm0,[pi/4]; divss xmm0,[rdi+410h]; movss [rdi+24h],xmm0).
constexpr unsigned kFovWrite=0xF8906;
const unsigned char kFovWriteCode[]={0xF3,0x0F,0x10,0x05,0x12,0xD1,0x66,0x01,0xF3,0x0F,0x5E,0x87,0x10,0x04,0x00,0x00,0xF3,0x0F,0x11,0x47,0x24};
const unsigned char kCamStepCode[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0x90};
// The seat's input (docs/stores-re.md §4): 1 = a pad (0 keyboard and mouse), the pad's button bits.
constexpr std::size_t kSeatPad=0x2B0,kSeatButtons=0x2E8;
constexpr std::uint16_t kDpadUp=0x0001,kDpadDown=0x0002;   // XInput's wButtons (XINPUT_GAMEPAD_DPAD_*)
constexpr std::size_t kCamMatrix=0x220;
// The seat aim's params {brake, accel, top} (docs/nix-re.md §3, stab.cpp kAimParams): top, rad/frame, at +8.
constexpr std::size_t kAimParams=0x90,kAimNetwork=0xC0;   // +0xC0 non-zero: the network gives this aim its input
constexpr unsigned kLookTo=0x4E220;
const unsigned char kLookToCode[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x0F,0x10,0x0A};
using LookToFn=float*(__fastcall*)(float*,const float*);
LookToFn opticLookTo=nullptr;

using CamStepFn=void(__fastcall*)(void*,void*);
CamStepFn nextCamStep=nullptr;
bool installed=false;

struct Capability { sightzoom::Kind kind; const void* weapon; bool mounted=false;
                    sightzoom::Eyepiece eyepiece=sightzoom::Eyepiece::binocular; };   // the mounted optic's (optic_mount.h)
struct Toggle { ObjRef ref,human; const void* v; unsigned seat; int step; bool held; Capability sight; int zero=0; bool upHeld=false,downHeld=false; std::uint16_t pad=0;
                bool fresh=false;      // fresh: the seat just taken (Debug=1 says its sight once)
                bool raised=false; };  // first person: the sight raised at 1x (Raise), else the cockpit's eye
Toggle toggle{};
// Publish the entire seat identity together: a camera must not combine another seat's zoom and timestamp. `aim` the
// seat's aim (seat+0xE0), what SightLayStep is told apart by before anything else is read.
struct Cue { ObjRef vehicle,human; unsigned seat; float zoom; Capability sight; int zero=0; const unsigned char* aim=nullptr; bool raised=false; };
Cue cue{};
// The sight is up (its camera, its lay, its reticle): magnified, or raised at 1x in a vehicle's first person (War
// Thunder's gunner sight: firstperson.h FirstPersonGunnerSight, sightzoom.h Raise). Otherwise at 1x the turret camera
// (third person) or the cockpit's eye (first person) has the view.
bool Raised(const Cue& c) noexcept { return c.zoom>1.0f || (c.raised && FirstPersonGunnerSight()); }
bool Up(const Cue& c) noexcept { return !VehicleLookActive(c.vehicle.obj) && Raised(c); }
// The lay (sightzoom.h LayStep): the superelevation the seat's gun carries over the line of sight now, as SightLayStep
// last turned it (game thread, under cueLock; the camera reads it). Not engaged: the aim step never reached it (its hooks
// are not in, stab.cpp RemapAimAxis cannot map the axis): the camera falls back to the set range's whole angle.
struct LayState { ObjRef vehicle,human; unsigned seat=0; const unsigned char* aim=nullptr; float laid=0.0f; bool engaged=false; };
LayState lay{};
SRWLOCK cueLock=SRWLOCK_INIT;
// Camera-thread state only. The original step may leave FOV untouched when its target expires.
struct Applied { void* camera; float before,written; float matrixBefore[16],matrixWritten[16]; bool view=false; } applied{};

Capability SeatCapability(const unsigned char* v,unsigned index) noexcept {
    using sightzoom::Kind;
    if(!HudReady() || index>=SeatCount(v))return {};
    // Virtual weapons have explicit providers; do not mistake a transport seat/fuel holder for one of them.
    if(IsSazabi(v))return Cfg().sazabi && index==0 ? Capability{Kind::mech,nullptr} : Capability{};
    if(index==kGunnerSeat && GunshipCrewSeats(v))return {Kind::sensor,nullptr};
    const bool aircraft=PlayerJetOwnSight(v),heli=BodyOf(v)==PluginBody::none && IsHelicopter(v);
    if((heli && !Cfg().playerHeliGunSight) || (!aircraft && !heli && !Cfg().stockVehicleHud))return {};
    const auto seat=SeatAt(const_cast<unsigned char*>(v),index);
    const auto holders=At<const unsigned char* const*>(seat,kSeatWeapons);
    const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!n || n>16 || !Readable(holders,n*sizeof(void*)))return {};
    const unsigned char* picked=PayloadSightPicked(v,index);
    if(!picked)return {}; // no live fire-control weapon, not a request to fall back to an arbitrary holder
    const bool observing=HighCamOn(v) || TurretCamHighTransition(v);
    const auto of=[&](const unsigned char* weapon) noexcept -> Capability {
        RoundModel round{};float muzzle[3],direction[3];
        if(!Readable(weapon,edf::kWeaponAmmoGravity+4) || IsFuelTank(weapon) || !ReadRound(weapon,&round) ||
           !round.rtti || round.kind==RoundKind::none || !edf::MeanMuzzle(weapon,64,muzzle,direction))return {};
        if(!std::isfinite(muzzle[0]+muzzle[1]+muzzle[2]+direction[0]+direction[1]+direction[2]))return {};
        const int mark=At<std::int32_t>(weapon,edf::kWeaponMark);
        const bool indirect=!EnergyWeapon(round.style) && ((round.lobbed && round.alive>=600) || mark==edf::kMarkLofted ||
            (round.kind==RoundKind::arc && mark==edf::kMarkGround && round.alive>=600));
        Capability found{indirect ? Kind::indirect : aircraft || (heli && index==0) ? Kind::flight :
            EnergyWeapon(round.style) ? Kind::sensor : round.kind==RoundKind::homing ? Kind::missile : round.kind==RoundKind::rocket || round.lobbed ? Kind::rocket : Kind::optical,weapon};
        if(observing)found.kind=Kind::indirect;
        if(sightzoom::Magnifies(found.kind)) {
            optic::Pose pose;
            found.mounted=optic::Mounted(v,seat,weapon,&pose);
            if(found.mounted)found.eyepiece=pose.eyepiece;
        }
        return found;
    };
    const auto live=[&](std::uint64_t i) noexcept -> const unsigned char* {
        if(!Readable(holders[i],kHolderWeapon+8))return nullptr;
        const auto ctrl=At<const unsigned char*>(holders[i],kHolderCtrl);
        return Readable(ctrl,12) && At<std::int32_t>(ctrl,8)>0 ? At<const unsigned char*>(holders[i],kHolderWeapon) : nullptr;
    };
    Capability found{};
    for(std::uint64_t i=0;i<n;++i)if(holders[i] && Readable(holders[i],kHolderWeapon+8) &&
                                    At<const unsigned char*>(holders[i],kHolderWeapon)==picked){found=of(picked);break;}
    // The fire-control pick without an optic of its own (the Titan's hull gatling on the right trigger, 2026-10-09: "按了右
    // 键使用机枪以后，自瞄和瞄具等都无法使用了"): the seat's sight is still there; the first live weapon of the same seat
    // whose model carries a mounted optic owns the scope, so the scope and its key do not come and go with the trigger
    // last pulled.
    if(!observing && found.kind!=Kind::none && !found.mounted && sightzoom::Magnifies(found.kind))
        for(std::uint64_t i=0;i<n;++i) {
            const unsigned char* const weapon=live(i);
            if(!weapon || weapon==picked)continue;
            const Capability other=of(weapon);
            if(other.mounted){found=other;break;}
        }
    return found;
}

// An arc gun's round for the range setting (its speed, its fall a frame: AmmoGravityFactor x the world's gravity); false
// for a guided or motor round, an energy weapon (nothing to lay over).
bool ArcOf(const unsigned char* w,float* speed,float* drop) noexcept {
    RoundModel m{};float g[3];
    if(!w || !ReadRound(w,&m) || m.kind!=RoundKind::arc || m.lobbed || !(m.speed>0.0f) || !(m.factor>0.0f) || !edf::WorldGravity(image,g))
        return false;
    *speed=m.speed;*drop=m.factor*std::fabs(g[1])/3600.0f;
    return *drop>0.0f && std::isfinite(*drop);
}
// The farthest the setting goes for weapon `w`: its round's reach (speed x life), and for an arc round no farther than
// it lands on the level (sightzoom.h BallisticReach); 0 not known (kZeroMost).
int ReachOf(const unsigned char* w) noexcept {
    RoundModel m{};
    if(!w || !ReadRound(w,&m) || !(m.speed>0.0f) || m.alive<=0)return 0;
    float reach=m.speed*static_cast<float>(m.alive),speed=0.0f,drop=0.0f;
    if(ArcOf(w,&speed,&drop)){const float most=sightzoom::BallisticReach(speed,drop);if(most>0.0f && most<reach)reach=most;}
    return reach>=sightzoom::kZeroStep && reach<1e6f ? static_cast<int>(reach) : 0;
}
// The setting `zero` as it holds for weapon `w`: no farther than that weapon reaches. The seat keeps one setting for all
// its weapons (the drum is the sight's) and it is held to the reach only when it is turned (NextZero), so a setting made
// on the cannon is past the coaxial gun's reach when the trigger changes (review of #137: ZeroAngle went to 45 degrees
// there and the view looked into the ground); the weapon looked through now is held to its own.
int ZeroFor(const unsigned char* w,int zero) noexcept {
    if(zero<=0)return 0;
    const int reach=ReachOf(w);
    return reach>0 && zero>reach ? reach : zero;
}
// The sight's superelevation for weapon `w` at the setting `zero` (sightzoom.h ZeroAngle), 0 for a round with nothing
// to lay over.
float ZeroOf(const unsigned char* w,int zero) noexcept {
    zero=ZeroFor(w,zero);
    float speed=0.0f,drop=0.0f;
    if(zero<=0 || !ArcOf(w,&speed,&drop))return 0.0f;
    const float a=sightzoom::ZeroAngle(speed,drop,static_cast<float>(zero));
    return std::isfinite(a) && a<0.785398163f ? a : 0.0f;
}

Cue Snapshot() noexcept {
    AcquireSRWLockShared(&cueLock);
    const Cue c=cue;
    ReleaseSRWLockShared(&cueLock);
    return c;
}

// Whether two capabilities are the same scope as far as the camera and the player see it: one physical optic per seat
// (the user, 2026-10-11: "用副武器的时候瞄具不会强制切回去，而是两边共用才对"). The primary gun, the coaxial gun on the
// secondary trigger and the stores the switch picks all look through the seat's one sight, so picking another weapon
// keeps the view, the magnification and the eye; only losing the optic, an overhead / indirect view or no weapon end it.
bool SameScope(const Capability& a,const Capability& b) noexcept {
    return a.kind!=sightzoom::Kind::none && b.kind!=sightzoom::Kind::none && a.mounted==b.mounted && a.eyepiece==b.eyepiece &&
           sightzoom::Magnifies(a.kind)==sightzoom::Magnifies(b.kind);
}

bool Current(const Cue& c,Capability* live=nullptr) noexcept {
    if(!Cfg().enabled || !Cfg().sightZoom || !c.vehicle)return false;
    const unsigned char* human=PlayerHuman();
    if(!human || !c.human.Is(human) || human[kDead])return false;
    const auto ctrl=At<const unsigned char*>(human,kHumanVehicleCtrl);
    if(!Readable(ctrl,16) || At<int>(ctrl,8)==0)return false;
    const auto v=At<unsigned char*>(human,kHumanVehicleCtrl-8);
    if(!c.vehicle.Is(v) || v[kDead] || c.seat>=SeatCount(v))return false;
    const unsigned char* seat=SeatAt(v,c.seat);
    if(SeatRider(seat)!=Rider::player || At<const void*>(seat,kSeatRider)!=human)return false;
    const Capability now=SeatCapability(v,c.seat);
    if(!SameScope(now,c.sight))return false;
    if(live)*live=now;
    return true;
}

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (edf::AsyncKey(vk)&0x8000)!=0;
}

// The superelevation the camera lays the line of sight under the bore by for cue `c`, weapon `w`: what the gun carries
// now (the lay engaged on this seat), else the set range's whole angle (the aim step's hooks are not in).
float LaidFor(const Cue& c,const unsigned char* w) noexcept {
    AcquireSRWLockShared(&cueLock);
    const LayState l=lay;
    ReleaseSRWLockShared(&cueLock);
    if(l.engaged && l.vehicle.obj==c.vehicle.obj && l.human.obj==c.human.obj && l.seat==c.seat && l.aim==c.aim &&
       std::isfinite(l.laid))return l.laid;
    return ZeroOf(w,c.zero);
}

void Publish(unsigned char* v,unsigned seat,float zoom) noexcept {
    const Cue next{ObjRef::Of(v),ObjRef::Of(PlayerHuman()),seat,zoom,toggle.sight,toggle.zero,SeatAt(v,seat)+kSeatAim,toggle.raised};
    AcquireSRWLockExclusive(&cueLock);
    cue=next;
    ReleaseSRWLockExclusive(&cueLock);
}

void __fastcall CamStepHook(void* cam,void* step) {
    // Retract only our own write before the original step; it may not write FOV on every branch.
    // Never dereference a remembered camera: only the camera passed to this invocation is live.
    __try {
        auto c=static_cast<unsigned char*>(cam);
        if(applied.camera==cam && At<float>(c,kCamFov)==applied.written)
            *reinterpret_cast<float*>(c+kCamFov)=applied.before;
        if(applied.camera==cam && applied.view && std::memcmp(c+kCamMatrix,applied.matrixWritten,sizeof(applied.matrixWritten))==0)
            std::memcpy(c+kCamMatrix,applied.matrixBefore,sizeof(applied.matrixBefore));
        if(applied.camera==cam)applied=Applied{};
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    nextCamStep(cam,step);
    if(MapOwnsView())return;
    const Cue state=Snapshot();
    __try {
        auto c=static_cast<unsigned char*>(cam);
        Capability live{};
        if(!Current(state,&live) || !Up(state) || !live.mounted || !opticLookTo)return;
        const void* human=state.human.obj;
        if(At<const void*>(c,kCamTargetRef)!=human && At<const void*>(c,kCamTarget)!=human)return;
        const float before=At<float>(c,kCamFov);
        if(!std::isfinite(before) || !(before>0.0f))return;
        const auto vehicle=static_cast<const unsigned char*>(state.vehicle.obj);
        optic::Pose pose;
        if(!optic::Mounted(vehicle,SeatAt(const_cast<unsigned char*>(vehicle),state.seat),
                           static_cast<const unsigned char*>(live.weapon),&pose))return;
        // The line of sight under the bore by the superelevation the gun carries (sightzoom.h LayStep, Lowered): as the
        // lay raises the barrel the bore climbs and the line of sight stays on the target.
        alignas(16) float direction[4]={pose.direction[0],pose.direction[1],pose.direction[2],0};
        sightzoom::Lowered(pose.direction,LaidFor(state,static_cast<const unsigned char*>(live.weapon)),direction);
        alignas(16) float matrix[16];
        opticLookTo(matrix,direction);
        for(float value:matrix)if(!std::isfinite(value))return;
        for(int i=0;i<3;++i)matrix[12+i]=pose.eye[i];
        matrix[15]=1.0f;
        const float written=before/state.zoom;
        applied=Applied{};applied.camera=cam;applied.before=before;applied.written=written;applied.view=true;
        std::memcpy(applied.matrixBefore,c+kCamMatrix,sizeof(matrix));
        std::memcpy(applied.matrixWritten,matrix,sizeof(matrix));
        std::memcpy(c+kCamMatrix,matrix,sizeof(matrix));
        *reinterpret_cast<float*>(c+kCamFov)=written;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

}  // namespace

bool InstallSightZoom() noexcept {
    if(installed)return true;
    if(!Matches(kCamStep,kCamStepCode,sizeof(kCamStepCode)) || !Matches(kFovWrite,kFovWriteCode,sizeof(kFovWriteCode)) ||
       !Matches(kLookTo,kLookToCode,sizeof(kLookToCode))) {
        Log("SIGHTZOOM the camera's field of view is not set where docs/zoom-re.md has it: no sight zoom");
        return false;
    }
    auto slot=reinterpret_cast<void**>(image+kCamVtable)+kCamStepSlot;
    if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&CamStepHook),reinterpret_cast<void**>(&nextCamStep))){Log("SIGHTZOOM the camera's step could not be hooked");return false;}
    opticLookTo=reinterpret_cast<LookToFn>(image+kLookTo);
    installed=true;
    return true;
}

void SightZoomFrame(unsigned char* v,unsigned seat,bool padButton) noexcept {
    const Config& c=Cfg();
    if(!installed || !c.enabled || !c.sightZoom){ResetSightZoom();return;}
    if(!v || v[kDead] || seat>=SeatCount(v))return;
    const unsigned char* s=SeatAt(v,seat);
    if(SeatRider(s)!=Rider::player || At<const void*>(s,kSeatRider)!=PlayerHuman())return;
    const Capability sight=SeatCapability(v,seat);
    if(sight.kind==sightzoom::Kind::none){ResetSightZoom();return;}
    // A seat just taken: 1x, a key held while boarding no press. Wall time is not
    // ownership: pausing or a slow frame cannot erase the player's scope choice.
    // The magnification belongs to the seat's sight, not to the weapon last fired: the primary, the secondary
    // trigger's gun and the switch's stores share it (SameScope). A weapon that cannot be looked at through the
    // optic (an indirect round, the overhead view) shows 1x while it is picked; the step is kept for the next one.
    if(!toggle.ref.Is(v) || !toggle.human.Is(PlayerHuman()) || toggle.seat!=seat)
        toggle=Toggle{ObjRef::Of(v),ObjRef::Of(PlayerHuman()),v,seat,0,true,sight,0,false,false,0,true};
    if(c.debug && (toggle.fresh || toggle.sight.kind!=sight.kind || toggle.sight.mounted!=sight.mounted || toggle.sight.weapon!=sight.weapon))
        Log("SIGHTZOOM v=%p seat %u: sight kind %d, weapon %p, optic %s",v,seat,static_cast<int>(sight.kind),sight.weapon,
            sight.mounted ? "mounted" : "none (no vc_optic_ on its mount's chain)");
    toggle.fresh=false;
    toggle.sight=sight;
    // First person starts at the cockpit's eye every time it is turned on (the user, 2026-10-30: "驾驶室视角得按观察键才
    // 显示。该成瞄准键？"); third person has no raised 1x.
    const bool firstPerson=FirstPersonGunnerSight();
    if(!firstPerson)toggle.raised=false;
    if(!sightzoom::Magnifies(sight.kind) || !sight.mounted){toggle.held=true;TakeScopeWheel(false);Publish(v,seat,1.0f);return;}
    if(GamePaused()){toggle.held=true;TakeScopeWheel(false);Publish(v,seat,sightzoom::At(toggle.step));return;}
    const bool keys=At<unsigned char>(s,kSeatPad)==0;
    // A pad button another view keeps (padButton false) is still followed, so one held across the hand-over is no press.
    const bool down=keys ? KeyHeld(c.sightZoomKey)
                         : c.sightZoomButton && (At<std::uint16_t>(s,kSeatButtons)&static_cast<std::uint16_t>(c.sightZoomButton))!=0 &&
                           !seatsw::ChordOwns(At<std::uint16_t>(s,kSeatButtons),static_cast<unsigned>(c.thermalButton),static_cast<unsigned>(c.sightZoomButton));
    if(MapHoldsKeys()){toggle.held=true;TakeScopeWheel(false);Publish(v,seat,sightzoom::At(toggle.step));return;}
    if(down && !toggle.held && (keys || padButton)) {
        const sightzoom::Raise r=sightzoom::Press(toggle.step,toggle.raised,firstPerson);
        toggle.step=r.step;toggle.raised=r.raised;
        Log("SIGHTZOOM v=%p seat %u: %s by the %s",v,seat,
            r.step ? (r.step==1 ? "3x" : "6x") : r.raised || !firstPerson ? "1x" : "the cockpit",keys ? "key" : "pad button");
    }
    toggle.held=down;
    // The range setting, while the sight is magnified: the wheel (swallowed then: map.cpp TakeScopeWheel) and the
    // keys on the keyboard and mouse, the d-pad on a pad; one step a notch or a press (sightzoom.h NextZero).
    // At 1x too in a vehicle's first person (War Thunder's gunner sight: its range is set at any magnification; the
    // sight lays the gun by it, so a range that could not be turned there left the rounds short of the reticle).
    const bool scoped=sightzoom::At(toggle.step)>1.0f || (firstPerson && toggle.raised);
    int notches=TakeScopeWheel(scoped);   // the wheel whichever input the seat is on (a pad player's mouse too)
    const bool up=scoped && keys && KeyHeld(c.sightRangeUpKey),downKey=scoped && keys && KeyHeld(c.sightRangeDownKey);
    notches+=(up && !toggle.upHeld ? 1 : 0)-(downKey && !toggle.downHeld ? 1 : 0);
    toggle.upHeld=up;toggle.downHeld=downKey;
    std::uint16_t pad=0;
    if(scoped && !keys && c.sightRangePad && MapPadButtons(&pad)) {
        const std::uint16_t edge=static_cast<std::uint16_t>(pad&~toggle.pad);
        notches+=((edge&kDpadUp) ? 1 : 0)-((edge&kDpadDown) ? 1 : 0);
    }
    toggle.pad=pad;
    if(notches) {
        const int was=toggle.zero;
        toggle.zero=sightzoom::NextZero(toggle.zero,notches,ReachOf(static_cast<const unsigned char*>(sight.weapon)),c.sightRangeStep);
        if(toggle.zero!=was)Log("SIGHTZOOM v=%p seat %u: range set %d m",v,seat,toggle.zero);
    }
    Publish(v,seat,sightzoom::At(toggle.step));
}

void SightZoomStock(unsigned char* v) noexcept {
    if(!Cfg().enabled || !Cfg().sightZoom){ResetSightZoom();return;}
    if(v[kDead]){if(toggle.ref.Is(v))ResetSightZoom();return;}
    const unsigned n=SeatCount(v);
    for(unsigned i=0;i<n;++i) {
        if(SeatRider(SeatAt(v,i))!=Rider::player || At<const void*>(SeatAt(v,i),kSeatRider)!=PlayerHuman())continue;
        const bool reserved=i==0 && ((HighCamOffered(v) && (Cfg().highCamButton & Cfg().sightZoomButton)!=0 && !HighCamYieldsButton(v)) ||
            (Cfg().sazabi && IsSazabi(v) && (Cfg().sazabiLockButton & Cfg().sightZoomButton)!=0));
        SightZoomFrame(v,i,!reserved);
        return;
    }
    if(toggle.ref.Is(v))ResetSightZoom();
}

bool SightZoomCanMount(const void* vehicle,unsigned seat) noexcept {
    if(MapOwnsView())return false;
    const Cue c=Snapshot();
    __try { return c.vehicle.obj==vehicle && c.seat==seat && c.sight.mounted && Current(c); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool SightZoomRaised(const void* vehicle) noexcept {
    if(MapOwnsView())return false;
    const Cue c=Snapshot();
    __try { return (!vehicle || c.vehicle.obj==vehicle) && Raised(c) && Current(c); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

bool SightZoomMounted(const void* vehicle) noexcept {
    if(MapOwnsView())return false;
    const Cue c=Snapshot();
    __try { return (!vehicle || c.vehicle.obj==vehicle) && c.sight.mounted && Up(c) && Current(c); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

float SightZoomNow(const void* vehicle) noexcept {
    const Cue c=Snapshot();
    __try {
        if((vehicle && c.vehicle.obj!=vehicle) || !Current(c))return 1.0f;
        return c.zoom;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 1.0f;}
}

bool SightZoomScope(SightScope* out) noexcept {
    *out=SightScope{};
    if(MapOwnsView())return false;
    const Cue c=Snapshot();
    __try {
        if(!Current(c) || !sightzoom::ScopeField(c.sight.kind,c.zoom,c.sight.mounted && Up(c)))return false;
        out->vehicle=c.vehicle.obj;out->human=c.human.obj;out->kind=c.sight.kind;out->zoom=c.zoom;out->eyepiece=c.sight.eyepiece;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){*out=SightScope{};return false;}
}

sightzoom::Kind SightZoomView(const void* vehicle) noexcept {
    if(MapOwnsView())return sightzoom::Kind::none;
    const Cue c=Snapshot();
    __try {
        return (!vehicle || c.vehicle.obj==vehicle) && Current(c) ? c.sight.kind : sightzoom::Kind::none;
    } __except(EXCEPTION_EXECUTE_HANDLER){return sightzoom::Kind::none;}
}

int SightZoomRange(const void* vehicle,float* angle) noexcept {
    if(angle)*angle=0.0f;
    if(MapOwnsView())return 0;
    const Cue c=Snapshot();
    __try {
        Capability live{};
        if((vehicle && c.vehicle.obj!=vehicle) || !Up(c) || !Current(c,&live) || !live.mounted)return 0;
        const auto w=static_cast<const unsigned char*>(live.weapon);
        if(angle)*angle=LaidFor(c,w);
        return ZeroFor(w,c.zero);   // the range this weapon is laid for (held to its reach)
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
}

void SightLayStep(unsigned char* aim,float before) noexcept {
    if(!installed || !aim)return;
    const Cue c=Snapshot();
    if(aim!=c.aim)return;   // every seat aim in the world comes here: only the player's magnified sight's is laid
    __try {
        if(At<unsigned char>(aim,kAimNetwork)!=0)return;   // the network steps this aim (docs/nix-re.md §3): not ours
        Capability live{};
        const bool sighted=Up(c) && Current(c,&live) && live.mounted;
        AcquireSRWLockShared(&cueLock);
        LayState l=lay;
        ReleaseSRWLockShared(&cueLock);
        const bool same=l.engaged && l.vehicle.Is(c.vehicle.obj) && l.human.obj==c.human.obj && l.seat==c.seat && l.aim==aim;
        if(!sighted) {
            // Out of the sight (1x, an indirect pick, another seat): the barrel stays where it is; the turret camera's
            // own steering (turretcam.cpp) has the gun again. Laid anew when the sight comes up.
            if(l.engaged){AcquireSRWLockExclusive(&cueLock);lay=LayState{};ReleaseSRWLockExclusive(&cueLock);}
            return;
        }
        const auto w=static_cast<const unsigned char*>(live.weapon);
        const float want=ZeroOf(w,c.zero);
        if(!same) {
            // The sight comes up on a gun that already carries its setting (the turret camera laid it by the arc at 1x,
            // or the player before): the line of sight goes under the bore by the whole angle at once, as it always did;
            // from here on every change of the setting or of the weapon's round is laid on the gun.
            if(!AimAxisAdjustable())return;
            l=LayState{c.vehicle,c.human,c.seat,aim,want,true};
        } else if(AutoTurretSteers(c.vehicle.obj,c.seat)!=1) {   // EDF6AutoTurret laying on a lock already solves the arc
            float* axis=reinterpret_cast<float*>(aim+kAimAxes+kAxisStride);   // the pitch axis {min, max, angle, rate}
            const float top=At<float>(aim,kAimParams+8);   // the turret's top rate (rad/frame), the stock params' third
            const float rate=std::isfinite(top) && top>0.0f && top<sightzoom::kLayRate ? top : sightzoom::kLayRate;
            const sightzoom::Lay step=sightzoom::LayStep(want,l.laid,rate,axis[2],axis[0],axis[1]);
            if(step.angle!=axis[2] && !RemapAimAxis(aim,1,before,step.angle))return;
            if(Cfg().debug && step.laid!=l.laid && step.laid==want)
                Log("SIGHTZOOM v=%p seat %u: gun laid %.2f deg over the line of sight for %d m",c.vehicle.obj,c.seat,want*57.29578f,ZeroFor(w,c.zero));
            l.laid=step.laid;
        }
        AcquireSRWLockExclusive(&cueLock);
        lay=l;
        ReleaseSRWLockExclusive(&cueLock);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void ResetSightZoom() noexcept {
    toggle=Toggle{};
    TakeScopeWheel(false);
    AcquireSRWLockExclusive(&cueLock);
    cue=Cue{};
    lay=LayState{};
    ReleaseSRWLockExclusive(&cueLock);
    // The camera hook still owns its last FOV write until that camera next steps.
}

}  // namespace crew
