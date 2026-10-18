// More than four players in an EDF5 / EDF4.1 script mission online, the pure parts (src/edf5online_room.h): EDF6Coop's
// version marker, its hook shape, the room word; and the one spawn layout both plugins use (src/spawn_layout.h), to the
// bit: the golden values below belong in EDF6Coop's test of its copy too. No game, no Coop.
#include "../src/edf5online_room.h"
#include "../src/spawn_layout.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
std::uint32_t Bits(float f) { std::uint32_t b;std::memcpy(&b,&f,4);return b; }

// EDF6Coop's SpawnOffset before it took spawn_layout.h (multislot/src/mission.cpp, 2.5.0): the CRT's float cos / sin.
void CoopFloatRing(std::uint32_t index,const float* table,float* out) {
    std::memcpy(out,table,4*sizeof(float));
    const float dx=table[4]-table[0],dz=table[6]-table[2];
    float spacing=std::sqrt(dx*dx+dz*dz);
    if(!(spacing>0.5f))spacing=3.0f;
    const float first=spacing*(2.0f+static_cast<float>((32-5)/3))+spacing;
    std::uint32_t slot=index-32;
    for(int ring=0;;++ring) {
        const float radius=first+spacing*static_cast<float>(ring);
        const auto seats=static_cast<std::uint32_t>(6.2831853f*radius/spacing);
        if(slot<seats) {
            const float angle=6.2831853f*static_cast<float>(slot)/static_cast<float>(seats);
            out[0]=table[0]+radius*std::cos(angle);
            out[2]=table[2]+radius*std::sin(angle);
            return;
        }
        slot-=seats;
    }
}
}  // namespace

int main(int argc,char** argv) {
    using namespace crew::bvmroom;
    int a=0,b=0,c=0;
    Check(ParseCoopVersion("EDF6COOP_VERSION=2.5.0",a,b,c) && a==2 && b==5 && c==0,"2.5.0 read");
    Check(ParseCoopVersion("EDF6COOP_VERSION=10.12.345",a,b,c) && a==10 && b==12 && c==345,"many digits");
    Check(!ParseCoopVersion("EDF6COOP_VERSION=2.5",a,b,c) && !ParseCoopVersion("EDF6COOP_VERSION=2.5.0x",a,b,c) &&
          !ParseCoopVersion("EDF6COOP_8P_VERSION=2.5.0",a,b,c) && !ParseCoopVersion(nullptr,a,b,c) &&
          !ParseCoopVersion("EDF6COOP_VERSION=.5.0",a,b,c),"a marker of another shape is no version");
    Check(AtLeast(2,5,0,2,5,0) && AtLeast(2,6,0,2,5,0) && AtLeast(3,0,0,2,5,0) && AtLeast(2,5,1,2,5,0),"2.5.0 and later");
    Check(!AtLeast(2,4,9,2,5,0) && !AtLeast(1,9,9,2,5,0),"older than 2.5.0");
    const unsigned char hooked8[]={0xE9,1,2,3,4,0x90,0x90,0x90},hooked7[]={0xE9,1,2,3,4,0x90,0x90};
    const unsigned char stock8[]={0x4B,0x8B,0x94,0xC7,0x78,0x01,0x00,0x00},stock7[]={0x49,0x8B,0x90,0x78,0x01,0x00,0x00};
    const unsigned char other[]={0xE9,1,2,3,4,0xCC,0xCC,0xCC};
    Check(CoopJumpShape(hooked8,8) && CoopJumpShape(hooked7,7),"EDF6Coop's jmp + nops");
    Check(!CoopJumpShape(stock8,8) && !CoopJumpShape(stock7,7) && !CoopJumpShape(other,8) && !CoopJumpShape(hooked8,4),
          "stock bytes, another plugin's jump, a short site: not EDF6Coop's hook");
    Check(kAllCoopSites==0xF && sizeof(kCoopSites)/sizeof(kCoopSites[0])==4,"four sites");
    Check(Capable("1") && !Capable("") && !Capable("2") && !Capable(nullptr) && !Capable("1;"),"the word");
    Check(std::strlen(kKey)<=32 && !std::strncmp(kKey,"EDF6X_",6),"a member attribute key EDF6Coop takes");

    // The layout: the creation loop's four finished offsets of a spawn point turned 30 degrees (the BVM / AngelScript
    // constants (0,0,0) (-1,0,-1) (1,0,-1) (0,0,-2), normalised, scaled by 1, turned).
    const float table[16]={0,0,0,0, -0.25881904f,0,-0.96592583f,0, 0.96592583f,0,-0.25881904f,0, 0.5f,0,-0.8660254f,0};
    struct Golden { std::uint32_t index,x,z; };
    // spawn_layout.h's bits (x, z); printed by `edf5_online_room_test print`.
    const Golden golden[]={{0,0x00000000u,0x00000000u},{3,0x3F000000u,0xBF5DB3D7u},{4,0xBF0483EEu,0xBFF746EAu},
                           {5,0x3FF746EAu,0xBF0483EEu},{6,0x3F800000u,0xBFDDB3D7u},{31,0xC0363567u,0xC12A00C1u},
                           {32,0x413FFFFFu,0x00000000u},{33,0x413F539Du,0x3F808777u},{40,0x41167819u,0x40EE854Eu},
                           {107,0x414FFFFFu,0x00000000u},{1023,0xC0E4FEF9u,0xC19DF1DCu}};
    if(argc>1 && !std::strcmp(argv[1],"print")) {
        for(const Golden& g:golden) {
            float o[4];edf6spawn::SpawnOffset(g.index,table,o);
            std::printf("{%u,0x%08Xu,0x%08Xu},\n",g.index,Bits(o[0]),Bits(o[2]));
        }
        return 0;
    }
    for(const Golden& g:golden) {
        float o[4];edf6spawn::SpawnOffset(g.index,table,o);
        Check(Bits(o[0])==g.x && Bits(o[2])==g.z,"spawn_layout.h: the golden bits");
        Check(o[1]==0 && o[3]==0,"y and w the table's");
    }
    for(std::uint32_t i=0;i<4;++i) {
        float o[4];edf6spawn::SpawnOffset(i,table,o);
        Check(!std::memcmp(o,table+i*4,sizeof(o)),"players 1-4 the loop's own");
    }
    {
        float o[4];edf6spawn::SpawnOffset(4,table,o);
        Check(o[0]==2*table[4] && o[2]==2*table[6],"player 5 along player 2's direction, twice as far");
        edf6spawn::SpawnOffset(7,table,o);
        Check(o[0]==3*table[4] && o[2]==3*table[6],"player 8 along player 2's, three times");
    }
    // The rings: where EDF6Coop's float cos / sin put them, within a micrometre per metre (the change Coop takes with
    // the header: its players 33.. move by that little), and on the first ring's radius.
    for(std::uint32_t i=32;i<1024;++i) {
        float o[4],f[4];
        edf6spawn::SpawnOffset(i,table,o);
        CoopFloatRing(i,table,f);
        Check(std::fabs(o[0]-f[0])<1e-4f && std::fabs(o[2]-f[2])<1e-4f,"the rings where EDF6Coop's float layout had them");
    }
    for(std::uint32_t i=32;i<40;++i) {
        float o[4];edf6spawn::SpawnOffset(i,table,o);
        Check(std::fabs(std::sqrt(o[0]*o[0]+o[2]*o[2])-12.0f)<1e-4f,"33.. on the first ring, 12 spacings out");
    }
    // sin / cos of the turn against the CRT's in double: within a few ulps of the angle (|angle| < 2 pi, its ulp 8.9e-16).
    for(std::uint32_t seats=1;seats<200;seats+=7)
        for(std::uint32_t slot=0;slot<seats;++slot) {
            double s=0,co=0;edf6spawn::TurnSinCos(slot,seats,s,co);
            const double angle=6.283185307179586*static_cast<double>(slot)/static_cast<double>(seats);
            Check(std::fabs(s-std::sin(angle))<4e-15 && std::fabs(co-std::cos(angle))<4e-15,"the turn's sin and cos");
        }
    std::printf("edf5_online_room_test: %d checks passed\n",checks);
    return 0;
}
