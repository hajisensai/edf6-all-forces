// EDF6Autopilot: a test-only plugin (never shipped: tools/installer.py PLUGINS does not list it, CMake builds it as a
// MODULE into build/tools) that lets tests/autopilot/drive.py play the game in the background, for measurements
// (the user, 2026-10-07: "use code, or let it run in the background"; no keys sent to the desktop, no focus taken):
//  - EDF.dll reads the keyboard with GetKeyboardState / GetKeyState and checks GetForegroundWindow; those three imports
//    are pointed here: the game window counts as the foreground one, and the keys listed in <dll>.keys (hex virtual-key
//    codes, written by the driver) count as held, on top of the real ones;
//  - every second, and when the keys change, a row in <dll>.log: the game's committed memory (and its peak), its RAM,
//    and the machine's free RAM and commit, so a mission's load shows as a curve.
#include <Windows.h>
#include <psapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#pragma warning(push)
#pragma warning(disable:4201)
#include "PluginAPI.h"
#pragma warning(pop)

namespace {
HMODULE self=nullptr;
wchar_t keysPath[MAX_PATH]{},logPath[MAX_PATH]{};
volatile LONG held[256]{};
volatile HWND gameWindow=nullptr;
ULONGLONG started=0;

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
        Sleep(15);
    }
}
}  // namespace

extern "C" __declspec(dllexport) bool EDFMLAPI EML6_Load(PluginInfo* info) {
    if(!info)return false;
    started=GetTickCount64();
    GetModuleFileNameW(self,keysPath,MAX_PATH);
    wcscpy_s(logPath,keysPath);
    wchar_t* dot=wcsrchr(keysPath,L'.');wcscpy_s(dot,MAX_PATH-(dot-keysPath),L".keys");
    dot=wcsrchr(logPath,L'.');wcscpy_s(dot,MAX_PATH-(dot-logPath),L".log");
    info->infoVersion=PluginInfo::MaxInfoVer;info->name="EDF6 Autopilot (test only)";info->version=PLUG_VER(0,1,0,0);
    const HMODULE edf=GetModuleHandleW(L"EDF.dll");
    if(!edf){Log("no EDF.dll");return false;}
    realForeground=reinterpret_cast<ForegroundFn>(PatchImport(edf,"USER32.dll","GetForegroundWindow",reinterpret_cast<void*>(&Foreground)));
    realKeyboardState=reinterpret_cast<KeyboardStateFn>(PatchImport(edf,"USER32.dll","GetKeyboardState",reinterpret_cast<void*>(&KeyboardState)));
    realKeyState=reinterpret_cast<KeyStateFn>(PatchImport(edf,"USER32.dll","GetKeyState",reinterpret_cast<void*>(&KeyState)));
    Log("LOADED foreground=%d keyboardState=%d keyState=%d",realForeground!=nullptr,realKeyboardState!=nullptr,realKeyState!=nullptr);
    if(!realForeground)realForeground=&GetForegroundWindow;
    if(!realKeyboardState)realKeyboardState=&GetKeyboardState;
    if(!realKeyState)realKeyState=&GetKeyState;
    LogMemory("load");
    CloseHandle(CreateThread(nullptr,0,&Loop,nullptr,0,nullptr));
    return true;
}

BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if(reason==DLL_PROCESS_ATTACH){self=instance;DisableThreadLibraryCalls(instance);}
    return TRUE;
}
