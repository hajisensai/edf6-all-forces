// EDF6Autopilot: a test-only plugin (never shipped: tools/installer.py PLUGINS does not list it, CMake builds it as a
// MODULE into build/tools) that lets tests/autopilot/drive.py play the game in the background, for measurements
// (the user, 2026-10-07: "use code, or let it run in the background"; no keys sent to the desktop, no focus taken):
//  - EDF.dll reads the keyboard with GetKeyboardState / GetKeyState and checks GetForegroundWindow; those three imports
//    are pointed here: the game window counts as the foreground one, and the keys listed in <dll>.keys (hex virtual-key
//    codes, written by the driver) count as held, on top of the real ones;
//  - every second, and when the keys change, a row in <dll>.log: the game's committed memory (and its peak), its RAM,
//    and the machine's free RAM and commit, so a mission's load shows as a curve;
//  - commands in <dll>.cmd (one at a time, cleared once run): "mem", "quit" (the game's own ExitApp way), and, written
//    before the launch, "mission <row|RM015|M001|range> [difficulty 0-4]": straight into that offline mission, no menu
//    ("range": the test range's own mission pack, row 0 of the offline mode whose content id is the TestRangeContent
//    the installer wrote into the EDF6VehicleCrew.ini next to this DLL; RM015 is the stock mission again). The
//    menus are MAINSCRIPT.AS's routines, each run by name through createCoRoutine(const string &in) (0x12946F0, registered at
//    0x129463E; 0x1294B00 is the (string, any@) overload); its leftover
//    Debug_StartOffline() skips the logo, title, Epic login and slot choice, and the first HQMain() becomes
//    PlayMission_Offline() with the mission row / difficulty set (GS+0x48 / +0x4C, GS = *(EDF+0x20B2890)); for "range"
//    the mode is switched first the way the script's SetMode(int) does it (EnterRangeMode).
//    All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include <Windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <string>
#pragma warning(push)
#pragma warning(disable:4201)
#include "PluginAPI.h"
#pragma warning(pop)
#include "edf/memory.h"
#include "edf/patch.h"

namespace {
HMODULE self=nullptr;
wchar_t keysPath[MAX_PATH]{},logPath[MAX_PATH]{},cmdPath[MAX_PATH]{};
volatile LONG held[256]{};
volatile HWND gameWindow=nullptr;
ULONGLONG started=0;
unsigned char* image=nullptr;

// The game's script routine starter and its exit (ExitApp 0x1183190: the may-close flag, then WM_CLOSE to its window).
constexpr std::size_t kCreateCoRoutine=0x12946F0,kGlobalState=0x20B2890,kMissionRow=0x48,kDifficulty=0x4C;
constexpr std::size_t kMayClose=0x21360B8,kGameHwnd=0x21360A8;
const unsigned char kCoRoutineSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
using CoRoutineFn=void*(__fastcall*)(void*,const std::wstring*);
CoRoutineFn realCoRoutine=nullptr;
volatile LONG missionRow=-1,missionDifficulty=1;
volatile LONG rangeContent=-1;   // "mission range": the test range's content id read from the ini (-1: not asked)

// The game's mode list: count at GS+0x30, mode pointers at GS+0x20 (0x8C4CE3 walks them). A mode's ModeList item
// (cfg = *(mode+0x10), its values at cfg + *(cfg+8)): +0x68 item 8 the online flag (0x8A2379), +0x74 item 9 the content
// id, +0x80 item 10 the type (0x8C4CA0 compares both).
constexpr std::size_t kModeArray=0x20,kModeCount=0x30,kModeCfg=0x10,kItemOnline=0x68,kItemContent=0x74,kItemType=0x80;
// The mode switch (GS, index): what the script's `bool SetMode(int)` (0x70FAB0) calls once modes[index] is not null.
constexpr std::size_t kSetMode=0xDC460;
// mov [rsp+8],rbx; mov [rsp+10h],rbp; mov [rsp+18h],rsi; mov [rsp+20h],rdi; push r14; sub rsp,20h;
// mov rax,[rcx+20h]; mov rbp,rcx; mov esi,edx: the modes array indexed by edx
const unsigned char kSetModeSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,
    0x89,0x7C,0x24,0x20,0x41,0x56,0x48,0x83,0xEC,0x20,0x48,0x8B,0x41,0x20,0x48,0x8B,0xE9,0x8B,0xF2};
using SetModeFn=void(__fastcall*)(void*,unsigned);

using ForegroundFn=HWND(WINAPI*)();
using KeyboardStateFn=BOOL(WINAPI*)(PBYTE);
using KeyStateFn=SHORT(WINAPI*)(int);
ForegroundFn realForeground=nullptr;
KeyboardStateFn realKeyboardState=nullptr;
KeyStateFn realKeyState=nullptr;

void Log(const char* format,...) noexcept {
    char line[512];
    const int head=std::snprintf(line,sizeof(line),"[%8.1f s] ",(GetTickCount64()-started)/1000.0);
    va_list args;va_start(args,format);
    std::vsnprintf(line+head,sizeof(line)-head-2,format,args);
    va_end(args);
    strcat_s(line,"\r\n");
    const HANDLE f=CreateFileW(logPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return;
    DWORD wrote=0;WriteFile(f,line,static_cast<DWORD>(std::strlen(line)),&wrote,nullptr);CloseHandle(f);
}

BOOL CALLBACK FindGameWindow(HWND w,LPARAM) {
    DWORD pid=0;GetWindowThreadProcessId(w,&pid);
    if(pid!=GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w,GW_OWNER))return TRUE;
    RECT r{};GetClientRect(w,&r);
    if(r.right-r.left<320)return TRUE;
    gameWindow=w;
    return FALSE;
}

HWND WINAPI Foreground() {
    const HWND real=realForeground();
    DWORD pid=0;GetWindowThreadProcessId(real,&pid);
    if(pid==GetCurrentProcessId() || !gameWindow)return real;
    return gameWindow;
}

BOOL WINAPI KeyboardState(PBYTE keys) {
    const BOOL ok=realKeyboardState(keys);
    if(ok)for(int vk=0;vk<256;++vk)if(held[vk])keys[vk]|=0x80;
    return ok;
}

SHORT WINAPI KeyState(int vk) {
    const SHORT real=realKeyState(vk);
    return vk>=0 && vk<256 && held[vk] ? static_cast<SHORT>(real|0x8000) : real;
}

// The import slot of `function` (from `dll`) in `module`'s import table pointed at `hook`; the old target returned.
void* PatchImport(HMODULE module,const char* dll,const char* function,void* hook) noexcept {
    auto base=reinterpret_cast<unsigned char*>(module);
    const auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
    const auto& dir=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for(auto d=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+dir.VirtualAddress);d->Name;++d) {
        if(_stricmp(reinterpret_cast<const char*>(base+d->Name),dll))continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA*>(base+d->OriginalFirstThunk);
        auto slots=reinterpret_cast<IMAGE_THUNK_DATA*>(base+d->FirstThunk);
        for(;names->u1.AddressOfData;++names,++slots) {
            if(IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal))continue;
            const auto by=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            if(std::strcmp(by->Name,function))continue;
            DWORD old=0;
            if(!VirtualProtect(&slots->u1.Function,sizeof(void*),PAGE_READWRITE,&old))return nullptr;
            void* const was=reinterpret_cast<void*>(slots->u1.Function);
            slots->u1.Function=reinterpret_cast<ULONG_PTR>(hook);
            VirtualProtect(&slots->u1.Function,sizeof(void*),old,&old);
            return was;
        }
    }
    return nullptr;
}

void LogMemory(const char* why) noexcept {
    PROCESS_MEMORY_COUNTERS_EX p{};
    MEMORYSTATUSEX m{};m.dwLength=sizeof(m);
    if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&p),sizeof(p)) ||
       !GlobalMemoryStatusEx(&m))return;
    constexpr double kMb=1024.0*1024.0;
    Log("MEM %s commit %.0f MB (peak %.0f) ram %.0f MB | machine ram free %.0f / %.0f, commit free %.0f / %.0f",why,
        p.PrivateUsage/kMb,p.PeakPagefileUsage/kMb,p.WorkingSetSize/kMb,m.ullAvailPhys/kMb,m.ullTotalPhys/kMb,
        m.ullAvailPageFile/kMb,m.ullTotalPageFile/kMb);
}

// The script's routines as they start: with a mission asked for, the title becomes Debug_StartOffline and the first HQ
// menu the mission itself (the declarations are the script's: "string Name()").
// The first calls' arguments, each read as an MSVC std::wstring (diagnostics while the signature is unconfirmed).
void DumpArg(const char* name,const void* p) noexcept {
    wchar_t text[64]{};
    unsigned long long size=0,cap=0;
    __try {
        const auto b=static_cast<const unsigned char*>(p);
        size=*reinterpret_cast<const unsigned long long*>(b+0x10);
        cap=*reinterpret_cast<const unsigned long long*>(b+0x18);
        const wchar_t* chars=cap>7 ? *reinterpret_cast<wchar_t* const*>(b) : reinterpret_cast<const wchar_t*>(b);
        for(int i=0;i<63 && i<static_cast<int>(size<200 ? size : 0);++i)text[i]=chars[i];
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    char narrow[64]{};
    for(int i=0;i<63 && text[i];++i)narrow[i]=text[i]<128 && text[i]>=32 ? static_cast<char>(text[i]) : '?';
    Log("  %s=%p size=%llu cap=%llu \"%s\"",name,p,size,cap,narrow);
}

volatile LONG dumped=0;

// The offline mode (item 8 online 0, item 10 type 0) whose content id is `content`; -1 if none (or unreadable). The
// pack's online mode carries the same id, hence the two checks.
int FindRangeMode(const unsigned char* gs,int content) noexcept {
    __try {
        const unsigned count=*reinterpret_cast<const unsigned*>(gs+kModeCount);
        const auto modes=*reinterpret_cast<unsigned char* const* const*>(gs+kModeArray);
        for(unsigned i=0;i<count;++i) {
            if(!modes[i])continue;
            const auto cfg=*reinterpret_cast<const unsigned char* const*>(modes[i]+kModeCfg);
            const unsigned char* values=cfg+*reinterpret_cast<const int*>(cfg+8);
            if(!*reinterpret_cast<const int*>(values+kItemOnline) && !*reinterpret_cast<const int*>(values+kItemType) &&
               *reinterpret_cast<const int*>(values+kItemContent)==content)return static_cast<int>(i);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("RANGE the mode list is unreadable");}
    return -1;
}

// "mission range": the test range pack's offline mode made current, as SetMode(int) does; false leaves the menu as is.
bool EnterRangeMode(unsigned char* gs) noexcept {
    const LONG content=InterlockedExchange(&rangeContent,-1);
    if(content<0)return true;   // no range asked: the row alone
    if(!content){Log("RANGE TestRangeContent=0 in EDF6VehicleCrew.ini (the test range pack is not installed)");return false;}
    const int mode=FindRangeMode(gs,content);
    if(mode<0){Log("RANGE no offline mode with content id %ld",content);return false;}
    if(!edf::Matches(image,kSetMode,kSetModeSig,sizeof(kSetModeSig))){Log("RANGE EDF+%#zx does not match",kSetMode);return false;}
    reinterpret_cast<SetModeFn>(image+kSetMode)(gs,static_cast<unsigned>(mode));
    Log("RANGE mode %d (content id %ld) made current",mode,content);
    return true;
}

void* __fastcall CoRoutine(void* out,const std::wstring* decl) {
    if(InterlockedIncrement(&dumped)<=60){Log("COROUTINE call");DumpArg("rcx",out);DumpArg("rdx",decl);}
    if(missionRow>=0 && decl) {
        if(*decl==L"string StartToTitle()") {
            static const std::wstring offline=L"string Debug_StartOffline()";
            Log("SCRIPT StartToTitle -> Debug_StartOffline");
            return realCoRoutine(out,&offline);
        }
        if(*decl==L"string HQMain()") {
            const LONG row=InterlockedExchange(&missionRow,-1);
            auto gs=*reinterpret_cast<unsigned char**>(image+kGlobalState);
            if(gs && !EnterRangeMode(gs)) {
                Log("SCRIPT HQMain: the test range's mode not entered, the menu as is");
                return realCoRoutine(out,decl);
            }
            if(gs) {
                *reinterpret_cast<int*>(gs+kMissionRow)=row;
                *reinterpret_cast<int*>(gs+kDifficulty)=missionDifficulty;
                static const std::wstring play=L"string PlayMission_Offline()";
                Log("SCRIPT HQMain -> PlayMission_Offline: row %ld, difficulty %ld",row,missionDifficulty);
                LogMemory("mission start");
                return realCoRoutine(out,&play);
            }
            Log("SCRIPT HQMain: no game state, the menu as is");
        }
    }
    return realCoRoutine(out,decl);
}

bool HookCoRoutine() noexcept {
    unsigned char* const at=image+kCreateCoRoutine;
    if(!edf::Matches(image,kCreateCoRoutine,kCoRoutineSig,sizeof(kCoRoutineSig)))return false;
    // The original: its first instruction (mov [rsp+18h],rbx, 5 bytes, position-free), then on at +5.
    unsigned char tramp[5+12]={0x48,0x89,0x5C,0x24,0x18,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto back=reinterpret_cast<std::uintptr_t>(at+5);
    std::memcpy(tramp+7,&back,8);
    realCoRoutine=reinterpret_cast<CoRoutineFn>(edf::AllocateNearCode(at,tramp,sizeof(tramp)));
    void* const thunk=edf::AllocateNearThunk(at,reinterpret_cast<void*>(&CoRoutine));
    if(!realCoRoutine || !thunk)return false;
    unsigned char jump[5]={0xE9};
    const auto rel=static_cast<std::int32_t>(static_cast<unsigned char*>(thunk)-(at+5));
    std::memcpy(jump+1,&rel,4);
    return edf::PatchCode(at,kCoRoutineSig,jump,sizeof(jump));
}

// The way the game's own quit ends it (ExitApp 0x1183190): its may-close flag, then WM_CLOSE posted to its window (posted:
// this thread is not the window's), so its loop ends and it exits as from the menu.
void Quit() noexcept {
    const HWND window=*reinterpret_cast<HWND*>(image+kGameHwnd);
    DWORD old=0;
    unsigned char* const flag=image+kMayClose;
    if(VirtualProtect(flag,1,PAGE_READWRITE,&old)){*flag=1;VirtualProtect(flag,1,old,&old);}
    Log("QUIT window %p",window);
    PostMessageW(window,WM_CLOSE,0,0);
}

// The test range pack's content id: [VehicleCrew] TestRangeContent of the EDF6VehicleCrew.ini in this DLL's own
// directory (Mods/Plugins), written by the installer; 0 when the pack is not installed.
LONG ReadRangeContent() noexcept {
    wchar_t ini[MAX_PATH]{};
    if(!GetModuleFileNameW(self,ini,MAX_PATH))return 0;
    wchar_t* slash=wcsrchr(ini,L'\\');
    if(!slash)return 0;
    if(wcscpy_s(slash+1,MAX_PATH-(slash+1-ini),L"EDF6VehicleCrew.ini"))return 0;
    const UINT id=GetPrivateProfileIntW(L"VehicleCrew",L"TestRangeContent",0,ini);
    return id<=65535 ? static_cast<LONG>(id) : 0;
}

// "mission <row|RM015|M001|range> [difficulty]": the offline list's row (0-based: RM015 is 13, M001 is 1), difficulty
// 0-4. RM015 is the stock mission again (the test range left it for its own pack); "range" is that pack's row 0.
void AskMission(const char* text) noexcept {
    char name[32]{};int difficulty=1;
    if(sscanf_s(text,"%*s %31s %d",name,static_cast<unsigned>(sizeof(name)),&difficulty)<1)return;
    const bool range=!_stricmp(name,"range");
    const int row=range ? 0 : !_stricmp(name,"RM015") ? 13 : !_stricmp(name,"M001") ? 1 : std::atoi(name);
    missionDifficulty=difficulty<0 || difficulty>4 ? 1 : difficulty;
    rangeContent=range ? ReadRangeContent() : -1;
    missionRow=row;
    Log("MISSION asked: %s row %d, difficulty %ld (applied when the script starts its title)",
        range ? "the test range pack's" : "offline",row,missionDifficulty);
    if(range)Log("MISSION the test range's content id %ld (EDF6VehicleCrew.ini TestRangeContent)",rangeContent);
}

// One command from <dll>.cmd (the driver writes it, this clears it once done).
void RunCommand(const char* text) noexcept {
    char word[32]{};
    if(sscanf_s(text,"%31s",word,static_cast<unsigned>(sizeof(word)))!=1)return;
    Log("CMD %s",text);
    if(!std::strcmp(word,"mem"))LogMemory("cmd");
    else if(!std::strcmp(word,"quit")){LogMemory("quit");Quit();}
    else if(!std::strcmp(word,"mission"))AskMission(text);
    else Log("CMD unknown: %s",word);
}

void PollCommand() noexcept {
    char text[256]{};
    const HANDLE f=CreateFileW(cmdPath,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return;
    DWORD got=0;ReadFile(f,text,sizeof(text)-1,&got,nullptr);CloseHandle(f);
    if(!got)return;
    DeleteFileW(cmdPath);
    RunCommand(text);
}

// The keys file read every 15 ms: whitespace separated hex virtual-key codes, all held until the file changes.
DWORD WINAPI Loop(void*) {
    char last[512]{};
    ULONGLONG nextMemory=0;
    for(;;) {
        if(!gameWindow)EnumWindows(&FindGameWindow,0);
        char text[512]{};
        const HANDLE f=CreateFileW(keysPath,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        if(f!=INVALID_HANDLE_VALUE){DWORD got=0;ReadFile(f,text,sizeof(text)-1,&got,nullptr);CloseHandle(f);}
        if(std::strcmp(text,last)) {
            LONG next[256]{};
            for(char* p=text;*p;) {
                char* end=nullptr;
                const unsigned long vk=std::strtoul(p,&end,16);
                if(end==p){++p;continue;}
                if(vk<256)next[vk]=1;
                p=end;
            }
            for(int vk=0;vk<256;++vk)InterlockedExchange(&held[vk],next[vk]);
            strcpy_s(last,text);
            Log("KEYS [%s]",text);
        }
        const ULONGLONG now=GetTickCount64();
        if(now>=nextMemory){LogMemory("tick");nextMemory=now+1000;}
        PollCommand();
        Sleep(15);
    }
}
}  // namespace

extern "C" __declspec(dllexport) bool EDFMLAPI EML6_Load(PluginInfo* info) {
    if(!info)return false;
    started=GetTickCount64();
    GetModuleFileNameW(self,keysPath,MAX_PATH);
    wcscpy_s(logPath,keysPath);
    wcscpy_s(cmdPath,keysPath);
    wchar_t* dot=wcsrchr(keysPath,L'.');wcscpy_s(dot,MAX_PATH-(dot-keysPath),L".keys");
    dot=wcsrchr(logPath,L'.');wcscpy_s(dot,MAX_PATH-(dot-logPath),L".log");
    dot=wcsrchr(cmdPath,L'.');wcscpy_s(dot,MAX_PATH-(dot-cmdPath),L".cmd");
    info->infoVersion=PluginInfo::MaxInfoVer;info->name="EDF6 Autopilot (test only)";info->version=PLUG_VER(0,1,0,0);
    const HMODULE edf=GetModuleHandleW(L"EDF.dll");
    if(!edf){Log("no EDF.dll");return false;}
    image=reinterpret_cast<unsigned char*>(edf);
    // Every command/hook below uses fixed RVAs, including Quit(). A coroutine
    // prologue alone does not validate those independent globals on another build.
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64 ||
       nt->FileHeader.TimeDateStamp!=0x678CCB46 || nt->OptionalHeader.SizeOfImage!=0x22CE000) {
        Log("unsupported EDF.dll: no hooks or commands installed");
        return false;
    }
    realForeground=reinterpret_cast<ForegroundFn>(PatchImport(edf,"USER32.dll","GetForegroundWindow",reinterpret_cast<void*>(&Foreground)));
    realKeyboardState=reinterpret_cast<KeyboardStateFn>(PatchImport(edf,"USER32.dll","GetKeyboardState",reinterpret_cast<void*>(&KeyboardState)));
    realKeyState=reinterpret_cast<KeyStateFn>(PatchImport(edf,"USER32.dll","GetKeyState",reinterpret_cast<void*>(&KeyState)));
    Log("LOADED foreground=%d keyboardState=%d keyState=%d",realForeground!=nullptr,realKeyboardState!=nullptr,realKeyState!=nullptr);
    if(!realForeground)realForeground=&GetForegroundWindow;
    if(!realKeyboardState)realKeyboardState=&GetKeyboardState;
    if(!realKeyState)realKeyState=&GetKeyState;
    Log("HOOK createCoRoutine=%d",HookCoRoutine());
    LogMemory("load");
    PollCommand();   // a mission asked before the launch, before the script's main loop runs
    CloseHandle(CreateThread(nullptr,0,&Loop,nullptr,0,nullptr));
    return true;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){self=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
