// The EDF5 scripts' player natives online (src/edf5online.cpp) on a private EDF.dll mapping: native 0x10's call
// points at AngelScript's PreloadPlayerResource, CreatePlayer's empty online branch enters the creation loop with the
// session's count, at most the loop's four, each player given AngelScript's pad and split (a session fixture); another
// build is left untouched. Never starts the game.
#include "../src/crew.h"
#include "../src/memory.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include "../src/edf5online.cpp"

namespace crew {
unsigned char* image=nullptr;
bool gateReady=true;
bool MissionParticipantGateReady() noexcept { return gateReady; }
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

// The session object's users (edf5online.cpp ReadList): an object whose vtable slot 1 fills a
// std::vector<std::shared_ptr<User>>, each user's +0x10 bit 1 the remote flag, each control block counting its
// dispose and destroy, the storage given back through freeFn.
namespace SessionFixture {
struct Ctrl { void* const* vtable; volatile long uses; volatile long weaks; int disposed; int destroyed; };
void __fastcall Dispose(void* c) { ++static_cast<Ctrl*>(c)->disposed; }
void __fastcall Destroy(void* c) { ++static_cast<Ctrl*>(c)->destroyed; }
void* const ctrlVtable[]={reinterpret_cast<void*>(&Dispose),reinterpret_cast<void*>(&Destroy)};
struct User { unsigned char bytes[0x20]; };
std::vector<bool> remote;
User users[8]{};
Ctrl ctrls[8]{};
crew::Shared* storage=nullptr;
std::size_t storageBytes=0;
bool freed=false;
crew::SharedVector* __fastcall List(void*,crew::SharedVector* out) {
    const std::size_t n=remote.size();
    storageBytes=(n ? n : 1)*sizeof(crew::Shared);
    storage=static_cast<crew::Shared*>(std::malloc(storageBytes));
    freed=false;
    for(std::size_t i=0;i<n;++i) {
        std::memset(&users[i],0,sizeof(User));
        users[i].bytes[0x10]=remote[i] ? 2 : 1;   // bit 1 remote; bit 0 set either way (not the flag)
        ctrls[i]=Ctrl{ctrlVtable,1,1,0,0};
        storage[i]={&users[i],&ctrls[i]};
    }
    *out={storage,storage+n,reinterpret_cast<crew::Shared*>(reinterpret_cast<unsigned char*>(storage)+storageBytes)};
    return out;
}
void* const sessionVtable[]={nullptr,reinterpret_cast<void*>(&List)};
struct Session { void* const* vtable; } session{sessionVtable};
alignas(16) unsigned char holder[0x100]{};   // the session base points 0x98 into it, the object at +0xD0
void __fastcall Free(void* block,std::size_t bytes) {
    if(block==storage && bytes==storageBytes){freed=true;std::free(block);storage=nullptr;}
}
void Users(std::initializer_list<bool> list) { remote.assign(list.begin(),list.end()); }
bool Released() {
    for(std::size_t i=0;i<remote.size();++i)
        if(ctrls[i].uses!=0 || ctrls[i].weaks!=0 || ctrls[i].disposed!=1 || ctrls[i].destroyed!=1)return false;
    return freed;
}
alignas(16) unsigned char players[2][0x200]{};
const void* Player(bool notLocal) { players[notLocal][0x128]=notLocal ? 1 : 0;return players[notLocal]; }
void Install(unsigned char* at) {
    void* const s=&session;std::memcpy(holder+0xD0,&s,8);
    unsigned char* const base=holder+0x98;std::memcpy(at,&base,8);
}
void Uninstall(unsigned char* at) { unsigned char* const none=nullptr;std::memcpy(at,&none,8); }
}  // namespace SessionFixture

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

    // The count: the session's (GS+0x14FF8), never the local players' (GS+0x14FF4), at most four; and for each index
    // the pad and split AngelScript's creation gives (0x1D9A60..0x1D9A7B), from the session's users.
    alignas(16) static unsigned char status[0x15000]{};
    unsigned char* const statusPtr=status;
    DWORD old=0;VirtualProtect(image+kGameStatus,8,PAGE_READWRITE,&old);
    std::memcpy(image+kGameStatus,&statusPtr,8);
    DWORD oldSession=0;VirtualProtect(image+kSessionBase,8,PAGE_READWRITE,&oldSession);
    SessionFixture::Install(image+kSessionBase);
    freeFn=&SessionFixture::Free;
    Put<std::uint32_t>(status,kLocalPlayers,1);

    // The 2026-10-10 room seen from the joiner: the host remote first, this machine second.
    SessionFixture::Users({true,false});
    Put<std::uint32_t>(status,kSessionPlayers,2);
    Check(Edf5BvmOnlinePlayers()==2,"two in the session: both created");
    Check(SessionFixture::Released(),"the session's user list let go: every shared_ptr and its storage");
    int pad=7,split=7;
    Check(Edf5BvmOnlinePlayerArgs(0,&pad,&split) && pad==-1,"the remote host: no pad (no input, no viewport)");
    Edf5BvmOnlinePlayerMade(0,SessionFixture::Player(false));
    Check(Edf5BvmOnlinePlayerArgs(1,&pad,&split) && pad==0 && split==1,"this machine's player: pad 0, the whole screen");
    Edf5BvmOnlinePlayerMade(1,SessionFixture::Player(false));
    Check(!Edf5BvmOnlinePlayerArgs(0,&pad,&split),"the loop's last player disarms: an offline creation keeps its own");
    // The host's view of the same room: itself first.
    SessionFixture::Users({false,true});
    Check(Edf5BvmOnlinePlayers()==2,"the host: both created");
    Check(Edf5BvmOnlinePlayerArgs(0,&pad,&split) && pad==0 && split==1,"the host's own player: pad 0");
    Edf5BvmOnlinePlayerMade(0,SessionFixture::Player(false));
    Check(Edf5BvmOnlinePlayerArgs(1,&pad,&split) && pad==-1,"the joiner on the host: no pad");
    Edf5BvmOnlinePlayerMade(1,nullptr);
    // Two local players (split screen online) of three: pads 0 and 1 as each is made, split by two.
    Put<std::uint32_t>(status,kLocalPlayers,2);
    SessionFixture::Users({false,true,false});
    Put<std::uint32_t>(status,kSessionPlayers,3);
    Check(Edf5BvmOnlinePlayers()==3,"three in the session");
    Check(Edf5BvmOnlinePlayerArgs(0,&pad,&split) && pad==0 && split==2,"the first local: pad 0, half the screen");
    Edf5BvmOnlinePlayerMade(0,SessionFixture::Player(false));
    Check(Edf5BvmOnlinePlayerArgs(1,&pad,&split) && pad==-1,"the remote between them");
    Edf5BvmOnlinePlayerMade(1,SessionFixture::Player(false));
    Check(Edf5BvmOnlinePlayerArgs(2,&pad,&split) && pad==1 && split==2,"the second local: pad 1 (a remote does not count)");
    Edf5BvmOnlinePlayerMade(2,SessionFixture::Player(false));
    // A local player whose creation failed is not counted, nor one the game marks not local (+0x128 bit 0).
    SessionFixture::Users({false,false,false});
    Check(Edf5BvmOnlinePlayers()==3,"three locals");
    Edf5BvmOnlinePlayerMade(0,nullptr);
    Check(Edf5BvmOnlinePlayerArgs(1,&pad,&split) && pad==0,"after a failed creation the next local takes pad 0");
    Edf5BvmOnlinePlayerMade(1,SessionFixture::Player(true));
    Check(Edf5BvmOnlinePlayerArgs(2,&pad,&split) && pad==0,"a player marked not local does not count");
    Edf5BvmOnlinePlayerMade(2,nullptr);
    Put<std::uint32_t>(status,kLocalPlayers,1);

    SessionFixture::Users({false,true,true,true});
    Put<std::uint32_t>(status,kSessionPlayers,4);
    Check(Edf5BvmOnlinePlayers()==4 && !Logged("EDF5 online:"),"four: all of them, nothing logged");
    SessionFixture::Users({false,true,true,true,true,true});
    Put<std::uint32_t>(status,kSessionPlayers,6);
    Check(Edf5BvmOnlinePlayers()==4,"six: the loop's four, never past its slots");
    Check(Logged("EDF5 online: 6 players in the session, an EDF5 mission creates 4"),"the cut logged");
    Check(!Edf5BvmOnlinePlayerArgs(4,&pad,&split),"no arguments past the four");
    lines.clear();
    Check(Edf5BvmOnlinePlayers()==4 && !Logged("EDF5 online:"),"once, not every call");
    SessionFixture::Users({true,true,true,true,false,true});
    lines.clear();clampLogged=false;
    Check(Edf5BvmOnlinePlayers()==4 && Logged("this machine's player is past the script's 4 slots"),
          "a machine whose player is cut is told so");
    SessionFixture::Users({false,true,true});
    Put<std::uint32_t>(status,kSessionPlayers,3);
    Check(Edf5BvmOnlinePlayers()==3,"back to three");

    // Nothing created when the per-player arguments cannot be put right.
    SessionFixture::Users({false});
    Check(Edf5BvmOnlinePlayers()==0 && Logged("users unreadable"),"a session list shorter than its count: nobody");
    Check(!Edf5BvmOnlinePlayerArgs(0,&pad,&split),"and nothing armed");
    SessionFixture::Users({false,true,true});
    gateReady=false;
    Check(Edf5BvmOnlinePlayers()==0 && Logged("mission_participant_gate) is not installed"),
          "no participant gate: nobody (no wrong pads, no split screen)");
    gateReady=true;
    SessionFixture::Uninstall(image+kSessionBase);
    Check(Edf5BvmOnlinePlayers()==0,"no session object: nobody");
    VirtualProtect(image+kSessionBase,8,oldSession,&oldSession);
    const unsigned char* const none=nullptr;
    std::memcpy(image+kGameStatus,&none,8);
    Check(Edf5BvmOnlinePlayers()==0 && Logged("no game status"),"no game status: nobody");
    VirtualProtect(image+kGameStatus,8,old,&old);
    std::printf("edf5_online_native_test: %d checks passed\n",checks);
    return 0;
}
