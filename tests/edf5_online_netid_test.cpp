// The EDF5 scripts' players registered online (src/edf5online.cpp, 0x22B045) on a private EDF.dll mapping, the game's
// own code from the registration to the room's maps: 0x7834F0 (the network wrapper's cast 0x22FCA0, the id 0x7772D0,
// its holder 0x774BD0, the wrapper 0x782720 and the ownership bits 0x782880, the room's pending list 0x75BE80), then the
// room's flush of that list (0x75BF70 -> 0x75C030 -> 0x75B780, its maps at +0x120 and +0xC0), for real soldiers' vtables
// (PaleWing, AssultSoldier) on made-up objects, a made-up session and room (MSVC's list / unordered_map layouts), the
// mapping's malloc / free / __RTDynamicCast imports this process's. Two machines of one room as 0x22AB90 registers its
// players online: the host (its own player 1, the guest's copy 2) and the guest (the host's copy 1, its own 2), and a
// room of eight. Stock, every player has the id of the script's position (one CreatePlayer, one position) and the room
// keeps the last one registered: the host's own player is in neither map, so its state sync (0x75D2E0 walks +0x120)
// never sends it. Patched, each is under kPlayerNetId | its index, the same on every machine; the host's own is the one
// entry the sync takes (+0x128 bit 1), and the game's own send test (NetworkObject slot 4, 0x774920) says send.
// Never starts the game.
#include "../src/crew.h"
#include "../src/memory.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include "../src/edf5online.cpp"
#include "../src/edf5online_room.h"

namespace crew {
unsigned char* image=nullptr;
bool MissionParticipantGateReady() noexcept { return true; }
bool Edf5CoopBvmTableHooked() noexcept { return true; }
bool Edf5RoomAllowsBvmPlayers(std::uint32_t,char*,std::size_t) noexcept { return true; }
}
namespace {
std::vector<std::string> lines;
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){
        std::fprintf(stderr,"FAIL: %s\n",why);
        for(const auto& line:lines)std::fprintf(stderr,"  log: %s\n",line.c_str());
        std::exit(1);
    }
}
}  // namespace
namespace crew {
void Log(const char* format,...) noexcept {
    char text[600];va_list args;va_start(args,format);vsnprintf_s(text,sizeof(text),_TRUNCATE,format,args);va_end(args);
    lines.emplace_back(text);
}
}

namespace {
using crew::image;
template<class T> T& Field(void* p,std::size_t off) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p)+off); }

constexpr unsigned kFlush=0x75BF70;   // with crew::kRegister (0x7834F0) and crew::kSessionBase
constexpr unsigned kMalloc=0x1756300,kFree=0x17562D0,kDynamicCast=0x1756098;   // the IAT slots
constexpr unsigned kPaleWing=0x17D0FF8,kPaleWingNet=0x17D12F0,kRanger=0x17CDF28,kRangerNet=0x17CE220;
constexpr std::size_t kNet=0x120,kNetFlags=0x128,kNetHolder=0x138,kSelf=0x28;
constexpr std::size_t kSendMap=0x120,kObjectMap=0xC0,kPending=8;
constexpr std::uint32_t kScriptAt=0x1234;   // 0x10AC6E0's answer, one for the whole loop

// A fault in the mapping: where, then out (no handler of the game's could run: its imports are not resolved).
LONG CALLBACK Fault(EXCEPTION_POINTERS* e) {
    if(e->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
    const auto rip=reinterpret_cast<unsigned char*>(e->ContextRecord->Rip);
    if(rip>=image && rip<image+0x22CE000)std::fprintf(stderr,"FAIL: access violation at EDF+0x%llX\n",
                                                      static_cast<unsigned long long>(rip-image));
    else std::fprintf(stderr,"FAIL: access violation at %p\n",static_cast<void*>(rip));
    std::fflush(stderr);
    std::_Exit(1);
}

struct Ctrl { void* const* vtable; volatile long uses; volatile long weaks; };
void __fastcall Keep(void*) {}
void* const ctrlVtable[]={reinterpret_cast<void*>(&Keep),reinterpret_cast<void*>(&Keep)};

// MSVC's std::list head and std::unordered_map (0x7592E0: +8 its list, +0x18 the bucket vector of lo/hi pairs, +0x30
// the mask, +0x38 the bucket count), empty.
void** Head() { auto** h=static_cast<void**>(std::calloc(1,0x40));h[0]=h;h[1]=h;return h; }
void MakeList(unsigned char* at) { Field<void**>(at,0)=Head();Field<std::uint64_t>(at,8)=0; }
void MakeMap(unsigned char* at) {
    constexpr int buckets=8;
    void** const head=Head();
    auto** const vec=static_cast<void**>(std::malloc(sizeof(void*)*2*buckets));
    for(int i=0;i<2*buckets;++i)vec[i]=head;
    Field<float>(at,0)=1.0f;
    Field<void**>(at,8)=head;Field<std::uint64_t>(at,0x10)=0;
    Field<void**>(at,0x18)=vec;Field<void**>(at,0x20)=vec+2*buckets;Field<void**>(at,0x28)=vec+2*buckets;
    Field<std::uint64_t>(at,0x30)=buckets-1;Field<std::uint64_t>(at,0x38)=buckets;
}
// The session 0x782880 reads (*(EDF+0x20B2AC0) - 0x98: the session at +0xD0, its control block at +0xD8) and its room
// (+0x50): the pending list at +8, the lists at +0x100 / +0x110, the maps.
struct alignas(16) Machine {
    unsigned char holder[0x200]{};
    Ctrl sessionCtrl{ctrlVtable,1000,1000};
    unsigned char session[0x100]{};
    unsigned char room[0x200]{};
};
Machine* NewMachine() {
    auto* const m=new Machine;
    Field<void*>(m->holder,0xD0)=m->session;Field<void*>(m->holder,0xD8)=&m->sessionCtrl;
    Field<void*>(m->session,0x50)=m->room;
    MakeList(m->room+kPending);MakeList(m->room+0x100);MakeList(m->room+0x110);
    MakeMap(m->room+kObjectMap);MakeMap(m->room+kSendMap);
    unsigned char* const base=m->holder+0x98;
    std::memcpy(image+crew::kSessionBase,&base,8);
    return m;
}
struct Soldier { unsigned char* obj; Ctrl ctrl; };
Soldier* NewSoldier(bool paleWing) {
    auto* const s=new Soldier{static_cast<unsigned char*>(_aligned_malloc(0x4000,16)),Ctrl{ctrlVtable,1000,1000}};
    std::memset(s->obj,0,0x4000);
    Field<void*>(s->obj,0)=image+(paleWing ? kPaleWing : kRanger);
    Field<void*>(s->obj,kNet)=image+(paleWing ? kPaleWingNet : kRangerNet);
    Field<void*>(s->obj,kSelf)=s->obj;Field<void*>(s->obj,kSelf+8)=&s->ctrl;   // its enable_shared_from_this
    return s;
}

using RegisterFn=void*(__fastcall*)(void*,void*,bool,std::uint32_t,std::int32_t,float);
using FlushFn=void(__fastcall*)(void*);
using SendFn=bool(__fastcall*)(void*);
// 0x22AFE7..0x22B045 for mission player `index`: the registration as the creation calls it (the patched site's landing,
// or the game's function), then the room's flush the next update makes.
void Register(RegisterFn fn,Machine* m,Soldier* s,int index,bool copy) {
    void* weak[2]={s->obj,&s->ctrl};
    fn(m->room,weak,copy,kScriptAt,index,0.0f);
}
struct Entry { std::uint32_t key[4]; unsigned char* obj; };
std::vector<Entry> Entries(Machine* m,std::size_t map) {
    std::vector<Entry> out;
    void** const head=Field<void**>(m->room,map+8);
    for(auto** n=static_cast<void**>(head[0]);n!=head;n=static_cast<void**>(n[0])) {
        Entry e{};
        std::memcpy(e.key,reinterpret_cast<unsigned char*>(n)+0x10,sizeof(e.key));
        // +0xC0's value {object, its NetworkObject}; +0x120's a sender whose +0x28 is that same value.
        unsigned char* const value=Field<unsigned char*>(n,0x30);
        e.obj=map==kObjectMap ? Field<unsigned char*>(value,0) : Field<unsigned char*>(Field<unsigned char*>(value,0x28),0);
        out.push_back(e);
    }
    return out;
}
const Entry* Keyed(const std::vector<Entry>& v,std::uint32_t id) {
    for(const auto& e:v)if(e.key[0]==id && !e.key[1] && !e.key[2] && !e.key[3])return &e;
    return nullptr;
}
bool Local(const unsigned char* obj) { return (Field<std::uint16_t>(const_cast<unsigned char*>(obj),kNetFlags)&2)!=0; }
bool Sends(unsigned char* obj) {   // NetworkObject slot 4 0x774920, as 0x75D2E0 asks it
    void* const net=obj+kNet;
    return (*reinterpret_cast<SendFn* const*>(net))[4](net);
}

// One machine of `count` players, `own` its own player's index: each registered in the creation's order, then flushed.
struct Run { Machine* m; std::vector<Soldier*> players; };
Run Room(RegisterFn fn,int count,int own) {
    Run r{NewMachine(),{}};
    for(int i=0;i<count;++i) {
        r.players.push_back(NewSoldier(i%2==0));
        Register(fn,r.m,r.players.back(),i,i!=own);
    }
    reinterpret_cast<FlushFn>(image+kFlush)(r.m->room);
    return r;
}
}  // namespace

int main(int argc,char** argv) {
    using namespace crew;
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: pass the supported EDF.dll path (private mapping, no DllMain)");return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    Check(image!=nullptr,"private DLL mapped without its entrypoint");
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    Check(nt->FileHeader.TimeDateStamp==0x678CCB46 && nt->OptionalHeader.SizeOfImage==0x22CE000,"supported game profile");
    Check(AddVectoredExceptionHandler(1,&Fault)!=nullptr,"fault handler");
    HMODULE vcruntime=GetModuleHandleA("vcruntime140.dll");
    if(!vcruntime)vcruntime=LoadLibraryA("vcruntime140.dll");
    Check(vcruntime!=nullptr,"vcruntime140 for __RTDynamicCast");
    const auto import=[](unsigned slot,void* to) {
        DWORD old=0;
        Check(VirtualProtect(image+slot,8,PAGE_READWRITE,&old)!=0,"an IAT slot writable");
        std::memcpy(image+slot,&to,8);
    };
    import(kMalloc,reinterpret_cast<void*>(&std::malloc));
    import(kFree,reinterpret_cast<void*>(&std::free));
    import(kDynamicCast,reinterpret_cast<void*>(GetProcAddress(vcruntime,"__RTDynamicCast")));
    {DWORD old=0;Check(VirtualProtect(image+kSessionBase,8,PAGE_READWRITE,&old)!=0,"the session base writable");}
    const auto stock=reinterpret_cast<RegisterFn>(image+kRegister);
    Check(image[kRegisterCall]==0xE8 && CallsTo(kRegisterCall,kRegister),"0x22B045 calls 0x7834F0");

    // Stock: one id for both, the last registered kept.
    {
        const Run host=Room(stock,2,0),guest=Room(stock,2,1);
        for(const Run* r:{&host,&guest}) {
            const auto send=Entries(r->m,kSendMap),objects=Entries(r->m,kObjectMap);
            Check(send.size()==1 && objects.size()==1,"stock: two players, one entry in each of the room's maps");
            Check(Keyed(send,kScriptAt) && Keyed(objects,kScriptAt),"stock: under the script's position");
            Check(send[0].obj==r->players[1]->obj && objects[0].obj==r->players[1]->obj,"stock: the second player's");
        }
        Check(!Local(host.players[1]->obj) && Local(host.players[0]->obj),"stock, host: its own player 1, the guest's copy 2");
        Check(Local(guest.players[1]->obj) && !Local(guest.players[0]->obj),"stock, guest: the host's copy 1, its own 2");
        bool hostSends=false;
        for(const auto& e:Entries(host.m,kSendMap))hostSends=hostSends || (e.obj==host.players[0]->obj);
        Check(!hostSends,"stock, host: its own player not in the map the state sync walks");
    }

    Check(InstallEdf5Online() && Edf5OnlineReady(),"installed on the supported build");
    unsigned char* to=image+kRegisterCall+5;
    {std::int32_t rel;std::memcpy(&rel,image+kRegisterCall+1,4);to+=rel;}
    Check(to[0]==0xFF && to[1]==0x25,"0x22B045 to a thunk");
    std::uintptr_t target;std::memcpy(&target,to+6,8);
    Check(target==reinterpret_cast<std::uintptr_t>(&Edf5BvmRegisterPlayer),"which jumps to Edf5BvmRegisterPlayer");
    const auto patched=reinterpret_cast<RegisterFn>(to);   // through the site's own landing

    // Patched, two machines of one room: each player under its index, both kept, the same ids on both.
    for(int count:{2,8}) {
        const Run host=Room(patched,count,0),guest=Room(patched,count,1);
        for(const Run* r:{&host,&guest}) {
            const auto send=Entries(r->m,kSendMap),objects=Entries(r->m,kObjectMap);
            Check(send.size()==static_cast<std::size_t>(count) && objects.size()==static_cast<std::size_t>(count),
                  "patched: every player in each of the room's maps");
            for(int i=0;i<count;++i) {
                const Entry* const s=Keyed(send,kPlayerNetId|static_cast<std::uint32_t>(i));
                const Entry* const o=Keyed(objects,kPlayerNetId|static_cast<std::uint32_t>(i));
                Check(s && o && s->obj==r->players[i]->obj && o->obj==r->players[i]->obj,
                      "patched: player i under kPlayerNetId | i in both maps, on both machines");
            }
        }
        int local=0;
        for(const auto& e:Entries(host.m,kSendMap)) {
            if(!Local(e.obj))continue;
            ++local;
            Check(e.obj==host.players[0]->obj,"patched, host: the local entry is its own player");
            Check(Sends(e.obj),"patched, host: the game's send test (0x774920) sends it");
        }
        Check(local==1,"patched, host: one local entry for the state sync");
        Check(Local(guest.players[1]->obj) && Field<void*>(guest.players[1]->obj,kNetHolder)!=nullptr,
              "patched, guest: its own player local, with its id holder");
    }
    std::printf("edf5 online netid: %d checks\n",checks);
    return 0;
}
