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
// The ground vehicles (CarBase / TankBase: main slot 51 0x672AD0, docs/online-re.md section 4.6) get the same row as
// `NET ground`: auth / op / reg as above, ai = veh+0x1A bit 8 (the native AI list: the writer then sends no velocity),
// seat0 = seat 0 rider's net word (-1 none), rx = how far the body is from the last received position (+0x2630) and
// its settled flags (+0x2650 position, +0x2651 rotation; set once corrected, cleared by each packet), stale = slot 51's
// frame count (+0x2624, seat 0's input block cleared at 30, then 0 again), tx = the position
// throttle (+0x2660: countdown, interval, min, max) and how far the body is from the last sent position (+0x26B0).
#include "crew.h"
#include "gunnerrecoil.h"
#include "heli.h"
#include "memory.h"
#include <atomic>
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr unsigned kOnline=0x7748F0,kIsHost=0x784210,kSeatLocal=0x630DF0,kOperator=0x630F90;
constexpr unsigned kGameStatus=0x20B2890;
constexpr std::size_t kModeTable=0x20,kModeCount=0x30,kCurrentMode=0x38,kModeRaw=0x10,kRawRelative=8,kRawOnline=0x68;
// Complete side-effect-free native session predicate, including its RIP-relative singleton load.
// GameDataMgr is published after construction and absent before startup / after teardown. The native
// function assumes it exists; plugin initialization and picker-thread queries do not have that contract.
constexpr unsigned char kOnlineCode[]={
    0x48,0x8B,0x05,0x99,0xDF,0x93,0x01,0x8B,0x48,0x38,0x83,0xF9,0xFF,0x75,0x03,0x32,
    0xC0,0xC3,0x48,0x8B,0x40,0x20,0x48,0x8B,0x0C,0xC8,0x48,0x8B,0x51,0x10,0x48,0x63,
    0x42,0x08,0x83,0x7C,0x10,0x68,0x00,0x0F,0x95,0xC0,0xC3
};
constexpr std::size_t kNetFlagsWord=0x128;
constexpr std::size_t kRxPos=0x1D60,kRxBlend=0x1D70,kRxSettled=0x1D74,kRxFrames=0x1D78,kTxThrottle=0x1D80,kTxFast=0x1D90;
constexpr unsigned kGroundSlot51=0x672AD0;   // CarBase / TankBase's network step (slot 51)
constexpr std::size_t kSlot51=51,kListFlags=0x1A;
constexpr std::size_t kGroundStale=0x2624,kGroundRxPos=0x2630,kGroundRxSettled=0x2650,kGroundTx=0x2660,kGroundTxPos=0x26B0;
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

int SessionReadFault(DWORD code) noexcept {
    // Explicit lifecycle/readability checks below are the normal path. Only a concurrent free
    // between a successful check and its read reaches this boundary; unrelated faults propagate.
    return code==EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}
bool SessionCodeKnown(const unsigned char* base) noexcept {
    if(!base)return false;
    // Cache the immutable code identity, never the singleton's presence or a session result.
    static std::atomic<const unsigned char*> ready{nullptr},reported{nullptr};
    if(ready.load(std::memory_order_acquire)==base)return true;
    bool valid=false;
    __try {
        if(edf::Matches(base,kOnline,kOnlineCode,sizeof(kOnlineCode))) {
            const auto displacement=At<std::int32_t>(base+kOnline,3);
            valid=static_cast<std::int64_t>(kOnline)+7+displacement==kGameStatus;
        }
    } __except(SessionReadFault(GetExceptionCode())) {valid=false;}
    if(valid)ready.store(base,std::memory_order_release);
    else if(reported.exchange(base,std::memory_order_relaxed)!=base)
        Log("NET session check off: EDF+%#x does not match the checked session lifecycle profile",kOnline);
    return valid;
}
const unsigned char* SessionAddress(const void* pointer,std::int64_t offset) noexcept {
    const auto at=reinterpret_cast<std::uintptr_t>(pointer);
    if(!at)return nullptr;
    if(offset>=0) {
        const auto forward=static_cast<std::uint64_t>(offset);
        if(forward>UINTPTR_MAX-at)return nullptr;
        return reinterpret_cast<const unsigned char*>(at+forward);
    }
    const auto back=static_cast<std::uint64_t>(-(offset+1))+1;
    return back<=at ? reinterpret_cast<const unsigned char*>(at-back) : nullptr;
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
bool IsGround(const unsigned char* v) noexcept {
    const auto vt=At<void* const*>(v,0);
    return Readable(vt,(kSlot51+1)*8) && vt[kSlot51]==image+kGroundSlot51;
}

float Off(const unsigned char* v,std::size_t at) noexcept {
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    const float* q=reinterpret_cast<const float*>(v+at);
    const float dx=p[0]-q[0],dy=p[1]-q[1],dz=p[2]-q[2];
    return std::sqrt(dx*dx+dy*dy+dz*dz);
}

void ReportGround(unsigned char* v,bool host) noexcept {
    const bool seated=At<const void*>(v,kSeats)!=nullptr && At<std::size_t>(v,kSeatCount)>0;
    const int auth=seated ? reinterpret_cast<SeatLocalFn>(image+kSeatLocal)(v,0,0,0,true) : -1;
    const int op=seated ? reinterpret_cast<OperatorFn>(image+kOperator)(v,true,true) : -1;
    int seat0=-1;
    if(seated) {
        const auto* rider=At<const unsigned char*>(At<const unsigned char*>(v,kSeats),kSeatRider);
        if(rider && Readable(rider,kNetFlagsWord+2))seat0=At<std::uint16_t>(rider,kNetFlagsWord);
    }
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    Log("NET ground v=%p host=%d auth=%d op=%d reg=0x%04X ai=%d seat0=%d pos=(%.1f,%.1f,%.1f) rx off=%.2f m settled=%d/%d"
        " stale=%d tx=%d/%d/%d/%d sent off=%.2f m",v,host,auth,op,At<std::uint16_t>(v,kNetFlagsWord),(v[kListFlags]&8)!=0,seat0,
        p[0],p[1],p[2],Off(v,kGroundRxPos),v[kGroundRxSettled],v[kGroundRxSettled+1],At<int>(v,kGroundStale),At<int>(v,kGroundTx),
        At<int>(v,kGroundTx+4),At<int>(v,kGroundTx+8),At<int>(v,kGroundTx+0xC),Off(v,kGroundTxPos));
}
}  // namespace

// Whether a session is on. An EDF.dll whose session function is not the one read counts as no session (logged once):
// the plugin then does what it does offline, as it did before the online gates (AutoCrew, its damage, its pilots); an
// unknown session state never switches them off for a player who is not online at all.
bool InSession() noexcept {
    const auto* base=image;
    if(!SessionCodeKnown(base))return false;
    __try {
        if(!Readable(base+kGameStatus,sizeof(void*)))return false;
        const auto* manager=At<const unsigned char*>(base,kGameStatus);
        if(!manager || !Readable(manager,kCurrentMode+sizeof(std::int32_t)))return false;
        const auto current=At<std::int32_t>(manager,kCurrentMode);
        const auto count=At<std::uint64_t>(manager,kModeCount);
        if(current<0 || static_cast<std::uint64_t>(current)>=count || count>UINTPTR_MAX/sizeof(void*))return false;
        const auto* table=At<const unsigned char*>(manager,kModeTable);
        const auto* entry=SessionAddress(table,static_cast<std::int64_t>(current)*static_cast<std::int64_t>(sizeof(void*)));
        if(!entry || !Readable(entry,sizeof(void*)))return false;
        const auto* mode=At<const unsigned char*>(entry,0);
        if(!mode || !Readable(mode,kModeRaw+sizeof(void*)))return false;
        const auto* raw=At<const unsigned char*>(mode,kModeRaw);
        if(!raw || !Readable(raw,kRawRelative+sizeof(std::int32_t)))return false;
        const auto relative=At<std::int32_t>(raw,kRawRelative);
        const auto* flag=SessionAddress(raw,static_cast<std::int64_t>(relative)+static_cast<std::int64_t>(kRawOnline));
        if(!flag || !Readable(flag,sizeof(std::int32_t)))return false;
        const auto online=At<std::int32_t>(flag,0)!=0;
        // A replaced or dismantled graph is unknown. Recheck the same borrowed identities without
        // invoking the native function, which would load the singleton a second time unchecked.
        if(image!=base || At<const void*>(base,kGameStatus)!=manager ||
           At<std::int32_t>(manager,kCurrentMode)!=current || At<std::uint64_t>(manager,kModeCount)!=count ||
           At<const void*>(manager,kModeTable)!=table || At<const void*>(entry,0)!=mode ||
           At<const void*>(mode,kModeRaw)!=raw || At<std::int32_t>(raw,kRawRelative)!=relative)return false;
        return online;
    } __except(SessionReadFault(GetExceptionCode())) {return false;}
}

void NetProbe(unsigned char* v) noexcept {
    if(!Cfg().debug)return;
    const bool heli=IsHelicopter(v);
    if(!heli && !IsGround(v))return;
    const bool online=InSession();
    if(!online) {
        if(lastOnline!=0){lastOnline=0;Log("NET session off");}
        return; // no room manager exists to ask whether this machine owns it
    }
    if(!checked) {
        checked=SigsMatch() ? 1 : -1;
        if(checked<0)Log("NET probe off: the session functions do not match docs/online-re.md");
    }
    if(checked<0)return;
    const bool due=Due(v,GetTickCount64());
    if(lastOnline==1 && !due)return;
    __try {
        // A room may close after the public query. This narrow read-only diagnostic boundary covers
        // that release window; startup/offline never enters the room's host / operator functions.
        const bool host=reinterpret_cast<IsHostFn>(image+kIsHost)(nullptr);
        if(!InSession())return;
        if(lastOnline!=1){lastOnline=1;Log("NET session on, this machine %s the host",host ? "is" : "is not");}
        if(due && heli && Readable(v+kTxFast,1))Report(v,host);
        if(due && !heli && Readable(v+kGroundTxPos,16))ReportGround(v,host);
    } __except(SessionReadFault(GetExceptionCode())) {}
}
}  // namespace crew
