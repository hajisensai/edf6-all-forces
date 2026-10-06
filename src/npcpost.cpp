// NPC tanks back to their post (docs/npc-ai-design.md §8; ini TankReturnToPost, TankPostHold, TankReverseMax).
//
// The stock tank AI (CarBase action 0x661440, run from slot 7) drives only along a script's route (0x660490) or after a
// target out of its gun's reach (0x661020 with r8b 1); otherwise it writes no move at all. A tank its own main gun's
// recoil (fix/npc-tank-recoil) or a ram pushed off its spot stays where it was pushed. So for a tank with an NPC in
// seat 0, no route and nothing to chase, the plugin writes seat 0's stick block the way 0x661020 writes it, in the
// vehicle's input slot 55 before the stock input reads it (0x67F0A9 / 0x67F14A for the tanks, 0x65A49F for the Grape):
// the stock action of this frame has written (or cleared) the block already, the next one clears it again, so the
// write holds for exactly the read it is meant for (§8.1).
// The post: a map command's guard point (NpcPostCommand), else where the stock spawn left its navigation target
// (+0x25E0, set from the spawn position by slot 6 0x6731C0, kept while it has no route), else where it was first seen.
// Online: the host alone (the NPC driver has no network identity: every machine takes itself for its authority,
// docs/online-re.md §3.4; the plugin adds no second opinion).
#include "crew.h"
#include "layout.h"
#include "memory.h"
#include "npc_logic.h"
#include <cmath>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kSlotAi=72;
constexpr unsigned kTankAi=0x661440;                 // CarBase's AI action: every tank, the Titan, the Grape
constexpr std::size_t kRoute=0x4A8;
constexpr std::size_t kStockGoal=0x25E0;             // the navigation target (the spawn position until a route)
constexpr std::size_t kDriveMode=0x1AD0;             // bit 0 clear: slot 55 does not read the stick (dl = 0)
constexpr std::size_t kTarget=0x518,kTargetCtrl=0x520;
constexpr std::size_t kSeatSteer=0x2C0,kSeatThrottle=0x2C4;
constexpr std::size_t kArmReach=0x224;
constexpr float kBackAngle=2.199f;                  // 0x17DEBAC: the stock reverses past this off the nose
constexpr float kTurnOnSpot=0.314159f;              // 0x17A3E04: the stock turns on the spot past this
constexpr float kGoalNear=30.0f;                    // m: +0x25E0 this near when first seen is its spawn point
constexpr ULONGLONG kStaleMs=2000,kLogMs=2000;
constexpr int kMaxPosts=64;

struct Post { ObjRef ref; ULONGLONG seen,loggedAt; float at[3]; bool commanded; bool active; };
Post posts[kMaxPosts]{};
ULONGLONG fullLoggedAt=0;

Post* PostOf(const unsigned char* v,ULONGLONG ms) noexcept {
    Post* slot=nullptr;
    for(auto& p:posts) {
        if(p.ref.Is(v))return &p;
        if(!slot && (!p.ref || p.ref.obj==v || ms-p.seen>kStaleMs))slot=&p;
    }
    if(!slot) {
        if(ms-fullLoggedAt>10000){fullLoggedAt=ms;Log("NPCPOST table full: v=%p keeps no post",v);}
        return nullptr;
    }
    *slot=Post{};
    slot->ref=ObjRef::Of(v);
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float* goal=reinterpret_cast<const float*>(v+kStockGoal);
    const bool spawn=std::isfinite(goal[0]+goal[1]+goal[2]) && npc::Horiz(pos,goal)<kGoalNear;
    std::memcpy(slot->at,spawn ? goal : pos,12);
    Log("NPCPOST v=%p %s post (%.0f,%.0f,%.0f)",v,spawn ? "spawn" : "first-seen",slot->at[0],slot->at[1],slot->at[2]);
    return slot;
}

// The longest true reach of seat 0's weapons (0: none read).
float SeatReach(const unsigned char* seat) noexcept {
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto n=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!n || n>8 || !Readable(holders,n*8))return 0.0f;
    float reach=0.0f;
    for(std::uint64_t i=0;i<n;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kArmReach+4))continue;
        const float r=At<float>(w,kArmReach);
        if(std::isfinite(r) && r>reach)reach=r;
    }
    return reach;
}

// The stock AI chases its target now (0x661440: no route, the target out of its seat-0 gun's reach): it drives.
bool Chasing(const unsigned char* v) noexcept {
    const auto t=At<const unsigned char*>(v,kTarget);
    const auto ctrl=At<const unsigned char*>(v,kTargetCtrl);
    if(!t || !ctrl || !Readable(ctrl,0x10) || At<std::int32_t>(ctrl,8)==0 || !Readable(t,kPosition+12))return false;
    const float reach=SeatReach(SeatAt(const_cast<unsigned char*>(v),0));
    return reach>0.0f && npc::Dist(reinterpret_cast<const float*>(v+kPosition),reinterpret_cast<const float*>(t+kPosition))>reach;
}

bool TankAi(const unsigned char* v) noexcept {
    const auto vt=At<void* const*>(v,0);
    return Readable(vt,(kSlotAi+1)*8) && vt[kSlotAi]==image+kTankAi;
}
}  // namespace

void NpcPostInput(unsigned char* v) noexcept {
    if(!Cfg().customNpcAi || !Cfg().tankReturnToPost || v[kDead] || !TankAi(v))return;
    if(SeatCount(v)==0 || SeatRider(SeatAt(v,0))!=Rider::dummy)return;
    if(At<const void*>(v,kRoute))return;                         // a script's route: the stock drives it (§4.3)
    if(InSession() && !IsRoomHost())return;
    const ULONGLONG ms=GameMs();
    Post* const p=PostOf(v,ms);
    if(!p)return;
    p->seen=ms;
    if(!(At<std::uint32_t>(v,kDriveMode)&1) || Chasing(v)){p->active=false;return;}
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const npc::Steer s=npc::ReturnToPost(reinterpret_cast<const float*>(v+kPosition),m,m+8,p->at,Cfg().tankPostHold,
                                         Cfg().tankReverseMax,kBackAngle,kTurnOnSpot);
    if(s.active!=p->active)Log("NPCPOST v=%p %s (%.1f m off its post)",v,s.active ? "driving back" : "back at its post",s.dist);
    p->active=s.active;
    if(!s.active)return;
    unsigned char* const seat=SeatAt(v,0);
    Put<float>(seat,kSeatSteer,npc::SteerStick(s));
    Put<float>(seat,kSeatThrottle,npc::ThrottleStick(s));
    if(Cfg().debug && ms-p->loggedAt>kLogMs) {
        p->loggedAt=ms;
        Log("NPCPOST v=%p dist=%.1f bearing=%.2f reverse=%d steer=%.2f throttle=%.2f",v,s.dist,s.bearing,s.reverse,npc::SteerStick(s),npc::ThrottleStick(s));
    }
}

bool NpcPostCommand(const void* v,const float* at) noexcept {
    for(auto& p:posts) {
        if(!p.ref.Is(v))continue;
        if(at){std::memcpy(p.at,at,12);p.commanded=true;}
        Log("NPCPOST v=%p post moved to (%.0f,%.0f,%.0f) by a command",v,p.at[0],p.at[1],p.at[2]);
        return true;
    }
    return false;
}

void ResetNpcPosts() noexcept {
    for(auto& p:posts)p=Post{};
    fullLoggedAt=0;
}
}  // namespace crew
