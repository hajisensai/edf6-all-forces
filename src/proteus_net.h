// Proteus object events: driver-owned control/pose and target-owner-owned shield HP are separate streams. Version 2
// (2026-10-09): `barrier` is the native shield's HP share, kBroken its broken lock; version 1 packets are refused.
#pragma once
#include "drill_net.h"
#include "proteus_logic.h"
namespace crew::proteus_net {
constexpr std::int8_t kTag=15;
constexpr std::uint32_t kMagic=0x544E5250,kVersion=2;
enum class Kind : std::uint32_t { control,defense };
constexpr std::uint32_t kActive=1,kShieldOn=2,kOverheated=4,kTwoSeats=8,kBroken=16,kFlags=31;
struct State {
    std::uint32_t magic=kMagic,version=kVersion;
    std::uint64_t sender=0;
    std::uint32_t sequence=0;
    std::int32_t controller=-1;
    Kind kind=Kind::control;
    std::uint32_t flags=0,mode=0,reserved=0;
    float stagger=0,heat=0,nose[3]{0,0,1},barrier=1,quiet=1.0e9f,lift=0;
    float deploySec=1.5f,stowSec=1.5f,shieldBlock=1,shieldArc=120,barrierShare=0.5f,regenSec=40,delaySec=4;
    float fieldRadius=0,fieldDefense=0,fieldAttack=0,fieldFireRate=1,fieldEnergy=0,fieldPower=0;
    std::int32_t fieldTeam=0;
    std::uint32_t reserved2=0,reserved3=0;
};
static_assert(sizeof(State)==136 && offsetof(State,reserved3)==132,"pointer-free, explicit wire layout");
inline bool Valid(const State& s) noexcept {
    if(s.magic!=kMagic || s.version!=kVersion || !s.sender || !s.sequence || s.controller< -1 || s.kind>Kind::defense ||
       (s.flags&~kFlags) || s.mode>3 || s.reserved || s.reserved2 || s.reserved3 || s.fieldTeam<0 || s.fieldTeam>255)return false;
    const float scalars[]={s.stagger,s.heat,s.nose[0],s.nose[1],s.nose[2],s.barrier,s.quiet,s.lift,s.deploySec,s.stowSec,
        s.shieldBlock,s.shieldArc,s.barrierShare,s.regenSec,s.delaySec,s.fieldRadius,s.fieldDefense,s.fieldAttack,s.fieldFireRate,s.fieldEnergy,s.fieldPower};
    for(float v:scalars)if(!std::isfinite(v))return false;
    const float len=s.nose[0]*s.nose[0]+s.nose[1]*s.nose[1]+s.nose[2]*s.nose[2];
    return s.stagger>=0 && s.stagger<=120 && s.heat>=0 && s.heat<=1 && len>0.9f && len<1.1f &&
        s.barrier>=0 && s.barrier<=1 && s.quiet>=0 && s.quiet<=1.1e9f && std::fabs(s.lift)<=100 &&
        s.deploySec>=0 && s.deploySec<=120 && s.stowSec>=0 && s.stowSec<=120 && s.shieldBlock>=0 && s.shieldBlock<=1 &&
        s.shieldArc>=10 && s.shieldArc<=360 && s.barrierShare>=0 && s.barrierShare<=100 &&
        s.regenSec>=0 && s.regenSec<=1.0e6f && s.delaySec>=0 && s.delaySec<=1.0e6f && s.fieldRadius>=0 && s.fieldRadius<=1.0e6f &&
        s.fieldDefense>=0 && s.fieldDefense<=1 && s.fieldAttack>=0 && s.fieldAttack<=100 && s.fieldFireRate>=1 && s.fieldFireRate<=100 &&
        s.fieldEnergy>=0 && s.fieldEnergy<=1.0e6f && s.fieldPower>=0 && s.fieldPower<=1.0e6f;
}
inline bool Decode(const void* bytes,std::size_t size,State& out) noexcept {
    if(!bytes || size!=sizeof(State))return false;State s;std::memcpy(&s,bytes,sizeof(s));
    if(!Valid(s))return false;out=s;return true;
}
struct Gate {
    drill_net::Gate control,defense;
    bool Admit(const State& s,bool session,bool localControl,bool localDefense,std::int32_t controller) noexcept {
        if(!Valid(s))return false;
        drill_net::State key;key.sender=s.sender;key.sequence=s.sequence;key.controller=s.controller;
        return s.kind==Kind::control ? control.Admit(key,session,localControl,controller)
                                    : defense.Admit(key,session,localDefense,controller);
    }
};
}
namespace crew {
bool InstallProteusNet() noexcept;
bool ProteusNetSend(unsigned char*,proteus_net::State) noexcept;
std::int32_t ProteusNetController(unsigned char*) noexcept;
std::int32_t ProteusNetReference(const void*) noexcept;
void ProteusNetReceived(unsigned char*,const proteus_net::State&) noexcept;
}
