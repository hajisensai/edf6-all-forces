#include "mission_participants.h"
#include "crew.h"
#include "memory.h"

namespace crew {
namespace mission_participants {
constexpr unsigned kEnumAllTeams=0x5E0C80,kTeamManager=0x20B2978,kGameStatus=0x20B2890;
constexpr unsigned kMaxActors=1024;
constexpr unsigned char kEnumSignature[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83};
using WalkFn=void(__fastcall*)(void*,void*);
struct Visitor {
    const void* const* vtable;
    void** puids;
    unsigned capacity,count;
    bool failed;
    const void* actors[kMaxActors];
    unsigned indices[kMaxActors];
    const ObjRef* expectedActors=nullptr;
    unsigned expectedCount=0;
};
void __fastcall Ignore(void*) noexcept {}
void __fastcall Visit(Visitor* v,const unsigned char* object) noexcept {
    if(v->failed || !object)return;
    __try {
        if(!IsSoldierClass(object))return;
        if(!Readable(object,0x1EE0)){v->failed=true;return;}
        if(At<unsigned char>(object,0x18)&4)return; // deleted from scene, not the +2E8 death/revival state
        const auto user=At<const unsigned char*>(object,0x1ED0);
        if(!user)return; // ordinary friendly/enemy NPC Soldier
        const auto ctrl=At<const unsigned char*>(object,0x1ED8);
        if(!Readable(ctrl,16) || At<LONG>(ctrl,8)<=0 || !Readable(user,0x4C)){v->failed=true;return;}
        const int index=At<int>(user,0x48);
        if(index<0)return; // lobby user has not received a mission slot
        void* puid=At<void*>(user,0x18);
        if(!puid || static_cast<unsigned>(index)>=kMaxActors){v->failed=true;return;}
        if(v->expectedActors && (static_cast<unsigned>(index)>=v->expectedCount ||
            v->expectedActors[index].obj!=object || v->expectedActors[index].ctrl!=At<const void*>(object,kSelfCtrl))) {
            v->failed=true;return;
        }
        for(unsigned i=0;i<v->count;++i) {
            if(v->actors[i]==object)return; // avoid duplicate visitation, not duplicate PUIDs
            if(v->indices[i]==static_cast<unsigned>(index)){v->failed=true;return;}
        }
        if(v->count>=v->capacity){v->failed=true;return;}
        v->actors[v->count]=object;v->indices[v->count]=static_cast<unsigned>(index);
        v->puids[v->count++]=puid;
    } __except(EXCEPTION_EXECUTE_HANDLER){v->failed=true;}
}
const void* const kVisitorVtable[]={reinterpret_cast<const void*>(&Ignore),reinterpret_cast<const void*>(&Visit)};
bool IndicesInRange(const Visitor& visitor,unsigned expected) noexcept {
    for(unsigned i=0;i<visitor.count;++i)if(visitor.indices[i]>=expected)return false;
    return true;
}
}

static bool ReadMissionParticipantsChecked(void** puids,unsigned capacity,unsigned* count,unsigned* expectedPlayers,
                                          const ObjRef* created=nullptr,unsigned createdCount=0) noexcept {
    using namespace mission_participants;
    if(count)*count=0;
    if(expectedPlayers)*expectedPlayers=0;
    if(!puids || !count || !expectedPlayers || !capacity || capacity>kMaxActors)return false;
    __try {
        if(!Matches(kEnumAllTeams,kEnumSignature,sizeof(kEnumSignature)))return false;
        void* mgr=At<void*>(image,kTeamManager);
        const auto status=At<const unsigned char*>(image,kGameStatus);
        if(!Readable(mgr,0x50) || !Readable(status,0x14FFC))return false;
        const unsigned expected=At<unsigned>(status,0x14FF8);
        if(!expected || expected>kMaxActors || !Readable(At<void*>(mgr,0x38),7*0x38))return false;
        Visitor visitor{kVisitorVtable,puids,capacity,0,false,{},{},created,createdCount};
        reinterpret_cast<WalkFn>(image+kEnumAllTeams)(mgr,&visitor);
        // Snapshot invalidated by scene replacement or a mission-count change during native traversal.
        if(visitor.failed || !IndicesInRange(visitor,expected) || At<void*>(image,kTeamManager)!=mgr || At<const void*>(image,kGameStatus)!=status ||
           At<unsigned>(status,0x14FF8)!=expected)return false;
        *count=visitor.count;*expectedPlayers=expected;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
bool ReadMissionParticipants(void** puids,unsigned capacity,unsigned* count,unsigned* expectedPlayers) noexcept {
    return ReadMissionParticipantsChecked(puids,capacity,count,expectedPlayers);
}
bool MissionParticipantCreationsMatch(const ObjRef* created,unsigned expected) noexcept {
    using namespace mission_participants;
    if(!created || !expected || expected>kMaxActors || !Readable(created,expected*sizeof(ObjRef)))return false;
    void* puids[kMaxActors]{};
    unsigned count=0,nativeExpected=0;
    return ReadMissionParticipantsChecked(puids,expected,&count,&nativeExpected,created,expected) &&
        count==expected && nativeExpected==expected;
}
}
