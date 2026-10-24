// Controlled enemy-only native team lease. No player identity, VehicleBase, seat, collision or bullet-filter writes.
#include "pvp_controlled.h"
#include "pvp.h"
#include "memory.h"
#include <cstring>

namespace crew::controlled_pvp {
namespace {
constexpr unsigned kMaxLeases=16,kMaxSpawnGroups=128,kMaxSpawnReservations=128;
constexpr std::size_t kLastTeam=0x318;
constexpr std::uint32_t kRegistered=0x40;
constexpr unsigned char kSetTeamSignature[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
struct MemberSaved { ObjRef ref{};int team=-1,last=-1;bool changed=false,baselineKnown=true,modeKnown=false,disabled=false,registered=false;unsigned wanted=0,group=0; };
struct EmitterSaved { Emitter binding{};int team=-1;bool used=false,changed=false,baselineKnown=true,storageHeld=false;unsigned wanted=0,group=0; };
struct SpawnGroup {bool used=false;ObjRef root{};Hooks hooks{};};
struct State {
    std::uint64_t id=0;ObjRef root{};Hooks hooks{};char puid[65]{};
    MemberSaved members[kMaxMembers]{};EmitterSaved emitters[kMaxEmitters]{};SpawnGroup groups[kMaxSpawnGroups]{};
    unsigned generation=0;int assigned=0;bool restoring=false,tagged=false,modes=false;Status status{};
};
struct Collection { ObjRef members[kMaxMembers]{};unsigned memberGroups[kMaxMembers]{};unsigned count=0;
    Emitter emitters[kMaxEmitters]{};unsigned emitterGroups[kMaxEmitters]{};unsigned emitted=0;int teams[kMaxEmitters]{}; };
struct Reservation {std::uint64_t id=0,lease=0;ObjRef parent{};SpawnProof proof{};unsigned remaining=0;bool accepted=false;};
State leases[kMaxLeases]{};
std::uint64_t serial=0,spawnSerial=0;
Reservation reservations[kMaxSpawnReservations]{};
bool Same(const ObjRef& a,const ObjRef& b) noexcept {return a.obj==b.obj && a.ctrl==b.ctrl;}
bool Same(const Emitter& a,const Emitter& b) noexcept {
    return a.kind==b.kind && Same(a.member,b.member) && a.object==b.object && a.incarnation==b.incarnation && Same(a.storage,b.storage);
}
bool TeamValid(int team) noexcept {return team>=-1 && team<7;}
bool DynamicOwner(const Emitter& emitter) noexcept {
    return emitter.kind==EmitterKind::liveOwnerTeam || emitter.kind==EmitterKind::liveOwnerLastTeam;
}
bool FollowsOwner(const Emitter& emitter) noexcept {
    return DynamicOwner(emitter) || emitter.kind==EmitterKind::ownerTeamCache || emitter.kind==EmitterKind::ownerLastTeamCache;
}
int OwnerField(const Emitter& emitter) noexcept {
    const bool last=emitter.kind==EmitterKind::liveOwnerLastTeam || emitter.kind==EmitterKind::ownerLastTeamCache;
    return At<int>(emitter.member.obj,last ? kLastTeam : kTeam);
}
Life MemberLife(const ObjRef& ref) noexcept {
    if(!ref.obj || !ref.ctrl || !Readable(ref.ctrl,16))return Life::invalid;
    if(At<LONG>(ref.ctrl,8)<=0)return Life::gone;
    if(!Readable(ref.obj,kObjectFlags+4))return Life::invalid;
    if(At<const void*>(ref.obj,kSelfCtrl)!=ref.ctrl || (At<unsigned char>(ref.obj,0x18)&4u))return Life::gone;
    return Life::live;
}
bool Live(const ObjRef& ref) noexcept {return MemberLife(ref)==Life::live;}
bool CurrentIdentity(const ObjRef& ref) noexcept {
    return ref.obj && ref.ctrl && Readable(ref.ctrl,16) && At<LONG>(ref.ctrl,8)>0 &&
        Readable(ref.obj,kSelfCtrl+sizeof(void*)) && At<const void*>(ref.obj,kSelfCtrl)==ref.ctrl && At<const void*>(ref.obj,kSelf)==ref.obj;
}
Life StorageLife(const ObjRef& ref) noexcept {
    if(!ref.obj || !ref.ctrl || !Readable(ref.ctrl,16))return Life::invalid;
    if(At<LONG>(ref.ctrl,8)<=0)return Life::gone;
    if(!Readable(ref.obj,kSelfCtrl+sizeof(void*)))return Life::invalid;
    if(At<const void*>(ref.obj,kSelfCtrl)!=ref.ctrl || (At<unsigned char>(ref.obj,0x18)&4u))return Life::gone;
    return At<const void*>(ref.obj,kSelf)==ref.obj ? Life::live : Life::invalid;
}
void ReleaseEmitter(EmitterSaved& emitter) noexcept {
    if(emitter.storageHeld) {
        auto* control=const_cast<unsigned char*>(static_cast<const unsigned char*>(emitter.binding.storage.ctrl));
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+0xC))==0)
            reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(control,0)[1])(control);
    }
    emitter={};
}
bool HoldStorage(const Emitter& binding) noexcept {
    if(!binding.storage.obj && !binding.storage.ctrl)return true;
    if(StorageLife(binding.storage)!=Life::live || !Readable(binding.storage.ctrl,16,true))return false;
    InterlockedIncrement(reinterpret_cast<volatile LONG*>(const_cast<unsigned char*>(static_cast<const unsigned char*>(binding.storage.ctrl))+0xC));
    return true;
}
bool Ready() noexcept {return image && Matches(kSetTeam,kSetTeamSignature,sizeof(kSetTeamSignature));}
void WeakRelease(MemberSaved& member) noexcept {
    if(member.ref.ctrl) {
        auto* control=const_cast<unsigned char*>(static_cast<const unsigned char*>(member.ref.ctrl));
        if(InterlockedDecrement(reinterpret_cast<volatile LONG*>(control+0xC))==0)
            reinterpret_cast<void(__fastcall*)(void*)>(At<void* const*>(control,0)[1])(control);
    }
    member={};
}
State* Find(std::uint64_t id) noexcept {if(id)for(auto& s:leases)if(s.id==id)return &s;return nullptr;}
MemberSaved* Member(State& s,const ObjRef& ref) noexcept {for(auto& m:s.members)if(m.ref && Same(m.ref,ref))return &m;return nullptr;}
EmitterSaved* Binding(State& s,const Emitter& binding) noexcept {for(auto& e:s.emitters)if(e.used && Same(e.binding,binding))return &e;return nullptr;}
const Hooks& GroupHooks(const State& s,unsigned group) noexcept {return group ? s.groups[group-1].hooks : s.hooks;}
const ObjRef& GroupRoot(const State& s,unsigned group) noexcept {return group ? s.groups[group-1].root : s.root;}
bool Collected(const Collection& c,const ObjRef& ref) noexcept {for(unsigned i=0;i<c.count;++i)if(Same(c.members[i],ref))return true;return false;}
bool Gather(const ObjRef& root,const Hooks& hooks,Collection& c) noexcept {
    if(!Ready() || !Live(root) || !hooks.collect ||
       !hooks.collect(hooks.user,root,c.members,kMaxMembers,&c.count,c.emitters,kMaxEmitters,&c.emitted) ||
       !c.count || c.count>kMaxMembers || c.emitted>kMaxEmitters || !Collected(c,root) ||
       (c.emitted && (!hooks.readEmitter || !hooks.writeEmitter)))return false;
    for(unsigned i=0;i<c.count;++i) {
        if(!Live(c.members[i]) || !TeamValid(At<int>(c.members[i].obj,kTeam)) || !TeamValid(At<int>(c.members[i].obj,kLastTeam)))return false;
        // Permanently disabled structural parts may legitimately have both fields -1.
        // If such a part later opens, restoration needs typed proof; never invent an enabled side.
        if(At<int>(c.members[i].obj,kTeam)!=-1 && At<int>(c.members[i].obj,kLastTeam)<0)return false;
        for(unsigned j=0;j<i;++j)if(Same(c.members[i],c.members[j]) || c.members[i].obj==c.members[j].obj)return false;
    }
    for(unsigned i=0;i<c.emitted;++i) {
        const auto& e=c.emitters[i];
        if(!e.object || !Collected(c,e.member) || e.kind<EmitterKind::weaponBase || e.kind>EmitterKind::ownerLastTeamCache ||
           ((e.storage.obj || e.storage.ctrl) &&
            ((e.kind!=EmitterKind::ownerTeamCache && e.kind!=EmitterKind::ownerLastTeamCache) || StorageLife(e.storage)!=Life::live)) ||
           hooks.readEmitter(hooks.user,e,&c.teams[i])!=Life::live || !TeamValid(c.teams[i]))return false;
        for(unsigned j=0;j<i;++j)if(Same(e,c.emitters[j]))return false;
    }
    return true;
}
bool AppendGroup(Collection& out,const Collection& next,unsigned group) noexcept {
    if(next.count>kMaxMembers-out.count || next.emitted>kMaxEmitters-out.emitted)return false;
    for(unsigned i=0;i<next.count;++i) {
        if(Collected(out,next.members[i]))return false;
        out.members[out.count]=next.members[i];out.memberGroups[out.count++]=group;
    }
    for(unsigned i=0;i<next.emitted;++i) {
        for(unsigned j=0;j<out.emitted;++j)if(Same(out.emitters[j],next.emitters[i]))return false;
        out.emitters[out.emitted]=next.emitters[i];out.teams[out.emitted]=next.teams[i];out.emitterGroups[out.emitted++]=group;
    }
    return true;
}
bool GatherState(State& s,Collection& out,bool restoring=false) noexcept {
    const Life rootLife=MemberLife(s.root);
    if(rootLife==Life::live) {
        Collection main{};if(!Gather(s.root,s.hooks,main) || !AppendGroup(out,main,0))return false;
    } else if(rootLife==Life::invalid || !restoring)return false;
    for(unsigned i=0;i<kMaxSpawnGroups;++i)if(s.groups[i].used) {
        const Life life=MemberLife(s.groups[i].root);if(life==Life::invalid)return false;
        if(life==Life::gone)continue;
        Collection extra{};
        if(!Gather(s.groups[i].root,s.groups[i].hooks,extra) || !AppendGroup(out,extra,i+1))return false;
    }
    return true;
}
void Publish(State& s,Result result,bool follows=false) noexcept {
    s.status={};s.status.result=result;s.status.restoring=s.restoring;s.status.followsOwnerTeam=follows;
    if(Live(s.root))s.status.controlledTeam=At<int>(s.root.obj,kTeam);
    for(const auto& m:s.members)s.status.members+=m.ref ? 1u : 0u;
    for(const auto& e:s.emitters)s.status.emitters+=e.used ? 1u : 0u;
}
void CaptureModes(State& s) noexcept {
    s.modes=true;
    for(auto& member:s.members)if(member.ref && !member.modeKnown && Live(member.ref)) {
        member.disabled=At<int>(member.ref.obj,kTeam)==-1;
        member.registered=(At<std::uint32_t>(member.ref.obj,kObjectFlags)&kRegistered)!=0;member.modeKnown=true;
    }
}
void ReleaseModes(State& s) noexcept {s.modes=false;for(auto& member:s.members)member.modeKnown=false;}
bool NativeTeam(MemberSaved& member,int team,int last) noexcept {
    const Life life=MemberLife(member.ref);if(life==Life::gone)return true;
    if(life!=Life::live || !Ready())return false;
    auto* object=const_cast<void*>(member.ref.obj);
    if(!member.modeKnown)return false;
    const int desired=member.disabled ? -1 : team;
    const bool registered=member.registered;
    // Some native slot-22 overrides recursively retag children. Use the pre-operation state of the ENTIRE set,
    // not a child flag already modified by its parent's SetTeam callback.
    if(desired==-1 && registered)return false;
    if(At<int>(object,kTeam)!=desired || ((At<std::uint32_t>(object,kObjectFlags)&kRegistered)!=0)!=registered)
        reinterpret_cast<void(__fastcall*)(void*,int,bool)>(image+kSetTeam)(object,desired,registered);
    Put<int>(object,kLastTeam,last); // SetTeam(-1,false) intentionally does not write this native field.
    return Live(member.ref) && At<int>(object,kTeam)==desired && At<int>(object,kLastTeam)==last &&
        ((At<std::uint32_t>(object,kObjectFlags)&kRegistered)!=0)==registered;
}
bool KnowMember(State& s,MemberSaved& member) noexcept {
    if(member.baselineKnown)return true;
    const Life life=MemberLife(member.ref);
    if(life==Life::gone){member.baselineKnown=true;member.changed=false;return true;}
    if(life!=Life::live)return false;
    const auto& hooks=GroupHooks(s,member.group);const auto& groupRoot=GroupRoot(s,member.group);
    const MemberSaved* root=Member(s,groupRoot);
    int team=-1,last=-1;
    if(!root || !root->baselineKnown || !hooks.memberBaseline ||
       !hooks.memberBaseline(hooks.user,groupRoot,member.ref,root->team,root->last,&team,&last) ||
       !TeamValid(team) || !TeamValid(last) || (team!=-1 && last<0))return false;
    member.team=team;member.last=last;member.baselineKnown=true;return true;
}
bool RestoreMember(State& s,MemberSaved& member) noexcept {
    const Life life=MemberLife(member.ref);
    if(life==Life::gone){member.changed=false;return true;}
    if(life!=Life::live)return false;
    if(!member.changed && !s.tagged)return true;
    if(!KnowMember(s,member))return false;
    const int enabledTeam=member.team==-1 ? member.last : member.team;
    // A structural (-1,-1) member has no valid original enabled side. An invalid
    // native open can fault restoration; a later real close is the only safe state
    // that can release it. Keep all other operation snapshots across recursive retries.
    if(enabledTeam==-1 && member.modeKnown && !member.disabled &&
       At<int>(member.ref.obj,kTeam)==-1 && !(At<std::uint32_t>(member.ref.obj,kObjectFlags)&kRegistered)) {
        member.disabled=true;member.registered=false;
    }
    if(!member.modeKnown || !TeamValid(enabledTeam) || (!member.disabled && enabledTeam<0) ||
       !NativeTeam(member,enabledTeam,member.last))return false;
    member.changed=false;return true;
}
Life ReadBinding(State& s,const Emitter& binding,unsigned group,int* team) noexcept {
    if(binding.storage.obj || binding.storage.ctrl) {
        const Life life=StorageLife(binding.storage);if(life!=Life::live)return life;
    } else {const Life life=MemberLife(binding.member);if(life!=Life::live)return life;}
    const auto& hooks=GroupHooks(s,group);
    if(!hooks.readEmitter)return Life::invalid;
    const Life life=hooks.readEmitter(hooks.user,binding,team);
    // A detached owner/container does not destroy independently retained storage.
    // Only the full storage identity above may retire that cache without restoring it.
    if(binding.storage.ctrl && life==Life::gone)return Life::invalid;
    return life==Life::live && !TeamValid(*team) ? Life::invalid : life;
}
bool RestoreEmitter(State& s,EmitterSaved& emitter) noexcept {
    if(!emitter.used || (!emitter.changed && !s.tagged))return true;
    int current=-1;const Life life=ReadBinding(s,emitter.binding,emitter.group,&current);
    if(life==Life::gone){emitter.changed=false;return true;}
    const auto& hooks=GroupHooks(s,emitter.group);
    if(life!=Life::live || !hooks.writeEmitter)return false;
    if(DynamicOwner(emitter.binding)) {
        if(current!=OwnerField(emitter.binding))return false;
        emitter.changed=false;return true;
    }
    if(!emitter.baselineKnown) {
        MemberSaved* owner=Member(s,emitter.binding.member);int team=-1;
        if(!owner || !KnowMember(s,*owner) || !hooks.emitterBaseline ||
           !hooks.emitterBaseline(hooks.user,emitter.binding,owner->team,owner->last,&team) || !TeamValid(team))return false;
        emitter.team=team;emitter.baselineKnown=true;
    }
    if(current!=emitter.team && !hooks.writeEmitter(hooks.user,emitter.binding,emitter.team))return false;
    current=-1;
    const Life after=ReadBinding(s,emitter.binding,emitter.group,&current);
    if(after==Life::gone){emitter.changed=false;return true;}
    if(after!=Life::live || current!=emitter.team)return false;
    emitter.changed=false;return true;
}
bool RestoreItems(State& s,bool detachedOnly=false) noexcept {
    CaptureModes(s);
    bool ok=true;
    for(auto& m:s.members)if(m.ref && (!detachedOnly || m.wanted!=s.generation) && !RestoreMember(s,m))ok=false;
    for(auto& e:s.emitters)if(e.used && (!detachedOnly || e.wanted!=s.generation) && !RestoreEmitter(s,e))ok=false;
    if(ok && detachedOnly) {
        for(auto& e:s.emitters)if(e.used && e.wanted!=s.generation)ReleaseEmitter(e);
        for(auto& group:s.groups)if(group.used && MemberLife(group.root)==Life::gone)group={};
        for(auto& m:s.members)if(m.ref && m.wanted!=s.generation && !Same(m.ref,s.root))WeakRelease(m);
    }
    return ok;
}
bool Sync(State& s,const Collection& c) noexcept {
    const bool inherited=s.tagged;
    if(++s.generation==0){s.generation=1;for(auto& m:s.members)m.wanted=0;for(auto& e:s.emitters)e.wanted=0;}
    // Reserve/save the entire current write set before applying a single team.
    for(unsigned i=0;i<c.count;++i) {
        const auto& ref=c.members[i];
        MemberSaved* saved=Member(s,ref);
        if(!saved) {
            for(const auto& other:leases)if(other.id && &other!=&s)
                for(const auto& m:other.members)if(m.ref && Same(m.ref,ref))return false;
            const int team=At<int>(ref.obj,kTeam),last=At<int>(ref.obj,kLastTeam);
            if(!TeamValid(team) || !TeamValid(last) || (team!=-1 && last<0))return false;
            for(auto& slot:s.members)if(!slot.ref){saved=&slot;break;}
            if(!saved || !Live(ref))return false;
            auto* control=const_cast<unsigned char*>(static_cast<const unsigned char*>(ref.ctrl));
            InterlockedIncrement(reinterpret_cast<volatile LONG*>(control+0xC));
            *saved={};saved->ref=ref;saved->team=team;saved->last=last;
            saved->changed=inherited;saved->baselineKnown=!inherited;saved->wanted=s.generation;saved->group=c.memberGroups[i];
        } else {
            if(!inherited){saved->team=At<int>(ref.obj,kTeam);saved->last=At<int>(ref.obj,kLastTeam);saved->baselineKnown=true;}
            saved->wanted=s.generation;
        }
        if(!KnowMember(s,*saved))return false;
    }
    for(unsigned i=0;i<c.emitted;++i) {
        const auto& binding=c.emitters[i];EmitterSaved* saved=Binding(s,binding);
        if(!saved) {
            MemberSaved* owner=Member(s,binding.member);
            if(!owner || !KnowMember(s,*owner))return false;
            for(auto& slot:s.emitters)if(!slot.used){saved=&slot;break;}
            if(!saved || !HoldStorage(binding))return false;
            *saved={};saved->binding=binding;saved->team=c.teams[i];saved->used=true;saved->storageHeld=binding.storage.ctrl!=nullptr;
            saved->changed=inherited;saved->baselineKnown=!inherited || DynamicOwner(binding);saved->wanted=s.generation;saved->group=c.emitterGroups[i];
        } else {
            if(!inherited){saved->team=c.teams[i];saved->baselineKnown=true;}
            saved->wanted=s.generation;
        }
        if(!saved->baselineKnown) {
            MemberSaved* owner=Member(s,binding.member);int team=-1;const auto& hooks=GroupHooks(s,saved->group);
            if(!owner || !hooks.emitterBaseline ||
               !hooks.emitterBaseline(hooks.user,binding,owner->team,owner->last,&team) || !TeamValid(team))return false;
            saved->team=team;saved->baselineKnown=true;
        }
    }
    CaptureModes(s);return RestoreItems(s,true);
}
bool PrepareRestore(State& s) noexcept {
    CaptureModes(s);
    bool complete=true;
    if(s.tagged) {
        Collection current{};
        complete=GatherState(s,current,true) && Sync(s,current);
    }
    const bool restored=RestoreItems(s);
    return complete && restored;
}
bool Apply(State& s,int team) noexcept {
    CaptureModes(s);s.tagged=true;
    for(auto& m:s.members)if(m.ref && m.wanted==s.generation) {
        m.changed=true;
        if(!Live(m.ref) || !NativeTeam(m,team,team))return false;
    }
    for(auto& e:s.emitters)if(e.used && e.wanted==s.generation) {
        int current=-1;
        const auto& hooks=GroupHooks(s,e.group);
        if(ReadBinding(s,e.binding,e.group,&current)!=Life::live || !hooks.writeEmitter)return false;
        e.changed=true;
        const int desired=FollowsOwner(e.binding) ? OwnerField(e.binding) : team;
        if(current!=desired && !hooks.writeEmitter(hooks.user,e.binding,desired))return false;
        if(ReadBinding(s,e.binding,e.group,&current)!=Life::live || current!=desired)return false;
    }
    s.assigned=team;return true;
}
bool ApplyAcceptedGroup(State& s,unsigned group) noexcept {
    const int team=PvpSupportTeam(s.puid);
    if(team<0 || team>2)return false;
    // The parent may be logically deleted but is still held by the accepted-action consumer.
    // Only this new, unpublished group can be tagged; never run AI or SetTeam on its dead parent.
    if(team) {
        CaptureModes(s);s.tagged=true;
        for(auto& m:s.members)if(m.ref && m.group==group) {
            m.changed=true;if(!Live(m.ref) || !NativeTeam(m,team,team))return false;
        }
        for(auto& e:s.emitters)if(e.used && e.group==group) {
            int current=-1;const auto& hooks=GroupHooks(s,group);
            if(ReadBinding(s,e.binding,group,&current)!=Life::live || !hooks.writeEmitter)return false;
            e.changed=true;const int desired=FollowsOwner(e.binding) ? OwnerField(e.binding) : team;
            if(current!=desired && !hooks.writeEmitter(hooks.user,e.binding,desired))return false;
            if(ReadBinding(s,e.binding,group,&current)!=Life::live || current!=desired)return false;
        }
        ReleaseModes(s);
    }
    // A closing root cannot claim the whole existing family follows a fresh side.
    Publish(s,Result::ready,false);return true;
}
bool Fault(State& s,Result reason) noexcept {
    s.restoring=true;
    const bool restored=PrepareRestore(s);
    Publish(s,restored ? reason : Result::restoring);return false;
}
bool Step(State& s) noexcept {
    if(s.restoring){Publish(s,Result::restoring);return false;}
    const int team=PvpSupportTeam(s.puid);
    if(team<0){Publish(s,Result::pvpPending);return false;}
    if(team>2)return Fault(s,Result::unsupported);
    CaptureModes(s);
    if(team==0) {
        if(!PrepareRestore(s))return Fault(s,Result::nativeFailure);
        s.tagged=false;s.assigned=0;
    }
    Collection collection{};
    if(!GatherState(s,collection))return Fault(s,Result::unsupported);
    if(!Sync(s,collection))return Fault(s,Result::capacity);
    if(team && !Apply(s,team))return Fault(s,Result::nativeFailure);
    ReleaseModes(s);Publish(s,Result::ready,team!=0);return true;
}
}
bool Probe(const ObjRef& root,const Hooks& hooks) noexcept {
    __try {Collection collection{};return Gather(root,hooks,collection);}
    __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool Acquire(const ObjRef& root,const char* authenticatedSourcePuid,const Hooks& hooks,Lease* out) noexcept {
    if(!out || out->id || !pvp_protocol::ValidPuid(authenticatedSourcePuid))return false;
    __try {
        if(PvpSupportTeam(authenticatedSourcePuid)<0)return false;
        Collection collection{};if(!Gather(root,hooks,collection))return false;
        State* state=nullptr;
        for(auto& s:leases) {
            if(s.id && Same(s.root,root))return false;
            if(!s.id && !state)state=&s;
        }
        if(!state || serial==UINT64_MAX)return false;
        *state={};state->id=++serial;state->root=root;state->hooks=hooks;
        std::memcpy(state->puid,authenticatedSourcePuid,std::strlen(authenticatedSourcePuid)+1);out->id=state->id;
        if(!Sync(*state,collection)){Fault(*state,Result::capacity);Restore(out);return false;}
        const int team=PvpSupportTeam(authenticatedSourcePuid);
        if(team<0){Publish(*state,Result::pvpPending);return false;}
        if(team>2 || (team && !Apply(*state,team))) {Fault(*state,Result::nativeFailure);Restore(out);return false;}
        ReleaseModes(*state);Publish(*state,Result::ready,team!=0);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(State* s=Find(out->id)){s->restoring=true;Publish(*s,Result::restoring);Restore(out);}
        return false;
    }
}
bool Frame(const Lease& lease) noexcept {
    __try {State* s=Find(lease.id);return s && Step(*s);}
    __except(EXCEPTION_EXECUTE_HANDLER) {
        if(State* s=Find(lease.id)){s->restoring=true;Publish(*s,Result::restoring);}
        return false;
    }
}
bool Restore(Lease* lease) noexcept {
    if(!lease)return false;
    if(!lease->id)return true;
    __try {
        State* s=Find(lease->id);if(!s)return false;
        s->restoring=true;
        if(!PrepareRestore(*s)){Publish(*s,Result::restoring);return false;}
        for(auto& r:reservations)if(r.lease==s->id)r={};
        for(auto& e:s->emitters)if(e.used)ReleaseEmitter(e);
        for(auto& m:s->members)if(m.ref)WeakRelease(m);
        *s={};lease->id=0;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(State* s=Find(lease->id)){s->restoring=true;Publish(*s,Result::restoring);}
        return false;
    }
}
bool ReadStatus(const Lease& lease,Status* out) noexcept {
    if(!out)return false;
    __try {
        State* s=Find(lease.id);if(!s){*out={};return false;}
        *out=s->status;
        if(Live(s->root))out->controlledTeam=At<int>(s->root.obj,kTeam);
        else {out->controlledTeam=-1;out->followsOwnerTeam=false;}
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){*out={};return false;}
}
bool ReadBaseline(const ObjRef& root,const ObjRef& member,int* team,int* lastTeam) noexcept {
    if(!team || !lastTeam)return false;
    for(auto& s:leases)if(s.id && Same(s.root,root)) {
        const auto* original=Member(s,member);
        if(!original || !original->baselineKnown)return false;
        *team=original->team;*lastTeam=original->last;return true;
    }
    return false;
}

bool ReadAppliedTeam(const ObjRef& member,int* team) noexcept {
    if(!team || !Live(member))return false;
    for(auto& s:leases)if(s.id && Member(s,member) && !s.restoring && s.tagged && s.status.result==Result::ready &&
        s.status.followsOwnerTeam && (s.assigned==1 || s.assigned==2) && PvpSupportTeam(s.puid)==s.assigned) {
        *team=s.assigned;return true;
    }
    return false;
}

bool ReadBaselineForMember(const ObjRef& member,int* team,int* lastTeam) noexcept {
    if(!team || !lastTeam)return false;
    const MemberSaved* found=nullptr;
    for(auto& s:leases)if(s.id)if(const MemberSaved* original=Member(s,member)) {
        if(!original->baselineKnown || found)return false;
        found=original;
    }
    if(!found)return false;
    *team=found->team;*lastTeam=found->last;return true;
}

bool HasLeases() noexcept {for(const auto& s:leases)if(s.id)return true;return false;}
bool HasTemporaryTeam(const ObjRef& member) noexcept {
    State* found=nullptr;
    for(auto& s:leases)if(s.id && Member(s,member)) {if(found)return false;found=&s;}
    return found && found->tagged;
}

bool ReadControlRoot(const ObjRef& member,ObjRef* root) noexcept {
    if(!root)return false;*root={};
    State* found=nullptr;
    for(auto& s:leases)if(s.id && Member(s,member)) {if(found)return false;found=&s;}
    if(!found)return false;*root=found->root;return true;
}

bool ReadEmitterBaseline(const Emitter& binding,int* team) noexcept {
    if(!team)return false;
    const EmitterSaved* found=nullptr;
    for(auto& s:leases)if(s.id)if(const auto* original=Binding(s,binding)) {
        if(!original->baselineKnown || found)return false;
        found=original;
    }
    if(!found)return false;
    *team=found->team;return true;
}

namespace {
State* OwnerOf(const ObjRef& parent) noexcept {
    State* owner=nullptr;
    for(auto& s:leases)if(s.id && Member(s,parent)) {if(owner)return nullptr;owner=&s;}
    return owner;
}
bool SpawnRoom(const State& s,const SpawnProof& proof,unsigned count) noexcept {
    if(!count || !proof.hooks.collect || !proof.maxMembers || proof.maxMembers>kMaxMembers || proof.maxEmitters>kMaxEmitters)return false;
    std::uint64_t members=0,emitters=0,groups=0;
    for(const auto& m:s.members)members+=m.ref ? 1u : 0u;
    for(const auto& e:s.emitters)emitters+=e.used ? 1u : 0u;
    for(const auto& g:s.groups)groups+=g.used ? 1u : 0u;
    for(const auto& r:reservations)if(r.id && r.lease==s.id) {
        members+=std::uint64_t(r.remaining)*r.proof.maxMembers;emitters+=std::uint64_t(r.remaining)*r.proof.maxEmitters;groups+=r.remaining;
    }
    return members+std::uint64_t(count)*proof.maxMembers<=kMaxMembers &&
        emitters+std::uint64_t(count)*proof.maxEmitters<=kMaxEmitters && groups+count<=kMaxSpawnGroups;
}
}
bool CanPrepareSpawns(const ObjRef& parent,const SpawnProof& proof,unsigned count) noexcept {
    __try {
        State* s=OwnerOf(parent);if(!s)return false;const int team=PvpSupportTeam(s->puid);
        return !s->restoring && Live(parent) && team>=0 && team<=2 && SpawnRoom(*s,proof,count);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool PrepareSpawns(const ObjRef& parent,const SpawnProof& proof,unsigned count,std::uint64_t* token) noexcept {
    if(token)*token=0;
    if(!token)return false;
    __try {
        if(!CanPrepareSpawns(parent,proof,count) || spawnSerial==UINT64_MAX)return false;
        State* s=OwnerOf(parent);if(!s)return false;
        for(auto& r:reservations)if(!r.id) {
            r={};r.id=++spawnSerial;r.lease=s->id;r.parent=parent;r.proof=proof;r.remaining=count;*token=r.id;return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool CanPrepareAcceptedSpawns(const ObjRef& parent,const SpawnProof& proof,unsigned count) noexcept {
    __try {
        State* s=OwnerOf(parent);if(!s || s->restoring || !CurrentIdentity(parent) || !CurrentIdentity(s->root))return false;
        const auto* saved=Member(*s,parent);const int team=PvpSupportTeam(s->puid);
        return saved && saved->baselineKnown && team>=0 && team<=2 && SpawnRoom(*s,proof,count);
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool PrepareAcceptedSpawns(const ObjRef& parent,const SpawnProof& proof,unsigned count,std::uint64_t* token) noexcept {
    if(token)*token=0;if(!token || !CanPrepareAcceptedSpawns(parent,proof,count))return false;
    __try {
        State* s=OwnerOf(parent);if(!s || spawnSerial==UINT64_MAX)return false;
        for(auto& r:reservations)if(!r.id) {
            r={};r.id=++spawnSerial;r.lease=s->id;r.parent=parent;r.proof=proof;r.remaining=count;r.accepted=true;*token=r.id;return true;
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool AdoptSpawn(std::uint64_t token,const ObjRef& parent,const ObjRef& child) noexcept {
    __try {
        Reservation* reservation=nullptr;State* s=nullptr;
        const auto resolve=[&]() noexcept {
            reservation=nullptr;s=nullptr;
            for(auto& r:reservations)if(r.id==token){reservation=&r;break;}
            if(!reservation || !reservation->remaining || !Same(reservation->parent,parent))return false;
            s=Find(reservation->lease);if(!s || s->restoring || !Member(*s,parent))return false;
            const int desired=PvpSupportTeam(s->puid);
            return desired>=0 && desired<=2 && (reservation->accepted ?
                (CurrentIdentity(parent) && CurrentIdentity(s->root) && Member(*s,parent)->baselineKnown) : Live(parent));
        };
        if(!resolve())return false;
        Collection born{};unsigned group=kMaxSpawnGroups;
        const auto inspect=[&]() noexcept {
            born={};group=kMaxSpawnGroups;
            if(!Gather(child,reservation->proof.hooks,born) || born.count>reservation->proof.maxMembers ||
               born.emitted>reservation->proof.maxEmitters)return false;
            for(unsigned i=0;i<kMaxSpawnGroups;++i)if(!s->groups[i].used){group=i;break;}
            if(group==kMaxSpawnGroups)return false;
            unsigned freeMembers=0,freeEmitters=0;
            for(const auto& m:s->members)freeMembers+=m.ref ? 0u : 1u;
            for(const auto& e:s->emitters)freeEmitters+=e.used ? 0u : 1u;
            if(freeMembers<born.count || freeEmitters<born.emitted)return false;
            for(unsigned i=0;i<born.count;++i)for(const auto& other:leases)if(other.id)
                for(const auto& m:other.members)if(m.ref && Same(m.ref,born.members[i]))return false;
            return true;
        };
        if(!inspect())return false;
        if(reservation->proof.prepareInstance) {
            const auto originalLease=s->id;
            const auto prepare=reservation->proof.prepareInstance;void* const user=reservation->proof.hooks.user;
            if(!prepare(user,child))return false;
            // Native initialization can dispatch callbacks. Never retain a borrowed token/state across that boundary.
            if(!resolve() || s->id!=originalLease || !inspect())return false;
        }
        // Pin the group root first: even a partial capture must never leave a borrowed group-control pointer behind.
        const auto capture=[s,group](const ObjRef& ref) noexcept {
            if(!Live(ref))return false;
            for(auto& m:s->members)if(!m.ref) {
                auto* control=const_cast<unsigned char*>(static_cast<const unsigned char*>(ref.ctrl));
                InterlockedIncrement(reinterpret_cast<volatile LONG*>(control+0xC));m={};m.ref=ref;
                m.team=At<int>(ref.obj,kTeam);m.last=At<int>(ref.obj,kLastTeam);m.baselineKnown=true;m.group=group+1;m.wanted=s->generation;return true;
            }
            return false;
        };
        if(!capture(child))return false;
        s->groups[group]={true,child,reservation->proof.hooks};
        // The newborn has its real native baseline and has not been activated/published. Save it before inheriting PVP.
        for(unsigned i=0;i<born.count;++i)if(!Same(born.members[i],child) && !capture(born.members[i]))return Fault(*s,Result::invalidIdentity);
        for(unsigned i=0;i<born.emitted;++i)for(auto& e:s->emitters)if(!e.used) {
            if(!HoldStorage(born.emitters[i]))return Fault(*s,Result::invalidIdentity);
            e={};e.binding=born.emitters[i];e.team=born.teams[i];e.used=true;e.storageHeld=e.binding.storage.ctrl!=nullptr;
            e.baselineKnown=true;e.group=group+1;e.wanted=s->generation;break;
        }
        --reservation->remaining;
        if(reservation->accepted && !Live(s->root)) {
            if(ApplyAcceptedGroup(*s,group+1))return true;
            return Fault(*s,Result::nativeFailure);
        }
        return Step(*s);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(State* s=OwnerOf(parent)){s->restoring=true;Publish(*s,Result::restoring);}
        return false;
    }
}
void FinishSpawns(std::uint64_t token) noexcept {if(token)for(auto& r:reservations)if(r.id==token){r={};return;}}

bool FaultForMember(const ObjRef& member,Result reason) noexcept {
    __try {
        State* s=OwnerOf(member);if(!s)return false;
        Fault(*s,reason);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(State* s=OwnerOf(member)){s->restoring=true;Publish(*s,Result::restoring);return true;}
        return false;
    }
}

bool OwnsMember(const ObjRef& member) noexcept {
    for(auto& s:leases)if(s.id && Member(s,member))return true;
    return false;
}

}
