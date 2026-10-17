// EDF4.1's weapon sound effects and voices on EDF6 (plan P7). Research: jobs/4bf89026/tmp/wsound41/NOTES.md (weapon SE)
// and tmp/voice41/NOTES.md (voices); RVAs are for EDF.dll TimeDateStamp 0x678CCB46.
//
// Weapon SE. A shot asks the sound system for its cue by name: FireSe -> 0x7B3EA0 -> the lookup 0x7AEAE0(system, out, type, name)
// walks the std::set<Bank*> at system+0x98 (system = *(EDF+0x20B2948)), skips every bank whose type (bank+4) is not `type`
// (the SE lookups all ask type 0) and answers {Bank*, cue index} for the first bank that knows the name (0x7A9F80). 4.1's
// weapon cues (332 that EDF6 lacks, the other 1087 of its 1469 colliding with EDF6's by name) live in one bank the installer
// ships as SOUND/PC/EDF41_WEAPON_SE.ACB (hash-patched, own bank name). Loading it like EDF6's own SE bank (0x7B2190, which
// stores it in a member of the sound object) would replace that bank; loading it like an object's game_sound bank, through
// the cached loader 0x7AA440(out, wstring path, type, 1, byte) (0x5466DF), adds one to the set. A bank's type is only ever
// compared by that lookup (0x7AEBBF; the others store it for re-creation, 0x7A92A4 / 0x7A9BB1), so our bank gets a type no
// caller asks for: the stock lookups never see it, and EDF6's own cues always answer first. The lookup is then wrapped: when
// it finds nothing for type 0 and our bank is loaded, the name is tried on our bank alone (0x7A9F80) and `out` filled the way
// the lookup does (bank pointer, then the index dword). The lookup has nine callers, all calls (0x7B2984 ... 0x7B46B5): each
// is redirected, rather than patching the function's entry, because RedirectCall verifies the target it replaces and needs no
// prologue copy. The bank loads right after EDF6's own SE bank (the call at 0x7B09C6, whose callee we run first): the sound
// system has just loaded a bank through the same cache (0x7B095C), so it exists. The handle is kept until the game shuts down.
//
// Shutdown (the exit crash of 2026-10-15/16, docs/edf41-classes.md 12). The loader's reference is a counted one into EDF's static
// bank cache (EDF+0x20B38A0: std::map of 0xA0-byte nodes, the count at node+0x98, the bank at node+0x48 with its "registered with
// the sound system" byte at +8). Every stock owner gives its reference back inside the application destructor 0x7021F0, before
// that destructor deletes the sound system and nulls *(EDF+0x20B2948) (0x702493): measured at its entry 12 to 19 nodes, at the atexit 0 (with ours given back).
// A reference still held after that keeps its node until EDF.dll's atexit 0x1750870 (the "SoundBank::Release error ref count"
// report, then the cache's destructor 0x7A9850 -> node 0x7A99D0 -> bank 0x7AABC0), which unregisters the still-registered bank
// from the null sound system: 0x7ADCE0 reads [0+0x40], EDF+0x7ADCFB, EDF6Coop's handler turns it into FAST_FAIL. Our node was
// the only one left (the report names EDF41_WEAPON_SE.ACB, the dumps' rbx is our node+0x48). It did not crash every run only
// because the exit often ends earlier: an EDF.dll static destructor that runs before that atexit (0x111822C's object, releasing
// its D3D11 objects, 0x175714) can make d3d11/dxcore end the process with STATUS_THREAD_IS_TERMINATING (NtTerminateProcess
// 0xC000004B) -- seen on plain M019 runs; runs with the emitters went on to the cache, which is why the crash looked tied to them.
// So the reference is given back the stock way (0x7AAC80, the BankRef release: lock the cache, count down, erase at zero)
// from edf41c::AtGameShutdown, right before the application destructor runs, while the sound system is alive.
//
// Voices. The script's voice key plays from the first bank of the SeManager's list (*(EDF+0x20B2950), list at +0xE0) that has
// it (0x7AA150): banks load by 0x7B2500(mgr, wchar path ending .awb, flag), which reads <base>.AWB and <base>_LIST.AWE (an AWE
// the game accepts is version 2: tools/ writes it). The locale banks are loaded by 0x7044A0, whose last load is the call at
// 0x704C8F (straight-line code, every pass), and are all unloaded by 0x7B2CD0 at the start of the next pass (a language
// change). Ours is loaded after that call with flag 1, so it goes with them and comes back with the language. The language is
// the game's own index, dword at EDF+0x20B2B34 into the table at EDF+0x1F56208 (ja, en, kr, cn, sc).
//
// Where the files are, and the path the game is given. The installer writes them to <game>\Mods\SOUND\PC. The game turns a
// path into a file through its prefix table (EDF.dll 0x6FF0A7..: "APP:/SOUND/ADX" and "app:/sound/adx" -> "./SOUND/PC",
// then "app:/" -> "/cri_bind/") and EDFModLoader hooks the stream Open it ends in (EDF.dll+741C0, vtable 0x175F0F0 slot 1):
// ONLY a path that starts "/cri_bind/" is tried again as "./Mods/<rest>". The stock banks' app:/sound/adx/<name> becomes a
// plain "./sound/pc/<name>" in the game folder, which the redirector never sees: that is why the first real run loaded
// neither bank (a file only under Mods was "not found" there). So the weapon bank is asked for as app:/sound/pc/<name>:
// no "sound/adx" rule matches it, the "app:/" rule gives /cri_bind/sound/pc/<name>, the redirector serves Mods\SOUND\PC\<name>.
// (The weapon SGO / voice tables in Mods\MISSION reach the game the same way.) That works for the weapon ACB because the game
// reads the whole file itself, through that stream. A voice bank does not: SoundBank::Initialize (0x7AB580) checks the AWB
// exists (0x50E20) and reads the _LIST.AWE (0x84E80) through the game's stream, but the AWB's table of contents is opened by
// CRI's own file system, criAtomAwb_LoadToc (0xFEC2D4, call at 0x7AB845), with the translated narrow path: CRI never goes
// through 0x741C0, so "/cri_bind/SOUND/PC/EDF41_VOICE.JA.AWB" is a file that does not exist and the TOC load returns NULL
// (measured: exists=1, AWE read and accepted, toc=NULL). Initialize fails, 0x7AA440 answers {NULL, NULL}, and 0x7B2500, which
// does not look at the result, still appends that empty node to the SeManager's list, where it stays (0x7B2CD0 drops only
// nodes 0x7AA5C0 accepts, and an empty one it does not). So a voice bank is asked for by the path CRI can open itself, the
// same form the stock voice banks end up as ("./SOUND/PC/<name>", relative to the game folder, which is the working
// directory): "./Mods/SOUND/PC/<name>". No prefix rule matches it, the game's stream and CRI both open it as is. Should a
// load still fail, the empty node 0x7B2500 appended is taken out again the way 0x7B2CD0 removes one.
#include "crew.h"
#include "memory.h"
#include "edf41classes.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace crew {
namespace {
constexpr std::size_t kSystemGlobal=0x20B2948;      // *(EDF+this): the sound system (the set of banks at +0x98)
constexpr std::size_t kLanguageIndex=0x20B2B34;     // dword: 0 ja, 1 en, 2 kr, 3 cn, 4 sc (the table below)
constexpr std::size_t kLanguageTable=0x1F56208;     // wchar_t* [5]: L"ja", L"en", L"kr", L"cn", L"sc"
constexpr std::size_t kSeManagerGlobal=0x20B2950;   // *(EDF+this): the SeManager (voice banks, list at +0xE0)
constexpr std::size_t kBankLoad=0x7AA440;           // (out{Bank*,ctrl}, wstring* path, type, flag 1, byte) -> bool
constexpr std::size_t kBankRelease=0x7AAC80;        // (BankRef*): the cache reference given back (erased at count 0), control cleared
constexpr std::size_t kLookup=0x7AEAE0;             // (system, out{Bank*,int}, type, const wchar_t* name) -> bool
constexpr std::size_t kBankFind=0x7A9F80;           // (Bank*, wchar_t* name) -> cue index or -1
constexpr std::size_t kSeBankLoad=0x7B2190;         // EDF6's own SE bank, called at kSeBankLoadSite
constexpr std::size_t kSeBankLoadSite=0x7B09C6;
constexpr std::size_t kVoiceLoad=0x7B2500;          // (SeManager, wchar_t* path .awb, flag)
constexpr std::size_t kVoiceLoadSite=0x704C8F;      // the last locale bank (tikyuu6_voice_dlc2) of 0x7044A0
constexpr std::size_t kLookupSites[]={0x7B2984,0x7B3A1C,0x7B3B28,0x7B3C55,0x7B3CB1,0x7B3D0A,0x7B3E83,0x7B4073,0x7B46B5};
constexpr int kOurType=0x41;                        // no lookup asks for it; the other banks are 0 (SE) and 1 (voices)
constexpr int kLanguageCount=5;

const wchar_t kWeaponFile[]=L"EDF41_WEAPON_SE.ACB";
const wchar_t kWeaponPath[]=L"app:/sound/pc/edf41_weapon_se.acb";   // the "/cri_bind/" route, see the top of this file
constexpr std::size_t kBankListSize=0xE8;           // SeManager: size of the std::list of banks that starts at +0xE0

// MSVC's std::wstring as the game's code reads it (the game builds with the same layout): a pointer or eight inline
// characters, size, capacity. A capacity of 8 or more makes the callee read the pointer; it copies, never frees ours.
struct GameString {
    const wchar_t* data;
    std::uint64_t spare;
    std::uint64_t size;
    std::uint64_t capacity;
};
struct BankRef {              // 0x7AA440's out: the bank and its control block (a counted reference, kept for good)
    void* bank;
    void* control;
};
struct CueRef {               // 0x7AEAE0's out
    void* bank;
    int index;
};

using BankLoadFn=bool(__fastcall*)(BankRef* out,const GameString* path,int type,int flag,bool extra);
// name: the cue's characters (0x7AEAE0 copies them into a local std::wstring with assign(const wchar_t*), 0x7AEB39;
// the callers pass c_str() values, e.g. 0x7B3D3E's DSGO string at the 0x7B3E83 site)
using LookupFn=bool(__fastcall*)(void* system,CueRef* out,int type,const wchar_t* name);
using FindFn=int(__fastcall*)(void* bank,const wchar_t* name);
using SeLoadFn=std::uint64_t(__fastcall*)(void*,const void*,std::uint64_t,std::uint64_t,std::uint64_t,std::uint64_t);
using VoiceLoadFn=std::uint64_t(__fastcall*)(void* manager,const wchar_t* path,std::uint64_t flag);

BankRef weaponBank{};                                // given back at the game's shutdown (ReleaseWeaponBank)
std::atomic<void*> weaponBankPtr{nullptr};           // set once the bank is loaded
std::atomic<void*> weaponSystem{nullptr};            // the sound system it was loaded into
std::atomic<long> fallbackHits{0};

// <game>\Mods\SOUND\PC\<file> is a file (the only place the installer writes to).
bool ModsFileThere(const wchar_t* file) noexcept {
    wchar_t path[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(path,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    wchar_t full[MAX_PATH];
    if(swprintf_s(full,L"%ls\\Mods\\SOUND\\PC\\%ls",path,file)<0)return false;
    const DWORD a=GetFileAttributesW(full);
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}

void LoadWeaponBank() noexcept {
    void* system=*reinterpret_cast<void**>(image+kSystemGlobal);
    if(!system || weaponSystem.load()==system)return;
    weaponSystem.store(system);
    weaponBankPtr.store(nullptr);
    if(!ModsFileThere(kWeaponFile)) {
        Log("EDF41SOUND weapon bank Mods\\SOUND\\PC\\%ls not installed: 4.1's weapon sounds are off",kWeaponFile);
        return;
    }
    const GameString path{kWeaponPath,0,wcslen(kWeaponPath),wcslen(kWeaponPath)};
    BankRef ref{};
    const bool ok=reinterpret_cast<BankLoadFn>(image+kBankLoad)(&ref,&path,kOurType,1,false);
    if(!ok || !ref.bank) {
        Log("EDF41SOUND weapon bank %ls failed to load (ok=%d bank=%p)",kWeaponPath,ok ? 1 : 0,ref.bank);
        return;
    }
    weaponBank=ref;                 // the reference stays until the game shuts down (ReleaseWeaponBank)
    weaponBankPtr.store(ref.bank);
    Log("EDF41SOUND weapon bank loaded: bank=%p control=%p type=0x%X system=%p",ref.bank,ref.control,kOurType,system);
}

// Right before the application destructor (edf41c::AtGameShutdown): the bank's cache reference given back while the sound system
// still exists, as every stock owner does in that destructor (see the top of this file). The lookups stop answering from it first.
void ReleaseWeaponBank() noexcept {
    static const unsigned char print[10]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x51,0x08};
    weaponBankPtr.store(nullptr);
    weaponSystem.store(nullptr);
    if(!weaponBank.control)return;
    if(!Readable(image+kBankRelease,sizeof(print)) || std::memcmp(image+kBankRelease,print,sizeof(print))!=0)return;
    reinterpret_cast<void(__fastcall*)(BankRef*)>(image+kBankRelease)(&weaponBank);
    weaponBank=BankRef{};
}

// EDF6's own SE bank loaded (the original runs first): the sound system exists and has its stock banks.
std::uint64_t __fastcall SeBankLoadHook(void* self,const void* a,std::uint64_t b,std::uint64_t c,std::uint64_t d,std::uint64_t e) {
    const std::uint64_t result=reinterpret_cast<SeLoadFn>(image+kSeBankLoad)(self,a,b,c,d,e);
    LoadWeaponBank();
    return result;
}

// The stock lookup first; only a miss for type 0 asks our bank.
bool __fastcall LookupHook(void* system,CueRef* out,int type,const wchar_t* name) {
    if(reinterpret_cast<LookupFn>(image+kLookup)(system,out,type,name))return true;
    void* bank=weaponBankPtr.load();
    if(type!=0 || !bank || system!=weaponSystem.load())return false;
    const wchar_t* cue=name;
    const int index=reinterpret_cast<FindFn>(image+kBankFind)(bank,cue);
    if(index==-1)return false;
    out->bank=bank;
    out->index=index;
    const long hits=++fallbackHits;
    if(hits<=8 || (hits&(hits-1))==0)Log("EDF41SOUND weapon cue '%ls' answered by 4.1's bank (index 0x%X, hit %ld)",cue,index,hits);
    return true;
}

// How many banks the SeManager holds (the std::list's size).
std::uint64_t BankCount(const void* manager) noexcept {
    return *reinterpret_cast<const std::uint64_t*>(static_cast<const char*>(manager)+kBankListSize);
}

// The SeManager's bank list: std::list nodes {next, prev, value}, value a shared_ptr {Bank*, control} (0x7B2CF6 reads it so).
struct BankNode {
    BankNode* next;
    BankNode* prev;
    BankRef value;
};
constexpr std::size_t kBankListHead=0xE0;
constexpr std::size_t kGameDelete=0x12D85EC;        // the game's operator delete(void*, size_t): 0x7B2D3A frees a node with it
constexpr std::size_t kNodeSize=0x20;               // 0x7B25DE allocates a node of this size
constexpr std::size_t kAweCheck=0x7A8050;           // (AWE bytes, int) -> bool: the list's header check
constexpr std::size_t kAweCheckSite=0x7AB708;       // in SoundBank::Initialize (0x7AB580)
constexpr std::size_t kAwbToc=0xFEC2D4;             // criAtomAwb_LoadToc(binder, const char* path, work, size) -> handle
constexpr std::size_t kAwbTocSite=0x7AB845;
constexpr std::size_t kCueInBank=0x7AA150;          // (BankRef*, const char* key) -> cue id or -1
constexpr std::size_t kCueInBankSites[]={0x7B2B97,0x7B2C67};   // the SeManager's two walks of its list (0x7B2B00, 0x7B2C00)

using GameDeleteFn=void(__cdecl*)(void*,std::size_t);
using AweCheckFn=bool(__fastcall*)(const void* awe,int arg);
using AwbTocFn=void*(__fastcall*)(void* binder,const char* path,void* work,std::uint64_t size);
using CueInBankFn=int(__fastcall*)(const BankRef* bank,const char* key);

std::atomic<void*> voiceBankPtr{nullptr};            // the bank our last load put in the list (null when it failed)
std::atomic<long> voiceHits{0};

// What SoundBank::Initialize did with OUR files (recorded only while LoadVoiceBank's call runs, on that thread), so a
// failed load can say which step refused it.
struct VoiceLoadTrace {
    bool active;
    int aweChecked;      // -1 not reached (AWB missing or AWE not read), else the check's answer
    int tocTried;        // 0 not reached, 1 tried
    void* toc;           // the AWB's table of contents (NULL: CRI could not open / read the AWB)
    char tocPath[128];
};
thread_local VoiceLoadTrace trace{};

bool __fastcall AweCheckHook(const void* awe,int arg) {
    const bool ok=reinterpret_cast<AweCheckFn>(image+kAweCheck)(awe,arg);
    if(trace.active)trace.aweChecked=ok ? 1 : 0;
    return ok;
}

void* __fastcall AwbTocHook(void* binder,const char* path,void* work,std::uint64_t size) {
    void* toc=reinterpret_cast<AwbTocFn>(image+kAwbToc)(binder,path,work,size);
    if(trace.active) {
        trace.tocTried=1;
        trace.toc=toc;
        strncpy_s(trace.tocPath,path ? path : "(null)",_TRUNCATE);
    }
    return toc;
}

// The script's voice key looked up bank by bank; log when OUR bank is the one that answers (with the dev probe, every
// lookup: the key, the bank, the answer).
std::atomic<long> probeLookups{0};
int __fastcall CueInBankHook(const BankRef* bank,const char* key) {
    const int cue=reinterpret_cast<CueInBankFn>(image+kCueInBank)(bank,key);
    if(Cfg().edf41VoiceProbe && ++probeLookups<=3000)
        Log("EDF41SOUND probe lookup '%s' in bank %p -> %d",key ? key : "?",bank ? bank->bank : nullptr,cue);
    void* ours=voiceBankPtr.load();
    if(cue!=-1 && ours && bank && bank->bank==ours) {
        const long hits=++voiceHits;
        if(hits<=8 || (hits&(hits-1))==0)Log("EDF41SOUND voice cue '%s' answered by 4.1's voice bank %p (cue 0x%X, hit %ld)",key ? key : "?",ours,cue,hits);
    }
    return cue;
}

// The caption lookup (0x7B17F0: (voice table owner, out, key wstring*) -> out {name wstring, caption wstring, ...}) at its
// three call sites (the subtitle's two 0x5DB6A7 / 0x5DB99F, 0x5DD5C5): with the dev probe, the key it was given and what came
// back. The MSVC wstring: 16 bytes of inline buffer or a pointer, size at +0x10, capacity at +0x18 (inline below 8).
constexpr std::size_t kCaption=0x7B17F0;
constexpr std::size_t kCaptionSites[]={0x5DB6A7,0x5DB99F,0x5DD5C5};
using CaptionFn=void*(__fastcall*)(void*,void*,void*);
std::atomic<long> probeCaptions{0};
void WideOf(const void* s,wchar_t* out,std::size_t room) noexcept {
    out[0]=0;
    if(!s)return;
    const char* p=static_cast<const char*>(s);
    const std::uint64_t size=*reinterpret_cast<const std::uint64_t*>(p+0x10);
    const std::uint64_t cap=*reinterpret_cast<const std::uint64_t*>(p+0x18);
    const wchar_t* text=cap>=8 ? *reinterpret_cast<wchar_t* const*>(p) : reinterpret_cast<const wchar_t*>(p);
    const std::size_t n=size<room-1 ? static_cast<std::size_t>(size) : room-1;
    for(std::size_t i=0;i<n;++i)out[i]=text[i];
    out[n]=0;
}
// Inside the caption lookup: its row lookup by the cut key (0x7B1AD8: 0x55A30(table handle, const wchar_t* name) -> row or -1),
// whichever caller asked; with the dev probe, every name and its answer.
constexpr std::size_t kRowByName=0x55A30,kRowByNameSite=0x7B1AD8;
using RowByNameFn=int(__fastcall*)(void*,const wchar_t*);
std::atomic<long> probeRows{0};
int __fastcall RowByNameHook(void* table,const wchar_t* name) {
    const int row=reinterpret_cast<RowByNameFn>(image+kRowByName)(table,name);
    if(++probeRows<=2000)Log("EDF41SOUND probe row '%ls' -> %d (table %p, thread %lu)",name ? name : L"?",row,table,GetCurrentThreadId());
    return row;
}

// The bank walks' play calls (0x7B2BED -> 0x7AA9F0, 0x7B2CB8 -> 0x7AA700: (BankRef*, {cue, ...}*, a, b[, c]) -> started):
// with the dev probe, whether a cue our 4.1 voice bank answered actually started playing.
constexpr std::size_t kPlayA=0x7AA9F0,kPlayASite=0x7B2BED,kPlayB=0x7AA700,kPlayBSite=0x7B2CB8;
using PlayAFn=bool(__fastcall*)(const BankRef*,const int*,void*,void*,void*);
using PlayBFn=bool(__fastcall*)(const BankRef*,const int*,void*,void*);
std::atomic<long> probePlays{0};
void LogPlay(const char* which,const BankRef* bank,const int* cue,bool started) noexcept {
    void* ours=voiceBankPtr.load();
    const bool ourBank=bank && ours && bank->bank==ours;
    if(++probePlays<=3000 && (ourBank || !started))
        Log("EDF41SOUND probe play %s bank %p%s cue %d -> started %d",which,bank ? bank->bank : nullptr,ourBank ? " (4.1 voice)" : "",
            cue ? *cue : -1,started ? 1 : 0);
}
bool __fastcall PlayAHook(const BankRef* bank,const int* cue,void* a,void* b,void* c) {
    const bool r=reinterpret_cast<PlayAFn>(image+kPlayA)(bank,cue,a,b,c);
    LogPlay("A",bank,cue,r);
    return r;
}
bool __fastcall PlayBHook(const BankRef* bank,const int* cue,void* a,void* b) {
    const bool r=reinterpret_cast<PlayBFn>(image+kPlayB)(bank,cue,a,b);
    LogPlay("B",bank,cue,r);
    return r;
}

void* __fastcall CaptionHook(void* owner,void* out,void* key) {
    wchar_t k[64];
    WideOf(key,k,64);   // read before the call: it cuts the key in place (0x7B1A19)
    void* r=reinterpret_cast<CaptionFn>(image+kCaption)(owner,out,key);
    if(++probeCaptions<=600) {
        wchar_t name[48],caption[48];
        WideOf(out,name,48);
        WideOf(static_cast<char*>(out)+0x20,caption,48);
        Log("EDF41SOUND probe caption key '%ls' -> name '%ls' caption '%ls'",k,name,caption);
    }
    return r;
}

BankNode* ListHead(void* manager) noexcept {
    return *reinterpret_cast<BankNode**>(static_cast<char*>(manager)+kBankListHead);
}

// 0x7B2500 appended a node whose load failed ({NULL, NULL}: 0x7AA440's out when Initialize fails, 0x7A92F4). Nothing ever
// removes it (0x7B2CD0 keeps what 0x7AA5C0 refuses), so take it out the way 0x7B2CD0 removes one: unlink, size - 1, the
// game's own delete of the 0x20-byte node. With both members NULL there is no reference to drop (0x7B2D23 / 0x7B2D2C).
bool DropEmptyNode(void* manager,BankNode* node) noexcept {
    if(node->value.bank || node->value.control)return false;
    node->prev->next=node->next;
    node->next->prev=node->prev;
    --*reinterpret_cast<std::uint64_t*>(static_cast<char*>(manager)+kBankListSize);
    reinterpret_cast<GameDeleteFn>(image+kGameDelete)(node,kNodeSize);
    return true;
}

// Which step of SoundBank::Initialize refused our files (from the trace).
const char* FailureReason() noexcept {
    if(trace.aweChecked<0)return "the AWB was not found or the _LIST.AWE not read through the game's file stream";
    if(trace.aweChecked==0)return "the _LIST.AWE was refused by the header check 0x7A8050 (not a version 2 AWBE)";
    if(!trace.tocTried)return "Initialize stopped before the AWB's table of contents";
    if(!trace.toc)return "CRI could not open the AWB (criAtomAwb_LoadToc returned NULL for the path)";
    return "the AWB's TOC loaded but the bank has more than 0x10000 cues or no CRI handle (0x7AB8A9)";
}

// The voice file name (L"EDF41_VOICE.JA.AWB") for a language, if both of its files are in Mods\SOUND\PC.
bool VoiceFilesThere(const wchar_t* lang,wchar_t* awbName,std::size_t capacity) noexcept {
    wchar_t upper[3]={lang[0]>=L'a' ? static_cast<wchar_t>(lang[0]-32) : lang[0],lang[0] && lang[1]>=L'a' ? static_cast<wchar_t>(lang[1]-32) : lang[1],0};
    wchar_t list[64];
    if(swprintf_s(awbName,capacity,L"EDF41_VOICE.%ls.AWB",upper)<0 || swprintf_s(list,L"EDF41_VOICE.%ls_LIST.AWE",upper)<0)return false;
    return ModsFileThere(awbName) && ModsFileThere(list);
}

// Load one bank file (its name in Mods\SOUND\PC) the way the locale banks load; say what happened.
void LoadVoiceFile(void* manager,const wchar_t* name,const wchar_t* language) noexcept {
    wchar_t path[96];
    if(swprintf_s(path,L"./Mods/SOUND/PC/%ls",name)<0)return;   // the path CRI opens itself, see the top of this file
    const std::uint64_t before=BankCount(manager);
    trace={};
    trace.active=true;
    trace.aweChecked=-1;
    reinterpret_cast<VoiceLoadFn>(image+kVoiceLoad)(manager,path,1);
    trace.active=false;
    const std::uint64_t after=BankCount(manager);
    if(after!=before+1) {
        Log("EDF41SOUND voice bank %ls: the list went %llu -> %llu, not one more (language %ls): 4.1's voices are off",path,
            static_cast<unsigned long long>(before),static_cast<unsigned long long>(after),language);
        return;
    }
    BankNode* node=ListHead(manager)->prev;            // 0x7B2500 appends: the last node is the one it just added
    if(node->value.bank) {
        voiceBankPtr.store(node->value.bank);
        Log("EDF41SOUND voice bank %ls loaded for language %ls (manager=%p, banks %llu -> %llu, bank %p, toc %p)",path,language,manager,
            static_cast<unsigned long long>(before),static_cast<unsigned long long>(after),node->value.bank,trace.toc);
        return;
    }
    const bool dropped=DropEmptyNode(manager,node);
    Log("EDF41SOUND ERROR voice bank %ls FAILED to load for language %ls: %s (AWE check %d, TOC path '%s' -> %p). The empty node "
        "0x7B2500 appended was %s (banks now %llu): 4.1's voices are off",path,language,FailureReason(),trace.aweChecked,
        trace.tocTried ? trace.tocPath : "-",trace.toc,dropped ? "removed" : "NOT removed (it holds a control block)",
        static_cast<unsigned long long>(BankCount(manager)));
}

// The current language's bank, else (kr / cn / sc, which 4.1 has no recording of) Japanese, else English. The previous
// pass's bank is gone by now: 0x7B2CD0 unloaded it with the locale banks at the start of this pass (it was loaded with the
// same flag 1).
void LoadVoiceBank(void* manager) noexcept {
    voiceBankPtr.store(nullptr);
    const int index=*reinterpret_cast<const int*>(image+kLanguageIndex);
    if(index<0 || index>=kLanguageCount)return;
    const wchar_t* language=reinterpret_cast<const wchar_t* const*>(image+kLanguageTable)[index];
    if(!language || !language[0] || !language[1])return;
    const wchar_t* const order[3]={language,L"ja",L"en"};
    const int tries=index<=1 ? 1 : 3;                  // ja and en have their own; the rest fall back
    for(int i=0;i<tries;++i) {
        wchar_t name[48];
        if(!VoiceFilesThere(order[i],name,48))continue;
        LoadVoiceFile(manager,name,language);
        return;
    }
    Log("EDF41SOUND no EDF41_VOICE bank for language %ls: 4.1's voices are off",language);
}

// The locale banks loaded (the original runs first, so ours is the last in the list).
std::uint64_t __fastcall VoiceLoadHook(void* manager,const wchar_t* path,std::uint64_t flag) {
    const std::uint64_t result=reinterpret_cast<VoiceLoadFn>(image+kVoiceLoad)(manager,path,flag);
    if(manager)LoadVoiceBank(manager);
    return result;
}
}  // namespace

bool InstallEdf41Sound() noexcept {
    if(!Cfg().edf41Sound) {
        Log("EDF41SOUND off (ini Edf41Sound=0)");
        return false;
    }
    bool changed=false;
    bool ok=RedirectCall(image+kSeBankLoadSite,image+kSeBankLoad,reinterpret_cast<void*>(&SeBankLoadHook),changed);
    int lookups=0;
    for(const std::size_t site:kLookupSites)
        lookups+=RedirectCall(image+site,image+kLookup,reinterpret_cast<void*>(&LookupHook),changed) ? 1 : 0;
    const bool voice=RedirectCall(image+kVoiceLoadSite,image+kVoiceLoad,reinterpret_cast<void*>(&VoiceLoadHook),changed);
    // what Initialize did with our voice files (for the failure log) and which voice cues our bank answers
    const bool aweProbe=RedirectCall(image+kAweCheckSite,image+kAweCheck,reinterpret_cast<void*>(&AweCheckHook),changed);
    const bool tocProbe=RedirectCall(image+kAwbTocSite,image+kAwbToc,reinterpret_cast<void*>(&AwbTocHook),changed);
    int cueProbes=0;
    for(const std::size_t site:kCueInBankSites)
        cueProbes+=RedirectCall(image+site,image+kCueInBank,reinterpret_cast<void*>(&CueInBankHook),changed) ? 1 : 0;
    if(Cfg().edf41VoiceProbe) {
        int captions=0;
        for(const std::size_t site:kCaptionSites)
            captions+=RedirectCall(image+site,image+kCaption,reinterpret_cast<void*>(&CaptionHook),changed) ? 1 : 0;
        const bool rows=RedirectCall(image+kRowByNameSite,image+kRowByName,reinterpret_cast<void*>(&RowByNameHook),changed);
        const bool playA=RedirectCall(image+kPlayASite,image+kPlayA,reinterpret_cast<void*>(&PlayAHook),changed);
        const bool playB=RedirectCall(image+kPlayBSite,image+kPlayB,reinterpret_cast<void*>(&PlayBHook),changed);
        Log("EDF41SOUND probe on: play calls %d/%d",playA ? 1 : 0,playB ? 1 : 0);
        Log("EDF41SOUND probe on: caption sites %d/3, row lookup %d",captions,rows ? 1 : 0);
    }
    edf41c::AtGameShutdown(&ReleaseWeaponBank);   // the bank's reference given back before the sound system goes (the exit crash)
    Log("EDF41SOUND hooked: se bank load=%d lookups=%d/%d voice load=%d (awe check=%d awb toc=%d voice cue=%d/2)",ok ? 1 : 0,lookups,
        static_cast<int>(sizeof(kLookupSites)/sizeof(kLookupSites[0])),voice ? 1 : 0,aweProbe ? 1 : 0,tocProbe ? 1 : 0,cueProbes);
    return ok && voice && lookups>0;
}
}  // namespace crew
