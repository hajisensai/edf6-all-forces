// More than four players in an EDF5 / EDF4.1 script mission online: whether EDF6Coop's BvmPlayerTableHooks are in
// place, this machine's word to the room, and whether every mission player's machine gave it (edf5online_room.h).
// The game-facing half of src/edf5online.cpp's count; the member attributes as outfit41.cpp uses them.
#include "edf5online_room.h"
#include "coop_extension_api.h"
#include "coop_member_attribute_api.h"
#include "crew.h"
#include <Windows.h>
#include <cstdio>
#include <cstring>

namespace crew {
namespace {
using namespace bvmroom;

HMODULE Coop() noexcept { return GetModuleHandleW(L"EDF6Coop.dll"); }

// Where a jmp rel32 at `site` lands, when that is committed executable memory.
bool LandsInCode(const unsigned char* site) noexcept {
    std::int32_t rel=0;
    std::memcpy(&rel,site+1,4);
    const unsigned char* to=site+5+rel;
    MEMORY_BASIC_INFORMATION m{};
    if(VirtualQuery(to,&m,sizeof(m))!=sizeof(m) || m.State!=MEM_COMMIT)return false;
    const DWORD p=m.Protect&0xFF;
    return p==PAGE_EXECUTE || p==PAGE_EXECUTE_READ || p==PAGE_EXECUTE_READWRITE || p==PAGE_EXECUTE_WRITECOPY;
}

int hookedLogged=-1;
}  // namespace

// EDF6Coop's own word when it exports it (EDF6Coop_BvmPlayerTableHooks: bit i the i-th of kCoopSites installed);
// otherwise EDF6Coop 2.5.0 or later (its exported EDF6CoopVersion) and each of the four sites jumping out as its
// mid-function hooks do. Asked at each creation: EDF6Coop may load after this plugin.
bool Edf5CoopBvmTableHooked() noexcept {
    const HMODULE coop=Coop();
    char why[160]="";
    bool ok=false;
    if(!coop)std::snprintf(why,sizeof(why),"no EDF6Coop.dll");
    else if(const auto says=reinterpret_cast<std::uint32_t(__cdecl*)()>(GetProcAddress(coop,"EDF6Coop_BvmPlayerTableHooks"))) {
        std::uint32_t sites=0;
        __try { sites=says(); } __except(EXCEPTION_EXECUTE_HANDLER){sites=0;}
        ok=(sites&kAllCoopSites)==kAllCoopSites;
        if(!ok)std::snprintf(why,sizeof(why),"EDF6Coop says sites 0x%X of 0x%X",sites,kAllCoopSites);
    } else {
        const char* marker=reinterpret_cast<const char*>(GetProcAddress(coop,"EDF6CoopVersion"));
        int a=0,b=0,c=0;
        bool parsed=false;
        __try { parsed=ParseCoopVersion(marker,a,b,c); } __except(EXCEPTION_EXECUTE_HANDLER){parsed=false;}
        if(!parsed)std::snprintf(why,sizeof(why),"EDF6Coop's version unreadable (older than 2.5.0)");
        else if(!AtLeast(a,b,c,2,5,0))std::snprintf(why,sizeof(why),"EDF6Coop %d.%d.%d is older than 2.5.0",a,b,c);
        else {
            ok=true;
            for(const CoopSite& s:kCoopSites) {
                bool site=false;
                __try { site=CoopJumpShape(image+s.rva,s.size) && LandsInCode(image+s.rva); } __except(EXCEPTION_EXECUTE_HANDLER){site=false;}
                if(!site){ok=false;std::snprintf(why,sizeof(why),"EDF6Coop %d.%d.%d has not hooked EDF+0x%X ([Mission] Extend=0?)",a,b,c,s.rva);break;}
            }
        }
    }
    if(hookedLogged!=(ok ? 1 : 0)) {
        hookedLogged=ok ? 1 : 0;
        if(ok)Log("EDF5 online: EDF6Coop's BvmPlayerTableHooks in place (EDF+0x21F3CF/0x222761/0x228AED/0x22A685)");
        else Log("EDF5 online: EDF6Coop's BvmPlayerTableHooks not in place (%s): EDF5/4.1 missions create four players",why);
    }
    return ok;
}

namespace {
const EDF6CoopMemberAttributeApi* attrApi=nullptr;
EDF6CoopResolveMissionPlayerPuidFn resolvePuid=nullptr;
bool looked=false;
void LookCoop() noexcept {
    if(looked)return;
    const HMODULE coop=Coop();
    if(!coop)return;   // looked again later: EDF6Coop may load after us
    looked=true;
    const auto get=reinterpret_cast<EDF6CoopGetMemberAttributeApiFn>(GetProcAddress(coop,"EDF6CoopGetMemberAttributeApi"));
    const EDF6CoopMemberAttributeApi* table=nullptr;
    __try { table=get ? get(EDF6COOP_MEMBER_ATTRIBUTE_VERSION) : nullptr; } __except(EXCEPTION_EXECUTE_HANDLER){table=nullptr;}
    if(table && (table->version!=EDF6COOP_MEMBER_ATTRIBUTE_VERSION || !EDF6COOP_MA_HAS(table,snapshot)))table=nullptr;
    attrApi=table;
    resolvePuid=reinterpret_cast<EDF6CoopResolveMissionPlayerPuidFn>(GetProcAddress(coop,"EDF6Coop_ResolveMissionPlayerPuid"));
    Log("EDF5 online: EDF6Coop member attributes %s, mission player ids %s",attrApi ? "found" : "missing (older EDF6Coop)",
        resolvePuid ? "found" : "missing (older EDF6Coop)");
}

bool published=false;
ULONGLONG lastTick=0;
void EDF6COOP_MA_CALL Ignore(void*,const EDF6CoopMemberValue*) {}

// The capable members' ids, from one snapshot.
constexpr std::uint32_t kMost=EDF6COOP_EXTENSION_MAX_PEERS;
struct Members {
    std::uint32_t count=0;
    char id[kMost][EDF6COOP_MEMBER_ID_SIZE];
    char lacking[EDF6COOP_MEMBER_ID_SIZE]{};   // one member without the word, for the log
};
Members members;
void EDF6COOP_MA_CALL Collect(void* context,const EDF6CoopMemberValue* m) {
    auto* s=static_cast<Members*>(context);
    if(!m || !m->memberId)return;
    if(!(m->hasValue && Capable(m->value))) {
        if(!s->lacking[0])strncpy_s(s->lacking,m->memberId,_TRUNCATE);
        return;
    }
    if(s->count<kMost)strncpy_s(s->id[s->count++],m->memberId,_TRUNCATE);
}
bool Among(const Members& s,const char* id) noexcept {
    for(std::uint32_t i=0;i<s.count;++i)if(std::strcmp(s.id[i],id)==0)return true;
    return false;
}
}  // namespace

// This machine's word told once it can create them (the attribute stays published in every room after), the others'
// watched (EDF6Coop reads a key from its first snapshot on). The room's and the HQ's frame, at most every 0.5 s.
void Edf5OnlineRoomTick() noexcept {
    const ULONGLONG now=GetTickCount64();
    if(lastTick && now-lastTick<500)return;
    lastTick=now;
    LookCoop();
    if(!attrApi)return;
    __try { attrApi->snapshot(kKey,&Ignore,nullptr,nullptr); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if(published || !Edf5OnlineReady() || !resolvePuid || !Edf5CoopBvmTableHooked())return;
    std::uint32_t ok=0;
    __try { ok=attrApi->publish(kKey,kValue); } __except(EXCEPTION_EXECUTE_HANDLER){ok=0;}
    if(ok){published=true;Log("EDF5 online: told the room %s=%s (EDF5/4.1 missions create every player here)",kKey,kValue);}
}

// Every mission player 0..count-1 resolved to its member, and every one of them told the room kKey = kValue in this
// machine's copy of the lobby (itself included: what the others read of it).
bool Edf5RoomAllowsBvmPlayers(std::uint32_t count,char* why,std::size_t size) noexcept {
    LookCoop();
    if(!attrApi || !resolvePuid) {
        std::snprintf(why,size,"EDF6Coop's member attributes or mission player ids missing (older EDF6Coop)");
        return false;
    }
    members.count=0;members.lacking[0]=0;
    std::uint32_t ok=0;
    __try { ok=attrApi->snapshot(kKey,&Collect,&members,nullptr); } __except(EXCEPTION_EXECUTE_HANDLER){ok=0;}
    if(!ok){std::snprintf(why,size,"the room's members unreadable");return false;}
    for(std::uint32_t i=0;i<count;++i) {
        EDF6CoopPeer peer{};
        std::uint32_t got=0;
        __try { got=resolvePuid(static_cast<std::int32_t>(i),&peer); } __except(EXCEPTION_EXECUTE_HANDLER){got=0;}
        if(!got || !peer.id[0]){std::snprintf(why,size,"EDF6Coop knows no member for mission player %u",i+1);return false;}
        if(!Among(members,peer.id)) {
            std::snprintf(why,size,"mission player %u (%s) did not tell the room %s=%s (an older All Forces, or EDF6Coop's "
                          "BvmPlayerTableHooks missing there)",i+1,peer.id,kKey,kValue);
            return false;
        }
    }
    return true;
}
}  // namespace crew
