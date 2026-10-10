// The container airdrop's data and the carrier's decisions (src/airdrop_logic.h) checked offline: the stock container's
// InitParam and VehicleSetup as the probe read them (docs/evidence/airdrop-probe-2026-10-10.txt), where the carried
// container hangs, and when the plane lets it go on a pass over the point.
#include "../src/airdrop_logic.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(!ok){++failures;std::printf("FAIL %s (%g, %g)\n",what,a,b);}
}
using namespace airdrop;

// The stock call's bytes (probe5, 2026-10-10): create.param+0x30..+0x57 and configure.setup.
void Layouts() {
    ContainerInitParam p{};
    p.vtable=reinterpret_cast<const void*>(0x17D7818);p.counter=kNoCounter;
    const auto* b=reinterpret_cast<const unsigned char*>(&p);
    bool zero=true;
    for(std::size_t i=8;i<0x50;++i)zero=zero && !b[i];
    Check(zero,"a carrier with no network identity: the base and the derived id all zero (0x7767AF skips the fill)");
    std::uint32_t counter=0;std::memcpy(&counter,b+0x50,4);
    Check(counter==0xFFFFFFFFu,"the counter at +0x50 unset, as the offline stock transporter's +0x778 read",counter);
    Check(alignof(ContainerInitParam)==16,"aligned as the game's stack param");

    const VehicleSetup s=EmptySetup();
    const auto* t=reinterpret_cast<const unsigned char*>(&s);
    std::uint64_t size=1,cap=0;std::uint16_t type=0;
    std::memcpy(&size,t+0x10,8);std::memcpy(&cap,t+0x18,8);std::memcpy(&type,t+0x30,2);
    Check(size==0 && cap==7 && !s.text[0],"no text: an empty MSVC wstring (size 0, capacity 7, its own buffer)",
          static_cast<double>(size),static_cast<double>(cap));
    Check(type==0xFFFF,"no value: the variant's type 0xFFFF, so the container hands nothing to the vehicle's slot 46",type);
    // The stock request's setup as configure got it: no text, a value of type 2 (probe: configure.setup+0x30 = 0002).
    VehicleSetup stock=EmptySetup();stock.valueType=2;
    Check(stock.textSize==0 && stock.valueType!=kNoValue,"the stock request's form: a value, no text");
    Check(sizeof(SharedRef)==16,"the requester as a shared_ptr's two words");
}

void Carry() {
    // A carrier heading +x, tilted (nose down a little), at (100, 150, -40).
    const float plane[16]={0,0,-1,0, 0.1f,0.99f,0,0, 0.99f,-0.1f,0,0, 100,150,-40,1};
    float m[16];
    CarryMatrix(plane,m);
    Check(m[12]==100 && std::fabs(m[13]-(150-kBellyDrop))<1e-4f && m[14]==-40,"the container under the carrier's origin",m[13]);
    Check(m[4]==0 && m[5]==1 && m[6]==0,"the container level (its up the world's), whatever the plane's attitude");
    Check(std::fabs(m[8]-1.0f)<1e-4f && std::fabs(m[10])<1e-4f && m[9]==0,"facing the carrier's heading on the level",m[8],m[10]);
    const float cross=m[0]*m[8]+m[2]*m[10];
    Check(std::fabs(cross)<1e-5f && std::fabs(m[0]*m[0]+m[2]*m[2]-1.0f)<1e-5f,"its right square to its forward, unit",cross);
    // The same rows the ground support uses (support_spawn.cpp: {z,0,-x}, {0,1,0}, {x,0,z}).
    const float diag[16]={0,0,0,0, 0,1,0,0, 0.6f,0,0.8f,0, 0,0,0,1};
    CarryMatrix(diag,m);
    Check(std::fabs(m[0]-0.8f)<1e-5f && std::fabs(m[2]+0.6f)<1e-5f,"right = (z, 0, -x)",m[0],m[2]);
    const float straightDown[16]={1,0,0,0, 0,0,-1,0, 0,-1,0,0, 0,50,0,1};
    CarryMatrix(straightDown,m);
    Check(std::isfinite(m[8]) && std::fabs(m[8]*m[8]+m[10]*m[10]-1.0f)<1e-5f,"a carrier pointing straight down: still a unit heading");
}

// A pass over the point at a lateral offset: the level distances a carrier flying straight on at `speed` m per step
// measures, and where it lets go.
struct Pass { bool released; float at; int step; };
Pass Fly(float offset,float speed=6.0f) {
    float closest=1e30f;
    for(int i=0;i<400;++i) {
        const float along=-1200.0f+speed*static_cast<float>(i);
        const float dist=std::sqrt(along*along+offset*offset);
        if(dist<closest)closest=dist;
        if(ReleaseNow(dist,closest,0))return {true,dist,i};
    }
    return {false,0,0};
}

void Release() {
    const Pass over=Fly(0.0f);
    Check(over.released && over.at<=kOverPoint,"straight over the point: let go within kOverPoint of it",over.at);
    const Pass near=Fly(60.0f);
    Check(near.released && near.at>=60.0f+kPassAway && near.at<=60.0f+kPassAway+6.0f,
          "a pass 60 m to the side: let go one step past kPassAway beyond its closest",near.at);
    const Pass far=Fly(300.0f);
    Check(!far.released,"a pass 300 m to the side: kept (it comes round again, or the time limit lets it go)");
    Check(!ReleaseNow(500.0f,500.0f,0) && !ReleaseNow(200.0f,150.0f,0),"approaching, or past a far closest: kept");
    Check(ReleaseNow(kOverPoint,kOverPoint,0),"at kOverPoint: let go");
    Check(!ReleaseNow(145.0f,145.0f,kHoverSettleMs-1) && ReleaseNow(145.0f,145.0f,kHoverSettleMs),
          "hovering short of the point (a map edge's band): let go once it has stopped coming nearer");
    // A fast carrier (the steps 40 m apart) still never skips the point it passes near.
    const Pass fast=Fly(10.0f,40.0f);
    Check(fast.released && fast.at<=kPassNear+40.0f,"a fast pass: let go at its first step past the closest",fast.at);
    Check(kCarryMs>kFallMs,"the carry's limit longer than the fall's");
    // A helicopter coming in to hover: the distance falls to its hover point's few metres and stays there.
    float closest=1e30f;bool let=false;
    for(float d=400.0f;d>=3.0f && !let;d-=5.0f){if(d<closest)closest=d;let=ReleaseNow(d,closest,0);}
    Check(let,"a helicopter slowing into its hover over the point: let go as it comes within kOverPoint");
}
}  // namespace

int main() {
    Layouts();
    Carry();
    Release();
    std::printf("airdrop_check: %d/%d passed\n",cases-failures,cases);
    return failures ? 1 : 0;
}
