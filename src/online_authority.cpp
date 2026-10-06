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

// A seat holds a player of this machine (Rider::player: IsPlayer leaves out another machine's, common/seat.cpp).
bool LocalPlayerAboard(const unsigned char* v) noexcept {
    const unsigned n=SeatCount(v);
    for(unsigned i=0;i<n;++i)if(SeatRider(SeatAt(const_cast<unsigned char*>(v),i))==Rider::player)return true;
    return false;
}

// The facts of one question; false when the object cannot be read (online: then nothing is this machine's).
bool Read(const void* object,online::Facts* f) noexcept {
    *f=online::Facts{InSession(),true,false,0,false,false,false,0};
    if(!f->session)return true;
    f->known=Known();
    f->host=Host();
    const auto o=static_cast<const unsigned char*>(object);
    if(!f->known || !Readable(o,kNetWord+2))return false;
    __try {
        f->net=At<std::uint16_t>(o,kNetWord);
        f->vehicle=Readable(o,kSeatCount+8) && SeatCount(o)>0 && KnownVehicle(o);
        if(f->vehicle) {
            f->npcSeat0=NpcSeat0(o);
            f->localPlayer=LocalPlayerAboard(o);
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

bool SeatNpcRider(unsigned char* vehicle,bool spawned) noexcept {
    if(!OnlineMaySeatNpc(vehicle))return false;
    reinterpret_cast<RideAiFn*>(At<void**>(vehicle,0))[kSlotRideAi](vehicle,spawned);
    return true;
}
}  // namespace crew
