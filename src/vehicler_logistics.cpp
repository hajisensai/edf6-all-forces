// The Vehicler's logistics points at runtime (vehicler_logistics.h): the host's book (vehicler_logistics_logic.h Ledger),
// a guest's mirror of it, the ZWLG channel (vehicler_logistics_net.h), the call key and the HUD's snapshot.
#include "vehicler_logistics.h"
#include "vehicler_logistics_logic.h"
#include "vehicler_logistics_net.h"
#include "command_identity.h"
#include "config.h"
#include "edf/input_source.h"
#include "edf_layout.h"
#include "layout.h"
#include "map.h"
#include "memory.h"
#include "net_object_ref.h"
#include "net_object_resolve.h"
#include "netprobe.h"
#include "online_authority.h"
#include "plugin_log.h"
#include "plugin_net.h"
#include "scene.h"
#include "seats.h"
#include "soldier_classes_logic.h"
#include "soldier_factory.h"
#include "zw_vehicle_catalog.h"
#include "zw_vehicle_dispatch.h"
#include <cmath>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
// The host sends its book as it changes, no more often than kStateGapMs, and every kStateKeepMs regardless (a message
// lost is mended by the next); a guest sends a credit again every kCreditAgainMs until acked, kCreditTries times at most.
constexpr ULONGLONG kStateGapMs=100,kStateKeepMs=1000,kCreditAgainMs=1500;
constexpr int kCreditTries=8,kCreditsMost=32;
constexpr ULONGLONG kLongPressMs=500;    // the key held this long calls the resupply; shorter picks the next vehicle
constexpr ULONGLONG kNoticeMs=5000;      // a notice's moment on the HUD
constexpr int kNetLogsMost=24;           // the channel's log lines a mission

zwlog::Ledger ledger;      // the host's (offline: this machine's)
zwlog::Mirror mirror;      // a guest's
bool guest=false;          // this machine was a guest at the last tick
bool hooked=false;         // the ledger's hooks given to the dispatch
std::uint32_t sentSerial=0;
ULONGLONG stateAt=0;
int netLogs=0;
struct Credit { bool used; unsigned char victim[32],player[32]; unsigned points; ULONGLONG sentAt; int tries; };
Credit credits[kCreditsMost]{};

// The call key's state (game thread).
struct Key { bool down=false,fired=false; ULONGLONG since=0; int pick=-1; };
Key key;
// The local player's resupply as last seen, for the HUD's notices.
struct Watch { bool pending=false; const unsigned char* owned=nullptr; VehiclerNotice notice=VehiclerNotice::none; ULONGLONG at=0; };
Watch watch;

SRWLOCK hudLock=SRWLOCK_INIT;
VehiclerHudView hud{};
bool hudShown=false;

bool IsGuest() noexcept { return InSession() && !OnlineHostOnly(); }
bool Keeper() noexcept { return !IsGuest(); }   // offline, or the host

#define ZWLG_LOG(...) do { if(netLogs<kNetLogsMost){++netLogs;Log(__VA_ARGS__);} } while(0)

// A player's slot in the book by its reference (zw_vehicle_dispatch.h ZwRequesterRef's): -1 not a player's.
int SlotOf(const unsigned char ref[32]) noexcept {
    int slot=-1;
    return netref::IsPlayer(ref,&slot) && slot>=0 && slot<zwlog::kSlotsMost ? slot : -1;
}
// The book's key of soldier `s`: its slot when it is a Vehicler player (this machine's or another's copy), -1 otherwise.
int KeyOf(const unsigned char* s) noexcept {
    __try {
        if(!s || !Readable(s,kDead+1) || soldier::ZwType(s)!=zwclass::kVehicler)return -1;
        if(InSession() ? !edf::IsAnyPlayer(s) : !edf::IsPlayer(s))return -1;
        unsigned char ref[32];
        return ZwRequesterRef(s,ref) ? SlotOf(ref) : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
}
// Whose kill: zwlog::Attribute over what the shooter is (a soldier, or a vehicle's seats and the one holding `weapon`).
int ShooterKey(unsigned char* shooter,const unsigned char* weapon) noexcept {
    zwlog::Shooter s;
    __try {
        if(!shooter || !Readable(shooter,kDead+1))return -1;
        if(VehicleClassOf(shooter)<0){s.self=KeyOf(shooter);return zwlog::Attribute(s);}
        s.vehicle=true;
        const unsigned seats=edf::SeatCount(shooter);
        s.seats=static_cast<int>(seats<static_cast<unsigned>(zwlog::kSeatsMost) ? seats : zwlog::kSeatsMost);
        for(int i=0;i<s.seats;++i) {
            unsigned char* const seat=edf::SeatAt(shooter,static_cast<unsigned>(i));
            s.rider[i]=edf::LivingSoldierInSeat(image,seat) ? KeyOf(At<const unsigned char*>(seat,kSeatRider)) : -1;
            if(!weapon || s.weaponSeat>=0)continue;
            const auto holders=At<const unsigned char* const*>(seat,kSeatWeapons);
            const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
            if(count>16 || !Readable(holders,count*sizeof(void*)))continue;
            for(std::uint64_t k=0;k<count;++k)
                if(Readable(holders[k],kHolderWeapon+8) && At<const unsigned char*>(holders[k],kHolderWeapon)==weapon)s.weaponSeat=i;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){return -1;}
    return zwlog::Attribute(s);
}

// ---- the channel ----
bool SendMsg(const zwlog::Msg& m,std::uint32_t peer,bool host) noexcept {
    unsigned char bytes[zwlog::kStateWire];
    const std::size_t size=zwlog::WireOf(m.kind);
    if(!zwlog::Encode(m,bytes,size))return false;
    if(host)return plugin_net::SendHost(bytes,size);
    return peer ? plugin_net::Send(peer,bytes,size) : plugin_net::Broadcast(bytes,size)>0;
}
void SendState(std::uint32_t peer) noexcept {
    if(!SendMsg(zwlog::StateOf(ledger),peer,false) && peer)ZWLG_LOG("ZWLG the book not sent to peer %u",peer);
}
void SendCredits(ULONGLONG now) noexcept {
    if(!plugin_net::Ready() || !plugin_net::HostPeer())return;
    for(Credit& c:credits) {
        if(!c.used || (c.tries && now-c.sentAt<kCreditAgainMs))continue;
        if(c.tries>=kCreditTries){ZWLG_LOG("ZWLG a kill's credit (slot %d, %u) never acked: dropped",SlotOf(c.player),c.points);c.used=false;continue;}
        zwlog::Msg m;m.kind=zwlog::Kind::credit;std::memcpy(m.victim,c.victim,32);std::memcpy(m.player,c.player,32);m.points=c.points;
        if(SendMsg(m,0,true)){c.sentAt=now;++c.tries;}
    }
}
// A kill booked here (the keeper's) or queued for the host (a guest's).
void Book(const unsigned char* victimRef,int slot,unsigned points,const char* why) noexcept {
    if(Keeper()) {
        unsigned added=0;
        const zwlog::Booked b=ledger.Book(victimRef,slot,points,&added);
        if(b==zwlog::Booked::kept)Log("ZWLG slot %d +%u (%s): %u now",slot,added,why,ledger.Points(slot));
        else if(b==zwlog::Booked::full)ZWLG_LOG("ZWLG slot %d: no row left in the book: the kill not kept",slot);
        return;
    }
    if(!victimRef){ZWLG_LOG("ZWLG a kill of slot %d settled here with no reference to report it by: not kept",slot);return;}
    for(Credit& c:credits)if(c.used && !std::memcmp(c.victim,victimRef,32))return;   // reported already
    for(Credit& c:credits) {
        if(c.used)continue;
        c=Credit{};c.used=true;std::memcpy(c.victim,victimRef,32);
        netref::MakePlayer(slot,c.player);c.points=points;
        Log("ZWLG slot %d +%u (%s): reported to the host",slot,points,why);
        SendCredits(GetTickCount64());
        return;
    }
    ZWLG_LOG("ZWLG slot %d: %u credits waiting for the host already: this kill not reported",slot,kCreditsMost);
}

bool Receive(std::uint32_t peer,const char*,const void* bytes,std::size_t size,std::uint64_t) noexcept {
    zwlog::Msg m;
    if(!zwlog::Decode(bytes,size,m)){ZWLG_LOG("ZWLG peer %u: a message that does not read (%zu bytes): dropped",peer,size);return true;}
    switch(m.kind) {
    case zwlog::Kind::credit: {
        if(IsGuest() || !plugin_net::Host() || !peer)return true;   // the host's to book
        const int slot=SlotOf(m.player);
        if(slot<0)return true;
        unsigned added=0;
        const zwlog::Booked b=ledger.Book(m.victim,slot,m.points,&added);
        if(b==zwlog::Booked::kept)Log("ZWLG peer %u reports slot %d +%u: %u now",peer,slot,added,ledger.Points(slot));
        zwlog::Msg a;a.kind=zwlog::Kind::ack;std::memcpy(a.victim,m.victim,32);
        SendMsg(a,peer,false);
        return true;
    }
    case zwlog::Kind::ack:
        if(!IsGuest() || peer!=plugin_net::HostPeer())return true;
        for(Credit& c:credits)if(c.used && !std::memcmp(c.victim,m.victim,32))c.used=false;
        return true;
    case zwlog::Kind::state:
        if(!IsGuest() || peer!=plugin_net::HostPeer())return true;   // only the host's word
        if(!mirror.heard)Log("ZWLG the host's book heard: %d rows, gauge %u",m.count,m.gauge);
        zwlog::Hear(mirror,m);
        return true;
    case zwlog::Kind::handed:
    case zwlog::Kind::retired:
        if(!IsGuest() || peer!=plugin_net::HostPeer())return true;
        ZwDeliverHeard(m.kind==zwlog::Kind::retired,m.victim,m.player,(m.points&zwlog::kHandedFollows)!=0);
        return true;
    }
    return true;
}
void Joined(std::uint32_t peer) noexcept { if(plugin_net::Host() && !IsGuest())SendState(peer); }
void ResetNet() noexcept { sentSerial=ledger.serial-1;stateAt=0; }

// ---- the ledger's hooks (zw_vehicle_dispatch.h ZwDeliverLedger: the keeper's) ----
bool Hold(const unsigned char requester[32],unsigned price,std::uint64_t* ticket) noexcept {
    const int slot=requester ? SlotOf(requester) : -1;
    const bool ok=slot>=0 && ledger.Hold(slot,price,ticket);
    Log("ZWLG slot %d: %u held for a resupply%s (%u left)",slot,price,ok ? "" : ": refused (too few points, or one held)",
        slot>=0 ? ledger.Points(slot) : 0u);
    return ok;
}
void Settle(std::uint64_t ticket) noexcept { if(!ledger.Settle(ticket))ZWLG_LOG("ZWLG ticket %llu settled: none such",static_cast<unsigned long long>(ticket)); }
void Refund(std::uint64_t ticket) noexcept {
    const zwlog::Row* r=ledger.OfTicket(ticket);
    const int slot=r ? r->slot : -1;
    if(!ledger.Refund(ticket))ZWLG_LOG("ZWLG ticket %llu given back: none such",static_cast<unsigned long long>(ticket));
    else Log("ZWLG slot %d: the resupply's price given back (%u now)",slot,ledger.Points(slot));
}

// ---- this machine's player ----
int MySlot(const unsigned char* human) noexcept {
    unsigned char ref[32];
    return human && ZwRequesterRef(human,ref) ? SlotOf(ref) : -1;
}
bool KeyDown() noexcept {
    const Config& c=Cfg();
    if(MapHoldsKeys())return false;
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    if(pid!=GetCurrentProcessId())return false;
    if(c.vehiclerLogisticsKey>0 && (edf::AsyncKey(c.vehiclerLogisticsKey)&0x8000))return true;
    std::uint16_t buttons=0;std::uint8_t lt=0,rt=0;
    const int mask=c.vehiclerLogisticsButton;
    return mask>0 && MapPadState(&buttons,&lt,&rt) && (buttons&mask)==mask;
}
// The kinds `unlocked` holds in order after `from` (wrapping; -1 the first), -1 none.
int NextKind(std::uint32_t unlocked,int from) noexcept {
    for(int i=1;i<=kZwVehicleCount;++i) {
        const int k=((from<0 ? -1 : from)+i)%kZwVehicleCount;
        if(k>=0 && (unlocked&(1u<<k)))return k;
    }
    return -1;
}
int Count(std::uint32_t unlocked) noexcept { int n=0;for(int k=0;k<kZwVehicleCount;++k)n+=(unlocked>>k)&1u;return n; }
void Notice(VehiclerNotice n) noexcept { watch.notice=n;watch.at=GetTickCount64(); }

void ShortName(int kind,wchar_t* out,std::size_t size) noexcept {
    out[0]=0;
    const ZwVehicleSpec* v=ZwVehicleInfo(kind);
    if(!v)return;
    const wchar_t* name=v->row;   // ZW_V1_HSTVL: after the second '_'
    for(int under=0;*name && under<2;++name)if(*name==L'_')++under;
    swprintf_s(out,size,L"%ls",*name ? name : v->row);
}

void Frame(unsigned char* human,int slot,unsigned points,bool transit) noexcept {
    const ULONGLONG now=GetTickCount64();
    const std::uint32_t unlocked=ZwUnlockedKinds(0);
    if(key.pick<0 || !(unlocked&(1u<<key.pick))) {
        const int mine=ZwMissionVehicle(0);
        key.pick=mine>=0 && (unlocked&(1u<<mine)) ? mine : NextKind(unlocked,-1);
    }
    // The key: a long press calls once, a short one (released before kLongPressMs) picks the next vehicle.
    const bool down=KeyDown();
    if(down && !key.down){key.since=now;key.fired=false;}
    if(down && !key.fired && now-key.since>=kLongPressMs) {
        key.fired=true;
        const unsigned price=ZwVehiclePrice(key.pick);
        if(key.pick<0)Log("ZWLG the call key: no vehicle unlocked to call");
        else if(transit || ZwDeliverPending(human))Log("ZWLG the call key: a resupply is on its way already");
        else if(points<price){Log("ZWLG the call key: %u of %u points",points,price);Notice(VehiclerNotice::poor);}
        else {
            float at[3]{};
            __try {
                if(Readable(human,kMatrix+64))std::memcpy(at,human+kMatrix+48,sizeof(at));
            } __except(EXCEPTION_EXECUTE_HANDLER){}
            if(!RequestZwDeliver(human,key.pick,at))Notice(VehiclerNotice::refused);
        }
    }
    if(!down && key.down && !key.fired && now-key.since<kLongPressMs) {
        const int next=NextKind(unlocked,key.pick);
        if(next>=0 && next!=key.pick){key.pick=next;Log("ZWLG the call key: %ls picked",kZwVehicles[next].row);}
    }
    key.down=down;
    // The notices: the resupply ended as the player's new vehicle (arrived) or without one (refused).
    const bool pending=ZwDeliverPending(human);
    const unsigned char* owned=ZwOwnedVehicle(human);
    if(watch.pending && !pending)Notice(owned && owned!=watch.owned ? VehiclerNotice::arrived : VehiclerNotice::refused);
    if(!pending)watch.owned=owned;
    watch.pending=pending;
    if(watch.notice!=VehiclerNotice::none && now-watch.at>=kNoticeMs)watch.notice=VehiclerNotice::none;
    // The snapshot.
    VehiclerHudView v;
    v.points=points;v.gauge=Keeper() ? ledger.gauge : mirror.gauge;
    if(!v.gauge)v.gauge=kZwLogisticsGauge;
    v.price=ZwVehiclePrice(key.pick);
    ShortName(key.pick,v.name,_countof(v.name));
    v.choices=Count(unlocked);
    v.heard=Keeper() || mirror.heard;
    v.transit=transit || pending;
    if(unsigned char* const coming=ZwDeliverVehicle(human)) {
        __try {
            if(Readable(coming,kMatrix+64) && Readable(human,kMatrix+64)) {
                const float* a=reinterpret_cast<const float*>(coming+kMatrix+48);
                const float* b=reinterpret_cast<const float*>(human+kMatrix+48);
                const float dx=a[0]-b[0],dz=a[2]-b[2];
                v.distance=std::sqrt(dx*dx+dz*dz);
            }
        } __except(EXCEPTION_EXECUTE_HANDLER){}
    }
    v.notice=watch.notice;
    v.key=Cfg().vehiclerLogisticsKey;v.button=Cfg().vehiclerLogisticsButton;
    (void)slot;
    AcquireSRWLockExclusive(&hudLock);
    hud=v;hudShown=true;
    ReleaseSRWLockExclusive(&hudLock);
}
void Hide() noexcept {
    AcquireSRWLockExclusive(&hudLock);
    hudShown=false;
    ReleaseSRWLockExclusive(&hudLock);
}
}  // namespace

plugin_net::Channel VehiclerLogisticsNetChannel() noexcept {
    plugin_net::Channel c;
    c.name="vehicler logistics";c.magic=zwlog::kMagic;c.version=zwlog::kVersion;
    c.minSize=zwlog::kCreditWire;c.maxSize=zwlog::kStateWire;
    c.receive=&Receive;c.reset=&ResetNet;c.joined=&Joined;
    return c;
}

bool VehiclerLogisticsWanted() noexcept {
    if(!Cfg().enabled)return false;
    if(InSession())return true;
    const unsigned char* const me=PlayerHuman();
    return me && soldier::ZwType(me)==zwclass::kVehicler;
}

void VehiclerKill(unsigned char* victim,unsigned char* shooter,const unsigned char* weapon,bool enemy,bool part) noexcept {
    zwlog::VictimInfo info;info.enemy=enemy;info.part=part;
    const unsigned points=zwlog::Credit(info);
    if(!points || !victim)return;
    const int slot=ShooterKey(shooter,weapon);
    if(slot<0)return;
    unsigned char ref[32]{};
    const bool named=InSession() && EncodeNetRef(static_cast<const void*>(victim),ref) && netref::IsNative(ref);
    Book(named ? ref : nullptr,slot,points,weapon ? "its weapon's round" : "its attack");
}

void VehiclerSendDelivery(bool retired,const unsigned char hull[32],const unsigned char courier[32],bool follows) noexcept {
    if(!InSession() || IsGuest() || !hull || !courier)return;
    zwlog::Msg m;
    m.kind=retired ? zwlog::Kind::retired : zwlog::Kind::handed;
    std::memcpy(m.victim,hull,32);std::memcpy(m.player,courier,32);
    m.points=!retired && follows ? zwlog::kHandedFollows : 0u;
    if(!SendMsg(m,0,false))ZWLG_LOG("ZWLG the delivery's word (%s) not sent",retired ? "retired" : "handed");
}

void VehiclerLogisticsTick() noexcept {
    if(!hooked) {
        ZwDeliverLedger l;l.hold=&Hold;l.settle=&Settle;l.refund=&Refund;
        SetZwDeliverLedger(l);hooked=true;
    }
    const ULONGLONG now=GetTickCount64();
    // The keeper moves: a guest that hosts now starts from the host's last word; a host that is a guest now waits for its.
    const bool nowGuest=IsGuest();
    if(nowGuest!=guest) {
        if(!nowGuest){zwlog::Adopt(ledger,mirror);Log("ZWLG this machine keeps the book now (from the host's last word: %d rows)",mirror.count);}
        else {mirror.Start();Log("ZWLG the book is the host's now");}
        for(Credit& c:credits) {   // the credits not acked: booked here now, or sent to the new host
            if(!c.used || nowGuest)continue;
            const int slot=SlotOf(c.player);
            if(slot>=0)ledger.Book(c.victim,slot,c.points);
            c.used=false;
        }
        guest=nowGuest;
    }
    if(!nowGuest) {
        for(const zwlog::Row& r:ledger.rows) {   // the transit flags the guests' HUD shows
            if(r.slot<0)continue;
            unsigned char ref[32];
            if(netref::MakePlayer(r.slot,ref))ledger.Transit(r.slot,ZwDeliverInTransit(ref));
        }
        if(InSession() && plugin_net::Ready() && plugin_net::Host() && plugin_net::PeerCount()) {
            const bool changed=sentSerial!=ledger.serial;
            if((changed && now-stateAt>=kStateGapMs) || now-stateAt>=kStateKeepMs){SendState(0);sentSerial=ledger.serial;stateAt=now;}
        }
    }
    else SendCredits(now);
    // This machine's player: the key and the HUD, only a Vehicler's.
    unsigned char* const human=PlayerHuman();
    if(!Cfg().enabled || !human || soldier::ZwType(human)!=zwclass::kVehicler){Hide();return;}
    const int slot=MySlot(human);
    unsigned points=0;bool transit=false;
    if(!nowGuest)points=slot>=0 ? ledger.Points(slot) : 0u;
    else if(const zwlog::MirrorRow* r=slot>=0 ? mirror.Find(slot) : nullptr){points=r->points;transit=(r->flags&zwlog::kRowTransit)!=0;}
    Frame(human,slot,points,transit);
}

void ResetVehiclerLogistics() noexcept {
    ledger.Start(kZwLogisticsGauge);
    mirror.Start();
    for(Credit& c:credits)c=Credit{};
    key=Key{};watch=Watch{};
    guest=IsGuest();sentSerial=ledger.serial-1;stateAt=0;netLogs=0;
    Hide();
}

bool VehiclerHudNow(VehiclerHudView* out) noexcept {
    if(!out)return false;
    AcquireSRWLockShared(&hudLock);
    const bool shown=hudShown;
    if(shown)*out=hud;
    ReleaseSRWLockShared(&hudLock);
    return shown;
}
}  // namespace crew
