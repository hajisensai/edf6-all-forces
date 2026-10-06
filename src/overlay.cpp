// The in-mission call pick (airstrike.cpp CallPick): its keys (ini CallNextKey / CallPrevKey, virtual-key
// codes, 0 = off) and the banner that shows what is picked. One thread does both for the plugin's life (the
// keys are read from the live config each poll, so turning a key on in the ini works without a restart): it
// polls the keys while
// one of the game's windows is in front (keys typed into another program never count), and owns the
// banner, a borderless layered window over the top of the game's window that never takes the focus or a
// click (WS_EX_NOACTIVATE, WS_EX_TRANSPARENT) and hides itself after kBannerMs. In exclusive full screen
// the game covers it; the pick still works and is logged (CALLS pick).
#include "crew.h"

#pragma comment(lib,"user32.lib")
#pragma comment(lib,"gdi32.lib")

namespace crew {
namespace {
constexpr DWORD kPollMs=30,kBannerMs=2500;
constexpr int kBannerW=640,kBannerH=60,kBannerTop=80;
constexpr wchar_t kClass[]=L"EDF6VehicleCrewBanner";

HWND banner=nullptr;
HFONT font=nullptr;
wchar_t text[128]{};
ULONGLONG shownAt=0;

bool GameInFront(HWND* game) noexcept {
    const HWND w=GetForegroundWindow();
    DWORD pid=0;
    if(!w || w==banner || !GetWindowThreadProcessId(w,&pid) || pid!=GetCurrentProcessId())return false;
    *game=w;
    return true;
}

LRESULT CALLBACK BannerProc(HWND w,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(msg!=WM_PAINT)return DefWindowProcW(w,msg,wp,lp);
    PAINTSTRUCT ps;
    const HDC dc=BeginPaint(w,&ps);
    RECT r;GetClientRect(w,&r);
    const HBRUSH back=CreateSolidBrush(RGB(20,24,32));
    FillRect(dc,&r,back);
    DeleteObject(back);
    SetBkMode(dc,TRANSPARENT);
    SetTextColor(dc,RGB(255,220,120));
    const HGDIOBJ old=SelectObject(dc,font);
    DrawTextW(dc,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    SelectObject(dc,old);
    EndPaint(w,&ps);
    return 0;
}

void MakeBanner() noexcept {
    WNDCLASSW wc{};
    wc.lpfnWndProc=BannerProc;
    wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=kClass;
    RegisterClassW(&wc);
    banner=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,kClass,L"",
                           WS_POPUP,0,0,kBannerW,kBannerH,nullptr,nullptr,wc.hInstance,nullptr);
    if(banner)SetLayeredWindowAttributes(banner,0,225,LWA_ALPHA);
    font=CreateFontW(30,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Microsoft YaHei");
}

void Show(HWND game) noexcept {
    if(!banner)return;
    RECT r;
    if(!GetClientRect(game,&r))return;
    POINT top{(r.left+r.right)/2-kBannerW/2,r.top+kBannerTop};
    ClientToScreen(game,&top);
    SetWindowPos(banner,HWND_TOPMOST,top.x,top.y,kBannerW,kBannerH,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    InvalidateRect(banner,nullptr,TRUE);
    shownAt=GetTickCount64();
}

DWORD WINAPI PickerThread(void*) {
    MakeBanner();
    bool nextDown=false,prevDown=false;
    for(;;) {
        MSG m;
        while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}
        if(banner && shownAt && GetTickCount64()-shownAt>kBannerMs){ShowWindow(banner,SW_HIDE);shownAt=0;}
        HWND game=nullptr;
        const Config& cfg=Cfg();   // one snapshot per poll
        const bool front=cfg.enabled && !MapHoldsKeys() && GameInFront(&game);   // the map view holds the keys (map.cpp)
        const bool next=front && cfg.callNextKey && (GetAsyncKeyState(static_cast<int>(cfg.callNextKey))&0x8000);
        const bool prev=front && cfg.callPrevKey && (GetAsyncKeyState(static_cast<int>(cfg.callPrevKey))&0x8000);
        const int step=(next && !nextDown) ? 1 : (prev && !prevDown) ? -1 : 0;
        nextDown=next;prevDown=prev;
        if(step) {
            CallPick(step,text,sizeof(text)/sizeof(text[0]));
            Show(game);
        }
        Sleep(kPollMs);
    }
}
}  // namespace

void StartCallPicker() noexcept {
    const HANDLE t=CreateThread(nullptr,0,PickerThread,nullptr,0,nullptr);
    if(t)CloseHandle(t);
    Log("CALLS pick thread %s (keys from the ini: next=%#lx prev=%#lx, 0 = off)",t ? "on" : "could not start",Cfg().callNextKey,
        Cfg().callPrevKey);
}
}  // namespace crew
