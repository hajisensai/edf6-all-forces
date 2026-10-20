// EDF6Coop's room member attributes (coop_member_attribute_api.h, docs/member-attribute-api.md) as every module of the
// plugin uses them: the EDF4.1 outfit (outfit41.cpp), the EDF5 / EDF4.1 player count (edf5online_room.cpp), the PvP
// capability (pvp.cpp), the room pre-sync (room_sync.cpp), the room's own checks (mod_room.cpp) and the host's settings
// (host_settings.cpp). One copy of:
//   - the table and the mission-player resolver looked up in EDF6Coop.dll (again when the module changes: a test
//     harness may load it late), refused when its version or size is not v1's (Api / ResolvePuid, coop_attr.cpp);
//   - the 500 ms beat a room frame ticks them on (Beat);
//   - a key published only when its value changed, EDF6Coop's refusal logged once a reason (Publisher), its first read
//     starting the watch (Watch);
//   - a member's value by its EOS id, the host's, a mission player's (its mission index resolved to its member), and
//     whether every member / every mission player has a value that passes a test (MemberValue, HostValue,
//     MissionPlayerValue, EveryMember, EveryMissionPlayer).
// The functions below take the table and the resolver as arguments, so tests/coop_attr_test.cpp runs them over a fake
// table offline. Any thread (EDF6Coop's contract); the callbacks run on the calling thread.
#pragma once
#include "coop_extension_api.h"
#include "coop_member_attribute_api.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace crew::coopattr {
using Table=EDF6CoopMemberAttributeApi;
using ResolveFn=EDF6CoopResolveMissionPlayerPuidFn;
inline constexpr std::size_t kValueMost=EDF6COOP_MEMBER_ATTRIBUTE_MAX_VALUE+1;

// The table as EDF6Coop hands it, kept only when it is v1's and reaches `snapshot` (what every reader needs).
inline const Table* Usable(const Table* t) noexcept {
    return t && t->version==EDF6COOP_MEMBER_ATTRIBUTE_VERSION && EDF6COOP_MA_HAS(t,snapshot) && t->snapshot ? t : nullptr;
}

// A room frame's beat: Due at most every `every` ms (the first call always).
struct Beat {
    std::uint64_t last=0;
    bool Due(std::uint64_t now,std::uint64_t every=500) noexcept {
        if(last && now-last<every)return false;
        last=now;
        return true;
    }
};

// A key's watch: the first snapshot of a key starts EDF6Coop reading it (values show up from its next beat on).
inline void EDF6COOP_MA_CALL IgnoreMember(void*,const EDF6CoopMemberValue*) {}
inline bool Watch(const Table* t,const char* key) noexcept { return t && key && t->snapshot(key,&IgnoreMember,nullptr,nullptr)!=0; }

// One key of this machine: published when its value changed (EDF6Coop keeps it for every room after), a refusal's
// reason told once (the caller logs it: `refused`).
struct Publisher {
    const char* key=nullptr;
    char published[kValueMost]{};   // the value EDF6Coop last accepted ("" none yet)
    std::uint32_t refusedWhy=~0u;
    enum class Result { same, published, refused, refusedAgain };
    Result Publish(const Table* t,const char* value,std::uint32_t* why=nullptr) noexcept {
        if(!t || !t->publish || !key || !value)return Result::refusedAgain;
        if(std::strcmp(value,published)==0)return Result::same;
        if(t->publish(key,value)) {
            strncpy_s(published,value,_TRUNCATE);
            refusedWhy=~0u;
            return Result::published;
        }
        const std::uint32_t w=EDF6COOP_MA_HAS(t,lastError) && t->lastError ? t->lastError() : 0u;
        if(why)*why=w;
        if(w==refusedWhy)return Result::refusedAgain;
        refusedWhy=w;
        return Result::refused;
    }
    // Another EDF6Coop (or none): nothing it accepted is known any more.
    void Forget() noexcept { published[0]=0;refusedWhy=~0u; }
};

// One member's value of a key.
struct Value {
    bool found=false;   // the member is in the room
    bool has=false;     // and has a value
    bool isHost=false,isSelf=false;
    char id[EDF6COOP_MEMBER_ID_SIZE]{};
    char text[kValueMost]{};
};
namespace detail {
struct Find { const char* id; bool host; Value* out; };
inline void EDF6COOP_MA_CALL Each(void* context,const EDF6CoopMemberValue* m) {
    auto* f=static_cast<Find*>(context);
    if(!m || !m->memberId || f->out->found)return;
    if(f->host ? !m->isHost : (!f->id || std::strcmp(m->memberId,f->id)!=0))return;
    Value& v=*f->out;
    v.found=true;v.isHost=m->isHost!=0;v.isSelf=m->isSelf!=0;
    strncpy_s(v.id,m->memberId,_TRUNCATE);
    v.has=m->hasValue!=0 && m->value;
    if(v.has)strncpy_s(v.text,m->value,_TRUNCATE);
}
}  // namespace detail
// The member `id`'s value; false when the snapshot failed (no table, a bad key).
inline bool MemberValue(const Table* t,const char* key,const char* id,Value& out) noexcept {
    out=Value{};
    if(!t || !id)return false;
    detail::Find f{id,false,&out};
    return t->snapshot(key,&detail::Each,&f,nullptr)!=0;
}
// The host's value (the member EDF6Coop marks isHost; none outside a room).
inline bool HostValue(const Table* t,const char* key,Value& out) noexcept {
    out=Value{};
    if(!t)return false;
    detail::Find f{nullptr,true,&out};
    return t->snapshot(key,&detail::Each,&f,nullptr)!=0;
}
// Mission player `index`'s value: its member resolved by EDF6Coop (EDF6Coop_ResolveMissionPlayerPuid), then read.
// False (`why` says) when it cannot be resolved or read; `out.found` false when the member is not in the room.
inline bool MissionPlayerValue(const Table* t,ResolveFn resolve,const char* key,int index,Value& out,char* why,std::size_t size) noexcept {
    out=Value{};
    if(!t || !resolve){if(why)std::snprintf(why,size,"EDF6Coop's member attributes or mission player ids missing (older EDF6Coop)");return false;}
    EDF6CoopPeer peer{};
    if(!resolve(index,&peer) || !peer.id[0]){if(why)std::snprintf(why,size,"EDF6Coop knows no member for mission player %d",index+1);return false;}
    if(!MemberValue(t,key,peer.id,out)){if(why)std::snprintf(why,size,"the room's members unreadable");return false;}
    return true;
}

// Every member's value passes `ok` (a member without one fails). `why` names the first that does not.
using Test=bool(*)(const char* value,void* context);
namespace detail {
struct Every { Test ok; void* context; std::uint32_t members=0,failing=0; char first[EDF6COOP_MEMBER_ID_SIZE]{}; bool firstHas=false; };
inline void EDF6COOP_MA_CALL Check(void* context,const EDF6CoopMemberValue* m) {
    auto* e=static_cast<Every*>(context);
    if(!m || !m->memberId)return;
    ++e->members;
    const bool has=m->hasValue!=0 && m->value;
    if(has && e->ok(m->value,e->context))return;
    if(!e->failing++){strncpy_s(e->first,m->memberId,_TRUNCATE);e->firstHas=has;}
}
}  // namespace detail
inline bool EveryMember(const Table* t,const char* key,Test ok,void* context,char* why,std::size_t size) noexcept {
    if(!t || !ok){if(why)std::snprintf(why,size,"EDF6Coop's member attributes missing (older EDF6Coop)");return false;}
    detail::Every e{ok,context};
    if(!t->snapshot(key,&detail::Check,&e,nullptr)){if(why)std::snprintf(why,size,"the room's members unreadable");return false;}
    if(!e.failing)return true;
    if(why)std::snprintf(why,size,"%u of %u members (first %s) %s",e.failing,e.members,e.first,
                         e.firstHas ? "say otherwise" : "tell the room nothing of it (an older All Forces)");
    return false;
}
// Every mission player 0..count-1 resolved to its member, and that member's value passes `ok`.
inline bool EveryMissionPlayer(const Table* t,ResolveFn resolve,const char* key,std::uint32_t count,Test ok,void* context,
                               char* why,std::size_t size) noexcept {
    for(std::uint32_t i=0;i<count;++i) {
        Value v;
        if(!MissionPlayerValue(t,resolve,key,static_cast<int>(i),v,why,size))return false;
        if(!v.found){if(why)std::snprintf(why,size,"mission player %u (%s) is not among the room's members",i+1,v.id);return false;}
        if(!v.has || !ok(v.text,context)) {
            if(why)std::snprintf(why,size,"mission player %u (%s) %s",i+1,v.id,
                                 v.has ? "says otherwise" : "tells the room nothing of it (an older All Forces)");
            return false;
        }
    }
    return true;
}

// --- The game-facing half (coop_attr.cpp): EDF6Coop.dll's table and resolver, looked up again when the module changes.
const Table* Api() noexcept;
ResolveFn ResolvePuid() noexcept;
// The room now (inRoom 0 outside one, or without EDF6Coop).
EDF6CoopRoomState Room() noexcept;
}  // namespace crew::coopattr
