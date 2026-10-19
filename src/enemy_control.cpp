// Enemy bodies retain their real native object, skills, physics and network owner.
// AI decisions, input and target selection are leased. The original
// Soldier remains the authenticated source and resumes immediately on release.
#include "crew.h"
#include "enemy_control.h"
#include "enemy_control_native.h"
#include "enemy_control_catalog.h"
#include "enemy_control_net.h"
#include "enemy_control_capacity.h"
#include "enemy_control_adapter.h"
#include "enemy_control_native_net.h"
#include "enemy_control_core_pose.h"
#include "enemy_control_native_pose.h"
#include "enemy_control_deiroi.h"
#include "enemy_control_humanoid.h"
#include "enemy_control_pvp_native.h"
#include "pvp_controlled.h"
#include "pvp.h"
#include "enemy_control_weapon_aim.h"
#include "enemy_control_unit_guide.h"
#include "enemy_control_target.h"
#include "enemy_control_radar.h"
#include "enemy_control_core_action.h"
#include "enemy_control_action_native.h"
#include "enemy_control_walking.h"
#include "enemy_control_walking_guns.h"
#include "enemy_control_timeship.h"
#include "online_authority.h"
#include "support_soldier.h"
#include "edf41classes.h"
#include "memory.h"
#include "map.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <vector>
#include <iterator>
#include <deque>

namespace crew {
namespace {
using namespace enemy_control;
constexpr unsigned kCandidates=32,kVtableSlots=128,kPublished=kCandidates+kMaxControllers;
constexpr ULONGLONG kFreshInput=250,kCandidateLife=3000;
constexpr float kPi=3.14159265358979323846f;
struct Catalogue {
    const enemy_native::NativeProfile* profile=nullptr;
    const EnemyControlAdapter* adapter=nullptr;
    std::vector<enemy_native::NativeAbility> native;
    std::vector<Ability> abilities;
    std::uint64_t hash=0;
};
struct Candidate { ObjRef owner{},target{}; Snapshot snapshot{}; Catalogue catalogue{}; ULONGLONG seen=0; };
struct QueuedInput { Input input{};ULONGLONG at=0; };
struct Session {
    ObjRef owner{},target{};
    Snapshot snapshot{};
    Catalogue catalogue{};
    enemy_native::NativeLease native{};
    enemy_target::NativeLease nativeTarget{};
    enemy_target::Selection focus{};
    controlled_pvp::Lease pvpLease{};
    controlled_pvp::Hooks pvpHooks{};
    char authenticatedPuid[65]{};
    const void* const* original=nullptr;
    void* vtable[kVtableSlots+1]{}; // [-1] is the original complete object locator
    Input input{};
    std::deque<QueuedInput> queued;
    ULONGLONG received=0,frame=~0ULL;
    std::uint64_t executed=0,nextSequence=0,acceptedSequence=0,lastAction=0,lastActionHash=0,pendingAction=0,pendingActionHash=0;
    std::uint32_t wasHeld=0;
    bool local=false,networked=false,uiHeld=false,physicalHeld=false,faulted=false,sourceProtected=false,nativeOwned=false,adapterAttempted=false,closing=false,aborted=false;
    std::uint32_t sourceOwnedFlags=0;
    float yaw=0,pitch=0,aimPoint[3]{};
    std::uint64_t aimSequence=0;
    bool aimPointValid=false;
};
struct Published { Snapshot snapshot{}; std::vector<Ability> abilities; };
Candidate candidates[kCandidates]{};
Session sessions[kMaxControllers]{};
Published published[kPublished]{};
SRWLOCK publishLock=SRWLOCK_INIT;
EnemyControlNetwork network{};
const EnemyControlAdapter* adapters[16]{};
std::uint64_t revision=0,nextLease=0;
bool installed=false,configured=true,deiroiReady=false,humanoidReady=false,walkingReady=false,timeShipReady=false,targetReady=false;

bool On() noexcept { return installed && configured && Cfg().enabled && Cfg().enemyControl; }
bool Same(const ObjRef& a,const ObjRef& b) noexcept { return a.obj==b.obj && a.ctrl==b.ctrl; }
bool CurrentObject(const ObjRef& ref) noexcept {
    if(!ref.obj || !ref.ctrl || !Readable(ref.ctrl,16) || At<LONG>(ref.ctrl,8)<=0)return false;
    return Readable(ref.obj,kSelfCtrl+8) && At<const void*>(ref.obj,kSelfCtrl)==ref.ctrl;
}
bool Live(const ObjRef& ref) noexcept {
    if(!ref.obj || !ref.ctrl || !Readable(ref.ctrl,16) || At<LONG>(ref.ctrl,8)<=0)return false;
    const auto* object=static_cast<const unsigned char*>(ref.obj);
    return Readable(object,kDead+1) && !(object[0x18]&4) && !object[kDead] &&
        At<const void*>(object,kSelfCtrl)==ref.ctrl;
}
bool Keep(const ObjRef& ref) noexcept {
    if(!Live(ref) || !Readable(ref.ctrl,16,true))return false;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(const_cast<unsigned char*>(static_cast<const unsigned char*>(ref.ctrl))+12));
    return true;
}
bool DropReady(const ObjRef& ref) noexcept {
    __try {return !ref.ctrl ? !ref.obj : Readable(ref.ctrl,16,true) && At<LONG>(ref.ctrl,12)>0;}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Drop(ObjRef& ref) noexcept {
    __try {
    if(!DropReady(ref))return false;
    if(ref.ctrl) {
        auto* ctrl=const_cast<unsigned char*>(static_cast<const unsigned char*>(ref.ctrl));
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(ctrl+12))==0)
            reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(ctrl,0)[1])(ctrl);
    }
    ref={};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
void Name(const char* name,wchar_t (&out)[96]) noexcept {
    unsigned i=0;
    if(name)for(;name[i] && i+1<96;++i)out[i]=static_cast<unsigned char>(name[i]);
    out[i]=0;
}
void HashByte(std::uint64_t& hash,unsigned char byte) noexcept { hash=(hash^byte)*1099511628211ull; }
void Hash32(std::uint64_t& hash,std::uint32_t value) noexcept {
    for(unsigned shift=0;shift<32;shift+=8)HashByte(hash,static_cast<unsigned char>(value>>shift));
}
bool ReadCatalogue(const ObjRef& target,Catalogue& result) {
    result={};
    for(const auto* adapter:adapters)if(adapter && adapter->Match(target.obj)) {
        const auto count=adapter->Abilities(target.obj,nullptr,0);
        if(count>4096)return false;
        result.abilities.resize(count);
        if(count && adapter->Abilities(target.obj,result.abilities.data(),count)!=count)return false;
        result.adapter=adapter;result.hash=adapter->ProfileHash(target.obj);
        if(!result.hash)return false;
        for(unsigned i=0;i<count;++i) {
            auto& ability=result.abilities[i];ability.channel=i;
            if(!ability.id || !ability.name[0] || !(ability.flags&(AbilityFlag::held|AbilityFlag::edge)))return false;
            for(unsigned j=0;j<i;++j)if(result.abilities[j].id==ability.id)return false;
        }
        return true;
    }
    const auto* profile=enemy_native::Profile(target.obj);
    if(!profile || profile->slots<15 || profile->slots>kVtableSlots)return false;
    enemy_native::NativeLease lease{};
    if(!enemy_native::Capture(const_cast<void*>(target.obj),&lease))return false;
    const unsigned count=enemy_native::Abilities(lease,nullptr,0);
    if(!count || count>4096)return false;
    result.profile=profile;result.native.resize(count);result.abilities.reserve(count);
    if(count && enemy_native::Abilities(lease,result.native.data(),count)!=count)return false;
    std::uint64_t hash=1469598103934665603ull;Hash32(hash,profile->profileHash);
    for(unsigned i=0;i<count;++i) {
        const auto& native=result.native[i];
        if(!native.id || !*native.id || !native.name || !*native.name)return false;
        Ability ability{};ability.channel=i;
        ability.id=AbilityId(native.id,native.name,static_cast<unsigned>(native.held)|(static_cast<unsigned>(native.pressed)<<16));
        if(!ability.id)return false;
        for(const auto& old:result.abilities)if(old.id==ability.id)return false;
        ability.flags=(native.held ? AbilityFlag::held : 0u)|(native.pressed ? AbilityFlag::edge : 0u);
        if(!native.held && !native.pressed)ability.flags=AbilityFlag::edge;
        wcsncpy_s(ability.name,native.name,_TRUNCATE);
        result.abilities.push_back(ability);
        Hash32(hash,ability.id);Hash32(hash,ability.flags);Hash32(hash,native.held);Hash32(hash,native.pressed);
        Hash32(hash,static_cast<std::uint32_t>(native.kind));Hash32(hash,static_cast<std::uint32_t>(native.argument));
        if(native.action)for(const wchar_t* text=native.action;*text;++text)Hash32(hash,static_cast<std::uint32_t>(*text));
    }
    result.hash=hash ? hash : 1;
    return true;
}
const Ability* FindAbility(const Catalogue& catalogue,std::uint32_t id) noexcept {
    for(const auto& a:catalogue.abilities)if(a.id==id)return &a;
    return nullptr;
}
Session* ByLease(std::uint64_t lease) noexcept {
    if(!lease)return nullptr;
    for(auto& s:sessions)if(s.snapshot.lease==lease && s.snapshot.phase!=Phase::idle)return &s;
    return nullptr;
}
Session* ByObject(const void* object) noexcept {
    for(auto& s:sessions)if(s.target.obj==object && s.snapshot.lease)return &s;
    return nullptr;
}
Session* ByOwner(const ObjRef& owner) noexcept {
    for(auto& s:sessions)if(s.snapshot.lease && Same(s.owner,owner))return &s;
    return nullptr;
}
Candidate* ByCandidate(const Snapshot& snapshot) noexcept {
    for(auto& c:candidates)if(c.snapshot.revision==snapshot.revision && c.snapshot.ownerHumanKey==snapshot.ownerHumanKey &&
        c.snapshot.profileHash==snapshot.profileHash && !std::memcmp(c.snapshot.owner,snapshot.owner,32) &&
        !std::memcmp(c.snapshot.target,snapshot.target,32))return &c;
    return nullptr;
}
bool SessionIdentity(const Session& session,const Snapshot& snapshot) noexcept {
    return session.snapshot.lease==snapshot.lease && session.snapshot.revision==snapshot.revision &&
        session.snapshot.ownerHumanKey==snapshot.ownerHumanKey && session.snapshot.profileHash==snapshot.profileHash &&
        !std::memcmp(session.snapshot.owner,snapshot.owner,32) && !std::memcmp(session.snapshot.target,snapshot.target,32);
}
void Selected(Snapshot& snapshot,const Catalogue& catalogue) noexcept {
    const auto* selected=FindAbility(catalogue,snapshot.selectedAbility);
    if(!selected && !catalogue.abilities.empty()){selected=&catalogue.abilities.front();snapshot.selectedAbility=selected->id;}
    snapshot.selected=selected ? *selected : Ability{};
}
void ReadBodyStatus(Snapshot& snapshot,const ObjRef& target) noexcept {
    snapshot.healthKnown=false;
    if(!CurrentObject(target))return;
    const auto* object=static_cast<const unsigned char*>(target.obj);
    if(Readable(object+kTeam,sizeof(std::int32_t)))snapshot.controlledTeam=At<std::int32_t>(object,kTeam);
    if(!Readable(object+0x2F4,8))return;
    const float maximum=At<float>(object,0x2F4),health=At<float>(object,0x2F8);
    if(!std::isfinite(maximum) || !std::isfinite(health) || maximum<=0 || health<0)return;
    snapshot.health=health;snapshot.maxHealth=maximum;snapshot.healthKnown=true;
}
void MergeNetwork(Snapshot& snapshot) noexcept {
    if(!network.online || !network.online())return;
    EnemyControlNetworkStatus state{};
    if(!ReadEnemyControlNetworkStatus(snapshot.owner,&state) || !state.request ||
        std::memcmp(snapshot.target,state.target,32) || snapshot.profileHash!=state.profileHash)return;
    if(snapshot.request && snapshot.request!=state.request)return;
    if(!snapshot.request && snapshot.phase!=Phase::pending && snapshot.phase!=Phase::restoring &&
        (!snapshot.lease || snapshot.lease!=state.lease))return;
    snapshot.request=state.request;
    using NetPhase=enemy_control_protocol::Phase;
    using NetResult=enemy_control_protocol::Result;
    switch(state.phase) {
        case NetPhase::preparing:case NetPhase::beginning:case NetPhase::activating:snapshot.phase=Phase::pending;break;
        case NetPhase::active:snapshot.phase=Phase::active;break;
        case NetPhase::draining:case NetPhase::restoring:snapshot.phase=Phase::restoring;break;
        default:
            if(state.result==NetResult::Pending)snapshot.phase=Phase::pending;
            else if(state.result==NetResult::Complete)snapshot.phase=Phase::idle;
            else if(state.result!=NetResult::Idle && state.result!=NetResult::Active)snapshot.phase=Phase::failed;
            break;
    }
    if(state.lease)snapshot.lease=state.lease;
    switch(state.result) {
        case NetResult::Unsupported:snapshot.reason=Reason::unsupportedFamily;break;
        case NetResult::Unauthorized:snapshot.reason=Reason::invalidRequester;break;
        case NetResult::Conflict:snapshot.reason=Reason::busy;break;
        case NetResult::InvalidInput:case NetResult::ProtocolError:snapshot.reason=Reason::stale;break;
        case NetResult::PrepareFailed:case NetResult::BeginFailed:case NetResult::ApplyFailed:
        case NetResult::RestoreFailed:snapshot.reason=Reason::nativeFailure;break;
        case NetResult::Unavailable:case NetResult::SendFailed:case NetResult::TimedOut:
        case NetResult::SessionLost:snapshot.reason=Reason::networkUnavailable;break;
        default:snapshot.reason=Reason::none;break;
    }
}
void Publish() {
    enemy_radar::View radar[kMaxControllers]{};unsigned radarCount=0;
    AcquireSRWLockExclusive(&publishLock);
    unsigned i=0;
    for(auto& c:candidates) {
        if(c.snapshot.revision)MergeNetwork(c.snapshot);
        published[i].snapshot=c.snapshot;published[i].abilities=c.catalogue.abilities;++i;
    }
    for(auto& s:sessions) {
        if(On() && s.local && !s.closing && s.snapshot.phase==Phase::active && Live(s.owner) && Live(s.target) && Readable(s.target.obj,0x31C)) {
            auto& r=radar[radarCount];r.ownerObject=reinterpret_cast<std::uintptr_t>(s.owner.obj);r.ownerControl=reinterpret_cast<std::uintptr_t>(s.owner.ctrl);
            const auto* actor=static_cast<const unsigned char*>(s.target.obj);
            std::memcpy(r.center,actor+0x90,12);std::memcpy(r.side,actor+0x70,12);std::memcpy(r.forward,actor+0x80,12);
            r.team=At<int>(actor,0x314);if(r.team==-1)r.team=At<int>(actor,0x318);
            if(r.team>=0 && r.team<=6)++radarCount;
        }
        Selected(s.snapshot,s.catalogue);
        Snapshot view=s.snapshot;view.sourceProtected=s.sourceProtected;ReadBodyStatus(view,s.target);
        controlled_pvp::Status teamStatus{};
        if(s.pvpLease.id && controlled_pvp::ReadStatus(s.pvpLease,&teamStatus)) {
            view.controlledTeam=teamStatus.controlledTeam;view.followsOwnerTeam=teamStatus.followsOwnerTeam;
        }
        if(view.lease)MergeNetwork(view);
        published[i].snapshot=view;published[i].abilities=s.catalogue.abilities;
        for(auto& a:published[i].abilities)if(a.id==s.snapshot.selectedAbility)a.state=view.selected.state;
        ++i;
    }
    ReleaseSRWLockExclusive(&publishLock);
    EnemyControlRadarPublish(radar,radarCount);
}
bool CandidateClear(Candidate& candidate) noexcept {
    if(!DropReady(candidate.owner) || !DropReady(candidate.target))return false;
    if(!Drop(candidate.owner) || !Drop(candidate.target))return false;
    candidate=Candidate{};return true;
}
void MakeId(const ObjRef& object,unsigned char (&id)[32],std::uint64_t serial,unsigned kind) noexcept {
    if(ReadNativeObjectId(static_cast<const unsigned char*>(object.obj),id))return;
    // Offline identities stay inside this local candidate table. Network entry
    // points resolve real NativeNetId32 and never accept these local tokens.
    std::memset(id,0,32);std::memcpy(id,"ECLOCAL",7);
    std::memcpy(id+8,&serial,8);std::memcpy(id+16,&kind,4);
}
void FillSnapshot(Snapshot& snapshot,const ObjRef& owner,const ObjRef& target,const Catalogue& catalogue) noexcept {
    snapshot={};snapshot.revision=++revision;snapshot.ownerHumanKey=reinterpret_cast<std::uintptr_t>(owner.obj);
    snapshot.profileHash=catalogue.hash;snapshot.abilityCount=static_cast<std::uint32_t>(catalogue.abilities.size());
    Name(catalogue.adapter ? catalogue.adapter->name : catalogue.profile ? catalogue.profile->name : "Enemy",snapshot.name);
    MakeId(owner,snapshot.owner,snapshot.revision,1);MakeId(target,snapshot.target,snapshot.revision,2);
    ReadBodyStatus(snapshot,target);
    Selected(snapshot,catalogue);
}
Reason CanPrepare(const ObjRef& requester,const ObjRef& target,Catalogue* catalogue) {
    if(!On())return Reason::disabled;
    if(!targetReady)return Reason::nativeMismatch;
    if(!Live(requester) || !IsAnyPlayer(static_cast<const unsigned char*>(requester.obj)) || !IsSoldierClass(requester.obj) ||
        !HumanOnFoot(static_cast<const unsigned char*>(requester.obj)))return Reason::invalidRequester;
    if(!Live(target))return Reason::invalidTarget;
    if(Same(requester,target))return Reason::sameObject;
    if(ByOwner(requester) || ByObject(target.obj))return Reason::busy;
    if(!ReadCatalogue(target,*catalogue))return enemy_native::Profile(target.obj) ? Reason::incompleteSkills : Reason::unsupportedFamily;
    if(!catalogue->adapter && ((catalogue->profile->kind==enemy_native::Kind::deiroi && !deiroiReady) ||
        (catalogue->profile->kind==enemy_native::Kind::humanoid && !humanoidReady) ||
        (catalogue->profile->kind==enemy_native::Kind::walking && !walkingReady) ||
        (!std::strcmp(catalogue->profile->name,"EDF6_TimeShip") && !timeShipReady)))return Reason::nativeMismatch;
    if(!catalogue->adapter && (!EnemyControlWeaponAimReady(catalogue->profile) ||
        !EnemyControlUnitGuideReady(catalogue->profile)))return Reason::nativeMismatch;
    if(network.online && network.online()) {
        if(!EnemyControlLocalCapacityReady())return Reason::networkCapacity;
        PvpStatus pvp{};
        if(ReadPvpStatus(&pvp) && (pvp.enabled || pvp.pending)) {
            controlled_pvp::Hooks hooks{};bool ready=false;
            if(catalogue->adapter)ready=catalogue->adapter->PvpHooks && catalogue->adapter->PvpHooks(target.obj,&hooks);
            else {
                enemy_native::NativeLease native{};
                ready=enemy_native::Capture(const_cast<void*>(target.obj),&native) && EnemyControlNativePvpHooks(native,&hooks);
            }
            if(!ready || !controlled_pvp::Probe(target,hooks))return Reason::nativeMismatch;
        }
        unsigned char sourceId[32]{},targetId[32]{};
        if(!ReadNativeObjectId(static_cast<const unsigned char*>(requester.obj),sourceId) ||
            !ReadNativeObjectId(static_cast<const unsigned char*>(target.obj),targetId))return Reason::noIdentity;
        if(catalogue->adapter) {
            if(!catalogue->adapter->NetworkPoseReady || !catalogue->adapter->NetworkPoseReady(target.obj))return Reason::networkUnavailable;
        } else {
            enemy_native::NativeLease pose{};
            if(!enemy_native::Capture(const_cast<void*>(target.obj),&pose) ||
                (!EnemyControlNativeNetworkPoseReady(pose) && !enemy_native::PoseReady(pose)))return Reason::networkUnavailable;
        }
    }
    return Reason::none;
}
std::uint64_t __fastcall Input4(void* object,void* dt);
std::uint64_t __fastcall Input5(void* object,void* dt);
std::uint64_t __fastcall Think(void* object,void* dt);
void* __fastcall Target14(void* object);

bool Patch(Session& session) noexcept {
    const auto slots=session.catalogue.profile->slots;
    auto* object=const_cast<void*>(session.target.obj);
    if(!Readable(object,8,true))return false;
    const auto table=At<const void* const*>(object,0);
    if(!Readable(table-1,(static_cast<std::size_t>(slots)+1)*8))return false;
    session.original=table;
    std::memcpy(session.vtable,table-1,(static_cast<std::size_t>(slots)+1)*8);
    session.vtable[1+4]=reinterpret_cast<void*>(&Input4);session.vtable[1+5]=reinterpret_cast<void*>(&Input5);
    session.vtable[1+7]=reinterpret_cast<void*>(&Think);
    session.vtable[1+14]=reinterpret_cast<void*>(&Target14);
    Put<void*>(object,0,session.vtable+1);
    session.nativeOwned=true;return true;
}
bool AimRay(const Session& session,float yaw,float pitch,enemy_target::Ray* ray) noexcept {
    if(!ray || !Live(session.target) || !Readable(session.target.obj,0x9C) || !std::isfinite(yaw) || !std::isfinite(pitch))return false;
    const auto* object=static_cast<const unsigned char*>(session.target.obj);
    const auto* position=reinterpret_cast<const float*>(object+0x90);
    float pivot[3]={position[0],position[1]+2.0f,position[2]};edf41c::AimPoint(object,pivot);
    for(unsigned i=0;i<3;++i)if(!std::isfinite(pivot[i]))return false;
    std::memcpy(ray->origin,pivot,12);ray->range=2000.0f;
    ray->direction[0]=std::sin(yaw)*std::cos(pitch);ray->direction[1]=std::sin(pitch);ray->direction[2]=std::cos(yaw)*std::cos(pitch);
    return true;
}
bool BuildWeaponAim(Session& session) noexcept {
    if(!session.input.absoluteAim)return true;
    enemy_target::Ray ray{};
    if(!AimRay(session,session.input.aimYaw,session.input.aimPitch,&ray))return false;
    float from[3],point[3];
    for(unsigned i=0;i<3;++i){from[i]=ray.origin[i]+ray.direction[i]*0.1f;point[i]=ray.origin[i]+ray.direction[i]*ray.range;}
    float hit[3];const float distance=MapRay(from,point,hit);
    if(distance>0.0001f && std::isfinite(distance)) {
        bool finite=true;float forward=0;
        for(unsigned i=0;i<3;++i){finite=finite && std::isfinite(hit[i]);forward+=(hit[i]-ray.origin[i])*ray.direction[i];}
        if(finite && forward>0 && forward<ray.range)std::memcpy(point,hit,12);
    }
    enemy_target::Selection selected{};
    if(session.networked) {
        if(!enemy_target::Resolve(session.owner,session.target,session.input.fullFocusId,ray,&selected))selected={};
    } else if(!enemy_target::Pick(session.owner,session.target,ray,&selected))selected={};
    if(!enemy_target::Apply(session.nativeTarget,selected.actor ? &selected : nullptr) ||
        !enemy_target::TryAssign(session.focus,selected))return false;
    std::memcpy(session.aimPoint,point,12);session.aimSequence=session.input.sequence;session.aimPointValid=true;return true;
}
std::uint64_t Run(Session& session,unsigned slot,void* dt) noexcept {
    const auto original=reinterpret_cast<enemy_native::InputFn>(session.original[slot]);
    if(slot!=session.catalogue.profile->inputSlot)return original(const_cast<void*>(session.target.obj),dt);
    enemy_native::Input native{};
    std::uint64_t weaponMask=0;
    if(!session.queued.empty()) {
        const auto next=session.queued.front();session.queued.pop_front();
        if(GameMs()<next.at || GameMs()-next.at>kFreshInput)session.faulted=true;
        else session.input=next.input;
    }
    const bool fresh=!session.faulted && !session.closing && On() && session.snapshot.phase==Phase::active && session.received && GameMs()>=session.received &&
        GameMs()-session.received<=kFreshInput;
    const bool first=fresh && session.executed!=session.input.sequence;
    if(fresh) {
        native.right=session.input.move[0];native.forward=session.input.move[2];native.vertical=session.input.move[1];native.seed=session.input.seed;
        native.yaw=session.input.look[0];native.pitch=session.input.look[1];
        if(session.input.absoluteAim &&
            (!enemy_native::WorldMoveToPad(session.native,session.input.move,session.input.aimYaw,&native) ||
             !enemy_native::AimToPad(session.native,session.input.aimYaw,session.input.aimPitch,&native)))session.faulted=true;
        if(session.input.absoluteAim && session.catalogue.profile->kind==enemy_native::Kind::deiroi &&
            !enemy_native::UpdateDeiroiAim(session.native,session.input.aimYaw,session.input.aimPitch))session.faulted=true;
        for(const auto& active:session.input.active)if(active.id) {
            const auto* ability=FindAbility(session.catalogue,active.id);
            if(!ability)continue;
            const auto& a=session.catalogue.native[ability->channel];
            if(a.kind==enemy_native::AbilityKind::humanoidWeapon) {
                if(a.argument<0 || a.argument>=64)session.faulted=true;
                else weaponMask|=1ull<<static_cast<unsigned>(a.argument);
            } else if(a.kind==enemy_native::AbilityKind::pad && (a.held&(1u<<5)))weaponMask=~0ull;
            if(active.phase==1)native.held|=a.held;
            if(first) {
                if(active.phase==2){native.held|=a.held;native.pressed|=a.pressed;}
                else native.pressed|=a.pressed&~session.wasHeld;
            }
        }
    }
    if(session.faulted)native={};
    if(first && !session.faulted) {
        // Synthetic pad records have independent held/pressed fields. One edge
        // input sequence is consumed once even when several native updates run.
        native.pressed|=native.held&~session.wasHeld;
        session.executed=session.input.sequence;
    }
    session.wasHeld=native.held;
    if(fresh && !session.faulted && !BuildWeaponAim(session)){session.faulted=true;native={};}
    const bool actionScope=session.networked && enemy_native::NeedsNativeActions(session.native);
    if(actionScope && !enemy_native::BeforeNativeActionInput(session.native,IsOnlineAuthority(session.target.obj),&native)) {
        enemy_native::AfterNativeActionInput(session.native);session.faulted=true;return 0;
    }
    std::uint64_t result=0;
    const bool ran=session.catalogue.profile->kind==enemy_native::Kind::humanoid ?
        enemy_native::RunHumanoidInput(session.native,native,original,dt,weaponMask,&result) :
        enemy_native::RunInput(session.native,native,original,dt,&result);
    if(!ran)session.faulted=true;
    if(first && !session.faulted)for(const auto& active:session.input.active)if(active.id && active.phase==2) {
        const auto* ability=FindAbility(session.catalogue,active.id);
        if(!ability)continue;
        const auto& a=session.catalogue.native[ability->channel];
        if(!a.held && !a.pressed)
            enemy_native::ExecuteAbility(session.native,a,session.input.sequence,session.input.seed); // native cooldown/shape gates may refuse this press
    }
    if(actionScope && !enemy_native::AfterNativeActionInput(session.native))session.faulted=true;
    return result;
}
void* __fastcall Target14(void* object) {
    const auto* s=ByObject(object);
    // The original GOB selector returns a raw real actor, including nullptr.
    // A null controlled focus must never fall through to autonomous retargeting.
    if(!s || !EnemyControlTargetActive(object) || !CurrentObject(s->owner))return nullptr;
    return Live(s->focus.actor) ? const_cast<void*>(s->focus.actor.obj) : nullptr;
}
std::uint64_t __fastcall Input4(void* object,void* dt) {
    auto* s=ByObject(object);return s && s->original ? Run(*s,4,dt) : 0;
}
std::uint64_t __fastcall Input5(void* object,void* dt) {
    auto* s=ByObject(object);return s && s->original ? Run(*s,5,dt) : 0;
}
std::uint64_t __fastcall Think(void* object,void* dt) {
    auto* s=ByObject(object);
    if(s && s->snapshot.lease) {
        std::uint64_t result=0;
        // An explicitly registered plugin wrapper can own non-AI work here
        // (4.1 dragon flames). Only its decisions are paused, never that work.
        enemy_native::RunControlledAiExtension(s->native,dt,&result);return result;
    }
    return s && s->original ? reinterpret_cast<enemy_native::InputFn>(s->original[7])(object,dt) : 0;
}
bool RefreshPvp(Session& session) noexcept {
    if(!session.networked)return true;
    if(session.pvpLease.id)return controlled_pvp::Frame(session.pvpLease);
    const int team=PvpSupportTeam(session.authenticatedPuid);
    if(team==0)return true;
    if(team<0)return false;
    const bool hooks=session.catalogue.adapter ? session.catalogue.adapter->PvpHooks &&
        session.catalogue.adapter->PvpHooks(session.target.obj,&session.pvpHooks) :
        EnemyControlNativePvpHooks(session.native,&session.pvpHooks);
    return hooks && controlled_pvp::Acquire(session.target,session.authenticatedPuid,session.pvpHooks,&session.pvpLease);
}
bool RefreshAdapterCatalogue(Session& session) {
    if(!session.catalogue.adapter) {
        if(!session.nativeOwned)return true;
        for(unsigned i=0;i<session.catalogue.abilities.size();++i)
            if(!enemy_native::ReadAbilityState(session.native,session.catalogue.native[i],&session.catalogue.abilities[i]))return false;
        return true;
    }
    const auto* adapter=session.catalogue.adapter;
    if(!adapter->Match(session.target.obj) || adapter->ProfileHash(session.target.obj)!=session.catalogue.hash ||
        adapter->Abilities(session.target.obj,nullptr,0)!=session.catalogue.abilities.size())return false;
    std::vector<Ability> current(session.catalogue.abilities.size());
    if(!current.empty() && adapter->Abilities(session.target.obj,current.data(),static_cast<unsigned>(current.size()))!=current.size())return false;
    for(unsigned i=0;i<current.size();++i) {
        const auto& before=session.catalogue.abilities[i];const auto& after=current[i];
        if(before.id!=after.id || before.flags!=after.flags || std::wcscmp(before.name,after.name))return false;
        current[i].channel=i;
    }
    session.catalogue.abilities=std::move(current);return true;
}
bool InputAllowed() noexcept {
    if(GamePaused())return false;
    DWORD pid=0;GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
float Axis(float value) noexcept { return !std::isfinite(value) ? 0.0f : (std::max)(-1.0f,(std::min)(1.0f,value)); }
bool Held(int key) noexcept { return (edf::AsyncKey(key)&0x8000)!=0; }
void Sample(Session& session,Input& sample) noexcept {
    const auto* human=static_cast<const unsigned char*>(session.owner.obj);
    const auto* pad=At<const unsigned char*>(human,kHumanPad);
    const int index=At<int>(human,0xD40);
    const auto* record=pad && index>=0 && index<4 ? pad+static_cast<std::size_t>(index)*0xA80 : nullptr;
    bool physical=false;
    if(InputAllowed() && !MapHoldsHumanKeys(human) && Readable(record,0x688)) {
        sample.move[0]=Axis(At<float>(record,0x0C));sample.move[2]=Axis(At<float>(record,0x24));
        sample.look[0]=Axis(At<float>(record,0x3C));sample.look[1]=Axis(At<float>(record,0x54));
        physical=record[0xD0]!=0;
        // Keyboard/mouse are the first local input record only. A second local
        // player consumes its own native record and never inherits P1's keys.
        if(index==0) {
            const float right=(Held('D') ? 1.0f : 0.0f)-(Held('A') ? 1.0f : 0.0f);
            const float forward=(Held('W') ? 1.0f : 0.0f)-(Held('S') ? 1.0f : 0.0f);
            if(right || forward){sample.move[0]=right;sample.move[2]=forward;}
            if(session.catalogue.adapter || (session.catalogue.profile->axes&8))
                sample.move[1]=(Held(VK_SPACE) ? 1.0f : 0.0f)-(Held(VK_CONTROL) ? 1.0f : 0.0f);
            const float dx=At<float>(record,0x66C),dy=At<float>(record,0x684);
            if(std::isfinite(dx) && std::isfinite(dy) && std::fabs(dx)<4000 && std::fabs(dy)<4000) {
                sample.look[0]=Axis(sample.look[0]+dx/24.0f);sample.look[1]=Axis(sample.look[1]+dy/24.0f);
            }
            physical=physical || Held(VK_LBUTTON);
        }
    }
    const bool fire=physical || (InputAllowed() && session.uiHeld);
    const auto* selected=FindAbility(session.catalogue,session.snapshot.selectedAbility);
    if(fire && selected) {
        if(selected->flags&AbilityFlag::held)sample.active[0]={selected->id,1};
        else if(!session.physicalHeld)sample.active[0]={selected->id,2};
    }
    session.physicalHeld=fire;
    session.yaw=std::remainder(session.yaw+sample.look[0]*0.04f,2.0f*kPi);session.pitch+=sample.look[1]*0.025f;
    session.pitch=(std::max)(-1.2f,(std::min)(1.2f,session.pitch));
    sample.absoluteAim=true;sample.aimYaw=session.yaw;sample.aimPitch=session.pitch;
    enemy_target::Ray ray{};enemy_target::Selection target{};
    if(AimRay(session,sample.aimYaw,sample.aimPitch,&ray) && enemy_target::Pick(session.owner,session.target,ray,&target))
        std::memcpy(sample.fullFocusId,target.nativeId,32);
}
bool ProtectSource(Session& session) noexcept {
    __try {
        if(!CurrentObject(session.owner) || !Readable(static_cast<const unsigned char*>(session.owner.obj)+0x380,4,true))return false;
        const auto flags=At<std::uint32_t>(session.owner.obj,0x380);
        session.sourceOwnedFlags=0x81u&~flags;
        Put<std::uint32_t>(const_cast<void*>(session.owner.obj),0x380,flags|0x81u);
        session.sourceProtected=true;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool RestoreSource(Session& session) noexcept {
    if(!session.sourceProtected)return true;
    __try {
        if(!CurrentObject(session.owner)){session.sourceProtected=false;return true;}
        auto* owner=const_cast<void*>(session.owner.obj);
        if(!Readable(static_cast<unsigned char*>(owner)+0x380,4,true))return false;
        // Only bits this lease changed from zero to one are ours. PVP registry,
        // mission invulnerability and every unrelated flag remain as now owned.
        const auto flags=At<std::uint32_t>(owner,0x380);
        Put<std::uint32_t>(owner,0x380,flags&~session.sourceOwnedFlags);
        session.sourceProtected=false;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool RestoreTable(Session* s) noexcept {
    bool returned=false;
    __try {
        if(s->catalogue.adapter)returned=!s->adapterAttempted || !CurrentObject(s->target) || s->catalogue.adapter->Restore(s->target.obj);
        else if(!CurrentObject(s->target))returned=true;
        else if(At<const void*>(s->target.obj,0)!=s->vtable+1)returned=true;
        else if(Readable(s->target.obj,8,true)) {
            Put<const void*>(const_cast<void*>(s->target.obj),0,s->original);
            returned=At<const void*>(s->target.obj,0)==s->original;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {returned=false;}
    return returned;
}

}

bool RegisterEnemyControlAdapter(const EnemyControlAdapter* adapter) noexcept {
    if(!adapter || !adapter->name || !adapter->Match || !adapter->Abilities || !adapter->ProfileHash ||
        !adapter->Begin || !adapter->Apply || !adapter->Restore)return false;
    for(const auto* old:adapters)if(old==adapter)return true;
    for(auto& slot:adapters)if(!slot){slot=adapter;return true;}
    return false;
}
bool InstallEnemyControl() noexcept {
    installed=true;InstallEnemyControlUnitGuide();targetReady=enemy_target::Install();deiroiReady=enemy_native::InstallDeiroiControl();
    humanoidReady=enemy_native::InstallHumanoidControl();walkingReady=enemy_native::InstallWalkingControl() && enemy_native::InstallWalkingGunControl();
    timeShipReady=enemy_native::InstallTimeShipControl();return true;
}
void ConfigureEnemyControl(bool enabled) noexcept { configured=enabled;if(!enabled)ResetEnemyControl(); }
void ConfigureEnemyControlNetwork(const EnemyControlNetwork& bridge) noexcept { network=bridge; }
void ResetEnemyControl() noexcept {
    for(auto& session:sessions)if(session.snapshot.lease) {
        if(!EnemyControlAbortNetworkActions(session.snapshot.lease))continue;
        EnemyControlRestore(session.snapshot.lease);
    }
    for(auto& candidate:candidates)CandidateClear(candidate);
    ++revision;Publish();
}
bool EnemyControlPrepare(const ObjRef& requester,const ObjRef& target,std::uint64_t hash,Reason* reason) noexcept {
    Reason result=Reason::nativeFailure;
    Catalogue catalogue;
    result=CanPrepare(requester,target,&catalogue);
    if(result==Reason::none && catalogue.hash!=hash)result=Reason::stale;
    if(reason)*reason=result;
    return result==Reason::none;
}
bool EnemyControlBegin(const ObjRef& requester,const ObjRef& target,std::uint64_t hash,std::uint64_t lease,bool local,const char* authenticatedPuid) noexcept {
    const bool online=network.online && network.online();
    if(online && (!authenticatedPuid || !authenticatedPuid[0] || !std::memchr(authenticatedPuid,0,65)))return false;
    if(!lease || ByLease(lease))return false;
    Catalogue catalogue;
    if(CanPrepare(requester,target,&catalogue)!=Reason::none || catalogue.hash!=hash)return false;
    Session* free=nullptr;for(auto& s:sessions)if(!s.snapshot.lease){free=&s;break;}
    if(!free || !Keep(requester))return false;
    if(!Keep(target)){ObjRef undo=requester;Drop(undo);return false;}
    auto& s=*free;s=Session{};s.owner=requester;s.target=target;s.catalogue=std::move(catalogue);s.local=local;s.networked=online;
    FillSnapshot(s.snapshot,requester,target,s.catalogue);s.snapshot.lease=lease;s.snapshot.phase=Phase::pending;
    const auto* matrix=reinterpret_cast<const float*>(static_cast<const unsigned char*>(target.obj)+0x60);
    s.yaw=std::atan2(matrix[8],matrix[10]);
    if(!s.catalogue.adapter && !enemy_native::Capture(const_cast<void*>(target.obj),&s.native)) {
        EnemyControlRestore(lease);return false;
    }
    if(online) {
        std::memcpy(s.authenticatedPuid,authenticatedPuid,std::strlen(authenticatedPuid)+1);
        const int team=PvpSupportTeam(s.authenticatedPuid);
        const bool hooks=s.catalogue.adapter ? s.catalogue.adapter->PvpHooks && s.catalogue.adapter->PvpHooks(target.obj,&s.pvpHooks) :
            EnemyControlNativePvpHooks(s.native,&s.pvpHooks);
        if(team<0 || (!hooks && team!=0) || (hooks && !controlled_pvp::Acquire(target,s.authenticatedPuid,s.pvpHooks,&s.pvpLease))) {
            EnemyControlRestore(lease);return false;
        }
    }
    if(!enemy_target::Begin(target,&s.nativeTarget)) {EnemyControlRestore(lease);return false;}
    s.adapterAttempted=s.catalogue.adapter!=nullptr;
    const bool captured=s.catalogue.adapter ? s.catalogue.adapter->Begin(target.obj,lease) : Patch(s);
    if(!captured || !ProtectSource(s)) {
        EnemyControlRestore(lease);return false;
    }
    if(s.networked && !s.catalogue.adapter && enemy_native::NeedsNativeActions(s.native)) {
        enemy_native::Input neutral{};
        const bool before=enemy_native::BeforeNativeActionInput(s.native,IsOnlineAuthority(target.obj),&neutral);
        const bool after=enemy_native::AfterNativeActionInput(s.native);
        if(!before || !after){EnemyControlAbortNetworkActions(lease);EnemyControlRestore(lease);return false;}
    }
    Log("ENEMYCONTROL begin owner=%p target=%p profile=%s lease=%llu native slots=%u",requester.obj,target.obj,
        s.catalogue.adapter ? s.catalogue.adapter->name : s.catalogue.profile->name,static_cast<unsigned long long>(lease),
        s.catalogue.profile ? s.catalogue.profile->slots : 0u);
    Publish();return true;
}
bool EnemyControlActivate(std::uint64_t lease) noexcept {
    auto* s=ByLease(lease);
    if(!s || !On() || !Live(s->owner) || !Live(s->target) || s->snapshot.phase!=Phase::pending)return false;
    s->snapshot.phase=Phase::active;s->received=GameMs();Publish();return true;
}
bool EnemyControlValidateInput(std::uint64_t lease,const Input& input) noexcept {
    const auto* s=ByLease(lease);
    if(!s || s->closing || s->snapshot.phase!=Phase::active || !On() || !Live(s->owner) || !Live(s->target) || !ValidInput(input))return false;
    for(const auto& active:input.active)if(active.id) {
        const auto* ability=FindAbility(s->catalogue,active.id);
        if(!ability || (active.phase==1 && !(ability->flags&AbilityFlag::held)) ||
            (active.phase==2 && !(ability->flags&(AbilityFlag::edge|AbilityFlag::held))))return false;
    }
    return true;
}
bool EnemyControlApply(std::uint64_t lease,const Input& input) noexcept {
    if(!EnemyControlValidateInput(lease,input) || !input.sequence)return false;
    auto* s=ByLease(lease);
    if(input.sequence<=s->acceptedSequence || s->queued.size()>=64)return false;
    bool event=false;for(const auto& a:input.active)event=event || a.phase==2;
    bool merged=false;
    if(!event && !s->queued.empty()) {
        auto& last=s->queued.back();bool same=true;
        for(unsigned i=0;i<kConcurrentAbilities;++i)same=same && last.input.active[i].id==input.active[i].id &&
            last.input.active[i].phase==input.active[i].phase;
        if(same){last={input,GameMs()};merged=true;}
    }
    if(!merged)s->queued.push_back({input,GameMs()});
    s->acceptedSequence=input.sequence;s->received=GameMs();return true;
}
bool EnemyControlRestore(std::uint64_t lease) noexcept {
    auto* s=ByLease(lease);if(!s)return true;
    s->snapshot.phase=Phase::restoring;s->queued.clear();s->uiHeld=s->physicalHeld=false;
    if(!DropReady(s->owner) || !DropReady(s->target)){Publish();return false;}
    if(!s->catalogue.adapter && s->nativeOwned && !enemy_native::RestoreNativeActions(s->native)){Publish();return false;}
    if(!s->catalogue.adapter && !RestoreEnemyControlUnitGuide(s->target.obj,s->target.ctrl)){Publish();return false;}
    if(s->pvpLease.id && !controlled_pvp::Restore(&s->pvpLease)){Publish();return false;}
    if(!enemy_target::Restore(s->nativeTarget)){Publish();return false;}
    // Release any scoped native pad and held commands while the guarded input
    // table still owns the body. A failed release must keep the AI paused.
    if(!s->catalogue.adapter && s->nativeOwned && !enemy_native::RestorePose(s->native)){Publish();return false;}
    if(!s->catalogue.adapter && s->nativeOwned && s->catalogue.profile->kind==enemy_native::Kind::deiroi &&
        !enemy_native::RestoreDeiroiControl(s->native)){Publish();return false;}
    if(!s->catalogue.adapter && s->nativeOwned && s->catalogue.profile->kind==enemy_native::Kind::walking &&
        (!enemy_native::RestoreWalkingControl(s->native) || !enemy_native::RestoreWalkingGunControl(s->native))){Publish();return false;}
    if(!s->catalogue.adapter && s->nativeOwned && !std::strcmp(s->catalogue.profile->name,"EDF6_TimeShip") &&
        !enemy_native::RestoreTimeShipControl(s->native)){Publish();return false;}
    if(!s->catalogue.adapter && s->nativeOwned && !enemy_native::Restore(&s->native)){Publish();return false;}
    if(!enemy_target::TryClear(s->focus)){Publish();return false;}
    const bool returned=RestoreTable(s);
    // A live object still pointing at this session's cloned table owns the
    // session. Keep the baseline and weak references until restoration succeeds.
    if(!returned || !RestoreSource(*s)){Publish();return false;}
    if(!Drop(s->owner) || !Drop(s->target)){Publish();return false;}
    *s=Session{};Publish();return true;
}

bool EnemyControlLeaseLive(std::uint64_t lease) noexcept {
    const auto* s=ByLease(lease);return s && On() && !s->faulted && !s->closing && Live(s->owner) && Live(s->target);
}
void EnemyControlFrame() noexcept {
    if(!On()){ResetEnemyControl();return;}
    const auto now=GameMs();
    for(auto& c:candidates)if(c.seen) {
        MergeNetwork(c.snapshot);
        if(c.snapshot.phase!=Phase::pending && c.snapshot.phase!=Phase::restoring && c.snapshot.phase!=Phase::active &&
            now>=c.seen && now-c.seen>kCandidateLife)CandidateClear(c);
    }
    for(auto& s:sessions)if(s.snapshot.lease) {
        if(s.networked!=(network.online && network.online())){
            if(EnemyControlAbortNetworkActions(s.snapshot.lease))EnemyControlRestore(s.snapshot.lease);continue;
        }
        if(s.closing){if(s.aborted)EnemyControlRestore(s.snapshot.lease);continue;} // Normal close is drained by the authenticated protocol.
        if(s.snapshot.phase==Phase::restoring || !Live(s.owner) || !Live(s.target) || s.faulted) {
            if(s.networked) {EnemyControlQuiesceNetworkActions(s.snapshot.lease);continue;}
            EnemyControlRestore(s.snapshot.lease);continue;
        }
        if((!s.catalogue.adapter && !EnemyControlUnitGuideReady(s.catalogue.profile)) || !RefreshPvp(s) || !RefreshAdapterCatalogue(s)){
            s.faulted=true;if(s.networked)EnemyControlQuiesceNetworkActions(s.snapshot.lease);else EnemyControlRestore(s.snapshot.lease);continue;
        }
        if(s.frame==GameFrame())continue;
        s.frame=GameFrame();
        if(s.local && s.snapshot.phase==Phase::active) {
            Input input{};Sample(s,input);
            if(network.online && network.online()) {if(network.input)network.input(s.snapshot,input);}
            else {input.sequence=++s.nextSequence;input.seed=s.snapshot.lease^input.sequence;EnemyControlApply(s.snapshot.lease,input);}
        }
        if(s.catalogue.adapter) {
            if(!s.queued.empty()) {
                const auto next=s.queued.front();s.queued.pop_front();
                if(now<next.at || now-next.at>kFreshInput)s.faulted=true;else s.input=next.input;
            }
            const bool fresh=s.snapshot.phase==Phase::active && s.received && now>=s.received && now-s.received<=kFreshInput;
            Input current=fresh ? s.input : Input{};
            const bool first=fresh && s.executed!=current.sequence;
            if(!first)for(auto& active:current.active)if(active.phase==2)active={};
            if(fresh && !BuildWeaponAim(s))s.faulted=true;
            if(!s.catalogue.adapter->Apply(s.target.obj,current,first))s.faulted=true;
            if(first)s.executed=current.sequence;
        }
    }
    Publish();
}
bool EnemyControlDescribe(const ObjRef& requester,const ObjRef& target,Snapshot* out) noexcept {
    if(!out)return false;*out={};
    if(!Live(requester) || !IsPlayer(static_cast<const unsigned char*>(requester.obj))) {out->reason=Reason::invalidRequester;return false;}
    for(auto& c:candidates)if(Same(c.owner,requester) && Same(c.target,target) && c.seen) {
        c.seen=GameMs();MergeNetwork(c.snapshot);
        if(c.snapshot.phase!=Phase::pending && c.snapshot.phase!=Phase::restoring && c.snapshot.phase!=Phase::active) {
            Catalogue fresh;const auto reason=CanPrepare(requester,target,&fresh);
            unsigned char ownerId[32]{},targetId[32]{};
            MakeId(requester,ownerId,c.snapshot.revision,1);MakeId(target,targetId,c.snapshot.revision,2);
            if(reason!=c.snapshot.reason || fresh.hash!=c.snapshot.profileHash ||
                std::memcmp(ownerId,c.snapshot.owner,32) || std::memcmp(targetId,c.snapshot.target,32)) {
                c.catalogue=std::move(fresh);FillSnapshot(c.snapshot,requester,target,c.catalogue);c.snapshot.reason=reason;
            } else {c.catalogue=std::move(fresh);Selected(c.snapshot,c.catalogue);}
        }
        *out=c.snapshot;Publish();return true;
    }
    Candidate* c=nullptr;
    for(auto& candidate:candidates)if(!candidate.seen){c=&candidate;break;}
    if(!c)for(auto& candidate:candidates)if(candidate.snapshot.phase!=Phase::pending && candidate.snapshot.phase!=Phase::restoring &&
        candidate.snapshot.phase!=Phase::active && (!c || candidate.seen<c->seen))c=&candidate;
    if(!c){out->reason=Reason::busy;return false;}
    if(!CandidateClear(*c)){out->reason=Reason::nativeFailure;return false;}Catalogue catalogue;
    const auto reason=CanPrepare(requester,target,&catalogue);
    if(!Keep(requester)){out->reason=Reason::invalidRequester;return false;}
    c->owner=requester;
    if(Live(target) && Keep(target))c->target=target;
    c->catalogue=std::move(catalogue);FillSnapshot(c->snapshot,requester,target,c->catalogue);
    c->snapshot.reason=reason;c->seen=GameMs();*out=c->snapshot;Publish();return true;
}
bool EnemyControlReadSnapshot(const ObjRef& requester,Snapshot* out) noexcept {
    if(!out)return false;
    for(auto& s:sessions)if(s.snapshot.lease && Same(s.owner,requester)){Selected(s.snapshot,s.catalogue);*out=s.snapshot;out->sourceProtected=s.sourceProtected;ReadBodyStatus(*out,s.target);controlled_pvp::Status team{};
        if(s.pvpLease.id && controlled_pvp::ReadStatus(s.pvpLease,&team)){out->controlledTeam=team.controlledTeam;out->followsOwnerTeam=team.followsOwnerTeam;}
        MergeNetwork(*out);return true;}
    for(auto& c:candidates)if((c.snapshot.phase==Phase::pending || c.snapshot.phase==Phase::restoring || c.snapshot.phase==Phase::active) && Same(c.owner,requester)) {MergeNetwork(c.snapshot);*out=c.snapshot;return true;}
    *out={};return false;
}
bool EnemyControlReadSnapshotForView(std::uintptr_t owner,Snapshot* out) noexcept {
    if(!out || !owner)return false;bool found=false;
    AcquireSRWLockShared(&publishLock);
    for(unsigned i=kCandidates;i<kPublished;++i)if(published[i].snapshot.ownerHumanKey==owner && published[i].snapshot.lease) {
        *out=published[i].snapshot;found=true;break;
    }
    ReleaseSRWLockShared(&publishLock);return found;
}
unsigned EnemyControlAbilities(const Snapshot& snapshot,unsigned first,Ability* out,unsigned capacity) noexcept {
    if(!out || !capacity)return 0;unsigned count=0;
    AcquireSRWLockShared(&publishLock);
    for(const auto& p:published)if(p.snapshot.revision==snapshot.revision && p.snapshot.ownerHumanKey==snapshot.ownerHumanKey &&
        p.snapshot.lease==snapshot.lease && p.snapshot.profileHash==snapshot.profileHash) {
        if(first<p.abilities.size()) {
            count=(std::min)(capacity,static_cast<unsigned>(p.abilities.size()-first));
            std::copy_n(p.abilities.begin()+first,count,out);
        }
        break;
    }
    ReleaseSRWLockShared(&publishLock);return count;
}
bool EnemyControlRequest(const Snapshot& snapshot) noexcept {
    auto* c=ByCandidate(snapshot);
    if(!c || !Live(c->owner) || !Live(c->target) || c->snapshot.phase==Phase::pending ||
        c->snapshot.phase==Phase::restoring || c->snapshot.phase==Phase::active)return false;
    Catalogue fresh;const auto reason=CanPrepare(c->owner,c->target,&fresh);
    if(reason!=Reason::none || fresh.hash!=c->snapshot.profileHash)return false;
    c->snapshot.reason=Reason::none;
    if(network.online && network.online()) {
        c->snapshot.revision=++revision;c->snapshot.request=0;c->snapshot.lease=0;
        const bool sent=network.request && network.request(c->snapshot);
        c->snapshot.phase=sent ? Phase::pending : Phase::failed;c->snapshot.reason=sent ? Reason::none : Reason::networkUnavailable;
        Publish();return sent;
    }
    if(!nextLease)nextLease=1ull<<63;
    const auto lease=++nextLease;
    return EnemyControlBegin(c->owner,c->target,c->snapshot.profileHash,lease,true) && EnemyControlActivate(lease);
}
bool EnemyControlSelectAbility(const Snapshot& snapshot,std::uint32_t ability) noexcept {
    auto* s=ByLease(snapshot.lease);
    if(!s || !s->local || !SessionIdentity(*s,snapshot) || !FindAbility(s->catalogue,ability))return false;
    s->uiHeld=s->physicalHeld=false;
    if(s->snapshot.selectedAbility!=ability){s->snapshot.selectedAbility=ability;s->snapshot.revision=++revision;}
    Selected(s->snapshot,s->catalogue);Publish();return true;
}
bool EnemyControlSetAbilityHeld(const Snapshot& snapshot,bool held) noexcept {
    auto* s=ByLease(snapshot.lease);
    if(!s || !s->local)return false;
    if(!SessionIdentity(*s,snapshot)) {
        // A release from the preceding skill revision is safe for this exact
        // owner/body lease; a different local player's stale card is not.
        if(held || s->snapshot.ownerHumanKey!=snapshot.ownerHumanKey || s->snapshot.profileHash!=snapshot.profileHash ||
            std::memcmp(s->snapshot.owner,snapshot.owner,32) || std::memcmp(s->snapshot.target,snapshot.target,32))return false;
    }
    s->uiHeld=held;return true;
}
bool EnemyControlExit(const Snapshot& snapshot) noexcept {
    auto* s=ByLease(snapshot.lease);
    if(!s) {
        auto* c=ByCandidate(snapshot);
        if(!c || (c->snapshot.phase!=Phase::pending && c->snapshot.phase!=Phase::restoring) || !network.online || !network.online())return false;
        return network.stop && network.stop(snapshot);
    }
    if(!s->local || !SessionIdentity(*s,snapshot))return false;
    s->uiHeld=false;
    if(network.online && network.online())return network.stop && network.stop(snapshot);
    return EnemyControlRestore(snapshot.lease);
}
bool EnemyControlTargetActive(const void* target) noexcept {
    const auto* s=ByObject(target);
    return s && !s->catalogue.adapter && CurrentObject(s->target) &&
        At<const void*>(s->target.obj,0)==s->vtable+1;
}
bool EnemyControlNetworkContext(const void* target,bool* online) noexcept {
    if(!online)return false;
    const auto* s=ByObject(target);
    if(!s || !CurrentObject(s->target))return false;
    if(s->catalogue.adapter) {
        if(!s->adapterAttempted || !s->catalogue.adapter->Match(target) || s->catalogue.adapter->ProfileHash(target)!=s->catalogue.hash)return false;
    } else if(!EnemyControlTargetActive(target))return false;
    *online=s->networked;return true;
}
bool EnemyControlCurrentInput(const void* target,Input* out) noexcept {
    if(!out)return false;*out={};
    const auto* s=ByObject(target);
    if(!On() || !s || s->faulted || s->closing || !EnemyControlTargetActive(target) || s->snapshot.phase!=Phase::active ||
        !Live(s->owner) || !Live(s->target) || !s->received || GameMs()<s->received ||
        GameMs()-s->received>kFreshInput || !s->input.absoluteAim)return false;
    *out=s->input;return true;
}
bool EnemyControlWeaponAim(const void* target,float out[3]) noexcept {
    const auto* session=ByObject(target);
    if(!out || !On() || !session || (!session->catalogue.adapter ? !EnemyControlTargetActive(target) :
            !session->adapterAttempted || !CurrentObject(session->target) || !session->catalogue.adapter->Match(target) ||
            session->catalogue.adapter->ProfileHash(target)!=session->catalogue.hash) ||
        (session->snapshot.phase!=Phase::active && !session->closing) ||
        !(session->closing ? CurrentObject(session->owner) && CurrentObject(session->target) : Live(session->owner) && Live(session->target)) ||
        !session->aimPointValid)return false;
    // Existing native actions may finish at their last accepted target when a
    // heartbeat is late. New input is neutralized after kFreshInput; a finishing
    // burst must not silently snap back to the actor's old autonomous AI target.
    std::memcpy(out,session->aimPoint,12);return true;
}
bool EnemyControlAimTarget(const void* root,ObjRef* out) noexcept {
    if(!out)return false;*out={};float point[3];if(!EnemyControlWeaponAim(root,point))return false;
    const auto* s=ByObject(root);if(Live(s->focus.actor))*out=s->focus.actor;return true;
}
bool EnemyControlWeaponLock(const void* root,void* out16) noexcept {
    if(!out16 || !Readable(out16,16,true))return false;
    float point[3];if(!EnemyControlWeaponAim(root,point))return false;
    std::memset(out16,0,16);const auto* s=ByObject(root);
    if(Live(s->focus.actor) && s->focus.lock.obj)enemy_target::CopyLock(s->focus,out16);
    return true;
}
bool EnemyControlWeaponLockIdentity(const void* root,unsigned char id[32],int* ordinal) noexcept {
    __try {
        if(!id || !ordinal || !Readable(id,32,true) || !Readable(ordinal,sizeof(*ordinal),true))return false;
        std::memset(id,0,32);*ordinal=-1;float point[3];if(!EnemyControlWeaponAim(root,point))return false;
        const auto* session=ByObject(root);const auto& focus=session->focus;
        if(!Live(focus.actor) || !focus.lock.obj || !focus.lock.ctrl || focus.lockIndex<0 || !Readable(focus.actor.obj,0x380))return true;
        const auto count=At<std::uint64_t>(focus.actor.obj,0x378);const auto* table=At<const unsigned char*>(focus.actor.obj,0x368);
        if(count>1024 || static_cast<unsigned>(focus.lockIndex)>=count || !Readable(table,static_cast<std::size_t>(count)*16))return true;
        const auto* member=table+static_cast<unsigned>(focus.lockIndex)*16;
        if(At<const void*>(member,0)!=focus.lock.obj || At<const void*>(member,8)!=focus.lock.ctrl ||
            !Readable(focus.lock.ctrl,16) || At<LONG>(focus.lock.ctrl,8)<=0 || !Readable(focus.lock.obj,0x50) ||
            At<const void*>(focus.lock.obj,8)!=focus.actor.obj || At<int>(focus.lock.obj,0)!=0 ||
            !static_cast<const unsigned char*>(focus.lock.obj)[0x29] || !static_cast<const unsigned char*>(focus.lock.obj)[0x2A])return true;
        unsigned char actual[32]{};
        if(!ReadNativeObjectId(static_cast<const unsigned char*>(focus.actor.obj),actual) || std::memcmp(actual,focus.nativeId,32))return true;
        std::memcpy(id,actual,32);*ordinal=focus.lockIndex;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool EnemyControlOwnsTargetSelector(const void* root,const void* function) noexcept {
    return function==reinterpret_cast<const void*>(&Target14) && EnemyControlTargetActive(root);
}
unsigned EnemyControlWeaponAimRoots(const void** out,unsigned capacity) noexcept {
    if(!out || !capacity)return 0;
    unsigned count=0;
    for(const auto& session:sessions) {
        float point[3];
        if(EnemyControlWeaponAim(session.target.obj,point)) {
            if(count==capacity)break;out[count++]=session.target.obj;
        }
    }
    return count;
}

const unsigned char* EnemyControlBodyForMap(const unsigned char* human) noexcept {
    if(!On() || !human)return nullptr;
    for(const auto& session:sessions)if(session.local && session.snapshot.phase==Phase::active &&
        session.owner.obj==human && Live(session.owner) && Live(session.target))
        return static_cast<const unsigned char*>(session.target.obj);
    return nullptr;
}

bool EnemyControlNormalizeNetworkInput(std::uint64_t lease,Input* input) noexcept {
    auto* s=ByLease(lease);
    if(!s || !input || s->closing || s->snapshot.phase!=Phase::active || !Live(s->owner) || !Live(s->target) || !ValidInput(*input))return false;
    if(!input->absoluteAim){std::memset(input->fullFocusId,0,32);return true;}
    enemy_target::Ray ray{};if(!AimRay(*s,input->aimYaw,input->aimPitch,&ray))return false;
    return enemy_target::Normalize(s->owner,s->target,input->fullFocusId,ray);
}
bool EnemyControlNeedsHostActions(std::uint64_t lease) noexcept {
    const auto* s=ByLease(lease);
    return s && s->networked && s->nativeOwned && !s->catalogue.adapter && CurrentObject(s->target) && enemy_native::NeedsNativeActions(s->native);
}
bool EnemyControlCaptureNetworkAction(std::uint64_t lease,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;auto* s=ByLease(lease);
    return s && EnemyControlNeedsHostActions(lease) && bytes && used && enemy_native::CaptureNativeAction(s->native,bytes,capacity,used);
}
enemy_control::ActionResult EnemyControlApplyNetworkAction(std::uint64_t lease,std::uint64_t sequence,const void* bytes,std::size_t size) noexcept {
    using Result=enemy_control::ActionResult;auto* s=ByLease(lease);
    if(!s || !EnemyControlNeedsHostActions(lease) || !sequence || !bytes || !size || size>512)return Result::failed;
    if(sequence<s->lastAction || (s->pendingAction && sequence!=s->pendingAction))return Result::failed;
    std::uint64_t hash=1469598103934665603ull;
    const auto* p=static_cast<const unsigned char*>(bytes);for(std::size_t i=0;i<size;++i)HashByte(hash,p[i]);
    if(sequence==s->lastAction)return hash==s->lastActionHash ? Result::applied : Result::failed;
    if(s->pendingAction && s->pendingActionHash!=hash)return Result::failed;
    s->pendingAction=sequence;s->pendingActionHash=hash;
    const auto result=enemy_native::ApplyNativeAction(s->native,bytes,size);
    if(result==Result::applied){s->lastAction=sequence;s->lastActionHash=hash;s->pendingAction=0;s->pendingActionHash=0;}
    return result;
}
bool EnemyControlQuiesceNetworkActions(std::uint64_t lease) noexcept {
    auto* s=ByLease(lease);if(!s)return true;
    s->closing=true;s->snapshot.phase=Phase::restoring;for(auto& active:s->input.active)active={};s->queued.clear();s->uiHeld=s->physicalHeld=false;
    bool ok=true;
    if(s->catalogue.adapter)ok=!s->adapterAttempted || s->catalogue.adapter->Apply(s->target.obj,Input{},false);
    else if(s->nativeOwned)ok=enemy_native::QuiesceNativeActions(s->native);
    Publish();return ok;
}
bool EnemyControlNetworkActionsEmpty(std::uint64_t lease) noexcept {
    const auto* s=ByLease(lease);return !s || s->catalogue.adapter || !s->nativeOwned || enemy_native::NativeActionsEmpty(s->native);
}
bool EnemyControlAbortNetworkActions(std::uint64_t lease) noexcept {
    auto* s=ByLease(lease);if(!s)return true;
    s->closing=s->aborted=true;s->snapshot.phase=Phase::restoring;for(auto& active:s->input.active)active={};s->queued.clear();s->uiHeld=s->physicalHeld=false;
    return s->catalogue.adapter || !s->nativeOwned || enemy_native::AbortNativeActions(s->native);
}
bool EnemyControlNeedsNetworkPose(std::uint64_t lease) noexcept {
    const auto* s=ByLease(lease);
    return s && s->networked && !s->catalogue.adapter && s->snapshot.phase==Phase::active &&
        Live(s->owner) && Live(s->target) && enemy_native::PoseRequired(s->native);
}
bool EnemyControlCaptureNetworkPose(std::uint64_t lease,void* bytes,std::size_t capacity,std::size_t* used) noexcept {
    if(used)*used=0;
    const auto* s=ByLease(lease);
    return EnemyControlNeedsNetworkPose(lease) && bytes && used &&
        enemy_native::CapturePose(s->native,bytes,capacity,used);
}
bool EnemyControlApplyNetworkPose(std::uint64_t lease,const void* bytes,std::size_t size) noexcept {
    auto* s=ByLease(lease);
    return EnemyControlNeedsNetworkPose(lease) && bytes && size &&
        enemy_native::ApplyPose(s->native,bytes,size);
}
bool EnemyControlSuppressHuman(const unsigned char* human) noexcept {
    if(!human)return false;
    for(const auto& s:sessions)if(s.local && s.snapshot.lease && s.sourceProtected && s.owner.obj==human && Live(s.owner) && CurrentObject(s.target))return true;
    return false;
}
bool EnemyControlCamera(const unsigned char* human,const float*,const float*,float* eye,float* look) noexcept {
    if(!On() || !human || !eye || !look)return false;
    for(const auto& s:sessions)if(s.local && s.snapshot.phase==Phase::active && s.owner.obj==human && Live(s.owner) && Live(s.target)) {
        const auto* object=static_cast<const unsigned char*>(s.target.obj);
        const auto* position=reinterpret_cast<const float*>(object+0x90);
        // AimPoint follows the native lock point (large monsters and separate
        // body sizes), without guessing a universal neck/bone offset.
        float pivot[3]={position[0],position[1]+2,position[2]};
        edf41c::AimPoint(object,pivot);
        const float height=(std::max)(2.0f,std::fabs(pivot[1]-position[1]));
        const float distance=(std::min)(120.0f,(std::max)(8.0f,height*2.5f));
        const float direction[3]={std::sin(s.yaw)*std::cos(s.pitch),std::sin(s.pitch),std::cos(s.yaw)*std::cos(s.pitch)};
        for(unsigned i=0;i<3;++i){look[i]=pivot[i]+direction[i]*distance;eye[i]=pivot[i]-direction[i]*distance;}
        // As in the native high camera, move only the collision-query pivot
        // a tiny amount toward the eye. The optical target stays unchanged.
        float query[3];for(unsigned i=0;i<3;++i)query[i]=pivot[i]-direction[i]*(std::min)(0.1f,distance*0.5f);
        float hit[3];const float obstruction=MapRay(query,eye,hit);
        // Inclusive start hits carry no usable exit distance. Keep the desired
        // rig rather than collapsing eye onto the target; positive near walls
        // still shorten the rig, without a minimum-distance wall bypass.
        if(obstruction>0.0001f) {
            float along=0;for(unsigned i=0;i<3;++i)along+=(pivot[i]-hit[i])*direction[i];
            if(std::isfinite(along) && along>0 && along<distance) {
                const float stop=(std::max)(0.01f,along-(std::min)(0.05f,along*0.5f));
                for(unsigned i=0;i<3;++i)eye[i]=pivot[i]-direction[i]*stop;
            }
        }
        return true;
    }
    return false;
}
}
