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
// system has just loaded a bank through the same cache (0x7B095C), so it exists. The handle is kept for the process.
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
// neither bank (a file only under Mods was "not found" there). So a bank under Mods is asked for as app:/sound/pc/<name>:
// no "sound/adx" rule matches it, the "app:/" rule gives /cri_bind/sound/pc/<name>, the redirector serves Mods\SOUND\PC\<name>.
// (The weapon SGO / voice tables in Mods\MISSION reach the game the same way.) 0x7B2500 is void: whether a voice bank got
// in is read from the SeManager's bank list (std::list at +0xE0, its size at +0xE8) before and after.
#include "crew.h"
#include "memory.h"

#include <atomic>
#include <cstdint>
#include <cwchar>

namespace crew {
namespace {
constexpr std::size_t kSystemGlobal=0x20B2948;      // *(EDF+this): the sound system (the set of banks at +0x98)
constexpr std::size_t kLanguageIndex=0x20B2B34;     // dword: 0 ja, 1 en, 2 kr, 3 cn, 4 sc (the table below)
constexpr std::size_t kLanguageTable=0x1F56208;     // wchar_t* [5]: L"ja", L"en", L"kr", L"cn", L"sc"
constexpr std::size_t kSeManagerGlobal=0x20B2950;   // *(EDF+this): the SeManager (voice banks, list at +0xE0)
constexpr std::size_t kBankLoad=0x7AA440;           // (out{Bank*,ctrl}, wstring* path, type, flag 1, byte) -> bool
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

BankRef weaponBank{};                                // process lifetime
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
    weaponBank=ref;                 // the reference stays: the bank lives as long as the process
    weaponBankPtr.store(ref.bank);
    Log("EDF41SOUND weapon bank loaded: bank=%p control=%p type=0x%X system=%p",ref.bank,ref.control,kOurType,system);
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

// The voice file name (L"EDF41_VOICE.JA.AWB") for a language, if both of its files are in Mods\SOUND\PC.
bool VoiceFilesThere(const wchar_t* lang,wchar_t* awbName,std::size_t capacity) noexcept {
    wchar_t upper[3]={lang[0]>=L'a' ? static_cast<wchar_t>(lang[0]-32) : lang[0],lang[0] && lang[1]>=L'a' ? static_cast<wchar_t>(lang[1]-32) : lang[1],0};
    wchar_t list[64];
    if(swprintf_s(awbName,capacity,L"EDF41_VOICE.%ls.AWB",upper)<0 || swprintf_s(list,L"EDF41_VOICE.%ls_LIST.AWE",upper)<0)return false;
    return ModsFileThere(awbName) && ModsFileThere(list);
}

// The current language's bank, else (kr / cn / sc, which 4.1 has no recording of) Japanese, else English.
void LoadVoiceBank(void* manager) noexcept {
    const int index=*reinterpret_cast<const int*>(image+kLanguageIndex);
    if(index<0 || index>=kLanguageCount)return;
    const wchar_t* language=reinterpret_cast<const wchar_t* const*>(image+kLanguageTable)[index];
    if(!language || !language[0] || !language[1])return;
    const wchar_t* const order[3]={language,L"ja",L"en"};
    const int tries=index<=1 ? 1 : 3;                  // ja and en have their own; the rest fall back
    for(int i=0;i<tries;++i) {
        wchar_t name[48];
        if(!VoiceFilesThere(order[i],name,48))continue;
        wchar_t path[96];
        if(swprintf_s(path,L"app:/sound/pc/%ls",name)<0)return;
        for(wchar_t* p=path+14;*p;++p)if(*p>=L'A' && *p<=L'Z')*p=static_cast<wchar_t>(*p+32);
        const std::uint64_t before=BankCount(manager);
        reinterpret_cast<VoiceLoadFn>(image+kVoiceLoad)(manager,path,1);
        const std::uint64_t after=BankCount(manager);
        if(after>before)Log("EDF41SOUND voice bank %ls loaded for language %ls (manager=%p, banks %llu -> %llu)",path,language,manager,
                            static_cast<unsigned long long>(before),static_cast<unsigned long long>(after));
        else Log("EDF41SOUND voice bank %ls was NOT added for language %ls (manager=%p, banks %llu)",path,language,manager,
                 static_cast<unsigned long long>(after));
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
    Log("EDF41SOUND hooked: se bank load=%d lookups=%d/%d voice load=%d",ok ? 1 : 0,lookups,
        static_cast<int>(sizeof(kLookupSites)/sizeof(kLookupSites[0])),voice ? 1 : 0);
    return ok && voice && lookups>0;
}
}  // namespace crew
