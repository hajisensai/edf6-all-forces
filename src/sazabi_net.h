// Pointer-free snapshots, carried by the 506 NetworkObject. 888 bytes leaves the stock EOS 1170-byte packet room
// for its object/event envelope; no coop fragmentation is required. The drill uses a different (505) vtable slot.
#pragma once
#include "drill_net.h"
#include <type_traits>
namespace crew::sazabi_net {
constexpr std::int8_t kTag=15;
constexpr std::uint32_t kMagic=0x49425A53,kVersion=1;
constexpr std::uint32_t kDriven=1,kNpc=2,kAir=4,kBoost=8,kClimb=16,kOverheat=32,kFlags=63;
struct Pose {
    float t=0,gait=0,stride=0,air=0,lean=0,bank=0,crouch=0,aimPitch=0,aimYaw=0,aim=0,guard=0,swing=-1;
    std::int32_t combo=0;
    float boost=0,recoil=0,cannon=0;
};
struct Motion { float heading=0,aimPitch=0,yawRate=0,sinceBurst=0,vel[3]{},gauge=1; };
struct Arms {
    float charge=0,cannonCool=0,megaLeft=0,megaSize=1,guardShare=1;
    float aim[3]{},megaFrom[3]{},megaEnd[5][3]{};
};
struct Funnel {
    std::uint32_t phase=0;
    std::int32_t target=-1,order=0,shots=0;
    float recharge=0,sortie=0,at[3]{},dir[3]{0,0,1},vel[3]{};
};
struct Shot { std::uint32_t sequence=0,age=0;float from[3]{},at[3]{}; };
struct Sound { std::uint32_t sequence=0,kind=0,age=0;float at[3]{}; };
struct State {
    std::uint32_t magic=kMagic,version=kVersion;
    std::uint64_t sender=0;
    std::uint32_t sequence=0;
    std::int32_t controller=-1;
    std::uint32_t flags=0,special=0;
    Pose pose;
    Motion motion;
    Arms arms;
    Funnel funnels[6];
    Shot shots[6];
    Sound sounds[4];
    std::uint32_t reserved=0,reserved2=0;
};
static_assert(sizeof(Pose)==64 && sizeof(Motion)==32 && sizeof(Arms)==104 && sizeof(Funnel)==60);
static_assert(sizeof(State)==888 && offsetof(State,reserved)==880,"no implicit tail padding or oversized packets");
static_assert(std::is_trivially_copyable_v<State> && std::is_standard_layout_v<State>);
inline bool Finite(const float* p,std::size_t n,float bound=1.0e7f) noexcept {
    for(std::size_t i=0;i<n;++i)if(!std::isfinite(p[i]) || std::fabs(p[i])>bound)return false;
    return true;
}
inline bool Unit(const float* p) noexcept {
    const float q=p[0]*p[0]+p[1]*p[1]+p[2]*p[2];return q>0.9f && q<1.1f;
}
inline bool Valid(const State& s) noexcept {
    if(s.magic!=kMagic || s.version!=kVersion || !s.sender || !s.sequence || s.controller< -1 ||
       (s.flags&~kFlags) || s.special>2 || s.reserved || s.reserved2)return false;
    const Pose& p=s.pose;
    if(!Finite(&p.t,12) || !Finite(&p.boost,3) || p.combo<0 || p.combo>2 || p.swing< -1 || p.swing>1.1f ||
       p.guard<0 || p.guard>1 || p.stride<0 || p.stride>1 || p.air<0 || p.air>1 || p.aim<0 || p.aim>1)return false;
    if(!Finite(&s.motion.heading,8) || s.motion.gauge<0 || s.motion.gauge>1 || !Finite(&s.arms.charge,26) ||
       s.arms.charge<0 || s.arms.charge>1 || s.arms.megaLeft<0 || s.arms.megaLeft>2 ||
       s.arms.guardShare<0 || s.arms.guardShare>1 || s.arms.megaSize<=0 || s.arms.megaSize>100)return false;
    for(const auto& f:s.funnels)if(f.phase>5 || f.target< -1 || f.order<0 || f.order>5 || f.shots<0 || f.shots>3 ||
        !Finite(&f.recharge,11) || f.recharge<0 || f.sortie<0 || !Unit(f.dir))return false;
    for(const auto& x:s.shots)if(!Finite(x.from,3) || !Finite(x.at,3) || x.age>60000)return false;
    for(const auto& x:s.sounds)if(x.kind>=14 || x.age>60000 || !Finite(x.at,3))return false;
    return true;
}
inline bool Decode(const void* data,std::size_t size,State& out) noexcept {
    if(!data || size!=sizeof(State))return false;
    State s;std::memcpy(&s,data,sizeof(s));if(!Valid(s))return false;out=s;return true;
}
struct Gate {
    drill_net::Gate watermarks;
    bool Admit(const State& s,bool session,bool authority,std::int32_t controller) noexcept {
        if(!Valid(s))return false;
        drill_net::State key;key.sender=s.sender;key.sequence=s.sequence;key.controller=s.controller;
        return watermarks.Admit(key,session,authority,controller);
    }
};
} // namespace crew::sazabi_net
namespace crew {
bool InstallSazabiNet() noexcept;
bool SazabiNetSend(unsigned char*,sazabi_net::State) noexcept;
std::int32_t SazabiNetController(unsigned char*) noexcept;
std::int32_t SazabiNetReference(const void*) noexcept;
void SazabiNetReceived(unsigned char*,const sazabi_net::State&) noexcept;
}
