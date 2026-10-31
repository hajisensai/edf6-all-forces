// Production ownership bridge with fake native identities and Coop's exported
// slot-to-PUID resolver. No game, network, native object memory or DLL is used.
#include <Windows.h>
namespace {
HMODULE FixtureGetModuleHandleW(LPCWSTR name) noexcept;
FARPROC FixtureGetProcAddress(HMODULE module,LPCSTR name) noexcept;
}
#define GetModuleHandleW FixtureGetModuleHandleW
#define GetProcAddress FixtureGetProcAddress
#include "../src/pvp_support.cpp"
#undef GetProcAddress
#undef GetModuleHandleW
#include <cstdio>
#include <cwchar>

namespace {
unsigned char actor[crew::support_net::kMaxUnits]{},control[crew::support_net::kMaxUnits]{},nativePuid[2]{};
constexpr char kOwner[]="nativeowner001";
struct Fixture {
    bool strictOk=true,markOk=true,modulePresent=true,exportPresent=true,resolveOk=true,sealed=true,nativeOk=true;
    crew::ObjRef commandPlayer{actor,control},markPlayer{actor,control};
    void* commandPuid=nativePuid;
    void* markPuid=nativePuid;
    int markSlot=3,expectedSlot=3,team=0;
    char resolvedText[65]="nativeowner001";
    unsigned strictCalls=0,markCalls=0,resolveCalls=0,sealCalls=0,teamCalls=0,nativeCalls=0;
    unsigned markFailAt=0,teamRejectAt=0;
    int resolvedSlot=-1,attachedTeam=-1;
    unsigned attachedCount=0;
    const crew::ObjRef* attachedObjects=nullptr;
    crew::ObjRef attachedRefs[crew::support_net::kMaxUnits]{};
    char attachedPuid[65]{};
    bool correctCommandArgs=true,correctMarkArgs=true,correctSealArgs=true,correctLoaderArgs=true;
    bool cachedOwner=false,readable=true,soldier=true,actualPlayer=true,nativeIdOk=true;
    crew::ObjRef expectedParent{},driverVehicle{},driverPlayer{};
    bool driverOk=false,session=true;
    unsigned driverCalls=0;
    char cachedText[65]="nativeowner001";
    unsigned char parentId[32]{};
    unsigned ownerCalls=0,soldierCalls=0,playerCalls=0,objectIdCalls=0;
    bool correctOwnerArgs=true,correctObjectIdArgs=true;
    unsigned char expectedId[32]{};
};
Fixture fixture{};
unsigned checks=0,failures=0;
void Check(bool passed,const char* expression,int line) {
    ++checks;
    if(!passed){++failures;std::fprintf(stderr,"line %d: %s\n",line,expression);}
}
#define CHECK(value) Check((value),#value,__LINE__)

crew::support_net::Plan Reset(bool owned=false,unsigned count=1) noexcept {
    fixture=Fixture{};
    crew::support_net::Plan plan{};plan.count=count;
    if(owned) {
        crew::MakePlayerRequester(3,plan.requester);   // the requester's mission slot (player_requester.h)
        std::memcpy(fixture.expectedId,plan.requester,sizeof(plan.requester));
    }
    return plan;
}
std::uint32_t EDF6COOP_CALL FixtureResolve(std::int32_t slot,EDF6CoopPeer* out) noexcept {
    ++fixture.resolveCalls;fixture.resolvedSlot=slot;
    if(!out || !fixture.resolveOk || slot!=fixture.expectedSlot)return 0;
    std::memcpy(out->id,fixture.resolvedText,sizeof(out->id));return 1;
}
HMODULE FixtureGetModuleHandleW(LPCWSTR name) noexcept {
    fixture.correctLoaderArgs=fixture.correctLoaderArgs && name && !std::wcscmp(name,L"EDF6Coop.dll");
    return fixture.modulePresent ? reinterpret_cast<HMODULE>(&fixture) : nullptr;
}
FARPROC FixtureGetProcAddress(HMODULE module,LPCSTR name) noexcept {
    fixture.correctLoaderArgs=fixture.correctLoaderArgs && module==reinterpret_cast<HMODULE>(&fixture) && name &&
        !std::strcmp(name,"EDF6Coop_ResolveMissionPlayerPuid");
    return fixture.exportPresent ? reinterpret_cast<FARPROC>(&FixtureResolve) : nullptr;
}
}

namespace edf {
bool Readable(const void* pointer,std::size_t size,bool) noexcept {
    return fixture.readable && pointer && size && reinterpret_cast<std::uintptr_t>(pointer)>=0x10000;
}
bool IsAnyPlayer(const unsigned char* human) noexcept {
    ++fixture.playerCalls;
    return fixture.actualPlayer && human==fixture.expectedParent.obj;
}
}

namespace crew {
bool InSession() noexcept {return fixture.session;}
bool IsSoldierClass(const void* human) noexcept {
    ++fixture.soldierCalls;
    return fixture.soldier && human==fixture.expectedParent.obj;
}
bool ReadNativeObjectId(const unsigned char* object,unsigned char* out32) noexcept {
    ++fixture.objectIdCalls;
    fixture.correctObjectIdArgs=fixture.correctObjectIdArgs && object==fixture.expectedParent.obj && out32;
    if(!fixture.nativeIdOk || object!=fixture.expectedParent.obj || !out32)return false;
    std::memcpy(out32,fixture.parentId,32);return true;
}
bool PvpNativeSupportDriver(const ObjRef& parent,ObjRef* out) noexcept {
    ++fixture.driverCalls;if(!out)return false;*out={};
    if(!fixture.driverOk || parent.obj!=fixture.driverVehicle.obj || parent.ctrl!=fixture.driverVehicle.ctrl)return false;
    *out=fixture.driverPlayer;return true;
}
bool PvpNativeSupportOwner(const ObjRef& parent,char* puid,unsigned capacity) noexcept {
    ++fixture.ownerCalls;
    fixture.correctOwnerArgs=fixture.correctOwnerArgs && puid && capacity>=sizeof(fixture.cachedText);
    if(!fixture.cachedOwner || parent.obj!=fixture.expectedParent.obj || parent.ctrl!=fixture.expectedParent.ctrl ||
       !parent.obj || !parent.Is(parent.obj) || !puid || capacity<sizeof(fixture.cachedText))return false;
    std::memcpy(puid,fixture.cachedText,sizeof(fixture.cachedText));return true;
}
bool ResolveCommandIdentities(const unsigned char requester[32],const unsigned char units[][32],
    unsigned count,const unsigned char focus[32],CommandIdentities* out) noexcept {
    ++fixture.strictCalls;
    fixture.correctCommandArgs=fixture.correctCommandArgs && requester && !std::memcmp(requester,fixture.expectedId,32) &&
        !units && !count && !focus && out;
    if(!out)return false;
    *out={};
    if(!fixture.strictOk)return false;
    out->requester=fixture.commandPlayer;out->requesterPuid=fixture.commandPuid;return true;
}
bool ResolveMarkIdentities(const unsigned char marker[32],const unsigned char target[32],MarkIdentities* out) noexcept {
    ++fixture.markCalls;
    fixture.correctMarkArgs=fixture.correctMarkArgs && marker && !std::memcmp(marker,fixture.expectedId,32) && !target && out;
    if(!out)return false;
    *out={};
    if(!fixture.markOk || (fixture.markFailAt && fixture.markCalls==fixture.markFailAt))return false;
    out->player=fixture.markPlayer;out->puid=fixture.markPuid;out->slot=fixture.markSlot;return true;
}
// The player's requester identity (player_requester.h), as the fixture's parent ID says it is.
bool PlayerRequesterOf(const ObjRef& who,unsigned char out[32]) noexcept {
    ++fixture.objectIdCalls;
    fixture.correctObjectIdArgs=fixture.correctObjectIdArgs && who.obj==fixture.expectedParent.obj && out;
    std::memset(out,0,32);
    if(!fixture.nativeIdOk || who.obj!=fixture.expectedParent.obj)return false;
    std::memcpy(out,fixture.parentId,32);return true;
}
bool SupportCommandRequesterMatches(void* puid,const char* text) noexcept {
    ++fixture.sealCalls;
    fixture.correctSealArgs=fixture.correctSealArgs && puid==fixture.markPuid;
    return fixture.sealed && puid==nativePuid && text && !std::strcmp(text,kOwner);
}
int PvpSupportTeam(const char* puid) noexcept {
    ++fixture.teamCalls;
    if(fixture.teamRejectAt && fixture.teamCalls>=fixture.teamRejectAt)return -1;
    if(fixture.team==0)return 0;
    if(fixture.team<0 || !puid || std::strcmp(puid,kOwner))return -1;
    return fixture.team;
}
bool PvpNativeAttachSupport(const char* puid,const ObjRef* objects,unsigned count,int team) noexcept {
    ++fixture.nativeCalls;fixture.attachedObjects=objects;fixture.attachedCount=count;fixture.attachedTeam=team;
    if(puid && std::memchr(puid,0,sizeof(fixture.attachedPuid)))
        std::memcpy(fixture.attachedPuid,puid,std::strlen(puid)+1);
    if(count>support_net::kMaxUnits)return false;
    for(unsigned i=0;i<count;++i)fixture.attachedRefs[i]=objects[i];
    return fixture.nativeOk;
}
}

int main() {
    using crew::PvpAttachSupportPlan;
    using crew::PvpValidateSupportPlan;
    crew::ObjRef objects[crew::support_net::kMaxUnits]{};
    for(unsigned i=0;i<crew::support_net::kMaxUnits;++i)objects[i]={actor+i,control+i};

    auto plan=Reset();
    CHECK(PvpValidateSupportPlan(plan));
    CHECK(PvpAttachSupportPlan(plan,objects,1));
    CHECK(fixture.strictCalls==0 && fixture.nativeCalls==0);
    plan=Reset();fixture.team=1;CHECK(!PvpValidateSupportPlan(plan));
    plan=Reset();fixture.team=2;CHECK(!PvpValidateSupportPlan(plan));
    plan=Reset();fixture.team=-1;CHECK(!PvpValidateSupportPlan(plan));
    CHECK(!PvpAttachSupportPlan(plan,objects,1));CHECK(fixture.nativeCalls==0);
    plan=Reset();plan.team=1;CHECK(!PvpValidateSupportPlan(plan));
    plan=Reset();plan.team=2;CHECK(!PvpValidateSupportPlan(plan));
    plan=Reset();plan.team=3;CHECK(!PvpValidateSupportPlan(plan));
    plan=Reset();plan.count=0;CHECK(!PvpValidateSupportPlan(plan));
    plan=Reset();plan.count=crew::support_net::kMaxUnits+1;CHECK(!PvpValidateSupportPlan(plan));

    // Invalid native padding is refused before either native resolver is called.
    for(unsigned i=20;i<24;++i) {
        plan=Reset(true);plan.requester[i]=1;CHECK(!PvpValidateSupportPlan(plan));
        CHECK(fixture.strictCalls==0 && fixture.markCalls==0);
    }
    // The requester is the player's mission slot, never a NetId: a stock mission numbers the players' NetIds per machine,
    // so a guest found no host player by the host's and refused every plan it asked for (2026-10-31).
    plan=Reset(true);plan.requester[0]=0x41;plan.requester[1]=plan.requester[2]=plan.requester[3]=0;plan.requester[12]=5;
    CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.markCalls==0 && fixture.strictCalls==0);
    plan=Reset(true);fixture.strictOk=false;CHECK(PvpValidateSupportPlan(plan));CHECK(fixture.strictCalls==0);
    plan=Reset(true);fixture.markOk=false;CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.resolveCalls==0);
    plan=Reset(true);fixture.markPlayer={};CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.resolveCalls==0);
    plan=Reset(true);fixture.markPuid=nullptr;CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.resolveCalls==0);
    plan=Reset(true);fixture.markPuid=nativePuid+1;CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.sealCalls==1);
    plan=Reset(true);fixture.markSlot=-1;CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.resolveCalls==0);
    plan=Reset(true);fixture.markSlot=4;CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.resolveCalls==0);

    plan=Reset(true);fixture.modulePresent=false;CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.resolveCalls==0);
    plan=Reset(true);fixture.exportPresent=false;CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.resolveCalls==0);
    plan=Reset(true);fixture.resolveOk=false;CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.sealCalls==0);
    plan=Reset(true);fixture.resolvedText[0]=0;CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.sealCalls==0);
    plan=Reset(true);std::memset(fixture.resolvedText,'x',sizeof(fixture.resolvedText));
    CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.sealCalls==0);
    plan=Reset(true);fixture.resolvedText[2]='\n';CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.sealCalls==0);
    plan=Reset(true);std::memcpy(fixture.resolvedText,"wrongowner",11);CHECK(!PvpValidateSupportPlan(plan));
    CHECK(fixture.sealCalls==1);
    plan=Reset(true);fixture.sealed=false;CHECK(!PvpValidateSupportPlan(plan));CHECK(fixture.sealCalls==1);

    plan=Reset(true);fixture.team=-1;CHECK(!PvpValidateSupportPlan(plan));
    plan=Reset(true);fixture.team=1;plan.team=2;CHECK(!PvpValidateSupportPlan(plan));
    plan=Reset(true);fixture.team=2;plan.team=1;CHECK(!PvpValidateSupportPlan(plan));
    plan=Reset(true);fixture.team=1;CHECK(!PvpValidateSupportPlan(plan));
    plan=Reset(true);fixture.team=2;plan.team=2;CHECK(PvpValidateSupportPlan(plan));
    CHECK(fixture.resolvedSlot==3 && fixture.correctCommandArgs && fixture.correctMarkArgs &&
        fixture.correctLoaderArgs && fixture.correctSealArgs);
    plan=Reset(true);fixture.team=1;plan.team=1;CHECK(PvpAttachSupportPlan(plan,objects,1));
    CHECK(fixture.nativeCalls==1 && fixture.attachedTeam==1 && fixture.attachedCount==1);
    CHECK(!std::strcmp(fixture.attachedPuid,kOwner));

    // Owned stock-mode support is retained for a later mode enable, even though
    // its current plan's team is zero. The native bridge receives every object.
    plan=Reset(true,crew::support_net::kMaxUnits);
    CHECK(PvpAttachSupportPlan(plan,objects,crew::support_net::kMaxUnits));
    CHECK(fixture.nativeCalls==1 && fixture.attachedTeam==0 &&
        fixture.attachedCount==crew::support_net::kMaxUnits && fixture.attachedObjects==objects);
    CHECK(fixture.strictCalls==0 && fixture.markCalls==2 && fixture.resolveCalls==2 && fixture.sealCalls==2);
    for(unsigned i=0;i<crew::support_net::kMaxUnits;++i)
        CHECK(fixture.attachedRefs[i].obj==actor+i && fixture.attachedRefs[i].ctrl==control+i);
    CHECK(fixture.correctCommandArgs && fixture.correctMarkArgs && fixture.correctLoaderArgs && fixture.correctSealArgs);

    plan=Reset(true,crew::support_net::kMaxUnits);
    CHECK(!PvpAttachSupportPlan(plan,objects,crew::support_net::kMaxUnits-1));
    CHECK(!PvpAttachSupportPlan(plan,objects,crew::support_net::kMaxUnits+1));
    CHECK(!PvpAttachSupportPlan(plan,nullptr,crew::support_net::kMaxUnits));
    CHECK(fixture.markCalls==0 && fixture.nativeCalls==0);
    plan=Reset();CHECK(!PvpAttachSupportPlan(plan,objects,0));CHECK(fixture.nativeCalls==0);
    plan=Reset();CHECK(!PvpAttachSupportPlan(plan,objects,2));CHECK(fixture.nativeCalls==0);
    plan=Reset(true);fixture.nativeOk=false;CHECK(!PvpAttachSupportPlan(plan,objects,1));CHECK(fixture.nativeCalls==1);

    // A valid planning/validation identity cannot authorize a changed player or
    // a mode transaction that begins before the final native attachment.
    plan=Reset(true);fixture.markFailAt=2;CHECK(!PvpAttachSupportPlan(plan,objects,1));
    CHECK(fixture.markCalls==2 && fixture.nativeCalls==0);
    plan=Reset(true);fixture.teamRejectAt=2;CHECK(!PvpAttachSupportPlan(plan,objects,1));
    CHECK(fixture.teamCalls==2 && fixture.nativeCalls==0);

    if(failures)return 1;
    std::printf("PVP support fixture: %u checks passed\n",checks);return 0;
}
