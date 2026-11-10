// The Andryusha's fuses and bomblets offline (src/andryusha.h; docs/andryusha-re.md, test site #37):
//   cmake --build build --target andryusha_check && build\andryusha_check.exe
#include "../src/andryusha.h"
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace crew::andryusha;

namespace {
int failed=0,checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failed;std::printf("FAIL %s\n",what);}
}
float AsFloat(std::uint32_t bits) { float f;std::memcpy(&f,&bits,4);return f; }
float Flat(const float* a,const float* b) { return std::sqrt((a[0]-b[0])*(a[0]-b[0])+(a[2]-b[2])*(a[2]-b[2])); }
// A slope: the ground rises 0.1 m a metre east of x = 0.
float Slope(void*,float x,float,float) { return x>0.0f ? 0.1f*x : 0.0f; }
}  // namespace

int main() {
    // The marks: the Katyusha's contact (1.5 m at AmmoSize 1.34) to within a millimetre, distinct, round-tripped.
    Check(std::fabs(AsFloat(kMarkImpactBits)*1.34f-1.5f)<1e-3f,"impact mark keeps the contact sphere");
    Check(std::fabs(AsFloat(kMarkAirburstBits)*1.34f-1.5f)<1e-3f,"airburst mark keeps the contact sphere");
    Check(kMarkImpactBits!=kMarkAirburstBits,"two marks");
    Check(FuseOf(kMarkImpactBits)==Fuse::impact && FuseOf(kMarkAirburstBits)==Fuse::airburst,"marks read back");
    Check(FuseOf(0x3F8F4880u)==Fuse::none && FuseOf(0x3F800000u)==Fuse::none && FuseOf(0x3F801005u)==Fuse::none,
          "the Katyusha's, a stock 1.0 and a barrage's mark are no Andryusha's");
    Check(MarkOf(Toggled(Fuse::impact))==kMarkAirburstBits && MarkOf(Toggled(Fuse::airburst))==kMarkImpactBits,"toggle");

    // The airburst fuse.
    Check(!AirburstDue(20.0f,-50.0f,kArmFrames-1),"not armed in its first second");
    Check(!AirburstDue(20.0f,30.0f,600),"never while climbing");
    Check(!AirburstDue(kBurstHeight+5.0f,-60.0f,600),"too high");
    Check(AirburstDue(kBurstHeight+0.5f,-60.0f,600),"passes under the height this frame (1 m a frame)");
    Check(AirburstDue(kBurstHeight-3.0f,-10.0f,600),"under the height");
    Check(!AirburstDue(-1.0f,-60.0f,600),"no ground under it: the impact fuse");
    Check(ProbeDepth(-120.0f)>kBurstHeight+2.0f*120.0f/60.0f,"the probe reaches two frames past the height");

    // An impact's bomblets: on a ring kImpactRingMin..Max round the point, evenly round, from just over it.
    const float at[3]={100.0f,5.0f,-40.0f};
    float from[3],aims[kBomblets][3];
    ImpactAims(at,SeedAt(at),nullptr,nullptr,from,aims);
    Check(from[0]==at[0] && from[2]==at[2] && std::fabs(from[1]-at[1]-kImpactLift)<1e-4f,"impact: leave over the point");
    bool ring=true,spread=true;
    for(int i=0;i<kBomblets;++i) {
        const float d=Flat(aims[i],at);
        if(d<kImpactRingMin-1e-3f || d>kImpactRingMax+1e-3f)ring=false;
        if(aims[i][1]!=at[1])ring=false;
        for(int j=0;j<i;++j)if(Flat(aims[i],aims[j])<2.0f)spread=false;
    }
    Check(ring,"impact: every aim on the ring, at the point's height without a ground");
    // Evenly round: every eighth of the circle has one.
    int sectors[kBomblets]{};
    const float turn=std::atan2(aims[0][2]-at[2],aims[0][0]-at[0]);
    for(int i=0;i<kBomblets;++i) {
        float a=std::atan2(aims[i][2]-at[2],aims[i][0]-at[0])-turn+kPi/static_cast<float>(kBomblets);
        while(a<0.0f)a+=2.0f*kPi;
        while(a>=2.0f*kPi)a-=2.0f*kPi;
        ++sectors[static_cast<int>(a/(2.0f*kPi)*static_cast<float>(kBomblets))%kBomblets];
    }
    bool even=true;
    for(int s:sectors)if(s!=1)even=false;
    Check(even && spread,"impact: one bomblet in each eighth of the circle, apart");
    // The ground under each aim is the caller's.
    float slope[kBomblets][3];
    ImpactAims(at,SeedAt(at),&Slope,nullptr,from,slope);
    bool onSlope=true;
    for(int i=0;i<kBomblets;++i)if(std::fabs(slope[i][1]-Slope(nullptr,slope[i][0],slope[i][2],0.0f))>1e-4f)onSlope=false;
    Check(onSlope,"impact: aims on the ground the caller finds");
    // The same burst splits alike (every machine), another one otherwise.
    float again[kBomblets][3],other[kBomblets][3];
    ImpactAims(at,SeedAt(at),nullptr,nullptr,from,again);
    const float nudged[3]={at[0]+0.5f,at[1],at[2]};
    ImpactAims(nudged,SeedAt(nudged),nullptr,nullptr,from,other);
    Check(std::memcmp(again,aims,sizeof(aims))==0,"impact: deterministic");
    Check(std::memcmp(other,aims,sizeof(aims))!=0,"impact: another point, another pattern");

    // An airburst's bomblets: from the burst point onto the disc under it, spread over it.
    const float high[3]={-20.0f,60.0f,300.0f},below[3]={-20.0f,25.0f,300.0f};
    AirburstAims(high,below,SeedAt(high),nullptr,nullptr,from,aims);
    Check(std::memcmp(from,high,12)==0,"airburst: leave the burst point");
    bool disc=true;
    float farthest=0.0f,nearest=1e9f;
    for(int i=0;i<kBomblets;++i) {
        const float d=Flat(aims[i],below);
        if(d>kAirburstSpread+1e-3f || aims[i][1]!=below[1])disc=false;
        if(d>farthest)farthest=d;
        if(d<nearest)nearest=d;
        for(int j=0;j<i;++j)if(Flat(aims[i],aims[j])<3.0f)disc=false;
    }
    Check(disc,"airburst: every aim on the disc under it, apart");
    Check(farthest>0.8f*kAirburstSpread && nearest<0.4f*kAirburstSpread,"airburst: the disc covered, centre to edge");

    std::printf("andryusha_check: %d checks, %d failed\n",checks,failed);
    return failed ? 1 : 0;
}
