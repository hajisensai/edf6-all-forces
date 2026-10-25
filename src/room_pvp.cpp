// The room's PvP setup (room_pvp.h, the pure side room_pvp_logic.h / room_pvp_dialog.h). EDF.dll TimeDateStamp
// 0x678CCB46, RVAs.
//
// The start gate (static RE, 2026-10-25): the room's ready handshake 0x73D960 (driven from the room's OnUpdate at
// 0x8FE3D9) counts every member's ready mark and, on the host, publishes the start list (lobby attributes l1 / l2 at
// 0x73E808 / 0x73E81E) that every machine then starts from. Whether this machine may publish is the one call at 0x73E3E9
// (IsLocalHost 0x12BE580(lobby), its result read only at 0x73E648). That call goes through StartGate: the host publishes
// only once every member acknowledged the lock; until then the handshake stays as it is (state 2, retried each frame),
// the host's ready button still cancels, and the guests have been sent nothing.
#include "room_pvp.h"
#include "coop_attr.h"
#include "crew.h"
#include "memory.h"
#include "mod_room.h"
#include <cstring>

namespace crew {
namespace {
using namespace roompvp;
constexpr std::uint32_t kGateCall=0x73E3E9,kIsLocalHost=0x12BE580,kGateUse=0x73E648;
constexpr unsigned char kGateCallSig[]={0x48,0x8B,0xCF,0xE8,0x92,0x01,0xB8,0x00,0x88,0x44,0x24,0x30};   // at kGateCall-3
constexpr unsigned char kIsLocalHostSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x30,0x0F,
                                           0x57,0xC0,0x0F,0x11,0x44,0x24,0x20,0x48,0x8B,0x89,0x60,0x01,0x00,0x00};
constexpr unsigned char kGateUseSig[]={0x80,0x7C,0x24,0x30,0x00,0x0F,0x84};
using IsLocalHostFn=bool(__fastcall*)(void*);
bool gateInstalled=false;

// UI thread (the room's frame and the handshake inside it).
pm::Pick mine=DefaultPick();
Settings hostSettings{};          // what this machine sets when it is the host
coopattr::Publisher selPub{kSelKey},setPub{kSetKey},lockPub{kLockKey},ackPub{kAckKey};
struct Room {
    bool online=false,host=false;
    char self[65]{};
    Settings settings{};   // the host's (this machine's own when it is the host)
    bool hasLock=false;Lock lock{};   // the host's published lock (the host: its own)
    int count=0;Member members[kMaxMembers]{};
    bool acked[kMaxMembers]{};
};
Room room{};
std::uint32_t nextSerial=1;
std::int64_t lastContent=-1,lastMission=-1;   // the room's mission last seen (a PvP preset map turns PvP on)
bool holding=false;   // the host's start is held this frame
// The lock let through, for the mission (any thread).
SRWLOCK matchLock=SRWLOCK_INIT;
bool hasMatch=false;Lock match{};

struct Collect { Room* r; };
void EDF6COOP_MA_CALL EachSel(void* context,const EDF6CoopMemberValue* m) {
    Room& r=*static_cast<Collect*>(context)->r;
    if(!m || !m->memberId || r.count>=static_cast<int>(kMaxMembers))return;
    Member& out=r.members[r.count++];
    out=Member{};
    strncpy_s(out.id,m->memberId,_TRUNCATE);
    out.host=m->isHost!=0;
    out.hasPick=m->hasValue && m->value && DecodePick(m->value,&out.pick);
}
struct Acks { Room* r; char text[kMaxMembers][64]; bool has[kMaxMembers]; };
void EDF6COOP_MA_CALL EachAck(void* context,const EDF6CoopMemberValue* m) {
    auto& a=*static_cast<Acks*>(context);
    if(!m || !m->memberId)return;
    for(int i=0;i<a.r->count;++i)if(!std::strcmp(a.r->members[i].id,m->memberId)) {
        a.has[i]=m->hasValue && m->value;
        if(a.has[i])strncpy_s(a.text[i],m->value,_TRUNCATE);
    }
}

void Publish(const coopattr::Table* t,coopattr::Publisher& p,const char* value) noexcept {
    std::uint32_t why=0;
    if(p.Publish(t,value,&why)==coopattr::Publisher::Result::refused)Log("ROOMPVP EDF6Coop refused %s (reason %u)",p.key,why);
}

// One read of the room: members and their picks, the host's settings and lock, every acknowledgement.
void Read(const coopattr::Table* t) noexcept {
    const EDF6CoopRoomState rs=coopattr::Room();
    Room next{};
    next.online=rs.inRoom!=0;next.host=rs.isHost!=0;
    strncpy_s(next.self,rs.self,_TRUNCATE);
    if(next.online) {
        Collect c{&next};
        t->snapshot(kSelKey,&EachSel,&c,nullptr);
        if(next.host)next.settings=hostSettings;
        else {
            coopattr::Value v;
            if(coopattr::HostValue(t,kSetKey,v) && v.has)DecodeSettings(v.text,&next.settings);
        }
        coopattr::Value lv;
        if(next.host){next.hasLock=room.hasLock;next.lock=room.lock;}
        else if(coopattr::HostValue(t,kLockKey,lv) && lv.has)next.hasLock=DecodeLock(lv.text,&next.lock);
        Acks a{&next,{},{}};
        t->snapshot(kAckKey,&EachAck,&a,nullptr);
        for(int i=0;i<next.count;++i)next.acked[i]=next.members[i].host || (a.has[i] && next.hasLock && AckMatches(a.text[i],next.lock));
    }
    room=next;
}
}  // namespace

void RoomPvpTick() noexcept {
    const coopattr::Table* t=coopattr::Api();
    if(!t){selPub.Forget();setPub.Forget();lockPub.Forget();ackPub.Forget();room=Room{};return;}
    char value[kValueMost];
    EncodePick(mine,value,sizeof(value));
    Publish(t,selPub,value);
    Read(t);
    if(!room.online)return;
    if(room.host) {
        // A PvP preset map (the test range pack's rows after the range itself, testrange/gen.py PVP_PLANS) turns PvP on
        // when the host picks it, any other map off again; between picks the host's own switch stands.
        std::int64_t content=-1,mission=-1;
        if(PublishedRoomMission(&content,&mission) && (content!=lastContent || mission!=lastMission)) {
            const bool preset=Cfg().testRangeContent>0 && content==Cfg().testRangeContent && mission>=1;
            const bool wasPreset=Cfg().testRangeContent>0 && lastContent==Cfg().testRangeContent && lastMission>=1;
            if(preset!=wasPreset || lastContent<0) {
                hostSettings.enabled=preset;
                Log("ROOMPVP the room's mission is content %lld row %lld: PvP %s",static_cast<long long>(content),
                    static_cast<long long>(mission),preset ? "on (a PvP preset map)" : "off");
            }
            lastContent=content;lastMission=mission;
        }
        EncodeSettings(hostSettings,value,sizeof(value));
        Publish(t,setPub,value);
    } else if(room.hasLock) {
        // A member acknowledges the host's lock only when it holds this member's current pick: a pick changed since makes
        // the host lock again.
        for(int i=0;i<room.lock.count;++i)if(!std::strcmp(room.lock.members[i].id,room.self)) {
            const pm::Pick& p=room.lock.members[i].pick;
            if(p.faction==mine.faction && p.role==mine.role && p.unit==mine.unit) {
                EncodeAck(room.lock,value,sizeof(value));
                Publish(t,ackPub,value);
                // The lock this member acknowledged: what the mission preloads (every member's unit, debug_spawn.cpp).
                AcquireSRWLockExclusive(&matchLock);match=room.lock;hasMatch=true;ReleaseSRWLockExclusive(&matchLock);
            }
        }
    }
    holding=false;
}

namespace {
// The host's gate, called by the handshake each frame something is pending: false holds the start.
bool StartAllowed() noexcept {
    if(!room.online || !room.host || !hostSettings.enabled) {   // no PvP: the room starts as it always did
        AcquireSRWLockExclusive(&matchLock);hasMatch=false;ReleaseSRWLockExclusive(&matchLock);
        return true;
    }
    const coopattr::Table* t=coopattr::Api();
    if(!t)return true;
    holding=true;
    Lock want=BuildLock(hostSettings,room.members,room.count);
    if(!room.hasLock || !SameMatch(want,room.lock)) {
        want.serial=nextSerial++;
        char value[kValueMost];
        if(!EncodeLock(want,value,sizeof(value))){Log("ROOMPVP the lock does not fit a member attribute: the start is held");return false;}
        Publish(t,lockPub,value);
        room.lock=want;room.hasLock=true;
        for(auto& a:room.acked)a=false;
        Log("ROOMPVP lock %u published (%d members): the start waits for every member's acknowledgement",want.serial,want.count);
        return false;
    }
    for(int i=0;i<room.count;++i)if(!room.acked[i])return false;
    AcquireSRWLockExclusive(&matchLock);match=room.lock;hasMatch=true;ReleaseSRWLockExclusive(&matchLock);
    Log("ROOMPVP lock %u acknowledged by every member: the start goes on",room.lock.serial);
    return true;
}
bool __fastcall StartGate(void* lobby) noexcept {
    bool host=false;
    __try { host=reinterpret_cast<IsLocalHostFn>(image+kIsLocalHost)(lobby); } __except(EXCEPTION_EXECUTE_HANDLER){ return false; }
    return host && StartAllowed();
}
}  // namespace

bool InstallRoomPvp() noexcept {
    __try {
        if(!Matches(kGateCall-3,kGateCallSig,sizeof(kGateCallSig)) || !Matches(kIsLocalHost,kIsLocalHostSig,sizeof(kIsLocalHostSig)) ||
           !Matches(kGateUse,kGateUseSig,sizeof(kGateUseSig))) {
            Log("ROOMPVP start gate refused: unexpected EDF.dll code (the room starts without the PvP lock)");
            return false;
        }
        bool changed=false;
        gateInstalled=RedirectCall(image+kGateCall,image+kIsLocalHost,reinterpret_cast<void*>(&StartGate),changed);
        Log("ROOMPVP start gate=%d",gateInstalled);
        return gateInstalled;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void RoomPvpLeave() noexcept { room=Room{};holding=false; }

View RoomPvpView() noexcept {
    View v;
    v.host=room.host;v.online=room.online;v.settings=room.host ? hostSettings : room.settings;v.mine=mine;
    v.locking=room.host && room.hasLock && hostSettings.enabled && [&]{for(int i=0;i<room.count;++i)if(!room.acked[i])return true;return false;}();
    for(int i=0;i<room.count && v.count<static_cast<int>(kMaxMembers);++i) {
        Line& l=v.lines[v.count++];
        l.ordinal=i;l.host=room.members[i].host;l.self=!std::strcmp(room.members[i].id,room.self);
        l.hasPick=room.members[i].hasPick;l.pick=l.self ? mine : room.members[i].pick;if(l.self)l.hasPick=true;
        l.acked=room.acked[i];
    }
    return v;
}

void RoomPvpChange(const Action& a) noexcept {
    switch(a.change) {
    case Change::mode:if(room.host)hostSettings.enabled=a.enabled;break;
    case Change::preset:if(room.host && a.preset>=0 && a.preset<pm::kPresetCount)hostSettings.relations.FromPreset(static_cast<pm::Preset>(a.preset));break;
    case Change::pick:if(pm::PickValid(a.pick))mine=a.pick;break;
    case Change::none:break;
    }
}

void RoomPvpSummary(wchar_t* out,std::size_t n) noexcept {
    if(!out || !n)return;
    if(!room.online){out[0]=0;return;}
    rd::detail::Put(out,n,hudtext::Tr(Tx::pvpRoomEntry),hudtext::Tr(FactionText(mine.faction)),hudtext::Tr(RoleText(mine.role)));
}

// For EDF6Coop's room member rows (their faction beside each name): the member's faction as this room reads it, in the
// HUD's language, and its colour (0xAARRGGBB). 1 written; 0 when the room has no PvP on, the member is unknown or the
// arguments are bad (`out` then ""). UI thread (the room screen's frame, where EDF6Coop lays its rows out).
extern "C" __declspec(dllexport) int EDF6AF_PvpMemberLabel(const char* memberId,wchar_t* out,std::uint32_t outChars,
                                                           std::uint32_t* argb) noexcept {
    if(!out || !outChars)return 0;
    out[0]=0;
    if(!memberId || !room.online || !(room.host ? hostSettings.enabled : room.settings.enabled))return 0;
    for(int i=0;i<room.count;++i)if(!std::strcmp(room.members[i].id,memberId)) {
        const bool self=!std::strcmp(memberId,room.self);
        const pm::Pick p=self ? mine : room.members[i].hasPick ? room.members[i].pick : DefaultPick();
        rd::detail::Put(out,outChars,L"%ls / %ls%ls",hudtext::Tr(FactionText(p.faction)),hudtext::Tr(RoleText(p.role)),
                        room.acked[i] && room.hasLock ? hudtext::Tr(Tx::pvpRoomAcked) : L"");
        if(argb)*argb=pm::kFactions[p.faction].color;
        return 1;
    }
    return 0;
}

bool RoomPvpMatch(pvp_protocol::State* out,std::uint32_t* serial) noexcept {
    AcquireSRWLockShared(&matchLock);
    const bool ok=hasMatch && match.settings.enabled;
    if(ok){if(out)*out=MatchState(match);if(serial)*serial=match.serial;}
    ReleaseSRWLockShared(&matchLock);
    return ok;
}
}  // namespace crew
