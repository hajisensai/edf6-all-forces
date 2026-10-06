// The jets' stores (stores.h, docs/stores-re.md). All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
//  - The 506 builds its weapons in slot 46 (0x61B770) for exactly four holders (`cmp ebx,4` at 0x61B85F; one
//    SetWeaponObject 0x633330 a holder, no bound check): a jet's fifth store and on had no weapon. The loop's end is
//    redirected to a cave that compares with the vehicle's holder count (veh +0x648, vehicle_weapon_setting's rows,
//    which pylib/vcobjects.py jet_sgo makes as many as mission_setup[3]'s entries). The stock 506s have four and four.
//    Without the plugin the game builds the first four: every jet SGO has at least four holders, so none reads an
//    entry that is not there.
//  - A weapon's SGO: weapon +0x08 is its resource node (shared by every weapon from that file), node +0x20 its key,
//    an MSVC std::wstring of the upper-cased path (inline under 8 characters' capacity, +0x38).
#include "stores.h"
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cstring>
#include <cwchar>
#include <cwctype>

namespace crew {
namespace {
#include "stores.inc"
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr std::size_t kWeaponNode=0x08,kNodeKey=0x20,kKeyLength=0x30,kKeyCapacity=0x38;
constexpr std::size_t kWeaponTrigger=0x139,kWeaponLockRange=0x6D0,kWeaponAmmo=0xBE8,kWeaponLocked=0xC68;
constexpr std::size_t kLoopEnd=0x61B85D,kLoopTop=0x61B7F0,kLoopOut=0x61B864;
const unsigned char kLoopCode[]={0xFF,0xC3,0x83,0xFB,0x04,0x7C,0x8C,0x0F,0xB7,0x45,0xD8};   // inc ebx; cmp ebx,4; jl
const unsigned char kBuildSig[]={0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8B,0xEC};   // 0x61B770
const unsigned char kSetWeaponSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};          // 0x633330
bool storesOk=false;
// The weapons' locks (their search order, the target cycle, reading them) are lockon.cpp's.
// The HUD's lock marks (docs/stores-re.md §8): its two lock slots (0x1F0 bytes from HUD +0x720) each draw their weapon's
// locks in 0xFA060(slot, ctx, camera): the snapshot of locked / fired-at marks (+0x1D0 vector, its count +0x1E8), the
// lock in progress (+0x1B0) and the lock-range frame (+0x140), rebuilt every frame (0xFC620) from the weapon at slot
// +0x90. For a store's weapon the plugin draws its own (hud.cpp LockMark): the detour clears what the slot would draw
// this frame. Its first 14 bytes (mov r11,rsp; mov [r11+18],rbx; push rbp/rsi/rdi/r12/r13; no rip-relative) go to a
// trampoline. Locking, guidance and the lists are untouched: the HUD only reads them.
constexpr std::size_t kMarkDraw=0xFA060,kMarkCopied=14,kSlotWeapon=0x90,kSlotMarkCount=0x1E8,kSlotLocking=0x1B0,kSlotFrame=0x140;
const unsigned char kMarkDrawSig[]={0x4C,0x8B,0xDC,0x49,0x89,0x5B,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
                                    0x49,0x8D,0xAB,0x28,0xFF,0xFF,0xFF};
using MarkDrawFn=void(__fastcall*)(unsigned char*,void*,void*);
MarkDrawFn markNext=nullptr;

// The file name of the weapon's SGO (after the last separator of its key), or nullptr.
const wchar_t* FileOf(const unsigned char* weapon,std::size_t* length) noexcept {
    const auto node=At<const unsigned char*>(weapon,kWeaponNode);
    if(!Readable(node,kKeyCapacity+8))return nullptr;
    const std::size_t n=At<std::size_t>(node,kKeyLength),cap=At<std::size_t>(node,kKeyCapacity);
    if(n==0 || n>260 || cap<n)return nullptr;
    const wchar_t* key=cap>=8 ? At<const wchar_t*>(node,kNodeKey) : reinterpret_cast<const wchar_t*>(node+kNodeKey);
    if(!Readable(key,n*sizeof(wchar_t)))return nullptr;
    std::size_t at=n;
    while(at>0 && key[at-1]!=L'\\' && key[at-1]!=L'/')--at;
    *length=n-at;
    return key+at;
}

// The store whose files `name` (`length` characters) is one of: its prefix, then digits, then ".SGO".
const StoreSpec* SpecOf(const wchar_t* name,std::size_t length) noexcept {
    for(const auto& s:kStores) {
        const std::size_t p=std::wcslen(s.prefix);
        if(length<=p+4 || _wcsnicmp(name,s.prefix,p)!=0 || _wcsnicmp(name+length-4,L".SGO",4)!=0)continue;
        bool digits=true;
        for(std::size_t i=p;i<length-4;++i)digits=digits && std::iswdigit(name[i]);
        if(digits)return &s;
    }
    return nullptr;
}

}  // namespace

const wchar_t* WeaponFile(const unsigned char* w,std::size_t* length) noexcept {
    *length=0;
    return Readable(w,kWeaponNode+8) ? FileOf(w,length) : nullptr;
}

const StoreSpec* StoreOf(const unsigned char* w) noexcept {
    std::size_t length=0;
    const wchar_t* name=WeaponFile(w,&length);
    return name ? SpecOf(name,length) : nullptr;
}

bool IsStoreWeapon(const unsigned char* w) noexcept { return StoreOf(w)!=nullptr; }

namespace {
void __fastcall MarkDrawHook(unsigned char* slot,void* ctx,void* camera) {
    __try {
        const auto w=Readable(slot,kSlotMarkCount+8) ? At<const unsigned char*>(slot,kSlotWeapon) : nullptr;
        if(w && IsStoreWeapon(w)){Put<std::uint64_t>(slot,kSlotMarkCount,0);slot[kSlotLocking]=0;slot[kSlotFrame]=0;}
    } __except(EXCEPTION_EXECUTE_HANDLER){}
    markNext(slot,ctx,camera);
}

// `copied` first bytes of `rva` into a trampoline (they, then jmp [rip] back past them) and a jmp [rip] to `hook` over
// them, the rest nop'd. The trampoline, or nullptr (nothing patched).
void* Detour(std::size_t rva,const unsigned char* sig,std::size_t sigSize,std::size_t copied,void* hook) noexcept {
    if(copied<14 || copied>32 || !Matches(rva,sig,sigSize))return nullptr;
    unsigned char tramp[32+14];
    std::memcpy(tramp,image+rva,copied);
    const unsigned char jmp[6]={0xFF,0x25,0,0,0,0};
    std::memcpy(tramp+copied,jmp,6);
    const auto back=reinterpret_cast<std::uintptr_t>(image+rva+copied);
    std::memcpy(tramp+copied+6,&back,8);
    void* const t=edf::AllocateNearCode(image+rva,tramp,copied+14);
    if(!t)return nullptr;
    unsigned char patch[32];
    std::memset(patch,0x90,copied);
    std::memcpy(patch,jmp,6);
    const auto to=reinterpret_cast<std::uintptr_t>(hook);
    std::memcpy(patch+6,&to,8);
    if(edf::PatchCode(image+rva,image+rva,patch,copied))return t;
    VirtualFree(t,0,MEM_RELEASE);
    return nullptr;
}

void* BuildLoopCave() noexcept {
    unsigned char cave[]={
        0xFF,0xC3,                                   // inc ebx
        0x3B,0x9E,0x48,0x06,0x00,0x00,               // cmp ebx,[rsi+0x648]   (rsi: the vehicle, 0x61B794)
        0x7D,0x0E,                                   // jge out
        0xFF,0x25,0,0,0,0, 0,0,0,0,0,0,0,0,          // jmp [rip] -> the loop's top
        0xFF,0x25,0,0,0,0, 0,0,0,0,0,0,0,0};         // out: jmp [rip] -> after the loop
    const auto top=reinterpret_cast<std::uintptr_t>(image+kLoopTop),out=reinterpret_cast<std::uintptr_t>(image+kLoopOut);
    std::memcpy(cave+16,&top,8);std::memcpy(cave+30,&out,8);
    return edf::AllocateNearCode(image+kLoopEnd,cave,sizeof(cave));
}

// The other stock classes' weapon builds (slot 46; docs/stock-payload-re.md §4): each calls SetWeaponObject for the
// holders its own weapons need (403 and the 503 a loop of three, 603 two written out, 404 six, 409 three, 402 / 505 /
// 601 one), never past them; the Car's (the Grape) loops over the list and builds every entry itself. The stores the installer hangs on their requests (tools/make_stock_stores.py) are holders after those: the
// hook lets the stock build run, then builds every holder still without a weapon from the same weapon list, as the
// 506's loop does (SetWeaponObject's second argument, the seat's turret block, is read only for an entry with a third
// item, the turret's speeds 0x6335D1; a store's entry has two, so it gets none). The weapon list is the child of the
// setup the build reads (index 2, the helicopters' 3) found by its shape: as many entries as the vehicle has holders,
// the first a node. A stock vehicle has no holder past its build's, or no entry for it: nothing is built.
// The SGO nodes are read as rounds.cpp reads them (tag 2 variants; the child and count functions checked at load).
struct Node { unsigned char data[16]; std::uint16_t tag; unsigned char pad[6]; };
struct NodePick { Node* out; std::int32_t index; };
constexpr std::uint16_t kNodeTag=2,kNoTag=0xFFFF;
constexpr unsigned kNodeChild=0x2390D0,kNodeCount=0x240AF0;
const unsigned char kNodeChildSig[]={0x40,0x53,0x48,0x83,0xEC,0x30,0x48,0x8B,0x1A,0x48,0x8B,0x01};
const unsigned char kNodeCountSig[]={0x4C,0x8B,0x01,0x4C,0x8B,0xCA,0x8B,0x51,0x08,0x49,0x39,0x50};
using NodeChildFn=void(__fastcall*)(const Node*,NodePick*);
using NodeCountFn=void(__fastcall*)(const Node*,std::int32_t*);
using SetWeaponFn=void(__fastcall*)(unsigned char* vehicle,void* turret,const Node* entry,int holder);
using BuildFn=void(__fastcall*)(unsigned char* vehicle,const Node* setup);
constexpr std::size_t kSlotBuild=46;
constexpr std::size_t kHolders=0x638,kHolderCount=0x648,kHolderStride=0x48;   // layout.h (the vehicle's holders)

struct BuildClass { unsigned vtable,build; const char* name; };
const BuildClass kBuilds[]={
    {0x17D8B50,0x5FDBC0,"402_Rocket"},{0x17D8FA0,0x5FEFB0,"403_Tank"},{0x17D9458,0x6000E0,"404_Tank"},
    {0x17DA508,0x617CF0,"503_Bike"},{0x17DADB0,0x61B060,"505_Tank"},{0x17DC250,0x620B20,"601_Tank"},
    {0x17DC620,0x621A50,"603_Flak"},{0x17DEF98,0x64B3C0,"Helicopter409"},{0x17E01B0,0x65A910,"Car"},
};
constexpr int kBuildCount=static_cast<int>(sizeof(kBuilds)/sizeof(kBuilds[0]));
BuildFn nextBuild[kBuildCount]{};
bool nodesOk=false;

int NodeCount(const Node& n) noexcept {
    if(n.tag!=kNodeTag)return 0;
    std::int32_t c=0;
    reinterpret_cast<NodeCountFn>(image+kNodeCount)(&n,&c);
    return c;
}
bool NodeChild(const Node& n,int index,Node* out) noexcept {
    if(index<0 || index>=NodeCount(n))return false;
    *out=Node{};out->tag=kNoTag;
    NodePick pick{out,index};
    reinterpret_cast<NodeChildFn>(image+kNodeChild)(&n,&pick);
    return out->tag==kNodeTag;
}

unsigned char* HolderWeaponAt(const unsigned char* holders,std::uint64_t i) noexcept {
    return At<unsigned char*>(holders+i*kHolderStride,kHolderWeapon);
}

// The setup's weapon list: the child with an entry a holder, its first entry a node (the scale pair and the drive's
// numbers are numbers). The last such, as the helicopters' list follows their numbers.
bool WeaponList(const Node& setup,std::uint64_t holders,Node* out) noexcept {
    bool found=false;
    const int n=NodeCount(setup);
    for(int i=0;i<n;++i) {
        Node c{},first{};
        if(!NodeChild(setup,i,&c) || static_cast<std::uint64_t>(NodeCount(c))!=holders || !NodeChild(c,0,&first))continue;
        *out=c;found=true;
    }
    return found;
}

void Fill(unsigned char* v,const Node* setup,int cls) noexcept {
    __try {
        const auto holders=At<unsigned char*>(v,kHolders);
        const auto count=At<std::uint64_t>(v,kHolderCount);
        if(!holders || count==0 || count>64 || !Readable(holders,count*kHolderStride))return;
        std::uint64_t built=0;   // one past the last holder the stock build gave a weapon: the ones after it are ours
        for(std::uint64_t i=0;i<count;++i)if(HolderWeaponAt(holders,i))built=i+1;
        if(!setup || built>=count)return;
        Node list{};
        if(!WeaponList(*setup,count,&list))return;
        for(std::uint64_t i=built;i<count;++i) {
            Node entry{};
            if(!NodeChild(list,static_cast<int>(i),&entry))continue;
            reinterpret_cast<SetWeaponFn>(image+0x633330)(v,nullptr,&entry,static_cast<int>(i));
            if(Cfg().debug)Log("STORES v=%p %s: holder %llu built (%p)",v,kBuilds[cls].name,i,HolderWeaponAt(holders,i));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log("STORES v=%p %s: building its extra holders faulted",v,kBuilds[cls].name);
    }
}

template<int I> void __fastcall BuildHook(unsigned char* v,const Node* setup) {
    nextBuild[I](v,setup);
    Fill(v,setup,I);
}
template<int... I> struct BuildHooks { static constexpr BuildFn table[]={&BuildHook<I>...}; };
using AllBuildHooks=BuildHooks<0,1,2,3,4,5,6,7,8>;
static_assert(sizeof(AllBuildHooks::table)/sizeof(AllBuildHooks::table[0])==kBuildCount,"one hook per class");

int InstallBuilds() noexcept {
    int hooked=0;
    for(int i=0;i<kBuildCount;++i) {
        auto slot=reinterpret_cast<void**>(image+kBuilds[i].vtable)+kSlotBuild;
        if(*slot!=image+kBuilds[i].build)Log("HOOK stores %s: its weapon build is %p, not the stock one: chaining onto it",kBuilds[i].name,*slot);
        void* next=nullptr;
        if(edf::ChainVtableSlot(slot,reinterpret_cast<void*>(AllBuildHooks::table[i]),&next)){nextBuild[i]=reinterpret_cast<BuildFn>(next);++hooked;}
    }
    return hooked;
}
}  // namespace

bool IsLoadoutWeapon(const unsigned char* w) noexcept {
    std::size_t length=0;
    const wchar_t* name=WeaponFile(w,&length);
    return name && length>7 && _wcsnicmp(name,L"EDF6VC_",7)==0;
}

int ReadStores(unsigned char* v,Store* out,int most) noexcept {
    if(SeatCount(v)==0)return 0;   // without the loop patch the first four holders still have their weapons
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>16 || !Readable(holders,count*8))return 0;
    int n=0;
    for(std::uint64_t i=0;i<count && n<most;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponLocked+8))continue;
        std::size_t length=0;
        const wchar_t* name=FileOf(w,&length);
        const StoreSpec* spec=name ? SpecOf(name,length) : nullptr;
        if(!spec)continue;
        const std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        const auto locked=At<std::uint64_t>(w,kWeaponLocked);
        const float range=At<float>(w,kWeaponLockRange);
        out[n++]=Store{w,spec,ammo>0 ? ammo : 0,locked<64 ? static_cast<std::int32_t>(locked) : 0,
                       std::isfinite(range) && range>0.0f ? range : 0.0f};
    }
    return n;
}

void TriggerStore(const Store& s) noexcept {
    if(s.weapon && s.ammo>0 && Readable(s.weapon+kWeaponTrigger,1,true))s.weapon[kWeaponTrigger]=1;
}

const JetMass* JetMassOf(float mark) noexcept {
    for(const auto& m:kJetMasses)if(m.mark==mark)return &m;
    return nullptr;
}

Burden BurdenOf(float mark,const Store* stores,int count) noexcept {
    const JetMass* const kind=JetMassOf(mark);
    const float clean=kind ? kind->mass : 0.0f;
    Burden b{1.0f,0.0f};
    if(clean<=0.0f)return b;
    float kg=0.0f;
    for(int i=0;i<count;++i){kg+=stores[i].spec->mass*static_cast<float>(stores[i].ammo);b.drag+=stores[i].spec->drag*static_cast<float>(stores[i].ammo);}
    b.mass=(clean+kg)/clean;
    return b;
}

bool InstallStores() noexcept {
    __try {
        if(!Matches(0x61B770,kBuildSig,sizeof(kBuildSig)) || !Matches(0x633330,kSetWeaponSig,sizeof(kSetWeaponSig)) ||
           !Matches(kLoopEnd,kLoopCode,sizeof(kLoopCode))) {
            Log("HOOK stores=0 (unexpected EDF.dll code: a jet's fifth store and on have no weapon)");
            return false;
        }
        void* cave=BuildLoopCave();
        if(!cave){Log("HOOK stores=0 (no memory near the 506's weapon build)");return false;}
        const auto rel=static_cast<std::int32_t>(static_cast<unsigned char*>(cave)-(image+kLoopEnd+5));
        unsigned char jump[7]={0xE9,0,0,0,0,0x90,0x90};
        std::memcpy(jump+1,&rel,4);
        storesOk=edf::PatchCode(image+kLoopEnd,kLoopCode,jump,sizeof(jump));
        if(!storesOk)VirtualFree(cave,0,MEM_RELEASE);
        markNext=reinterpret_cast<MarkDrawFn>(Detour(kMarkDraw,kMarkDrawSig,sizeof(kMarkDrawSig),kMarkCopied,
                                                     reinterpret_cast<void*>(&MarkDrawHook)));
        nodesOk=Matches(kNodeChild,kNodeChildSig,sizeof(kNodeChildSig)) && Matches(kNodeCount,kNodeCountSig,sizeof(kNodeCountSig));
        const int builds=nodesOk ? InstallBuilds() : 0;
        Log("HOOK stores=%d (the 506 builds a weapon for every holder; %zu store kinds) stockMarks=%s builds=%d/%d (the stock "
            "classes build their extra holders)",storesOk,sizeof(kStores)/sizeof(kStores[0]),markNext ? "hidden for stores" : "kept",
            builds,kBuildCount);
        return storesOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
