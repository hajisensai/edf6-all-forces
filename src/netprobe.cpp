// The online probe (Debug=1 only; docs/online-re.md): for a two-machine test of the multi-crew gunship, once a
// second per helicopter-class vehicle (506 / 409 / 410 / base: the plugin's jets and gunship are 506 bodies) while a
// session is on, which machine runs it and how its pose replication stands. Nothing is written.
//   online   0x7748F0: a session is on (its member table has us); offline the probe says nothing
//   host     0x784210: this machine is the room's owner (1 offline)
//   auth     0x630DF0(veh, 0, 0, 0, 1): seat 0's rider is this machine's (offline: 1). What the heli's
//            replication write (slot 7 0x6559D0) asks before it sends the pose: the machine that answers 1 is the
//            one whose pose the others follow
//   op       0x630F90(veh, 1, 1): 1 local / 2 remote / 0 nobody: seat 0's rider, else the seat's last rider
//            (+0x300), else the host (the dl=1 fallback)
//   reg      word veh+0x128 (its NetworkObject's +8): bit 0 registered as another machine's, bit 1 as ours, 0 never
//            registered (the plugin's own CreateObject: no network identity at all)
//   rx       the replica side (heli slot 51 0x651F90): frames since the last pose packet (+0x1D78), the blend
//            weight (+0x1D70, 0.07 per frame, ramping to 1.0 after 15 frames), settled (+0x1D74), and how far the
//            body is from the last received / sent position (+0x1D60)
//   tx       the pose's send throttle (+0x1D80: countdown, interval, min, max frames; +0x1D90 the fast flag),
//            the send rate statics could not find (docs/online-re.md section 4)
#include "crew.h"
#include "gunnerrecoil.h"
#include "heli.h"
#include "memory.h"
#include <cmath>

namespace crew {
namespace {
constexpr unsigned kOnline=0x7748F0,kIsHost=0x784210,kSeatLocal=0x630DF0,kOperator=0x630F90;
constexpr std::size_t kNetFlagsWord=0x128;
constexpr std::size_t kRxPos=0x1D60,kRxBlend=0x1D70,kRxSettled=0x1D74,kRxFrames=0x1D78,kTxThrottle=0x1D80,kTxFast=0x1D90;
constexpr ULONGLONG kLogMs=1000;
// Room for every vehicle (it was 16: past 16 helicopters each newcomer pushed the oldest out and logged at once, so
// the once-a-second row came every frame, about 1000 rows/s with 22 helis, 2026-10-07 CPU audit).
constexpr int kWatchCount=256;

struct Sig { unsigned rva; unsigned char bytes[16]; };
const Sig kSigs[]={
    {kOnline,{0x48,0x8B,0x05,0x99,0xDF,0x93,0x01,0x8B,0x48,0x38,0x83,0xF9,0xFF,0x75,0x03,0x32}},
    {kIsHost,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0xE8}},
    {kSeatLocal,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7C,0x24,0x18,0x41}},
    {kOperator,{0x48,0x89,0x5C,0x24,0x20,0x44,0x88,0x44,0x24,0x18,0x88,0x54,0x24,0x10,0x55,0x56}},
};
using OnlineFn=bool(__fastcall*)(const void*);
using IsHostFn=bool(__fastcall*)(const void*);
using SeatLocalFn=bool(__fastcall*)(void*,int,int,int,bool);
using OperatorFn=int(__fastcall*)(void*,bool,bool);

int checked=0;   // 0 not yet, 1 the four functions are the ones read, -1 not (the probe stays off)
struct Watch { const void* v; ULONGLONG at; };
Watch watch[kWatchCount]{};
int lastOnline=-1;

bool SigsMatch() noexcept {
    __try {
        for(const auto& s:kSigs)if(!edf::Matches(image,s.rva,s.bytes,sizeof(s.bytes)))return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Due(const void* v,ULONGLONG now) noexcept {
    Watch* slot=&watch[0];
    for(auto& w:watch) {
        if(w.v==v) {
            if(now-w.at<kLogMs)return false;
            w.at=now;
            return true;
        }
        if(w.at<slot->at)slot=&w;
    }
    *slot=Watch{v,now};
    return true;
}

void Report(unsigned char* v,bool host) noexcept {
    const bool seated=At<const void*>(v,kSeats)!=nullptr && At<std::size_t>(v,kSeatCount)>0;
    const int auth=seated ? reinterpret_cast<SeatLocalFn>(image+kSeatLocal)(v,0,0,0,true) : -1;
    const int op=seated ? reinterpret_cast<OperatorFn>(image+kOperator)(v,true,true) : -1;
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    const float* rx=reinterpret_cast<const float*>(v+kRxPos);
    const float dx=p[0]-rx[0],dy=p[1]-rx[1],dz=p[2]-rx[2];
    Log("NET v=%p host=%d auth=%d op=%d reg=0x%04X pos=(%.1f,%.1f,%.1f) rx frames=%d blend=%.2f settled=%d off=%.2f m"
        " tx=%d/%d/%d/%d fast=%d",v,host,auth,op,At<std::uint16_t>(v,kNetFlagsWord),p[0],p[1],p[2],At<int>(v,kRxFrames),
        At<float>(v,kRxBlend),At<unsigned char>(v,kRxSettled),std::sqrt(dx*dx+dy*dy+dz*dz),At<int>(v,kTxThrottle),
        At<int>(v,kTxThrottle+4),At<int>(v,kTxThrottle+8),At<int>(v,kTxThrottle+0xC),At<unsigned char>(v,kTxFast));
}
}  // namespace

// Whether a session is on. An EDF.dll whose session function is not the one read counts as no session (logged once):
// the plugin then does what it does offline, as it did before the online gates (AutoCrew, its damage, its pilots); an
// unknown session state never switches them off for a player who is not online at all.
bool InSession() noexcept {
    // Both the picker and game thread call this: C++ initializes the signature once, with synchronization.
    static const bool sig=[]() noexcept {
        bool ok=false;
        __try { ok=edf::Matches(image,kOnline,kSigs[0].bytes,sizeof(kSigs[0].bytes)); } __except(EXCEPTION_EXECUTE_HANDLER) { ok=false; }
        if(!ok)Log("NET session check off: EDF+%#x does not match docs/online-re.md, the plugin acts as offline",kOnline);
        return ok;
    }();
    return sig && reinterpret_cast<OnlineFn>(image+kOnline)(nullptr);
}

void NetProbe(unsigned char* v) noexcept {
    if(!Cfg().debug || !IsHelicopter(v))return;
    if(!checked) {
        checked=SigsMatch() ? 1 : -1;
        if(checked<0)Log("NET probe off: the session functions do not match docs/online-re.md");
    }
    if(checked<0)return;
    const bool online=reinterpret_cast<OnlineFn>(image+kOnline)(nullptr);
    const bool host=reinterpret_cast<IsHostFn>(image+kIsHost)(nullptr);
    if(static_cast<int>(online)!=lastOnline) {
        lastOnline=online;
        Log("NET session %s, this machine %s the host",online ? "on" : "off",host ? "is" : "is not");
    }
    if(online && Due(v,GetTickCount64()) && Readable(v+kTxFast,1))Report(v,host);
}
}  // namespace crew
