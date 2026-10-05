// The plugin's missiles guided as modern missiles: every missile store (pylib/vcobjects.py STORES, src/stores.h) the
// jets and the submarine carrier carry. The stock MissileBullet01 (vtable 0x17A1C10, update slot 5 0x26A880) is
// pure pursuit: it turns its velocity straight at the lock point, at most Ammo_CustomParameter[5] rad a frame whatever
// its speed, so a target inside its turning circle is circled for ever (the user's screenshot, 2026-10-04). Our rounds
// keep the stock motion, motor sound, life and blast, with the stock steering off (CP[8], the homing delay,
// kNoStockHoming) and CP[9] kPluginMark telling them apart; before each stock update (Guide) the plugin:
//  - keeps the launcher's velocity vector: the stock round splits its velocity into its own part (+0x13D0, which the
//    stock code speeds up by CP[4] a frame along itself and clamps to CP[6]) and what it inherited from the
//    launcher (AmmoOwnerMove 1: +0x13E0, which the stock code fades by 0.9 a frame); each frame the inherited part
//    is folded into its own, so nothing fades;
//  - burns its motor for its burn time, then coasts: kCoastDrag of its speed lost a frame (the stock thrust taken back);
//  - steers by proportional navigation: an acceleration across its path of its navigation constant times the line
//    of sight's turn rate crossed with its velocity, at most its g limit: it flies to where the target will be;
// The burn, the g limit and the navigation constant are the missile's own (Ammo_CustomParameter[3], which the stock
// round stores at +0x1390 and never reads; vcobjects.py Missile.motion): an air-to-air
// missile burns long, a ship's missile is another; acceleration and top speed are CP[4] / CP[6] as stock.
//  - fuses by proximity: closer than kFuseShare of its blast radius (AmmoExplosion) to the target within the coming
//    frame, it goes off now (core +0xAF4 |= 0x20, its age set to its life: the stock expiry blasts it there).
// Docs: docs/missile-re.md. All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include "vecmath.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
using vec::Clamp;using vec::Cross;using vec::Dot;using vec::Len;using vec::Normalize;
constexpr std::size_t kVtable=0x17A1C10,kUpdate=0x26A880,kSlotUpdate=5;
const unsigned char kUpdateSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57};
// The round (B): its lock entry (+0x10 the aim point, +0x29 valid), velocities (m a frame), CP[4] / CP[6] / CP[8] /
// CP[9]; its core (B + 0x140): position, flags (bit 0 dead, 0x20 blast on expiry), age and life (frames), blast radius.
constexpr std::size_t kLock=0xB10,kLockCtrl=0xB18,kLockAim=0x10,kLockValid=0x29;   // B+0xB18: the entry's control block
constexpr std::size_t kGuidance=0x1390;   // CP[3]: burn frames, g limit, navigation constant
constexpr std::size_t kIgnition=0x13AC,kFlown=0x1400;   // CP[7][0] (frames before ignition), frames since launch
constexpr std::size_t kOwn=0x13D0,kInherited=0x13E0,kAccel=0x13A0,kHomingDelay=0x13B8,kHomingFrames=0x13BC;
constexpr std::size_t kCore=0x140,kPos=kCore+0xB80,kFlags=kCore+0xAF4,kAge=kCore+0xAF8,kLife=kCore+0xA08,kBlast=kCore+0xA20;
constexpr std::int32_t kNoStockHoming=1000000,kPluginMark=4242;   // vcobjects.py MISSILE_NO_STOCK_HOMING / MISSILE_MARK
constexpr std::uint32_t kDead=1,kBlastOnExpiry=0x20;
constexpr float kFrame=1.0f/60.0f;
// The guidance's bounds (CP[3] outside them is clamped): burn frames, g, navigation constant.
constexpr float kBurnMost=3600.0f,kGLeast=1.0f,kGMost=80.0f,kNavLeast=2.0f,kNavMost=6.0f;
constexpr float kCoastDrag=0.004f;       // of its speed a frame after burnout (600 m/s -> ~330 in 2.5 s)
constexpr float kMinSpeed=1.0f;          // m a frame: no slower (it falls out of the sky as its life ends)
constexpr float kFuseShare=0.6f,kFuseLeast=4.0f;   // m
constexpr float kG=9.8f;

// Each round guided: the target where it was last frame (its velocity, m a frame), and the game frame it was last
// guided in: an entry not guided for kStaleFrames is a round gone (dead rounds are never told of), free again.
struct Round { const unsigned char* b; std::int32_t age; ULONGLONG frame; float last[3]; bool seen; };
constexpr int kRounds=128;
constexpr ULONGLONG kStaleFrames=2;
Round rounds[kRounds]{};

using UpdateFn=void(__fastcall*)(void*,void*,void*,void*);
UpdateFn nextUpdate=nullptr;
bool guideOk=false;

// The entry of round `b` (`age` frames old): its own while it was guided last frame at a younger age; a new round at
// that address (younger than the entry, or the entry stale) starts afresh in that entry, so no address has two. A
// new address takes a free or stale entry, else the least recently guided.
}  // namespace

// A round guided this frame or the last whose lock point (Round::last) is within `radius` of `at`: a missile coming
// for whatever is there (the player's missile warning, playerjet.cpp).
bool MissileHoming(const float* at,float radius) noexcept {
    const ULONGLONG frame=GameFrame();
    for(const auto& r:rounds) {
        if(!r.b || !r.seen || frame-r.frame>1)continue;
        const float d[3]={r.last[0]-at[0],r.last[1]-at[1],r.last[2]-at[2]};
        if(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]<radius*radius)return true;
    }
    return false;
}

namespace {
Round& RoundOf(const unsigned char* b,std::int32_t age,ULONGLONG frame) noexcept {
    Round* free=nullptr;
    for(auto& r:rounds) {
        if(r.b==b) {
            if(age<r.age || frame-r.frame>kStaleFrames)r=Round{b,age,frame,{},false};
            return r;
        }
        const bool stale=!r.b || frame-r.frame>kStaleFrames;
        if(!free || (stale && free->b && frame-free->frame<=kStaleFrames) || (!stale && free->b && r.frame<free->frame))free=&r;
    }
    *free=Round{b,age,frame,{},false};
    return *free;
}

void Detonate(unsigned char* b,const float* r) noexcept {
    Put<std::uint32_t>(b,kFlags,At<std::uint32_t>(b,kFlags)|kBlastOnExpiry);
    Put<std::int32_t>(b,kAge,At<std::int32_t>(b,kLife));
    if(Cfg().debug)Log("MISSILE %p proximity fuse: %.1f m off, age %d",b,Len(r),At<std::int32_t>(b,kAge));
}

void Guide(unsigned char* b) noexcept {
    if(At<const void*>(b,0)!=image+kVtable)return;
    if(At<std::int32_t>(b,kHomingDelay)!=kNoStockHoming || At<std::int32_t>(b,kHomingFrames)!=kPluginMark)return;
    if(At<std::uint32_t>(b,kFlags)&kDead)return;
    // Before its motor lights (CP[7][0] frames; 0 for every store) the stock code fades and drops what it inherited:
    // left to it.
    if(At<std::int32_t>(b,kFlown)<At<std::int32_t>(b,kIgnition))return;
    const std::int32_t age=At<std::int32_t>(b,kAge);
    Round& round=RoundOf(b,age,GameFrame());
    round.age=age;round.frame=GameFrame();
    float* own=reinterpret_cast<float*>(b+kOwn);
    float* inherited=reinterpret_cast<float*>(b+kInherited);
    float vel[3];
    for(int i=0;i<3;++i)vel[i]=own[i]+inherited[i];
    float speed=Len(vel);
    if(!std::isfinite(speed))return;
    float dir[3]={vel[0],vel[1],vel[2]};
    if(!Normalize(dir))return;
    const float* guidance=reinterpret_cast<const float*>(b+kGuidance);
    const float burn=std::isfinite(guidance[0]) ? Clamp(guidance[0],0.0f,kBurnMost) : 0.0f;
    const float maxG=std::isfinite(guidance[1]) ? Clamp(guidance[1],kGLeast,kGMost) : kGLeast;
    const float nav=std::isfinite(guidance[2]) ? Clamp(guidance[2],kNavLeast,kNavMost) : kNavLeast;
    // Proportional navigation at the lock point.
    const auto lock=At<const unsigned char*>(b,kLock);
    const auto ctrl=At<const unsigned char*>(b,kLockCtrl);
    const float* pos=reinterpret_cast<const float*>(b+kPos);
    // As the stock steering takes it (0x269C0A): the entry is the target's (its lock point, rewritten every frame by
    // its own update, 0x6C7700); the round's reference is weak: with the target gone the point stops, so a dead
    // entry (use count 0) is no lock, it flies on.
    if(lock && ctrl && Readable(ctrl,0x10) && At<std::int32_t>(ctrl,8)>0 && Readable(lock,kLockValid+1) && lock[kLockValid]) {
        const float* aim=reinterpret_cast<const float*>(lock+kLockAim);
        float tv[3]={0.0f,0.0f,0.0f};
        if(round.seen)for(int i=0;i<3;++i)tv[i]=aim[i]-round.last[i];
        if(Len(tv)>20.0f)tv[0]=tv[1]=tv[2]=0.0f;   // the lock moved to another target: no velocity from that
        std::memcpy(round.last,aim,12);round.seen=true;
        const float r[3]={aim[0]-pos[0],aim[1]-pos[1],aim[2]-pos[2]};
        const float rv[3]={tv[0]-vel[0],tv[1]-vel[1],tv[2]-vel[2]};
        const float r2=Dot(r,r);
        // Closest approach within the coming frame.
        const float v2=Dot(rv,rv),t=v2>1e-6f ? Clamp(-Dot(r,rv)/v2,0.0f,1.0f) : 0.0f;
        const float miss[3]={r[0]+rv[0]*t,r[1]+rv[1]*t,r[2]+rv[2]*t};
        const float blast=At<float>(b,kBlast),fuse=std::isfinite(blast) && blast*kFuseShare>kFuseLeast ? blast*kFuseShare : kFuseLeast;
        if(Len(miss)<fuse){Detonate(b,miss);return;}
        if(r2>1.0f) {
            float w[3];Cross(r,rv,w);
            for(auto& x:w)x/=r2;   // the line of sight's turn, rad a frame
            float a[3];Cross(w,vel,a);
            for(auto& x:a)x*=nav;
            const float along=Dot(a,dir);
            for(int i=0;i<3;++i)a[i]-=dir[i]*along;
            const float most=maxG*kG*kFrame*kFrame,len=Len(a);
            if(len>most)for(auto& x:a)x*=most/len;
            for(int i=0;i<3;++i)dir[i]=vel[i]+a[i];
            if(!Normalize(dir))return;
        }
    } else round.seen=false;   // no lock: the next one starts its target's motion afresh
    // The motor: the stock code adds CP[4] along its velocity after this; past its burn that is taken back here.
    if(static_cast<float>(age)>burn) {
        const float accel=At<float>(b,kAccel);
        speed=speed*(1.0f-kCoastDrag)-(std::isfinite(accel) ? accel : 0.0f);
        if(speed<kMinSpeed)speed=kMinSpeed;
    }
    for(int i=0;i<3;++i){own[i]=dir[i]*speed;inherited[i]=0.0f;}
}

void __fastcall UpdateHook(void* b,void* a2,void* a3,void* a4) noexcept {
    if(Cfg().enabled) {
        __try { Guide(static_cast<unsigned char*>(b)); }
        __except(EXCEPTION_EXECUTE_HANDLER){}
    }
    nextUpdate(b,a2,a3,a4);
}
}  // namespace

bool InstallMissiles() noexcept {
    __try {
        if(!Matches(kUpdate,kUpdateSig,sizeof(kUpdateSig))){Log("HOOK missiles=0 (unexpected EDF.dll code: stock pursuit)");return false;}
        const auto slot=reinterpret_cast<void**>(image+kVtable)+kSlotUpdate;
        void* const current=*slot;
        if(current!=image+kUpdate)Log("MISSILE update: chaining onto %p (another plugin)",current);
        nextUpdate=reinterpret_cast<UpdateFn>(current);
        guideOk=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&UpdateHook));
        Log("HOOK missiles=%d (proportional navigation, proximity fuse)",guideOk);
        return guideOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void ResetMissiles() noexcept {
    for(auto& r:rounds)r=Round{};
}
}  // namespace crew
