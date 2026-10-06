// NPC tanks back to their post (docs/npc-ai-design.md §8; ini TankReturnToPost, TankPostHold, TankReverseMax).
//
// The stock tank AI (CarBase action 0x661440, run from slot 7) drives only along a script's route (0x660490) or after a
// target outside its turret's traversal (0x661020 with r8b 1, turn only); otherwise it writes no move at all. A tank its own main gun's
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
constexpr std::size_t kSeatSteer=0x2C0,kSeatThrottle=0x2C4;
constexpr float kBackAngle=2.199f;                  // 0x17DEBAC: the stock reverses past this off the nose
constexpr float kTurnOnSpot=0.314159f;              // 0x17A3E04: the stock turns on the spot past this
constexpr float kGoalNear=30.0f;                    // m: +0x25E0 this near when first seen is its spawn point
constexpr ULONGLONG kStaleMs=2000,kLogMs=2000;
constexpr int kMaxPosts=64;

struct Post { ObjRef ref; ULONGLONG seen,loggedAt; float at[3],home[3]; bool commanded; bool active; Command cmd; };
Post posts[kMaxPosts]{};
ULONGLONG fullLoggedAt=0;

Post* PostOf(const unsigned char* v,ULONGLONG ms) noexcept {
    Post* slot=nullptr;
    for(auto& p:posts) {
        if(p.ref.Is(v) && p.seen && ms-p.seen<=kStaleMs)return &p;
        if(p.ref.Is(v)){slot=&p;break;}
        if(!slot && (!p.ref || p.ref.obj==v || ms-p.seen>kStaleMs))slot=&p;
    }
    if(!slot) {
        if(ms-fullLoggedAt>10000){fullLoggedAt=ms;Log("NPCPOST table full: v=%p keeps no post",v);}
        return nullptr;
    }
    // After a player or script drove it, even the old spawn goal nearby is no longer its post.
    const bool resumed=slot->ref.Is(v);
    *slot=Post{};
    slot->ref=ObjRef::Of(v);
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float* goal=reinterpret_cast<const float*>(v+kStockGoal);
    const bool spawn=!resumed && std::isfinite(goal[0]+goal[1]+goal[2]) && npc::Horiz(pos,goal)<kGoalNear;
    std::memcpy(slot->at,spawn ? goal : pos,12);
    std::memcpy(slot->home,slot->at,12);
    Log("NPCPOST v=%p %s post (%.0f,%.0f,%.0f)",v,spawn ? "spawn" : "first-seen",slot->at[0],slot->at[1],slot->at[2]);
    return slot;
}

// Slot 7 clears the seat block (0x673330..0x673349), then runs the stock action. Respect what that action actually
// wrote: 0x6616EB tests turret traversal, not weapon range, and 0x66172E turns towards an out-of-arc enemy.
bool StockDriving(const unsigned char* seat) noexcept {
    return At<float>(seat,kSeatSteer)!=0.0f || At<float>(seat,kSeatThrottle)!=0.0f;
}

bool TankAi(const unsigned char* v) noexcept {
    const auto vt=At<void* const*>(v,0);
    return Readable(vt,(kSlotAi+1)*8) && vt[kSlotAi]==image+kTankAi;
}

bool Available(const Post& p,ULONGLONG ms) noexcept {
    const auto v=static_cast<const unsigned char*>(p.ref.obj);
    return Cfg().enabled && Cfg().customNpcAi && Cfg().tankReturnToPost && !InSession() &&
        p.seen && ms-p.seen<=500 && Readable(v,kStockGoal+12) && p.ref.Is(v) && !v[kDead] && TankAi(v) &&
        !At<const void*>(v,kRoute) && SeatCount(v)>0 && SeatRider(SeatAt(const_cast<unsigned char*>(v),0))==Rider::dummy;
}

void Relinquish(const unsigned char* v) noexcept {
    for(auto& p:posts)if(p.ref.Is(v)) {
        const ObjRef ref=p.ref;
        p=Post{};p.ref=ref; // retain only the identity so a later NPC starts at its new position
    }
}
}  // namespace

bool NpcDrivable(const unsigned char* v) noexcept {
    return TankAi(v) && At<std::uint64_t>(v,kHolderCount)>0;   // an unarmed truck of the Grape's class: no driver (crew.cpp Crew)
}

void NpcPostInput(unsigned char* v) noexcept {
    if(!Cfg().customNpcAi || !Cfg().tankReturnToPost || v[kDead] || !TankAi(v))return;
    if(SeatCount(v)==0 || SeatRider(SeatAt(v,0))!=Rider::dummy) {
        // Not NPC-driven (the player took the wheel, or nobody): its post is forgotten; an NPC later starts from where it is.
        Relinquish(v);
        return;
    }
    if(At<const void*>(v,kRoute)){Relinquish(v);return;} // even a short script route invalidates the old post
    if(InSession() && !IsRoomHost())return;
    const ULONGLONG ms=GameMs();
    Post* const p=PostOf(v,ms);
    if(!p)return;
    p->seen=ms;
    unsigned char* const seat=SeatAt(v,0);
    if(!(At<std::uint32_t>(v,kDriveMode)&1) || StockDriving(seat)){p->active=false;return;}
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    const npc::Steer s=npc::ReturnToPost(reinterpret_cast<const float*>(v+kPosition),m,m+8,p->at,Cfg().tankPostHold,
                                         Cfg().tankReverseMax,kBackAngle,kTurnOnSpot);
    if(s.active!=p->active)Log("NPCPOST v=%p %s (%.1f m off its post)",v,s.active ? "driving back" : "back at its post",s.dist);
    p->active=s.active;
    if(!s.active)return;
    Put<float>(seat,kSeatSteer,npc::SteerStick(s));
    Put<float>(seat,kSeatThrottle,npc::ThrottleStick(s));
    if(Cfg().debug && ms-p->loggedAt>kLogMs) {
        p->loggedAt=ms;
        Log("NPCPOST v=%p dist=%.1f bearing=%.2f reverse=%d steer=%.2f throttle=%.2f",v,s.dist,s.bearing,s.reverse,npc::SteerStick(s),npc::ThrottleStick(s));
    }
}

bool NpcPostCommand(const void* v,const float* at) noexcept {
    for(auto& p:posts) {
        if(!p.ref.Is(v) || !Available(p,GameMs()))continue;
        std::memcpy(p.at,at ? at : p.home,12);p.commanded=at!=nullptr;
        Log("NPCPOST v=%p post %s (%.0f,%.0f,%.0f) by a command",v,at ? "moved to" : "back on its spawn",p.at[0],p.at[1],p.at[2]);
        return true;
    }
    return false;
}

// The map's tanks (mapcmd.cpp): those keeping a post now (seen within kCommandSeenMs).
int TankCommandUnits(CommandUnit* out,int most) noexcept {
    const ULONGLONG ms=GameMs();
    int n=0;
    for(const auto& p:posts)
        if(n<most && Available(p,ms) && ReadCommandUnit(p.ref,"TANK",p.cmd,false,&out[n])) { out[n].status=p.active ? "RETURNING" : "AT POST"; ++n; }
    return n;
}
bool TankCommand(const void* v,const Command& c) noexcept {
    if(c.order!=Order::guard && c.order!=Order::none)return false;
    if(!NpcPostCommand(v,c.order==Order::guard ? c.at : nullptr))return false;
    for(auto& p:posts)if(p.ref.Is(v))p.cmd=c;
    return true;
}

void ResetNpcPosts() noexcept {
    for(auto& p:posts)p=Post{};
    fullLoggedAt=0;
}
}  // namespace crew
