// The jets a player summons with a vehicle request, loaded before the battle (player_vehicle.h; the design, the decision
// and the wire: player_vehicle_logic.h). EDF.dll TimeDateStamp 0x678CCB46, RVAs (docs/airdrop-vehicle-re.md §1, H):
//   0x5E5070  Transporter508's init: its container made, then configured at 0x5E5687: call 0x5E8B40(container,
//             shared_ptr<requester>* rdx, const wchar_t* vehicle SGO r8, VehicleSetup* r9, float level [rsp+20h]): the
//             one call site of the configure (watched here, a rel32 redirect; the plugin's own airdrops call 0x5E8B40
//             directly and are not seen);
//   0x5E8C00  the container's state once let go: landed, it makes the vehicle from its configured path (+0xB80) with
//             CreateObject at 0x5E8FDC (airdrop.cpp redirects that call; its VehicleStep asks PlayerVehicleStepPath).
// Every machine of the room runs its own transporter and container for every member's request (the vehicle is then
// registered under the ID the container derives, the same everywhere): every machine swaps the path the same way, by the
// requester machine's decision (player_vehicle_logic.h Engine). Game thread only (the transporter's init, the container's
// update, the channel's receive / update).
#include "player_vehicle.h"
#include "player_vehicle_logic.h"
#include "airdrop.h"
#include "airdrop_logic.h"
#include "command_identity.h"
#include "crew.h"
#include "memory.h"
#include "plugin_net.h"
#include "room_sync.h"
#include "support_config.h"
#include "support_protocol.h"
#include "support_soldier.h"
#include "support_variants.h"
#include <cstring>
#include <cwchar>
#include <iterator>

namespace crew {
namespace {
constexpr unsigned kConfigure=0x5E8B40,kConfigureCall=0x5E5687,kPreload=0x7A3780;
constexpr std::size_t kPreloadMgr=0x20B29A8;
constexpr std::size_t kContainerVehicleSgo=0xB80;   // the configured path (a std::wstring; 0x5E8BC7)
// mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi; push rdi; sub rsp,20h; mov rax,[rdx+8]; mov rsi,rcx (airdrop.cpp)
const unsigned char kConfigureSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,
                                     0x83,0xEC,0x20,0x48,0x8B,0x42,0x08,0x48,0x8B,0xF1};
// The call site's arguments: movss [rsp+20h],xmm6; lea r9,[rbp-50h]; mov r8,[rbp-58h]; mov rdx,rdi; mov rcx,[rsp+68h].
const unsigned char kCallArgsSig[]={0xF3,0x0F,0x11,0x74,0x24,0x20,0x4C,0x8D,0x4D,0xB0,0x4C,0x8B,0x45,0xA8,0x48,0x8B,0xD7,0x48,0x8B,
                                    0x4C,0x24,0x68};
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
using ConfigureFn=void(*)(unsigned char*,const airdrop::SharedRef*,const wchar_t*,const void*,float);

bool watching=false;      // the configure call redirected
bool landing=false;       // airdrop.cpp's vehicle step calls PlayerVehicleStepPath (it reports through InstallAirdrop)
playerveh::Engine engine;
std::uint32_t origin=0;
bool haveSent=false;std::uint32_t haveGeneration=~0u;
int logs=0;

// A container configured for one of the twins, until it makes its vehicle.
struct Pending {
    unsigned char* box=nullptr;
    const wchar_t* path=nullptr;   // its configured path as its vehicle step hands it to CreateObject (airdrop.cpp's key)
    unsigned char requester[wire::kIdBytes]{};
    VehicleBody body=VehicleBody::none;
    std::uint32_t seq=0;
    bool local=false;
};
constexpr int kPendingMost=32;
Pending pending[kPendingMost];
wchar_t swapped[160];   // the path handed back (CreateObject copies it before PlayerVehicleStepPath is asked again)

const wchar_t* ConfiguredPath(const unsigned char* box) noexcept {
    const unsigned char* s=box+kContainerVehicleSgo;
    return At<std::uint64_t>(s,0x18)>=8 ? At<const wchar_t*>(s,0) : reinterpret_cast<const wchar_t*>(s);
}
bool ModsObject(const wchar_t* file) noexcept {
    wchar_t path[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,path,static_cast<DWORD>(std::size(path)));
    wchar_t* slash=n && n<std::size(path) ? wcsrchr(path,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    if(wcscat_s(path,L"\\Mods\\OBJECT\\") || wcscat_s(path,file))return false;
    const DWORD a=GetFileAttributesW(path);
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}

void SendHave(std::uint32_t peer=0) noexcept {
    if(!plugin_net::Ready() || !origin)return;
    static playerveh::Msg m;   // large (kHaveMost variants)
    m=engine.Have(origin);
    unsigned char bytes[playerveh::kMostSize];
    const std::size_t size=playerveh::Encode(m,bytes,sizeof(bytes));
    if(!size)return;
    if(peer)plugin_net::Send(peer,support_net::kExtPlayerVehicle,bytes,size);
    else plugin_net::Broadcast(support_net::kExtPlayerVehicle,bytes,size);
}
void SendMsg(const playerveh::Msg& m,std::uint32_t peer=0) noexcept {
    if(!plugin_net::Ready())return;
    unsigned char bytes[playerveh::kMostSize];
    const std::size_t size=playerveh::Encode(m,bytes,sizeof(bytes));
    if(!size)return;
    if(peer)plugin_net::Send(peer,support_net::kExtPlayerVehicle,bytes,size);
    else plugin_net::Broadcast(support_net::kExtPlayerVehicle,bytes,size);
}

// The transporter's configure of its container (0x5E5687): the stock one, then a request of a twin noted.
void ConfigureHook(unsigned char* box,const airdrop::SharedRef* requester,const wchar_t* sgo,const void* setup,float level) {
    reinterpret_cast<ConfigureFn>(image+kConfigure)(box,requester,sgo,setup,level);
    if(!landing)return;
    __try {
        const int twin=playerveh::TwinOfPath(sgo);
        const unsigned char* who=requester ? static_cast<const unsigned char*>(requester->object) : nullptr;
        if(twin<0 || !box || !who)return;
        Pending p;
        // The requester by its mission slot (player_requester.h), the same on every machine; its NetId is not.
        if(!PlayerRequesterOf(who,p.requester) || wire::ZeroId(p.requester)) {
            if(logs++<32)Log("PLAYERVEH a %ls request by %p that is no live player: the stock twin",kPlayerVehicles[twin].twin,who);
            return;
        }
        p.box=box;p.path=ConfiguredPath(box);p.body=kPlayerVehicles[twin].body;
        p.local=who==PlayerHuman();
        if(p.local) {
            const playerveh::Msg m=engine.Decide(p.requester,p.body,origin ? origin : 1,InSession());
            p.seq=m.seq;
            if(InSession())SendMsg(m);
            Log("PLAYERVEH this player's %ls request #%u: %s",kPlayerVehicles[twin].twin,m.seq,
                m.variant.bits ? "the loaded jet (every machine has it)" : "the stock twin (no loadout, not preloaded, or a machine lacks it)");
        } else p.seq=engine.Count(p.requester,p.body);
        Pending* slot=nullptr;
        for(auto& q:pending)if(!q.box){slot=&q;break;}
        if(!slot){slot=&pending[0];Log("PLAYERVEH more than %d containers in the air: the oldest forgotten (stock)",kPendingMost);}
        *slot=p;
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("PLAYERVEH configure watch fault");}
}

// --- The channel ---
bool Receive(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    static playerveh::Msg m;   // large
    if(!playerveh::Decode(bytes,size,m)) {
        if(logs++<32)Log("PLAYERVEH peer %u: a message that does not read (%zu bytes): dropped",peer,size);
        return true;
    }
    if(m.kind==playerveh::Kind::have){engine.Heard(static_cast<int>(peer),m);return true;}
    // A decide / sync speaks for a player: it must be the sender's own (a peer never decides another machine's request).
    bool sender=false;
    __try { sender=SenderIsPlayer(m.requester,puid,nullptr,true); } __except(EXCEPTION_EXECUTE_HANDLER){sender=false;}
    if(!sender) {
        if(logs++<32)Log("PLAYERVEH peer %u: a %s for a player that is not its sender's: dropped",peer,m.kind==playerveh::Kind::decide ? "decision" : "sync");
        return true;
    }
    engine.Received(m);
    return true;
}
void Update(const CommandNetworkContext&,std::uint64_t) noexcept {
    const std::uint32_t generation=plugin_net::RoomGeneration();
    bool feature[playerveh::kPeersMost]{};
    const int peers=static_cast<int>(plugin_net::PeerCount());
    for(int i=0;i<peers && i<playerveh::kPeersMost;++i)feature[i]=plugin_net::PeerHas(static_cast<std::uint32_t>(i+1),support_net::kExtPlayerVehicle);
    engine.Room(generation,peers,feature);
    if(landing && origin && (!haveSent || generation!=haveGeneration)){SendHave();haveSent=true;haveGeneration=generation;}
}
void Reset() noexcept { haveSent=false; }
void Joined(std::uint32_t peer) noexcept {
    if(!landing || !origin)return;
    SendHave(peer);
    unsigned char id[wire::kIdBytes];
    bool ok=false;
    __try { const unsigned char* me=PlayerHuman();ok=me && PlayerRequesterOf(me,id) && !wire::ZeroId(id); } __except(EXCEPTION_EXECUTE_HANDLER){ok=false;}
    if(!ok)return;
    playerveh::Msg syncs[kPlayerVehicleCount];
    const int n=engine.Syncs(id,origin,syncs,kPlayerVehicleCount);
    for(int i=0;i<n;++i)SendMsg(syncs[i],peer);
}
}  // namespace

bool InstallPlayerVehicles() noexcept {
    if(!Matches(kConfigure,kConfigureSig,sizeof(kConfigureSig)) || !Matches(kConfigureCall-22,kCallArgsSig,sizeof(kCallArgsSig)) ||
       !Matches(kPreload,kPreloadSig,sizeof(kPreloadSig))) {
        Log("PLAYERVEH profile mismatch: the requests make the stock twins");
        return false;
    }
    bool changed=false;
    watching=RedirectCall(image+kConfigureCall,image+kConfigure,reinterpret_cast<void*>(&ConfigureHook),changed);
    if(!watching)Log("PLAYERVEH the configure call not watched%s: the requests make the stock twins",changed ? " (half patched)" : "");
    landing=watching && AirdropWatching();
    if(watching && !landing)Log("PLAYERVEH the container's vehicle step is not watched (airdrop.cpp): the requests make the stock twins");
    return landing;
}

void PreloadPlayerVehicles() noexcept {
    for(auto& p:pending)p=Pending{};
    origin=plugin_net::NewOrigin();haveSent=false;
    playerveh::Variant preloaded[playerveh::kHaveMost];int n=0;
    const SupportConfig& cfg=SupportCfg();
    void* mgr=nullptr;
    __try { mgr=At<void*>(image,kPreloadMgr); } __except(EXCEPTION_EXECUTE_HANDLER){mgr=nullptr;}
    const auto preload=[&](const playerveh::Variant& v) noexcept {
        if(!landing || !mgr || n>=playerveh::kHaveMost || !playerveh::Valid(v))return;
        for(int i=0;i<n;++i)if(playerveh::Same(preloaded[i],v))return;
        VehicleLoadout l;wchar_t file[96],app[160];
        if(!LoadoutOfVariant(v.bits,v.body,&l) || !VehicleVariantFile(l,file,96))return;
        if(!ModsObject(file)){NoteMissingVariant(file,"a player's own loaded jet");return;}
        if(!playerveh::LoadedPath(v,app,std::size(app)))return;
        __try {
            reinterpret_cast<void(*)(void*,const wchar_t*,std::int32_t,std::int32_t)>(image+kPreload)(mgr,app,2,-1);
            preloaded[n++]=v;
        } __except(EXCEPTION_EXECUTE_HANDLER){Log("PLAYERVEH preload fault: %ls",file);}
    };
    for(int i=0;i<kPlayerVehicleCount;++i)
        if(cfg.playerVehicle[i].On() && cfg.playerVehicle[i].count)preload(playerveh::Variant{cfg.playerVehicle[i].body,LoadoutVariant(cfg.playerVehicle[i])});
    struct Ctx { decltype(preload)* f; } ctx{&preload};
    const int others=roomsync::ForEachRoomPlayerVehicle([](void* c,VehicleBody body,std::uint64_t bits) noexcept {
        (*static_cast<Ctx*>(c)->f)(playerveh::Variant{body,bits});
    },&ctx);
    engine.Begin(cfg.playerVehicle,kPlayerVehicleCount,preloaded,n);
    Log("PLAYERVEH mission: %d loaded jet(s) preloaded (this player's and %d of the room's other members')%s",n,others,
        landing ? "" : ": off (the request's container is not watched)");
}

const wchar_t* PlayerVehicleStepPath(const wchar_t* sgo) noexcept {
    if(!landing || !sgo)return sgo;
    for(auto& p:pending) {
        if(!p.box || p.path!=sgo)continue;
        const Pending q=p;p=Pending{};
        const playerveh::Engine::Landing l=engine.Land(q.requester,q.body,q.seq);
        const int twin=PlayerVehicleOfBody(q.body);
        if(!l.found)Log("PLAYERVEH %s request #%u of %ls landed with no decision heard: the stock twin",q.local ? "this player's" : "a member's",
                        q.seq,twin>=0 ? kPlayerVehicles[twin].twin : L"?");
        else if(l.missing)Log("PLAYERVEH request #%u: decided loaded, but this machine has not preloaded the file: the stock twin (MISMATCH)",q.seq);
        if(!l.bits)return sgo;
        if(!playerveh::LoadedPath(playerveh::Variant{q.body,l.bits},swapped,std::size(swapped)))return sgo;
        Log("PLAYERVEH request #%u lands as %ls",q.seq,swapped);
        return swapped;
    }
    return sgo;
}

plugin_net::Channel PlayerVehicleNetChannel() noexcept {
    plugin_net::Channel c;
    c.name="player vehicles";c.magic=playerveh::kMagic;c.version=playerveh::kVersion;c.featureBit=support_net::kExtPlayerVehicle;
    c.minSize=playerveh::kHead;c.maxSize=playerveh::kMostSize;
    c.receive=&Receive;c.update=&Update;c.reset=&Reset;c.joined=&Joined;
    return c;
}
}  // namespace crew
