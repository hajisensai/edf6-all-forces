// The commander mode: commander.h (what and why), commander_logic.h (its decisions), docs/feature-2026-11-21-commander-mode.md.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
//  - The walk: the native every-team walk 0x5E0C80(manager, visitor) (mission_participants.cpp's, team_walk.h checks its
//    rows first): a Soldier with a lobby user (+0x1ED0) is a player's (every machine's players: mission_participants.cpp),
//    one without is an NPC's; deleted from the scene (+0x18 bit 2) skipped, dead (+0x2E8) not counted.
//  - The guard: +0x380 bits 0 (NoHpLoss: ApplyDamage 0x547C30 leaves the HP of an unforced hit, docs/edf41-classes.md
//    §2c) and 7 (NoTarget: the hostile walk 0x5E0F20 skips it at 0x5E0FBF, pvp_native.cpp's commander), only the bits this
//    module set given back (enemy_control.cpp ProtectSource's rule).
#include "commander.h"
#include "commander_logic.h"
#include "command_identity.h"
#include "host_settings.h"
#include "layout.h"
#include "map.h"
#include "memory.h"
#include "npc_arms.h"
#include "npcai.h"
#include "pvp.h"
#include "support_soldier.h"
#include "team_walk.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kEnumAllTeams=0x5E0C80,kTeamManager=0x20B2978;
constexpr std::size_t kUser=0x1ED0,kSceneFlags=0x18,kReadable=0x1EE0;
constexpr std::uint32_t kGuardBits=0x81u;     // NoHpLoss | NoTarget
constexpr ULONGLONG kWalkMs=250;
constexpr float kAhead=3.0f;                  // m in front of the requester's soldier: its NPC's spot
constexpr float kPlanReach=6.0f;              // m: a plan's soldier no farther than this from its target (Validate)
constexpr unsigned kMostDeployments=16,kMostPlayers=16;

struct Deployment {
    std::uint64_t transaction=0;
    WeakRef soldier{};   // held (objref.h)
    unsigned char requester[32]{};
    WeakRef leader{};
    bool remote=false,own=false,active=false;
};
Deployment deployments[kMostDeployments]{};
struct Guard { WeakRef ref{}; std::uint32_t added=0; };
Guard guards[kMostPlayers]{};

commander::Request request{};
commander::Watch watch{};
bool on=false,released=false,lost=false,submitting=false;
std::uint32_t requestId=0;
ULONGLONG walkedAt=0;
std::uint64_t nextOffline=0x4000000000000000ULL;   // offline transactions: their own range (mission crews' 0x8000...)
// Offline: the plan made, waiting on its weapon's load (npc_arms.h NpcArmsLoaded) before it is applied.
bool offlinePending=false;
SupportPlan offlinePlan{};

using objref::Alive;
Deployment* Find(std::uint64_t tx) noexcept {
    for(auto& d:deployments)if(d.transaction==tx)return &d;
    return nullptr;
}
Deployment* Free() noexcept {for(auto& d:deployments)if(!d.transaction)return &d;return nullptr;}

// --- the walk ---
struct Walk {
    const unsigned char* players[kMostPlayers]{};
    unsigned playerCount=0;
    int friendly=0;            // live friendly NPC soldiers
    int playerTeam=0;
    bool failed=false;
};
struct Visitor { const void* const* vtable; Walk* walk; };
void __fastcall Ignore(void*) noexcept {}
void __fastcall Visit(Visitor* v,const unsigned char* object) noexcept {
    Walk& w=*v->walk;
    if(w.failed || !object)return;
    __try {
        if(!IsSoldierClass(object) || !Readable(object,kReadable) || (At<unsigned char>(object,kSceneFlags)&4))return;
        if(At<const void*>(object,kUser)) {
            for(unsigned i=0;i<w.playerCount;++i)if(w.players[i]==object)return;
            if(w.playerCount<kMostPlayers)w.players[w.playerCount++]=object;
            return;
        }
        if(object[kDead])return;
        const int team=At<int>(object,kTeam);
        if(team==w.playerTeam || team==kTeamFriend)++w.friendly;
    } __except(EXCEPTION_EXECUTE_HANDLER){w.failed=true;}
}
const void* const kVisitorVtable[]={reinterpret_cast<const void*>(&Ignore),reinterpret_cast<const void*>(&Visit)};
bool WalkTeams(const unsigned char* me,Walk* out) noexcept {
    __try {
        void* manager=At<void*>(image,kTeamManager);
        if(!Readable(manager,0x50) || !team_walk::WalkRows(manager))return false;
        out->playerTeam=At<int>(me,kTeam);
        Visitor visitor{kVisitorVtable,out};
        reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kEnumAllTeams)(manager,&visitor);
        return !out->failed && At<void*>(image,kTeamManager)==manager;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// --- the guard ---
void GuardPlayer(const unsigned char* soldier) noexcept {
    Guard* g=nullptr;
    for(auto& row:guards)if(row.ref.obj==soldier){g=&row;break;}
    __try {
        if(g && !g->ref.Is(soldier)){g->ref.Reset();g->added=0;}   // another object at the address: a fresh row
        if(!g)for(auto& row:guards)if(!row.ref.obj){g=&row;break;}
        if(!g)return;
        auto* object=const_cast<unsigned char*>(soldier);
        const auto flags=At<std::uint32_t>(object,kObjectFlags);
        const auto missing=kGuardBits&~flags;
        if(!missing)return;
        Put<std::uint32_t>(object,kObjectFlags,flags|kGuardBits);
        g->ref=ObjRef::Of(object);g->added|=missing;
        Log("COMMANDER guard: soldier soldier %p NoTarget/NoHpLoss (+0x380 |= 0x%X)",soldier,missing);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}
void ReleaseGuards(const char* why) noexcept {
    for(auto& g:guards) {
        if(g.ref.obj && g.added) {
            __try {
                if(objref::Identity(g.ref)>=objref::Life::removed) {   // the object still there (dead or deleted too)
                    auto* object=const_cast<void*>(g.ref.obj);
                    Put<std::uint32_t>(object,kObjectFlags,At<std::uint32_t>(object,kObjectFlags)&~g.added);
                    Log("COMMANDER guard released on %p (+0x380 &= ~0x%X): %s",g.ref.obj,g.added,why);
                }
            } __except(EXCEPTION_EXECUTE_HANDLER){}
        }
        g.ref.Reset();g.added=0;
    }
}

// --- the defeat ---
// The stock death of this machine's own player (its authority), so the stock "every player down" ends the mission.
bool KillLocalPlayer(unsigned char* human) noexcept;

void Release(const char* why) noexcept {
    if(released)return;
    released=true;
    ReleaseGuards(why);
    MapSetCommanderMode(false);
    Log("COMMANDER mode ends for this player this mission: %s",why);
}

// --- the NPC ---
bool BuildPlan(const unsigned char* soldier,int kind,SupportPlan* out) noexcept {
    if(!soldier || !out || !commander::CommanderKind(static_cast<std::uint32_t>(kind)))return false;
    *out=SupportPlan{};
    out->catalogId=commander::kCatalog;out->count=1;
    auto& u=out->units[0];
    u.resourceId=SupportSoldierResource(static_cast<SupportWeapon>(kind),true);
    __try {
        if(!Readable(soldier,kMatrix+64))return false;
        const float* m=reinterpret_cast<const float*>(soldier+kMatrix);
        float fx=m[8],fz=m[10];
        const float len=std::sqrt(fx*fx+fz*fz);
        if(!(len>1e-3f) || !std::isfinite(len)){fx=0;fz=1;}else{fx/=len;fz/=len;}
        const float* p=reinterpret_cast<const float*>(soldier+kPosition);
        if(!std::isfinite(p[0]+p[1]+p[2]))return false;
        std::memcpy(out->target,p,12);
        // Facing the requester's way: rows right, up, forward (the stock's), then the spot.
        u.matrix[0]=fz;u.matrix[2]=-fx;u.matrix[5]=1;u.matrix[8]=fx;u.matrix[10]=fz;u.matrix[15]=1;
        u.matrix[12]=p[0]+fx*kAhead;u.matrix[13]=p[1];u.matrix[14]=p[2]+fz*kAhead;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
    return true;
}

bool Apply(std::uint64_t tx,const SupportPlan& plan,bool remote) noexcept {
    if(Deployment* d=Find(tx))return d->soldier.obj!=nullptr;
    Deployment* d=Free();
    if(!d)return false;
    const auto& u=plan.units[0];
    const bool networked=InSession();
    ObjRef soldier;
    if(!ApplySupportSoldierResource(u.matrix,u.resourceId,networked ? u.netId : nullptr,!networked,&soldier,nullptr,u.arms)) {
        Log("COMMANDER spawn %llu: soldier (resource %X) not created",static_cast<unsigned long long>(tx),u.resourceId);
        return false;
    }
    if(!HoldSupportSoldier(soldier,true)){DeleteSupportSoldier(soldier);return false;}
    *d=Deployment{};
    d->transaction=tx;d->soldier=soldier;d->remote=remote;
    std::memcpy(d->requester,plan.requester,sizeof(d->requester));
    d->own=!networked || SupportPlanRequesterIsLocal(plan);
    Log("COMMANDER spawn %llu: soldier kind %u at (%.0f,%.0f,%.0f), weapon %u, %s%s",static_cast<unsigned long long>(tx),
        u.resourceId>>8,u.matrix[12],u.matrix[13],u.matrix[14],static_cast<unsigned>(u.arms&0xFFFFu),
        networked ? "networked" : "offline",d->own ? " (this player's)" : "");
    return true;
}

// Every machine, every frame: a made soldier let go once its transaction is active (offline at once), then following its
// requester's player (the leader every peer resolves from the plan's requester; offline this machine's player).
void Activate() noexcept {
    for(auto& d:deployments) {
        if(!d.transaction || d.active || !d.soldier.obj)continue;
        if(InSession() && !SupportTransactionActive(d.transaction))continue;
        d.active=true;
        if(!Alive(d.soldier))continue;
        HoldSupportSoldier(d.soldier,false);
        ObjRef leader{};
        if(!InSession()) {
            if(unsigned char* me=PlayerHuman())leader=ObjRef::Of(me);
        } else {
            MarkIdentities ids{};
            if(ResolveMarkIdentities(d.requester,nullptr,&ids))leader=ids.player;
        }
        d.leader=leader;
        const bool follows=leader && FollowSupportSoldier(d.soldier,leader);
        Log("COMMANDER %llu active: %s",static_cast<unsigned long long>(d.transaction),
            follows ? "follows its commander's soldier (the map's squad of that player)" :
                      "its commander's soldier not found / follow refused: it stands as a free squad");
    }
}

void Ask(unsigned char* human,ULONGLONG now) noexcept {
    const auto next=commander::NextAsk(request,SupportSoldiersReady() && !human[kDead],now);
    if(next==commander::Ask::wait)return;
    if(next==commander::Ask::giveUp){Release("no NPC came of the requests (see the COMMANDER / SUPPORT lines above)");return;}
    ++request.tries;request.at=now;
    int cls=-1,equipped[npctier::kWeaponSlots];
    if(!LocalPlayerLoadout(&cls,equipped,npctier::kWeaponSlots)){Log("COMMANDER request: the player's loadout is not readable");return;}
    const int kind=commander::KindFor(cls,equipped,npctier::kWeaponSlots);
    if(kind<0)return;
    if(!InSession()) {
        if(!BuildPlan(human,kind,&offlinePlan))return;
        support_net::RequesterArms arms;CommanderRequesterArms(kind,&arms);
        offlinePlan.units[0].arms=arms.kind[kind];
        offlinePending=true;
        Log("COMMANDER request (offline, try %d): class %d -> soldier kind %d",request.tries,cls,kind);
        return;
    }
    const float* p=reinterpret_cast<const float*>(human+kPosition);
    wchar_t note[128]{};
    submitting=true;requestId=0;
    const bool sent=SubmitSupportRequest(static_cast<int>(commander::kCatalog),p,note,_countof(note),static_cast<std::uint64_t>(kind));
    submitting=false;
    Log("COMMANDER request (try %d): class %d -> soldier kind %d %s (request %u)",request.tries,cls,kind,
        sent ? "sent to the host" : "not sent",requestId);
}

void OfflineTick() noexcept {
    if(!offlinePending)return;
    const auto loaded=NpcArmsLoaded(offlinePlan);
    if(loaded==support_net::PlanResult::pending)return;
    offlinePending=false;
    if(loaded==support_net::PlanResult::refused){Log("COMMANDER offline: its weapon cannot be loaded");return;}
    if(ValidateCommanderPlan(offlinePlan))Apply(nextOffline++,offlinePlan,false);
}
}  // namespace

const unsigned char* CommanderAvatar(const unsigned char* requester) noexcept {
    if(!requester || !CommanderModeInEffect() || released || lost)return nullptr;
    for(const auto& d:deployments)if(d.active && d.leader.Is(requester) && Alive(d.soldier))
        return static_cast<const unsigned char*>(d.soldier.obj);
    return nullptr;
}

support_net::PlanResult PlanCommander(std::uint32_t peer,const float* target,std::uint64_t loadout,SupportPlan* out) noexcept {
    using support_net::PlanResult;
    (void)target;
    PvpStatus pvp{};
    if(!out || !CommanderModeInEffect() || (ReadPvpStatus(&pvp) && pvp.enabled) || loadout>=static_cast<std::uint64_t>(kSupportWeaponCount) ||
       !commander::CommanderKind(static_cast<std::uint32_t>(loadout)) || !SupportSoldiersReady()) {
        Log("COMMANDER plan for peer %u refused: the mode is off here, a PvP match, kind %llu or soldiers not ready",peer,
            static_cast<unsigned long long>(loadout));
        return PlanResult::refused;
    }
    ObjRef requester{};
    if(!SupportAuthenticatedRequester(peer,&requester) || !Alive(requester)) {
        Log("COMMANDER plan for peer %u refused: its player is not found alive in the sealed world",peer);
        return PlanResult::refused;
    }
    // One NPC a commander: a second request while its first is here (made, not yet lost) is refused.
    unsigned char id[32]{};
    if(PlayerRequesterOf(requester,id))
        for(const auto& d:deployments)
            if(d.transaction && !std::memcmp(d.requester,id,sizeof(id)) && (!d.active || Alive(d.soldier))) {
                Log("COMMANDER plan for peer %u refused: its NPC is already here",peer);
                return PlanResult::refused;
            }
    if(!BuildPlan(static_cast<const unsigned char*>(requester.obj),static_cast<int>(loadout),out))return PlanResult::refused;
    Log("COMMANDER plan for peer %u: soldier kind %llu at its player",peer,static_cast<unsigned long long>(loadout));
    return PlanResult::ready;
}

bool ValidateCommanderPlan(const SupportPlan& p) noexcept {
    if(p.catalogId!=commander::kCatalog || p.count!=1 || !CommanderModeInEffect() || !SupportSoldiersReady())return false;
    const auto& u=p.units[0];
    if(!IsSupportLeaderResource(u.resourceId) || u.role || u.variant)return false;
    const auto kind=SupportSoldierWeapon(u.resourceId);
    if(!commander::CommanderKind(static_cast<std::uint32_t>(kind)) || !SupportKindInstalled(kind))return false;
    if(u.arms && !SupportSoldierArmsReady())return false;   // a player weapon needs the weapon hooks (support_dispatch Validate)
    const float dx=u.matrix[12]-p.target[0],dz=u.matrix[14]-p.target[2];
    return std::isfinite(dx+dz+u.matrix[13]) && dx*dx+dz*dz<=kPlanReach*kPlanReach;
}

bool ApplyCommanderPlan(std::uint64_t tx,const SupportPlan& p,bool remote) noexcept {
    return tx && ValidateCommanderPlan(p) && Apply(tx,p,remote);
}

void DestroyCommanderPlan(std::uint64_t tx) noexcept {
    Deployment* d=Find(tx);
    if(!d)return;
    if(d->soldier.obj)DeleteSupportSoldier(d->soldier);
    Log("COMMANDER %llu rolled back",static_cast<unsigned long long>(tx));
    *d=Deployment{};
}

bool CommanderRequestNotice(std::uint32_t req,support_net::RequestStatus status) noexcept {
    using support_net::RequestStatus;
    if(submitting && status==RequestStatus::accepted && req){requestId=req;return true;}
    if(!req || req!=requestId)return false;
    if(status>=RequestStatus::refused)
        Log("COMMANDER request %u ended without an NPC (status %u): asked again in %llu s",req,static_cast<unsigned>(status),
            static_cast<unsigned long long>(commander::kRetryMs/1000));
    return true;
}

void CommanderFrame(unsigned char* human) noexcept {
    if(!human)return;
    __try {
        Activate();
        OfflineTick();
        PvpStatus pvp{};
        const bool wanted=CommanderModeInEffect() && !(ReadPvpStatus(&pvp) && pvp.enabled);
        if(!wanted) {
            if(on && !released)Release(CommanderModeInEffect() ? "a PvP match" : "the mode is off");
            MapSetCommanderMode(false);
            return;
        }
        if(released || lost){MapSetCommanderMode(false);return;}
        if(!on) {
            on=true;
            if(!Cfg().customNpcAi){Release("CustomNpcAi=0 (the NPCs' map commands need it)");return;}
            Log("COMMANDER mode on: this player commands from the map; its NPC is asked for");
        }
        MapSetCommanderMode(true);
        const ULONGLONG now=GameMs();
        Ask(human,now);
        if(released || now-walkedAt<kWalkMs)return;
        walkedAt=now;
        Walk w;
        if(!WalkTeams(human,&w))return;
        for(unsigned i=0;i<w.playerCount;++i)GuardPlayer(w.players[i]);
        bool ownAlive=false;
        for(const auto& d:deployments)if(d.transaction && d.own && Alive(d.soldier))ownAlive=true;
        if(ownAlive && !request.given){request.given=true;Log("COMMANDER this player's NPC is in the field (%d friendly NPC soldiers)",w.friendly);}
        if(commander::Sample(watch,ownAlive,w.friendly,now)) {
            lost=true;
            Log("COMMANDER every friendly NPC soldier is down: the mission is lost (this player's soldier given the stock death)");
            ReleaseGuards("the mission is lost");
            MapSetCommanderMode(false);
            if(!KillLocalPlayer(human))Log("COMMANDER the stock death could not be given (see above): the mission goes on");
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("COMMANDER frame faulted");}
}

void ResetCommander() noexcept {
    // Mission teardown owns its objects: borrowed references dropped, nothing deleted or written (mission_crew_support.cpp).
    for(auto& d:deployments)d=Deployment{};
    for(auto& g:guards)g=Guard{};
    request={};watch={};on=released=lost=submitting=false;requestId=0;walkedAt=0;offlinePending=false;offlinePlan={};
    MapSetCommanderMode(false);
}

namespace {
// The stock death (static RE 2026-11-21, H unless noted; the disassembly behind it in the feature doc §2.1):
//  - SendMessage 0x1DA480(obj, u32 msg, void* data) calls slots 10, 9, 11; the damage queue sends 0x10000000 with its
//    entry's GameDamageInfo (0x543AB9). GameObjectBase slot 9 0x54A530: `add edx,0xF0000000; cmp edx,0xF` (0x54A555), so
//    0x10000000 is case 0 (0x54A579): ApplyDamage 0x547C30(obj, gdi), then on the +0x2E8 edge PlayerDown and
//    `or byte [gdi+3],1` (the GDI must be writable). The soldiers' slot 9 (AssultSoldier / PaleWing / Engineer 0x598FD0,
//    HeavyArmor 0x567250 -> 0x598FD0 -> HumanBase 0x572810) pass 0x10000000 on to 0x54A530 unchanged.
//  - The GDI ApplyDamage and the soldier's slots read: 0x90 bytes. +0x24 attacker team -1 (no friend test), +0x10/+0x18
//    attacker null (skipped), +0x30 the soldier's centre, +0x50 a finite damage (FLT_MAX could make inf*0 = NaN, which
//    minss / maxss turn into a full HP), +0x60 0x40 forced (past slot 16/17, +0x380 bit 0 and 0x800; never 0x200),
//    +0x78 a valid 0x30-byte hit record (0x54E400 and 0x5987DB read it without its count), +0x88 count 0.
//  - Online ApplyDamage only asks whether a session runs (0x7748F0 reads no object): the HP comes off where it is called,
//    so only this machine's own player (its pad +0x340, +0x128 bit 0 clear). The death travels as the soldier's own
//    network message 0x11 (slot 40 0x59D2B0), the receivers' slot 41 0x577020 (the stock's own way a player dies online).
constexpr unsigned kSendMessage=0x1DA480,kGobMessage=0x54A530,kGobSwitch=0x54A555,kGobDamageCase=0x54A579,kApplyDamage=0x547C30;
constexpr unsigned kSoldierMessage=0x598FD0,kHeavyMessage=0x567250;
constexpr std::uint32_t kDamageMessage=0x10000000u;
constexpr std::size_t kMessageSlot=9,kRemoteFlags=0x128,kHpMaxAt=0x2F4;
const unsigned char kSendMessageCode[]={0x48,0x89,0x5C,0x24,0x18,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0x01,0x48,0x8B,0xF9};
const unsigned char kGobMessageCode[]={0x48,0x89,0x5C,0x24,0x10,0x56,0x57,0x41,0x56,0x48,0x81,0xEC,0x80,0x00,0x00,0x00};
const unsigned char kGobSwitchCode[]={0x81,0xC2,0x00,0x00,0x00,0xF0,0x83,0xFA,0x0F};
const unsigned char kGobDamageCaseCode[]={0x0F,0xB6,0x9F,0xE8,0x02,0x00,0x00,0x48,0x8B,0xD6,0x48,0x8B,0xCF,0xE8,0xA5,0xD6,0xFF,0xFF};
const unsigned char kApplyDamageCode[]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56};
const unsigned char kSoldierMessageCode[]={0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24};
const unsigned char kHeavyMessageCode[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x48,0x8B,0xEC,0x48,0x83};
bool DeathCodeAsRead() noexcept {
    return Matches(kSendMessage,kSendMessageCode,sizeof(kSendMessageCode)) && Matches(kGobMessage,kGobMessageCode,sizeof(kGobMessageCode)) &&
        Matches(kGobSwitch,kGobSwitchCode,sizeof(kGobSwitchCode)) && Matches(kGobDamageCase,kGobDamageCaseCode,sizeof(kGobDamageCaseCode)) &&
        Matches(kApplyDamage,kApplyDamageCode,sizeof(kApplyDamageCode)) && Matches(kSoldierMessage,kSoldierMessageCode,sizeof(kSoldierMessageCode)) &&
        Matches(kHeavyMessage,kHeavyMessageCode,sizeof(kHeavyMessageCode));
}
bool KillLocalPlayer(unsigned char* human) noexcept {
    if(!DeathCodeAsRead()){Log("COMMANDER the stock damage path (EDF+%X / %X / %X) is not as read",kSendMessage,kGobMessage,kApplyDamage);return false;}
    __try {
        if(!human || !Readable(human,kReadable) || human[kDead])return human && human[kDead];
        // This machine's own player only: its pad, not a remote copy.
        if(!At<const void*>(human,kHumanPad) || (At<std::uint32_t>(human,kRemoteFlags)&1u)){Log("COMMANDER not this machine's own soldier");return false;}
        const auto* vtable=At<const unsigned char*>(human,0);
        const auto slot=reinterpret_cast<std::uintptr_t>(At<const void*>(vtable,kMessageSlot*8))-reinterpret_cast<std::uintptr_t>(image);
        if(slot!=kSoldierMessage && slot!=kHeavyMessage){Log("COMMANDER the soldier's message slot is EDF+%llX, not a soldier's",
                                                              static_cast<unsigned long long>(slot));return false;}
        alignas(16) unsigned char gdi[0x90]{};
        alignas(16) float hit[12]{};   // the one hit record: +0 the point, +0x10 (0,1,0,0)
        const float* p=reinterpret_cast<const float*>(human+kPosition);
        const float hpMax=At<float>(human,kHpMaxAt);
        const float damage=(std::isfinite(hpMax) && hpMax>1.0f ? hpMax : 1.0f)*1000.0f;
        hit[0]=p[0];hit[1]=p[1];hit[2]=p[2];hit[3]=1.0f;hit[5]=1.0f;
        Put<std::int32_t>(gdi,0x24,-1);
        Put<float>(gdi,0x30,p[0]);Put<float>(gdi,0x34,p[1]);Put<float>(gdi,0x38,p[2]);Put<float>(gdi,0x3C,1.0f);
        Put<float>(gdi,0x50,damage);
        Put<std::uint16_t>(gdi,0x60,0x40);
        Put<void*>(gdi,0x78,hit);
        reinterpret_cast<bool(*)(void*,std::uint32_t,void*)>(image+kSendMessage)(human,kDamageMessage,gdi);
        const bool dead=human[kDead]!=0;
        Log("COMMANDER the stock death given (damage %.0f): %s",damage,dead ? "down" : "still standing (a script's lowest HP or no damage taken): left as it is");
        return dead;
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("COMMANDER the stock death faulted");return false;}
}
}  // namespace
}  // namespace crew
