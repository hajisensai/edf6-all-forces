// The bullets' candidate hook (src/jet_hooks.cpp AddBodyHook / InstallBulletPass) against a private executable
// buffer standing in for EDF.dll: no game is loaded. It installs on its own signatures (no 506 physics, no heli
// profile, no jets).
#include "../src/jet_hooks.cpp"
#include <cstdio>
#include <cstdlib>

namespace {
int failures=0;
void Check(bool pass,const char* what) {
    std::printf("%s %s\n",pass ? "PASS" : "FAIL",what);
    if(!pass)++failures;
}
void __fastcall StockAddBody(void*,std::uint32_t) {}
}  // namespace

// What the hook's other parts are, here: the shield and the sidecar never let a round through.
namespace crew {
unsigned char* image=nullptr;
const Config& Cfg() noexcept { static Config c{};return c; }
void Log(const char*,...) noexcept {}
int FaultLog(const char*,const EXCEPTION_POINTERS*) noexcept { return EXCEPTION_EXECUTE_HANDLER; }
ULONGLONG GameMs() noexcept { return 1000; }
ULONGLONG GameFrame() noexcept { return 60; }
bool ShieldLetsThrough(void*,std::uint32_t) noexcept { return false; }
bool SidecarBulletPass(const void*,const void*,const void*) noexcept { return false; }
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
    VirtualFree(image,0,MEM_RELEASE);
    std::printf("bullet_pass_test: %d failures\n",failures);
    return failures ? 1 : 0;
}
