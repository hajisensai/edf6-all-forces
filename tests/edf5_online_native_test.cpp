// The EDF5 scripts' player natives online (src/edf5online.cpp) on a private EDF.dll mapping: native 0x10's call
// points at AngelScript's PreloadPlayerResource, CreatePlayer's empty online branch enters the creation loop with the
// session's count, at most the loop's four; another build is left untouched. Never starts the game.
#include "../src/crew.h"
#include "../src/memory.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../src/edf5online.cpp"

namespace crew {
unsigned char* image=nullptr;
}
namespace {
std::vector<std::string> lines;
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}
}
bool Logged(const char* part) {
    for(const auto& line:lines)if(line.find(part)!=std::string::npos)return true;
    return false;
}
// The rel32 call at `site` and where it lands, through a near thunk (mov rax,imm64; jmp rax) when there is one.
unsigned char* CallLanding(unsigned site) {
    unsigned char* p=crew::image+site;
    std::int32_t rel;std::memcpy(&rel,p+1,4);
    unsigned char* to=p+5+rel;
    if(to[0]==0x48 && to[1]==0xB8 && to[10]==0xFF && to[11]==0xE0) {std::uintptr_t a;std::memcpy(&a,to+2,8);return reinterpret_cast<unsigned char*>(a);}
    return to;
}
}  // namespace
namespace crew {
void Log(const char* format,...) noexcept {
    char text[600];va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
    lines.emplace_back(text);
}
}

int main(int argc,char** argv) {
    using namespace crew;
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: pass the supported EDF.dll path (private mapping, no DllMain)");return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Check(image!=nullptr,"private DLL mapped without its entrypoint");
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    Check(nt->FileHeader.TimeDateStamp==0x678CCB46 && nt->OptionalHeader.SizeOfImage==0x22CE000,"supported game profile");
    const std::string mode=argc>2 ? argv[2] : "";

    if(mode=="foreign") {   // another build: one byte of the creation's online branch differs, nothing is written
        unsigned char before[sizeof(kPreloadCallCode)];std::memcpy(before,image+kPreloadCallAt,sizeof(before));
        DWORD old=0;VirtualProtect(image+kCreateAt+0x30,1,PAGE_EXECUTE_READWRITE,&old);
        image[kCreateAt+0x30]^=0xFF;
        VirtualProtect(image+kCreateAt+0x30,1,old,&old);
        Check(!InstallEdf5Online() && !Edf5OnlineReady(),"another build refused");
        Check(!std::memcmp(before,image+kPreloadCallAt,sizeof(before)),"native 0x10's call untouched");
        Check(image[kCreateOnline]==kCreateCode[kCreateOnline-kCreateAt],"CreatePlayer untouched");
        Check(Logged("HOOK edf5 online=0"),"the refusal logged");
        std::printf("edf5_online_native_test: %d checks passed (foreign)\n",checks);
        return 0;
    }

    // The stock natives: native 0x10 calls the BVM preload, whose online half is an empty list; CreatePlayer's too.
    Check(CallLanding(kPreloadCall)==image+kBvmPreload,"stock: native 0x10 calls 0x225E30");
    Check(image[0x225EA9]==0xE8 && CallLanding(0x225EA9)==image+0x20C5B0 && image[0x225EA3]==0x33 && image[0x225EA0]==0x45,
          "stock: the BVM preload's online list is assign(nullptr, nullptr)");
    Check(image[0x22B399]==0xE8 && CallLanding(0x22B399)==image+0x20C5B0 && image[0x22B39F]==0xE9,
          "stock: CreatePlayer's online branch assigns an empty list and leaves");

    Check(InstallEdf5Online() && Edf5OnlineReady(),"installed on the supported build");
    Check(InstallEdf5Online(),"a second install is the first's answer");
    Check(Logged("HOOK edf5 online=1"),"installed, logged");
    Check(CallLanding(kPreloadCall)==image+kAsPreload,"native 0x10 calls AngelScript's PreloadPlayerResource (0x1B8CC0)");
    Check(!std::memcmp(image+kPreloadCallAt,kPreloadCallCode,4),"its argument setup kept");

    // 0x22B36C: mov rax,&Edf5BvmOnlinePlayers; call rax; mov r13d,eax; jmp short 0x22B3B0.
    const unsigned char* p=image+kCreateOnline;
    std::uintptr_t fn=0;std::memcpy(&fn,p+2,8);
    Check(p[0]==0x48 && p[1]==0xB8 && fn==reinterpret_cast<std::uintptr_t>(&Edf5BvmOnlinePlayers),"the count from the plugin");
    Check(p[10]==0xFF && p[11]==0xD0,"called");
    Check(p[12]==0x44 && p[13]==0x8B && p[14]==0xE8,"into the loop's count r13d");
    Check(p[15]==0xEB && kCreateOnline+17+static_cast<std::int8_t>(p[16])==kCreateLoop,"then the creation loop at 0x22B3B0");
    Check(!std::memcmp(image+kCreateAt,kCreateCode,kCreateOnline-kCreateAt),"the online test before it kept");
    Check(image[kCreateLoop]==0x66 && image[kCreateLoop+1]==0x0F,"the loop itself untouched (movdqa xmm0)");

    // The count: the session's (GS+0x14FF8), never the local players' (GS+0x14FF4), at most four.
    alignas(16) static unsigned char status[0x15000]{};
    unsigned char* const statusPtr=status;
    DWORD old=0;VirtualProtect(image+kGameStatus,8,PAGE_READWRITE,&old);
    std::memcpy(image+kGameStatus,&statusPtr,8);
    Put<std::uint32_t>(status,kLocalPlayers,1);
    Put<std::uint32_t>(status,kSessionPlayers,2);
    Check(Edf5BvmOnlinePlayers()==2,"two in the session: both created (the 2026-10-10 room)");
    Put<std::uint32_t>(status,kSessionPlayers,4);
    Check(Edf5BvmOnlinePlayers()==4 && !Logged("EDF5 online:"),"four: all of them, nothing logged");
    Put<std::uint32_t>(status,kSessionPlayers,6);
    Check(Edf5BvmOnlinePlayers()==4,"six: the loop's four, never past its slots");
    Check(Logged("EDF5 online: 6 players in the session, an EDF5 mission creates 4"),"the cut logged");
    lines.clear();
    Check(Edf5BvmOnlinePlayers()==4 && !Logged("EDF5 online:"),"once, not every call");
    Put<std::uint32_t>(status,kSessionPlayers,3);
    Check(Edf5BvmOnlinePlayers()==3,"back to three");
    Put<std::uint32_t>(status,kSessionPlayers,7);
    Check(Edf5BvmOnlinePlayers()==4 && Logged("EDF5 online: 7 players"),"logged again after a mission within the slots");
    const unsigned char* const none=nullptr;
    std::memcpy(image+kGameStatus,&none,8);
    Check(Edf5BvmOnlinePlayers()==0,"no game status: nobody");
    VirtualProtect(image+kGameStatus,8,old,&old);
    std::printf("edf5_online_native_test: %d checks passed\n",checks);
    return 0;
}
