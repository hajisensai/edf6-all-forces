// The Rune Blade at runtime (runeblade.h; the design: docs/newclass-re/runeblade-plan.md §2).
//  - The frame: AssultSoldier's live vtable slot 5 chained (edf::ChainVtableSlot, as vehicler_foot.cpp: the new classes'
//    clones copy every slot they do not own before each object is made). Only a ZwRuneBlade's frame (its vptr the clone's,
//    soldier::zwVtables[1]: one compare) does anything. On its own machine (+0x128 bit 0 clear) the frame's jump press /
//    hold, support, stick, yaw and carry go through runeblade::StepFrame before the stock frame: a step writes the game's
//    own jump request (+0x1294 speed, +0x1290 flag: consumed at 0x575A7A by 0x11B9450, which also tells the other players)
//    and the level carry; after the stock frame a glide holds the fall. The gauge is the object's +0x304 / +0x308 / +0x2FC.
//  - A copy on another machine: its gauge is written by the channel (the owner's word), nothing else.
//  - The channel "runeblade" (ZWRB, runeblade_net.h): this machine's Rune Blades' state each update (paced), their steps
//    and combo stages at once; the receive takes only the player's own word (SenderIsPlayer).
#include "runeblade.h"
#include "runeblade_combo.h"
#include "runeblade_ether.h"
#include "runeblade_net.h"
#include "runeblade_npc_logic.h"
#include "soldier_factory.h"
#include "command_identity.h"
#include "edf_layout.h"
#include "firstperson.h"
#include "memory.h"
#include "plugin_log.h"
#include "plugin_net.h"
#include "scene.h"
#include "edf/patch.h"
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <new>

namespace crew {
namespace {
namespace rb=runeblade;
using FrameFn=void(__fastcall*)(void*,void*,void*,void*);
constexpr std::size_t kSoldierVtable=0x17CDF28,kFrameSlot=5,kStockFrame=0x550C30;
void* nextFrame=nullptr;   // set before the slot is written (ChainVtableSlot)
bool installed=false;

// The plugin's part of a Rune Blade (soldier::SetExtension; freed with the object).
struct Rb {
    rb::Motion motion;
    rb::combo::State combo;
    rb::net::Pace pace;
    std::uint32_t paceGeneration=0;   // joins seen when the pace was last good (a newer join: the state again)
    std::uint8_t tag=0,stepSerial=0,stateSerial=0;
    bool gliding=false;               // this frame's (its own machine), or as last heard (a copy)
    bool initialised=false;
    bool takeoff=false;               // an NPC's takeoff asked (RuneBladeNpcTakeoff), taken by its next frame on the ground
};
void Release(void* p) noexcept { delete static_cast<Rb*>(p); }

std::uint32_t joinGeneration=0;
bool netReady=false;
unsigned logs=0;
rb::StepCueFn stepCue=nullptr;
rb::RemoteStageFn remoteStage=nullptr;

// The HUD's copy (the draw thread reads it).
SRWLOCK hudLock=SRWLOCK_INIT;
RuneBladeHudView hudView{};
bool hudHave=false;
ULONGLONG hudAt=0;
constexpr ULONGLONG kHudStaleMs=500;

bool IsRb(const void* o) noexcept {
    const void* vt=soldier::zwVtables[1];
    return vt && o && *static_cast<const void* const*>(o)==vt;
}
bool Remote(const void* o) noexcept { return (At<std::uint16_t>(o,rb::kNetFlags)&1u)!=0; }
Rb* Ext(void* o,bool make) noexcept {
    auto* e=static_cast<Rb*>(soldier::Extension(o));
    if(e || !make)return e;
    e=new(std::nothrow) Rb{};
    if(!e)return nullptr;
    if(!soldier::SetExtension(o,e,&Release)){delete e;return nullptr;}
    return e;
}
rb::Ether Read(const void* o) noexcept {
    return rb::Sane(At<float>(o,rb::kEtherLevelAt),At<float>(o,rb::kEtherMaxAt),At<std::int32_t>(o,rb::kEtherDepletedAt)!=0);
}
void Write(void* o,const rb::Ether& e) noexcept {
    Put<float>(o,rb::kEtherMaxAt,e.max);Put<float>(o,rb::kEtherLevelAt,e.level);
    Put<std::int32_t>(o,rb::kEtherDepletedAt,e.depleted ? 1 : 0);
}
bool Able(const unsigned char* o) noexcept {
    return !o[kDead] && !(At<std::uint32_t>(o,rb::kHumanFlags)&rb::kRiding) && At<std::int32_t>(o,rb::kAttach)==0;
}
bool OwnedHere(const void* o) noexcept { return IsRb(o) && !Remote(o) && !static_cast<const unsigned char*>(o)[kDead]; }

void Tell(void* o,const rb::net::Msg& m) noexcept {
    if(!netReady)return;
    unsigned char bytes[rb::net::kWire];
    if(rb::net::Encode(m,bytes,sizeof(bytes)))plugin_net::Broadcast(bytes,sizeof(bytes));
    (void)o;
}
void TellCue(void* o,Rb& e,rb::Cue cue) noexcept {
    if(stepCue)stepCue(o,cue,true);
    rb::net::Msg m{};
    if(!netReady || !PlayerRequesterOf(o,m.player))return;
    m.kind=rb::net::Kind::step;m.serial=++e.stepSerial;m.cue=static_cast<std::uint8_t>(cue);
    Tell(o,m);
}

void Publish(const Rb& e,const rb::Ether& ether) noexcept {
    AcquireSRWLockExclusive(&hudLock);
    hudView=RuneBladeHudView{ether.level,ether.max,ether.depleted,e.gliding,e.motion.steps};
    hudHave=true;hudAt=GameMs();
    ReleaseSRWLockExclusive(&hudLock);
}

// This machine's Rune Blade's frame, around the stock one.
void OwnFrame(void* self,void* a,void* b,void* c) noexcept {
    const auto next=reinterpret_cast<FrameFn>(nextFrame);
    auto* h=static_cast<unsigned char*>(self);
    Rb* const e=Ext(self,true);
    if(!e){next(self,a,b,c);return;}
    rb::Ether ether=Read(self);
    if(!e->initialised){ether=rb::Ether{};e->initialised=true;}
    rb::StepIn in;
    in.press=h[rb::kJumpPress]!=0;in.hold=h[rb::kJumpHold]!=0;
    in.ground=h[rb::kSupport]==rb::kOnGround;in.able=Able(h);
    const float* stick=reinterpret_cast<const float*>(h+rb::kMoveStick);
    in.stick[0]=stick[0];in.stick[1]=stick[2];
    in.yaw=At<float>(h,rb::kLookYaw);
    std::memcpy(in.carry,h+rb::kCarry,sizeof(in.carry));
    const rb::StepOut out=rb::StepFrame(e->motion,ether,in);
    e->gliding=out.glide;
    if(out.jump){Put<float>(h,rb::kJumpSpeed,rb::kStepUp);h[rb::kJumpFlag]=1;}
    // An NPC's takeoff (runeblade_npc_logic.h): the game's own jump request from the ground, at the stock jump's speed
    // (0x5669A8 walk+0x30 = 10); 0x575A7A takes it on the ground too (0x11B9450 clears the support +0x711 and detaches),
    // and tells the peers as any jump of this machine's (0x575AB9). Free, as the player's ground jump.
    const bool npcTakeoff=e->takeoff && in.ground && in.able && !out.jump;
    e->takeoff=false;
    if(npcTakeoff){Put<float>(h,rb::kJumpSpeed,rb::npchop::kTakeoffUp);h[rb::kJumpFlag]=1;if(stepCue)stepCue(self,rb::Cue::takeoff,true);}
    if(out.push){float* carry=reinterpret_cast<float*>(h+rb::kCarry);carry[0]=out.pushX;carry[2]=out.pushZ;}
    Write(self,ether);
    if(out.cue!=rb::Cue::none)TellCue(self,*e,out.cue);
    next(self,a,b,c);
    if(out.glide) {
        float* carry=reinterpret_cast<float*>(h+rb::kCarry);
        carry[1]=rb::GlideFall(carry[1]);
    }
    if(self==PlayerHuman())Publish(*e,Read(self));
}

void __fastcall FrameHook(void* self,void* a,void* b,void* c) noexcept {
    const auto next=reinterpret_cast<FrameFn>(nextFrame);
    if(!IsRb(self) || Remote(self)){next(self,a,b,c);return;}
    OwnFrame(self,a,b,c);
}

// ---- the channel ----
void SendState(void* o) noexcept {
    Rb* const e=Ext(o,false);
    if(!e || !e->initialised)return;
    rb::net::Msg m{};
    int slot=-1;
    if(!PlayerRequesterOf(o,m.player) || !netref::IsPlayer(m.player,&slot))return;
    const rb::Ether ether=Read(o);
    const std::uint8_t flags=static_cast<std::uint8_t>((ether.depleted ? rb::net::kDepleted : 0)|(e->gliding ? rb::net::kGliding : 0));
    const ULONGLONG ms=GameMs();
    if(e->paceGeneration!=joinGeneration){e->pace.sent=false;e->paceGeneration=joinGeneration;}
    if(!rb::net::Due(e->pace,ether.level,flags,ms))return;
    m.kind=rb::net::Kind::state;m.ether=ether.level;m.flags=flags;m.serial=++e->stateSerial;
    unsigned char bytes[rb::net::kWire];
    const auto key=plugin_net::StateKey(plugin_net::StateStream::runeBlade,static_cast<std::uint32_t>(slot));
    if(!key || !rb::net::Encode(m,bytes,sizeof(bytes)))return;
    plugin_net::BroadcastState(key,bytes,sizeof(bytes));
    rb::net::Sent(e->pace,ether.level,flags,ms);
}

bool Receive(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    rb::net::Msg m{};
    if(!rb::net::Decode(bytes,size,m)) {
        if(logs++<16)Log("RUNEBLADE peer %u: a message that does not read (%zu bytes): dropped",peer,size);
        return true;
    }
    if(!netReady)return true;
    __try {
        MarkIdentities who{};
        if(!SenderIsPlayer(m.player,puid,&who) || !who.player) {
            if(logs++<16)Log("RUNEBLADE peer %u: kind %u for a player not its own (or not here): dropped",peer,
                             static_cast<unsigned>(m.kind));
            return true;
        }
        void* const o=const_cast<void*>(who.player.obj);
        if(!IsRb(o) || !Remote(o))return true;   // not a Rune Blade, or this machine's own
        switch(m.kind) {
        case rb::net::Kind::state: {
            rb::Ether e=Read(o);
            e.level=m.ether>e.max ? e.max : m.ether;e.depleted=(m.flags&rb::net::kDepleted)!=0;
            Write(o,e);
            if(Rb* const x=Ext(o,true)){x->gliding=(m.flags&rb::net::kGliding)!=0;x->initialised=true;}
            break;
        }
        case rb::net::Kind::step:
            if(stepCue)stepCue(o,static_cast<rb::Cue>(m.cue),false);
            break;
        case rb::net::Kind::stage:
            if(remoteStage)remoteStage(o,m.tag,m.stage);
            break;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return true;
}
void Update(const CommandNetworkContext& context,std::uint64_t) noexcept {
    netReady=context.ready && context.send;
    if(!netReady || !installed)return;
    __try {
        unsigned char* players[4]{};
        const int n=LocalPlayers(players,4);
        for(int i=0;i<n;++i)if(OwnedHere(players[i]))SendState(players[i]);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void Reset() noexcept { netReady=false;++joinGeneration; }
void Joined(std::uint32_t) noexcept { ++joinGeneration; }

bool PrintMatches(std::size_t rva,const unsigned char* bytes,std::size_t n,const char* what) noexcept {
    if(Readable(image+rva,n) && Matches(rva,bytes,n))return true;
    Log("RUNEBLADE the code at EDF.dll+0x%zX (%s) is not what this was written for",rva,what);
    return false;
}
bool PrintsMatch() noexcept {
    static const unsigned char kJumpFlagRead[]={0x40,0x38,0xBE,0x90,0x12,0x00,0x00};        // cmp [rsi+0x1290],dil
    static const unsigned char kJumpSpeedRead[]={0xF3,0x0F,0x10,0xB6,0x94,0x12,0x00,0x00};  // movss xmm6,[rsi+0x1294]
    static const unsigned char kFrame[]={0x40,0x53,0x48,0x81,0xEC,0xE0,0x00,0x00,0x00};     // AssultSoldier slot 5 head
    const bool a=PrintMatches(0x575A7A,kJumpFlagRead,sizeof(kJumpFlagRead),"the jump request's flag read");
    const bool b=PrintMatches(0x575A87,kJumpSpeedRead,sizeof(kJumpSpeedRead),"the jump request's speed read");
    const bool c=PrintMatches(kStockFrame,kFrame,sizeof(kFrame),"AssultSoldier frame head");
    return a && b && c;
}
}  // namespace

// ---- the ether API (runeblade_ether.h) ----
namespace runeblade {
bool IsRuneBlade(const void* soldier) noexcept { return IsRb(soldier) && !static_cast<const unsigned char*>(soldier)[kDead]; }
bool EtherOwned(const void* soldier) noexcept { return OwnedHere(soldier); }
float EtherLevel(const void* soldier) noexcept { return IsRb(soldier) ? Read(soldier).level : -1.0f; }
float EtherMax(const void* soldier) noexcept { return IsRb(soldier) ? Read(soldier).max : -1.0f; }
bool EtherDepleted(const void* soldier) noexcept { return IsRb(soldier) && Read(soldier).depleted; }
bool EtherSpend(void* soldier,float amount) noexcept {
    if(!OwnedHere(soldier))return false;
    rb::Ether e=Read(soldier);
    if(!rb::Spend(e,amount))return false;
    Write(soldier,e);
    return true;
}
float EtherDrain(void* soldier) noexcept {
    if(!OwnedHere(soldier))return 0.0f;
    rb::Ether e=Read(soldier);
    const float taken=rb::Drain(e);
    Write(soldier,e);
    return taken;
}
float EtherBleed(void* soldier,float amount) noexcept {
    if(!OwnedHere(soldier))return 0.0f;
    rb::Ether e=Read(soldier);
    const float taken=rb::Bleed(e,amount);
    Write(soldier,e);
    return taken;
}

// ---- the combo API (runeblade_combo.h) ----
combo::Output ComboFeed(void* soldier,const combo::Spec* spec,const combo::Input& in) noexcept {
    if(!OwnedHere(soldier) || !spec)return combo::Output{};
    Rb* const e=Ext(soldier,true);
    if(!e)return combo::Output{};
    const combo::Output out=combo::Step(e->combo,spec,in);
    if(out.started>=0 && netReady) {
        net::Msg m{};
        if(PlayerRequesterOf(soldier,m.player)) {
            m.kind=net::Kind::stage;m.tag=e->tag;m.stage=static_cast<std::uint8_t>(out.started);
            Tell(soldier,m);
        }
    }
    return out;
}
bool ComboActive(const void* soldier) noexcept {
    const Rb* const e=IsRb(soldier) ? static_cast<const Rb*>(soldier::Extension(soldier)) : nullptr;
    return e && combo::Active(e->combo);
}
int ComboStage(const void* soldier) noexcept {
    const Rb* const e=IsRb(soldier) ? static_cast<const Rb*>(soldier::Extension(soldier)) : nullptr;
    return e ? e->combo.stage : -1;
}
void ComboCancel(void* soldier) noexcept {
    if(Rb* const e=IsRb(soldier) ? Ext(soldier,false) : nullptr)combo::Reset(e->combo);
}
void ComboTag(void* soldier,std::uint8_t weaponTag) noexcept {
    if(Rb* const e=OwnedHere(soldier) ? Ext(soldier,true) : nullptr)e->tag=weaponTag;
}
void SetRemoteStageSink(RemoteStageFn fn) noexcept { remoteStage=fn; }
void SetStepCueSink(StepCueFn fn) noexcept { stepCue=fn; }
}  // namespace runeblade

bool RuneBladeNpcTakeoff(void* soldier) noexcept {
    if(!OwnedHere(soldier) || soldier==PlayerHuman())return false;
    Rb* const e=Ext(soldier,true);
    if(!e)return false;
    e->takeoff=true;
    return true;
}

bool RuneBladeHudNow(RuneBladeHudView* out) noexcept {
    if(!out)return false;
    AcquireSRWLockShared(&hudLock);
    const bool have=hudHave && GameMs()-hudAt<kHudStaleMs;
    if(have)*out=hudView;
    ReleaseSRWLockShared(&hudLock);
    return have;
}

plugin_net::Channel RuneBladeNetChannel() noexcept {
    plugin_net::Channel c;
    c.name="runeblade";c.magic=rb::net::kMagic;c.version=rb::net::kVersion;
    c.minSize=c.maxSize=rb::net::kWire;c.takesState=&rb::net::TakesState;
    c.receive=&Receive;c.update=&Update;c.reset=&Reset;c.joined=&Joined;
    return c;
}

bool InstallRuneBlade() noexcept {
    __try {
        if(!PrintsMatch())return false;
        auto** slot=reinterpret_cast<void**>(image+kSoldierVtable)+kFrameSlot;
        if(*slot!=image+kStockFrame)Log("RUNEBLADE AssultSoldier frame (slot 5): chaining onto %p",*slot);
        installed=edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&FrameHook),&nextFrame);
        Log("HOOK runeblade: frame chained=%d (ether gauge +0x304/+0x308/+0x2FC, rune steps, glide, combo state, ZWRB)",installed);
        return installed;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
