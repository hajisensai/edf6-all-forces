// Production shape selection at the native constructor's pre-mission mark=0.
#include "../src/body506.cpp"
#include <cstdio>
#include <cstdlib>
namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
void Unused() noexcept { void(*volatile fn)()=std::abort; fn(); }
const Config& Cfg() noexcept { Unused(); static Config c; return c; }
bool JetBodyStep(unsigned char*,float*,float*) noexcept { Unused(); return false; }
bool JetHeldStep(unsigned char*,float*,float*) noexcept { Unused(); return false; }
bool SubBodyStep(unsigned char*,float*,float*) noexcept { Unused(); return false; }
bool PlayerJetBodyStep(unsigned char*,float*,float*) noexcept { Unused(); return false; }
bool SazabiBodyStep(unsigned char*,float*,float*) noexcept { Unused(); return false; }
bool DreadnoughtBodyStep(unsigned char*,float*,float*) noexcept { Unused(); return false; }
float MapRay(const float*,const float*,float*) noexcept { Unused(); return false; }
bool SubMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { Unused(); return false; }
bool PlayerJetMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { Unused(); return false; }
bool PrimerMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { Unused(); return false; }
bool SazabiMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { Unused(); return false; }
bool DreadnoughtMessage(unsigned char*,std::uint32_t,void*,MessageRestore*) noexcept { Unused(); return false; }
namespace {
// Two ragdoll parts: a stand-in (a convex, pylib/aircraft_collision.py) and then the airframe's compound.
unsigned char standIn[0x80]{},shapeFixture[0x80]{};
alignas(16) unsigned char parts[2*0xC0]{};
int shapeCalls=0;
void* ShapeFixture(void* proxy) { ++shapeCalls;return proxy==parts ? standIn : shapeFixture; }
void Jump(std::size_t at,std::uintptr_t fn) {
    const unsigned char prefix[]={0xFF,0x25,0,0,0,0};
    std::memcpy(image+at,prefix,6);std::memcpy(image+at+6,&fn,8);
}
void Check(bool yes,const char* why) { if(!yes){std::printf("FAIL %s\n",why);std::exit(1);} }
}
}
int main() {
    using namespace crew;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private fixture image");
    Jump(kGetShape,reinterpret_cast<std::uintptr_t>(&ShapeFixture));
    alignas(16) unsigned char vehicle[0x1800]{};
    Put<void*>(vehicle,0,image+kHelicopterBase);
    Put<std::size_t>(vehicle,0x13A8,2);
    Put<void*>(vehicle,0x1398,parts);
    Put<void*>(parts,0x50,parts);Put<void*>(parts+0xC0,0x50,parts+0xC0);
    Put<void*>(standIn,0,image+kCompoundVtable+0x100);   // not a compound
    Put<void*>(shapeFixture,0,image+kCompoundVtable);
    Put<std::uint64_t>(shapeFixture,0x28,kAirframeMagic);
    Check(At<float>(vehicle,kSpeedGain)==0.0f,"mission mark has not arrived");
    // range 2026-10-21: a stand-in part ahead of the airframe (the Dreadnought had flown as a 1 cm box)
    Check(AirframeShape(vehicle)==shapeFixture,"tagged aircraft uses its compound part, past a stand-in, before mission_setup");
    Put<float>(vehicle,kSpeedGain,7005.0f);
    Check(AirframeShape(vehicle)==shapeFixture,"same compound after mission_setup");
    Put<float>(vehicle,kSpeedGain,7401.0f);
    Put<std::uint64_t>(shapeFixture,0x28,0);
    Check(AirframeShape(vehicle)==nullptr,"untagged Sazabi retains original shape");
    Put<float>(vehicle,kSpeedGain,1.0f);
    Check(AirframeShape(vehicle)==nullptr,"untagged stock helicopter retains original shape");
    Put<std::uint64_t>(shapeFixture,0x28,kAirframeMagic+1);
    Check(AirframeShape(vehicle)==nullptr,"foreign tag fails closed");
    Put<std::uint64_t>(shapeFixture,0x28,kAirframeMagic);
    Put<void*>(shapeFixture,0,nullptr);
    Check(AirframeShape(vehicle)==nullptr,"no compound part fails closed");
    Put<void*>(shapeFixture,0,image+kCompoundVtable);
    Put<void*>(vehicle,0,nullptr);shapeCalls=0;
    Check(AirframeShape(vehicle)==nullptr && !shapeCalls,"non-helicopter never reads parts");
    Put<void*>(vehicle,0,image+kHelicopterBase);
    Put<std::size_t>(vehicle,0x13A8,65);
    Check(AirframeShape(vehicle)==nullptr,"implausible part count fails closed");
    Put<std::size_t>(vehicle,0x13A8,1);
    Check(AirframeShape(vehicle)==nullptr,"only the stand-in: no airframe");
    VirtualFree(image,0,MEM_RELEASE);
    std::puts("airframe constructor: 10 checks passed");
}
