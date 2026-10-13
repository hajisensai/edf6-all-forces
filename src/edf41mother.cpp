// EDF4.1's UfoMother301 (the mothership) as a class of its own, prototype (docs/edf41-ufomother301.md, phase 1).
// Development only: nothing here runs unless the ini says Edf41MotherDev=1 (tools/edf41_objects.py sets it when the
// installer was run with EDF41_MOTHER_DEV=1 and edf41port_dev/ exists; a release build never has either).
//
// What this settles (all of it logged as EDF41MOTHER):
//  1. Factory registration. The game creates a scene object by the SGO's xgs_scene_object_class: EDF.dll 0x1194410
//     (create from an SGO tree) reads the name (0x119ADE0), looks the factory up (GetSceneObjectFactory 0x1195C50: an
//     FNV-1a hash of the name's bytes into an unordered_map at manager+0x2C8; a miss logs "xgs::game::System::
//     GetSceneObjectFactory" and gives null) and calls the factory's vtable slot 2 (Create(factory, createInfo)). The
//     game's own classes are registered by 0x79D570, called from 0x701859 at start-up: per class a factory object
//     (8 bytes, only a vtable {dtor, name, create}), a shared_ptr control block, and 0x119AF00(manager, &{factory,
//     control}) = RegisterSceneObjectFactory (it asks the factory for its name and stores it in the map). We do the same
//     for "UfoMother301" right after 0x79D570 returns: the table is the game's own, no byte of it is patched.
//  2. A cloned vtable. The hull is built by UfoCarrier508's own constructor (0x4F8690, size 0x1630; the same class the
//     4.1 carriers already run on), so every field is initialised the way the class does it, and then its vtable
//     pointer (+0, the primary; the secondary at +0x120 is left alone) is pointed at a copy of the class's table
//     (0x17C9DC0, 49 slots, RTTI pointer copied so dynamic_cast still sees UfoCarrier508). In the copy, slots 0
//     (deleting destructor), 5 (Update(dt), the class's own, which calls GameObjectBase::Update 0x54BE40) and 9
//     (OnMessage(msg, data); the three-step SendMessage 0x1DA480 calls slots 10 / 9 / 11) are wrappers that log and
//     then call the class's own function; every other slot is a counting stub (lock inc, then jmp to the original),
//     so the log also says which slots the game really calls. Durability is the float at +0x2F8 (GameObjectBase,
//     0x545670 writes it), destroyed the byte at +0x2E8, the position the vec4 at +0x90. Damage arrives as message
//     0xF0000000 in slot 9 (the base handler 0x54A530 -> ApplyDamage 0x547C30); the script's ObjectAction (native 3200,
//     0x212A36) as message 0x1000000D with an int, GetPartsObject (native 3300, 0x222470) as 0x10000017 {grp, idx, out}.
//  3. Nothing here dereferences what it has not checked: every address of EDF.dll we call is compared with the bytes
//     it had when this was written, every game pointer goes through Readable, and a failure is logged and the stock
//     path is left alone.
#include "crew.h"
#include "memory.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace crew {
namespace {
constexpr std::size_t kRegisterAllSite=0x701859;      // call 0x79D570 in the start-up routine
constexpr std::size_t kRegisterAll=0x79D570;          // registers the game's scene object factories
constexpr std::size_t kRegisterFactory=0x119AF00;     // RegisterSceneObjectFactory(manager, shared_ptr<Factory>*)
constexpr std::size_t kGetFactory=0x1195C50;          // GetSceneObjectFactory(manager, const wchar_t* name)
constexpr std::size_t kManagerGlobal=0x20B2958;       // the manager (xgs::game::System's scene object registry)
constexpr std::size_t kNew=0x12D85B0;                 // the game's operator new(size)
constexpr std::size_t kCarrierCtor=0x4F8690;          // UfoCarrier508::UfoCarrier508(this, createInfo)
constexpr std::size_t kCarrierVtable=0x17C9DC0;       // its primary vtable
constexpr std::size_t kCarrierSize=0x1630;
constexpr int kSlots=49;
constexpr int kSlotDtor=0,kSlotUpdate=5,kSlotMessage=9;
constexpr std::size_t kDurabilityAt=0x2F8,kDestroyedAt=0x2E8,kPosAt=0x90;
constexpr std::uint32_t kMsgDamage=0xF0000000u,kMsgAction=0x1000000Du,kMsgParts=0x10000017u,kMsgGenerator=0x20100u;
constexpr std::uint32_t kUpdateLogEvery=600;          // frames between the state lines of an object
constexpr int kMaxMessageKinds=64;

struct Print { std::size_t rva; unsigned char bytes[8]; };
// The bytes of every EDF.dll function we call or replace, as they are in the build this plugin is for.
constexpr Print kPrints[]={
    {kRegisterAllSite,{0xE8,0x12,0xBD,0x09,0x00,0xE8,0xED,0xCF}},
    {kRegisterAll,{0x40,0x55,0x53,0x56,0x57,0x48,0x8D,0xAC}},
    {kRegisterFactory,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74}},
    {kNew,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kCarrierCtor,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},
    {0x4F8A70,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // slot 0
    {0x4F9540,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // slot 5
    {0x4F93E0,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74}},   // slot 9
};

using NewFn=void*(__fastcall*)(std::size_t);
using CtorFn=void*(__fastcall*)(void*,void*);
using RegisterFn=void(__fastcall*)(void*,void*);
using GetFactoryFn=void*(__fastcall*)(void*,const wchar_t*);
using RegisterAllFn=std::uint64_t(__fastcall*)(void*,void*,void*,void*);
using DtorFn=std::uint64_t(__fastcall*)(void*,unsigned);
using UpdateFn=std::uint64_t(__fastcall*)(void*,void*);
using MessageFn=std::uint64_t(__fastcall*)(void*,std::uint32_t,void*);

void* origSlots[kSlots];                  // the class's own functions
void* cloneBlock[kSlots+1];               // [0] the RTTI pointer, then the slots: what the objects' vptr points at
std::atomic<std::uint32_t> slotCalls[kSlots];
std::uint32_t slotSeen[kSlots];
std::atomic<bool> installed{false},registered{false};
std::atomic<int> created{0};

// Said once per kind of failure per run (a per-frame check must not fill the log).
void Fail(const char* what) noexcept {
    static std::mutex lock;
    static const char* said[32];
    static int count=0;
    std::lock_guard<std::mutex> hold(lock);
    for(int i=0;i<count;++i)
        if(said[i]==what)return;
    if(count<32)said[count++]=what;
    Log("EDF41MOTHER check failed: %s",what);
}

bool Rd(const void* p,std::size_t size,const char* what) noexcept {
    if(Readable(p,size))return true;
    Fail(what);
    return false;
}

// ---- the factory ---------------------------------------------------------------------------------------------
struct Factory { const void* const* vtable; };
extern Factory factory;
// The shared_ptr control block the game's own registrations make (0x79D5C0): vtable, use count at +8, weak count at +0xC, object at +0x10.
struct Control { const void* const* vtable; std::atomic<std::int32_t> use; std::atomic<std::int32_t> weak; void* object; };
void __fastcall ControlDestroy(void*) noexcept {}   // never reached: the counts start far from 1
void __fastcall ControlDelete(void*) noexcept {}
const void* const controlTable[2]={reinterpret_cast<const void*>(&ControlDestroy),reinterpret_cast<const void*>(&ControlDelete)};
Control control{controlTable,0x40000000,0x40000000,&factory};

void* __fastcall FactoryDtor(Factory* self,unsigned) noexcept { return self; }
const wchar_t* __fastcall FactoryName(Factory*) noexcept { return L"UfoMother301"; }
void __fastcall FactoryCreate(Factory*,void* info) noexcept;
const void* const factoryTable[3]={reinterpret_cast<const void*>(&FactoryDtor),reinterpret_cast<const void*>(&FactoryName),
                                   reinterpret_cast<const void*>(&FactoryCreate)};
Factory factory{factoryTable};

// ---- the wrappers ----------------------------------------------------------------------------------------------
struct Entry {
    std::uint32_t ticks=0;
    float lastDurability=0.0f;
    bool haveDurability=false;
};
std::mutex tableLock;
std::unordered_map<const void*,Entry> table;

bool Fields(const void* self,float& durability,std::uint8_t& destroyed,float* pos) noexcept {
    const char* p=static_cast<const char*>(self);
    if(!Rd(p+kDurabilityAt,4,"object durability field") || !Rd(p+kDestroyedAt,1,"object destroyed field") || !Rd(p+kPosAt,16,"object position field"))
        return false;
    std::memcpy(&durability,p+kDurabilityAt,4);
    destroyed=*reinterpret_cast<const std::uint8_t*>(p+kDestroyedAt);
    std::memcpy(pos,p+kPosAt,12);
    return true;
}

void DumpSlots() noexcept {
    char line[kSlots*12+64];
    int n=std::snprintf(line,sizeof(line),"EDF41MOTHER slot calls since the last line:");
    bool any=false;
    for(int i=0;i<kSlots && n>0 && n<static_cast<int>(sizeof(line))-16;++i) {
        const std::uint32_t now=slotCalls[i].load(std::memory_order_relaxed);
        if(now!=slotSeen[i]) {
            n+=std::snprintf(line+n,sizeof(line)-static_cast<std::size_t>(n)," %d:+%u",i,now-slotSeen[i]);
            slotSeen[i]=now;
            any=true;
        }
    }
    if(any)Log("%s",line);
}

std::uint64_t __fastcall DtorWrap(void* self,unsigned flags) {
    Log("EDF41MOTHER destructor %p flags %u",self,flags);
    {
        std::lock_guard<std::mutex> hold(tableLock);
        table.erase(self);
    }
    return reinterpret_cast<DtorFn>(origSlots[kSlotDtor])(self,flags);
}

std::uint64_t __fastcall UpdateWrap(void* self,void* dt) {
    const std::uint64_t result=reinterpret_cast<UpdateFn>(origSlots[kSlotUpdate])(self,dt);
    float durability=0.0f,pos[3]={};
    std::uint8_t destroyed=0;
    if(!Fields(self,durability,destroyed,pos))return result;
    std::uint32_t tick=0;
    bool changed=false;
    float before=0.0f;
    {
        std::lock_guard<std::mutex> hold(tableLock);
        Entry& e=table[self];
        tick=++e.ticks;
        if(e.haveDurability && (durability<e.lastDurability-0.5f || durability>e.lastDurability+0.5f)) { changed=true; before=e.lastDurability; }
        e.lastDurability=durability;
        e.haveDurability=true;
    }
    if(tick==1)Log("EDF41MOTHER first update %p durability %.1f destroyed %u pos %.1f %.1f %.1f",self,durability,destroyed,pos[0],pos[1],pos[2]);
    if(changed)Log("EDF41MOTHER durability %p %.1f -> %.1f (destroyed %u)",self,before,durability,destroyed);
    if(tick%kUpdateLogEvery==0) {
        Log("EDF41MOTHER update %p frame %u durability %.1f destroyed %u pos %.1f %.1f %.1f",self,tick,durability,destroyed,pos[0],pos[1],pos[2]);
        DumpSlots();
    }
    return result;
}

std::uint64_t __fastcall MessageWrap(void* self,std::uint32_t msg,void* data) {
    // A kind of message is logged the first times it comes; the ones that matter to the port every time.
    static std::mutex lock;
    static std::uint32_t kinds[kMaxMessageKinds];
    static std::uint32_t counts[kMaxMessageKinds];
    static int kindCount=0;
    int seen=0;
    {
        std::lock_guard<std::mutex> hold(lock);
        int at=-1;
        for(int i=0;i<kindCount;++i)
            if(kinds[i]==msg)at=i;
        if(at<0 && kindCount<kMaxMessageKinds) { at=kindCount++; kinds[at]=msg; counts[at]=0; }
        if(at>=0)seen=static_cast<int>(++counts[at]);
    }
    const bool important=msg==kMsgDamage || msg==kMsgAction || msg==kMsgParts || msg==kMsgGenerator;
    float before=0.0f,pos[3]={};
    std::uint8_t destroyed=0;
    const bool have=Fields(self,before,destroyed,pos);
    if(msg==kMsgAction && data && Rd(data,4,"action message data"))
        Log("EDF41MOTHER ObjectAction %p action %d (message 0x%X)",self,*static_cast<const std::int32_t*>(data),msg);
    else if(msg==kMsgParts && data && Rd(data,8,"parts message data"))
        Log("EDF41MOTHER GetPartsObject %p group %d index %d",self,static_cast<const std::int32_t*>(data)[0],static_cast<const std::int32_t*>(data)[1]);
    else if(seen<=3 || (important && msg==kMsgDamage && seen%20==1))
        Log("EDF41MOTHER message 0x%X #%d to %p data %p",msg,seen,self,data);
    const std::uint64_t result=reinterpret_cast<MessageFn>(origSlots[kSlotMessage])(self,msg,data);
    if(msg==kMsgDamage && have) {
        float after=before;
        std::uint8_t destroyedAfter=destroyed;
        float p2[3]={};
        if(Fields(self,after,destroyedAfter,p2) && (after!=before || destroyedAfter!=destroyed))
            Log("EDF41MOTHER damage %p durability %.1f -> %.1f destroyed %u -> %u",self,before,after,destroyed,destroyedAfter);
    }
    return result;
}

// ---- the creation ----------------------------------------------------------------------------------------------
void __fastcall FactoryCreate(Factory*,void* info) noexcept {
    if(!info || !Rd(info,0x28,"createInfo")) { Log("EDF41MOTHER create: no create info (%p), nothing made",info); return; }
    if(!installed.load()) { Log("EDF41MOTHER create: the vtable was not built, nothing made"); return; }
    void* obj=reinterpret_cast<NewFn>(image+kNew)(kCarrierSize);
    if(!obj) { Log("EDF41MOTHER create: out of memory for %zu bytes",kCarrierSize); return; }
    reinterpret_cast<CtorFn>(image+kCarrierCtor)(obj,info);
    void** vptr=static_cast<void**>(obj);
    if(!Rd(vptr,sizeof(void*),"new object") || *vptr!=image+kCarrierVtable) {
        Log("EDF41MOTHER create: object %p has vtable %p, not UfoCarrier508's %p: left as the class made it",obj,Rd(vptr,sizeof(void*),"new object")?*vptr:nullptr,static_cast<void*>(image+kCarrierVtable));
        return;
    }
    *vptr=&cloneBlock[1];
    const int n=++created;
    Log("EDF41MOTHER create #%d: UfoMother301 object %p built by UfoCarrier508's constructor, vtable %p -> clone %p; info %p",n,obj,static_cast<void*>(image+kCarrierVtable),static_cast<void*>(&cloneBlock[1]),info);
}

// ---- the registration -----------------------------------------------------------------------------------------
bool RegisterFactoryNow() noexcept {
    if(registered.exchange(true))return true;
    void* manager=nullptr;
    if(!Rd(image+kManagerGlobal,sizeof(void*),"manager global") || (manager=*reinterpret_cast<void**>(image+kManagerGlobal))==nullptr) {
        Log("EDF41MOTHER register: the scene object manager does not exist yet; UfoMother301 is not registered");
        registered=false;
        return false;
    }
    if(!Rd(manager,0x300,"manager object")) { registered=false; return false; }
    struct Shared { void* object; void* control; } shared{&factory,&control};
    reinterpret_cast<RegisterFn>(image+kRegisterFactory)(manager,&shared);
    void* found=reinterpret_cast<GetFactoryFn>(image+kGetFactory)(manager,L"UfoMother301");
    Log("EDF41MOTHER registered UfoMother301 with manager %p: lookup gives %p (ours %p) -> %s",manager,found,static_cast<void*>(&factory),found==&factory ? "OK" : "FAILED");
    return found==&factory;
}

std::uint64_t __fastcall RegisterAllHook(void* a,void* b,void* c,void* d) {
    const std::uint64_t result=reinterpret_cast<RegisterAllFn>(image+kRegisterAll)(a,b,c,d);
    RegisterFactoryNow();
    return result;
}

// Counting stubs for the slots we do not wrap: mov rax,&count / lock inc [rax] / mov rax,orig / jmp rax.
bool BuildClone() noexcept {
    for(const Print& p:kPrints) {
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41MOTHER the code at EDF.dll+0x%zX is not what this was written for: prototype off",p.rva);
            return false;
        }
    }
    const void* const* vt=reinterpret_cast<const void* const*>(image+kCarrierVtable);
    if(!Readable(vt-1,sizeof(void*)*(kSlots+1))) { Log("EDF41MOTHER the UfoCarrier508 vtable is not readable: prototype off"); return false; }
    cloneBlock[0]=const_cast<void*>(vt[-1]);
    for(int i=0;i<kSlots;++i) {
        origSlots[i]=const_cast<void*>(vt[i]);
        const auto at=reinterpret_cast<std::uintptr_t>(origSlots[i]);
        if(at<reinterpret_cast<std::uintptr_t>(image) || at>=reinterpret_cast<std::uintptr_t>(image)+0x22CE000) {
            Log("EDF41MOTHER vtable slot %d (%p) is outside EDF.dll: prototype off",i,origSlots[i]);
            return false;
        }
    }
    constexpr std::size_t kStub=32;
    unsigned char code[kSlots*kStub];
    std::memset(code,0xCC,sizeof(code));
    for(int i=0;i<kSlots;++i) {
        unsigned char* s=code+static_cast<std::size_t>(i)*kStub;
        const std::uint64_t counter=reinterpret_cast<std::uint64_t>(&slotCalls[i]),target=reinterpret_cast<std::uint64_t>(origSlots[i]);
        s[0]=0x48; s[1]=0xB8; std::memcpy(s+2,&counter,8);     // mov rax, counter
        s[10]=0xF0; s[11]=0xFF; s[12]=0x00;                     // lock inc dword ptr [rax]
        s[13]=0x48; s[14]=0xB8; std::memcpy(s+15,&target,8);   // mov rax, original
        s[23]=0xFF; s[24]=0xE0;                                 // jmp rax
    }
    unsigned char* stubs=static_cast<unsigned char*>(AllocateNearCode(image,code,sizeof(code)));
    if(!stubs) { Log("EDF41MOTHER no executable page for the counting stubs: prototype off"); return false; }
    for(int i=0;i<kSlots;++i)
        cloneBlock[1+i]=stubs+static_cast<std::size_t>(i)*kStub;
    cloneBlock[1+kSlotDtor]=reinterpret_cast<void*>(&DtorWrap);
    cloneBlock[1+kSlotUpdate]=reinterpret_cast<void*>(&UpdateWrap);
    cloneBlock[1+kSlotMessage]=reinterpret_cast<void*>(&MessageWrap);
    return true;
}
}  // namespace

bool InstallEdf41Mother() noexcept {
    if(!Cfg().edf41MotherDev) {
        Log("EDF41MOTHER off (ini Edf41MotherDev=0)");
        return false;
    }
    if(!BuildClone())return false;
    installed=true;
    bool changed=false;
    const bool ok=RedirectCall(image+kRegisterAllSite,image+kRegisterAll,reinterpret_cast<void*>(&RegisterAllHook),changed);
    // When this runs the game's start-up routine has not reached 0x701859 yet (the manager is null); if it has, the hook
    // can no longer fire and the log says so instead of the registration line.
    const bool late=Readable(image+kManagerGlobal,sizeof(void*)) && *reinterpret_cast<void**>(image+kManagerGlobal)!=nullptr;
    Log("EDF41MOTHER development switch ON: vtable clone built (%d slots, wrapped %d/%d/%d), register hook %s%s",kSlots,kSlotDtor,kSlotUpdate,kSlotMessage,ok ? "installed" : "FAILED",
        late ? " (WARNING: the scene object manager already exists, the game's registration ran before this plugin: UfoMother301 will not be registered)" : "");
    return ok;
}
}  // namespace crew
