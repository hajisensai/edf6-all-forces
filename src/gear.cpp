// Landing gear (the user, 2026-10-05: "give the planes landing gear that retracts and extends"). The fixed-wing jet
// models carry three gear legs (pylib/jet_gear.py: bones gear_nose, gear_main_l, gear_main_r under the body bone, each
// hinged along its local X at its strut's top; the bind pose is the gear down). Like the elevons (playerjet.cpp Elevons,
// body506.cpp HingePose) the plugin writes each leg's local matrix every frame: Rx(at * kLegUp[leg]) x bind, `at` 0 down
// .. 1 up; the engine makes world = local x parent's world. A model without the three bones (the carrier, the drones,
// the Primer fighter, a stock bomber) has no gear: nothing is written and the jet flies as before.
//  - Retracting, the nose leg starts first and the mains after it (kLegStart), each kLegSeconds from down to up;
//    extending, the mains first. A command the other way turns the legs back where they are.
//  - The player's jet (PlayerGear, from playerjet.cpp): the gear key (ini PlayerJetGearKey, G) or pad button (ini
//    PlayerJetGearButton, L3) toggles it; up is refused while it is on the ground (weight on the wheels). Down it adds
//    kGearDrag of the clean jet's parasitic drag (Air: full throttle then makes ~165 m/s instead of 260) and the cockpit
//    warns over kGearLimit (250 kt); a touchdown without it down and locked is a belly landing (Touch: a crash, then it
//    slides to a stop on its belly). Low and slow with it not down, the cockpit says GEAR (kWarn*).
//  - NPC jets (NpcGear, from jet_flight.cpp Wing): up in flight, down under kNpcGearHeight and kNpcGearSpeed.
// The jet's collision box is the model's with the gear down (vcobjects.jet_sgo), so the gear never changes how it sits
// on the ground: with it up after a belly landing the box still holds the body at the wheels' height.
#include "gear.h"
#include "body506.h"
#include "vecmath.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using vec::Clamp;
// pylib/jet_gear.py LEG_UP (tools/selftest.py holds them equal): each leg folds level into the body at its angle.
constexpr float kLegUp[kGearLegs]={1.578f,1.821f,1.821f};
const wchar_t* const kLegNames[kGearLegs]={L"gear_nose",L"gear_main_l",L"gear_main_r"};
constexpr float kLegSeconds=2.5f;                           // one leg, down to up
constexpr float kLegStartUp[kGearLegs]={0.0f,0.6f,0.9f};    // s after an up command each leg starts: the nose first
constexpr float kLegStartDown[kGearLegs]={0.6f,0.0f,0.3f};  // ...after a down command: the mains first
constexpr float kGearDrag=1.5f;
constexpr float kWarnHeight=150.0f,kWarnSpeed=90.0f;        // m, m/s: under both (and not climbing) with it not down: GEAR
constexpr float kNpcGearHeight=60.0f,kNpcGearSpeed=70.0f;   // m, m/s
constexpr int kMaxGear=96;   // every NPC jet (jet.cpp kMaxJets 64) and player jet (playerjet.cpp 16) at once, gearless ones too
constexpr ULONGLONG kStaleFrames=120;   // an entry not stepped for this many frames is free (its jet gone or gearless)

struct Gear {
    ObjRef ref;
    const unsigned char* model;   // the bone array the legs were found in
    unsigned char* rec[kGearLegs];
    float bind[kGearLegs][16];
    GearState state;
    float since;                  // s since the last command
    bool seen;
    bool held;                    // the player's gear key / button down last frame
    ULONGLONG blockedAt;          // GetTickCount64 of its last refused up command
    ULONGLONG frame;              // GameFrame of its last step
};
Gear table[kMaxGear]{};

Gear* Find(const void* v) noexcept {
    for(auto& g:table)if(g.ref.Is(v))return &g;
    return nullptr;
}
// v's entry, a new one in a free slot (never used, or not stepped for kStaleFrames); nullptr with none free.
Gear* Make(const void* v) noexcept {
    const ULONGLONG now=GameFrame();
    Gear* g=Find(v);
    for(auto& e:table) {
        if(g)break;
        if(e.ref && now-e.frame<=kStaleFrames)continue;
        e=Gear{};e.ref=ObjRef::Of(v);
        g=&e;
    }
    if(g)g->frame=now;
    else if(static bool said=false;!said){said=true;Log("GEAR v=%p: %d jets known already, its gear not posed (said once)",v,kMaxGear);}
    return g;
}

// The legs' bone records, looked up again when the model's bone array changes; false: not all three there.
bool Bones(Gear& g,const unsigned char* v) noexcept {
    const unsigned char* inst=v+kModelInst506;
    const auto bones=At<const unsigned char*>(inst,kInstBones506);
    if(!bones)return false;
    if(bones!=g.model) {
        g.model=bones;g.state.fitted=true;
        for(int i=0;i<kGearLegs;++i) {
            g.rec[i]=BoneRecord506(inst,kLegNames[i]);
            if(g.rec[i])std::memcpy(g.bind[i],g.rec[i]+kBoneLocal506,64);
            else g.state.fitted=false;
        }
        if(Cfg().debug)Log("GEAR v=%p: %s",v,g.state.fitted ? "three legs found" : "none in this model");
    }
    return g.state.fitted;
}

void Pose(Gear& g) noexcept {
    for(int i=0;i<kGearLegs;++i) {
        alignas(16) float m[16];
        HingePose(g.bind[i],g.state.at[i]*kLegUp[i],m);
        std::memcpy(g.rec[i]+kBoneLocal506,m,64);
    }
}

void Travel(Gear& g,float dt,bool snap) noexcept {
    GearState& s=g.state;
    const float want=s.wantUp ? 1.0f : 0.0f;
    g.since+=dt;
    s.down=s.up=true;
    for(int i=0;i<kGearLegs;++i) {
        const float start=s.wantUp ? kLegStartUp[i] : kLegStartDown[i];
        if(snap)s.at[i]=want;
        else if(g.since>=start)s.at[i]+=Clamp(want-s.at[i],-dt/kLegSeconds,dt/kLegSeconds);
        s.down=s.down && s.at[i]<=0.0f;
        s.up=s.up && s.at[i]>=1.0f;
    }
}

// The cockpit's copy (hud.cpp draws it): written here on the game thread, read whole under the lock on the draw thread.
SRWLOCK hudLock=SRWLOCK_INIT;
GearHud hud{};
void Publish(const GearHud& h) noexcept {
    AcquireSRWLockExclusive(&hudLock);
    hud=h;
    ReleaseSRWLockExclusive(&hudLock);
}
}  // namespace

GearState GearStep(unsigned char* v,bool wantUp,float dt,bool snap) noexcept {
    Gear* g=Make(v);
    if(!g || !Bones(*g,v))return GearState{};
    if(!g->seen){g->seen=true;snap=true;}
    if(wantUp!=g->state.wantUp){g->state.wantUp=wantUp;g->since=0.0f;}
    Travel(*g,dt,snap);
    Pose(*g);
    return g->state;
}

GearState GearOf(const void* v) noexcept {
    const Gear* g=Find(v);
    return g && g->seen ? g->state : GearState{};
}

void NpcGear(unsigned char* v,float speed,float dt) noexcept {
    bool up=true;
    if(speed<kNpcGearSpeed) {   // only then worth a ray: a landing or a taxi
        const float clear=GroundClearance(reinterpret_cast<const float*>(v+kPosition));
        up=clear==kNoGround || clear>kNpcGearHeight;
    }
    GearStep(v,up,dt,false);
}

void ResetGear() noexcept {
    for(auto& g:table)g=Gear{};
    GearHudClear();
}

GearState PlayerGear(unsigned char* v,bool held,bool keys,bool air,float speed,float clear,float climb,float dt) noexcept {
    Gear* g=Make(v);
    if(!g || !Bones(*g,v)){GearHudClear();return GearState{};}
    bool want=g->seen ? g->state.wantUp : air;   // first seen: up in the air (the catch's jet), down on the ground
    const bool pressed=held && !g->held;
    const ULONGLONG now=GetTickCount64();
    g->held=held;
    if(pressed) {
        const bool blocked=!want && !air;
        if(blocked)g->blockedAt=now;
        else want=!want;
        Log("PJET v=%p gear %s%s",v,blocked ? "up" : want ? "up" : "down",blocked ? ": refused, weight on the wheels" : "");
    }
    const GearState s=GearStep(v,want,dt,false);
    GearHud h{};
    h.shown=true;std::memcpy(h.at,s.at,sizeof(h.at));h.wantUp=s.wantUp;
    h.overspeed=!s.up && speed>kGearLimit;
    h.warn=air && !s.down && clear!=kNoGround && clear<kWarnHeight && speed<kWarnSpeed && climb<0.0f;
    h.blocked=g->blockedAt && now-g->blockedAt<kGearBlockedMs;h.keys=keys;h.tick=now;
    Publish(h);
    return s;
}

float GearDragShare(const void* v) noexcept {
    const GearState s=GearOf(v);
    if(!s.fitted)return 0.0f;
    float out=0.0f;
    for(float a:s.at)out+=1.0f-a;
    return kGearDrag*out/static_cast<float>(kGearLegs);
}

bool GearDown(const void* v) noexcept {
    const GearState s=GearOf(v);
    return !s.fitted || s.down;
}

void GearHudClear() noexcept {
    Publish(GearHud{});
}

bool GearHudLatest(GearHud* out) noexcept {
    AcquireSRWLockShared(&hudLock);
    *out=hud;
    ReleaseSRWLockShared(&hudLock);
    return out->shown;
}
}  // namespace crew
