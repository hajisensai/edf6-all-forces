// The offline HQ's mission pack switch: Q / E (a pad's LB / RB) on the HQ menu go to the previous / next mission pack
// (an offline story mode the mission-pack dialog would let the player pick), no title, slot and mode select on the
// way. EDF.dll TimeDateStamp 0x678CCB46, RVAs; the RE is docs/pack-switch-re.md, the pure side src/pack_switch_logic.h.
//
//  1. HUiHQMain::OnUpdate (vtable 0x1804DD8 slot 1, chained before room_ui.cpp so the ctx it hands on is the one the
//     HQ sees: none while 整备 is open over it), on the UI thread: while the HQ itself owns the input (0x8DAD00 with
//     its ctx, nothing of the game's over it, 整备 closed), a key going down picks the mode (Step) and the HQ is closed
//     with exit code 10 (Hui_HQMain_Return_GotoTitle) the way its 回到标题 dialog's Yes does (ResultCallback
//     set_close_exit 10: 0x7E3180(10), 0x84FA62). MAINSCRIPT's HQMain then saves everything (SaveAllData_ReturnTitle)
//     and returns "StartToTitle".
//  2. createCoRoutine (0x12946F0, MAINSCRIPT's main() starting each screen's routine by its declaration), on the
//     script's thread: that "string StartToTitle()" becomes "string AF_PackSwitch<mode>()", compiled once into the
//     same module (asCModule::CompileFunction, vtable slot 6, asCOMP_ADD_TO_MODULE) as createCoRoutine itself finds
//     routines in (the active context's function's module). It calls the script's ApplyModeNo(mode), the lobby's
//     switch of mode inside a session (SetMode, the mode's save loaded, UnlockDownloadContents: the packs' save
//     migration runs as from the mode select, airstrike.cpp), then "HQMain": a new HQ of the new pack (its mission
//     select opened by room_ui.cpp as at every HQ).
// Online the pack is the room's (the room is made with it): this is the offline HQ only. Nothing is patched unless
// every address checks out (REFUSED in the log: the HQ as it is).
#include "edf_layout.h"
#include "config.h"
#include "plugin_log.h"
#include "campaign.h"
#include "memory.h"
#include "map.h"
#include "armory.h"
#include "pack_switch_logic.h"
#include "edf/input_source.h"
#include "edf/patch.h"
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>

namespace crew {
namespace {
using namespace packswitch;

// --- The game's side ---
constexpr unsigned kGameStatus=0x20B2890;   // GameDataMgr*: modes at +0x20 (count u32 +0x30), the current index +0x38
constexpr std::size_t kModes=0x20,kModeCount=0x30,kModeNow=0x38,kModeNode=0x10;
constexpr std::size_t kItemOnline=0x68,kItemContent=0x74,kItemType=0x80;   // a mode's ModeList items 8, 9, 10
constexpr unsigned kHqSlot=0x1804DE0,kHqUpdate=0x89F740;   // HUiHQMain vtable 0x1804DD8 slot 1 (OnUpdate)
constexpr unsigned kGetInput=0x8DAD00;      // HUiBase::GetInput(this, ctx): ctx+8, nullptr while covered / in a transition
constexpr unsigned kCloseExit=0x7E3180;     // (int code): the UI exit code GetUiExitCode reads (0x20B39D8), the close request
constexpr int kGotoTitle=10;                // Hui_HQMain_Return_GotoTitle (UI_COMMON_CONST.H; LYT_HUIHQMAIN.SGO's Yes)
constexpr unsigned kCreateCoRoutine=0x12946F0,kActiveContext=0x11E5290;   // createCoRoutine(out, const wstring*), asGetActiveContext
constexpr unsigned kModuleVtable=0x1AEE1F8; // asCModule (RTTI .?AVasCModule@@)
constexpr unsigned kCompileFunction=0x1233650,kFunctionByDecl=0x1233B90;   // its slots 6 and 13 (AngelScript 2.28.2)
constexpr std::size_t kCtxGetFunction=0x148,kFuncGetModule=0x30,kModCompile=6,kModByDecl=13;
constexpr unsigned kAddToModule=1;          // asCOMP_ADD_TO_MODULE

// HUiBase::GetInput: mov [rsp+8],rbx; push rdi; sub rsp,20h; cmp qword [rcx+0D8h],0 (room_ui.cpp's).
const unsigned char kGetInputSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x48,0x83,0xB9,0xD8,0x00,0x00};
// HUiHQMain::OnUpdate: push rbx; sub rsp,30h; mov rbx,rcx; call 0x84B490 (room_ui.cpp's).
const unsigned char kHqUpdateSig[]={0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0xD9,0xE8,0x42,0xBD,0xFA,0xFF,0x48,0x8D};
// 0x7E3180: sub rsp,28h; mov [0x20B39D8],ecx; mov rcx,[UI system]; mov dword [rsp+30h],2 (the message: close).
const unsigned char kCloseExitSig[]={0x48,0x83,0xEC,0x28,0x89,0x0D,0x4E,0x08,0x8D,0x01,0x48,0x8B,0x0D,0xCF,0xF7,0x8C,0x01,
    0xC7,0x44,0x24,0x30,0x02,0x00,0x00,0x00};
// createCoRoutine's head (mov [rsp+18h],rbx; push rbp/rsi/rdi/r12..r15; lea rbp,[rsp-27h]): tests/autopilot's sig.
const unsigned char kCoRoutineSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,
    0x8D,0x6C,0x24,0xD9};
// 0x1294724..0x129477F: call asGetActiveContext; ...; ctx->GetFunction(0) (+148h); f->GetModule() (+30h); the decl
// narrowed (0x40890); module->GetFunctionByDecl (+68h): the module createCoRoutine finds routines in.
const unsigned char kCoRoutineLookupSig[]={0x48,0x8B,0x00,0x33,0xD2,0x49,0x8B,0xCF,0xFF,0x90,0x48,0x01,0x00,0x00,0x4C,0x8B,
    0x00,0x48,0x8B,0xC8,0x41,0xFF,0x50,0x30};
const unsigned char kCoRoutineActiveSig[]={0xE8,0x67,0x0B,0xF5,0xFF};   // 0x1294724: call 0x11E5290
const unsigned char kCoRoutineByDeclSig[]={0x48,0x8B,0xCB,0xFF,0x50,0x68};   // 0x129477C: mov rcx,rbx; call [rax+68h]
// asGetActiveContext: sub rsp,28h; call the thread's data; the active contexts' last.
const unsigned char kActiveContextSig[]={0x48,0x83,0xEC,0x28,0xE8,0xE7,0x4A,0x01,0x00,0x48,0x8B,0x48,0x08,0x48,0x85,0xC9};
// asCModule::CompileFunction: its saves, sub rsp,280h; mov r14d,r9d (lineOffset); compileFlags > 1 refused (0x123369B).
const unsigned char kCompileSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,
    0x56,0x41,0x57,0x48,0x81,0xEC,0x80,0x02,0x00,0x00,0x45,0x8B,0xF1};
const unsigned char kCompileFlagsSig[]={0x83,0xFE,0x01,0x0F,0x87};   // 0x123369B: cmp esi,1; ja (asINVALID_ARG)

struct Sig { unsigned rva; const unsigned char* bytes; std::size_t size; };
const Sig kSigs[]={{kGetInput,kGetInputSig,sizeof(kGetInputSig)},{kHqUpdate,kHqUpdateSig,sizeof(kHqUpdateSig)},
    {kCloseExit,kCloseExitSig,sizeof(kCloseExitSig)},{0x1294743,kCoRoutineLookupSig,sizeof(kCoRoutineLookupSig)},
    {0x1294724,kCoRoutineActiveSig,sizeof(kCoRoutineActiveSig)},{0x129477C,kCoRoutineByDeclSig,sizeof(kCoRoutineByDeclSig)},
    {kActiveContext,kActiveContextSig,sizeof(kActiveContextSig)},{kCompileFunction,kCompileSig,sizeof(kCompileSig)},
    {0x123369B,kCompileFlagsSig,sizeof(kCompileFlagsSig)}};

using UpdateFn=std::uint64_t(__fastcall*)(unsigned char*,void*);
using GetInputFn=void*(__fastcall*)(unsigned char*,void*);
using CloseExitFn=void(__fastcall*)(int);
using CoRoutineFn=void*(__fastcall*)(void*,const std::wstring*);
using ActiveContextFn=void*(*)();
using GetFunctionFn=void*(__fastcall*)(void*,unsigned);
using GetModuleFn=void*(__fastcall*)(void*);
using ByDeclFn=void*(__fastcall*)(void*,const char*);
using CompileFn=int(__fastcall*)(void*,const char*,const char*,int,unsigned,void**);

void* nextHq=nullptr;   // ChainVtableSlot writes it before the slot
CoRoutineFn nextCoRoutine=nullptr;
// The mode the HQ was closed for (UI thread asks, the script's thread takes it at the next routine start).
Pending pending;
Keys was{};   // UI thread only

template<class T> T Read(const unsigned char* p,std::size_t off) noexcept { T v; std::memcpy(&v,p+off,sizeof(T)); return v; }

unsigned char* GameStatus() noexcept {
    return Readable(image+kGameStatus,8) ? Read<unsigned char*>(image,kGameStatus) : nullptr;
}
// One mode's ModeList item at `off` (the values at node + [node+8]: 0x8C4CA0's read).
bool Item(const unsigned char* mode,std::size_t off,std::int32_t& out) noexcept {
    if(!mode || !Readable(mode+kModeNode,8))return false;
    const unsigned char* const node=Read<const unsigned char*>(mode,kModeNode);
    if(!node || !Readable(node+8,4))return false;
    const auto at=Read<std::int32_t>(node,8);
    if(!Readable(node+at+off,4))return false;
    out=Read<std::int32_t>(node+at,off);
    return true;
}
// The modes as Step sees them, and the current one; false when the table cannot be read.
bool ReadModes(unsigned char* gs,ModeView* views,int& count,int& current) noexcept {
    if(!gs || !Readable(gs+kModes,kModeNow+4-kModes))return false;
    const auto n=Read<std::uint32_t>(gs,kModeCount);
    unsigned char* const* const modes=Read<unsigned char* const*>(gs,kModes);
    if(n==0 || n>static_cast<std::uint32_t>(kMaxModes) || !modes || !Readable(modes,n*sizeof(void*)))return false;
    count=static_cast<int>(n);
    current=Read<std::int32_t>(gs,kModeNow);
    for(int i=0;i<count;++i) {
        std::int32_t online=1,type=1,content=-1;
        const bool ok=Item(modes[i],kItemOnline,online) && Item(modes[i],kItemType,type) && Item(modes[i],kItemContent,content);
        views[i].offlineStory=ok && online==0 && type==0;
        views[i].selectable=views[i].offlineStory && MissionPackSelectable(reinterpret_cast<std::uintptr_t>(gs),content);
    }
    return true;
}
std::int32_t ContentOfMode(unsigned char* gs,int i) noexcept {
    std::int32_t content=-1;
    unsigned char* const* const modes=Read<unsigned char* const*>(gs,kModes);
    if(modes && i>=0 && Readable(modes+i,8))Item(modes[i],kItemContent,content);
    return content;
}

bool InFront() noexcept {
    DWORD pid=0;
    GetWindowThreadProcessId(edf::ForegroundWindow(),&pid);
    return pid==GetCurrentProcessId();
}
Keys ReadKeys() noexcept {
    Keys k;
    if(MapHoldsKeys())return k;
    const bool front=InFront();
    if(front){k.prev=(edf::AsyncKey('Q')&0x8000)!=0;k.next=(edf::AsyncKey('E')&0x8000)!=0;}
    std::uint16_t buttons=0;
    std::uint8_t lt=0,rt=0;
    if((front || edf::PadSource()) && MapPadState(&buttons,&lt,&rt)) {
        k.prev|=(buttons&XINPUT_GAMEPAD_LEFT_SHOULDER)!=0;
        k.next|=(buttons&XINPUT_GAMEPAD_RIGHT_SHOULDER)!=0;
    }
    return k;
}

// The HQ's frame: the stock update first (whatever it does with this frame's input), then the switch when a key went
// down and the HQ owned the input before and after it (a sub-screen it opened this frame: nothing).
std::uint64_t __fastcall HqUpdate(unsigned char* self,void* ctx) {
    const auto getInput=reinterpret_cast<GetInputFn>(image+kGetInput);
    const bool owned=Cfg().enabled && ctx && getInput(self,ctx) && !armoryui::IsOpen();
    const std::uint64_t result=reinterpret_cast<UpdateFn>(nextHq)(self,ctx);
    const Keys now=ReadKeys();
    const int edge=Pressed(was,now);
    const bool ownedAfter=owned && getInput(self,ctx) && !armoryui::IsOpen();
    const int dir=HqFrame(was,now,ownedAfter,pending);
    if(edge && !dir && Cfg().debug)   // a press, not taken: which gate (a key press at a time: no flood)
        Log("PACKSWITCH %s on the HQ %p not taken: %s",edge<0 ? "Q" : "E",static_cast<void*>(self),
            pending.mode.load()>=0 ? "a switch is pending (the script has not started its next routine yet)" :
            !owned ? "the HQ did not own the input before its update (covered, in a transition, 整备 open, or off)" :
                     "the HQ lost the input in its own update (a sub-screen opened this frame)");
    if(!dir)return result;
    unsigned char* const gs=GameStatus();
    ModeView views[kMaxModes]{};
    int count=0,current=-1;
    if(!ReadModes(gs,views,count,current)){Log("PACKSWITCH the mode table cannot be read: not switched");return result;}
    const int to=Step(views,count,current,dir);
    if(to<0){Log("PACKSWITCH %s: no other mission pack to go to (mode %d)",dir<0 ? "Q" : "E",current);return result;}
    if(!Ask(pending,to))return result;   // HqFrame saw none pending, and only this thread asks
    Log("PACKSWITCH %s: mode %d (content %d) -> %d (content %d): the HQ closed as 回到标题 (exit %d, saved by the script)",
        dir<0 ? "Q" : "E",current,ContentOfMode(gs,current),to,ContentOfMode(gs,to),kGotoTitle);
    reinterpret_cast<CloseExitFn>(image+kCloseExit)(kGotoTitle);
    return result;
}

// The routine for `mode` in the module createCoRoutine looks routines up in, compiled there the first time.
bool EnsureRoutine(int mode,const char* decl) noexcept {
    void* const ctx=reinterpret_cast<ActiveContextFn>(image+kActiveContext)();
    if(!ctx || !Readable(ctx,8))return false;
    void** const ctxTable=*static_cast<void***>(ctx);
    if(!Readable(ctxTable,kCtxGetFunction+8))return false;
    void* const fn=reinterpret_cast<GetFunctionFn>(ctxTable[kCtxGetFunction/8])(ctx,0);
    if(!fn || !Readable(fn,8))return false;
    void** const fnTable=*static_cast<void***>(fn);
    if(!Readable(fnTable,kFuncGetModule+8))return false;
    void* const module=reinterpret_cast<GetModuleFn>(fnTable[kFuncGetModule/8])(fn);
    if(!module || !Readable(module,8) || *static_cast<void**>(module)!=image+kModuleVtable) {
        Log("PACKSWITCH the script's module is not an asCModule (%p): not compiled",module);
        return false;
    }
    void** const modTable=*static_cast<void***>(module);
    const auto byDecl=reinterpret_cast<ByDeclFn>(modTable[kModByDecl]);
    if(byDecl(module,decl))return true;
    char code[256];
    if(!Code(mode,code,sizeof(code)))return false;
    const int r=reinterpret_cast<CompileFn>(modTable[kModCompile])(module,"AllForcesPackSwitch",code,0,kAddToModule,nullptr);
    const bool ok=r>=0 && byDecl(module,decl);
    Log("PACKSWITCH %s compiled into the main script: %d (%s)",decl,r,ok ? "found" : "NOT found");
    return ok;
}

void* __fastcall CoRoutineHook(void* out,const std::wstring* decl) {
    const int mode=Take(pending);
    if(mode<0 || !decl)return nextCoRoutine(out,decl);
    if(*decl!=L"string StartToTitle()") {
        Log("PACKSWITCH the HQ went to another routine (%ls): mode %d not switched to",decl->c_str(),mode);
        return nextCoRoutine(out,decl);
    }
    wchar_t wide[64];
    char narrow[64];
    if(!Declaration(mode,wide,64) || !NarrowDeclaration(mode,narrow,sizeof(narrow)) || !EnsureRoutine(mode,narrow)) {
        Log("PACKSWITCH mode %d: no routine, back to the title as the stock HQ",mode);
        return nextCoRoutine(out,decl);
    }
    const std::wstring routine(wide);
    Log("PACKSWITCH StartToTitle -> %s",narrow);
    return nextCoRoutine(out,&routine);
}

// createCoRoutine's head: the stock one (a trampoline of its first instruction), or another plugin's jmp rel32 (the
// test autopilot's, loaded first by name) chained.
bool HookCoRoutine() noexcept {
    unsigned char* const at=image+kCreateCoRoutine;
    if(!Readable(at,sizeof(kCoRoutineSig)))return false;
    void* const thunk=AllocateNearThunk(at,reinterpret_cast<void*>(&CoRoutineHook));
    if(!thunk)return false;
    unsigned char jump[5]={0xE9};
    const auto rel=static_cast<std::int32_t>(static_cast<unsigned char*>(thunk)-(at+5));
    std::memcpy(jump+1,&rel,4);
    if(at[0]==0xE9) {
        std::int32_t prior;
        std::memcpy(&prior,at+1,4);
        unsigned char current[5];
        std::memcpy(current,at,5);
        nextCoRoutine=reinterpret_cast<CoRoutineFn>(at+5+prior);
        Log("PACKSWITCH createCoRoutine already redirected (another plugin, %p): chained",reinterpret_cast<void*>(nextCoRoutine));
        if(edf::PatchCode(at,current,jump,sizeof(jump)))return true;
        nextCoRoutine=nullptr;
        return false;
    }
    if(!Matches(kCreateCoRoutine,kCoRoutineSig,sizeof(kCoRoutineSig)))return false;
    // Its first instruction (mov [rsp+18h],rbx, 5 bytes, position-free), then on at +5.
    unsigned char tramp[5+12]={0x48,0x89,0x5C,0x24,0x18,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto back=reinterpret_cast<std::uintptr_t>(at+5);
    std::memcpy(tramp+7,&back,8);
    nextCoRoutine=reinterpret_cast<CoRoutineFn>(AllocateNearCode(at,tramp,sizeof(tramp)));
    if(!nextCoRoutine)return false;
    const unsigned char head[5]={0x48,0x89,0x5C,0x24,0x18};
    if(edf::PatchCode(at,head,jump,sizeof(jump)))return true;
    nextCoRoutine=nullptr;
    return false;
}
}  // namespace

// At load, before InstallRoomUi (its HQ hook then wraps this one: the ctx it hands on has no input while 整备 is open).
bool InstallPackSwitch() noexcept {
    for(const Sig& s:kSigs)
        if(!Matches(s.rva,s.bytes,s.size)) {
            Log("HOOK pack switch REFUSED: the code at EDF+0x%X is not as expected (the HQ stays as it is)",s.rva);
            return false;
        }
    void** const modTable=reinterpret_cast<void**>(image+kModuleVtable);
    if(!Readable(modTable,(kModByDecl+1)*8) || modTable[kModCompile]!=image+kCompileFunction
       || modTable[kModByDecl]!=image+kFunctionByDecl) {
        Log("HOOK pack switch REFUSED: asCModule's vtable is not AngelScript 2.28.2's (the HQ stays as it is)");
        return false;
    }
    if(!HookCoRoutine()){Log("HOOK pack switch REFUSED: createCoRoutine could not be hooked (the HQ stays as it is)");return false;}
    if(!edf::ChainVtableSlot(reinterpret_cast<void**>(image+kHqSlot),reinterpret_cast<void*>(&HqUpdate),&nextHq)) {
        Log("HOOK pack switch REFUSED: the HQ's OnUpdate slot could not be chained (createCoRoutine passes straight through)");
        return false;
    }
    Log("HOOK pack switch=1 (the HQ's Q / E, LB / RB: the previous / next mission pack)");
    return true;
}
}  // namespace crew
