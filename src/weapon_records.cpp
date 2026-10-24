// Weapon records past 2048 (docs/loadout-re.md 8.8; the layout and the pure part: src/weapon_records_logic.h).
//
// Relocation ported from "EDF6 WeaponLimit" v1.0, Copyright (c) 2026 AnonymousHello, MIT License (the full notice:
// third_party/EDF6WeaponLimit/LICENSE). Its persistence (a polling thread OR-merging into a file), its signature
// checks and its widened weapon-select copy are replaced here by:
//  - 101 instruction-exact constant patches (weapon_records_sites.inc), all verified before the first write, applied
//    together or rolled back together, behind EDF.dll's TimeDateStamp gate (plugin.cpp IdentifyImage).
//  - The game status's allocation grows (12 sites in the list) and its constructor's block init (0xD6468) is followed
//    by zeroing the relocated table: the block init itself (0xDEDA0) also initialises stack and record copies.
//  - The block's own copies, wrapped where the game makes them (all calls of memcpy with 0xDCE8 bytes):
//      out of the live block (rows 0..0x7FF mirrored into the block's old table first): the four status record
//      builders (0x70AF25, 0x70B079, 0x70B61C, 0x70B79C; rows 0x800.. remembered per record), MAIN.GST's save request
//      (0x7C2F66 -> 0x7BCD40, its job copies the block; rows 0x800.. remembered by the hash of the copied old table,
//      a write taking the newest request with its hash: weapon_records_logic.h PickSave);
//      into it (rows 0..0x7FF read back from it): the three record restores (0x70B23A, 0x70B421, 0x70B956; rows
//      0x800.. from what that record remembered), the new game's template (0xDB96D; rows 0x800.. zero) and the load
//      job's final copy of a decoded file (0x7C1AD1, on the save thread; rows 0x800.. from the sidecar);
//      the weapon select screen's snapshot (0x8B49B1): its two reads of the snapshot's table (0x8B4F70, 0x8B51A0, both
//      reached only after that copy in the same call) are redirected to a copy of the relocated table made there.
//    The one such copy left as it is: the screen's write-back 0x8BD7FE (the whole block from its snapshot): it touches
//    the old table only, which nothing reads before MirrorToBlock rewrites it (docs/loadout-re.md 8.8).
//  - The sidecar (MAIN.GST.edf6vc next to MAIN.GST): written when the save job's write of MAIN.GST succeeds (0x7C228B
//    -> 0x7B9990), read when the load job copies MAIN.GST into the block, deleted with MAIN.GST (the two DeleteFileW
//    calls 0x7B69F1, 0x7B7FEB). The paths are the game's own: its CreateFileW calls in the read (0x7B90CD) and the
//    write (0x7B9C28) are wrapped to remember them per thread.
// The network: nothing to do. Rooms check content numbers (docs/loadout-re.md 8.5), member records carry weapon ids
// as int32 and the equipped weapons' stars by value (0x78E9A0 reads them from the relocated table: patched).
#include "weapon_records.h"
#include "weapon_records_logic.h"
#include "crew.h"
#include "memory.h"
#include "pack_notice.h"
#include "edf/patch.h"
#include <atomic>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <vector>

namespace crew {
namespace {
using namespace wrec;

constexpr unsigned kGameStatus=0x20B2890;
constexpr unsigned kMemcpy=0x12DA77A;                     // jmp [memcpy]
constexpr unsigned kBlockInit=0xDEDA0,kBlockInitCall=0xD6468;   // the game status's constructor 0xD62C0
constexpr unsigned kOutCalls[]={0x70AF25,0x70B079,0x70B61C,0x70B79C};   // memcpy(record, GS block, 0xDCE8)
constexpr unsigned kInCalls[]={0x70B23A,0x70B421,0x70B956};            // memcpy(GS block, record, 0xDCE8)
constexpr unsigned kNewGameCall=0xDB96D;                  // 0xDB920: memcpy(GS block, template, 0xDCE8)
constexpr unsigned kLoadCopyCall=0x7C1AD1;                // the load job 0x7C1670: memcpy(dest, decoded, size)
constexpr unsigned kUiSnapCall=0x8B49B1;                  // HUiHQWeaponSelect 0x8B4280: memcpy(screen+0x800, block, 0xDCE8)
constexpr unsigned kSaveCall=0x7C2F66,kSave=0x7BCD40;     // MAIN.GST's save request (0x7C2E30), 9 arguments
constexpr unsigned kWriteCall=0x7C228B,kWrite=0x7B9990;   // the save job 0x7C1CB0 writes a file
constexpr unsigned kReadOpen=0x7B90CD,kWriteOpen=0x7B9C28;   // call [CreateFileW] in 0x7B8DE0 / 0x7B9990
constexpr unsigned kDeleteCalls[]={0x7B69F1,0x7B7FEB};    // call [DeleteFileW] in 0x7B61B0 / 0x7B7EC0
constexpr unsigned kUiOwned=0x8B4F70,kUiStatus=0x8B51A0;  // the screen's two reads of its snapshot's table
constexpr std::size_t kSaveSourceArg=5;                   // 0x7BCD40's sixth argument: the block it saves
// 0x7B9990(path, desc) writes desc's parts one after another (its loop 0x7B9CB0..0x7B9E0E): desc+8 -> {data, size}
// a part (16 bytes), desc+0x18 their count.
constexpr std::size_t kDescParts=8,kDescCount=0x18,kPartBytes=16,kMostParts=64;
constexpr wchar_t kMainSave[]=L"MAIN.GST";
constexpr wchar_t kSidecarSuffix[]=L".edf6vc";

const unsigned char kReadOpenSig[]={0xFF,0x15,0x8D,0xC5,0xF9,0x00};
const unsigned char kWriteOpenSig[]={0xFF,0x15,0x32,0xBA,0xF9,0x00};
const unsigned char kDeleteSigs[2][6]={{0xFF,0x15,0x61,0xEC,0xF9,0x00},{0xFF,0x15,0x67,0xD6,0xF9,0x00}};
const unsigned char kUiOwnedSig[]={0x0F,0xB6,0x84,0x8B,0xE8,0x7C,0x00,0x00};    // movzx eax, byte [rbx+rcx*4+0x7CE8]
const unsigned char kUiStatusSig[]={0x41,0x8B,0x94,0x8D,0xE8,0x84,0x00,0x00};   // mov edx, [r13+rcx*4+0x84E8]

std::atomic<bool> active{false};
SRWLOCK lock=SRWLOCK_INIT;
struct Locked { Locked(){AcquireSRWLockExclusive(&lock);} ~Locked(){ReleaseSRWLockExclusive(&lock);} };

// The weapon select screen's table (filled at its snapshot, read by the two redirected loads in the same call).
alignas(16) unsigned char uiTable[kTableBytes];

// Rows 0x800.. at a copy out of the block, for the copy coming back: a status record (by its data pointer) or a save
// job (by the hash of the old table it copied).
struct Kept { const void* record=nullptr; std::uint64_t hash=0; std::vector<std::uint8_t> ext; };
constexpr std::size_t kMostRecords=16,kMostSaves=8;
std::vector<Kept> records,saves;

using CopyFn=void*(__cdecl*)(void*,const void*,std::size_t);
using BlockInitFn=void*(__fastcall*)(unsigned char*);
using SaveFn=std::uintptr_t(__fastcall*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t,
                                        std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
using WriteFn=int(__fastcall*)(void*,void*);
using CreateFn=HANDLE(WINAPI*)(LPCWSTR,DWORD,DWORD,LPSECURITY_ATTRIBUTES,DWORD,DWORD,HANDLE);
using DeleteFn=BOOL(WINAPI*)(LPCWSTR);
// What the game's import slots held (EDF Mod Loader may redirect file APIs there): set by RedirectImportCall before it
// makes the call sites reach the hooks that call through them.
void* gameCreate=nullptr;
void* gameDelete=nullptr;

thread_local wchar_t readPath[MAX_PATH];
thread_local wchar_t writePath[MAX_PATH];

unsigned char* Gs() noexcept { return At<unsigned char*>(image,kGameStatus); }
bool LiveBlock(const void* p,std::size_t n) noexcept {
    unsigned char* const gs=Gs();
    return gs && p==gs+kBlock && n==kBlockSize;
}

void Remember(wchar_t* into,LPCWSTR path) noexcept {
    into[0]=0;
    if(path && wcsnlen_s(path,MAX_PATH)<MAX_PATH)wcscpy_s(into,MAX_PATH,path);
}
bool IsMainSave(const wchar_t* path) noexcept {
    const std::size_t n=wcsnlen_s(path,MAX_PATH),m=std::size(kMainSave)-1;
    if(n<=m || (path[n-m-1]!=L'\\' && path[n-m-1]!=L'/'))return false;
    return _wcsicmp(path+n-m,kMainSave)==0;
}
bool SidecarOf(const wchar_t* save,wchar_t* out) noexcept {
    return wcscpy_s(out,MAX_PATH,save)==0 && wcscat_s(out,MAX_PATH,kSidecarSuffix)==0;
}

// --- the sidecar file ---
// A sidecar the last load could not use whole (unreadable, damaged, or from a build keeping more rows) is moved
// aside to <sidecar>.unread before the save block is filled; when even that fails it is held: no save of this session
// deletes or overwrites it (its rows would be lost for good) until a load reads one or a new game starts.
constexpr wchar_t kUnreadSuffix[]=L".unread";
bool held=false;   // under the lock

bool WriteSidecar(const wchar_t* save,const std::uint8_t* ext) noexcept {
    wchar_t path[MAX_PATH],temp[MAX_PATH];
    if(!SidecarOf(save,path) || wcscpy_s(temp,path) || wcscat_s(temp,L".tmp"))return false;
    if(held) {
        Log("WEAPONREC save %ls: the sidecar the load could not read is kept as it is (rows past 0x800 of this session not saved)",save);
        return false;
    }
    const std::uint32_t rows=UsedRows(ext);
    if(!rows) {   // nothing past 0x800: no file (and none left over from an earlier save)
        if(DeleteFileW(path)){Log("WEAPONREC save %ls: no rows past 0x800, sidecar removed",save);return true;}
        return GetLastError()==ERROR_FILE_NOT_FOUND;
    }
    static std::uint8_t file[kSidecarSize];   // under the lock (the save job's thread)
    const std::size_t size=Serialize(ext,file);
    HANDLE h=CreateFileW(temp,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE){Log("WEAPONREC save %ls: cannot create the sidecar (%lu)",save,GetLastError());return false;}
    DWORD written=0;
    const bool ok=WriteFile(h,file,static_cast<DWORD>(size),&written,nullptr) && written==size && FlushFileBuffers(h);
    CloseHandle(h);
    if(!ok || !MoveFileExW(temp,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        Log("WEAPONREC save %ls: writing the sidecar failed (%lu)",save,GetLastError());
        DeleteFileW(temp);
        return false;
    }
    Log("WEAPONREC save %ls: %u rows past 0x800 in the sidecar",save,rows);
    return true;
}
// The sidecar the load could not use whole: moved aside (<sidecar>.unread, an earlier one replaced), else held.
// `owned`: what of it this session has (all of it unusable: none).
void SetAside(const wchar_t* save,const wchar_t* path,const char* why,const char* owned) noexcept {
    wchar_t aside[MAX_PATH];
    if(!wcscpy_s(aside,path) && !wcscat_s(aside,kUnreadSuffix) && MoveFileExW(path,aside,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        Log("WEAPONREC load %ls: the sidecar %s: %s; the file is kept as %ls",save,why,owned,aside);
        return;
    }
    held=true;
    Log("WEAPONREC load %ls: the sidecar %s and cannot be moved aside (%lu): %s; no save until the next load deletes or "
        "overwrites it",save,why,GetLastError(),owned);
}
void ReadSidecar(const wchar_t* save,std::uint8_t* ext) noexcept {
    std::memset(ext,0,kExtBytes);
    held=false;
    wchar_t path[MAX_PATH];
    if(!SidecarOf(save,path))return;
    HANDLE h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(h==INVALID_HANDLE_VALUE) {
        const DWORD e=GetLastError();
        if(e==ERROR_FILE_NOT_FOUND)Log("WEAPONREC load %ls: no sidecar (no rows past 0x800)",save);
        else {   // there but not readable now (a scanner holding it, its rights): never take it for "no rows"
            held=true;
            Log("WEAPONREC load %ls: cannot open the sidecar (%lu): rows past 0x800 not owned; no save until the next load "
                "deletes or overwrites it",save,e);
        }
        return;
    }
    // The whole file: one from a build keeping more rows is longer than ours.
    LARGE_INTEGER size{};
    std::vector<std::uint8_t> file;
    DWORD read=0;
    bool ok=GetFileSizeEx(h,&size) && size.QuadPart>=0 && size.QuadPart<=0x4000000;
    try { if(ok)file.resize(static_cast<std::size_t>(size.QuadPart)); } catch(...) { ok=false; }
    ok=ok && (file.empty() || ReadFile(h,file.data(),static_cast<DWORD>(file.size()),&read,nullptr)) && read==file.size();
    CloseHandle(h);
    const Parse p=ok ? Deserialize(file.data(),file.size(),ext) : Parse::kShort;
    const std::uint32_t rows=p==Parse::kOk ? SidecarRows(file.data(),file.size()) : 0;
    char why[96];
    if(p!=Parse::kOk) {
        std::snprintf(why,sizeof(why),"is unusable (%s)",ok ? ParseName(p) : "not read whole");
        SetAside(save,path,why,"rows past 0x800 not owned");
    } else if(rows>kSlots-kNativeSlots) {   // this build keeps fewer: saving over it would drop the rest for good
        std::snprintf(why,sizeof(why),"has %u rows past 0x800, this build keeps %u",rows,kSlots-kNativeSlots);
        SetAside(save,path,why,"the rows this build keeps are owned");
    } else Log("WEAPONREC load %ls: %u rows past 0x800 from the sidecar",save,rows);
}

void Keep(std::vector<Kept>& list,std::size_t most,const void* record,std::uint64_t hash,const std::uint8_t* ext) noexcept {
    Locked l;
    try {
        for(auto it=list.begin();it!=list.end();++it)
            if(record && it->record==record){list.erase(it);break;}
        if(list.size()>=most)list.erase(list.begin());
        Kept k;k.record=record;k.hash=hash;k.ext.assign(ext,ext+kExtBytes);
        list.push_back(std::move(k));
    } catch(...) { Log("WEAPONREC out of memory keeping rows past 0x800"); }
}
// The save block among what the save job writes (0x7B9990's desc): its one part of kBlockSize bytes, nullptr when
// there is not exactly one (`count`: the parts, -1 when the desc is not readable).
const unsigned char* SavedBlock(void* desc,long long* count) noexcept {
    *count=-1;
    __try {
        if(!Readable(desc,kDescCount+8))return nullptr;
        auto* const parts=At<unsigned char*>(desc,kDescParts);
        const auto n=At<std::uint64_t>(desc,kDescCount);
        if(!parts || n==0 || n>kMostParts || !Readable(parts,static_cast<std::size_t>(n)*kPartBytes))return nullptr;
        *count=static_cast<long long>(n);
        const unsigned char* found=nullptr;
        for(std::uint64_t i=0;i<n;++i) {
            if(At<std::uint64_t>(parts,i*kPartBytes+8)!=kBlockSize)continue;
            if(found)return nullptr;   // two of that size: which one MAIN.GST's block is cannot be told
            found=At<const unsigned char*>(parts,i*kPartBytes);
        }
        return found && Readable(found,kBlockSize) ? found : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// --- copies out of the block ---
void* __cdecl OutCopyHook(void* dst,const void* src,std::size_t n) {
    unsigned char* const gs=Gs();
    const bool live=active.load(std::memory_order_acquire) && LiveBlock(src,n);
    if(live)MirrorToBlock(gs);
    std::memcpy(dst,src,n);
    if(live)Keep(records,kMostRecords,dst,0,Ext(gs));
    return dst;
}
std::uintptr_t __fastcall SaveHook(std::uintptr_t a,std::uintptr_t b,std::uintptr_t c,std::uintptr_t d,std::uintptr_t e,
                                   std::uintptr_t source,std::uintptr_t g,std::uintptr_t h,std::uintptr_t i) {
    static_assert(kSaveSourceArg==5,"the save request's block is its sixth argument");
    unsigned char* const gs=Gs();
    if(active.load(std::memory_order_acquire) && gs && source==reinterpret_cast<std::uintptr_t>(gs+kBlock)) {
        MirrorToBlock(gs);   // before the job copies the block: MAIN.GST gets rows 0..0x7FF
        Keep(saves,kMostSaves,nullptr,Hash(gs+kOldTable,kNativeBytes),Ext(gs));
    }
    return reinterpret_cast<SaveFn>(image+kSave)(a,b,c,d,e,source,g,h,i);
}

// --- copies into the block ---
void* __cdecl InCopyHook(void* dst,const void* src,std::size_t n) {
    std::memcpy(dst,src,n);
    unsigned char* const gs=Gs();
    if(!active.load(std::memory_order_acquire) || !LiveBlock(dst,n))return dst;
    ReadFromBlock(gs);
    bool found=false;
    {
        Locked l;
        for(const Kept& k:records)if(k.record==src){std::memcpy(Ext(gs),k.ext.data(),kExtBytes);found=true;break;}
    }
    if(!found)Log("WEAPONREC a status record %p restored that this session never saw made: rows past 0x800 left as they were",src);
    return dst;
}
void* __cdecl NewGameCopyHook(void* dst,const void* src,std::size_t n) {
    std::memcpy(dst,src,n);
    unsigned char* const gs=Gs();
    if(active.load(std::memory_order_acquire) && LiveBlock(dst,n)) {
        ReadFromBlock(gs);
        std::memset(Ext(gs),0,kExtBytes);
        Locked l;
        held=false;   // a new game's first save replaces MAIN.GST, as the game's own
    }
    return dst;
}
void* __cdecl LoadCopyHook(void* dst,const void* src,std::size_t n) {
    std::memcpy(dst,src,n);
    unsigned char* const gs=Gs();
    if(!active.load(std::memory_order_acquire) || !LiveBlock(dst,n))return dst;
    ReadFromBlock(gs);
    if(IsMainSave(readPath)){Locked l;ReadSidecar(readPath,Ext(gs));}
    else {
        std::memset(Ext(gs),0,kExtBytes);
        Log("WEAPONREC a load filled the save block from \"%ls\", not a MAIN.GST: rows past 0x800 cleared",readPath);
    }
    return dst;
}
void* __cdecl UiSnapHook(void* dst,const void* src,std::size_t n) {
    std::memcpy(dst,src,n);
    if(!active.load(std::memory_order_acquire) || n!=kBlockSize)return dst;
    unsigned char* const gs=Gs();
    if(LiveBlock(src,n))std::memcpy(uiTable,gs+kNewTable,kTableBytes);
    else {   // the screen's InitParam named another block (+0xF0): it has no rows past 0x800
        std::memcpy(uiTable,static_cast<const unsigned char*>(src)+kOldInBlock,kNativeBytes);
        std::memset(uiTable+kNativeBytes,0,kExtBytes);
    }
    return dst;
}
void* __fastcall BlockInitHook(unsigned char* block) {
    void* const r=reinterpret_cast<BlockInitFn>(image+kBlockInit)(block);
    // Only the game status's constructor calls from here: the object was allocated with room for the table.
    if(active.load(std::memory_order_acquire))std::memset(block+kNewInBlock,0,kTableBytes);
    return r;
}

// --- the files ---
int __fastcall WriteHook(void* path,void* desc) {
    const int r=reinterpret_cast<WriteFn>(image+kWrite)(path,desc);
    if(!active.load(std::memory_order_acquire) || !IsMainSave(writePath))return r;
    long long parts=0;
    const unsigned char* const data=SavedBlock(desc,&parts);
    if(!data) {   // never silent: without it no save of this session writes the sidecar
        Log("WEAPONREC save %ls: no single 0x%zX-byte save block among the %lld parts written: the sidecar is NOT written",
            writePath,kBlockSize,parts);
        return r;
    }
    const std::uint64_t hash=Hash(data+kOldInBlock,kNativeBytes);
    Locked l;
    // The newest request with this table (weapon_records_logic.h PickSave: a job that ended before its write, or two
    // queued with the same rows 0..0x7FF, never pair a write with an older request's rows 0x800..). The earlier ones
    // with this table go; the newest stays for its own write.
    std::uint64_t hashes[kMostSaves]{};
    const std::size_t n=saves.size()<kMostSaves ? saves.size() : kMostSaves;
    for(std::size_t i=0;i<n;++i)hashes[i]=saves[i].hash;
    const std::size_t at=PickSave(hashes,n,hash);
    if(at==n) {
        if(r>=0)Log("WEAPONREC save %ls: written without a request this session saw: the sidecar is left as it was",writePath);
        return r;
    }
    if(r>=0)WriteSidecar(writePath,saves[at].ext.data());
    else Log("WEAPONREC save %ls failed (%d): the sidecar is left as it was",writePath,r);
    for(std::size_t i=at;i-->0;)if(saves[i].hash==hash)saves.erase(saves.begin()+static_cast<std::ptrdiff_t>(i));
    return r;
}
HANDLE WINAPI ReadOpenHook(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD disposition,DWORD flags,HANDLE tmpl) {
    Remember(readPath,path);
    return reinterpret_cast<CreateFn>(gameCreate)(path,access,share,sa,disposition,flags,tmpl);
}
HANDLE WINAPI WriteOpenHook(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES sa,DWORD disposition,DWORD flags,HANDLE tmpl) {
    Remember(writePath,path);
    return reinterpret_cast<CreateFn>(gameCreate)(path,access,share,sa,disposition,flags,tmpl);
}
BOOL WINAPI DeleteHook(LPCWSTR path) {
    const BOOL ok=reinterpret_cast<DeleteFn>(gameDelete)(path);
    if(ok && path && IsMainSave(path)) {
        wchar_t sidecar[MAX_PATH];
        if(SidecarOf(path,sidecar) && DeleteFileW(sidecar))Log("WEAPONREC %ls deleted: its sidecar too",path);
    }
    return ok;
}

// An import call (FF 15 rel32, 6 bytes) made a direct call to `hook` (E8 rel32 to a near thunk, then a nop).
// `*slotValue`: what the import slot held (the hook calls through it), set before the site is patched.
bool RedirectImportCall(unsigned rva,const unsigned char* sig,void* hook,void** slotValue) noexcept {
    unsigned char* const site=image+rva;
    std::int32_t rel=0;
    std::memcpy(&rel,sig+2,4);
    void* const* slot=reinterpret_cast<void* const*>(site+6+rel);
    if(!Readable(slot,8) || !*slot)return false;
    if(*slotValue && *slotValue!=*slot)return false;   // both sites of a pair use one slot
    *slotValue=*slot;
    void* const thunk=AllocateNearThunk(site,hook);
    if(!thunk)return false;
    const auto delta=static_cast<unsigned char*>(thunk)-(site+5);
    if(delta>0x7FFFFFF0 || delta<-0x7FFFFFF0)return false;
    unsigned char code[6]={0xE8,0,0,0,0,0x90};
    const auto rel32=static_cast<std::int32_t>(delta);
    std::memcpy(code+1,&rel32,4);
    return edf::PatchCode(site,sig,code,sizeof(code));
}

// The screen's reads: `load` (the stub's register setup, 10 bytes `mov reg, uiTable` + the load) then a jump back.
void* UiStub(unsigned rva,const unsigned char* load,std::size_t loadSize) noexcept {
    unsigned char code[48]{};
    std::size_t n=0;
    std::memcpy(code+n,load,loadSize);n+=loadSize;
    const unsigned char jump[]={0xFF,0x25,0,0,0,0};
    std::memcpy(code+n,jump,sizeof(jump));n+=sizeof(jump);
    const std::uint64_t back=reinterpret_cast<std::uint64_t>(image+rva+8);
    std::memcpy(code+n,&back,8);n+=8;
    return AllocateNearCode(image+rva,code,n);
}
bool JumpCode(unsigned rva,void* stub,unsigned char* out) noexcept {   // E9 rel32 + a 3-byte nop over the 8-byte load
    const auto delta=static_cast<unsigned char*>(stub)-(image+rva+5);
    if(!stub || delta>0x7FFFFFF0 || delta<-0x7FFFFFF0)return false;
    const auto rel32=static_cast<std::int32_t>(delta);
    out[0]=0xE9;std::memcpy(out+1,&rel32,4);out[5]=0x0F;out[6]=0x1F;out[7]=0x00;
    return true;
}

bool Redirect(unsigned site,unsigned target,void* hook) noexcept {
    bool changed=false;
    if(RedirectCall(image+site,image+target,hook,changed))return true;
    Log("WEAPONREC the call at EDF+0x%X (to 0x%X) is not the game's (another hook?)",site,target);
    return false;
}
}  // namespace

bool WeaponRecordsActive() noexcept { return active.load(std::memory_order_acquire); }
std::uint32_t WeaponRecordCapacity() noexcept { return WeaponRecordsActive() ? kSlots : kNativeSlots; }
unsigned char* WeaponRecord(unsigned char* gs,std::uint32_t id) noexcept {
    if(!gs || id>=WeaponRecordCapacity())return nullptr;
    return gs+(WeaponRecordsActive() ? kNewTable : kOldTable)+std::size_t{id}*kRecord;
}
void WeaponRecordsCheckRows(std::uint32_t rows) noexcept {
    const std::uint32_t most=WeaponRecordCapacity();
    if(rows<=most)return;
    static std::atomic<std::uint32_t> logged{0};
    if(logged.exchange(rows)!=rows)Log("WEAPONREC the weapon table has %u rows, the game status holds %u records: the game writes past them (%s)",
                                       rows,most,WeaponRecordsActive() ? "more rows than this build supports" : "the relocation was refused, see above");
    wchar_t text[400];
    std::swprintf(text,std::size(text),L"武器表有 %u 行，但游戏只能记录 %u 把武器的拥有状态%ls。超出的武器会写坏内存，可能导致闪退或存档损坏。\n\n"
        L"请重新运行安装器，或把日志（EDF6VehicleCrew.log 里 WEAPONREC 开头的行）发给作者。",rows,most,
        WeaponRecordsActive() ? L"" : L"（扩展武器记录的补丁没有装上）");
    notice::Post("weapon-records-rows",L"全军出击：武器数量超出上限",text,notice::kOnce);
}

bool InstallWeaponRecords() noexcept {
    if(GetModuleHandleW(L"edf6_weaponlimit.dll")) {
        Log("WEAPONREC REFUSED: edf6_weaponlimit.dll is loaded and relocates the same records; remove one of the two");
        return false;
    }
    if(Gs()) {
        Log("WEAPONREC REFUSED: the game status exists already (allocated with room for 0x800 records only)");
        return false;
    }
    // 1. Everything checked, nothing written.
    for(const Site& s:kSites) {
        if(!SiteConsistent(s) || !Matches(s.rva,s.code,s.size)) {
            Log("WEAPONREC REFUSED: code at EDF+0x%X is not the expected instruction (another patch there?)",s.rva);
            return false;
        }
    }
    if(!Matches(kReadOpen,kReadOpenSig,6) || !Matches(kWriteOpen,kWriteOpenSig,6) || !Matches(kDeleteCalls[0],kDeleteSigs[0],6) ||
       !Matches(kDeleteCalls[1],kDeleteSigs[1],6) || !Matches(kUiOwned,kUiOwnedSig,8) || !Matches(kUiStatus,kUiStatusSig,8)) {
        Log("WEAPONREC REFUSED: the save files' calls or the weapon select screen's reads are not the expected code");
        return false;
    }
    // 2. The wrappers (pass-through until active).
    bool hooks=true;
    for(unsigned site:kOutCalls)hooks=Redirect(site,kMemcpy,reinterpret_cast<void*>(&OutCopyHook)) && hooks;
    for(unsigned site:kInCalls)hooks=Redirect(site,kMemcpy,reinterpret_cast<void*>(&InCopyHook)) && hooks;
    hooks=Redirect(kNewGameCall,kMemcpy,reinterpret_cast<void*>(&NewGameCopyHook)) && hooks;
    hooks=Redirect(kLoadCopyCall,kMemcpy,reinterpret_cast<void*>(&LoadCopyHook)) && hooks;
    hooks=Redirect(kUiSnapCall,kMemcpy,reinterpret_cast<void*>(&UiSnapHook)) && hooks;
    hooks=Redirect(kBlockInitCall,kBlockInit,reinterpret_cast<void*>(&BlockInitHook)) && hooks;
    hooks=Redirect(kSaveCall,kSave,reinterpret_cast<void*>(&SaveHook)) && hooks;
    hooks=Redirect(kWriteCall,kWrite,reinterpret_cast<void*>(&WriteHook)) && hooks;
    bool files=RedirectImportCall(kReadOpen,kReadOpenSig,reinterpret_cast<void*>(&ReadOpenHook),&gameCreate);
    files=files && RedirectImportCall(kWriteOpen,kWriteOpenSig,reinterpret_cast<void*>(&WriteOpenHook),&gameCreate);
    files=files && RedirectImportCall(kDeleteCalls[0],kDeleteSigs[0],reinterpret_cast<void*>(&DeleteHook),&gameDelete);
    files=files && RedirectImportCall(kDeleteCalls[1],kDeleteSigs[1],reinterpret_cast<void*>(&DeleteHook),&gameDelete);
    if(!hooks || !files) {
        Log("WEAPONREC REFUSED: block copies hooked=%d save files hooked=%d; the records stay where the game keeps them (0x800)",hooks,files);
        return false;
    }
    // 3. The relocation: every constant and the screen's two reads, or none of them.
    const std::uint64_t table=reinterpret_cast<std::uint64_t>(uiTable);
    unsigned char ownedLoad[14]={0x48,0xB8};                 // mov rax, uiTable
    std::memcpy(ownedLoad+2,&table,8);
    const unsigned char ownedTail[]={0x0F,0xB6,0x04,0x88};   // movzx eax, byte [rax+rcx*4]
    std::memcpy(ownedLoad+10,ownedTail,4);
    unsigned char statusLoad[13]={0x48,0xBA};                // mov rdx, uiTable
    std::memcpy(statusLoad+2,&table,8);
    const unsigned char statusTail[]={0x8B,0x14,0x8A};       // mov edx, [rdx+rcx*4]
    std::memcpy(statusLoad+10,statusTail,3);
    unsigned char ownedJump[8],statusJump[8];
    if(!JumpCode(kUiOwned,UiStub(kUiOwned,ownedLoad,sizeof(ownedLoad)),ownedJump) ||
       !JumpCode(kUiStatus,UiStub(kUiStatus,statusLoad,sizeof(statusLoad)),statusJump)) {
        Log("WEAPONREC REFUSED: no code page near the weapon select screen");
        return false;
    }
    std::size_t applied=0;
    for(;applied<kSiteCount;++applied) {
        unsigned char patched[12];
        Patched(kSites[applied],patched);
        if(!edf::PatchCode(image+kSites[applied].rva,kSites[applied].code,patched,kSites[applied].size))break;
    }
    const bool constants=applied==kSiteCount;
    const bool owned=constants && edf::PatchCode(image+kUiOwned,kUiOwnedSig,ownedJump,8);
    const bool status=owned && edf::PatchCode(image+kUiStatus,kUiStatusSig,statusJump,8);
    if(!status) {   // back to the game's code: a half-moved table is worse than none
        std::size_t stuck=0;
        for(std::size_t i=0;i<applied;++i) {
            unsigned char patched[12];
            Patched(kSites[i],patched);
            if(!edf::PatchCode(image+kSites[i].rva,patched,kSites[i].code,kSites[i].size)) {
                ++stuck;
                Log("WEAPONREC putting back EDF+0x%X failed",kSites[i].rva);
            }
        }
        if(owned && !edf::PatchCode(image+kUiOwned,ownedJump,kUiOwnedSig,8)){++stuck;Log("WEAPONREC putting back EDF+0x%X failed",kUiOwned);}
        if(stuck) {   // part of the table's code moved and part not: no save is safe; say so where the player sees it
            Log("WEAPONREC REFUSED: writing the code failed at %s and %zu sites could not be put back: the game's weapon records are "
                "inconsistent this session",constants ? "the weapon select screen" : "a constant",stuck);
            notice::Post("weapon-records-stuck",L"全军出击：武器记录补丁回滚失败",
                         L"扩展武器记录的补丁写入失败，而且没能完全撤回。请不要存档，立即退出游戏，并把 EDF6VehicleCrew.log 发给作者。",
                         notice::kOnce);
            return false;
        }
        Log("WEAPONREC REFUSED: writing the code failed at %s; everything put back",
            constants ? "the weapon select screen" : "a constant");
        return false;
    }
    active.store(true,std::memory_order_release);
    Log("WEAPONREC installed: %zu constants relocated (records at GS+0x%zX, %u of them, object 0x%zX bytes), %zu block copies, "
        "the save files' calls, the weapon select screen; sidecar MAIN.GST%ls",kSiteCount,kNewTable,kSlots,kNewObjectSize,
        std::size(kOutCalls)+std::size(kInCalls)+4,kSidecarSuffix);
    return true;
}
}  // namespace crew
