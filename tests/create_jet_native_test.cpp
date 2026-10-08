// Reuse the audited native SceneObject prefix fixture, but call production CreateJet itself.
// Only the private EDF mapping's CreateObject service entry is redirected to that prefix boundary.
// No game process, DllMain, installation writes, resource loader or completed aircraft is fabricated.
#define wmain UnusedSoldierConstructorExample
#pragma warning(push)
// The included fixture's entry point uses wmain's implicit return; this renamed, unused
// example is ordinary C++ here. Keep that warning exemption local to the shared fixture.
#pragma warning(disable:4715)
#include "support_soldier_create_native_test.cpp"
#pragma warning(pop)
#undef wmain
#pragma warning(push)
// World-service sentinels below never return, so optimized, unexercised flight paths are unreachable.
#pragma warning(disable:4702)
#include "../src/jet_spawn.cpp"
#pragma warning(pop)

// Whole-translation-unit linkage requires these world services. None belongs to CreateJet's
// boundary: fail immediately if the test accidentally enters preload, crew, flight or terrain AI.
namespace { [[noreturn]] void UnexpectedWorldCall() {std::fputs("FAIL unexpected aircraft world service\n",stderr);std::abort();} }
namespace crew {
ULONGLONG GameMs() noexcept {UnexpectedWorldCall();}
float BodyMark(const void*) noexcept {UnexpectedWorldCall();}
bool IsHelicopter(const void*) noexcept {UnexpectedWorldCall();}
float MapRay(const float*,const float*,float*) noexcept {UnexpectedWorldCall();}
void HeliCalled(unsigned char*,bool,const float*,DWORD) noexcept {UnexpectedWorldCall();}
bool HeliCommand(const void*,const Command&) noexcept {UnexpectedWorldCall();}
bool ProteusReady() noexcept {UnexpectedWorldCall();}
bool NpcDriver(const unsigned char*) noexcept {UnexpectedWorldCall();}
bool SeatNpcRider(unsigned char*,bool) noexcept {UnexpectedWorldCall();}
void NoteLocalCopy(const void*,const void*) noexcept {UnexpectedWorldCall();}
bool FixBodyPart506(unsigned char*,const char*) noexcept {UnexpectedWorldCall();}
int FaultLog(const char*,const EXCEPTION_POINTERS*) noexcept {UnexpectedWorldCall();}
bool PrepareNpcVehicle(unsigned char*,bool) noexcept {UnexpectedWorldCall();}
namespace jet {
Jet jets[kMaxJets]{};
bool Alive(const ObjRef&) noexcept {UnexpectedWorldCall();}
Jet* FindJet(const unsigned char*) noexcept {UnexpectedWorldCall();}
Jet* NewEntry(unsigned char*,ULONGLONG) noexcept {UnexpectedWorldCall();}
bool SlotFree() noexcept {UnexpectedWorldCall();}
void JoinFlight(Jet&,unsigned) noexcept {UnexpectedWorldCall();}
bool IsJetVehicle(const unsigned char*,Role*,Role*) noexcept {UnexpectedWorldCall();}
void DollMake(int,const unsigned char*,DWORD) noexcept {UnexpectedWorldCall();}
bool PreloadDolls(void*,bool) noexcept {UnexpectedWorldCall();}
void PreloadShells(void*,bool,bool) noexcept {UnexpectedWorldCall();}
void ResetShells() noexcept {UnexpectedWorldCall();}
void Publish(bool) noexcept {UnexpectedWorldCall();}
}
}

namespace {
crew::jet::Body expectedBody=crew::jet::Body::strike;
unsigned char* __fastcall JetCreateBoundary(void* mgr,const float* matrix,const wchar_t* path,crew::jet::InitParam* init) {
    ++calls;seenMatrix=matrix;
    Check(mgr==manager,"production CreateJet forwards its current native object manager");
    Check(init->vtable==image+crew::jet::kInitParamVtable,"production CreateJet preserves InitParamBase");
    Check(wcscmp(path,crew::jet::Row(expectedBody).sgo)==0,"production CreateJet preserves selected canonical aircraft resource");
    static_assert(sizeof(crew::jet::InitParam)==sizeof(crew::support_native::InitParam));
    boundary(object,matrix,reinterpret_cast<crew::support_native::InitParam*>(init),mgr);
    // Stop after native parameter wiring and SIMD matrix copying. The rest needs a real world.
    return nullptr;
}
}

int wmain(int argc,wchar_t** argv) {
    using namespace crew;
    if(argc!=2 || GetFileAttributesW(argv[1])==INVALID_FILE_ATTRIBUTES){std::puts("SKIP: pass supported EDF.dll");return 77;}
    image=edf::IdentifyImage(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Check(image!=nullptr,"supported private EDF.dll mapping");BuildBoundary();
    Check(Matches(jet::kCreateObject,jet::kCreateObjectSig,sizeof(jet::kCreateObjectSig)),"actual CreateObject service entry identified");
    unsigned char jump[]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    PutAddress(jump,2,reinterpret_cast<const void*>(&JetCreateBoundary));Patch(jet::kCreateObject,jump,sizeof(jump));
    DWORD old=0;Check(VirtualProtect(image+jet::kObjectMgr,8,PAGE_READWRITE,&old)!=0,"private object manager slot writable");
    Put<void*>(image,jet::kObjectMgr,manager);
    void* handler=AddVectoredExceptionHandler(1,&AlignmentFault);Check(handler!=nullptr,"native alignment fault recorder installed");
    alignas(16) support_net::Plan plan{};
    for(unsigned i=0;i<support_net::kMaxUnits;++i) {
        float* matrix=plan.units[i].matrix;
        matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;matrix[12]=static_cast<float>(i*3);matrix[13]=2;matrix[14]=-5;
        Check((reinterpret_cast<std::uintptr_t>(matrix)&15u)!=0,"real support-plan matrix is not SIMD aligned");
        support_native::InitParam init{image+support_native::kInitVtable,{}};const int before=faults;
        boundary(object,matrix,&init,manager);
        Check(faults==before+1 && faultRva==0x1189CB0,"negative control faults at the original native MOVAPS source load");
    }
    const int legacyFaults=faults;
    const jet::Body bodies[]={jet::Body::strike,jet::Body::fighter,jet::Body::heli410,jet::Body::heli506};
    for(const auto body:bodies) {
        expectedBody=body;const int index=static_cast<int>(body);
        jet::broken[index]=false;jet::preloaded[index]=true;
        for(unsigned i=0;i<support_net::kMaxUnits;++i) {
            jet::InitParam init{image+jet::kInitParamVtable,{}};const int before=calls;
            Check(jet::CreateJet(body,plan.units[i].matrix,&init)==nullptr && calls==before+1,
                "actual production CreateJet reaches the native boundary and preserves deliberate fixture null result");
            Check((reinterpret_cast<std::uintptr_t>(seenMatrix)&15u)==0 && faults==legacyFaults,
                "production CreateJet passes aligned matrix for every actual Unit position");
            Check(std::memcmp(object+0x60,plan.units[i].matrix,64)==0,"original constructor copies all requested transform bytes exactly");
            Check(!jet::broken[index] && jet::preloaded[index],"valid native matrix copying does not quarantine the aircraft body");
        }
    }
    Check(RemoveVectoredExceptionHandler(handler)!=0,"native alignment recorder removed");
    std::printf("create_jet_native_test: %d checks passed; %d legacy MOVAPS faults, zero through production CreateJet\n",checks,legacyFaults);
    std::puts("Original native parameter wiring and constructor SIMD prefix only; resource/world constructor tail intentionally excluded.");
    return 0;
}
