// The player's dialogs about the mission packs (a room refused for its pack: src/mod_room.cpp, src/edf5campaign.cpp;
// a save whose rows cannot be moved: src/edf5campaign.cpp MigrateMissionPackSave; an older installer's ini). Each is
// shown on a thread of its own, so the caller is never held up by it: the game's menu thread inside an EOS call,
// EDF6Coop's EOS tick thread asking EDF6AF_RoomPackCompatible for a remembered room, the save load inside the
// script. The same message (by key) is not shown again while it is open, nor within its repeat time after (EDF6Coop
// asks about its remembered room at every room search). Header only: the tests compile the .cpp files that use it.
#pragma once
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <new>

namespace crew::notice {
constexpr std::uint64_t kOnce=~0ull;   // a repeat time: once per session
constexpr int kKeys=32;
struct Entry { std::uint64_t key=0,shown=0; bool open=false; };
struct State {
    SRWLOCK lock=SRWLOCK_INIT;
    Entry entries[kKeys]{};
    int next=0;
};
inline State& Shared() noexcept { static State s; return s; }
inline std::uint64_t (*nowMs)()=&GetTickCount64;   // the tests stand in their own clock
inline std::uint64_t Hash(const char* key) noexcept {
    std::uint64_t h=0xcbf29ce484222325ull;
    for(const char* p=key;p && *p;++p)h=(h^static_cast<unsigned char>(*p))*0x100000001b3ull;
    return h ? h : 1;
}
// Whether the message `key` may be shown now (and from now on it counts as open, until Closed).
inline bool Claim(const char* key,std::uint64_t repeatMs) noexcept {
    const std::uint64_t h=Hash(key),now=nowMs();
    State& s=Shared();
    AcquireSRWLockExclusive(&s.lock);
    Entry* e=nullptr;
    for(Entry& x:s.entries)if(x.key==h){e=&x;break;}
    bool ok=true;
    if(e)ok=!e->open && (repeatMs!=kOnce && now-e->shown>=repeatMs);
    else{e=&s.entries[s.next];s.next=(s.next+1)%kKeys;}
    if(ok)*e=Entry{h,now,true};
    ReleaseSRWLockExclusive(&s.lock);
    return ok;
}
inline void Closed(std::uint64_t h) noexcept {
    State& s=Shared();
    AcquireSRWLockExclusive(&s.lock);
    for(Entry& x:s.entries)if(x.key==h){x.open=false;x.shown=nowMs();}
    ReleaseSRWLockExclusive(&s.lock);
}
struct Dialog { std::uint64_t key; wchar_t title[96]; wchar_t text[1024]; };
inline DWORD WINAPI DialogThread(void* p) noexcept {
    Dialog* const d=static_cast<Dialog*>(p);
    // No owner window: the game's is another thread's (its message loop must not wait on this one). Topmost, so it
    // is seen over the game.
    MessageBoxW(nullptr,d->text,d->title,MB_OK|MB_ICONWARNING|MB_TOPMOST|MB_SETFOREGROUND);
    Closed(d->key);
    delete d;
    return 0;
}
// The dialog on a thread of its own; false when it could not be started (nothing shown, logged by the caller).
inline bool ShowAsync(std::uint64_t key,const wchar_t* title,const wchar_t* text) noexcept {
    Dialog* const d=new(std::nothrow) Dialog{};
    if(!d)return false;
    d->key=key;
    wcsncpy_s(d->title,title,_TRUNCATE);
    wcsncpy_s(d->text,text,_TRUNCATE);
    const HANDLE t=CreateThread(nullptr,0,&DialogThread,d,0,nullptr);
    if(!t){delete d;return false;}
    CloseHandle(t);
    return true;
}
// What shows a claimed message: ShowAsync in the game, a recorder in the tests (the message closes when it returns).
using Show=void(*)(const wchar_t* title,const wchar_t* text) noexcept;
inline Show show=nullptr;
// Shows `text` unless the same `key` is open or was shown less than `repeatMs` ago (kOnce: shown this session).
// Whether it was shown.
inline bool Post(const char* key,const wchar_t* title,const wchar_t* text,std::uint64_t repeatMs) noexcept {
    if(!Claim(key,repeatMs))return false;
    if(show){show(title,text);Closed(Hash(key));return true;}
    if(ShowAsync(Hash(key),title,text))return true;
    Closed(Hash(key));
    return false;
}
}  // namespace crew::notice
