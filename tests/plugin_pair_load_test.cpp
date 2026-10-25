// The two built plugins, EDF6VehicleCrew.dll and EDF6AutoTurret.dll, loaded for real into one process, in the order
// given (crew-first / turret-first: EDFModLoader loads by file name, but either order must work, common/edf/aimlink.h).
// Copies in a scratch directory of the test's own are loaded (EML6_Load writes its .log beside the DLL). Checked:
//  - each DLL exports exactly its list below (every extern "C" __declspec(dllexport) of src/ and autoturret/src/), each
//    one found by GetProcAddress and lying in that DLL (an export forwarded or resolved into the other is wrong);
//  - every aimlink.h name its owner exports, and aimlink::Resolve finds the peer's names in both orders: nothing while
//    the peer is not loaded, nothing again within the second (its throttle), the export once the peer is there;
//  - with no EDF.dll in the process both EML6_Load return false, before touching any game memory, and each log says
//    "<plugin> <version> loading (build <id>, <bytes> bytes, written ...)" for the very copy loaded, then "REFUSED".
// Only EML6_Load is called: before the image check it reads its ini (absent here: the defaults) and logs, nothing else
// (src/plugin.cpp, autoturret/src/plugin.cpp). The other exports read game objects or global state the game sets up,
// so they are looked up, never called.
#include <Windows.h>
#pragma warning(push)
#pragma warning(disable:4201)
#include "PluginAPI.h"
#pragma warning(pop)
#include "edf/aimlink.h"
#include "plugin_load_util.h"
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace {
using plugintest::Check;
namespace aimlink=edf::aimlink;

const char* const kCrewExports[]={
    "EML6_Load",
    "EDF6VehicleCrew_SetInputSourceV1","EDF6VehicleCrew_SetInputSourceV2",      // src/plugin.cpp
    "EDF6VehicleCrew_InputHeldV1",                                              // src/map.cpp
    "EDF6AF_DecodeRoomType","EDF6AF_RoomPackCompatible","EDF6AF_RoomIsolationReady",   // src/mod_room.cpp
    "EDF6AF_RoomPrefix","EDF6AF_RoomProfile",   // src/mod_room.cpp: the room namespace for EDF6Coop
    "EDF6AF_PvpMemberLabel",   // src/room_pvp.cpp: a member row's faction for EDF6Coop
    "EDF6VehicleCrew_PriorityZoneV1",                                           // src/proteus.cpp
    "EDF6VehicleCrew_StabilizerV3",                                             // src/stab.cpp
    "EDF6AF_SupportProtocolVersion","EDF6AF_GetMissionParticipants","EDF6AF_AllowMissionPlayer",
    "EDF6AF_MissionPlayerCreated","EDF6AF_GetMissionAdmissionState",           // src/support_net.cpp
    "EDF6VehicleCrew_ViewRayV1","EDF6VehicleCrew_CameraTurretV2","EDF6VehicleCrew_AimsTurretV4",
    "EDF6VehicleCrew_MapRayV1","EDF6VehicleCrew_TurretObserverV1",
    "EDF6VehicleCrew_PayloadAimV1",                                             // src/turretaim.cpp
};
const char* const kTurretExports[]={
    "EML6_Load","EDF6AutoTurret_SetInputSourceV1","EDF6AutoTurret_SteersV2","EDF6AutoTurret_StabilizerAwareV3",   // plugin.cpp
    "EDF6AutoTurret_TurretReadoutV1","EDF6AutoTurret_PlayerAimV4","EDF6AutoTurret_TurretLockV1",   // designate.cpp
};

// The aimlink.h contract: each name, and the DLL that exports it.
struct Link { const char* name; bool crew; };
const Link kLinks[]={
    {aimlink::kViewRay,true},{aimlink::kMapRay,true},{aimlink::kCameraTurret,true},
    {aimlink::kTurretObserver,true},{aimlink::kStabilizer,true},{aimlink::kPriorityZone,true},{aimlink::kInputHeld,true},
    {aimlink::kAimsTurret,true},{aimlink::kPayloadAim,true},
    {aimlink::kTurretReadout,false},{aimlink::kSteers,false},{aimlink::kStabilizerAware,false},
    {aimlink::kPlayerAim,false},
};
using AnyFn=void(__cdecl*)();
struct Resolver { AnyFn cache=nullptr; ULONGLONG triedAt=0; };
AnyFn Resolve(const Link& l,Resolver& r) {
    return aimlink::Resolve(l.crew ? aimlink::kCrewDll : aimlink::kTurretDll,l.name,r.cache,r.triedAt);
}

using LoadFn=bool(EDFMLAPI*)(PluginInfo*);

struct Plugin {
    const char* label;
    const wchar_t* fileName;
    const char* const* exports;
    std::size_t exportCount;
    const char* loadingPrefix;   // the load line up to the version
    std::filesystem::path copy;
    HMODULE module=nullptr;
};

void CheckExports(const Plugin& p,HMODULE other) {
    const std::set<std::string> have=plugintest::ExportNames(p.module);
    std::set<std::string> want(p.exports,p.exports+p.exportCount);
    for(const auto& n:have)if(!want.count(n))std::fprintf(stderr,"%s exports %s, which this test does not know: add it to the list\n",p.label,n.c_str());
    for(const auto& n:want)if(!have.count(n))std::fprintf(stderr,"%s does not export %s\n",p.label,n.c_str());
    Check(have==want,"each plugin exports exactly its list");
    for(const auto& n:want) {
        const auto f=reinterpret_cast<const void*>(GetProcAddress(p.module,n.c_str()));
        if(!f || plugintest::OwnerOf(f)!=p.module)std::fprintf(stderr,"%s!%s at %p, in module %p\n",p.label,n.c_str(),f,
                                                                static_cast<void*>(plugintest::OwnerOf(f)));
        Check(f!=nullptr,"the export is found");
        Check(plugintest::OwnerOf(f)==p.module,"the export lies in its own DLL");
        if(other && n!="EML6_Load")Check(GetProcAddress(other,n.c_str())==nullptr,"the other DLL does not export the same name");
    }
}

std::uint64_t FileSize(const std::filesystem::path& p) {
    std::error_code ec;
    const auto n=std::filesystem::file_size(p,ec);
    Check(!ec,"the copy's size");
    return n;
}

void CheckRefused(Plugin& p) {
    const auto load=reinterpret_cast<LoadFn>(reinterpret_cast<void*>(GetProcAddress(p.module,"EML6_Load")));
    Check(load!=nullptr,"EML6_Load found");
    PluginInfo info{};
    Check(!load(&info),"EML6_Load returns false with no EDF.dll in the process");
    Check(info.infoVersion==PluginInfo::MaxInfoVer && info.name && info.name[0],"PluginInfo filled in before the refusal");
    Check(info.version.raw!=0,"PluginInfo has the plugin's version");
    std::filesystem::path log=p.copy;
    log.replace_extension(L".log");
    const std::string text=plugintest::WaitForLog(log,"REFUSED: unsupported EDF.dll");
    if(plugintest::Count(text,"REFUSED: unsupported EDF.dll")!=1)std::fprintf(stderr,"%s log:\n%s\n",p.label,text.c_str());
    Check(plugintest::Count(text,"REFUSED: unsupported EDF.dll")==1,"the log says REFUSED once");
    const std::size_t at=text.find(p.loadingPrefix);
    if(at==std::string::npos)std::fprintf(stderr,"%s log:\n%s\n",p.label,text.c_str());
    Check(at!=std::string::npos,"the log has the load line");
    const std::string line=text.substr(at,text.find('\n',at)-at);
    std::printf("%s: %s\n",p.label,line.c_str());
    const std::size_t build=line.find(" loading (build ");
    Check(build!=std::string::npos,"the load line names the build: \"loading (build \"");
    const std::size_t idAt=build+std::strlen(" loading (build "),comma=line.find(", ",idAt);
    Check(comma!=std::string::npos && comma>idAt,"the build id is not empty");
    const std::string file=std::to_string(FileSize(p.copy))+" bytes, written ";
    Check(line.compare(comma+2,file.size(),file)==0,"the load line describes the very DLL file loaded (its size)");
    Check(text.find(p.loadingPrefix)<text.find("REFUSED"),"the load line comes before the refusal");
}
}  // namespace

int main(int argc,char** argv) {
    if(argc<5) {
        std::fprintf(stderr,"usage: plugin_pair_load_test <EDF6VehicleCrew.dll> <EDF6AutoTurret.dll> <scratch dir> crew-first|turret-first\n");
        return 2;
    }
    const std::string order=argv[4];
    Check(order=="crew-first" || order=="turret-first","order is crew-first or turret-first");
    Check(GetModuleHandleW(L"EDF.dll")==nullptr,"no EDF.dll in this process");
    const std::filesystem::path dir=std::filesystem::u8path(argv[3]);
    plugintest::FreshDir(dir);

    Plugin crew{"EDF6VehicleCrew",aimlink::kCrewDll,kCrewExports,std::size(kCrewExports),"EDF6VehicleCrew "};
    Plugin turret{"EDF6AutoTurret",aimlink::kTurretDll,kTurretExports,std::size(kTurretExports),"EDF6AutoTurret "};
    crew.copy=plugintest::CopyInto(std::filesystem::u8path(argv[1]),dir);
    turret.copy=plugintest::CopyInto(std::filesystem::u8path(argv[2]),dir);
    Check(crew.copy.filename()==aimlink::kCrewDll && turret.copy.filename()==aimlink::kTurretDll,
          "the built files carry the names aimlink.h looks them up by");
    Plugin& first=order=="crew-first" ? crew : turret;
    Plugin& second=order=="crew-first" ? turret : crew;
    const bool firstIsCrew=&first==&crew;

    // The first alone: its own names resolve, the peer's do not (and are not retried within the second).
    first.module=plugintest::LoadPlugin(first.copy);
    Check(GetModuleHandleW(second.fileName)==nullptr,"the peer is not loaded yet");
    CheckExports(first,nullptr);
    std::vector<Resolver> resolvers(std::size(kLinks));
    for(std::size_t i=0;i<std::size(kLinks);++i) {
        const Link& l=kLinks[i];
        const bool own=l.crew==firstIsCrew;
        const AnyFn f=Resolve(l,resolvers[i]);
        if(own)Check(f && reinterpret_cast<FARPROC>(f)==GetProcAddress(first.module,l.name),"an own aimlink name resolves at once");
        else Check(f==nullptr && resolvers[i].triedAt!=0,"the peer's name resolves to nothing while the peer is absent");
    }

    // Then the second: the peer's names come once the throttle's second is over.
    second.module=plugintest::LoadPlugin(second.copy);
    CheckExports(first,second.module);
    CheckExports(second,first.module);
    for(std::size_t i=0;i<std::size(kLinks);++i) {
        const Link& l=kLinks[i];
        const HMODULE owner=l.crew ? crew.module : turret.module;
        Resolver& r=resolvers[i];
        if(!r.cache) {
            r.triedAt=GetTickCount64();   // as if the last lookup had just failed
            Check(Resolve(l,r)==nullptr,"looked up again only once a second");
            r.triedAt=GetTickCount64()-1000;   // the second over, without waiting it out
        }
        const AnyFn f=Resolve(l,r);
        if(!f)std::fprintf(stderr,"aimlink %s not resolved\n",l.name);
        Check(f!=nullptr,"every aimlink name resolves with both loaded");
        Check(reinterpret_cast<FARPROC>(f)==GetProcAddress(owner,l.name) && plugintest::OwnerOf(reinterpret_cast<const void*>(f))==owner,
              "...to its owner's export");
        Resolver fresh;
        Check(Resolve(l,fresh)==f,"a fresh lookup finds the same export");
    }

    // EML6_Load in the load order, neither with an EDF.dll to work on.
    CheckRefused(first);
    CheckRefused(second);
    Check(GetModuleHandleW(L"EDF.dll")==nullptr,"still no EDF.dll in this process");
    std::printf("plugin_pair_load_test (%s): %d checks passed\n",order.c_str(),plugintest::checks);
    std::fflush(stdout);
    // The plugins stay loaded (as in the game: the crew log's writer thread runs on); the process ends here.
    return 0;
}
