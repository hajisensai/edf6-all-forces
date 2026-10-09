// The custom Q mark (qmark.h): the aim's ray, this machine's point mark, the teammates' marks, the HUD's snapshot, the cues.
// Game thread but QMarkViews (the HUD's draw thread: a snapshot under its lock). All addresses are EDF.dll's through the
// helpers it calls (TimeDateStamp 0x678CCB46).
#define _CRT_RAND_S
#include <cstdlib>
#include "crew.h"
#include "qmark.h"
#include "qmark_protocol.h"
#include "qmark_ray.h"
#include "command_identity.h"
#include "jetaudio.h"
#include "memory.h"
#include "npc_mark.h"
#include "support_net.h"
#include "support_soldier.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using qmark_net::kMaxPeers;
constexpr ULONGLONG kResolveMs=500;     // a teammate's mark whose enemy is not found here: looked for again after this
constexpr ULONGLONG kPublishFreshMs=500;

// This machine's point mark (wall clock: shown as long while the game is paused, as the old ring was).
struct OwnPoint { bool on; float at[3]; ULONGLONG until; };
OwnPoint ownPoint{};

// A teammate's (peer's) marks as this machine sees them.
struct Tracked {
    char puid[65];
    unsigned char marker[32],target[32];
    bool looked;                // the IDs above resolved once (or tried: kResolveMs)
    ULONGLONG lookedAt;
    int slot;                   // its player's mission slot, -1: not known (not found, or not the authenticated sender)
    ObjRef enemy;               // the enemy found here (pinned weak reference), empty: not found
    float enemyAt[3];
};
Tracked tracked[kMaxPeers+1]{};
qmark_net::Inbox inbox;
qmark_net::Outbox outbox;
std::uint32_t peerCount=0;
bool netReady=false;

// The HUD's copy.
SRWLOCK viewLock=SRWLOCK_INIT;
QMarkView views[kQMarkViews]{};
int viewCount=0;
ULONGLONG viewWall=0;

// The cues asked for since the last frame (bit per QMarkCue).
unsigned cues=0;
ULONGLONG frameDone=~0ULL;

bool Zero(const unsigned char* id) noexcept { for(int i=0;i<32;++i)if(id[i])return false;return true; }

void Drop(Tracked& t) noexcept { npcmark::Assign(t.enemy,{});t=Tracked{};t.slot=-1; }

// The teammate's marker and target found in this world (one native walk), its slot told only when the marker is the
// player of the machine that sent it (its native PUID that sender's: a peer cannot speak for another).
void Look(Tracked& t,ULONGLONG now) noexcept {
    t.looked=true;t.lookedAt=now;t.slot=-1;
    npcmark::Assign(t.enemy,{});
    MarkIdentities ids{};
    if(!ResolveMarkIdentities(Zero(t.marker) ? nullptr : t.marker,Zero(t.target) ? nullptr : t.target,&ids))return;
    if(ids.player && ids.puid && SupportCommandRequesterMatches(ids.puid,t.puid))t.slot=ids.slot;
    if(ids.target)npcmark::Assign(t.enemy,npcmark::Capture(ids.target.obj));
}

// Where each tracked enemy's lock point is now.
struct SeenAll { std::uint32_t n; };
void SeeTracked(void* ctx,const void* object,const float* aim) {
    const auto& s=*static_cast<const SeenAll*>(ctx);
    for(std::uint32_t p=1;p<=s.n;++p)if(tracked[p].enemy.obj==object)std::memcpy(tracked[p].enemyAt,aim,12);
}

void PlayCues() noexcept {
    const unsigned asked=cues;
    cues=0;
    const float v=Cfg().qmarkVolume;
    if(!asked || !(v>0.0f) || !Cfg().enabled)return;
    __try {
        if(!audio::ClipsReady())return;   // made on their own thread from the first call: the first cue may be missed
        audio::Beat(GameEffectVolume());
        const audio::Heard h{v,v,1.0f,0.0f};
        if(asked&(1u<<static_cast<int>(QMarkCue::own)))audio::PlayOnce(audio::kClipMarkOwn,h);
        else if(asked&(1u<<static_cast<int>(QMarkCue::off)))audio::PlayOnce(audio::kClipMarkOff,h);
        if(asked&(1u<<static_cast<int>(QMarkCue::team)))audio::PlayOnce(audio::kClipMarkTeam,h);
    } __except(EXCEPTION_EXECUTE_HANDLER){}
}

void Publish(ULONGLONG now) noexcept {
    QMarkView v[kQMarkViews]{};
    int n=0;
    const void* enemy=nullptr;
    float at[3];
    if(NpcMarkOwn(&enemy,at))v[n++]=QMarkView{true,true,-1,{at[0],at[1],at[2]}};
    if(ownPoint.on && now<ownPoint.until)v[n++]=QMarkView{true,false,-1,{ownPoint.at[0],ownPoint.at[1],ownPoint.at[2]}};
    else ownPoint.on=false;
    for(std::uint32_t p=1;p<=peerCount && n<kQMarkViews;++p) {
        const Tracked& t=tracked[p];
        const auto& r=inbox.Peer(p);
        if(inbox.Enemy(p,now) && n<kQMarkViews) {
            // Found here: where it is here; found and gone (dead): not shown (its marker lets it go); not found: as sent.
            const bool found=static_cast<bool>(t.enemy);
            if(!found || npcmark::Alive(t.enemy)) {
                const float* e=found ? t.enemyAt : r.state.enemyAt;
                v[n++]=QMarkView{false,true,t.slot,{e[0],e[1],e[2]}};
            }
        }
        if(inbox.Point(p,now) && n<kQMarkViews)v[n++]=QMarkView{false,false,t.slot,{r.state.pointAt[0],r.state.pointAt[1],r.state.pointAt[2]}};
    }
    AcquireSRWLockExclusive(&viewLock);
    std::memcpy(views,v,sizeof(views));viewCount=n;viewWall=now;
    ReleaseSRWLockExclusive(&viewLock);
}

// This machine's state to send: its player's ID, its enemy mark (by ID, else its position alone) and its point.
bool OwnState(qmark_net::State* s,ULONGLONG now) noexcept {
    *s=qmark_net::State{};
    const unsigned char* human=PlayerHuman();
    if(!human || !ReadNativeObjectId(human,s->marker))return false;   // no player in a world here: nothing to say
    const void* enemy=nullptr;
    if(NpcMarkOwn(&enemy,s->enemyAt)) {
        s->enemy=true;
        if(!ReadNativeObjectId(static_cast<const unsigned char*>(enemy),s->target))std::memset(s->target,0,32);
    }
    if(ownPoint.on && now<ownPoint.until) {
        s->point=true;std::memcpy(s->pointAt,ownPoint.at,12);
        const ULONGLONG left=ownPoint.until-now;
        s->pointLeftMs=static_cast<std::uint32_t>(left<qmark_net::kMaxPointMs ? left : qmark_net::kMaxPointMs);
    }
    return true;
}
}  // namespace

bool QMarkOn() noexcept { return npcmark::MarkOn(); }

bool QMarkRay(const unsigned char* human,float* eye,float* dir) noexcept {
    float centre[3];
    if(!CameraRayOf(human,eye,centre) && !CameraRay(eye,centre))return false;
    // The mouse's aim point: the primary player's alone (the flight readouts are its).
    float aimAt[3];
    const float* aim=nullptr;
    if(human && human==PlayerHuman()) {
        PlayerJetReadout j{};
        PlayerHeliReadout h{};
        if(PlayerJetHud(&j)) {
            if(j.rotor && j.heli.aiming){std::memcpy(aimAt,j.heli.aim,12);aim=aimAt;}
            else if(!j.rotor && j.aiming){std::memcpy(aimAt,j.aim,12);aim=aimAt;}
        } else if(PlayerHeliHud(&h) && h.f.aiming){std::memcpy(aimAt,h.f.aim,12);aim=aimAt;}
    }
    return qmark::AimRay(eye,centre,aim,dir);
}

void QMarkSetPoint(const float* at) noexcept {
    ownPoint=OwnPoint{true,{at[0],at[1],at[2]},GetTickCount64()+static_cast<ULONGLONG>(Cfg().qmarkPointSec*1000.0f)};
}

void QMarkPlay(QMarkCue cue) noexcept { cues|=1u<<static_cast<int>(cue); }

void QMarkFrame() noexcept {
    const ULONGLONG frame=GameFrame();
    if(frame==frameDone)return;
    frameDone=frame;
    const ULONGLONG now=GetTickCount64();
    if(!QMarkOn()) {
        ownPoint.on=false;cues=0;
        AcquireSRWLockExclusive(&viewLock);viewCount=0;ReleaseSRWLockExclusive(&viewLock);
        return;
    }
    // The teammates' enemies: found again when their IDs were not; dead ones let go of here (the pin only).
    bool any=false;
    for(std::uint32_t p=1;p<=peerCount;++p) {
        Tracked& t=tracked[p];
        if(!inbox.Enemy(p,now)){if(t.enemy)npcmark::Assign(t.enemy,{});continue;}
        if(!t.looked || (!t.enemy && now-t.lookedAt>=kResolveMs))Look(t,now);
        any=any || static_cast<bool>(t.enemy);
    }
    if(any){SeenAll s{peerCount};VisitLockPoints(&SeeTracked,&s);}
    Publish(now);
    PlayCues();
}

void ResetQMarks() noexcept {
    ownPoint=OwnPoint{};cues=0;
    ResetQMarkNetwork();
    AcquireSRWLockExclusive(&viewLock);viewCount=0;viewWall=0;ReleaseSRWLockExclusive(&viewLock);
}

void UpdateQMarkNetwork(bool ready,std::uint32_t peers,bool (*send)(std::uint32_t,const void*,std::size_t) noexcept,
                        std::uint64_t now) noexcept {
    if(!ready || !send || !peers || peers>kMaxPeers || !QMarkOn()) {
        if(netReady)ResetQMarkNetwork();
        return;
    }
    if(!netReady || peers!=peerCount) {   // a new room or roster: the peer indexes mean other machines now
        ResetQMarkNetwork();
        netReady=true;peerCount=peers;
    }
    qmark_net::State s;
    if(!OwnState(&s,now) || !outbox.Due(s,now))return;
    const qmark_net::State out=outbox.Stamp(s,now);
    unsigned char bytes[qmark_net::kWireSize];
    if(!qmark_net::Encode(out,bytes,sizeof(bytes)))return;
    for(std::uint32_t p=1;p<=peers;++p)send(p,bytes,sizeof(bytes));
}

bool ReceiveQMarkNetwork(std::uint32_t peer,const char* puid,const void* bytes,std::size_t size,std::uint64_t now) noexcept {
    if(!qmark_net::Owned(bytes,size))return false;
    qmark_net::State s;
    if(!netReady || !peer || peer>peerCount || !puid || !std::memchr(puid,0,65) || !QMarkOn() ||
       !qmark_net::Decode(bytes,size,s))return true;   // ours, but nothing to take (malformed, newer, not now)
    qmark_net::Change change=qmark_net::Change::none;
    if(!inbox.Receive(peer,s,now,&change))return true;
    Tracked& t=tracked[peer];
    if(std::strcmp(t.puid,puid) || std::memcmp(t.marker,s.marker,32) || std::memcmp(t.target,s.target,32)) {
        Drop(t);
        strcpy_s(t.puid,puid);std::memcpy(t.marker,s.marker,32);std::memcpy(t.target,s.target,32);
    }
    if(change==qmark_net::Change::marked) {
        QMarkPlay(QMarkCue::team);
        Log("QMARK peer %u marked %s at (%.0f,%.0f,%.0f)",peer,s.enemy ? "an enemy" : "a point",s.enemy ? s.enemyAt[0] : s.pointAt[0],
            s.enemy ? s.enemyAt[1] : s.pointAt[1],s.enemy ? s.enemyAt[2] : s.pointAt[2]);
    }
    return true;
}

void ResetQMarkNetwork() noexcept {
    for(auto& t:tracked)if(t.enemy || t.looked)Drop(t);
    inbox.Reset();
    std::uint32_t origin=0;
    if(rand_s(&origin))origin=static_cast<std::uint32_t>(GetTickCount64());
    outbox.Reset(origin|1u);
    netReady=false;peerCount=0;
}

int QMarkViews(QMarkView* out,int max) noexcept {
    AcquireSRWLockShared(&viewLock);
    int n=0;
    if(viewWall && GetTickCount64()-viewWall<=kPublishFreshMs)for(;n<viewCount && n<max;++n)out[n]=views[n];
    ReleaseSRWLockShared(&viewLock);
    return n;
}
}  // namespace crew
