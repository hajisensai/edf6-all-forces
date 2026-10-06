// The bullets' candidate hook (src/jet_hooks.cpp AddBodyHook / InstallBulletPass) against a private executable
// buffer standing in for EDF.dll: no game is loaded. It installs on its own signatures (no 506 physics, no heli
// profile, no jets), and with no jet flown and no sidecar passenger it hands every round to the stock function
// without looking up the round's owner or target.
#include "../src/jet_hooks.cpp"
#include <cstdio>
#include <cstdlib>

namespace {
int failures=0,lookups=0,sidecarAsks=0,stockCalls=0,passengersNow=0;
bool sidecarPasses=false;
unsigned char owner[0x100]{},target[0x100]{},core[0xA00]{},collector[0x100]{};
void Check(bool pass,const char* what) {
    std::printf("%s %s\n",pass ? "PASS" : "FAIL",what);
    if(!pass)++failures;
}
void __fastcall StockAddBody(void*,std::uint32_t) { ++stockCalls; }
const void* __fastcall BodyObjectRec(std::uint32_t) { ++lookups;return target; }
void Jump(unsigned rva,const void* to) {
    unsigned char* p=crew::image+rva;
    p[0]=0x48;p[1]=0xB8;std::memcpy(p+2,&to,8);p[10]=0xFF;p[11]=0xE0;
}
}  // namespace

// What the hook's other parts are, here: the shield never lets a round through; the sidecar's passengers and their
// pass are the test's.
namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept { static Config c{};return c; }
void Log(const char*,...) noexcept {}
int FaultLog(const char*,const EXCEPTION_POINTERS*) noexcept { return EXCEPTION_EXECUTE_HANDLER; }
ULONGLONG GameMs() noexcept { return 1000; }
ULONGLONG GameFrame() noexcept { return 60; }
bool ShieldLetsThrough(void*,std::uint32_t) noexcept { return false; }
int SidecarPassengers() noexcept { return passengersNow; }
bool SidecarBulletPass(const void* o,const void* t,const void*) noexcept { ++sidecarAsks;return sidecarPasses && o==owner && t==target; }
// The jets' install and their physics step are not this test's: they stop it if reached.
void MissingDependency() noexcept { void(*volatile stop)()=std::abort;stop(); }
bool Body506Ok() noexcept { MissingDependency();return false; }
bool InstallJetProps() noexcept { MissingDependency();return false; }
bool InstallBoosters() noexcept { MissingDependency();return false; }
bool InstallShields() noexcept { MissingDependency();return false; }
float ShieldBlock(const unsigned char*,float*) noexcept { MissingDependency();return 0.0f; }
bool JetMotionProps(void*) noexcept { MissingDependency();return false; }
namespace jet {
Jet jets[kMaxJets]{};
Jet* FindJet(const unsigned char*) noexcept { MissingDependency();return nullptr; }
bool InstallSpawn() noexcept { MissingDependency();return false; }
bool InstallBay(bool) noexcept { MissingDependency();return false; }
bool InstallDolls() noexcept { MissingDependency();return false; }
bool InstallFarRender() noexcept { MissingDependency();return false; }
void PrimerCorpseStep(unsigned char*) noexcept { MissingDependency(); }
}  // namespace jet
}  // namespace crew

int main() {
    using namespace crew;
    using namespace crew::jet;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x17A0000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    if(!image)return 2;
    std::memcpy(image+kAddBody,kAddBodySig,sizeof(kAddBodySig));
    std::memcpy(image+kBodyObject,kBodyObjectSig,sizeof(kBodyObjectSig));
    std::memcpy(image+kBodyObject+11,kBodyObjectSig2,sizeof(kBodyObjectSig2));
    auto slot=reinterpret_cast<void**>(image+kAddBodySlot);
    *slot=reinterpret_cast<void*>(&StockAddBody);
    // Only the hook's own signatures: no 506 physics hook, no heli profile, InstallJets never runs.
    Check(InstallBulletPass() && PassThrough() && !HooksOk() && *slot==reinterpret_cast<void*>(&AddBodyHook),
          "the bullets' hook installs on its own signatures, without the jets");
    Jump(kBodyObject,reinterpret_cast<const void*>(&BodyObjectRec));
    Put<void*>(collector,kCollectorCore,core);Put<void*>(core,kBulletOwner,owner);
    const auto addBody=reinterpret_cast<AddBodyFn>(*slot);

    addBody(collector,7);
    Check(stockCalls==1 && lookups==0 && sidecarAsks==0,"nothing to pass: the stock path, no body lookup, no table read");
    passengersNow=1;sidecarPasses=true;
    addBody(collector,7);
    Check(stockCalls==1 && lookups==1 && sidecarAsks==1,"a riding passenger's round through their own bike is left out");
    sidecarPasses=false;
    addBody(collector,7);
    Check(stockCalls==2 && lookups==2,"a round the passenger may not pass goes to the stock function");
    passengersNow=0;
    addBody(collector,7);
    Check(stockCalls==3 && lookups==2,"the passenger gone: the gate shuts again");
    // A flown plugin jet opens it too (Publish keeps the count with the table).
    jets[0].ref=ObjRef{owner,nullptr};jets[0].seen=GameMs();jets[0].flight=1;
    jets[1].ref=ObjRef{target,nullptr};jets[1].seen=GameMs();jets[1].flight=1;
    Publish(true);
    addBody(collector,7);
    Check(stockCalls==3 && lookups==3,"a jet's round through its wingman is left out");
    jets[0]=Jet{};jets[1]=Jet{};Publish(true);
    addBody(collector,7);
    Check(stockCalls==4 && lookups==3,"no jet flown: the gate shuts");
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("bullet_pass_test: %d failures\n",failures);
    return failures ? 1 : 0;
}
