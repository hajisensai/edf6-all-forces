// More than four players in an EDF5 / EDF4.1 script mission online: whether EDF6Coop's BvmPlayerTableHooks are in
// place, this machine's word to the room, and whether every mission player's machine gave it (edf5online_room.h).
// The game-facing half of src/edf5online.cpp's count; the member attributes through coop_attr.h.
#include "edf5online_room.h"
#include "coop_attr.h"
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
coopattr::Publisher publisher{kKey};
coopattr::Beat beat;
bool Says(const char* value,void*) { return Capable(value); }
}  // namespace

// This machine's word told once it can create them (the attribute stays published in every room after), the others'
// watched (EDF6Coop reads a key from its first snapshot on). The room's and the HQ's frame, at most every 0.5 s.
void Edf5OnlineRoomTick() noexcept {
    if(!beat.Due(GetTickCount64()))return;
    const coopattr::Table* t=coopattr::Api();
    if(!t){publisher.Forget();return;}
    __try { coopattr::Watch(t,kKey); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if(!Edf5OnlineReady() || !coopattr::ResolvePuid() || !Edf5CoopBvmTableHooked())return;
    std::uint32_t why=0;
    coopattr::Publisher::Result r=coopattr::Publisher::Result::refusedAgain;
    __try { r=publisher.Publish(t,kValue,&why); } __except(EXCEPTION_EXECUTE_HANDLER){r=coopattr::Publisher::Result::refusedAgain;}
    if(r==coopattr::Publisher::Result::published)Log("EDF5 online: told the room %s=%s (EDF5/4.1 missions create every player here)",kKey,kValue);
    else if(r==coopattr::Publisher::Result::refused)Log("EDF5 online: EDF6Coop refused %s=%s (lastError %u): tried again",kKey,kValue,why);
}

// Every mission player 0..count-1 resolved to its member, and every one of them told the room kKey = kValue in this
// machine's copy of the lobby (itself included: what the others read of it).
bool Edf5RoomAllowsBvmPlayers(std::uint32_t count,char* why,std::size_t size) noexcept {
    bool ok=false;
    __try {
        ok=coopattr::EveryMissionPlayer(coopattr::Api(),coopattr::ResolvePuid(),kKey,count,&Says,nullptr,why,size);
    } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;std::snprintf(why,size,"the room's members unreadable");}
    if(!ok && why && std::strstr(why,"says otherwise"))std::snprintf(why+std::strlen(why),size-std::strlen(why),
        " (%s is not %s)",kKey,kValue);
    else if(!ok && why && std::strstr(why,"nothing of it"))std::snprintf(why+std::strlen(why),size-std::strlen(why),
        " (%s=%s: an older All Forces, or EDF6Coop's BvmPlayerTableHooks missing there)",kKey,kValue);
    return ok;
}
}  // namespace crew
