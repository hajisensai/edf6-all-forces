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
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "layout.h"
#include "sightzoom.h"
#include "optic_mount.h"
#include "edf/patch.h"
#include "edf/weapon.h"
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
constexpr std::size_t kCamMatrix=0x220;
constexpr unsigned kLookTo=0x4E220;
const unsigned char kLookToCode[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x0F,0x10,0x0A};
using LookToFn=float*(__fastcall*)(float*,const float*);
LookToFn opticLookTo=nullptr;

using CamStepFn=void(__fastcall*)(void*,void*);
CamStepFn nextCamStep=nullptr;
bool installed=false;

struct Capability { sightzoom::Kind kind; const void* weapon; bool mounted=false; };
struct Toggle { ObjRef ref,human; const void* v; unsigned seat; int step; bool held; Capability sight; };
Toggle toggle{};
// Publish the entire seat identity together: a camera must not combine another seat's zoom and timestamp.
struct Cue { ObjRef vehicle,human; unsigned seat; float zoom; Capability sight; };
Cue cue{};
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
    Capability selected{};
    for(std::uint64_t i=0;i<n;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto weapon=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(weapon!=picked)continue;
        RoundModel round{};float muzzle[3],direction[3];
        if(!Readable(weapon,edf::kWeaponAmmoGravity+4) || IsFuelTank(weapon) || !ReadRound(weapon,&round) ||
           !round.rtti || round.kind==RoundKind::none || !edf::MeanMuzzle(weapon,64,muzzle,direction))continue;
        if(!std::isfinite(muzzle[0]+muzzle[1]+muzzle[2]+direction[0]+direction[1]+direction[2]))continue;
        const int mark=At<std::int32_t>(weapon,edf::kWeaponMark);
        const bool indirect=!EnergyWeapon(round.style) && ((round.lobbed && round.alive>=600) || mark==edf::kMarkLofted ||
            (round.kind==RoundKind::arc && mark==edf::kMarkGround && round.alive>=600));
        const Kind kind=indirect ? Kind::indirect : aircraft || (heli && index==0) ? Kind::flight :
            EnergyWeapon(round.style) ? Kind::sensor : round.kind==RoundKind::homing ? Kind::missile : round.kind==RoundKind::rocket || round.lobbed ? Kind::rocket : Kind::optical;
        const Capability found{kind,weapon};
        selected=found;break;
    }
    Capability found=selected;
    if(found.kind!=Kind::none && (HighCamOn(v) || TurretCamHighTransition(v)))found.kind=Kind::indirect;
    if(sightzoom::Magnifies(found.kind) && found.weapon) {
        optic::Pose pose;
        found.mounted=optic::Mounted(v,seat,static_cast<const unsigned char*>(found.weapon),&pose);
    }
    return found;
}

Cue Snapshot() noexcept {
    AcquireSRWLockShared(&cueLock);
    const Cue c=cue;
    ReleaseSRWLockShared(&cueLock);
    return c;
}

bool Current(const Cue& c) noexcept {
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
    return now.kind!=sightzoom::Kind::none && now.kind==c.sight.kind && now.weapon==c.sight.weapon && now.mounted==c.sight.mounted;
}

bool KeyHeld(int vk) noexcept {
    if(vk<=0 || MapHoldsKeys())return false;   // the map view holds the player's keys (map.cpp)
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    return pid==GetCurrentProcessId() && (GetAsyncKeyState(vk)&0x8000)!=0;
}

void Publish(unsigned char* v,unsigned seat,float zoom) noexcept {
    const Cue next{ObjRef::Of(v),ObjRef::Of(PlayerHuman()),seat,zoom,toggle.sight};
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
        if(!Current(state) || !(state.zoom>1.0f) || !state.sight.mounted || !opticLookTo)return;
        const void* human=state.human.obj;
        if(At<const void*>(c,kCamTargetRef)!=human && At<const void*>(c,kCamTarget)!=human)return;
        const float before=At<float>(c,kCamFov);
        if(!std::isfinite(before) || !(before>0.0f))return;
        const auto vehicle=static_cast<const unsigned char*>(state.vehicle.obj);
        optic::Pose pose;
        if(!optic::Mounted(vehicle,SeatAt(const_cast<unsigned char*>(vehicle),state.seat),
                           static_cast<const unsigned char*>(state.sight.weapon),&pose))return;
        alignas(16) float direction[4]={pose.direction[0],pose.direction[1],pose.direction[2],0};
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
    if(!toggle.ref.Is(v) || !toggle.human.Is(PlayerHuman()) || toggle.seat!=seat ||
       toggle.sight.kind!=sight.kind || toggle.sight.weapon!=sight.weapon)
        toggle=Toggle{ObjRef::Of(v),ObjRef::Of(PlayerHuman()),v,seat,0,true,sight};
    if(!sightzoom::Magnifies(sight.kind) || !sight.mounted){toggle.step=0;toggle.held=true;Publish(v,seat,1.0f);return;}
    if(GamePaused()){toggle.held=true;Publish(v,seat,sightzoom::At(toggle.step));return;}
    const bool keys=At<unsigned char>(s,kSeatPad)==0;
    const bool down=keys ? KeyHeld(c.sightZoomKey)
                         : padButton && c.sightZoomButton && (At<std::uint16_t>(s,kSeatButtons)&static_cast<std::uint16_t>(c.sightZoomButton))!=0;
    if(MapHoldsKeys()){toggle.held=true;Publish(v,seat,sightzoom::At(toggle.step));return;}
    if(down && !toggle.held) {
        toggle.step=sightzoom::Next(toggle.step);
        Log("SIGHTZOOM v=%p seat %u: %.0fx by the %s",v,seat,sightzoom::At(toggle.step),keys ? "key" : "pad button");
    }
    toggle.held=down;
    Publish(v,seat,sightzoom::At(toggle.step));
}

void SightZoomStock(unsigned char* v) noexcept {
    if(!Cfg().enabled || !Cfg().sightZoom){ResetSightZoom();return;}
    if(v[kDead]){if(toggle.ref.Is(v))ResetSightZoom();return;}
    const unsigned n=SeatCount(v);
    for(unsigned i=0;i<n;++i) {
        if(SeatRider(SeatAt(v,i))!=Rider::player || At<const void*>(SeatAt(v,i),kSeatRider)!=PlayerHuman())continue;
        const bool reserved=i==0 && ((HighCamOffered(v) && (Cfg().highCamButton & Cfg().sightZoomButton)!=0) ||
            (Cfg().sazabi && IsSazabi(v) && (Cfg().sazabiLockButton & Cfg().sightZoomButton)!=0));
        SightZoomFrame(v,i,!reserved);
        return;
    }
    if(toggle.ref.Is(v))ResetSightZoom();
}

bool SightZoomMounted(const void* vehicle) noexcept {
    if(MapOwnsView())return false;
    const Cue c=Snapshot();
    __try { return (!vehicle || c.vehicle.obj==vehicle) && c.sight.mounted && c.zoom>1.0f && Current(c); }
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

float SightZoomNow(const void* vehicle) noexcept {
    const Cue c=Snapshot();
    __try {
        if((vehicle && c.vehicle.obj!=vehicle) || !Current(c))return 1.0f;
        return c.zoom;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return 1.0f;}
}

sightzoom::Kind SightZoomView(const void* vehicle) noexcept {
    if(MapOwnsView())return sightzoom::Kind::none;
    const Cue c=Snapshot();
    __try {
        return (!vehicle || c.vehicle.obj==vehicle) && Current(c) ? c.sight.kind : sightzoom::Kind::none;
    } __except(EXCEPTION_EXECUTE_HANDLER){return sightzoom::Kind::none;}
}

void ResetSightZoom() noexcept {
    toggle=Toggle{};
    AcquireSRWLockExclusive(&cueLock);
    cue=Cue{};
    ReleaseSRWLockExclusive(&cueLock);
    // The camera hook still owns its last FOV write until that camera next steps.
}

}  // namespace crew
