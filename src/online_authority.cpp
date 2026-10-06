// The game's side of the online authority (src/online_authority.h has the rules and what each function is).
#include "online_authority.h"
#include "crew.h"
#include "memory.h"

namespace crew {
namespace {
constexpr unsigned kIsHost=0x784210,kOperator=0x630F90;
constexpr std::size_t kNetWord=0x128;   // object +0x120 (its NetworkObject) +8
// The same bytes netprobe.cpp checks (docs/online-re.md section 7).
const unsigned char kIsHostSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0xE8};
const unsigned char kOperatorSig[]={0x48,0x89,0x5C,0x24,0x20,0x44,0x88,0x44,0x24,0x18,0x88,0x54,0x24,0x10,0x55,0x56};
using IsHostFn=bool(__fastcall*)(const void*);
using OperatorFn=int(__fastcall*)(void*,bool,bool);
using RideAiFn=void(__fastcall*)(void*,bool);

// Both functions are the ones read (checked once; logged once when not: online, nothing is then this machine's).
bool Known() noexcept {
    static const bool ok=[]() noexcept {
        bool match=false;
        __try {
            match=edf::Matches(image,kIsHost,kIsHostSig,sizeof(kIsHostSig)) && edf::Matches(image,kOperator,kOperatorSig,sizeof(kOperatorSig));
        } __except(EXCEPTION_EXECUTE_HANDLER) { match=false; }
        if(!match)Log("NET authority off: EDF+%#x / EDF+%#x do not match docs/online-re.md, online the plugin runs nothing it gates",
                      kIsHost,kOperator);
        return match;
    }();
    return ok;
}

bool Host() noexcept { return Known() && reinterpret_cast<IsHostFn>(image+kIsHost)(nullptr); }

// Seat 0 holds a live rider that was never registered (RideAi's DummyVehicleRider, the plugin's NPC).
bool NpcSeat0(const unsigned char* v) noexcept {
    const unsigned char* seat=SeatAt(const_cast<unsigned char*>(v),0);
    if(SeatRider(seat)==Rider::none)return false;
    const auto rider=At<const unsigned char*>(seat,kSeatRider);
    return Readable(rider,kNetWord+2) && online::LocalCopy(At<std::uint16_t>(rider,kNetWord));
}

// The unregistered copies' owners (NoteLocalCopy), by object and its weak-this block (a new object at an old address is
// not the old one). Game thread only. A full table takes the place of a gone object first, else the oldest record.
struct CopyRecord { const void* obj; const void* ctrl; online::CopyOwner owner; };
constexpr int kCopyRecords=128;
CopyRecord copies[kCopyRecords]{};
int copyNext=0;
online::CopyOwner spawnScope=online::kCopyHost;

bool Live(const CopyRecord& r) noexcept {
    return r.obj && Readable(r.obj,kSelfCtrl+8) && At<const void*>(r.obj,kSelfCtrl)==r.ctrl;
}

// The recorded owner of `object` (a pointer compared first: `object` may be no object at all, as a jet's flight source).
online::CopyOwner RecordedOwner(const void* object) noexcept {
    if(!object)return online::kCopyHost;
    for(const auto& r:copies)if(r.obj==object && Live(r))return r.owner;
    return online::kCopyHost;
}

// The facts of one question; false when the object cannot be read (online: then nothing is this machine's).
bool Read(const void* object,online::Facts* f) noexcept {
    *f=online::Facts{InSession(),true,false,0,false,false,online::kCopyHost,0};
    if(!f->session)return true;
    f->known=Known();
    f->host=Host();
    const auto o=static_cast<const unsigned char*>(object);
    if(!Readable(o,kNetWord+2))return false;
    __try {
        f->net=At<std::uint16_t>(o,kNetWord);
        if(online::LocalCopy(f->net)){f->copyOwner=RecordedOwner(o);return true;}
        if(!f->known)return true;
        f->vehicle=Readable(o,kSeatCount+8) && SeatCount(o)>0 && KnownVehicle(o);
        if(f->vehicle) {
            f->npcSeat0=NpcSeat0(o);
            f->stock=reinterpret_cast<OperatorFn>(image+kOperator)(const_cast<unsigned char*>(o),true,true);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return true;
}
}  // namespace

bool OnlineHostOnly() noexcept {
    const bool session=InSession();
    return online::HostOnly(session,!session || Known(),!session || Host());
}

bool IsOnlineAuthority(const void* object) noexcept {
    online::Facts f;
    return Read(object,&f) && online::Authority(f);
}

bool OnlineRunsHere(const void* object) noexcept {
    online::Facts f;
    return Read(object,&f) && online::RunsHere(f);
}

bool OnlineMaySeatNpc(const void* vehicle) noexcept {
    online::Facts f;
    return Read(vehicle,&f) && online::MaySeatNpc(f);
}

bool OnlineShotCounts(const void* owner,online::Shooter by) noexcept {
    online::Facts f;
    return Read(owner,&f) && online::ShotCounts(f,by);
}

const unsigned char* OnlineAttacker(const unsigned char* owner,online::Shooter by) noexcept {
    online::Facts f;
    if(!Read(owner,&f) || !online::ShooterIsAttacker(f,by))return owner;
    const unsigned n=SeatCount(owner);
    for(unsigned i=0;i<n;++i) {
        const unsigned char* seat=SeatAt(const_cast<unsigned char*>(owner),i);
        if(SeatRider(seat)==Rider::player)return At<const unsigned char*>(seat,kSeatRider);
    }
    return owner;
}

online::CopyOwner SetSpawnOwner(online::CopyOwner owner) noexcept {
    const online::CopyOwner previous=spawnScope;
    spawnScope=owner;
    return previous;
}

online::CopyOwner CopyOwnerOfCaller(const unsigned char* human) noexcept {
    if(IsPlayer(human))return online::kCopyHere;
    return human && edf::RemoteRider(human) ? online::kCopyElsewhere : online::kCopyHost;
}

void NoteLocalCopy(const void* object,const void* parent) noexcept {
    if(!object)return;
    const online::CopyOwner inherited=RecordedOwner(parent);
    const online::CopyOwner owner=inherited!=online::kCopyHost ? inherited : spawnScope;
    CopyRecord* slot=nullptr;
    for(auto& r:copies)if(!slot && r.obj==object)slot=&r;
    for(auto& r:copies)if(!slot && !Live(r))slot=&r;
    if(!slot){slot=&copies[copyNext];copyNext=(copyNext+1)%kCopyRecords;}
    *slot=CopyRecord{object,At<const void*>(object,kSelfCtrl),owner};
}

bool SeatNpcRider(unsigned char* vehicle,bool spawned) noexcept {
    if(!OnlineMaySeatNpc(vehicle))return false;
    reinterpret_cast<RideAiFn*>(At<void**>(vehicle,0))[kSlotRideAi](vehicle,spawned);
    return true;
}
}  // namespace crew
