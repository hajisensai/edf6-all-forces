// EDF6Autopilot: a test-only plugin (never shipped: tools/installer.py PLUGINS does not list it, CMake builds it as a
// MODULE into build/tools) that lets tests/autopilot/drive.py play the game in the background, for measurements
// (the user, 2026-10-07: "use code, or let it run in the background"; no keys sent to the desktop, no focus taken):
//  - EDF.dll reads the keyboard with GetKeyboardState / GetKeyState and checks GetForegroundWindow; those three imports
//    are pointed here: the game window counts as the foreground one, and only the keys listed in <dll>.keys (hex
//    virtual-key codes, written by the driver) count as held (never the desktop's: the user's typing is not the game's);
//  - the system mouse and the desktop's activation stay the user's (KeepDesktop, KeepWindowInactive): the game's cursor is a virtual
//    one, its window is made off screen and never activates;
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
#include <intrin.h>
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
#include "airdrop_probe.h"

namespace {
HMODULE self=nullptr;
wchar_t keysPath[MAX_PATH]{},logPath[MAX_PATH]{},cmdPath[MAX_PATH]{};
// What the autopilot holds: codes 0x00-0xFF are virtual keys, 0x100 + n the virtual pad's button bit n (XInput
// wButtons), 0x110 / 0x111 its left / right trigger (kPadLeftTrigger, kPadRightTrigger).
constexpr int kKeyCodes=0x100,kPadBase=0x100,kPadLeftTrigger=0x110,kPadRightTrigger=0x111,kInputCodes=0x112;
volatile LONG held[kInputCodes]{};
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

// Whether the system cursor is safe from the game (KeepDesktop took SetCursorPos and GetCursorPos): only then is the
// game told it is the foreground window. A game told so moves the cursor every frame (see KeepDesktop), so without the
// cursor taken the autopilot plays without input rather than pull the user's mouse.
volatile LONG desktopSafe=0;

HWND WINAPI Foreground() {
    const HWND real=realForeground();
    DWORD pid=0;GetWindowThreadProcessId(real,&pid);
    if(pid==GetCurrentProcessId() || !gameWindow || !desktopSafe)return real;
    return gameWindow;
}

void* PatchImport(HMODULE module,const char* dll,const char* function,void* hook) noexcept;

// The keyboard (and mouse buttons) the game sees: only the autopilot's held keys, never the desktop's. The game believes
// it is the foreground window (Foreground), so with the real state the user's typing and clicks elsewhere would play it.
BOOL WINAPI KeyboardState(PBYTE keys) {
    if(!keys)return FALSE;
    for(int vk=0;vk<kKeyCodes;++vk)keys[vk]=held[vk] ? 0x80 : 0;
    return TRUE;
}

SHORT WINAPI KeyState(int vk) {
    return vk>=0 && vk<kKeyCodes && held[vk] ? static_cast<SHORT>(0x8000) : 0;
}

// --- The system mouse and the desktop's activation stay the user's (2026-10-10: a background run pulled the user's
// mouse to the right edge of the desktop, where the off-screen game window's centre clamps).
// The cause, measured (tests/autopilot drive.py run, the BACKGROUND rows): told it is the foreground window (Foreground
// above, needed for its input to run), EDF.dll's frame loop (0x1183880) takes the mouse as a focused game does: every
// frame GetCursorPos, the delta from its window's centre as the mouse look, then SetCursorPos back to that centre
// (0x1183B7D, returning to EDF+0x1183B83; also 0x11845D1). Pointing EDF.dll's own import slots elsewhere is not
// enough: the Epic overlay (EOSOVH-Win64-Shipping.dll, loaded by EOSSDK some seconds in) rewrites EDF.dll's
// SetCursorPos / GetCursorPos slots to its hooks (EOSOVH+0x249F0 / +0x24020), which go on to USER32. So the cursor
// calls are taken in USER32 itself, for the whole process (KeepDesktop): whoever calls them, through whatever slot,
// meets a virtual cursor. EDF.dll imports no ClipCursor, SetCapture, RegisterRawInputDevices or DirectInput (import
// scan, 2026-10-10); ClipCursor and SendInput are taken anyway so no module can use them (mouse_event is a whole
// function in USER32, not a stub, and is left: see StubWithRoom).
// The window side stays in EDF.dll's imports (its window is made at 2.4 s, before the overlay): ShowWindow
// (SW_SHOWDEFAULT, 0x1183733) shows without activating, SetWindowPos (0x1183788, 0x1184B2C, 0x1184C17) gets
// SWP_NOACTIVATE and keeps the window off screen, SetFocus (0x11837D3) does nothing, CreateWindowExW (0x11836F5) makes
// it off screen with WS_EX_NOACTIVATE; ShowCursor (0x1183798, 0x1183C32) counts on its own. SetForegroundWindow
// (EDF.dll 0xBC2C5 / 0x6B47BE / 0x6B48FE, EOSSDK too) is taken in USER32 with the cursor.
using ShowWindowFn=BOOL(WINAPI*)(HWND,int);
using SetWindowPosFn=BOOL(WINAPI*)(HWND,HWND,int,int,int,int,UINT);
using CreateWindowFn=HWND(WINAPI*)(DWORD,LPCWSTR,LPCWSTR,DWORD,int,int,int,int,HWND,HMENU,HINSTANCE,LPVOID);
ShowWindowFn realShowWindow=nullptr;
SetWindowPosFn realSetWindowPos=nullptr,realEosSetWindowPos=nullptr;
CreateWindowFn realCreateWindow=nullptr;
volatile LONG cursorX=0,cursorY=0,cursorSet=0,cursorCount=0;
volatile LONG windowCalls[2]{};   // ShowCursor, SetFocus: how often EDF.dll asked (both absorbed)
HWND volatile madeWindow=nullptr;   // the game's top-level window, as CreateWindowExW made it

int OffScreenX() noexcept {return GetSystemMetrics(SM_XVIRTUALSCREEN)+GetSystemMetrics(SM_CXVIRTUALSCREEN)+200;}

int WINAPI VirtualShowCursor(BOOL show) {
    InterlockedIncrement(&windowCalls[0]);
    return show ? InterlockedIncrement(&cursorCount) : InterlockedDecrement(&cursorCount);
}

HWND WINAPI NoFocus(HWND) {InterlockedIncrement(&windowCalls[1]);return nullptr;}

// The show commands that activate, as their non-activating forms.
BOOL WINAPI ShowNoActivate(HWND w,int cmd) {
    switch(cmd) {
    case SW_SHOWNORMAL: case SW_SHOWMAXIMIZED: case SW_RESTORE: case SW_SHOWDEFAULT: cmd=SW_SHOWNOACTIVATE;break;
    case SW_SHOW: cmd=SW_SHOWNA;break;
    case SW_MINIMIZE: cmd=SW_SHOWMINNOACTIVE;break;
    default: break;
    }
    return realShowWindow(w,cmd);
}

BOOL MovePos(SetWindowPosFn real,HWND w,HWND after,int x,int y,int cx,int cy,UINT flags) noexcept {
    if(w && w==madeWindow && !(flags&SWP_NOMOVE))x=OffScreenX();
    return real(w,after,x,y,cx,cy,flags|SWP_NOACTIVATE);
}
BOOL WINAPI EdfSetWindowPos(HWND w,HWND after,int x,int y,int cx,int cy,UINT flags) {
    return MovePos(realSetWindowPos,w,after,x,y,cx,cy,flags);
}
BOOL WINAPI EosSetWindowPos(HWND w,HWND after,int x,int y,int cx,int cy,UINT flags) {
    return MovePos(realEosSetWindowPos,w,after,x,y,cx,cy,flags);
}

HWND WINAPI OffScreenWindow(DWORD ex,LPCWSTR cls,LPCWSTR name,DWORD style,int x,int y,int w,int h,HWND parent,HMENU menu,
                            HINSTANCE inst,LPVOID param) {
    const bool top=!parent && !(style&WS_CHILD);
    const HWND made=realCreateWindow(top ? ex|WS_EX_NOACTIVATE : ex,cls,name,style,top ? OffScreenX() : x,y,w,h,parent,menu,
                                     inst,param);
    if(top && made)madeWindow=made;
    Log("BACKGROUND window %p made off screen (x %d, asked %d), no activation",made,top ? OffScreenX() : x,x);
    return made;
}

// The calls taken in USER32 (KeepDesktop): how often, and the first callers by module and offset.
enum DesktopCall { kSetCursor, kGetCursor, kClipCursor, kSendInput, kForeground, kKeyboard, kDesktopCalls };
const char* const kDesktopCallNames[kDesktopCalls]={"SetCursorPos","GetCursorPos","ClipCursor","SendInput",
                                                    "SetForegroundWindow","GetKeyboardState"};
volatile LONG desktopCalls[kDesktopCalls]{};

void Caller(DesktopCall which,void* at) noexcept {
    const LONG n=InterlockedIncrement(&desktopCalls[which]);
    if(n>3)return;
    HMODULE m=nullptr;
    char name[MAX_PATH]="?";
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCSTR>(at),&m) && m)GetModuleFileNameA(m,name,MAX_PATH);
    const char* base=std::strrchr(name,'\\');
    Log("BACKGROUND %s (call %ld) taken, from %s+%#llx",kDesktopCallNames[which],n,base ? base+1 : name,
        static_cast<unsigned long long>(static_cast<unsigned char*>(at)-reinterpret_cast<unsigned char*>(m)));
}

// The virtual cursor: SetCursorPos moves only it; GetCursorPos reads it (until the first set, where the real one was,
// read with GetCursorInfo: GetCursorPos itself is taken).
BOOL WINAPI DeskSetCursorPos(int x,int y) {
    Caller(kSetCursor,_ReturnAddress());
    InterlockedExchange(&cursorX,x);InterlockedExchange(&cursorY,y);InterlockedExchange(&cursorSet,1);
    return TRUE;
}
BOOL WINAPI DeskGetCursorPos(LPPOINT p) {
    Caller(kGetCursor,_ReturnAddress());
    if(!p)return FALSE;
    if(!cursorSet) {
        CURSORINFO real{};real.cbSize=sizeof(real);
        if(!GetCursorInfo(&real))return FALSE;
        InterlockedExchange(&cursorX,real.ptScreenPos.x);InterlockedExchange(&cursorY,real.ptScreenPos.y);
        InterlockedExchange(&cursorSet,1);
    }
    p->x=cursorX;p->y=cursorY;
    return TRUE;
}
BOOL WINAPI DeskClipCursor(const RECT*) {Caller(kClipCursor,_ReturnAddress());return TRUE;}
UINT WINAPI DeskSendInput(UINT count,LPINPUT,int) {Caller(kSendInput,_ReturnAddress());return count;}
BOOL WINAPI DeskSetForegroundWindow(HWND) {Caller(kForeground,_ReturnAddress());return TRUE;}
// The keyboard: the same overlay rewrites EDF.dll's GetKeyboardState / GetKeyState slots too (so the held keys never
// reached the game, 2026-10-10: holding W did not move the player), so the autopilot's keys are given in USER32 as well.
// Only GetKeyboardState (EDF.dll's key read, 0x95FA51): GetKeyState / GetAsyncKeyState are whole functions in USER32,
// and writing over their start crashed the game in USER32 once the overlay came up (2026-10-10, EDF6.exe.117352.dmp);
// the entries diverted here are all import stubs (jmp [win32u] padded to 16 bytes), checked against .pdata.
BOOL WINAPI DeskKeyboardState(PBYTE keys) {Caller(kKeyboard,_ReturnAddress());return KeyboardState(keys);}

struct Diversion { const char* function; void* hook; };
// SetPhysicalCursorPos / GetPhysicalCursorPos: on this Windows the same exports as SetCursorPos / GetCursorPos (one
// address each, 2026-10-10); listed so a build where they differ is covered too.
const Diversion kDiversions[]={
    {"SetCursorPos",reinterpret_cast<void*>(&DeskSetCursorPos)},
    {"SetPhysicalCursorPos",reinterpret_cast<void*>(&DeskSetCursorPos)},
    {"GetCursorPos",reinterpret_cast<void*>(&DeskGetCursorPos)},
    {"GetPhysicalCursorPos",reinterpret_cast<void*>(&DeskGetCursorPos)},
    {"ClipCursor",reinterpret_cast<void*>(&DeskClipCursor)},
    {"SendInput",reinterpret_cast<void*>(&DeskSendInput)},
    {"SetForegroundWindow",reinterpret_cast<void*>(&DeskSetForegroundWindow)},
    {"GetKeyboardState",reinterpret_cast<void*>(&DeskKeyboardState)}};
constexpr int kDiversionCount=static_cast<int>(sizeof(kDiversions)/sizeof(kDiversions[0]));

// Whether the 12 bytes at `at` are an import stub and its int3 padding (jmp [rip+x]; rex.w jmp [rip+x]; or
// mov edx,imm32 then rex.w jmp [rip+x]): only those are written over, so no neighbouring code is ever touched.
// Decided by USER32's unwind table, not by the bytes: an overlay (Steam's) may already have written its own jump over
// a stub (2026-10-10: the byte check then refused every entry and the run pulled the mouse again). A stub has no
// unwind entry and starts a 16-byte slot; a real function (GetKeyState, mouse_event) has one and is never written.
bool StubWithRoom(const unsigned char* at) noexcept {
    DWORD64 base=0;
    const auto address=reinterpret_cast<DWORD64>(at);
    if(address%16)return false;
    for(std::size_t i=0;i<12;++i)if(RtlLookupFunctionEntry(address+i,&base,nullptr))return false;
    return true;
}

// `at` made to start with an absolute jump to `hook` (mov rax,imm64; jmp rax). The original is never called again.
bool Divert(unsigned char* at,void* hook) noexcept {
    if(!StubWithRoom(at))return false;
    unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    std::memcpy(jump+2,&hook,sizeof(hook));
    DWORD old=0;
    if(!VirtualProtect(at,sizeof(jump),PAGE_EXECUTE_READWRITE,&old))return false;
    std::memcpy(at,jump,sizeof(jump));
    VirtualProtect(at,sizeof(jump),old,&old);
    FlushInstructionCache(GetCurrentProcess(),at,sizeof(jump));
    return true;
}

// Every USER32 entry above diverted once (an alias already diverted is skipped); how many entries were written.
int KeepDesktop() noexcept {
    const HMODULE user32=GetModuleHandleW(L"user32.dll");
    unsigned char* done[kDiversionCount]{};
    int count=0;
    for(int i=0;i<kDiversionCount;++i) {
        auto at=reinterpret_cast<unsigned char*>(GetProcAddress(user32,kDiversions[i].function));
        if(!at){Log("BACKGROUND user32 has no %s",kDiversions[i].function);continue;}
        bool alias=false;
        for(int j=0;j<i;++j)alias=alias || done[j]==at;
        if(alias){Log("BACKGROUND %s: the same export as one already taken",kDiversions[i].function);continue;}
        if(Divert(at,kDiversions[i].hook)){done[i]=at;++count;}
        else Log("BACKGROUND %s: not diverted (not an import stub: %02X %02X %02X %02X %02X %02X)",kDiversions[i].function,
                 at[0],at[1],at[2],at[3],at[4],at[5]);
    }
    const auto taken=[&](const char* fn) {
        const void* at=GetProcAddress(user32,fn);
        for(int i=0;i<kDiversionCount;++i)if(done[i] && done[i]==at)return true;
        return false;
    };
    desktopSafe=taken("SetCursorPos") && taken("GetCursorPos");
    if(!desktopSafe)Log("BACKGROUND the cursor calls are not taken: the game is NOT told it is in the foreground "
                        "(no input reaches it, but the user's mouse stays theirs)");
    return count;
}

// Where an import slot of EDF.dll points now (module and offset): the overlay's rewrite shows here.
void LogSlot(const char* what,std::size_t rva) noexcept {
    void* const to=*reinterpret_cast<void* const*>(image+rva);
    HMODULE m=nullptr;char name[MAX_PATH]="?";
    if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          static_cast<LPCSTR>(to),&m) && m)GetModuleFileNameA(m,name,MAX_PATH);
    const char* base=std::strrchr(name,'\\');
    Log("BACKGROUND EDF.dll import %s -> %s+%#llx",what,base ? base+1 : name,
        static_cast<unsigned long long>(static_cast<unsigned char*>(to)-reinterpret_cast<unsigned char*>(m)));
}

// The window side in the imports of EDF.dll (and EOSSDK's SetWindowPos); how many of the slots were found.
int KeepWindowInactive(HMODULE edf) noexcept {
    int done=0;
    auto put=[&](HMODULE m,const char* fn,void* hook)->void* {
        void* const was=m ? PatchImport(m,"USER32.dll",fn,hook) : nullptr;
        if(was)++done;
        else Log("BACKGROUND %s: no import slot",fn);
        return was;
    };
    put(edf,"ShowCursor",reinterpret_cast<void*>(&VirtualShowCursor));
    put(edf,"SetFocus",reinterpret_cast<void*>(&NoFocus));
    realShowWindow=reinterpret_cast<ShowWindowFn>(put(edf,"ShowWindow",reinterpret_cast<void*>(&ShowNoActivate)));
    realSetWindowPos=reinterpret_cast<SetWindowPosFn>(put(edf,"SetWindowPos",reinterpret_cast<void*>(&EdfSetWindowPos)));
    realCreateWindow=reinterpret_cast<CreateWindowFn>(put(edf,"CreateWindowExW",reinterpret_cast<void*>(&OffScreenWindow)));
    realEosSetWindowPos=reinterpret_cast<SetWindowPosFn>(put(GetModuleHandleW(L"EOSSDK-Win64-Shipping.dll"),"SetWindowPos",
                                                            reinterpret_cast<void*>(&EosSetWindowPos)));
    if(!realShowWindow)realShowWindow=&ShowWindow;
    if(!realSetWindowPos)realSetWindowPos=&SetWindowPos;
    if(!realEosSetWindowPos)realEosSetWindowPos=&SetWindowPos;
    if(!realCreateWindow)realCreateWindow=&CreateWindowExW;
    return done;
}

// --- The virtual pad. The game reads no gameplay key through the keyboard imports above (2026-10-10: W held through
// GetKeyboardState, which it reads twice a frame, moved nothing; its key reads there only ask "is any key down"), but it
// loads xinput9_1_0.dll and polls XInputGetState (EDF.dll's string at 0x1AE0310) for pads. So the autopilot is a pad:
// once that DLL is in, its XInputGetState starts with a jump to PadState, which reports pad 0 connected with the held
// pad codes as its buttons and triggers; other pads are not connected. Only written when the export is a whole function
// of at least 12 bytes by its unwind entry (so nothing beyond it is touched). The user's own pads are then not seen by
// this background game: they are the user's.
struct PadGamepad { WORD buttons; BYTE leftTrigger,rightTrigger; SHORT lx,ly,rx,ry; };
struct PadState { DWORD packet; PadGamepad pad; };
volatile LONG padPacket=0,padReads=0;
bool padIn=false;
ULONGLONG padLookAt=0;

DWORD WINAPI FakeXInputGetState(DWORD user,PadState* state) {
    InterlockedIncrement(&padReads);
    if(user>=4)return ERROR_DEVICE_NOT_CONNECTED;
    if(!state)return ERROR_BAD_ARGUMENTS;
    WORD buttons=0;
    for(int bit=0;bit<16;++bit)if(held[kPadBase+bit])buttons=static_cast<WORD>(buttons|(1u<<bit));
    *state=PadState{};
    state->pad.buttons=buttons;
    state->pad.leftTrigger=held[kPadLeftTrigger] ? 255 : 0;
    state->pad.rightTrigger=held[kPadRightTrigger] ? 255 : 0;
    state->packet=static_cast<DWORD>(InterlockedIncrement(&padPacket));
    return ERROR_SUCCESS;
}

// XInputGetCapabilities: EDF.dll asks it before each read (0x11883BC, flag 1 = gamepad) and reads only a pad that
// answers, so the virtual pad answers as a wired gamepad (type 1, subtype 1) on every index the game asks.
struct PadCaps { BYTE type,subType; WORD flags; PadGamepad pad; WORD leftMotor,rightMotor; };
DWORD WINAPI FakeXInputGetCapabilities(DWORD user,DWORD,PadCaps* caps) {
    if(user>=4)return ERROR_DEVICE_NOT_CONNECTED;
    if(!caps)return ERROR_BAD_ARGUMENTS;
    *caps=PadCaps{};
    caps->type=1;caps->subType=1;
    caps->pad.buttons=0xF3FF;caps->pad.leftTrigger=caps->pad.rightTrigger=255;
    return ERROR_SUCCESS;
}

bool WholeFunction(const unsigned char* at,std::size_t bytes) noexcept {
    DWORD64 base=0;
    const auto address=reinterpret_cast<DWORD64>(at);
    const RUNTIME_FUNCTION* f=RtlLookupFunctionEntry(address,&base,nullptr);
    return f && base+f->BeginAddress==address && f->EndAddress-f->BeginAddress>=bytes;
}

// Once a second until xinput9_1_0.dll is loaded, then once.
void InstallPad() noexcept {
    const ULONGLONG now=GetTickCount64();
    if(now<padLookAt)return;
    padLookAt=now+1000;
    const HMODULE xinput=GetModuleHandleW(L"xinput9_1_0.dll");
    if(!xinput)return;
    padIn=true;
    const struct { const char* name; void* hook; } kPadCalls[]={
        {"XInputGetState",reinterpret_cast<void*>(&FakeXInputGetState)},
        {"XInputGetCapabilities",reinterpret_cast<void*>(&FakeXInputGetCapabilities)}};
    unsigned char* found[2]{};
    for(int i=0;i<2;++i) {
        found[i]=reinterpret_cast<unsigned char*>(GetProcAddress(xinput,kPadCalls[i].name));
        if(!found[i] || !WholeFunction(found[i],12)) {
            Log("PAD xinput9_1_0!%s %p not a whole function: no virtual pad",kPadCalls[i].name,found[i]);
            return;
        }
    }
    for(int i=0;i<2;++i) {
        unsigned char jump[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
        std::memcpy(jump+2,&kPadCalls[i].hook,sizeof(void*));
        DWORD old=0;
        if(!VirtualProtect(found[i],sizeof(jump),PAGE_EXECUTE_READWRITE,&old)){Log("PAD %s not writable",kPadCalls[i].name);return;}
        std::memcpy(found[i],jump,sizeof(jump));
        VirtualProtect(found[i],sizeof(jump),old,&old);
        FlushInstructionCache(GetCurrentProcess(),found[i],sizeof(jump));
    }
    Log("PAD the virtual pad is in (xinput9_1_0!XInputGetState %p, XInputGetCapabilities %p)",found[0],found[1]);
}

// A key held (or let go) by the probe, on top of the keys file's (the next change of that file sets them all again).
void HoldKey(int code,bool down) noexcept {if(code>=0 && code<kInputCodes)InterlockedExchange(&held[code],down ? 1 : 0);}

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
                autopilot::AirdropProbeMissionStarted();
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
    else if(!std::strcmp(word,"probe")) {
        char what[32]{};
        if(sscanf_s(text,"%*s %31s",what,static_cast<unsigned>(sizeof(what)))==1 && !std::strcmp(what,"airdrop"))
            Log("PROBE airdrop installed=%d",autopilot::InstallAirdropProbe(image,&Log,&HoldKey));
        else Log("CMD probe: unknown probe %s",what);
    }
    else Log("CMD unknown: %s",word);
}

void PollCommand() noexcept {
    char text[1024]{};
    const HANDLE f=CreateFileW(cmdPath,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return;
    DWORD got=0;ReadFile(f,text,sizeof(text)-1,&got,nullptr);CloseHandle(f);
    if(!got)return;
    DeleteFileW(cmdPath);
    // One command per line (the driver may hand several before the launch: the mission, then a probe).
    char* next=nullptr;
    for(char* line=strtok_s(text,"\r\n",&next);line;line=strtok_s(nullptr,"\r\n",&next))RunCommand(line);
}

// The keys file read every 15 ms: whitespace separated hex virtual-key codes, all held until the file changes.
DWORD WINAPI Loop(void*) {
    char last[512]{};
    ULONGLONG nextMemory=0;
    unsigned memoryRows=0;
    for(;;) {
        if(!gameWindow)EnumWindows(&FindGameWindow,0);
        char text[512]{};
        const HANDLE f=CreateFileW(keysPath,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        if(f!=INVALID_HANDLE_VALUE){DWORD got=0;ReadFile(f,text,sizeof(text)-1,&got,nullptr);CloseHandle(f);}
        if(std::strcmp(text,last)) {
            LONG next[kInputCodes]{};
            for(char* p=text;*p;) {
                char* end=nullptr;
                const unsigned long vk=std::strtoul(p,&end,16);
                if(end==p){++p;continue;}
                if(vk<kInputCodes)next[vk]=1;
                p=end;
            }
            for(int vk=0;vk<kInputCodes;++vk)InterlockedExchange(&held[vk],next[vk]);
            strcpy_s(last,text);
            Log("KEYS [%s]",text);
        }
        const ULONGLONG now=GetTickCount64();
        if(now>=nextMemory) {
            LogMemory("tick");
            if(!(++memoryRows%10)) {
                Log("BACKGROUND taken in user32: SetCursorPos %ld, GetCursorPos %ld, ClipCursor %ld, "
                    "SendInput %ld, SetForegroundWindow %ld, key state %ld; the game's virtual cursor (%ld, %ld); "
                    "EDF.dll's ShowCursor %ld, SetFocus %ld",desktopCalls[kSetCursor],desktopCalls[kGetCursor],
                    desktopCalls[kClipCursor],desktopCalls[kSendInput],
                    desktopCalls[kForeground],desktopCalls[kKeyboard],cursorX,cursorY,windowCalls[0],windowCalls[1]);
                LogSlot("SetCursorPos",0x1755EC8);LogSlot("GetCursorPos",0x1755F80);
                LogSlot("GetKeyboardState",0x1756008);LogSlot("GetKeyState",0x1756010);
                Log("PAD XInputGetState read %ld times",padReads);
            }
            nextMemory=now+1000;
        }
        PollCommand();
        if(!padIn)InstallPad();
        autopilot::AirdropProbeTick();
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
    Log("BACKGROUND window kept inactive and off screen: %d of 6 import slots",KeepWindowInactive(edf));
    Log("BACKGROUND user32 diverted in the whole process: %d entries (aliases once)",KeepDesktop());
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
