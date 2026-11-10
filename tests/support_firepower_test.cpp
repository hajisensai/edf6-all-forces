// The production support firepower step (src/support_firepower.cpp) on a private image: the progress function a stub
// behind the checked prologue, the game status and the vehicles synthetic. No game, no EDF.dll.
#include "../src/support_firepower.cpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
}
namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
bool Near(float a,float b) { return std::fabs(a-b)<=1e-5f*std::fmax(1.0f,std::fabs(b)); }

alignas(16) unsigned char gs[0x100]{};
alignas(16) unsigned char ctrl[0x10]{},deadCtrl[0x10]{};
struct Vehicle {
    unsigned char body[0x700]{};
    unsigned char holders[4*0x48]{};
    explicit Vehicle(unsigned count,float base=1.0f,unsigned char* c=ctrl) {
        crew::Put<const void*>(body,edf::kSelfCtrl,c);
        crew::Put<float>(body,0x678,7.0f);   // what the game's setup wrote
        crew::Put<float>(body,0x67C,base);
        crew::Put<unsigned char*>(body,0x638,holders);
        crew::Put<std::size_t>(body,0x648,count);
        for(unsigned i=0;i<4;++i)crew::Put<float>(holders+i*0x48,0x40,7.0f);
    }
};
void Status(std::int32_t difficulty,float progress) {
    crew::Put<std::int32_t>(gs,0x4C,difficulty);crew::Put<float>(gs,0x48,progress);
}
}  // namespace

int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,kGameStatus+0x100,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private image");
    Put<long>(ctrl,8,1);Put<long>(deadCtrl,8,0);
    Vehicle a(3);
    Check(!MarkSupportFirepower(a.body,supportlevel::Curve::eros) && !SupportFirepowerReady(),"nothing marked before install");
    Check(!InstallSupportFirepower(),"another progress function refused");
    // The checked prologue, then a stub: the progress is the float at GS+0x48 (the game's divides the row by the rows).
    std::memcpy(image+kProgress,kProgressSig,sizeof(kProgressSig));
    const unsigned char tail[]={0xF3,0x0F,0x10,0x43,0x48,0x48,0x8B,0x5C,0x24,0x40,0x48,0x83,0xC4,0x30,0x5F,0xC3};
    std::memcpy(image+kProgress+sizeof(kProgressSig),tail,sizeof(tail));
    Check(InstallSupportFirepower() && SupportFirepowerReady(),"installed on the checked prologue");
    Check(!MarkSupportFirepower(a.body,supportlevel::Curve::eros),"no game status: no level, refused");
    Put<unsigned char*>(image,kGameStatus,gs);

    // Inferno, the last mission: level 99, the Eros's top (25).
    Status(4,1.0f);
    Check(supportlevel::SupportLevel(4,1.0f)==99,"inferno's last mission is level 99");
    Check(MarkSupportFirepower(a.body,supportlevel::Curve::eros),"marked");
    // Its level is the mark's: the game status read after it does not move it.
    Status(0,0.0f);
    Check(ApplySupportFirepower(a.body),"written");
    const float top=supportlevel::LevelDamage(supportlevel::Curve::eros,99);
    Check(Near(top,25.0f),"the Eros's damage at 99");
    Check(Near(At<float>(a.body,0x678),top),"+0x678 = the level's damage x +0x67C");
    for(unsigned i=0;i<3;++i)Check(Near(At<float>(a.holders+i*0x48,0x40),top),"each holder's +0x40 the same");
    Check(At<float>(a.holders+3*0x48,0x40)==7.0f,"past the holder count untouched");
    Check(ApplySupportFirepower(a.body) && Near(At<float>(a.body,0x678),top),"a later setup writes the same");

    // Easy, the first mission, a base of 2, the Blacker's: level 2 (held at the curve's first point, 0.7) x 2.
    Status(0,0.0f);
    Vehicle b(1,2.0f);
    Check(MarkSupportFirepower(b.body,supportlevel::Curve::blacker) && ApplySupportFirepower(b.body),"a tank marked, written");
    Check(Near(At<float>(b.body,0x678),1.4f) && Near(At<float>(b.holders,0x40),1.4f),"the base multiplies the level's damage");

    // An unmarked vehicle (a mission's, an enemy's): its setup's own, untouched.
    Vehicle c(2);
    Check(ApplySupportFirepower(c.body) && At<float>(c.body,0x678)==7.0f && At<float>(c.holders,0x40)==7.0f,
          "an unmarked vehicle keeps the game's");

    // No level (a difficulty out of range): refused at the mark, before any setup.
    Status(7,0.5f);
    Check(!MarkSupportFirepower(c.body,supportlevel::Curve::grape),"a difficulty with no level refused");
    Check(ApplySupportFirepower(c.body) && At<float>(c.body,0x678)==7.0f,"so it stays unmarked");
    Status(2,0.5f);

    // The same address again: the new mark replaces the old (a new object there, or the same marked again).
    Check(MarkSupportFirepower(b.body,supportlevel::Curve::grape) && ApplySupportFirepower(b.body),"remarked");
    Check(Near(At<float>(b.body,0x678),2.0f*supportlevel::LevelDamage(supportlevel::Curve::grape,supportlevel::SupportLevel(2,0.5f))),
          "the new mark's curve and level");

    // Unreadable holders: not written, the setup reported failed.
    Put<std::size_t>(b.body,0x648,100000);
    Check(!ApplySupportFirepower(b.body),"too many holders: refused");
    Put<std::size_t>(b.body,0x648,1);

    // The table: a dead object's mark is reused; a full table of live ones refuses; a mission start empties it.
    ResetSupportFirepower();
    Check(ApplySupportFirepower(a.body) && Near(At<float>(a.body,0x678),top),"after a reset nothing is marked (written as left)");
    std::vector<Vehicle>* many=new std::vector<Vehicle>();
    many->reserve(kMarks+1);
    for(unsigned i=0;i<kMarks+1;++i)many->emplace_back(1);
    for(unsigned i=0;i<kMarks;++i)if(!MarkSupportFirepower((*many)[i].body,supportlevel::Curve::eros))Check(false,"room for every mark");
    Check(!MarkSupportFirepower((*many)[kMarks].body,supportlevel::Curve::eros),"a full table refuses");
    Put<const void*>((*many)[0].body,edf::kSelfCtrl,deadCtrl);   // its object gone (its control block's count 0)
    Check(MarkSupportFirepower((*many)[kMarks].body,supportlevel::Curve::eros),"a dead object's mark is reused");
    Check(ApplySupportFirepower((*many)[0].body) && At<float>((*many)[0].body,0x678)==7.0f,"the dead one's mark is gone");
    ResetSupportFirepower();
    Check(MarkSupportFirepower((*many)[1].body,supportlevel::Curve::eros),"a mission start empties the table");
    delete many;
    ResetSupportFirepower();
    std::printf("support_firepower: %d checks passed\n",checks);
    return 0;
}
