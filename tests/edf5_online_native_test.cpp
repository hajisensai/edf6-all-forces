// The EDF5 scripts' player natives online (src/edf5online.cpp) on a private EDF.dll mapping: native 0x10's call
// points at AngelScript's PreloadPlayerResource, CreatePlayer's empty online branch enters the creation loop with the
// session's count, each player given AngelScript's pad and split (a session fixture); the game's own creation loop
// (0x22B3B0..0x22B878) run on a made-up frame and BVM object, stock and patched, for four, six, eight and forty
// players; the one creation (0x22AB90) run for an index the game makes no object for; another build is left
// untouched. The script's player table past four rows: the creation's rows and every reader of the table (the route
// guide 0x21F380, Factor_PlayerAreaIn's 0x207870, natives 0x3ED / 0x3EE / 0x3EF, 0x2252D0 / 0x2253A0, the first
// player's 0x229040) run for real for six and forty players, EDF6Coop's four bounds in place as its hooks place them.
// A failed write puts every site back and frees the page (`rollback`). Never starts the game.
#include "../src/crew.h"
#include "../src/memory.h"
#include <cstdarg>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>
#include "../src/edf5online.cpp"
#include "../src/edf5online_room.h"

namespace crew {
unsigned char* image=nullptr;
bool gateReady=true;
bool MissionParticipantGateReady() noexcept { return gateReady; }
// edf5online_room.cpp's answers, the test's to give.
bool coopHooked=true,roomAllows=true;
bool Edf5CoopBvmTableHooked() noexcept { return coopHooked; }
bool Edf5RoomAllowsBvmPlayers(std::uint32_t,char* why,std::size_t size) noexcept {
    if(!roomAllows)std::snprintf(why,size,"mission player 6 did not tell the room");
    return roomAllows;
}
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
    if(to[0]==0xFF && to[1]==0x25 && !to[2] && !to[3] && !to[4] && !to[5]) {std::uintptr_t a;std::memcpy(&a,to+6,8);return reinterpret_cast<unsigned char*>(a);}
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
// std::vector<std::shared_ptr<User>>, each user's +0x10 bit 1 the remote flag and +0x48 its mission index, each
// control block counting its dispose and destroy, the storage given back through freeFn - a big one (0x1000 bytes and
// over) allocated as MSVC does, 32-aligned with the real block 8 bytes before it.
namespace SessionFixture {
struct Ctrl { void* const* vtable; volatile long uses; volatile long weaks; int disposed; int destroyed; };
void __fastcall Dispose(void* c) { ++static_cast<Ctrl*>(c)->disposed; }
void __fastcall Destroy(void* c) { ++static_cast<Ctrl*>(c)->destroyed; }
void* const ctrlVtable[]={reinterpret_cast<void*>(&Dispose),reinterpret_cast<void*>(&Destroy)};
struct User { unsigned char bytes[0x60]; };
struct Entry { bool remote; int index; };
std::vector<Entry> entries;
std::size_t padTo=0;   // the list's length with empty entries after the users (a big storage)
User users[8]{};
Ctrl ctrls[8]{};
crew::Shared* storage=nullptr;
void* block=nullptr;
std::size_t storageBytes=0,blockBytes=0;
bool freed=false;
crew::SharedVector* __fastcall List(void*,crew::SharedVector* out) {
    const std::size_t n=entries.size()>padTo ? entries.size() : padTo;
    storageBytes=(n ? n : 1)*sizeof(crew::Shared);
    if(storageBytes>=0x1000) {
        blockBytes=storageBytes+0x27;
        block=std::malloc(blockBytes);
        auto at=(reinterpret_cast<std::uintptr_t>(block)+0x27)&~std::uintptr_t{0x1F};
        reinterpret_cast<void**>(at)[-1]=block;
        storage=reinterpret_cast<crew::Shared*>(at);
    } else {
        blockBytes=storageBytes;
        block=std::malloc(blockBytes);
        storage=static_cast<crew::Shared*>(block);
    }
    std::memset(storage,0,n*sizeof(crew::Shared));
    freed=false;
    for(std::size_t i=0;i<entries.size();++i) {
        std::memset(&users[i],0,sizeof(User));
        users[i].bytes[0x10]=entries[i].remote ? 2 : 1;   // bit 1 remote; bit 0 set either way (not the flag)
        std::memcpy(users[i].bytes+0x48,&entries[i].index,4);
        ctrls[i]=Ctrl{ctrlVtable,1,1,0,0};
        storage[i]={&users[i],&ctrls[i]};
    }
    *out={storage,storage+n,reinterpret_cast<crew::Shared*>(reinterpret_cast<unsigned char*>(storage)+storageBytes)};
    return out;
}
void* const sessionVtable[]={nullptr,reinterpret_cast<void*>(&List)};
struct Session { void* const* vtable; } session{sessionVtable};
alignas(16) unsigned char holder[0x100]{};   // the session base points 0x98 into it, the object at +0xD0
void __fastcall Free(void* at,std::size_t bytes) {
    if(at==block && bytes==blockBytes){freed=true;std::free(at);block=nullptr;storage=nullptr;}
}
// In list order, each at the mission index given (default: its place in the list).
void Users(std::initializer_list<Entry> list) { entries.assign(list.begin(),list.end());padTo=0; }
void Users(std::initializer_list<bool> list) {
    entries.clear();
    for(bool r:list)entries.push_back({r,static_cast<int>(entries.size())});
    padTo=0;
}
bool Released() {
    for(std::size_t i=0;i<entries.size();++i)
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
// GameStatus' online test (edf5online.cpp BvmOnline): +0x38 the mode slot, +0x20 the modes, the mode's +0x10 object
// with its virtual base's offset at +8 and +0x68 set online.
alignas(16) unsigned char modeObject[0x100]{};
alignas(16) unsigned char mode[0x40]{};
void* modes[1]{};
void Online(unsigned char* status,bool online) {
    std::memset(modeObject,0,sizeof(modeObject));
    const std::int32_t vbase=0x10;std::memcpy(modeObject+8,&vbase,4);
    const std::int32_t flag=1;std::memcpy(modeObject+vbase+0x68,&flag,4);
    void* const obj=modeObject;std::memcpy(mode+0x10,&obj,8);
    modes[0]=mode;
    void* const table=modes;std::memcpy(status+0x20,&table,8);
    const std::int32_t slot=online ? 0 : -1;std::memcpy(status+0x38,&slot,4);
}
}  // namespace SessionFixture

// The game's BVM creation loop run for real (LoopFixture::Loop): entered at 0x22B3B0 with the state 0x22B1C0 has there
// (rsi the BVM object, r13 the count, r15 0, xmm11 0, its frame: rbp = rsp + 0x100, the spawn point's matrix at
// rbp-0x30..rbp+0xF), left at 0x22B878 (the loop done) or 0x22BB94 (left early) back to the test. Its calls: 0x221DC0
// (the ground under a player, recorded), 0x22AB90 (the creation, recorded, a made-up player and control block per
// index; with `gate` as the participant gate wraps it, Edf5BvmOnlinePlayerArgs / PlayerMade), 0x1F74A0 (the script's
// group, recorded), 0x2086E0 (the object list's room, kept by the fixture). The frame is on this thread's stack (SEH
// works in it) in a buffer filled with a pattern: what the loop must never write (its stack cookie at rbp+0x1B0 and
// above) is checked.
namespace LoopFixture {
using crew::Shared;
constexpr int kMax=64;
struct Ctrl { void* const* vtable; volatile long uses; volatile long weaks; };
void __fastcall Noop(void*) {}
void* const ctrlVtable[]={reinterpret_cast<void*>(&Noop),reinterpret_cast<void*>(&Noop)};
alignas(16) unsigned char bodies[kMax][0x1600];   // a player's +0x158C / +0x1588 / +0x380 read and written
Ctrl ctrls[kMax];
alignas(16) unsigned char bvm[0x500];   // +0x490: 0x229040's done flag
Shared listData[kMax];
struct Made { float ground[4]; float matrixPos[4]; int pad,split,mode; bool online; int onlinePad,onlineSplit; };
Made made[kMax];
int grounds=0,creations=0,groupPushes=0;
const void* groupObjects[kMax];
bool gate=false;
bool __fastcall Ground(void*,float* pos,float*,float,bool) {
    if(grounds<kMax)std::memcpy(made[grounds].ground,pos,16);
    ++grounds;
    return true;
}
Shared* __fastcall Create(void*,Shared* out,const float* matrix,int index,int pad,int split,int,bool,int mode,void*) {
    ++creations;
    if(index<0 || index>=kMax){*out={};return out;}
    Made& m=made[index];
    std::memcpy(m.matrixPos,matrix+12,16);
    m.pad=pad;m.split=split;m.mode=mode;
    m.online=false;
    if(gate) {
        int p=pad,s=split;
        m.online=crew::Edf5BvmOnlinePlayerArgs(index,&p,&s);
        m.onlinePad=p;m.onlineSplit=s;
    }
    _InterlockedIncrement(&ctrls[index].weaks);   // a weak_ptr handed out (0x22B671 lets it go)
    *out={bodies[index],&ctrls[index]};
    if(gate && m.online)crew::Edf5BvmOnlinePlayerMade(index,bodies[index]);
    return out;
}
void __fastcall GroupPush(void*,const Shared* weak) { if(groupPushes<kMax)groupObjects[groupPushes]=weak->obj;++groupPushes; }
bool roomShort=false;
void __fastcall Reserve(void* list) {
    const auto count=crew::At<std::uint64_t>(list,0x1E0-0x1C8);
    if(count>=static_cast<std::uint64_t>(kMax))roomShort=true;
}
// The BVM object as the loop reads it: +0x168 the four rows, +0x1D0 the object list's storage and +0x1E0 its count,
// +0x2D0 the script's current group (0: the group at +0x210).
void Fresh() {
    crew::Edf5BvmRowsGone(bvm);   // the plugin's rows of the last run let go first (as the object's destructor does)
    std::memset(bvm,0,sizeof(bvm));std::memset(bodies,0,sizeof(bodies));std::memset(listData,0,sizeof(listData));
    std::memset(made,0,sizeof(made));std::memset(groupObjects,0,sizeof(groupObjects));
    for(auto& c:ctrls)c=Ctrl{ctrlVtable,1,1};
    Shared* const data=listData;std::memcpy(bvm+0x1D0,&data,8);
    grounds=creations=groupPushes=0;roomShort=false;
}
struct Run { std::uint64_t savedRsp,frameRsp,frameRbp,object,count; };
Run run{};
using EnterFn=void(*)(Run*);
EnterFn enter=nullptr;
unsigned char* code=nullptr;
// The spawn point (0x6F8280's matrix at rbp-0x30): turned 30 degrees, at (100, 5, 200).
const float kSpawn[16]={0.8660254f,0,-0.5f,0, 0,1,0,0, 0.5f,0,0.8660254f,0, 100,5,200,1};
constexpr unsigned kExitDone=0x22B878,kExitEarly=0x22BB94;
void Emit(unsigned char*& p,std::initializer_list<unsigned char> bytes) { for(unsigned char b:bytes)*p++=b; }
void Emit64(unsigned char*& p,const void* value) { std::memcpy(p,&value,8);p+=8; }
void Write(unsigned char* at,const unsigned char* bytes,std::size_t size) {
    DWORD old=0;VirtualProtect(at,size,PAGE_EXECUTE_READWRITE,&old);std::memcpy(at,bytes,size);VirtualProtect(at,size,old,&old);
}
bool Redirect(unsigned site,unsigned target,void* to) {
    bool changed=false;
    return crew::RedirectCall(crew::image+site,crew::image+target,to,changed) && changed;
}
// The loop's calls pointed at the fixture, its two ways out at the way back; the entry and exit code made.
bool Install() {
    using crew::image;
    code=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!code)return false;
    unsigned char* p=code;
    // Enter (rcx = &run): the callee-saved registers and xmm6-15 kept, rsp saved in run, the loop's state, jmp 0x22B3B0.
    Emit(p,{0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0xA8,0x00,0x00,0x00});
    Emit(p,{0xF3,0x0F,0x7F,0x34,0x24, 0xF3,0x0F,0x7F,0x7C,0x24,0x10,
            0xF3,0x44,0x0F,0x7F,0x44,0x24,0x20, 0xF3,0x44,0x0F,0x7F,0x4C,0x24,0x30, 0xF3,0x44,0x0F,0x7F,0x54,0x24,0x40,
            0xF3,0x44,0x0F,0x7F,0x5C,0x24,0x50, 0xF3,0x44,0x0F,0x7F,0x64,0x24,0x60, 0xF3,0x44,0x0F,0x7F,0x6C,0x24,0x70,
            0xF3,0x44,0x0F,0x7F,0xB4,0x24,0x80,0x00,0x00,0x00, 0xF3,0x44,0x0F,0x7F,0xBC,0x24,0x90,0x00,0x00,0x00});
    Emit(p,{0x48,0x89,0x21, 0x48,0x8B,0x61,0x08, 0x48,0x8B,0x69,0x10, 0x48,0x8B,0x71,0x18, 0x4C,0x8B,0x69,0x20,
            0x45,0x33,0xFF, 0x45,0x0F,0x57,0xDB, 0x48,0xB8});
    Emit64(p,image+0x22B3B0);
    Emit(p,{0xFF,0xE0});
    enter=reinterpret_cast<EnterFn>(code);
    // Exit: rsp back from run, xmm6-15 and the registers restored, ret to the caller of enter.
    unsigned char* const exitCode=p;
    Emit(p,{0x48,0xB8});Emit64(p,&run);
    Emit(p,{0x48,0x8B,0x20});
    Emit(p,{0xF3,0x0F,0x6F,0x34,0x24, 0xF3,0x0F,0x6F,0x7C,0x24,0x10,
            0xF3,0x44,0x0F,0x6F,0x44,0x24,0x20, 0xF3,0x44,0x0F,0x6F,0x4C,0x24,0x30, 0xF3,0x44,0x0F,0x6F,0x54,0x24,0x40,
            0xF3,0x44,0x0F,0x6F,0x5C,0x24,0x50, 0xF3,0x44,0x0F,0x6F,0x64,0x24,0x60, 0xF3,0x44,0x0F,0x6F,0x6C,0x24,0x70,
            0xF3,0x44,0x0F,0x6F,0xB4,0x24,0x80,0x00,0x00,0x00, 0xF3,0x44,0x0F,0x6F,0xBC,0x24,0x90,0x00,0x00,0x00});
    Emit(p,{0x48,0x81,0xC4,0xA8,0x00,0x00,0x00,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0x5E,0x5D,0x5B,0xC3});
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    std::memcpy(jump+2,&exitCode,8);
    Write(image+kExitDone,jump,sizeof(jump));
    Write(image+kExitEarly,jump,sizeof(jump));
    return Redirect(0x22B5E5,0x221DC0,reinterpret_cast<void*>(&Ground)) &&
           Redirect(0x22B626,0x22AB90,reinterpret_cast<void*>(&Create)) &&
           Redirect(0x22B760,0x1F74A0,reinterpret_cast<void*>(&GroupPush)) &&
           Redirect(0x22B78C,0x2086E0,reinterpret_cast<void*>(&Reserve));
}
constexpr unsigned char kPattern=0xA5;
struct Result { bool guardKept; std::size_t firstWritten; };
// The loop for `count` players on a fresh object. guardKept: nothing at or past rbp+0x1B0 (the stack cookie, then the
// function's saved registers and its caller's frame) written.
__declspec(noinline) Result Loop(std::uint32_t count) {
    Fresh();
    alignas(16) unsigned char stack[0x10000];
    std::memset(stack,kPattern,sizeof(stack));
    unsigned char* const rsp=stack+sizeof(stack)-0x1000;
    unsigned char* const rbp=rsp+0x100;
    std::memcpy(rbp-0x30,kSpawn,sizeof(kSpawn));
    std::memset(rbp-0x80,0,0x50);          // rbp-0x80..-0x31: the loop's weak temporaries, the list (rbp-0x58), the mode
    std::memset(rsp,0,0x80);               // the outgoing arguments and temporaries
    run=Run{0,reinterpret_cast<std::uint64_t>(rsp),reinterpret_cast<std::uint64_t>(rbp),
            reinterpret_cast<std::uint64_t>(bvm),count};
    enter(&run);
    Result r{true,0};
    for(unsigned char* q=rbp+0x1B0;q<stack+sizeof(stack);++q)
        if(*q!=kPattern){r.guardKept=false;r.firstWritten=static_cast<std::size_t>(q-rbp);break;}
    return r;
}
const void* RowObject(int i) { return crew::At<const void*>(bvm,0x170+0x18*static_cast<std::size_t>(i)); }
const void* RowCtrl(int i) { return crew::At<const void*>(bvm,0x178+0x18*static_cast<std::size_t>(i)); }
std::int32_t RowIndex(int i) { return crew::At<std::int32_t>(bvm,0x168+0x18*static_cast<std::size_t>(i)); }
std::uint64_t ListCount() { return crew::At<std::uint64_t>(bvm,0x1E0); }
bool SameFloats(const float* a,const float* b,int n) { return !std::memcmp(a,b,sizeof(float)*static_cast<std::size_t>(n)); }
}  // namespace LoopFixture

// 0x591130 for an index the game makes no object for (no user there, or past its online records' four), and the one
// import 0x22AB90 reaches on its empty path: __RTDynamicCast (0x12DA7AA -> its IAT slot), null in, null out.
void* __fastcall NoOnlineObject(int,const void*,int) { return nullptr; }
void* __cdecl NullCast(void* p,long,void*,void*,int) { return p; }
using UpperFn=crew::Shared*(__fastcall*)(void*,crew::Shared*,const float*,int,int,int,int,bool,int,void*);
// A fault inside the mapping cannot be caught by __try: unwinding its frames runs the game's own frame handlers, whose
// imports this mapping never resolved. Guarded(body, arg) keeps the registers and rsp; a vectored handler sends an
// access violation in the mapping to the landing, which takes them back and returns 0 (1: body returned).
namespace Guard {
std::uint64_t savedRsp=0;
unsigned char* landing=nullptr;
bool armed=false;
using GuardedFn=int(*)(void(*)(void*),void*);
GuardedFn guarded=nullptr;
LONG CALLBACK Handler(EXCEPTION_POINTERS* e) {
    const auto rip=reinterpret_cast<unsigned char*>(e->ContextRecord->Rip);
    if(!armed || e->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION ||
       rip<crew::image || rip>=crew::image+0x22CE000)return EXCEPTION_CONTINUE_SEARCH;
    e->ContextRecord->Rip=reinterpret_cast<DWORD64>(landing);
    return EXCEPTION_CONTINUE_EXECUTION;
}
bool Install() {
    using LoopFixture::Emit;using LoopFixture::Emit64;
    auto* const code=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!code || !AddVectoredExceptionHandler(1,&Handler))return false;
    unsigned char* p=code;
    // push the callee-saved registers; sub rsp,0xA8; xmm6-15 kept; savedRsp = rsp; body(arg) with its shadow space.
    Emit(p,{0x53,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x81,0xEC,0xA8,0x00,0x00,0x00});
    Emit(p,{0xF3,0x0F,0x7F,0x34,0x24, 0xF3,0x0F,0x7F,0x7C,0x24,0x10,
            0xF3,0x44,0x0F,0x7F,0x44,0x24,0x20, 0xF3,0x44,0x0F,0x7F,0x4C,0x24,0x30, 0xF3,0x44,0x0F,0x7F,0x54,0x24,0x40,
            0xF3,0x44,0x0F,0x7F,0x5C,0x24,0x50, 0xF3,0x44,0x0F,0x7F,0x64,0x24,0x60, 0xF3,0x44,0x0F,0x7F,0x6C,0x24,0x70,
            0xF3,0x44,0x0F,0x7F,0xB4,0x24,0x80,0x00,0x00,0x00, 0xF3,0x44,0x0F,0x7F,0xBC,0x24,0x90,0x00,0x00,0x00});
    Emit(p,{0x48,0xB8});Emit64(p,&savedRsp);
    Emit(p,{0x48,0x89,0x20, 0x48,0x8B,0xC1, 0x48,0x8B,0xCA, 0x48,0x83,0xEC,0x20, 0xFF,0xD0, 0x48,0x83,0xC4,0x20,
            0xB8,0x01,0x00,0x00,0x00, 0xEB,0x0F});   // mov [rax],rsp; mov rax,rcx; mov rcx,rdx; call; mov eax,1; jmp out
    landing=p;   // rsp = savedRsp; eax = 0 (15 bytes)
    Emit(p,{0x48,0xB8});Emit64(p,&savedRsp);
    Emit(p,{0x48,0x8B,0x20, 0x31,0xC0});
    // out: xmm6-15 and the registers back; ret.
    Emit(p,{0xF3,0x0F,0x6F,0x34,0x24, 0xF3,0x0F,0x6F,0x7C,0x24,0x10,
            0xF3,0x44,0x0F,0x6F,0x44,0x24,0x20, 0xF3,0x44,0x0F,0x6F,0x4C,0x24,0x30, 0xF3,0x44,0x0F,0x6F,0x54,0x24,0x40,
            0xF3,0x44,0x0F,0x6F,0x5C,0x24,0x50, 0xF3,0x44,0x0F,0x6F,0x64,0x24,0x60, 0xF3,0x44,0x0F,0x6F,0x6C,0x24,0x70,
            0xF3,0x44,0x0F,0x6F,0xB4,0x24,0x80,0x00,0x00,0x00, 0xF3,0x44,0x0F,0x6F,0xBC,0x24,0x90,0x00,0x00,0x00});
    Emit(p,{0x48,0x81,0xC4,0xA8,0x00,0x00,0x00,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5F,0x5E,0x5D,0x5B,0xC3});
    guarded=reinterpret_cast<GuardedFn>(code);
    return true;
}
}  // namespace Guard
// EDF6Coop's BvmPlayerTableHooks as EDF6Coop installs them (multislot/src/patches.cpp, code.cpp JumpBytes): the whole
// instruction replaced by a jmp rel32 to a thunk, the rest nop; the thunk reads the entry for player 0..3 and gives an
// empty one (rdx 0) for any other index (mission.cpp BvmPlayerEntry). `bounded` counts the empty ones given.
namespace CoopEmu {
struct Site { unsigned rva; std::size_t size; unsigned char cmp[4]; };
const Site kSites[]={{0x21F3CF,8,{0x49,0x83,0xFC,0x04}},    // cmp r12,4 (the loop's player)
                     {0x222761,7,{0x48,0x83,0xF8,0x04}},    // cmp rax,4 (movsxd rax,edx: the script's index)
                     {0x228AED,7,{0x48,0x83,0xF8,0x04}},
                     {0x22A685,7,{0x48,0x83,0xF8,0x04}}};
volatile long bounded=0;
bool Install() {
    using LoopFixture::Emit;using LoopFixture::Emit64;
    unsigned char code[512];
    unsigned char* p=code;
    std::size_t entry[4]{};
    for(int i=0;i<4;++i) {
        const Site& s=kSites[i];
        entry[i]=static_cast<std::size_t>(p-code);
        Emit(p,{s.cmp[0],s.cmp[1],s.cmp[2],s.cmp[3],0x73,static_cast<unsigned char>(s.size+14)});
        for(std::size_t k=0;k<s.size;++k)*p++=crew::image[s.rva+k];
        Emit(p,{0xFF,0x25,0,0,0,0});Emit64(p,crew::image+s.rva+s.size);
        // push rax; mov rax,&bounded; lock inc dword [rax]; pop rax; xor edx,edx; back.
        Emit(p,{0x50,0x48,0xB8});Emit64(p,const_cast<long*>(&bounded));
        Emit(p,{0xF0,0xFF,0x00,0x58,0x31,0xD2,0xFF,0x25,0,0,0,0});Emit64(p,crew::image+s.rva+s.size);
    }
    auto* const page=static_cast<unsigned char*>(crew::AllocateNearCode(crew::image+0x21F3CF,code,static_cast<std::size_t>(p-code)));
    if(!page)return false;
    for(int i=0;i<4;++i) {
        const Site& s=kSites[i];
        unsigned char bytes[8];
        const auto rel=static_cast<std::int32_t>(page+entry[i]-(crew::image+s.rva+5));
        bytes[0]=0xE9;std::memcpy(bytes+1,&rel,4);
        for(std::size_t k=5;k<s.size;++k)bytes[k]=0x90;
        LoopFixture::Write(crew::image+s.rva,bytes,s.size);
    }
    return true;
}
}  // namespace CoopEmu

// The table's readers run for real on LoopFixture's object after a loop: each player's vf 0x298 (0x222740 calls it) its
// index + 100; 0x22A650's process at +0x70; Factor_PlayerAreaIn's Initialize (0x207870) and 0x21F380's guide
// construction (0x12D85B0 new, 0x5C6C10 its constructor) recorded; 0x229040's two ways on (found at 0x229141 with rbp
// the player, nobody at 0x229208) back to the test through Guard's landing.
namespace Readers {
using LoopFixture::bodies;using LoopFixture::bvm;using LoopFixture::ctrls;
constexpr int kMost=LoopFixture::kMax;
std::int32_t __fastcall Vf298(void* self) {
    return 100+static_cast<std::int32_t>((static_cast<unsigned char*>(self)-&bodies[0][0])/static_cast<std::ptrdiff_t>(sizeof(bodies[0])));
}
void __fastcall Vf198(void*) {}
void* vtable[0x300/8]{};
alignas(16) unsigned char process[0x100]{};
void Dress() {
    vtable[0x298/8]=reinterpret_cast<void*>(&Vf298);
    vtable[0x198/8]=reinterpret_cast<void*>(&Vf198);
    void* const vt=vtable;
    for(int i=0;i<kMost;++i) {   // the vtable; the player's weak-this (+0x28 / +0x30), which the route guide's InitParam copies
        std::memcpy(bodies[i],&vt,8);
        void* const self=bodies[i];void* const ctrl=&ctrls[i];
        std::memcpy(bodies[i]+0x28,&self,8);std::memcpy(bodies[i]+0x30,&ctrl,8);
    }
    std::memset(process,0,sizeof(process));
    process[0x78]=1;   // [process+0x78] != 0: 0x22A650 keeps its count
    unsigned char* const at=process;std::memcpy(bvm+0x70,&at,8);
}
using GetFn=std::int32_t(__fastcall*)(void*,std::int32_t);
using WaitFn=void(__fastcall*)(void*,std::int32_t,std::int32_t,std::int32_t);
using SetFn=void(__fastcall*)(void*,std::int32_t,std::int32_t);
using FlagFn=void(__fastcall*)(void*,bool);
bool Flag(int i,unsigned bit) { return (crew::At<std::uint32_t>(bodies[i],0x380)>>bit)&1; }
// Factor_PlayerAreaIn's Initialize: what it was handed.
std::uint32_t areaCount=0;
crew::Shared areaCopy[kMost];
bool __fastcall AreaInit(void*,void*,const crew::Shared* players,std::uint32_t count,std::int32_t,std::int32_t*) {
    areaCount=count;
    for(std::uint32_t i=0;i<count && i<static_cast<std::uint32_t>(kMost);++i)areaCopy[i]=players[i];
    return true;
}
// rbp = bvm, then target(a1..a6): the call at 0x221687 as 0x2214B0 makes it.
struct AreaArgs { void* bvm; void* target; void* factor; void* area; const crew::Shared* players; std::uint64_t count,flag; std::int32_t* mode; };
using AreaCallFn=std::uint64_t(*)(AreaArgs*);
AreaCallFn areaCall=nullptr;
// The route guide: one per player it is made for (the InitParam's player at +0x38, 0x21F49B).
const void* guided[kMost];
int guides=0;
bool __fastcall GuideOnline(void*) { return true; }
alignas(16) unsigned char guideBody[0x300];
void* __fastcall GuideNew(std::size_t) { std::memset(guideBody,0,sizeof(guideBody));return guideBody; }
void* __fastcall GuideMake(void* at,const unsigned char* param) {
    if(guides<kMost)guided[guides]=crew::At<const void*>(param,0x38);
    ++guides;
    return at;
}
bool __fastcall NotBusy(void*) { return false; }
// 0x229040's ways on.
const void* firstFound=nullptr;
const void* const kNobody=reinterpret_cast<const void*>(1);
bool Install() {
    using LoopFixture::Emit;using LoopFixture::Redirect;
    auto* const code=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!code)return false;
    unsigned char* p=code;
    // push rbp; push rbx; sub rsp,0x38; rbx = args; rbp = bvm; [rsp+0x20] flag; [rsp+0x28] mode; rcx/rdx/r8/r9; call target.
    Emit(p,{0x55,0x53,0x48,0x83,0xEC,0x38,0x48,0x8B,0xD9,0x48,0x8B,0x2B,0x48,0x8B,0x43,0x30,0x48,0x89,0x44,0x24,0x20,
            0x48,0x8B,0x43,0x38,0x48,0x89,0x44,0x24,0x28,0x48,0x8B,0x4B,0x10,0x48,0x8B,0x53,0x18,0x4C,0x8B,0x43,0x20,
            0x4C,0x8B,0x4B,0x28,0xFF,0x53,0x08,0x48,0x83,0xC4,0x38,0x5B,0x5D,0xC3});
    areaCall=reinterpret_cast<AreaCallFn>(code);
    // 0x207870 -> AreaInit (mov rax,imm64; jmp rax).
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const void* to=reinterpret_cast<const void*>(&AreaInit);std::memcpy(jump+2,&to,8);
    LoopFixture::Write(crew::image+0x207870,jump,sizeof(jump));
    // 0x229141 (found, rbp the player): firstFound = rbp; 0x229208 (nobody): firstFound = kNobody; then Guard's landing.
    unsigned char found[25]={0x48,0xB8,0,0,0,0,0,0,0,0,0x48,0x89,0x28,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const void* slot=&firstFound;std::memcpy(found+2,&slot,8);std::memcpy(found+15,&Guard::landing,8);
    LoopFixture::Write(crew::image+0x229141,found,sizeof(found));
    unsigned char nobody[29]={0x48,0xB8,0,0,0,0,0,0,0,0,0x48,0xC7,0x00,0x01,0x00,0x00,0x00,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    std::memcpy(nobody+2,&slot,8);std::memcpy(nobody+19,&Guard::landing,8);
    LoopFixture::Write(crew::image+0x229208,nobody,sizeof(nobody));
    return Redirect(0x229069,0x5D9F70,reinterpret_cast<void*>(&NotBusy)) &&
           Redirect(0x21F417,0x7748F0,reinterpret_cast<void*>(&GuideOnline)) &&
           Redirect(0x21F66C,0x12D85B0,reinterpret_cast<void*>(&GuideNew)) &&
           Redirect(0x21F681,0x5C6C10,reinterpret_cast<void*>(&GuideMake));
}
void CallFirst(void*) { reinterpret_cast<void(__fastcall*)(void*)>(crew::image+0x229040)(bvm); }
// 0x229040 for real: the player it found, kNobody when none, null on a fault.
const void* First() {
    firstFound=nullptr;
    Guard::armed=true;
    const int returned=Guard::guarded(&CallFirst,nullptr);
    Guard::armed=false;
    return returned ? nullptr : firstFound;
}
void CallGuide(void*) { reinterpret_cast<void(__fastcall*)(void*,int,float)>(crew::image+0x21F380)(bvm,0,1.0f); }
// 0x21F380 for real (the route guide to object list entry 0): the guides made, -1 on a fault.
int Guide() {
    guides=0;
    Guard::armed=true;
    const int returned=Guard::guarded(&CallGuide,nullptr);
    Guard::armed=false;
    return returned ? guides : -1;
}
// Factor_PlayerAreaIn's call (0x221687) as 0x2214B0 makes it: rbp `object`, the four rows' weak_ptrs.
std::uint32_t Area(void* object) {
    crew::Shared four[4];
    for(int i=0;i<4;++i)four[i]={crew::At<void*>(bvm,0x170+0x18*static_cast<std::size_t>(i)),crew::At<void*>(bvm,0x178+0x18*static_cast<std::size_t>(i))};
    std::int32_t mode=0;
    AreaArgs args{object,CallLanding(0x221687),nullptr,nullptr,four,4,0,&mode};
    areaCount=0;
    return (areaCall(&args)&0xFF) ? areaCount : 0;
}
}  // namespace Readers

struct CreateCall { int index; crew::Shared* out; };
void CallCreate(void* arg) {
    auto* const call=static_cast<CreateCall*>(arg);
    alignas(16) float matrix[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    alignas(16) unsigned char list[0x18]{};
    reinterpret_cast<UpperFn>(crew::image+0x22AB90)(LoopFixture::bvm,call->out,matrix,call->index,-1,1,0,false,0,list);
}
// The real 0x22AB90 for mission player `index`: true when it returned, `out` its weak_ptr; false on an access violation.
bool CreateOne(int index,crew::Shared& out) {
    out={reinterpret_cast<void*>(1),reinterpret_cast<void*>(1)};
    CreateCall call{index,&out};
    Guard::armed=true;
    const int returned=Guard::guarded(&CallCreate,&call);
    Guard::armed=false;
    return returned!=0;
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
        Check(Matches(kNormalizeAt,kNormalizeCode,sizeof(kNormalizeCode)) && Matches(kLayoutAt,kLayoutCode,sizeof(kLayoutCode)) &&
              Matches(kRowAt,kRowCode,sizeof(kRowCode)) && Matches(kRowIndexAt,kRowIndexCode,sizeof(kRowIndexCode)) &&
              Matches(kObjectAt,kObjectCode,sizeof(kObjectCode)),"the loop's four sites and the object call untouched");
        Check(Matches(kGuideAt,kGuideCode,sizeof(kGuideCode)) && Matches(kFirstAt,kFirstCode,sizeof(kFirstCode)) &&
              CallsTo(kGetCall,kGet) && CallsTo(kWaitCall,kWait) && CallsTo(kSetCall,kSet) && CallsTo(kFlag11Call,kFlag11) &&
              CallsTo(kFlag7Call,kFlag7) && CallsTo(kAreaCall,kAreaInit) && CallsTo(kRowsGoneCall,kVectorDtor),
              "the table's readers and the rows' destructor call untouched");
        Check(Logged("HOOK edf5 online=0"),"the refusal logged");
        std::printf("edf5_online_native_test: %d checks passed (foreign)\n",checks);
        return 0;
    }
    if(mode=="rollback") {   // the last write (the creation's entry) fails: every site written before it put back, the page freed
        static std::vector<unsigned char> before;
        before.assign(image+0x200000,image+0x230000);
        static int refused=0;
        static unsigned char* page=nullptr;
        writeCode=[](unsigned rva,const unsigned char* bytes,std::size_t size) noexcept -> bool {
            if(rva==kCreateOnline && !refused++)return false;   // the first write of the entry: refused
            if((bytes[0]==0xE8 || bytes[0]==0xE9) && size>=5 && std::memcmp(image+rva,bytes,size)) {
                std::int32_t rel=0;std::memcpy(&rel,bytes+1,4);
                unsigned char* const to=image+rva+5+rel;
                if(to<image || to>=image+0x22CE000)page=reinterpret_cast<unsigned char*>(reinterpret_cast<std::uintptr_t>(to)&~std::uintptr_t{0xFFF});
            }
            return WriteImage(rva,bytes,size);
        };
        Check(!InstallEdf5Online() && !Edf5OnlineReady(),"a write refused: not installed");
        Check(refused==1 && page!=nullptr,"refused at the creation's entry, after every other site was written to the page");
        Check(!std::memcmp(before.data(),image+0x200000,before.size()),"every byte of EDF+0x200000..0x230000 as before");
        MEMORY_BASIC_INFORMATION m{};
        Check(VirtualQuery(page,&m,sizeof(m))==sizeof(m) && m.State==MEM_FREE,"the stubs' page given back");
        Check(Logged("every site written put back"),"the rollback logged");
        std::printf("edf5_online_native_test: %d checks passed (rollback)\n",checks);
        return 0;
    }

    // The stock natives: native 0x10 calls the BVM preload, whose online half is an empty list; CreatePlayer's too.
    Check(CallLanding(kPreloadCall)==image+kBvmPreload,"stock: native 0x10 calls 0x225E30");
    Check(image[0x225EA9]==0xE8 && CallLanding(0x225EA9)==image+0x20C5B0 && image[0x225EA3]==0x33 && image[0x225EA0]==0x45,
          "stock: the BVM preload's online list is assign(nullptr, nullptr)");
    Check(image[0x22B399]==0xE8 && CallLanding(0x22B399)==image+0x20C5B0 && image[0x22B39F]==0xE9,
          "stock: CreatePlayer's online branch assigns an empty list and leaves");

    // The session fixture, the game status online, before anything runs.
    alignas(16) static unsigned char status[0x15000]{};
    unsigned char* const statusPtr=status;
    DWORD old=0;VirtualProtect(image+kGameStatus,8,PAGE_READWRITE,&old);
    std::memcpy(image+kGameStatus,&statusPtr,8);
    DWORD oldSession=0;VirtualProtect(image+kSessionBase,8,PAGE_READWRITE,&oldSession);
    SessionFixture::Install(image+kSessionBase);
    SessionFixture::Online(status,true);
    freeFn=&SessionFixture::Free;
    Put<std::uint32_t>(status,kLocalPlayers,1);
    {
        const unsigned char* jmp=image+0x12DA7AA;
        std::int32_t rel=0;std::memcpy(&rel,jmp+2,4);
        Check(jmp[0]==0xFF && jmp[1]==0x25,"0x12DA7AA: the __RTDynamicCast import's jump");
        void* const cast=reinterpret_cast<void*>(&NullCast);
        LoopFixture::Write(image+0x12DA7AA+6+rel,reinterpret_cast<const unsigned char*>(&cast),8);
    }

    // The stock loop, run for real: four players, then six.
    using namespace LoopFixture;
    Check(LoopFixture::Install() && Guard::Install(),"the loop's calls on the fixture (stock call sites)");
    Result stock4=Loop(4);
    Check(stock4.guardKept && creations==4 && grounds==4 && groupPushes==4 && ListCount()==4,"stock, four: four created");
    Made baseline[4];std::memcpy(baseline,made,sizeof(baseline));
    float finished[16];   // the loop's four finished offsets: each player's ground point less the spawn point
    for(int i=0;i<4;++i) {
        Check(made[i].pad==i && made[i].split==4,"stock, four: pads 0..3, split by the count");
        Check(RowObject(i)==bodies[i] && RowCtrl(i)==&ctrls[i] && RowIndex(i)==i,"stock, four: rows 0..3 the players");
        Check(SameFloats(made[i].ground,made[i].matrixPos,3),"the creation's matrix stands where the ground was asked");
        for(int k=0;k<4;++k)finished[i*4+k]=made[i].ground[k]-kSpawn[12+k];
        Check(made[i].ground[1]==kSpawn[13],"y: the spawn point's (the offset's is 0)");
    }
    Check(finished[0]==0 && finished[2]==0,"player 1 on the spawn point");
    Check(std::fabs(std::sqrt(finished[4]*finished[4]+finished[6]*finished[6])-1.0f)<1e-5f,"player 2 a unit from it");
    Check(ctrls[0].weaks==3 && ctrls[0].uses==1,"a player: its weak_ptr in its row and the object list, its owner's");
    Result stock6=Loop(6);
    Check(!stock6.guardKept && stock6.firstWritten==0x1B0,"stock, six: the offsets' fifth written over the stack cookie");
    Check(RowIndex(4)!=0 || crew::At<const void*>(bvm,0x1D0)!=listData,"stock, six: a fifth row written over the object list");

    // 0x22AB90 for an index the game makes no object for: stock, it reads the null record (0x22AC3F).
    Check(LoopFixture::Redirect(kObjectCall,kOnlineObject,reinterpret_cast<void*>(&NoOnlineObject)),"0x591130 made null");
    Shared faulted{};
    Check(!CreateOne(5,faulted),"stock: 0x22AB90 faults on 0x591130's null");
    LoopFixture::Write(image+kObjectAt,kObjectCode,sizeof(kObjectCode));
    Check(Matches(kObjectAt,kObjectCode,sizeof(kObjectCode)),"0x591130's call back as it was");
    Fresh();

    // EDF6Coop loads first: its BvmPlayerTableHooks in place, in its own jump shape (edf5online_room.h CoopJumpShape).
    Check(CoopEmu::Install(),"EDF6Coop's four BvmPlayerTableHooks (emulated)");
    for(const auto& site:bvmroom::kCoopSites)Check(bvmroom::CoopJumpShape(image+site.rva,site.size),"EDF6Coop's jump shape at its site");
    Check(InstallEdf5Online() && Edf5OnlineReady(),"installed on the supported build, beside EDF6Coop's hooks");
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
    // The loop's four sites jump out (jmp rel32, the rest int3), the object call to the plugin.
    struct { unsigned rva; std::size_t size; } jumped[]={{kNormalizeAt,sizeof(kNormalizeCode)},{kLayoutAt,sizeof(kLayoutCode)},
                                                         {kRowAt,sizeof(kRowCode)},{kRowIndexAt,sizeof(kRowIndexCode)}};
    for(const auto& site:jumped) {
        bool rest=true;
        for(std::size_t i=5;i<site.size;++i)rest=rest && image[site.rva+i]==0xCC;
        Check(image[site.rva]==0xE9 && rest,"a loop site: jmp to its stub");
    }
    Check(CallLanding(kObjectCall)==reinterpret_cast<unsigned char*>(&Edf5BvmOnlineObject),"0x22AC2F calls Edf5BvmOnlineObject");
    for(const auto& site:{std::pair<unsigned,std::size_t>{kGuideAt,sizeof(kGuideCode)},{kFirstAt,sizeof(kFirstCode)}}) {
        bool rest=true;
        for(std::size_t i=5;i<site.second;++i)rest=rest && image[site.first+i]==0xCC;
        Check(image[site.first]==0xE9 && rest,"a reader's site: jmp to its stub");
    }
    Check(CallLanding(kGetCall)==reinterpret_cast<unsigned char*>(&GetHook) && CallLanding(kWaitCall)==reinterpret_cast<unsigned char*>(&WaitHook) &&
          CallLanding(kSetCall)==reinterpret_cast<unsigned char*>(&SetHook) &&
          CallLanding(kFlag11Call)==reinterpret_cast<unsigned char*>(&FlagHook<kFlag11>) &&
          CallLanding(kFlag7Call)==reinterpret_cast<unsigned char*>(&FlagHook<kFlag7>) &&
          CallLanding(kRowsGoneCall)==reinterpret_cast<unsigned char*>(&RowsGoneHook),"the natives' calls and the rows' destructor to the plugin");
    for(const auto& site:bvmroom::kCoopSites)Check(bvmroom::CoopJumpShape(image+site.rva,site.size),"EDF6Coop's hooks kept");
    Check(onlineObject==reinterpret_cast<OnlineObjectFn>(image+kOnlineObject),"which calls 0x591130");

    // The patched loop, four players: everything as the stock loop's (the stubs run the loop's own instructions).
    Result four=Loop(4);
    Check(four.guardKept && creations==4 && ListCount()==4 && groupPushes==4,"patched, four: four created");
    for(int i=0;i<4;++i) {
        Check(!std::memcmp(&made[i],&baseline[i],sizeof(Made)),"patched, four: each player's ground, matrix, pad, split as stock");
        Check(RowObject(i)==bodies[i] && RowIndex(i)==i && ctrls[i].weaks==3,"patched, four: rows 0..3 as stock");
    }
    // Eight: the first four as stock; 5..8 along players 2-4's directions twice as far, then player 2's three times;
    // none in a row, all in the object list and the group; nothing past the loop's frame written.
    Result eight=Loop(8);
    Check(eight.guardKept,"patched, eight: the stack cookie and the frame above it untouched");
    Check(creations==8 && grounds==8 && ListCount()==8 && groupPushes==8 && !roomShort,"patched, eight: eight created");
    for(int i=0;i<4;++i)Check(SameFloats(made[i].ground,baseline[i].ground,4) && made[i].pad==i,"patched, eight: 1-4 as stock");
    for(int i=4;i<8;++i) {
        float offset[4];Edf5BvmSpawnOffset(static_cast<std::uint32_t>(i),finished,offset);
        const float want[3]={kSpawn[12]+offset[0],kSpawn[13]+offset[1],kSpawn[14]+offset[2]};
        Check(SameFloats(made[i].ground,want,3),"patched, eight: 5-8 at the layout's offsets from the spawn point");
        Check(made[i].pad==-1 && made[i].split==8,"patched, eight: 5-8 no pad of the loop's own");
        Check(groupObjects[i]==bodies[i] && listData[i].obj==bodies[i],"patched, eight: 5-8 in the group and the object list");
        Check(ctrls[i].weaks==3 && ctrls[i].uses==1,"patched, eight: 5-8 held by the object list and the plugin's row");
        const auto* row=static_cast<const Row*>(Edf5BvmRowAt(bvm,static_cast<std::uint64_t>(i)));
        Check(row && row->obj==bodies[i] && row->ctrl==&ctrls[i] && row->index==i,"patched, eight: 5-8 in the plugin's rows");
    }
    {
        const float* p5=made[4].ground;const float* p2=made[1].ground;
        Check(std::fabs((p5[0]-kSpawn[12])-2*(p2[0]-kSpawn[12]))<1e-5f && std::fabs((p5[2]-kSpawn[14])-2*(p2[2]-kSpawn[14]))<1e-5f,
              "player 5 along player 2's direction, twice as far");
        const float* p8=made[7].ground;
        Check(std::fabs((p8[0]-kSpawn[12])-3*(p2[0]-kSpawn[12]))<1e-5f,"player 8 along player 2's, three times");
    }
    for(int i=0;i<4;++i)Check(RowObject(i)==bodies[i] && RowIndex(i)==i,"patched, eight: rows 0..3 players 1-4");
    Check(crew::At<std::uint64_t>(bvm,0x1C8)==0 && crew::At<const void*>(bvm,0x1D0)==listData,
          "patched, eight: nothing written past the four rows (the object list's own fields)");
    // Forty: 33.. on a ring around player 1, one spacing (players 1-2's) apart, at 12 spacings.
    Result forty=Loop(40);
    Check(forty.guardKept && creations==40 && ListCount()==40 && !roomShort,"patched, forty: forty created");
    for(int i=32;i<40;++i) {
        const float dx=made[i].ground[0]-kSpawn[12],dz=made[i].ground[2]-kSpawn[14];
        Check(std::fabs(std::sqrt(dx*dx+dz*dz)-12.0f)<1e-4f,"patched, forty: 33.. on the first ring");
    }
    for(int i=1;i<40;++i)
        for(int j=0;j<i;++j) {
            const float ex=made[i].ground[0]-made[j].ground[0],ez=made[i].ground[2]-made[j].ground[2];
            Check(ex*ex+ez*ez>0.25f,"patched, forty: nobody on anybody");
        }

    // The script's player table read by its natives, for real: six players (rows 0..3 the game's, 4..5 the plugin's),
    // EDF6Coop's four bounds in place.
    Check(Readers::Install(),"the readers' calls on the fixture");
    Put<std::uint32_t>(status,kSessionPlayers,6);
    {
        using namespace Readers;
        Result six=Loop(6);
        Check(six.guardKept && creations==6,"readers, six: six created");
        Dress();
        const auto get=reinterpret_cast<GetFn>(CallLanding(kGetCall));
        const auto set=reinterpret_cast<SetFn>(CallLanding(kSetCall));
        const auto wait=reinterpret_cast<WaitFn>(CallLanding(kWaitCall));
        const auto flag11=reinterpret_cast<FlagFn>(CallLanding(kFlag11Call));
        const auto flag7=reinterpret_cast<FlagFn>(CallLanding(kFlag7Call));
        // Native 0x3ED (0x222740): the player's vf 0x298 by the script's index. EDF6Coop alone: 5.. empty.
        long bounded=CoopEmu::bounded;
        Check(reinterpret_cast<GetFn>(image+kGet)(bvm,4)==0 && reinterpret_cast<GetFn>(image+kGet)(bvm,5)==0 &&
              CoopEmu::bounded==bounded+2,"stock with EDF6Coop: players 5-6 read as empty (Coop's bound)");
        bounded=CoopEmu::bounded;
        for(int i=0;i<6;++i)Check(get(bvm,i)==100+i,"native 0x3ED: every one of six players");
        Check(CoopEmu::bounded==bounded,"native 0x3ED: players 5-6 read from the plugin's rows, past EDF6Coop's bound");
        Check(get(bvm,6)==0 && get(bvm,-1)==0 && CoopEmu::bounded==bounded+2,"native 0x3ED: an index no player has, empty (Coop's bound)");
        Check(ctrls[5].uses==1 && ctrls[5].weaks==3,"native 0x3ED: the player's lock let go");
        // Native 0x3EF (0x228AC0): the player's +0x158C.
        set(bvm,5,77);set(bvm,0,55);
        Check(At<std::int32_t>(bodies[5],0x158C)==77 && At<std::int32_t>(bodies[0],0x158C)==55,"native 0x3EF: players 6 and 1");
        // Native 0x3EE (0x22A650): wait until the player's +0x1588 has the mask (the process's +0x68 set: wait).
        Put<std::int32_t>(bodies[5],0x1588,4);
        process[0x68]=0;wait(bvm,5,4,1);
        Check(process[0x68]==0,"native 0x3EE: player 6 has the flag, no wait");
        process[0x68]=0;wait(bvm,5,8,1);
        Check(process[0x68]==1,"native 0x3EE: player 6 lacks it, the script waits");
        Put<std::int32_t>(bodies[2],0x1588,8);
        process[0x68]=0;wait(bvm,2,8,1);
        Check(process[0x68]==0,"native 0x3EE: player 3 (the game's row) as before");
        // 0x2252D0 / 0x2253A0: every player's +0x380 bit 11 / bit 7.
        for(auto& b:bodies)Put<std::uint32_t>(b,0x380,0);
        reinterpret_cast<FlagFn>(image+kFlag11)(bvm,true);
        Check(Flag(3,11) && !Flag(4,11) && !Flag(5,11),"stock 0x2252D0: players 1-4 only");
        flag11(bvm,true);flag7(bvm,true);
        for(int i=0;i<6;++i)Check(Flag(i,11) && Flag(i,7),"0x2252D0 / 0x2253A0: every one of six players");
        Check(!Flag(6,11) && !Flag(6,7),"and nobody past them");
        flag11(bvm,false);
        for(int i=0;i<6;++i)Check(!Flag(i,11) && Flag(i,7),"0x2252D0 off: every one of six, bit 7 kept");
        // Factor_PlayerAreaIn (0x221687 -> 0x207870): every player's weak_ptr handed to Initialize.
        Check(Area(bvm)==6,"Factor_PlayerAreaIn: six players handed to Initialize");
        for(int i=0;i<6;++i)Check(areaCopy[i].obj==bodies[i] && areaCopy[i].ctrl==&ctrls[i],"Factor_PlayerAreaIn: each player's");
        Check(Area(process)==4,"Factor_PlayerAreaIn of an object without the plugin's rows: the game's four, untouched");
        // Native 1200 (0x21F380): a route guide for each of this machine's players.
        bounded=CoopEmu::bounded;
        Check(Guide()==6,"the route guide: one for each of six players");
        for(int i=0;i<6;++i)Check(guided[i]==bodies[i],"the route guide: each player's own, in order");
        Check(CoopEmu::bounded==bounded,"the route guide: players 5-6 past EDF6Coop's bound");
        bodies[4][0x128]=1;   // player 5 a remote one (+0x128 bit 0): no guide here
        Check(Guide()==5 && guided[4]==bodies[5],"the route guide: a remote player 5 skipped, player 6 guided");
        bodies[4][0x128]=0;
        // 0x229040: the first player there.
        Check(First()==bodies[0],"0x229040: player 1 first");
        for(int i=0;i<4;++i){Put<void*>(bvm,0x170+0x18*static_cast<std::size_t>(i),nullptr);Put<void*>(bvm,0x178+0x18*static_cast<std::size_t>(i),nullptr);}
        Check(First()==bodies[4],"0x229040: players 1-4 gone, player 5 found in the plugin's rows");
        // The object's rows go (0x20EB81): the game's, then the plugin's; the weak references back.
        const auto gone=reinterpret_cast<RowsGoneFn>(CallLanding(kRowsGoneCall));
        gone(bvm+kRows,0x18,4,image+0x20ECE0);
        Check(!Edf5BvmRowAt(bvm,4) && ctrls[4].weaks==2 && ctrls[5].weaks==2,"the rows' destructor: the plugin's rows let go");
        Check(First()==kNobody,"0x229040: nobody once they are gone");
        bounded=CoopEmu::bounded;
        Check(Guide()==0 && CoopEmu::bounded==bounded+2,"the route guide without rows: nobody, 5-6 bounded by EDF6Coop");
    }
    // Forty: every reader for every one of them.
    Put<std::uint32_t>(status,kSessionPlayers,40);
    {
        using namespace Readers;
        Result many=Loop(40);
        Check(many.guardKept && creations==40,"readers, forty: forty created");
        Dress();
        const auto get=reinterpret_cast<GetFn>(CallLanding(kGetCall));
        const auto flag7=reinterpret_cast<FlagFn>(CallLanding(kFlag7Call));
        for(int i=0;i<40;++i)Check(get(bvm,i)==100+i,"forty: native 0x3ED for each");
        for(auto& b:bodies)Put<std::uint32_t>(b,0x380,0);
        flag7(bvm,true);
        for(int i=0;i<40;++i)Check(Flag(i,7),"forty: 0x2253A0 on each");
        Check(!Flag(40,7),"forty: nobody past them");
        Check(Area(bvm)==40 && areaCopy[39].obj==bodies[39],"forty: Factor_PlayerAreaIn with all forty");
        Check(Guide()==40 && guided[39]==bodies[39] && guided[33]==bodies[33],"forty: a route guide for each");
    }
    Put<std::uint32_t>(status,kSessionPlayers,2);
    Fresh();

    // 0x22AB90 for an index the game makes no object for, patched: an empty player, as AngelScript's (0x1DC544).
    onlineObject=&NoOnlineObject;
    lines.clear();
    Shared empty{};
    Check(CreateOne(5,empty),"patched: 0x22AB90 returns where 0x591130 made nothing");
    Check(!empty.obj && !empty.ctrl,"an empty player (0x22AEF2)");
    Check(Logged("no player object for mission player 5"),"said");
    onlineObject=reinterpret_cast<OnlineObjectFn>(image+kOnlineObject);

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
    // The list's order is not the mission's: player i is the user whose +0x48 is i (0x5A35D0, 0x734705).
    SessionFixture::Users({SessionFixture::Entry{false,1},SessionFixture::Entry{true,0}});
    Check(Edf5BvmOnlinePlayers()==2,"a list in another order");
    Check(Edf5BvmOnlinePlayerArgs(0,&pad,&split) && pad==-1,"player 0 the remote user listed second");
    Edf5BvmOnlinePlayerMade(0,nullptr);
    Check(Edf5BvmOnlinePlayerArgs(1,&pad,&split) && pad==0,"player 1 this machine's, listed first");
    Edf5BvmOnlinePlayerMade(1,SessionFixture::Player(false));
    // Someone left: three in the count, nobody at index 1 (a hole, as 0x734670 leaves it): remote, no pad.
    SessionFixture::Users({SessionFixture::Entry{false,0},SessionFixture::Entry{true,2}});
    Put<std::uint32_t>(status,kSessionPlayers,3);
    Check(Edf5BvmOnlinePlayers()==3,"a hole in the list: still created");
    Check(Edf5BvmOnlinePlayerArgs(0,&pad,&split) && pad==0,"this machine's at 0");
    Edf5BvmOnlinePlayerMade(0,SessionFixture::Player(false));
    Check(Edf5BvmOnlinePlayerArgs(1,&pad,&split) && pad==-1,"nobody at 1: no pad");
    Edf5BvmOnlinePlayerMade(1,nullptr);
    Check(Edf5BvmOnlinePlayerArgs(2,&pad,&split) && pad==-1,"the remote at 2");
    Edf5BvmOnlinePlayerMade(2,nullptr);
    // Two local players (split screen online) of three: pads 0 and 1 as each is made, split by two.
    Put<std::uint32_t>(status,kLocalPlayers,2);
    SessionFixture::Users({false,true,false});
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

    // A loop left early (0x22B5EC) stays armed: an offline creation after it gets none of its arguments, and the next
    // mission start clears it.
    SessionFixture::Users({true,false});
    Put<std::uint32_t>(status,kSessionPlayers,2);
    Check(Edf5BvmOnlinePlayers()==2,"armed for two");
    Edf5BvmOnlinePlayerMade(0,nullptr);   // index 1 never reached
    SessionFixture::Online(status,false);
    Check(!Edf5BvmOnlinePlayerArgs(1,&pad,&split),"offline: the left-over online arguments never apply");
    SessionFixture::Online(status,true);
    Check(!Edf5BvmOnlinePlayerArgs(1,&pad,&split),"and they were dropped, online again or not");
    Check(Edf5BvmOnlinePlayers()==2,"armed again");
    Edf5BvmOnlinePlayerMade(0,nullptr);
    ResetEdf5Online();
    Check(!Edf5BvmOnlinePlayerArgs(1,&pad,&split),"a mission start clears an armed loop");

    // A big list (0x1000 bytes and over): its storage given back as the game does, the real block 8 bytes before.
    SessionFixture::Users({false,true});
    SessionFixture::padTo=300;
    Check(Edf5BvmOnlinePlayers()==2 && SessionFixture::Released(),"a big list's block and size given back right");
    Edf5BvmOnlinePlayerMade(0,nullptr);Edf5BvmOnlinePlayerMade(1,nullptr);
    SessionFixture::padTo=0;

    SessionFixture::Users({false,true,true,true});
    Put<std::uint32_t>(status,kSessionPlayers,4);
    lines.clear();
    Check(Edf5BvmOnlinePlayers()==4 && !Logged("EDF5 online:"),"four: all of them, nothing logged");
    SessionFixture::Users({false,true,true,true,true,true});
    Put<std::uint32_t>(status,kSessionPlayers,6);
    Check(Edf5BvmOnlinePlayers()==6,"six: all six (2026-10-18: the fifth on had none)");
    Check(Logged("6 players, past the script loop's 4"),"past four said");
    Check(Edf5BvmOnlinePlayerArgs(4,&pad,&split) && pad==-1,"the fifth: its arguments (remote, no pad)");
    lines.clear();
    Check(Edf5BvmOnlinePlayers()==6 && !Logged("EDF5 online:"),"once, not every call");
    // Past four only where every machine creates them: a member that did not say so, or EDF6Coop's hooks missing here,
    // and every machine creates four, said each time.
    roomAllows=false;lines.clear();
    Check(Edf5BvmOnlinePlayers()==4 && Logged("but mission player 6 did not tell the room: every machine of the room creates players 1-4 only"),
          "a member without the word: four, and why");
    Check(!Edf5BvmOnlinePlayerArgs(4,&pad,&split),"and the fifth not armed");
    roomAllows=true;coopHooked=false;lines.clear();
    Check(Edf5BvmOnlinePlayers()==4 && Logged("BvmPlayerTableHooks are not in place on this machine"),"EDF6Coop's hooks missing here: four, and why");
    SessionFixture::Users({true,true,true,true,true,false});
    lines.clear();
    Check(Edf5BvmOnlinePlayers()==4 && Logged("this machine's player is past the 4 a mission creates"),"this machine the sixth: it has none, said");
    coopHooked=true;
    SessionFixture::Users({false,true,true,true,true,true});
    lines.clear();
    Check(Edf5BvmOnlinePlayers()==6 && Logged("6 players, past the script loop's 4"),"both again: six, said again");
    ResetEdf5Online();
    // A room of eight seen from the machine whose player is the seventh: the real loop, the participant gate's way.
    SessionFixture::Users({true,true,true,true,true,true,false,true});
    Put<std::uint32_t>(status,kSessionPlayers,8);
    Check(Edf5BvmOnlinePlayers()==8,"eight: all eight");
    gate=true;
    Result room=Loop(8);
    gate=false;
    Check(room.guardKept && creations==8,"the room of eight: eight created");
    for(int i=0;i<8;++i)
        Check(made[i].online && made[i].onlinePad==(i==6 ? 0 : -1) && made[i].onlineSplit==1,
              "the room of eight: this machine's seventh on pad 0, the whole screen; the rest remote");
    Check(!Edf5BvmOnlinePlayerArgs(7,&pad,&split),"the loop's last player disarms");
    // Past the most a session holds (EDF6Coop's 1024): that many, said.
    Put<std::uint32_t>(status,kSessionPlayers,kBvmPlayerCapacity+1);
    lines.clear();clampLogged=false;
    Check(Edf5BvmOnlinePlayers()==kBvmPlayerCapacity && Logged("an EDF5 mission creates 1024"),"past 1024: 1024, said");
    ResetEdf5Online();
    SessionFixture::Users({false,true,true});
    Put<std::uint32_t>(status,kSessionPlayers,3);
    Check(Edf5BvmOnlinePlayers()==3,"back to three");
    SessionFixture::Users({true,true});
    Put<std::uint32_t>(status,kSessionPlayers,2);
    lines.clear();
    Check(Edf5BvmOnlinePlayers()==2 && Logged("no user of this machine among the session's 2"),
          "no user of this machine in the list: said, not a silent black screen");
    ResetEdf5Online();

    // Nothing created when the per-player arguments cannot be put right.
    gateReady=false;
    Check(Edf5BvmOnlinePlayers()==0 && Logged("mission_participant_gate) is not installed"),
          "no participant gate: nobody (no wrong pads, no split screen)");
    gateReady=true;
    SessionFixture::Uninstall(image+kSessionBase);
    Check(Edf5BvmOnlinePlayers()==0 && Logged("users unreadable"),"no session object: nobody");
    Check(!Edf5BvmOnlinePlayerArgs(0,&pad,&split),"and nothing armed");
    VirtualProtect(image+kSessionBase,8,oldSession,&oldSession);
    const unsigned char* const none=nullptr;
    std::memcpy(image+kGameStatus,&none,8);
    Check(Edf5BvmOnlinePlayers()==0 && Logged("no game status"),"no game status: nobody");
    VirtualProtect(image+kGameStatus,8,old,&old);
    std::printf("edf5_online_native_test: %d checks passed\n",checks);
    return 0;
}
