// EDF6AutoTurret: a Vehicle603_Flak whose guns carry our mark (LockonTargetType kMarkAir / kMarkGround,
// turret.h) slews its turret onto an enemy inside its tracking range by itself: anti-air guns prefer air
// targets, ground-attack guns (the Bohr's grenade launchers) ground ones. The aim solves the round's
// ballistic arc (a lofted launcher's, the Katyusha's, the high one). The rider keeps the trigger, and aims by hand
// while holding the stick; a lofted launcher the player rides is not steered at all (PlayerLofted).
// Enemies come straight from the game's lock-target registry (every lockable enemy, all around),
// not from the guns' lock lists, which only cover the front hemisphere and churn.
// It also time-fuses the anti-air shells to the target's range and proximity-fuses them near any enemy of
// the side that fired them. The guns themselves are ordinary no-lock guns (LockonType 0): they fire in the
// stock game with or without this plugin, which only adds the aim and the fuses.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46; see docs/re-notes.md.
#include <Windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#pragma warning(push)
#pragma warning(disable:4201)
#include "PluginAPI.h"
#pragma warning(pop)
#include "edf/host.h"
#include "append_log.h"
#include "turret.h"

namespace autoturret {
unsigned char* image=nullptr;
HMODULE module=nullptr;
wchar_t logPath[MAX_PATH]{};
wchar_t iniPath[MAX_PATH]{};

Config cfg{};
edf::IniWatch ini{};

VehicleInputFn nextInput[2]{};   // the flak's (0) and the rocket launcher's (1) input slots, as found

// GrenadeBullet01 (the flak round): slot 1 deleting dtor, slot 5 per-frame update.
constexpr unsigned kGrenadeVtable=0x17A17E0,kGrenadeDtor=0x265B10,kGrenadeUpdate=0x264AB0;
constexpr std::size_t kGrenadeDtorSlot=1,kGrenadeUpdateSlot=5;
// Weapon slot 17 = "round spawned" (weapon, bullet), called once per round by fire 0x696FD0.
constexpr std::size_t kSpawnSlot=17;
// Bullet: weak-this control block, and the flight control block C at +0x140 with its flags,
// age/lifetime in frames (expires when age >= lifetime, 0x236899), position, velocity (m/s)
// and the stuck-to-something byte.
constexpr std::size_t kBulletWeakCtrl=0x30,kCtl=0x140;
constexpr std::size_t kCtlFlags=0xAF4,kCtlAge=0xAF8,kCtlAlive=0xA08,kCtlPos=0xB80,kCtlVel=0xB90,kCtlStuck=0xC00;
// Blast radius: the damage sphere uses C+0x788 (copied from AmmoExplosion at C+0xA20 at spawn,
// 0x2320A9), while the burst effect is drawn with size C+0xA20/5 (0x264B92). Scaling only C+0xA20
// right before the burst enlarges the fireball without touching the damage.
constexpr std::size_t kCtlBlast=0x788,kCtlBlastVisual=0xA20;
constexpr std::uint32_t kRoundDead=0x1,kRoundBurstOnExpiry=0x20;
constexpr float kStoppedSpeed=60.0f;     // m/s; the flak leaves the barrel at ~480
constexpr ULONGLONG kEnemyMs=150;        // the world snapshot the proximity fuse trusts is at most this old

using SpawnFn=void(__fastcall*)(void*,void*);
using UpdateFn=void(__fastcall*)(void*,void*);
using DtorFn=void*(__fastcall*)(void*,unsigned);
UpdateFn originalUpdate=nullptr;
DtorFn originalDtor=nullptr;
// The weapon classes whose spawn slot is routed through SpawnHook (one per class of flak-round gun: the
// mod's flak guns are all one class, so this is a margin, not a limit anyone should meet).
struct SpawnPatch { const void* vtable; SpawnFn original; };
constexpr int kMaxSpawnPatches=4;
SpawnPatch spawnPatches[kMaxSpawnPatches]{};
bool spawnFullLogged=false;
bool proximityReady=false;

// Rounds fired by our flak guns, keyed by address and the bullet's own weak-this control block (a new
// round at a freed one's address is another round): the fuse its gun had when it was fired (frames, applied
// on its first update, kNoFuse: none or already applied), the team of the vehicle that fired it, and when.
// Bullet updates may run on several threads, hence the lock.
constexpr std::int32_t kNoFuse=-1,kNoTeam=-1;
struct Round { const void* bullet; const void* ctrl; std::int32_t fuse; std::int32_t team; ULONGLONG at; };
constexpr int kMaxRounds=128;
Round rounds[kMaxRounds]{};
SRWLOCK roundLock=SRWLOCK_INIT;

// The time fuse and the team each flak gun's rounds get, as the gun's vehicle stamped them on its input this
// frame (NoteGuns): read by SpawnHook for the round the gun fires. An entry counts only when stamped this
// game frame or the last (Fresh), so a gun no longer stamped (its vehicle gone, the weapon freed and its
// address reused) is never matched: nothing is kept past its frame, and no weapon field is ever written.
struct GunFuse { const void* weapon; std::int32_t fuse; std::int32_t team; ULONGLONG frame; };
constexpr int kMaxGunFuses=64;
GunFuse gunFuses[kMaxGunFuses]{};
SRWLOCK gunLock=SRWLOCK_INIT;
bool gunsFullLogged=false;

// The frame counter (SeeVehicle).
constexpr int kFrameSeen=64;
ULONGLONG frame=1;
const void* frameSeen[kFrameSeen]{};
int frameSeenCount=0;

// The world's lock points this frame (RefreshWorld; turret.h kMaxWorld).
Enemy worldEnemies[kMaxWorld]{};
int worldCount=0;
ULONGLONG worldFrame=0,worldAt=0;
bool worldFullLogged=false;

// Tracks by (vehicle, seat): a slot is free when unused, its vehicle is gone (another weak-this control
// block at the address, or none) or it has not been steered for kTrackIdleMs.
constexpr int kMaxTracks=64;
constexpr ULONGLONG kTrackIdleMs=10000;
Track tracks[kMaxTracks]{};
bool tracksFullLogged=false;

// Once-a-second snapshot of where Steer stopped, for Debug=1.
struct Diag {
    ULONGLONG at;
    unsigned calls,ridden;
    unsigned weapons,enemies,registry;
    unsigned tagged,proximity,contact,evicted;
    float nearest;         // closest any tagged round came to an enemy aim point, metres
    const char* stop;
};
Diag diag{};

// Data built before 0.3.0 (turret.h kLegacyLockonType): fire-start (0x690BB0) refuses a lock-on weapon with
// an empty lock list unless LockonType is 0 or 5, and those guns are type 4 with LockonRange 0, so they only
// fire with "cmp eax,5 / je" turned into "cmp eax,4 / jae". Patched only once such a gun is seen, so an
// install with current data never has the game's fire gate changed. Remove with kLegacyLockonType.
constexpr std::size_t kFireGate=0x690C2E;
constexpr unsigned char kFireGateStock[]={0x83,0xF8,0x05,0x74,0x23};
constexpr unsigned char kFireGateFree[]={0x83,0xF8,0x04,0x73,0x23};
bool legacySeen=false;

void Log(const char* format,...) noexcept {
    if(!logPath[0])return;
    char text[1000]{};va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
    SYSTEMTIME t{};GetLocalTime(&t);
    char line[1100];
    const int n=_snprintf_s(line,sizeof(line),_TRUNCATE,"[%02u:%02u:%02u] %s\r\n",t.wHour,t.wMinute,t.wSecond,text);
    static edf::AppendLog log;
    if(n>0)log.Write(logPath,line,static_cast<DWORD>(n));
}

void SeeVehicle(const void* vehicle) noexcept {
    for(int i=0;i<frameSeenCount;++i)
        if(frameSeen[i]==vehicle){++frame;frameSeenCount=0;break;}
    if(frameSeenCount<kFrameSeen)frameSeen[frameSeenCount++]=vehicle;
}
ULONGLONG Frame() noexcept { return frame; }

// The object at `obj` is still the one whose weak-this control block was `ctrl`, and alive. Under __try.
bool Same(const void* obj,const void* ctrl) noexcept {
    if(!ctrl || !Readable(obj,kSelfCtrl+8) || At<const void*>(obj,kSelfCtrl)!=ctrl)return false;
    return Readable(ctrl,edf::kCtrlUses+4) && At<std::int32_t>(ctrl,edf::kCtrlUses)>0;
}

Track* TrackFor(const unsigned char* vehicle,unsigned seat,bool player) noexcept {
    const auto ctrl=At<const void*>(vehicle,kSelfCtrl);
    const auto now=GetTickCount64();
    Track* free=nullptr;
    Track* npc=nullptr;   // the least recently refreshed NPC seat's: what a player's seat may take
    for(auto& t:tracks) {
        if(t.vehicle==vehicle && t.ctrl==ctrl && t.seat==seat){t.player=player;return &t;}
        if(!free && (!t.vehicle || now-t.at>kTrackIdleMs || !Same(t.vehicle,t.ctrl)))free=&t;
        if(!t.player && (!npc || t.at<npc->at))npc=&t;
    }
    if(!free && player && npc) {
        Log("TRACK all %d tracks in use: the player's seat (v=%p seat=%u) takes NPC v=%p seat=%u's",kMaxTracks,vehicle,seat,
            npc->vehicle,npc->seat);
        free=npc;
    }
    if(!free) {
        if(!tracksFullLogged){tracksFullLogged=true;Log("TRACK all %d tracks in use: v=%p seat=%u left stock",kMaxTracks,vehicle,seat);}
        return nullptr;
    }
    *free=Track{};free->vehicle=vehicle;free->ctrl=ctrl;free->seat=seat;free->at=now;free->player=player;
    return free;
}

bool Finite(const unsigned char* base,std::size_t offset,float* out) noexcept {
    for(int i=0;i<3;++i){out[i]=At<float>(base,offset+i*4);if(!std::isfinite(out[i]))return false;}
    return true;
}

// Target position in the vehicle's frame, measured from the turret pivot.
void ToLocal(const unsigned char* vehicle,const float* world,float* local) noexcept {
    const float* m=reinterpret_cast<const float*>(vehicle+kMatrix);
    const float d[3]={world[0]-m[12],world[1]-m[13],world[2]-m[14]};
    local[0]=Dot(d,m);local[1]=Dot(d,m+4)-cfg.pivotHeight;local[2]=Dot(d,m+8);
}

const std::int32_t* Relations(std::int32_t team) noexcept {
    if(team<0 || team>=kMaxTeam)return nullptr;
    const auto manager=At<const unsigned char*>(image,kTeams);
    if(!Readable(manager,kTeamArray+8))return nullptr;
    const auto rows=At<const unsigned char*>(manager,kTeamArray);
    if(!Readable(rows+team*kTeamStride,kTeamStride))return nullptr;
    const auto relation=At<const std::int32_t*>(rows+team*kTeamStride,kTeamRelation);
    return Readable(relation,kMaxTeam*4) ? relation : nullptr;
}

// Takes the world snapshot once a frame (the first scan of the frame). Game thread, under the caller's
// __try; the registry is the game's own and only changes on this thread.
void RefreshWorld() noexcept {
    if(worldFrame==frame)return;
    worldFrame=frame;worldAt=GetTickCount64();worldCount=0;
    const auto registry=At<const unsigned char*>(image,kRegistry);
    if(!Readable(registry,kRegList+0x10))return;
    const auto head=At<const unsigned char*>(registry,kRegList);
    if(!Readable(head,0x10))return;
    int n=0;
    for(auto node=At<const unsigned char*>(head,0);node!=head && n<kMaxNodes;node=At<const unsigned char*>(node,0),++n) {
        const auto target=At<const unsigned char*>(node,kNodeTarget);
        if(!target || target[0]!=0 || !target[kTargetValid] || !target[kTargetLockable])continue;
        const auto object=At<const unsigned char*>(target,kTargetObject);
        if(!object || object[kDead])continue;
        const auto team=At<std::int32_t>(object,kTeam);
        if(team<0 || team>=kMaxTeam)continue;
        if(worldCount>=kMaxWorld) {
            if(!worldFullLogged){worldFullLogged=true;Log("ENEMIES more than %d lock points: the rest are not seen this frame",kMaxWorld);}
            break;
        }
        Enemy& e=worldEnemies[worldCount];
        if(!Finite(target,kTargetAim,e.pos) || !Finite(object,kPosition,e.origin))continue;
        e.object=object;e.team=team;++worldCount;
    }
    diag.registry+=static_cast<unsigned>(n);
}

const Enemy* World(int* count) noexcept {
    RefreshWorld();
    *count=worldCount;
    return worldEnemies;
}

void ScanEnemies(const unsigned char* vehicle,float range,Nearby& out) noexcept {
    out.count=0;
    RefreshWorld();
    const auto relation=Relations(At<std::int32_t>(vehicle,kTeam));
    if(!relation)return;
    for(int i=0;i<worldCount && out.count<kMaxEnemies;++i) {
        const Enemy& e=worldEnemies[i];
        if(e.object==vehicle || relation[e.team]!=kEnemyRelation)continue;
        float local[3];ToLocal(vehicle,e.pos,local);
        if(Dot(local,local)<=range*range)out.e[out.count++]=&e;
    }
    diag.enemies+=static_cast<unsigned>(out.count);
}

Mark GunMark(const unsigned char* weapon,bool* legacy) noexcept {
    if(legacy)*legacy=false;
    const auto target=At<std::int32_t>(weapon,kLockonTargetType);
    if(target==kMarkAir)return Mark::air;
    if(target==kMarkGround)return Mark::ground;
    if(target==kMarkLofted)return Mark::lofted;
    if(At<std::int32_t>(weapon,kLockonType)!=kLegacyLockonType)return Mark::none;
    if(legacy)*legacy=true;
    return target==kLegacyGroundTargetType ? Mark::ground : Mark::air;
}

// One of our anti-air guns firing GrenadeBullet01, the only round the fuses can burst early. The
// Bohr fires the same class (so its blast hits buildings) but keeps its stock impact fuse.
bool FlakRounds(const unsigned char* weapon) noexcept {
    if(GunMark(weapon)!=Mark::air)return false;
    const auto factory=At<const unsigned char*>(weapon,kAmmoFactory);
    return Readable(factory,8) && At<const unsigned char*>(factory,0)==image+kGrenadeFactoryVtable;
}

void LegacyFireGate() noexcept {
    if(legacySeen)return;
    legacySeen=true;
    const bool ok=edf::PatchCode(image+kFireGate,kFireGateStock,kFireGateFree,sizeof(kFireGateFree));
    Log("LEGACY weapon data (LockonType 4, built before 0.3.0): fire gate patched=%d. Rerun "
        "autoturret/tools/build.py install: the rebuilt guns fire without any patch, with or without this plugin",ok);
}

void Tag(const void* bullet,std::int32_t fuse,std::int32_t team) noexcept {
    const Round round{bullet,At<const void*>(bullet,kBulletWeakCtrl),fuse,team,GetTickCount64()};
    AcquireSRWLockExclusive(&roundLock);
    // A free slot, else the round tagged longest ago: rounds live a few seconds at most, so a full table
    // means rounds that died untagged (their update never saw the dead flag); the eviction is counted.
    Round* slot=&rounds[0];
    for(auto& r:rounds) {
        if(!r.bullet){slot=&r;break;}
        if(r.at<slot->at)slot=&r;
    }
    if(slot->bullet)++diag.evicted;
    *slot=round;
    ReleaseSRWLockExclusive(&roundLock);
    ++diag.tagged;
}

void Untag(const void* bullet,const void* ctrl) noexcept {
    AcquireSRWLockExclusive(&roundLock);
    for(auto& r:rounds)if(r.bullet==bullet && r.ctrl==ctrl)r=Round{};
    ReleaseSRWLockExclusive(&roundLock);
}

// The round's tag (a copy); its fuse is handed out once, on the round's first update.
bool TakeRound(const void* bullet,Round& out) noexcept {
    const void* ctrl=At<const void*>(bullet,kBulletWeakCtrl);
    int at=-1;
    AcquireSRWLockShared(&roundLock);
    for(int i=0;i<kMaxRounds;++i)if(rounds[i].bullet==bullet && rounds[i].ctrl==ctrl){out=rounds[i];at=i;break;}
    ReleaseSRWLockShared(&roundLock);
    if(at<0)return false;
    if(out.fuse!=kNoFuse) {   // only this round's own update reaches here, so no other thread takes it
        AcquireSRWLockExclusive(&roundLock);
        if(rounds[at].bullet==bullet && rounds[at].ctrl==ctrl)rounds[at].fuse=kNoFuse;
        ReleaseSRWLockExclusive(&roundLock);
    }
    return true;
}

// Stamps `weapon` with the fuse and team its next rounds get (game thread).
void StampGun(const void* weapon,std::int32_t fuse,std::int32_t team) noexcept {
    AcquireSRWLockExclusive(&gunLock);
    GunFuse* slot=nullptr;
    for(auto& g:gunFuses) {
        if(g.weapon==weapon){slot=&g;break;}
        if(!slot && (!g.weapon || g.frame+1<frame))slot=&g;
    }
    if(slot)*slot=GunFuse{weapon,fuse,team,frame};
    ReleaseSRWLockExclusive(&gunLock);
    if(!slot && !gunsFullLogged){gunsFullLogged=true;Log("FUSE more than %d flak guns this frame: the rest fire unfused",kMaxGunFuses);}
}

// The stamp of `weapon` from this frame or the last, else {kNoFuse, kNoTeam}.
GunFuse GunStamp(const void* weapon) noexcept {
    GunFuse out{weapon,kNoFuse,kNoTeam,0};
    AcquireSRWLockShared(&gunLock);
    for(const auto& g:gunFuses)if(g.weapon==weapon && g.frame+1>=frame){out=g;break;}
    ReleaseSRWLockShared(&gunLock);
    return out;
}

void __fastcall SpawnHook(void* weapon,void* bullet) {
    const void* vtable=*static_cast<void**>(weapon);
    for(auto& p:spawnPatches)if(p.vtable==vtable){if(p.original)p.original(weapon,bullet);break;}
    __try {
        if(bullet && *static_cast<void**>(bullet)==image+kGrenadeVtable && FlakRounds(static_cast<const unsigned char*>(weapon))) {
            const GunFuse g=GunStamp(weapon);
            Tag(bullet,g.fuse,g.team);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Route the gun class's "round spawned" slot through SpawnHook, once per vtable.
void HookSpawn(const unsigned char* weapon) noexcept {
    if(!proximityReady)return;
    const auto vtable=At<void**>(weapon,0);
    SpawnPatch* free=nullptr;
    for(auto& p:spawnPatches) {
        if(p.vtable==vtable)return;
        if(!p.vtable && !free)free=&p;
    }
    if(!free) {
        if(!spawnFullLogged){spawnFullLogged=true;Log("HOOK spawn: %d gun classes already hooked, vtable=+0x%llX left unfused",kMaxSpawnPatches,
            static_cast<unsigned long long>(reinterpret_cast<const unsigned char*>(vtable)-image));}
        return;
    }
    if(!Readable(vtable+kSpawnSlot,8))return;
    void* original=vtable[kSpawnSlot];
    free->original=reinterpret_cast<SpawnFn>(original);free->vtable=vtable;
    const bool ok=edf::PatchVtableSlot(vtable+kSpawnSlot,original,reinterpret_cast<void*>(&SpawnHook));
    Log("HOOK spawn vtable=+0x%llX original=+0x%llX ok=%d",
        static_cast<unsigned long long>(reinterpret_cast<const unsigned char*>(vtable)-image),
        static_cast<unsigned long long>(static_cast<unsigned char*>(original)-image),ok);
}

// Closest approach of this frame's flight segment to the enemies of `team` seen this frame.
const Enemy* NearTarget(const float* pos,const float* step,std::int32_t team,float* burst) noexcept {
    if(GetTickCount64()-worldAt>kEnemyMs)return nullptr;
    const auto relation=Relations(team);
    if(!relation)return nullptr;
    const float stepLength=Dot(step,step);
    float best=1.0e18f;const Enemy* hit=nullptr;
    for(int i=0;i<worldCount;++i) {
        const Enemy& m=worldEnemies[i];
        if(relation[m.team]!=kEnemyRelation)continue;
        const float d[3]={m.pos[0]-pos[0],m.pos[1]-pos[1],m.pos[2]-pos[2]};
        const float t=stepLength>1e-6f ? Clamp(Dot(d,step)/stepLength,0.0f,1.0f) : 0.0f;
        const float q[3]={pos[0]+step[0]*t,pos[1]+step[1]*t,pos[2]+step[2]*t};
        const float e[3]={m.pos[0]-q[0],m.pos[1]-q[1],m.pos[2]-q[2]};
        if(Dot(e,e)<best){best=Dot(e,e);hit=&m;std::memcpy(burst,q,sizeof(q));}
    }
    if(std::sqrt(best)<diag.nearest)diag.nearest=std::sqrt(best);
    return best<cfg.proximity*cfg.proximity ? hit : nullptr;
}

// Runs before the round's own update. On the first one the round's lifetime becomes its gun's time fuse
// (never past the lifetime the data gave it). A fused round gets its age set to its lifetime, so the stock
// expiry check in the same update bursts it at its (moved) position.
void Fuze(unsigned char* bullet,const Round& round) noexcept {
    unsigned char* c=bullet+kCtl;
    const auto flags=At<std::uint32_t>(c,kCtlFlags);
    if(flags&kRoundDead)return;
    std::int32_t alive=At<std::int32_t>(c,kCtlAlive);
    if(round.fuse!=kNoFuse && round.fuse<alive) {
        alive=round.fuse>cfg.fuseMin ? round.fuse : (cfg.fuseMin<alive ? cfg.fuseMin : alive);
        Put<std::int32_t>(c,kCtlAlive,alive);
    }
    const auto age=At<std::int32_t>(c,kCtlAge);
    if(age<cfg.fuseMin || age>=alive)return;
    float pos[3],vel[3];
    for(int i=0;i<3;++i){pos[i]=At<float>(c,kCtlPos+i*4);vel[i]=At<float>(c,kCtlVel+i*4);}
    bool burst=false;
    if(cfg.contact && (c[kCtlStuck] || Dot(vel,vel)<kStoppedSpeed*kStoppedSpeed)){burst=true;++diag.contact;}
    if(!burst && cfg.proximity>0.0f && round.team!=kNoTeam) {
        const float step[3]={vel[0]/60.0f,vel[1]/60.0f,vel[2]/60.0f};
        float at[3];
        if(const Enemy* m=NearTarget(pos,step,round.team,at)) {
            burst=true;++diag.proximity;
            if(cfg.debug && diag.proximity<=4) {
                const float a[3]={m->pos[0]-at[0],m->pos[1]-at[1],m->pos[2]-at[2]};
                const float o[3]={m->origin[0]-at[0],m->origin[1]-at[1],m->origin[2]-at[2]};
                Log("BURST age=%d/%d aim=%.1fm origin=%.1fm blast=%.1fm",age,alive,std::sqrt(Dot(a,a)),std::sqrt(Dot(o,o)),At<float>(c,kCtlBlast));
            }
            for(int i=0;i<3;++i)Put<float>(c,kCtlPos+i*4,at[i]);
        }
    }
    if(burst || age+1>=alive)Put<float>(c,kCtlBlastVisual,At<float>(c,kCtlBlast)*cfg.burstVisual);   // bursting this update
    if(!burst)return;
    Put<std::uint32_t>(c,kCtlFlags,flags|kRoundBurstOnExpiry);
    Put<std::int32_t>(c,kCtlAge,alive);
}

void __fastcall UpdateHook(void* bullet,void* ctx) {
    bool ours=false;
    const void* ctrl=nullptr;
    __try {
        Round round{};
        ours=cfg.enabled && TakeRound(bullet,round);
        if(ours){ctrl=round.ctrl;Fuze(static_cast<unsigned char*>(bullet),round);}
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    originalUpdate(bullet,ctx);
    if(!ours)return;
    __try { if(At<std::uint32_t>(bullet,kCtl+kCtlFlags)&kRoundDead)Untag(bullet,ctrl); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

void* __fastcall DtorHook(void* bullet,unsigned flags) {
    __try { Untag(bullet,At<const void*>(bullet,kBulletWeakCtrl)); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
    return originalDtor(bullet,flags);
}

// Gravity along the vehicle's down axis, m/s^2: the world gravity vector rotated into the vehicle
// frame and its down component taken, as the game's vehicle aim does. 0 if it can't be read (the
// aim then flies straight lines, as before gravity was solved).
float Down(const unsigned char* vehicle) noexcept {
    float g[3];
    if(!edf::WorldGravity(image,g))return 0.0f;
    const float* m=reinterpret_cast<const float*>(vehicle+kMatrix);
    const float down=-Dot(g,m+4);
    return std::isfinite(down) ? down : 0.0f;
}

// Elevation (rad, up positive) and flight time (frames) to hit a point (x across, y up) on one arc: common/weapon.cpp
// edf::BallisticArc (the per-frame step's drop aimed over the point; pylib/ballistics.py arc is its Python copy).
bool Arc(double x,double y,const Shot& shot,bool high,float& elevation,float& time) noexcept {
    return edf::BallisticArc(x,y,shot.speed,shot.drop,high,elevation,time);
}

// Elevation (rad, up positive) and flight time (frames) to hit a point in the vehicle frame: the lower of the two
// arcs, the solve the game's vehicle aim runs (0x50350); a lofted gun's the higher, while its pitch is within the
// axis' stops (shot.pitchMin..pitchMax), else the lower. False when out of reach.
bool Ballistic(const float* local,const Shot& shot,float& elevation,float& time) noexcept {
    const double x=std::sqrt(local[0]*local[0]+local[2]*local[2]),y=local[1];
    if(shot.lofted && Arc(x,y,shot,true,elevation,time)) {
        const float pitch=cfg.pitchSign*elevation+cfg.pitchOffset;
        if(pitch>=shot.pitchMin && pitch<=shot.pitchMax)return true;
    }
    return Arc(x,y,shot,false,elevation,time);
}

// Wanted turret yaw/pitch and flight time (frames) for a point in the vehicle frame.
bool AimAngles(const float* local,const Shot& shot,float& yaw,float& pitch,float& time) noexcept {
    float elevation;
    if(!Ballistic(local,shot,elevation,time))return false;
    yaw=cfg.yawSign*std::atan2(local[0],local[2])+cfg.yawOffset;
    pitch=cfg.pitchSign*elevation+cfg.pitchOffset;
    return true;
}

// The seat's weapons, each passed to `visit` (holder count sanity-checked). Under the caller's __try.
template<class Visit> void ForEachGun(const unsigned char* seat,Visit visit) noexcept {
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>kMaxHolders || !Readable(holders,count*8))return;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto weapon=At<unsigned char*>(holders[i],kHolderWeapon);
        if(Readable(weapon,kAmmoGravity+4))visit(weapon);
    }
}

// The seat's marked guns as one round (the last one's speed and drop; whether any hunts the ground or lobs) and the
// farthest reach (m); false with none of ours (a stock flak: left alone).
bool ReadShot(const unsigned char* vehicle,const unsigned char* seat,Shot& shot,float& range) noexcept {
    bool armed=false;shot=Shot{};range=0.0f;
    float gravity=0.0f;
    ForEachGun(seat,[&](const unsigned char* weapon) noexcept {
        const Mark mark=GunMark(weapon);
        if(mark==Mark::none)return;
        armed=true;++diag.weapons;
        shot.speed=At<float>(weapon,kAmmoSpeed);
        gravity=At<float>(weapon,kAmmoGravity);
        shot.ground=shot.ground || mark==Mark::ground || mark==Mark::lofted;
        shot.lofted=shot.lofted || mark==Mark::lofted;
        const float reach=shot.speed*static_cast<float>(At<std::int32_t>(weapon,kAmmoAlive));
        if(reach>range)range=reach;
    });
    if(armed && std::isfinite(gravity) && gravity>0.0f)shot.drop=gravity*Down(vehicle)/kFramesPerSecondSq;
    return armed;
}

// The tracked enemy stays the target while it lives within the gun's full range (dropping it at the
// tracking-range edge made the turret flip between targets every second or two). A new one comes
// from within tracking range: any target of the gun's kind (air, or ground for a ground-attack gun)
// beats any other, then distance plus the turn it costs from where the guns point now. Returns the
// enemy object; `world` is its aim point, always taken from the object's first lock point so the lead
// sees a steady track. `only` (the player's lock, designate.cpp): that one alone, kept while the gun can reach it,
// and nothing else while it cannot.
const void* PickTarget(const unsigned char* vehicle,const unsigned char* seat,const void* keep,const void* dropped,const void* only,
                       Shot& shot,float range,float* world) noexcept {
    Nearby nearby;
    ScanEnemies(vehicle,range,nearby);
    const float track=cfg.trackRange*range;
    const auto axes=seat+kSeatAim+kAimAxes;
    const float yaw=At<float>(axes,kAxisAngle),pitch=At<float>(axes+kAxisStride,kAxisAngle);
    const bool aimed=std::isfinite(yaw) && std::isfinite(pitch);
    // Targets the guns cannot elevate (or depress) to, or lob a round onto, are out: chasing one
    // overhead pinned the pitch at its stop while the yaw whipped around, and every round went under it.
    const float pitchMin=At<float>(axes+kAxisStride,kAxisMin)-kPitchMargin,pitchMax=At<float>(axes+kAxisStride,kAxisMax)+kPitchMargin;
    // A lofted gun takes the high arc only where its pitch can get to it (Ballistic): the stops themselves.
    shot.pitchMin=At<float>(axes+kAxisStride,kAxisMin);shot.pitchMax=At<float>(axes+kAxisStride,kAxisMax);
    const auto reachable=[&](const float* l,float& wantYaw,float& wantPitch) noexcept {
        float time;
        return AimAngles(l,shot,wantYaw,wantPitch,time) && wantPitch>=pitchMin && wantPitch<=pitchMax;
    };
    const Enemy* best=nullptr;float bestScore=0.0f;
    bool kept=false;
    if(only)keep=only;
    for(int i=0;keep && i<nearby.count;++i) {
        if(nearby.e[i]->object!=keep)continue;    // the scan already limits it to full range
        float l[3],wantYaw,wantPitch;ToLocal(vehicle,nearby.e[i]->pos,l);
        if(reachable(l,wantYaw,wantPitch)){best=nearby.e[i];kept=true;}
        break;
    }
    for(int i=0;!kept && !only && i<nearby.count;++i) {
        const Enemy* e=nearby.e[i];
        if(e->object==dropped)continue;
        float l[3],wantYaw,wantPitch;ToLocal(vehicle,e->pos,l);
        const float distance=std::sqrt(Dot(l,l));
        if(distance>track || !reachable(l,wantYaw,wantPitch))continue;
        const float turn=aimed ? std::fabs(Wrap(wantYaw-yaw))+std::fabs(wantPitch-pitch) : 0.0f;
        const bool preferred=(l[1]>cfg.airHeight)!=shot.ground;
        // EDF6VehicleCrew's Proteus behind its front shield: enemies near it, or after it, first (PriorityWeight).
        const float score=distance*PriorityWeight(*e)+turn*cfg.slewWeight+(preferred ? 0.0f : 1.0e6f);
        if(!best || score<bestScore){best=e;bestScore=score;}
    }
    if(!best)return nullptr;
    for(int i=0;i<nearby.count;++i)   // the object's first lock point
        if(nearby.e[i]->object==best->object){std::memcpy(world,nearby.e[i]->pos,sizeof(nearby.e[i]->pos));break;}
    return best->object;
}

// Steer runs once per game frame, so the target's velocity is its per-call displacement
// (smoothed against bone jitter). Lead with the flight time to the lead point itself.
void Lead(const unsigned char* vehicle,Track& track,const void* target,const float* world,float* local,const Shot& shot) noexcept {
    const bool same=track.target==target && GetTickCount64()-track.at<200;
    if(!same){track.frames=0;std::memset(track.vel,0,sizeof(track.vel));}
    else {
        for(int i=0;i<3;++i) {
            const float v=world[i]-track.last[i];
            track.vel[i]=track.frames ? track.vel[i]+0.3f*(v-track.vel[i]) : v;
        }
        ++track.frames;
    }
    track.target=target;std::memcpy(track.last,world,sizeof(track.last));
    float aim[3];std::memcpy(aim,world,sizeof(aim));
    if(cfg.lead && track.frames>=2) {
        for(int pass=0;pass<2;++pass) {
            ToLocal(vehicle,aim,local);
            float elevation,t;
            if(!Ballistic(local,shot,elevation,t))break;
            for(int i=0;i<3;++i)aim[i]=world[i]+track.vel[i]*t;
        }
    }
    ToLocal(vehicle,aim,local);
}

// Turret input for one axis: feed-forward at the wanted angle's own rate plus a correction on the
// error. A pure proportional input lags a crossing target by rate/(gain x k), which put every
// round behind the target.
float AxisInput(Track& track,int a,float want,float angle,float error,bool wrap,float gain,float hull) noexcept {
    if(track.k[a]<=0.0f)track.k[a]=kTurnPerInput;
    if(track.frames>=1) {
        const float wanted=(wrap ? Wrap(want-track.want[a]) : want-track.want[a])-hull;
        track.rate[a]+=0.3f*(wanted-track.rate[a]);
        const float moved=(wrap ? Wrap(angle-track.axis[a]) : angle-track.axis[a])-hull;
        if(std::fabs(track.in[a])>0.15f) {
            const float k=moved/track.in[a];
            if(k>kTurnPerInputMin && k<kTurnPerInputMax)track.k[a]+=0.05f*(k-track.k[a]);
        }
    } else track.rate[a]=0.0f;
    const float ff=cfg.feedForward ? track.rate[a]/track.k[a] : 0.0f;
    const float in=Clamp(ff+error*gain,-1.0f,1.0f);
    track.want[a]=want;track.axis[a]=angle;track.in[a]=in;
    return in;
}

// A lofted launcher (the Katyusha) the player on this machine rides: they aim it with the camera, which follows the
// seat's aim axes (the vehicle's turn input, +0x2AA0, is what Steer writes; docs/re-notes.md "The Katyusha's camera
// and pose": inferred, EDF6VehicleCrew's LOFT debug line checks it), so any steering here turns the player's view
// (2026-10-05: lofted to 75..79 deg it stared at the sky). EDF6VehicleCrew lifts the launcher alone onto
// the high arc to the ground point the camera looks at (src/katyusha.cpp); NPC crews keep this plugin's aim.
bool PlayerLofted(const unsigned char* seat) noexcept {
    if(edf::SeatRider(image,seat)!=edf::Rider::player)return false;
    bool lofted=false;
    ForEachGun(seat,[&](const unsigned char* weapon) noexcept { lofted=lofted || GunMark(weapon)==Mark::lofted; });
    return lofted;
}

// The seat's gun's barrel (its first holder's muzzles, rebuilt from the bones) and its round's life in frames.
struct Barrel { bool ok; float pos[3],dir[3]; float life; };
Barrel SeatBarrel(const unsigned char* seat) noexcept {
    Barrel b{};
    const auto gun=SeatGun(seat);
    if(!gun || !Readable(gun,kAmmoGravity+4) || !Readable(gun+edf::kWeaponMatrix,0x40))return b;
    b.ok=edf::MeanMuzzle(gun,8,b.pos,b.dir);
    b.life=static_cast<float>(At<std::int32_t>(gun,kAmmoAlive));
    return b;
}

// Aims the ridden flak's turret (seat 0); returns the flight time (frames) to the aim point, the time fuse
// its rounds get, or -1 (no target, aimed by hand, out of reach: the rounds burst at max range). For this machine's
// player it keeps their bindings and lock (designate.cpp) and publishes the HUD's readout; in the lead-circle mode it
// tracks and fuses as ever but leaves the turret to them.
float Steer(unsigned char* vehicle,const unsigned char* seat) noexcept {
    if(PlayerLofted(seat)){diag.stop="player-lofted";return -1.0f;}
    Track* track=TrackFor(vehicle,0,true);   // the flak is aimed only for its rider (hasInput)
    if(!track){diag.stop="no-track";return -1.0f;}
    const auto now=GetTickCount64();
    float world[3]{},local[3]{},range=0.0f;Shot shot{};
    if(!ReadShot(vehicle,seat,shot,range)){diag.stop="unarmed";return -1.0f;}   // a stock flak: leave it alone
    const bool pilot=edf::SeatRider(image,seat)==edf::Rider::player;
    const Barrel barrel=pilot ? SeatBarrel(seat) : Barrel{};
    const float* muzzle=barrel.ok ? barrel.pos : nullptr;
    const float* bore=barrel.ok ? barrel.dir : nullptr;
    if(pilot)PilotFrame(vehicle,0,seat,muzzle,bore,range);
    const void* only=pilot ? Designated(vehicle,nullptr) : nullptr;
    // Who turns the gun (common/edf/aimlink.h PlayerGunRule): an NPC's always this plugin; the player's, with
    // EDF6VehicleCrew's turret camera turning it after the view, only onto their lock in AUTO; without that camera, as
    // ever (the auto-aim, the lead circle the player's own). Holding the aim stick then aims by hand (the stock input
    // already turned it); letting go hands the turret back at once, to a target near where it was dragged, never the
    // one dragged away from. With the camera the stick turns the view: no drag.
    edf::aimlink::PlayerGun rule=pilot ? PlayerControlRule(vehicle,0,LeadCircle(),only!=nullptr)
                                             : edf::aimlink::PlayerGun{true,true};
    if(pilot && !rule.steer)track->steered=0; // observation camera cannot drive this gun
    const float stick[2]={At<float>(seat,kStick),At<float>(seat,kStick+4)};
    const bool drag=rule.drag && cfg.dragDeadzone>0.0f && (std::fabs(stick[0])>cfg.dragDeadzone || std::fabs(stick[1])>cfg.dragDeadzone);
    if(drag && !track->dragging && track->target){track->dropped=track->target;track->droppedUntil=now+cfg.dragDropMs;}
    track->dragging=drag;
    if(drag)track->target=nullptr;
    const void* dropped=now<track->droppedUntil ? track->dropped : nullptr;
    const auto target=drag ? nullptr : PickTarget(vehicle,seat,track->target,dropped,only,shot,range,world);
    if(!target) {
        diag.stop=drag ? "manual" : "no-target";track->at=now;track->target=nullptr;
        if(pilot)PublishAim(vehicle,true,nullptr,nullptr,muzzle,bore,&shot,nullptr,barrel.life);
        return -1.0f;
    }
    diag.stop=rule.steer ? "aiming" : "tracking";
    Lead(vehicle,*track,target,world,local,shot);
    track->at=now;
    if(pilot)PublishAim(vehicle,true,target,world,muzzle,bore,&shot,track->vel,barrel.life);
    float wantYaw,wantPitch,flight;
    if(!AimAngles(local,shot,wantYaw,wantPitch,flight)){diag.stop="out-of-reach";return -1.0f;}
    if(!rule.steer)return flight;   // the time fuse still bursts the flak at the target's range
    const auto axes=seat+kSeatAim+kAimAxes;
    const float stock[2]={At<float>(axes,kAxisAngle),At<float>(axes+kAxisStride,kAxisAngle)};
    if(!std::isfinite(stock[0]) || !std::isfinite(stock[1])){diag.stop="bad-axis";return flight;}
    // A gun EDF6VehicleCrew's stabilizer holds is steered from where it holds it, the hull's turn not counted twice.
    float held[2],hull[2];
    Stabilized(vehicle,0,stock,held,hull);
    const float yaw=held[0],pitch=held[1];
    wantPitch=Clamp(wantPitch,At<float>(axes+kAxisStride,kAxisMin),At<float>(axes+kAxisStride,kAxisMax));
    const bool fullCircle=At<float>(axes,kAxisMax)-At<float>(axes,kAxisMin)>=2*kPi-0.01f;
    const float yawError=fullCircle ? Wrap(wantYaw-yaw) : wantYaw-yaw;
    const float in[2]={AxisInput(*track,0,wantYaw,yaw,yawError,fullCircle,cfg.gain,hull[0]),
                       AxisInput(*track,1,wantPitch,pitch,wantPitch-pitch,false,cfg.gain,hull[1])};
    if(!std::isfinite(in[0]) || !std::isfinite(in[1])){diag.stop="bad-input";return flight;}
    Put<float>(vehicle,kTurn,in[0]);Put<float>(vehicle,kTurn+4,in[1]);
    track->steered=Frame();
    if(cfg.debug && now-track->loggedAt>500) {
        track->loggedAt=now;
        Log("AIM %s flight=%.0ff drop=%.5f v=%p t=%p local=(%.1f,%.1f,%.1f) yaw=%.3f->%.3f pitch=%.3f->%.3f in=(%.2f,%.2f) rate=(%.2f,%.2f)/s k=(%.2f,%.2f)/s speed=%.0fm/s",
            shot.lofted?"lofted":shot.ground?"ground":"air",flight,shot.drop,vehicle,target,local[0],local[1],local[2],yaw,wantYaw,pitch,wantPitch,in[0],in[1],
            track->rate[0]*60.0f,track->rate[1]*60.0f,track->k[0]*60.0f,track->k[1]*60.0f,std::sqrt(Dot(track->vel,track->vel))*60.0f);
    }
    return flight;
}

// Every flak's guns, ridden or not (NPC Keplers fire flak too), once a frame: a gun with pre-0.3.0 data
// gets the legacy fire gate; a flak-round gun has its class's spawn slot hooked and is stamped with the
// fuse its rounds get (`flight` frames to the target, < 0: none, they burst at the data's max range) and
// its vehicle's team (the side whose enemies its rounds burst near).
void NoteGuns(const unsigned char* vehicle,const unsigned char* seat,float flight) noexcept {
    std::int32_t fuse=kNoFuse;
    if(cfg.fuse && flight>=0.0f) {
        const float frames=std::ceil(flight+cfg.fuseBias);
        fuse=frames>static_cast<float>(cfg.fuseMin) ? (frames<1.0e6f ? static_cast<std::int32_t>(frames) : 1000000) : cfg.fuseMin;
    }
    const auto team=At<std::int32_t>(vehicle,kTeam);
    ForEachGun(seat,[&](const unsigned char* weapon) noexcept {
        bool legacy=false;
        if(GunMark(weapon,&legacy)!=Mark::none && legacy)LegacyFireGate();
        if(!FlakRounds(weapon))return;
        HookSpawn(weapon);
        StampGun(weapon,fuse,team);
    });
}

void Flak(unsigned char* vehicle,bool ridden) noexcept {
    diag.stop="vehicle";
    if(!Readable(vehicle,kTurn+kTurnStride,true) || vehicle[kDead] || edf::SeatCount(vehicle)==0)return;
    const auto seat=edf::SeatAt(vehicle,0);
    float flight=-1.0f;
    if(ridden && cfg.enabled){++diag.ridden;flight=Steer(vehicle,seat);}
    NoteGuns(vehicle,seat,flight);
}

void FlushDiag(const void* vehicle) noexcept {
    const auto now=GetTickCount64();
    if(!cfg.debug || now-diag.at<1000)return;
    if(diag.at)Log("DIAG v=%p calls=%u ridden=%u weapons=%u registry=%u enemies=%u world=%d rounds=%u evicted=%u prox=%u contact=%u nearest=%.1fm last=%s",
        vehicle,diag.calls,diag.ridden,diag.weapons,diag.registry,diag.enemies,worldCount,
        diag.tagged,diag.evicted,diag.proximity,diag.contact,diag.nearest,diag.stop?diag.stop:"-");
    diag=Diag{};diag.at=now;diag.nearest=1.0e9f;
}

// Slot 55 of the flak, chained (edf::VehicleInputFn: all four register arguments forwarded, as
// EDF6VehicleCrew's hook on the same slot forwards them).
template<int K> void __fastcall HookInput(void* vehicle,std::uintptr_t hasInput,void* r8,void* r9) {
    nextInput[K](vehicle,hasInput,r8,r9);
    ++diag.calls;
    FlushDiag(vehicle);
    ReloadConfigIfChanged();
    __try {
        SeeVehicle(vehicle);
        Flak(static_cast<unsigned char*>(vehicle),(hasInput&0xFF)!=0);   // no rider: the stock code just zeroed the turn
    } __except(EXCEPTION_EXECUTE_HANDLER) {diag.stop="fault";}
}

// The flak's code, unpatched by anyone else (its input slot may hold another plugin's hook that ends
// in it: EDF6VehicleCrew chains the same slots on the first mission frame).
bool CheckProfile() noexcept {
    __try {
        const unsigned char input[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x0F,0xB6,0xDA};
        const unsigned char stick[]={0xF3,0x0F,0x10,0x83,0xD0,0x02,0x00,0x00};   // movss xmm0,[rbx+2D0]
        const unsigned char turn[]={0xF3,0x0F,0x11,0x87,0xA0,0x2A,0x00,0x00};    // movss [rdi+2AA0],xmm0
        const unsigned char apply[]={0x48,0x8D,0x93,0xA0,0x2A,0x00,0x00};        // lea rdx,[rbx+2AA0] (slot 4)
        const unsigned char lockType[]={0x89,0x86,0xB0,0x06,0x00,0x00};          // mov [rsi+6B0],eax
        return edf::Matches(image,kFlakInput,input,sizeof(input)) && edf::Matches(image,0x6214D8,stick,sizeof(stick))
            && edf::Matches(image,0x6214E7,turn,sizeof(turn)) && edf::Matches(image,0x621872,apply,sizeof(apply))
            && edf::Matches(image,0x68D124,lockType,sizeof(lockType));
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

// The proximity and time fuses ride on GrenadeBullet01's update; they stay off if the layout differs.
// Returns the number of slots patched.
int HookGrenade() noexcept {
    const auto vtable=reinterpret_cast<void**>(image+kGrenadeVtable);
    const unsigned char age[]={0x8B,0x8D,0xF8,0x0A,0x00,0x00,0xFF,0xC1};   // mov ecx,[rbp+AF8] / inc ecx
    const unsigned char expire[]={0x3B,0x8D,0x08,0x0A,0x00,0x00};          // cmp ecx,[rbp+A08]
    const bool layout=vtable[kGrenadeUpdateSlot]==image+kGrenadeUpdate && vtable[kGrenadeDtorSlot]==image+kGrenadeDtor
        && edf::Matches(image,0x236888,age,sizeof(age)) && edf::Matches(image,0x236899,expire,sizeof(expire));
    if(!layout){Log("HOOK grenade: unexpected layout, fuses off");return 0;}
    originalUpdate=reinterpret_cast<UpdateFn>(image+kGrenadeUpdate);
    originalDtor=reinterpret_cast<DtorFn>(image+kGrenadeDtor);
    const bool update=edf::PatchVtableSlot(vtable+kGrenadeUpdateSlot,image+kGrenadeUpdate,reinterpret_cast<void*>(&UpdateHook));
    const bool dtor=edf::PatchVtableSlot(vtable+kGrenadeDtorSlot,image+kGrenadeDtor,reinterpret_cast<void*>(&DtorHook));
    proximityReady=update && dtor;
    Log("HOOK grenade update=%d dtor=%d",update,dtor);
    return static_cast<int>(update)+static_cast<int>(dtor);
}

float ReadFloat(const wchar_t* key,float fallback) noexcept {
    wchar_t text[64]{};
    GetPrivateProfileStringW(L"AutoTurret",key,L"",text,64,iniPath);
    wchar_t* end=nullptr;
    const float value=std::wcstof(text,&end);
    return end!=text && std::isfinite(value) ? value : fallback;
}

// An integer key held to [lo, hi] (a key code, a button mask, milliseconds).
int ReadInt(const wchar_t* key,int fallback,int lo,int hi) noexcept {
    const int v=static_cast<int>(GetPrivateProfileIntW(L"AutoTurret",key,fallback,iniPath));
    return v<lo ? lo : v>hi ? hi : v;
}

void LoadConfig() noexcept {
    Config next{};
    next.enabled=GetPrivateProfileIntW(L"AutoTurret",L"Enabled",1,iniPath)!=0;
    next.debug=GetPrivateProfileIntW(L"AutoTurret",L"Debug",0,iniPath)!=0;
    next.gain=ReadFloat(L"Gain",next.gain);
    next.yawSign=ReadFloat(L"YawSign",next.yawSign);
    next.yawOffset=ReadFloat(L"YawOffset",next.yawOffset);
    next.pitchSign=ReadFloat(L"PitchSign",next.pitchSign);
    next.pitchOffset=ReadFloat(L"PitchOffset",next.pitchOffset);
    next.pivotHeight=ReadFloat(L"PivotHeight",next.pivotHeight);
    next.airHeight=ReadFloat(L"AirHeight",next.airHeight);
    next.lead=GetPrivateProfileIntW(L"AutoTurret",L"Lead",1,iniPath)!=0;
    next.trackRange=Clamp(ReadFloat(L"TrackRange",next.trackRange),0.0f,1.0f);
    next.feedForward=GetPrivateProfileIntW(L"AutoTurret",L"FeedForward",1,iniPath)!=0;
    next.slewWeight=Clamp(ReadFloat(L"SlewWeight",next.slewWeight),0.0f,5000.0f);
    next.dragDeadzone=Clamp(ReadFloat(L"DragDeadzone",next.dragDeadzone),0.0f,1.0f);
    next.dragDropMs=GetPrivateProfileIntW(L"AutoTurret",L"DragDropMs",next.dragDropMs,iniPath);
    next.fuse=GetPrivateProfileIntW(L"AutoTurret",L"FuseToTarget",1,iniPath)!=0;
    next.fuseBias=ReadFloat(L"FuseBiasFrames",next.fuseBias);
    next.fuseMin=GetPrivateProfileIntW(L"AutoTurret",L"FuseMinFrames",next.fuseMin,iniPath);
    next.proximity=ReadFloat(L"ProximityRadius",next.proximity);
    next.contact=GetPrivateProfileIntW(L"AutoTurret",L"ContactFuse",1,iniPath)!=0;
    next.burstVisual=Clamp(ReadFloat(L"BurstVisualScale",next.burstVisual),0.2f,10.0f);
    next.gunnerAi=GetPrivateProfileIntW(L"AutoTurret",L"GunnerAI",1,iniPath)!=0;
    next.gunnerAssist=GetPrivateProfileIntW(L"AutoTurret",L"GunnerAssist",1,iniPath)!=0;
    next.gunnerRange=Clamp(ReadFloat(L"GunnerRange",next.gunnerRange),10.0f,3000.0f);
    next.gunnerCone=Clamp(ReadFloat(L"GunnerCone",next.gunnerCone),0.001f,0.5f);
    next.gunnerMinDistance=Clamp(ReadFloat(L"GunnerMinDistance",next.gunnerMinDistance),0.0f,500.0f);
    next.gunnerYawSign=ReadFloat(L"GunnerYawSign",next.gunnerYawSign)>=0.0f ? 1.0f : -1.0f;
    next.gunnerPitchSign=ReadFloat(L"GunnerPitchSign",next.gunnerPitchSign)>=0.0f ? 1.0f : -1.0f;
    next.aimMode=GetPrivateProfileIntW(L"AutoTurret",L"AimMode",next.aimMode,iniPath)!=0 ? 1 : 0;
    next.modeKey=ReadInt(L"AimModeKey",next.modeKey,0,0xFE);
    next.modeButton=ReadInt(L"AimModeButton",next.modeButton,0,0xFFFF);
    next.lockKey=ReadInt(L"LockKey",next.lockKey,0,0xFE);
    next.lockButton=ReadInt(L"LockButton",next.lockButton,0,0xFFFF);
    next.lockCone=Clamp(ReadFloat(L"LockCone",next.lockCone),1.0f,90.0f);
    next.lockRange=Clamp(ReadFloat(L"LockRange",next.lockRange),0.0f,5000.0f);
    next.lockClearMs=static_cast<DWORD>(ReadInt(L"LockClearMs",static_cast<int>(next.lockClearMs),200,5000));
    cfg=next;
    Log("CONFIG enabled=%d debug=%d gain=%.2f yaw=%.0f%+.3f pitch=%.0f%+.3f pivot=%.1f air=%.1f lead=%d track=%.2f ff=%d slew=%.0f drag=%.2f/%lums fuse=%d%+.1f min=%d proximity=%.1f contact=%d",
        cfg.enabled,cfg.debug,cfg.gain,cfg.yawSign,cfg.yawOffset,cfg.pitchSign,cfg.pitchOffset,cfg.pivotHeight,
        cfg.airHeight,cfg.lead,cfg.trackRange,cfg.feedForward,cfg.slewWeight,cfg.dragDeadzone,cfg.dragDropMs,cfg.fuse,cfg.fuseBias,cfg.fuseMin,cfg.proximity,cfg.contact);
    Log("CONFIG gunners ai=%d assist=%d range=%.0f cone=%.3f min=%.0f sign=(%+.0f,%+.0f)",
        cfg.gunnerAi,cfg.gunnerAssist,cfg.gunnerRange,cfg.gunnerCone,cfg.gunnerMinDistance,cfg.gunnerYawSign,cfg.gunnerPitchSign);
    Log("CONFIG player mode=%s key=0x%X button=0x%X lock key=0x%X button=0x%X cone=%.0fdeg range=%.0fm clear=%lums",
        cfg.aimMode ? "lead" : "auto",cfg.modeKey,cfg.modeButton,cfg.lockKey,cfg.lockButton,cfg.lockCone,cfg.lockRange,cfg.lockClearMs);
}

// Edits to the ini apply within a second; no game restart needed.
void ReloadConfigIfChanged() noexcept {
    if(ini.Changed())LoadConfig();
}

// The rocket launcher (Vehicle402_Rocket, the Naegling's class: EDF6VehicleCrew's Katyusha is one): its input slot
// is the flak's line for line up to the turn it writes (+0x2AA0; 0x5FD958 stores it whole, the flak's per axis), so
// the same hook aims its marked guns. Its stock missiles carry no mark: left alone. 1 when hooked.
constexpr unsigned kRocketVtable=0x17D8B50,kRocketInput=0x5FD8E0;
int HookRocket() noexcept {
    __try {
        const unsigned char input[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x0F,0xB6,0xDA};
        const unsigned char turn[]={0x0F,0x11,0x87,0xA0,0x2A,0x00,0x00};     // movups [rdi+2AA0],xmm0
        const unsigned char apply[]={0x48,0x8D,0x93,0xA0,0x2A,0x00,0x00};    // lea rdx,[rbx+2AA0] (slot 4)
        if(!edf::Matches(image,kRocketInput,input,sizeof(input)) || !edf::Matches(image,0x5FD958,turn,sizeof(turn))
           || !edf::Matches(image,0x5FDB5B,apply,sizeof(apply))) {
            Log("HOOK rocket launcher: unexpected layout, its guns stay unaimed");
            return 0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){return 0;}
    auto slot=reinterpret_cast<void**>(image+kRocketVtable)+edf::kSlotInput;
    void* const current=*slot;
    if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&HookInput<1>),reinterpret_cast<void**>(&nextInput[1]))) {
        Log("HOOK rocket launcher input: slot changed under us");
        return 0;
    }
    if(current!=image+kRocketInput)Log("HOOK rocket launcher input: chaining onto %p (another plugin)",current);
    Log("HOOK rocket launcher input slot=1");
    return 1;
}

// The flak: its input slot, then the round hooks the fuses need. Returns the number of slots patched.
int HookFlak() noexcept {
    if(!CheckProfile()){Log("HOOK flak: unexpected layout or conflicting patch, flak off");return 0;}
    auto slot=reinterpret_cast<void**>(image+kFlakVtable)+edf::kSlotInput;
    void* const current=*slot;
    if(!edf::ChainVtableSlot(slot,reinterpret_cast<void*>(&HookInput<0>),reinterpret_cast<void**>(&nextInput[0]))) {
        Log("HOOK flak input: slot changed under us, flak off");
        return 0;
    }
    if(current!=image+kFlakInput)Log("HOOK flak input: chaining onto %p (another plugin)",current);
    Log("HOOK flak input slot=1");
    return 1+HookGrenade()+HookRocket();
}
}  // namespace autoturret

extern "C" __declspec(dllexport) bool EDFMLAPI EML6_Load(PluginInfo* info) {
    using namespace autoturret;
    if(!info)return false;
    GetModuleFileNameW(module,iniPath,MAX_PATH);
    auto dot=wcsrchr(iniPath,L'.');if(!dot)return false;
    wcscpy_s(dot,MAX_PATH-(dot-iniPath),L".ini");
    wcscpy_s(logPath,iniPath);
    dot=wcsrchr(logPath,L'.');wcscpy_s(dot,MAX_PATH-(dot-logPath),L".log");
    info->infoVersion=PluginInfo::MaxInfoVer;info->name="EDF6 Auto Turret";info->version=PLUG_VER(0,3,0,0);
    Log("EDF6AutoTurret 0.3.0 loading");
    ini.Start(iniPath);
    LoadConfig();
    image=edf::IdentifyImage(GetModuleHandleW(L"EDF.dll"));
    if(!image){Log("REFUSED: unsupported EDF.dll (the weapon data still works stock: the guns fire unaimed, unfused)");return false;}
    // The flak and the tank gunners stand alone: either one's code being patched by someone else
    // leaves only that one stock. The DLL stays loaded while any slot points into it.
    const int flak=HookFlak();
    const int gunners=HookGunners();
    Log("LOADED patches flak=%d gunners=%d",flak,gunners);
    return flak+gunners>0;
}

// EDF6VehicleCrew's turret camera asks whether this plugin turned `vehicle`'s seat `seat` this game frame (aimlink.h
// Steers): it then leaves the turret to it for the frame. The aim step it asks from runs after this frame's input slot.
extern "C" __declspec(dllexport) bool __cdecl EDF6AutoTurret_SteersV2(const void* vehicle,unsigned seat) {
    using namespace autoturret;
    if(!vehicle)return false;
    __try {
        const auto ctrl=At<const void*>(vehicle,kSelfCtrl);
        for(const Track& t:tracks)
            if(t.vehicle==vehicle && t.seat==seat && t.ctrl==ctrl)return t.steered!=0 && t.steered==Frame();
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    return false;
}

// EDF6VehicleCrew's gun stabilizer asks whether this plugin steers its guns from the held axes (aimlink.h V3): an older
// one would count the hull's turn twice, and gets no stabilizer on the seats it steers.
extern "C" __declspec(dllexport) bool __cdecl EDF6AutoTurret_StabilizerAwareV3() { return true; }

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){autoturret::module=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
