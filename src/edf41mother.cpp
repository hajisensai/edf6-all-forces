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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <cstring>
#include <cwchar>
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
// Evidence probes (dev switch only), 2026-10-13: the hull was not seen on the real game. The model instance of a carrier is at
// +0x6F0 (UfoCarrier508's constructor hands it the object's matrix, +0x60, through 0x1100B90: it sets the dirty byte +0xB0 and
// writes the four rows into the scene node at [instance+0x10], +0xB0..+0xEF); the stream every file open ends in is the object
// of vtable 0x175F0F0, slot 1 = Open(stream, std::wstring* path, flags) (EDF.dll+741C0; EDFModLoader redirects "/cri_bind/..." there).
constexpr std::size_t kModelAt=0x6F0,kNodeAt=0x10,kNodeMatrixAt=0xB0,kObjMatrixAt=0x60;
constexpr std::size_t kStreamVtable=0x175F0F0,kStreamOpenSlot=1;
constexpr int kSlots=49;
constexpr int kSlotDtor=0,kSlotUpdate=5,kSlotMessage=9;
// UfoCarrier508's vtable 0x17C9DC0 as the file has it (slot -> EDF.dll RVA); 7 is the ship AI, 5 Update, 9 OnMessage.
constexpr std::uint32_t kStockSlots[kSlots]={
    0x54B010, 0x4F8A70, 0x54BE10, 0x4F9530, 0x4F72F0, 0x4F9540, 0x4F91C0, 0x4F6D70, 0x54A290, 0x4F93E0, 0x54AA50, 0x54A970,
    0x266BE0, 0x54ABE0, 0x54C4B0, 0x54A330, 0x4F7190, 0x54AA30, 0x4F9250, 0x54A510, 0x54A4B0, 0x54AAC0, 0x54ABD0, 0x266BB0,
    0x266BC0, 0x266970, 0x54A320, 0x266BF0, 0x54A240, 0x54A260, 0x54A250, 0x54A2E0, 0x266BD0, 0x266960, 0x54F8D0, 0x54F8E0,
    0x54C800, 0x54CAA0, 0x54C900, 0x267180, 0x54CBE0, 0x54CBC0, 0x54CBD0, 0x54CB60, 0x54C6E0, 0x4F37F0, 0x4F90A0, 0x4F3FD0,
    0x4F7D30};

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

// ---- evidence probes -------------------------------------------------------------------------------------------------
float Len3(const float* v) noexcept { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }

// What the object's model instance looks like: is there a scene node, where is its matrix (the world position of the drawn model),
// how big are its rows (a scale of 0 draws nothing), and how far is it from the object's own position.
void ProbeModel(const void* obj,const char* tag,const char* when) noexcept {
    const char* o=static_cast<const char*>(obj);
    if(!Rd(o+kModelAt,0xC0,"model instance") || !Rd(o+kObjMatrixAt,0x40,"object matrix"))return;
    const char* inst=o+kModelAt;
    const char* node=*reinterpret_cast<const char* const*>(inst+kNodeAt);
    const std::uint8_t dirty=*reinterpret_cast<const std::uint8_t*>(inst+0xB0);
    float objM[16];
    std::memcpy(objM,o+kObjMatrixAt,sizeof(objM));
    if(!node || reinterpret_cast<std::uintptr_t>(node)<0x10000) {
        Log("EDF41MOTHER probe %s %p (%s): the model instance +0x6F0 has NO scene node (node %p): the model was not created; object rows %.3f %.3f %.3f",
            tag,obj,when,static_cast<const void*>(node),Len3(objM),Len3(objM+4),Len3(objM+8));
        return;
    }
    if(!Rd(node+kNodeMatrixAt,0x40,"scene node matrix")) { Log("EDF41MOTHER probe %s %p (%s): scene node %p unreadable",tag,obj,when,static_cast<const void*>(node)); return; }
    float nm[16];
    std::memcpy(nm,node+kNodeMatrixAt,sizeof(nm));
    const float dx=nm[12]-objM[12],dy=nm[13]-objM[13],dz=nm[14]-objM[14];
    Log("EDF41MOTHER probe %s %p (%s): scene node %p, node rows %.3f %.3f %.3f, node pos %.1f %.1f %.1f, object pos %.1f %.1f %.1f, apart %.1f m, dirty byte %u",
        tag,obj,when,static_cast<const void*>(node),Len3(nm),Len3(nm+4),Len3(nm+8),nm[12],nm[13],nm[14],objM[12],objM[13],objM[14],std::sqrt(dx*dx+dy*dy+dz*dz),dirty);
}

// Control: the stock carriers' (and the 4.1 carriers') own objects, probed the same way, to compare with the mothership.
void* origCarrierUpdate=nullptr;
std::uint64_t __fastcall CarrierUpdateProbe(void* self,void* dt) {
    const std::uint64_t result=reinterpret_cast<UpdateFn>(origCarrierUpdate)(self,dt);
    static std::mutex lock;
    static const void* seen[4];
    static std::uint32_t frames[4];
    static int count=0;
    int at=-1;
    std::uint32_t frame=0;
    {
        std::lock_guard<std::mutex> hold(lock);
        for(int i=0;i<count;++i)
            if(seen[i]==self)at=i;
        if(at<0 && count<4) { at=count++; seen[at]=self; frames[at]=0; }
        if(at>=0)frame=++frames[at];
    }
    if(at>=0 && (frame==2 || frame%1800==0))ProbeModel(self,"CONTROL carrier",frame==2 ? "frame 2" : "periodic");
    return result;
}

// Every file the game opens whose name has to do with the mothership or the carriers, and whether the open worked.
using OpenFn=std::uint64_t(__fastcall*)(void*,const void*,void*);
void* origOpen=nullptr;
std::atomic<int> opensLogged{0};
std::uint64_t __fastcall OpenProbe(void* stream,const void* path,void* flags) {
    const std::uint64_t result=reinterpret_cast<OpenFn>(origOpen)(stream,path,flags);
    if(opensLogged.load()<80 && Rd(path,0x20,"open path string")) {
        const char* w=static_cast<const char*>(path);
        const std::uint64_t size=*reinterpret_cast<const std::uint64_t*>(w+0x10),cap=*reinterpret_cast<const std::uint64_t*>(w+0x18);
        const wchar_t* text=cap<8 ? reinterpret_cast<const wchar_t*>(w) : *reinterpret_cast<const wchar_t* const*>(w);
        if(size>0 && size<259 && Rd(text,(size+1)*sizeof(wchar_t),"open path text")) {
            wchar_t low[260];
            for(std::uint64_t i=0;i<=size;++i)low[i]=static_cast<wchar_t>(towlower(text[i]));
            if(std::wcsstr(low,L"mother") || std::wcsstr(low,L"carrier")) {
                ++opensLogged;
                Log("EDF41MOTHER file open %ls -> %s (al=%u)",text,(result&0xFF) ? "ok" : "FAILED",static_cast<unsigned>(result&0xFF));
            }
        }
    }
    return result;
}

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
    if(tick==2)ProbeModel(self,"mothership","frame 2");
    if(tick%1800==0)ProbeModel(self,"mothership","periodic");
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

// The first time an object is made (every plugin hook is in by then): which slots of UfoCarrier508's live table differ from the
// stock ones the clone was built from. Information only: the clone does not follow them (BuildClone).
void AuditLiveTable() noexcept {
    static std::atomic<bool> done{false};
    if(done.exchange(true))return;
    const void* const* vt=reinterpret_cast<const void* const*>(image+kCarrierVtable);
    if(!Rd(vt,sizeof(void*)*kSlots,"UfoCarrier508 vtable at creation"))return;
    int differ=0;
    for(int i=0;i<kSlots;++i)
        if(vt[i]!=image+kStockSlots[i]) {
            ++differ;
            Log("EDF41MOTHER at creation, UfoCarrier508 slot %d is %p (stock %p): not followed by the mothership",i,vt[i],static_cast<void*>(image+kStockSlots[i]));
        }
    Log("EDF41MOTHER at creation, %d of %d UfoCarrier508 slots are patched by someone; the clone uses the stock functions",differ,kSlots);
}

// ---- what model the object holds ---------------------------------------------------------------------------------------
// Why not the game's SGO accessors: 0xD7480 (and 0x53060 / 0x52DE0 / 0x52F30 / 0x551E0, which src/edf41ai.cpp uses on the
// Berserker) walk a DSGO (a dictionary: count at tree+0x18, index table at +8, 0x28-byte nodes at +0x28); this object's SGO is
// a plain one (UfoCarrier508's constructor reads it through 0x528A0 / 0x52790: blob + [blob+0xC] + index * 12), and a plain
// tree handed to 0xD7480 faulted at EDF+0xD74D9 (2026-10-13 real run, dump EDF6.exe.119468). Where the document sits in the
// object was not established, so nothing here calls the game to read it: the object's memory is searched, every word
// checked with Readable first, for a pointer to (or a pointer in a block pointed to by) the wide name the constructor was
// given, and where it is found is said.
bool WideEquals(const void* at,const wchar_t* text) noexcept {
    const std::size_t n=std::wcslen(text);
    if(!Readable(at,(n+1)*sizeof(wchar_t)))return false;
    return std::wmemcmp(static_cast<const wchar_t*>(at),text,n+1)==0;
}

// Offsets of `obj` (and of blocks it points to, as "+a/+b") that hold a pointer to `text`; at most `max` lines.
int FindName(const char* obj,std::size_t size,const wchar_t* text,char (*where)[40],int max) noexcept {
    int found=0;
    for(std::size_t a=0;a+8<=size && found<max;a+=8) {
        const char* p=*reinterpret_cast<const char* const*>(obj+a);
        if(reinterpret_cast<std::uintptr_t>(p)<0x10000 || !Readable(p,8))continue;
        if(WideEquals(p,text)) { std::snprintf(where[found++],40,"+0x%zX",a); continue; }
        if(!Readable(p,0x80))continue;
        for(std::size_t b=0;b+8<=0x80 && found<max;b+=8) {
            const char* q=*reinterpret_cast<const char* const*>(p+b);
            if(reinterpret_cast<std::uintptr_t>(q)<0x10000 || !Readable(q,8))continue;
            if(WideEquals(q,text))std::snprintf(where[found++],40,"+0x%zX/+0x%zX",a,b);
        }
    }
    return found;
}

// Said at creation. The names are what tools/make_edf41_objects.py wrote into the SGO (the archive and the MDB of
// animation_model[0][0]); the half extents of that MDB are in the SGO as edf41_mother_model and in the log of the generator.
void LogModel(void* obj) noexcept {
    const char* base=static_cast<const char*>(obj);
    if(!Rd(base,kCarrierSize,"new object memory"))return;
    static const wchar_t* const names[]={L"MotherShip301Body.mdb",L"app:/Object/UfoMother301.mrab",L"app:/object/UfoMother301.mrab",L"UfoMother301.mrab"};
    int total=0;
    for(const wchar_t* name:names) {
        char where[4][40];
        const int n=FindName(base,kCarrierSize,name,where,4);
        total+=n;
        for(int i=0;i<n;++i)Log("EDF41MOTHER object %p holds the name %ls at %s",obj,name,where[i]);
    }
    if(!total)Log("EDF41MOTHER object %p: none of the 4.1 model names is held by its first %zu bytes (searched two pointer levels)",obj,kCarrierSize);
}

// ---- the creation ----------------------------------------------------------------------------------------------
void __fastcall FactoryCreate(Factory*,void* info) noexcept {
    if(!info || !Rd(info,0x28,"createInfo")) { Log("EDF41MOTHER create: no create info (%p), nothing made",info); return; }
    if(!installed.load()) { Log("EDF41MOTHER create: the vtable was not built, nothing made"); return; }
    AuditLiveTable();
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
    LogModel(obj);
    ProbeModel(obj,"mothership","just created");
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
    // The clone is the STOCK class, built from the table of its slots as the file has them (kStockSlots), not from the live
    // vtable: this plugin patches slot 7 of UfoCarrier508's own table (carrierlaser.cpp ShipAiHook, the ship AI for the
    // teleportation laser: it recognises ships by the old vptr and steps the laser's state machine once a frame, so on a
    // mothership it would only add a second step per frame), and a clone taken before or after that patch would differ.
    // The live table is compared slot by slot; what differs is logged and left out of the clone.
    const void* const* vt=reinterpret_cast<const void* const*>(image+kCarrierVtable);
    if(!Readable(vt-1,sizeof(void*)*(kSlots+1))) { Log("EDF41MOTHER the UfoCarrier508 vtable is not readable: prototype off"); return false; }
    cloneBlock[0]=const_cast<void*>(vt[-1]);
    HMODULE self=nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(&BuildClone),&self);
    for(int i=0;i<kSlots;++i) {
        void* const stock=image+kStockSlots[i];
        void* const live=const_cast<void*>(vt[i]);
        if(!Readable(stock,8)) { Log("EDF41MOTHER stock slot %d (EDF.dll+0x%X) is not readable: prototype off",i,kStockSlots[i]); return false; }
        if(live!=stock) {
            HMODULE owner=nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(live),&owner);
            Log("EDF41MOTHER vtable slot %d of UfoCarrier508 is patched (%p, %s): the mothership keeps the stock function EDF.dll+0x%X",
                i,live,owner && owner==self ? "this plugin's hook" : "another module",kStockSlots[i]);
        }
        origSlots[i]=stock;
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
    if(ok) {
        // Evidence probes. Both patch a vtable slot only while it still holds the stock function (PatchVtableSlot checks).
        const auto openSlot=reinterpret_cast<void**>(image+kStreamVtable+kStreamOpenSlot*8);
        if(Readable(openSlot,8)) { origOpen=*openSlot; Log("EDF41MOTHER file-open probe %s",PatchVtableSlot(openSlot,origOpen,reinterpret_cast<void*>(&OpenProbe)) ? "installed" : "NOT installed"); }
        const auto carrierUpdate=reinterpret_cast<void**>(image+kCarrierVtable+kSlotUpdate*8);
        if(Readable(carrierUpdate,8) && *carrierUpdate==image+kStockSlots[kSlotUpdate]) {
            origCarrierUpdate=*carrierUpdate;
            Log("EDF41MOTHER control probe on the stock carriers %s",PatchVtableSlot(carrierUpdate,origCarrierUpdate,reinterpret_cast<void*>(&CarrierUpdateProbe)) ? "installed" : "NOT installed");
        } else Log("EDF41MOTHER control probe not installed: UfoCarrier508's Update slot is not the stock one");
    }
    // When this runs the game's start-up routine has not reached 0x701859 yet (the manager is null); if it has, the hook
    // can no longer fire and the log says so instead of the registration line.
    const bool late=Readable(image+kManagerGlobal,sizeof(void*)) && *reinterpret_cast<void**>(image+kManagerGlobal)!=nullptr;
    Log("EDF41MOTHER development switch ON: vtable clone built (%d slots, wrapped %d/%d/%d), register hook %s%s",kSlots,kSlotDtor,kSlotUpdate,kSlotMessage,ok ? "installed" : "FAILED",
        late ? " (WARNING: the scene object manager already exists, the game's registration ran before this plugin: UfoMother301 will not be registered)" : "");
    return ok;
}
}  // namespace crew
