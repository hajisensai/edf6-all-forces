// Real CreateObject InitParam wiring and SceneObject constructor prefix. Optional EDF.dll.
// No DllMain, game process, installed file writes, resource loader or Havok world.
#include "../src/support_soldier.cpp"
#include "../src/support_net.h"
#include "edf/host.h"
#include <cstdio>
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
bool InSession() noexcept { return false; }
bool OnlineHostOnly() noexcept { return true; }
bool IsSoldierClass(const void*) noexcept { return false; } // boundary fixture never supplies a complete world soldier
void Log(const char*,...) noexcept {}
}
namespace {
using namespace crew;
int checks=0,calls=0,faults=0;
std::uintptr_t faultRva=0;
const float* seenMatrix=nullptr;
bool leaderPath=false;
alignas(16) unsigned char object[0x200]{},manager[0xD00]{};
using ConstructorBoundary=void*(__fastcall*)(void*,const float*,support_native::InitParam*,void*);
ConstructorBoundary boundary=nullptr;
void Check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
LONG CALLBACK AlignmentFault(EXCEPTION_POINTERS* ex) {
    const auto at=reinterpret_cast<std::uintptr_t>(ex->ExceptionRecord->ExceptionAddress);
    const auto base=reinterpret_cast<std::uintptr_t>(image);
    if(ex->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION || at!=base+0x1189CB0)return EXCEPTION_CONTINUE_SEARCH;
    ++faults;faultRva=at-base;
    // Negative control only: resume at the original native constructor epilogue. No C++ exception
    // unwinding and no invented/mocked alignment failure; the CPU raised the original MOVAPS fault.
    ex->ContextRecord->Rip=base+0x1189EB9;
    return EXCEPTION_CONTINUE_EXECUTION;
}
void Patch(unsigned rva,const unsigned char* bytes,std::size_t n) {
    DWORD old=0;Check(VirtualProtect(image+rva,n,PAGE_EXECUTE_READWRITE,&old)!=0,"private native code writable");
    std::memcpy(image+rva,bytes,n);DWORD restored=0;Check(VirtualProtect(image+rva,n,old,&restored)!=0,"private protection restored");
    FlushInstructionCache(GetCurrentProcess(),image+rva,n);
}
void PutAddress(unsigned char* code,std::size_t offset,const void* address) {std::memcpy(code+offset,&address,8);}
void BuildBoundary() {
    constexpr unsigned char matrixBlock[]={0x48,0x8B,0x45,0x08,0x0F,0x28,0x00,0x0F,0x29,0x43,0x60,
        0x0F,0x28,0x48,0x10,0x0F,0x29,0x4B,0x70,0x0F,0x28,0x40,0x20,0x0F,0x29,0x83,0x80,0,0,0,
        0x0F,0x28,0x48,0x30,0x0F,0x29,0x8B,0x90,0,0,0};
    constexpr unsigned char wireBlock[]={0x49,0x89,0x77,0x08,0x48,0x8D,0x54,0x24,0x20,0x49,0x89,0x57,0x10,
        0x49,0x89,0x47,0x18,0x48,0x8D,0x44,0x24,0x38,0x49,0x89,0x47,0x20,0xFF,0x87,0xB0,0x0C,0,0};
    Check(Matches(0x1189CAC,matrixBlock,sizeof(matrixBlock)),"unaltered native aligned source/destination loads");
    Check(Matches(0x1194854,wireBlock,sizeof(wireBlock)),"actual CreateObject InitParam wiring");
    // Keep the original constructor prologue, initialization, 52710 call and all four MOVAPS rows.
    // After those rows only, skip resource/ownership/world construction to its original epilogue.
    unsigned char jump[5]={0xE9};const std::int32_t relative=0x1189EB9-(0x1189CD5+5);
    std::memcpy(jump+1,&relative,4);Patch(0x1189CD5,jump,sizeof(jump));
    // Adapter: preserve nonvolatile registers, keep Windows shadow slots; put the native operands
    // in RSI=matrix, R15=InitParam, RDI=manager, then run the exact CreateObject wiring block.
    unsigned char code[160]{};std::size_t n=0;
    const unsigned char before[]={0x53,0x56,0x57,0x41,0x57,0x48,0x83,0xEC,0x58,
        0x48,0x89,0xCB,0x48,0x89,0xD6,0x4D,0x89,0xC7,0x4C,0x89,0xCF,0x31,0xC0};
    std::memcpy(code+n,before,sizeof before);n+=sizeof before;
    std::memcpy(code+n,wireBlock,sizeof wireBlock);n+=sizeof wireBlock;
    const unsigned char call[]={0x48,0x89,0xD9,0x4C,0x89,0xFA,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0};
    std::memcpy(code+n,call,sizeof call);PutAddress(code,n+8,image+0x1189C00);n+=sizeof call;
    const unsigned char after[]={0xFF,0x8F,0xB0,0x0C,0,0,0x48,0x83,0xC4,0x58,0x41,0x5F,0x5F,0x5E,0x5B,0xC3};
    std::memcpy(code+n,after,sizeof after);n+=sizeof after;
    void* page=VirtualAlloc(nullptr,n,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);Check(page!=nullptr,"private ABI adapter allocated");
    std::memcpy(page,code,n);FlushInstructionCache(GetCurrentProcess(),page,n);boundary=reinterpret_cast<ConstructorBoundary>(page);
}
unsigned char* __fastcall NativeCreateBoundary(void* mgr,const float* matrix,const wchar_t* path,support_native::InitParam* init) {
    ++calls;seenMatrix=matrix;
    Check(mgr==manager,"native CreateObject manager retained");
    Check(init->vtable==image+support_native::kInitVtable,"native InitParamBase vtable retained");
    leaderPath=wcscmp(path,support_native::kBodies[0][1])==0;
    Check(leaderPath || wcscmp(path,support_native::kBodies[0][0])==0,"canonical leader/member resource retained");
    boundary(object,matrix,init,mgr);
    // Deliberately stop after the real native boundary; this fixture does not fabricate a completed Soldier.
    return nullptr;
}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2 || GetFileAttributesW(argv[1])==INVALID_FILE_ATTRIBUTES){std::puts("SKIP: pass supported EDF.dll");return 77;}
    image=edf::IdentifyImage(LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Check(image!=nullptr,"supported private EDF.dll mapping");BuildBoundary();
    void* handler=AddVectoredExceptionHandler(1,&AlignmentFault);Check(handler!=nullptr,"expected native fault recorder installed");
    alignas(16) support_net::Plan plan{};
    static_assert(offsetof(support_net::Unit,matrix)==8);
    for(unsigned i=0;i<support_net::kMaxUnits;++i) {
        float* matrix=plan.units[i].matrix;
        matrix[0]=matrix[5]=matrix[10]=matrix[15]=1;matrix[12]=float(i*3);matrix[13]=2;matrix[14]=-5;
        const unsigned residue=static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(matrix)&15u);
        Check(residue==4 || residue==12,"real Plan matrix layout is not SIMD aligned");
        support_native::InitParam init{image+support_native::kInitVtable,{}};
        const int previousFaults=faults;
        boundary(object,matrix,&init,manager);
        Check(faults==previousFaults+1 && faultRva==0x1189CB0,"legacy raw Unit matrix faults at actual SceneObject MOVAPS");
    }
    const int legacyFaults=faults;
    support_native::profile=true;support_native::faulted=false;support_native::preloaded=true;
    support_native::missionManager=manager;support_native::create=&NativeCreateBoundary;
    DWORD old=0;Check(VirtualProtect(image+support_native::kObjectManager,8,PAGE_READWRITE,&old)!=0,"private global manager writable");
    Put<void*>(image,support_native::kObjectManager,manager);
    for(unsigned i=0;i<support_net::kMaxUnits;++i) {
        ObjRef output;
        const int prior=calls;
        const bool leader=i%4==0;
        Check(!ApplySupportSoldierSpawn(plan.units[i].matrix,leader,nullptr,&output) && calls==prior+1,
              "production Spawn reaches real constructor boundary and stops at fixture's deliberate null result");
        Check((reinterpret_cast<std::uintptr_t>(seenMatrix)&15u)==0 && faults==legacyFaults,"production native matrix copy is aligned and never faults");
        Check(std::memcmp(object+0x60,plan.units[i].matrix,64)==0,"native constructor copied all sixteen requested matrix values exactly");
        Check(leaderPath==leader && !support_native::faulted,"leader/member both preserve path and do not disable spawning");
    }
    Check(RemoveVectoredExceptionHandler(handler)!=0,"native fault recorder removed");
    std::printf("support_soldier_create_native_test: %d checks passed; %d original MOVAPS faults, zero with production aligned copy\n",checks,legacyFaults);
    std::puts("Exact native CreateObject parameter wiring and SceneObject constructor prefix; resource/world constructor tail intentionally not executed.");
}
