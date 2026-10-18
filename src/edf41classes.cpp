// EDF4.1 classes as the plugin's own scene object classes: the shared machinery (src/edf41classes.h).
// The factory registration and the vtable clone come from the UfoMother301 prototype (docs/edf41-ufomother301.md phase 1, real
// runs 2026-10-13 .. 10-15); the bullet from the dragon (edf41dragon.cpp, real runs 2026-10-14).
// EDF.dll TimeDateStamp 0x678CCB46.
#include "edf41classes.h"

#include "crew.h"
#include "memory.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace crew::edf41c {
namespace {
constexpr std::size_t kRegisterAllSite=0x701859;      // call 0x79D570 in the start-up routine
constexpr std::size_t kRegisterAll=0x79D570;          // registers the game's scene object factories
constexpr std::size_t kRegisterFactory=0x119AF00;     // RegisterSceneObjectFactory(manager, shared_ptr<Factory>*)
constexpr std::size_t kGetFactory=0x1195C50;          // GetSceneObjectFactory(manager, const wchar_t* name)
constexpr std::size_t kManagerGlobal=0x20B2958;       // the scene system (factories, objects, bullets, cameras)
constexpr std::size_t kNew=0x12D85B0;                 // the game's operator new(size)
constexpr std::size_t kCreateFromPath=0x11945E0;      // Object* (manager, const float matrix[16], const wchar_t* sgoPath, CreateContext*)
constexpr std::size_t kSgoKey=0x528A0;                // node* (SgoRoot* = object+0xA0, const wchar_t* key)
constexpr std::size_t kDeleteObject=0x118A1B0;        // marks a scene object deleted (bit 4 of +0x18)
constexpr std::size_t kCreateContextVtable=0x1762068;
constexpr std::size_t kSetCasState=0x116C2D0;         // bool (CasPlayers* = object+0x8F0, int player = [object+0xC80], const wchar_t* state, int)
constexpr std::size_t kCasAt=0x8F0,kCasPlayerAt=0xC80;
constexpr std::size_t kFactoryByName=0x1195C50;       // the same lookup serves bullet classes
constexpr std::size_t kParamCtor=0x1001D0,kParamDtor=0x100C90,kWeakAssign=0x6CEBB0,kSpawn=0x1194280;
constexpr std::size_t kParamSize=0x1E0;

struct Print { std::size_t rva; unsigned char bytes[8]; };
constexpr Print kPrints[]={
    {kRegisterAllSite,{0xE8,0x12,0xBD,0x09,0x00,0xE8,0xED,0xCF}},
    {kRegisterAll,{0x40,0x55,0x53,0x56,0x57,0x48,0x8D,0xAC}},
    {kRegisterFactory,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74}},
    {kNew,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kCarrierCtor,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},
    {0x4F8A70,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // UfoCarrier508 slot 1, the deleting destructor (its shape: kDtors)
    {0x4F9540,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // slot 5
    {0x4F93E0,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74}},   // slot 9
    {kCreateFromPath,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41}},
    {kSgoKey,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kDeleteObject,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kSetCasState,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kEaoCtor,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},
    {0x5B64F0,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // EventAnimationObject slot 1, the deleting destructor (kDtors)
    {0x5B6D20,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // slot 5
    {0x5B6540,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57}},   // slot 9
};

// The deleting destructors a clone's slot kSlotDtor may wrap: at `tail`, `test bl,1 / je +0x0D / mov edx,size` (flags kept in
// ebx from edx; bit 0 frees the object, operator delete 0x12D85EC with the class's size). A slot that is not one of these (the
// slot 0 of both classes is not) is never wrapped as the destructor.
struct DtorShape { std::uint32_t rva,tail,size; };
constexpr DtorShape kDtors[]={
    {0x5B64F0,0x5B651F,0xC90},    // EventAnimationObject: ragdoll +0xAA0, model +0x5C0, base 0x547470, delete 0xC90
    {0x4F8A70,0x4F8AC7,0x1630},   // UfoCarrier508: members +0x1600 / +0x15D0, base 0x4F62F0, delete 0x1630
};
bool DtorShaped(std::uint32_t rva) noexcept {
    for(const DtorShape& d:kDtors) {
        if(d.rva!=rva)continue;
        const unsigned char want[8]={0xF6,0xC3,0x01,0x74,0x0D,0xBA,static_cast<unsigned char>(d.size&0xFF),static_cast<unsigned char>((d.size>>8)&0xFF)};
        return Readable(image+d.tail,8) && std::memcmp(image+d.tail,want,8)==0;
    }
    return false;
}
std::atomic<bool> shuttingDown{false};

using NewFn=void*(__fastcall*)(std::size_t);
using CtorFn=void*(__fastcall*)(void*,void*);
using RegisterFn=void(__fastcall*)(void*,void*);
using GetFactoryFn=void*(__fastcall*)(void*,const wchar_t*);
using RegisterAllFn=std::uint64_t(__fastcall*)(void*,void*,void*,void*);
using CreateFromPathFn=void*(__fastcall*)(void*,const float*,const wchar_t*,void*);
using SgoKeyFn=const void*(__fastcall*)(const void*,const wchar_t*);
using DeleteObjectFn=void(__fastcall*)(void*);
using ParamCtorFn=void*(__fastcall*)(void*);
using ParamDtorFn=void(__fastcall*)(void*);
using WeakAssignFn=void*(__fastcall*)(void*,const void*);
using SpawnFn=void*(__fastcall*)(void*,const float*,void*,void*);

void Fail(const char* what) noexcept {
    static std::mutex lock;
    static const char* said[64];
    static int count=0;
    std::lock_guard<std::mutex> hold(lock);
    for(int i=0;i<count;++i)
        if(said[i]==what)return;
    if(count<64)said[count++]=what;
    Log("EDF41CLASS check failed: %s",what);
}
bool Rd(const void* p,std::size_t size,const char* what) noexcept {
    if(Readable(p,size))return true;
    Fail(what);
    return false;
}
void* Manager() noexcept {
    if(!Readable(image+kManagerGlobal,8))return nullptr;
    return *reinterpret_cast<void**>(image+kManagerGlobal);
}

// ---- factories: 8 bytes each, only a vtable {dtor, name, create}, and the shared_ptr control block the game's own registrations
// make (0x79D5C0: vtable, use +8, weak +0xC, object +0x10). The counts start far from 1, so the block is never released.
constexpr int kMostFactories=32;
struct Factory { const void* const* vtable; };
struct Control { const void* const* vtable; std::atomic<std::int32_t> use; std::atomic<std::int32_t> weak; void* object; };
void __fastcall ControlDestroy(void*) noexcept {}
void __fastcall ControlDelete(void*) noexcept {}
const void* const controlTable[2]={reinterpret_cast<const void*>(&ControlDestroy),reinterpret_cast<const void*>(&ControlDelete)};
Factory factories[kMostFactories];
Control controls[kMostFactories];
const wchar_t* names[kMostFactories];
CreateFn creates[kMostFactories];
std::atomic<int> factoryCount{0};
std::atomic<bool> ready{false},hooked{false},registered{false},checked{false},fingerprintsOk{false};

void* __fastcall FactoryDtor(Factory* self,unsigned) noexcept { return self; }
const wchar_t* __fastcall FactoryName(Factory* self) noexcept {
    const std::ptrdiff_t at=self-factories;
    return at>=0 && at<factoryCount.load() ? names[at] : L"";
}
void __fastcall FactoryCreate(Factory* self,void* info) noexcept {
    const std::ptrdiff_t at=self-factories;
    if(at<0 || at>=factoryCount.load()) return;
    if(!info || !Rd(info,0x28,"createInfo")) { Log("EDF41CLASS create %ls: no create info, nothing made",names[at]); return; }
    creates[at](info);
}
const void* const factoryTable[3]={reinterpret_cast<const void*>(&FactoryDtor),reinterpret_cast<const void*>(&FactoryName),
                                   reinterpret_cast<const void*>(&FactoryCreate)};

void RegisterNow() noexcept {
    if(registered.exchange(true))return;
    void* manager=Manager();
    if(!manager || !Rd(manager,0x300,"manager object")) {
        Log("EDF41CLASS register: the scene object manager does not exist yet; nothing registered");
        registered=false;
        return;
    }
    const int n=factoryCount.load();
    for(int i=0;i<n;++i) {
        struct Shared { void* object; void* control; } shared{&factories[i],&controls[i]};
        reinterpret_cast<RegisterFn>(image+kRegisterFactory)(manager,&shared);
        void* found=reinterpret_cast<GetFactoryFn>(image+kGetFactory)(manager,names[i]);
        Log("EDF41CLASS registered %ls: lookup gives %p (ours %p) -> %s",names[i],found,static_cast<void*>(&factories[i]),found==&factories[i] ? "OK" : "FAILED");
    }
}
std::uint64_t __fastcall RegisterAllHook(void* a,void* b,void* c,void* d) {
    const std::uint64_t result=reinterpret_cast<RegisterAllFn>(image+kRegisterAll)(a,b,c,d);
    RegisterNow();
    return result;
}

bool Fingerprints() noexcept {
    if(checked.exchange(true))return fingerprintsOk.load();
    for(const Print& p:kPrints)
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41CLASS the code at EDF.dll+0x%zX is not what this was written for: the 4.1 classes are off",p.rva);
            return false;
        }
    fingerprintsOk=true;
    return true;
}

bool Hook() noexcept {
    if(hooked.load())return true;
    if(!Fingerprints())return false;
    if(Manager()) {
        Log("EDF41CLASS the scene object manager already exists: the game's registration ran before the plugin, no 4.1 class can be registered");
        return false;
    }
    bool changed=false;
    if(!RedirectCall(image+kRegisterAllSite,image+kRegisterAll,reinterpret_cast<void*>(&RegisterAllHook),changed)) {
        Log("EDF41CLASS the registration hook failed: the 4.1 classes are off");
        return false;
    }
    hooked=true;
    ready=true;
    return true;
}

// Persistent node copies (Persist): never freed, a bullet keeps a pointer into one.
std::mutex persistLock;
std::deque<std::vector<char>> persisted;
std::size_t NodeBytes(const SgoNode* n,int depth) noexcept {
    if(depth>8 || !Readable(n,sizeof(SgoNode)))return 0;
    if(n->type==3)return sizeof(SgoNode)+static_cast<std::size_t>(n->a+1)*sizeof(wchar_t)+8;
    if(n->type!=0)return sizeof(SgoNode);
    if(n->a<0 || n->a>256)return 0;
    std::size_t total=sizeof(SgoNode)*(1+static_cast<std::size_t>(n->a));
    for(int i=0;i<n->a;++i) {
        const std::size_t c=NodeBytes(Child(n,i),depth+1);
        if(!c)return 0;
        total+=c;
    }
    return total;
}
// The node at `out + at` with its children laid out from `*end` (each array's children together); strings after.
bool CopyNode(const SgoNode* src,std::vector<char>& out,std::size_t at,std::size_t& end) noexcept {
    SgoNode copy=*src;
    if(src->type==3) {
        const std::size_t bytes=static_cast<std::size_t>(src->a)*sizeof(wchar_t);
        if(!Readable(reinterpret_cast<const char*>(src)+src->b,bytes))return false;
        std::memcpy(out.data()+end,reinterpret_cast<const char*>(src)+src->b,bytes);
        copy.b=static_cast<std::uint32_t>(end-at);
        end+=bytes+sizeof(wchar_t);
        end=(end+3)&~static_cast<std::size_t>(3);
    } else if(src->type==0) {
        const std::size_t first=end;
        end+=sizeof(SgoNode)*static_cast<std::size_t>(src->a);
        copy.b=static_cast<std::uint32_t>(first-at);
        for(int i=0;i<src->a;++i)
            if(!CopyNode(Child(src,i),out,first+sizeof(SgoNode)*static_cast<std::size_t>(i),end))return false;
    }
    std::memcpy(out.data()+at,&copy,sizeof(copy));
    return true;
}
}  // namespace

const std::uint32_t kEaoSlots[45]={
    0x5B6BB0, 0x5B64F0, 0x54BE10, 0x5B6D10, 0x5B6A10, 0x5B6D20, 0x125270, 0x54A000, 0x54A290, 0x5B6540, 0x54AA50, 0x54A970,
    0x266BE0, 0x54ABE0, 0x54C4B0, 0x54A330, 0x54AA40, 0x54AA30, 0x54A280, 0x54A510, 0x54A4B0, 0x54AAC0, 0x5B6BA0, 0x266BB0,
    0x266BC0, 0x266970, 0x54A320, 0x266BF0, 0x54A240, 0x54A260, 0x54A250, 0x54A2E0, 0x266BD0, 0x266960, 0x54F8D0, 0x54F8E0,
    0x54C800, 0x54CAA0, 0x54C900, 0x267180, 0x54CBE0, 0x54CBC0, 0x54CBD0, 0x54CB60, 0x54C6E0};
const std::uint32_t kCarrierSlots[49]={
    0x54B010, 0x4F8A70, 0x54BE10, 0x4F9530, 0x4F72F0, 0x4F9540, 0x4F91C0, 0x4F6D70, 0x54A290, 0x4F93E0, 0x54AA50, 0x54A970,
    0x266BE0, 0x54ABE0, 0x54C4B0, 0x54A330, 0x4F7190, 0x54AA30, 0x4F9250, 0x54A510, 0x54A4B0, 0x54AAC0, 0x54ABD0, 0x266BB0,
    0x266BC0, 0x266970, 0x54A320, 0x266BF0, 0x54A240, 0x54A260, 0x54A250, 0x54A2E0, 0x266BD0, 0x266960, 0x54F8D0, 0x54F8E0,
    0x54C800, 0x54CAA0, 0x54C900, 0x267180, 0x54CBE0, 0x54CBC0, 0x54CBD0, 0x54CB60, 0x54C6E0, 0x4F37F0, 0x4F90A0, 0x4F3FD0,
    0x4F7D30};

bool Ready() noexcept { return ready.load(); }

bool AddFactory(const wchar_t* className,CreateFn create) noexcept {
    if(!Hook())return false;
    if(registered.load()) { Log("EDF41CLASS %ls added after the registration: not registered",className); return false; }
    const int i=factoryCount.load();
    if(i>=kMostFactories) { Log("EDF41CLASS no room for %ls",className); return false; }
    factories[i].vtable=factoryTable;
    controls[i].vtable=controlTable;
    controls[i].use=0x40000000;
    controls[i].weak=0x40000000;
    controls[i].object=&factories[i];
    names[i]=className;
    creates[i]=create;
    factoryCount=i+1;
    return true;
}

// Counting stubs for the slots not replaced: mov rax,&count / lock inc [rax] / mov rax,orig / jmp rax.
bool BuildClone(Clone& c,DtorFn dtor,UpdateFn update,MessageFn message) noexcept {
    if(!Fingerprints())return false;
    if(dtor && (c.slots<=kSlotDtor || !DtorShaped(c.rvas[kSlotDtor]))) {
        Log("EDF41CLASS the %s slot %d (EDF.dll+0x%X) is not a deleting destructor of the shape this was written for: no clone",c.tag,kSlotDtor,
            c.slots>kSlotDtor ? c.rvas[kSlotDtor] : 0u);
        return false;
    }
    const void* const* vt=reinterpret_cast<const void* const*>(image+c.vtable);
    if(c.slots>64 || !Readable(vt-1,sizeof(void*)*static_cast<std::size_t>(c.slots+1))) { Log("EDF41CLASS the %s vtable is not readable",c.tag); return false; }
    c.block[0]=const_cast<void*>(vt[-1]);
    for(int i=0;i<c.slots;++i) {
        void* const stock=image+c.rvas[i];
        if(!Readable(stock,8)) { Log("EDF41CLASS stock slot %d of %s (EDF.dll+0x%X) is not readable",i,c.tag,c.rvas[i]); return false; }
        if(vt[i]!=stock)Log("EDF41CLASS vtable slot %d of %s is patched (%p): the clone keeps the stock EDF.dll+0x%X",i,c.tag,vt[i],c.rvas[i]);
        c.orig[i]=stock;
    }
    constexpr std::size_t kStub=32;
    unsigned char code[64*kStub];
    std::memset(code,0xCC,sizeof(code));
    for(int i=0;i<c.slots;++i) {
        unsigned char* s=code+static_cast<std::size_t>(i)*kStub;
        const std::uint64_t counter=reinterpret_cast<std::uint64_t>(&c.calls[i]),target=reinterpret_cast<std::uint64_t>(c.orig[i]);
        s[0]=0x48; s[1]=0xB8; std::memcpy(s+2,&counter,8);
        s[10]=0xF0; s[11]=0xFF; s[12]=0x00;
        s[13]=0x48; s[14]=0xB8; std::memcpy(s+15,&target,8);
        s[23]=0xFF; s[24]=0xE0;
    }
    unsigned char* stubs=static_cast<unsigned char*>(AllocateNearCode(image,code,static_cast<std::size_t>(c.slots)*kStub));
    if(!stubs) { Log("EDF41CLASS no executable page for the %s stubs",c.tag); return false; }
    for(int i=0;i<c.slots;++i)c.block[1+i]=stubs+static_cast<std::size_t>(i)*kStub;
    if(dtor)c.block[1+kSlotDtor]=reinterpret_cast<void*>(dtor);
    if(update)c.block[1+kSlotUpdate]=reinterpret_cast<void*>(update);
    if(message)c.block[1+kSlotMessage]=reinterpret_cast<void*>(message);
    return true;
}

void* CloneObject(Clone& c,std::size_t ctor,std::size_t size,void* createInfo) noexcept {
    void* obj=reinterpret_cast<NewFn>(image+kNew)(size);
    if(!obj) { Log("EDF41CLASS %s: out of memory",c.tag); return nullptr; }
    reinterpret_cast<CtorFn>(image+ctor)(obj,createInfo);
    void** vptr=static_cast<void**>(obj);
    if(!Rd(vptr,sizeof(void*),"new object") || *vptr!=image+c.vtable) {
        Log("EDF41CLASS %s: object %p has another vtable, left as the class made it",c.tag,obj);
        return nullptr;
    }
    *vptr=&c.block[1];
    return obj;
}

// ---- SGO --------------------------------------------------------------------------------------------------------
const SgoNode* Key(const void* object,const wchar_t* key) noexcept {
    const char* root=static_cast<const char*>(object)+kSgoAt;
    if(!object || !Rd(root,8,"object SGO root"))return nullptr;
    const SgoNode* n=static_cast<const SgoNode*>(reinterpret_cast<SgoKeyFn>(image+kSgoKey)(root,key));
    return n && Rd(n,sizeof(SgoNode),"SGO key node") ? n : nullptr;
}
const SgoNode* Child(const SgoNode* array,int i) noexcept {
    if(!array || array->type!=0 || i<0 || i>=array->a)return nullptr;
    const SgoNode* c=reinterpret_cast<const SgoNode*>(reinterpret_cast<const char*>(array)+array->b+static_cast<std::size_t>(i)*sizeof(SgoNode));
    return Rd(c,sizeof(SgoNode),"SGO child node") ? c : nullptr;
}
int Count(const SgoNode* array) noexcept { return array && array->type==0 ? array->a : -1; }
bool Float(const SgoNode* n,float* out) noexcept {
    if(!n)return false;
    if(n->type==2) { std::memcpy(out,reinterpret_cast<const char*>(n)+8,4); return std::isfinite(*out); }
    if(n->type==1) { *out=static_cast<float>(*reinterpret_cast<const std::int32_t*>(reinterpret_cast<const char*>(n)+8)); return true; }
    return false;
}
bool Int(const SgoNode* n,std::int32_t* out) noexcept {
    if(!n)return false;
    if(n->type==1) { std::memcpy(out,reinterpret_cast<const char*>(n)+8,4); return true; }
    float f;
    if(n->type==2 && Float(n,&f)) { *out=static_cast<std::int32_t>(f); return true; }
    return false;
}
bool Vec(const SgoNode* n,float* out,int count) noexcept {
    if(Count(n)<count)return false;
    for(int i=0;i<count;++i)
        if(!Float(Child(n,i),out+i))return false;
    return true;
}
bool Text(const SgoNode* n,wchar_t* out,std::size_t capacity) noexcept {
    if(!n || n->type!=3 || n->a<0 || static_cast<std::size_t>(n->a)+1>capacity)return false;
    const wchar_t* t=reinterpret_cast<const wchar_t*>(reinterpret_cast<const char*>(n)+n->b);
    if(n->a && !Rd(t,static_cast<std::size_t>(n->a)*sizeof(wchar_t),"SGO string"))return false;
    std::wmemcpy(out,t,static_cast<std::size_t>(n->a));
    out[n->a]=L'\0';
    return true;
}
const SgoNode* Persist(const SgoNode* n) noexcept {
    const std::size_t bytes=NodeBytes(n,0);
    if(!bytes)return nullptr;
    std::lock_guard<std::mutex> hold(persistLock);
    persisted.emplace_back(bytes+16,'\0');
    std::vector<char>& out=persisted.back();
    std::size_t end=sizeof(SgoNode);
    if(!CopyNode(n,out,0,end)) { persisted.pop_back(); return nullptr; }
    return reinterpret_cast<const SgoNode*>(out.data());
}

// ---- objects ----------------------------------------------------------------------------------------------------
void* CreateFromPath(const float* matrix,const wchar_t* sgoPath) noexcept {
    void* manager=Manager();
    if(!manager) { Fail("the scene manager is gone"); return nullptr; }
    alignas(16) unsigned char context[0x30]={};
    const std::uint64_t vt=reinterpret_cast<std::uint64_t>(image+kCreateContextVtable);
    std::memcpy(context,&vt,8);
    // The SceneObject constructor 0x1189C00 loads the four rows with MOVAPS (1189CB0..1189CCE; the native tests
    // support_soldier_create_native / create_jet_native fault on a misaligned one). Callers build plain float[16]
    // locals (edf41mother.cpp, edf41mother401.cpp), which C++ aligns to 4 only: copy, as CreateJet and the soldiers do.
    alignas(16) float nativeMatrix[16];
    std::memcpy(nativeMatrix,matrix,sizeof(nativeMatrix));
    return reinterpret_cast<CreateFromPathFn>(image+kCreateFromPath)(manager,nativeMatrix,sgoPath,context);
}
void DeleteObject(void* object) noexcept {
    const char* o=static_cast<const char*>(object);
    if(object && Rd(o+kFlagsAt,1,"object flags") && !(o[kFlagsAt]&4))reinterpret_cast<DeleteObjectFn>(image+kDeleteObject)(object);
}
bool Alive(const void* object) noexcept {
    const char* o=static_cast<const char*>(object);
    if(!object || !Rd(o+kFlagsAt,1,"object flags") || (o[kFlagsAt]&4) || !Rd(o+kDurabilityAt,4,"object durability"))return false;
    float d;
    std::memcpy(&d,o+kDurabilityAt,4);
    return d>0.0f;
}
bool PlayState(void* object,const wchar_t* state) noexcept {
    char* o=static_cast<char*>(object);
    if(!object || !Rd(o+kCasPlayerAt,4,"CAS player index") || !Rd(o+kCasAt,8,"CAS players"))return false;
    const int casPlayer=*reinterpret_cast<const std::int32_t*>(o+kCasPlayerAt);
    return reinterpret_cast<bool(__fastcall*)(void*,int,const wchar_t*,int)>(image+kSetCasState)(o+kCasAt,casPlayer,state,0);
}

// ---- matrices ---------------------------------------------------------------------------------------------------
void Multiply(const float* a,const float* b,float* out) noexcept {
    float r[16];
    for(int i=0;i<4;++i)
        for(int j=0;j<4;++j)
            r[i*4+j]=a[i*4+0]*b[0*4+j]+a[i*4+1]*b[1*4+j]+a[i*4+2]*b[2*4+j]+a[i*4+3]*b[3*4+j];
    std::memcpy(out,r,sizeof(r));
}
void EulerMatrix(const float* degrees,const float* position,float* out) noexcept {
    const float d2r=0.017453292f;
    const float cx=std::cos(degrees[0]*d2r),sx=std::sin(degrees[0]*d2r),cy=std::cos(degrees[1]*d2r),sy=std::sin(degrees[1]*d2r),
                cz=std::cos(degrees[2]*d2r),sz=std::sin(degrees[2]*d2r);
    const float rx[16]={1,0,0,0, 0,cx,sx,0, 0,-sx,cx,0, 0,0,0,1};
    const float ry[16]={cy,0,-sy,0, 0,1,0,0, sy,0,cy,0, 0,0,0,1};
    const float rz[16]={cz,sz,0,0, -sz,cz,0,0, 0,0,1,0, 0,0,0,1};
    float m[16];
    Multiply(rx,ry,m);
    Multiply(m,rz,m);
    m[12]=position[0]; m[13]=position[1]; m[14]=position[2]; m[15]=1.0f;
    std::memcpy(out,m,sizeof(m));
}
void AimMatrix(const float* from,const float* dir,float* out) noexcept {
    float f[3]={dir[0],dir[1],dir[2]};
    const float fl=std::sqrt(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if(fl<1e-6f) { f[0]=0; f[1]=-1; f[2]=0; } else { f[0]/=fl; f[1]/=fl; f[2]/=fl; }
    float r[3]={f[2],0.0f,-f[0]};
    float rl=std::sqrt(r[0]*r[0]+r[2]*r[2]);
    if(rl<1e-4f) { r[0]=1.0f; r[2]=0.0f; rl=1.0f; }
    r[0]/=rl; r[2]/=rl;
    const float u[3]={f[1]*r[2]-f[2]*r[1],f[2]*r[0]-f[0]*r[2],f[0]*r[1]-f[1]*r[0]};
    const float m[16]={r[0],r[1],r[2],0.0f,u[0],u[1],u[2],0.0f,f[0],f[1],f[2],0.0f,from[0],from[1],from[2],1.0f};
    std::memcpy(out,m,sizeof(m));
}
float Random01(std::uint64_t& state) noexcept {
    state=state*6364136223846793005ull+1442695040888963407ull;
    return static_cast<float>((state>>40)&0xFFFFFF)/16777216.0f;
}

// ---- bullets ----------------------------------------------------------------------------------------------------
namespace { void FillRawCue(unsigned char* preset,const wchar_t* cue,const SeParams& p) noexcept; }
bool Fire(void* owner,const float* matrix,const Bullet& b) noexcept {
    void* mgr=Manager();
    void* factory=mgr ? reinterpret_cast<GetFactoryFn>(image+kFactoryByName)(mgr,b.factory) : nullptr;
    if(!factory) { Fail("a bullet class EDF6 does not register"); return false; }
    char* o=static_cast<char*>(owner);
    if(!owner || !Rd(o+kTeamAt,4,"bullet owner team") || !Rd(o+kWeakSelfAt,16,"bullet owner weak pointer"))return false;
    alignas(16) unsigned char param[kParamSize];
    reinterpret_cast<ParamCtorFn>(image+kParamCtor)(param);
    std::memcpy(param+0x94,&b.speed,4);
    std::memcpy(param+0x98,&b.life,4);
    std::memcpy(param+0x9C,&b.damage,4);
    if(b.explosion>0.0f)std::memcpy(param+0xB0,&b.explosion,4);
    if(b.gravity!=0.0f)std::memcpy(param+0xE0,&b.gravity,4);
    if(b.radiusFactor>=0.0f)std::memcpy(param+0xC4,&b.radiusFactor,4);
    if(b.speedFactor>=0.0f)std::memcpy(param+0xC8,&b.speedFactor,4);
    std::memcpy(param+0xC0,&b.size,4);
    std::memcpy(param+0xCC,&b.hitStop,4);
    std::memcpy(param+0xD0,b.color,16);
    param[0xB7]=1;   // direct hits pass the shooter's friends
    std::memcpy(param+0x90,o+kTeamAt,4);
    reinterpret_cast<WeakAssignFn>(image+kWeakAssign)(param+0x38,o+kWeakSelfAt);
    if(b.custom) {
        const void* custom=b.custom;
        const std::uint16_t tag=1;
        std::memcpy(param+0xE8,&custom,8);
        std::memcpy(param+0xF8,&tag,2);
    }
    if(b.hitSe && b.hitSeTable) {   // the hit sound (UfoMother301, UfoRobo): a 4.1 preset name
        if(!FillSePreset(param+0x100,b.hitSeTable,b.hitSe)) {
            static std::atomic<int> said{0};
            if(said.fetch_add(1)<8)Log("EDF41CLASS bullet hit sound %ls: no such 4.1 preset / cue",b.hitSe);
        }
    } else if(b.hitCue && b.hitCueParams)FillRawCue(param+0x100,b.hitCue,*b.hitCueParams);   // the 401: a raw cue (none found: silent)
    static std::atomic<std::uint32_t> seed{0x9E3779B9u};
    const std::uint32_t s=(seed+=0x6D2B79F5u)>>16;
    std::memcpy(param+0x1A0,&s,4);
    alignas(16) float m[16];
    std::memcpy(m,matrix,sizeof(m));
    reinterpret_cast<SpawnFn>(image+kSpawn)(mgr,m,factory,param);
    reinterpret_cast<ParamDtorFn>(image+kParamDtor)(param);
    return true;
}

// ---- the player -------------------------------------------------------------------------------------------------
bool NearestPlayer(const float* from,float* out) noexcept {
    (void)from;
    const unsigned char* h=PlayerHuman();
    if(!h || !Rd(h+kPosAt,12,"player position"))return false;
    std::memcpy(out,h+kPosAt,12);
    return std::isfinite(out[0]+out[1]+out[2]);
}

void DevShoot(const void* object,const float* point,unsigned salt,float startBack) noexcept {
    const int every=Cfg().edf41DevShoot;
    if(every<=0 || !object || !point || !std::isfinite(point[0]+point[1]+point[2]))return;
    static std::mutex lock;
    static std::unordered_map<std::uintptr_t,unsigned> calls;
    {
        std::lock_guard<std::mutex> hold(lock);
        if(calls.size()>4096)calls.clear();
        const unsigned n=calls[reinterpret_cast<std::uintptr_t>(object)^(static_cast<std::uintptr_t>(salt)<<48)]++;
        if(salt<1000000u && (n+salt*7)%static_cast<unsigned>(every)!=0)return;   // salt >= 1000000: fire on every call (the caller paces)
    }
    void* shooter=PlayerHuman();
    float eye[3];
    if(!shooter || !NearestPlayer(point,eye))return;
    eye[1]+=1.6f;
    float dir[3]={point[0]-eye[0],point[1]-eye[1],point[2]-eye[2]};
    const float len=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
    if(len<1.0f)return;
    for(float& d : dir)d/=len;
    const float back=len<startBack ? len : startBack;
    const float from[3]={point[0]-dir[0]*back,point[1]-dir[1]*back,point[2]-dir[2]*back};
    alignas(16) float m[16];
    AimMatrix(from,dir,m);
    Bullet b;
    b.factory=L"LaserBullet02";
    b.speed=10.0f;
    b.life=6;
    b.damage=Cfg().edf41DevShootDamage;
    b.size=1.0f;
    // ini Edf41DevShootBlast > 0: an explosion of that radius. The bullet core then sets 0x10 (explosion radius > 1e-6, 0x2320E3) and
    // a hit goes through the area damage only (0x232616 -> ApplyAreaDamage 0x542670; the direct-hit queue is cleared, 0x23268E), the
    // path every rocket and missile takes: what a part takes from them, seen in its class's own taken / refused counts.
    if(std::isfinite(Cfg().edf41DevShootBlast) && Cfg().edf41DevShootBlast>0.0f)b.explosion=Cfg().edf41DevShootBlast;
    // LaserBullet02's constructor throws without its custom parameter (EDF+0x253F8B): [mode], 0 plain (docs/edf41-ufomother301.md
    // 2.1; E510's mf_LaserCustomParameter). An SGO array node {0, 1 child, at +12} and its int node {1, -, value 0}.
    alignas(4) static const std::int32_t plain[6]={0,1,12,1,0,0};
    b.custom=reinterpret_cast<const SgoNode*>(plain);
    const bool ok=Fire(shooter,m,b);
    static std::atomic<unsigned> shots{0};
    const unsigned k=++shots;
    if(k<=5 || k%200==0)Log("EDF41DEV shot #%u at %p (%.0f %.0f %.0f) from %.0f m short, player %p, damage %.0f: %s",k,object,point[0],point[1],point[2],
                            back,shooter,b.damage,ok ? "fired" : "not fired");
}

// ---- EDF6 primitives (re6/PRIMITIVES.md) --------------------------------------------------------------------------
namespace {
constexpr std::size_t kFindLocator=0x6BADD0,kLocatorMatrix=0x6BB5A0,kBoneIndex=0x11002A0,kSetTeam=0x54EE70,kSetLevel=0x54E740,
    kDifficulty=0xD7720,kGameStatus=0x20B2890,kGetTarget=0x30C0F0,kBodyByName=0x6EA670,kBodyById=0x6EA430,kPresetInit=0x7B3200,
    kCueLookup=0x7AEAE0,kSoundSystem=0x20B2948,kPresetPlay=0x7B4510,kSoundMove=0x7A8C20,kSoundStop=0x7A8CD0,kEffects=0x20B2980,
    kFlame=0x2E7FF0,kBlazing=0x2E0B70,kExplosion=0x2E5ED0,kExplosionBig=0x2E7A30,kDropItems=0x548AE0,kActivate=0x548D50;
constexpr Print kPrimPrints[]={
    {kFindLocator,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41}},{kLocatorMatrix,{0x48,0x8B,0xC4,0x48,0x89,0x70,0x10,0x48}},
    {kBoneIndex,{0x48,0x8B,0x09,0xE9,0xB8,0x7D,0x00,0x00}},{kSetTeam,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kSetLevel,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55}},{kDifficulty,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74}},
    {kGetTarget,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},{kBodyByName,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C}},
    {kBodyById,{0x4C,0x8B,0x41,0x78,0x45,0x33,0xC9,0x48}},{kPresetInit,{0x48,0x83,0xEC,0x18,0x45,0x33,0xC9,0xC7}},
    {kCueLookup,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},{kPresetPlay,{0x48,0x83,0xEC,0x48,0x48,0x8B,0x41,0x20}},
    {kSoundMove,{0x4C,0x8B,0x41,0x08,0x4D,0x85,0xC0,0x74}},{kSoundStop,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kFlame,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48}},{kBlazing,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},
    {kExplosion,{0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41}},{kExplosionBig,{0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41}},
    {kDropItems,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x48}},{kActivate,{0x40,0x53,0x48,0x83,0xEC,0x20,0x80,0xB9}},
    {kParamCtor,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74}},{kParamDtor,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74}},
    {kWeakAssign,{0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83}},{kSpawn,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x48}},
};
std::atomic<int> primState{0};   // 0 unchecked, 1 good, 2 refused
bool Prims() noexcept {
    int s=primState.load();
    if(s)return s==1;
    for(const Print& p:kPrimPrints)
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41CLASS the code at EDF.dll+0x%zX is not what this was written for: effects, sounds, teams and targets are off",p.rva);
            primState=2;
            return false;
        }
    primState=1;
    return true;
}
template<class F> F At(std::size_t rva) noexcept { return reinterpret_cast<F>(image+rva); }
void* Global(std::size_t rva) noexcept { return Readable(image+rva,8) ? *reinterpret_cast<void**>(image+rva) : nullptr; }
}  // namespace

void SetFlag(void* object,std::uint32_t bits,bool on) noexcept {
    char* o=static_cast<char*>(object);
    if(!object || !Rd(o+kFlags380,4,"object flags +0x380"))return;
    std::uint32_t& f=*reinterpret_cast<std::uint32_t*>(o+kFlags380);
    f=on ? (f|bits) : (f&~bits);
}

bool FindLocator(void* model,const wchar_t* name,LocatorHandle* out) noexcept {
    *out={nullptr,nullptr};
    if(!Prims() || !model || !Rd(model,8,"model component"))return false;
    return At<bool(__fastcall*)(void*,LocatorHandle*,int,const wchar_t*)>(kFindLocator)(model,out,0,name) && out->entry && out->bone;
}
bool LocatorMatrix(const LocatorHandle& h,float* out) noexcept {
    if(!Prims() || !h.entry || !h.bone)return false;
    alignas(16) float m[16];
    At<float*(__fastcall*)(const LocatorHandle*,float*)>(kLocatorMatrix)(&h,m);
    std::memcpy(out,m,sizeof(m));
    return std::isfinite(m[12]+m[13]+m[14]);
}
int BoneIndex(void* skeleton,const wchar_t* name) noexcept {
    if(!Prims() || !skeleton || !Rd(skeleton,0x28,"skeleton"))return -1;
    const std::wstring n(name);
    const int i=At<int(__fastcall*)(void*,const std::wstring*)>(kBoneIndex)(skeleton,&n);
    const std::int64_t count=*reinterpret_cast<const std::int64_t*>(static_cast<char*>(skeleton)+0x20);
    return i>=0 && i<count ? i : -1;
}
bool BoneMatrix(void* skeleton,int index,float* out) noexcept {
    if(index<0 || !skeleton || !Rd(skeleton,0x28,"skeleton"))return false;
    const char* records=*reinterpret_cast<char* const*>(static_cast<char*>(skeleton)+0x10);
    const std::int64_t count=*reinterpret_cast<const std::int64_t*>(static_cast<char*>(skeleton)+0x20);
    if(index>=count)return false;
    const char* rec=records+static_cast<std::size_t>(index)*0x110;
    if(!records || !Rd(rec+0xB0,64,"bone record"))return false;
    std::memcpy(out,rec+0xB0,64);
    return std::isfinite(out[12]+out[13]+out[14]);
}

bool SetTeam(void* object,std::int32_t team,bool reg) noexcept {
    if(!Prims() || !object)return false;
    return At<bool(__fastcall*)(void*,std::int32_t,bool)>(kSetTeam)(object,team,reg);
}
void SetLevel(void* object,float rawLevel) noexcept {
    if(Prims() && object)At<void(__fastcall*)(void*,float)>(kSetLevel)(object,rawLevel);
}
float LevelS(const void* object) noexcept {
    const char* o=static_cast<const char*>(object);
    if(!object || !Rd(o+kLevelAt,4,"object level"))return 1.0f;
    float l;
    std::memcpy(&l,o+kLevelAt,4);
    return std::isfinite(l) && l>0.0f ? l/10.0f : 1.0f;
}
float Difficulty(float x) noexcept {
    void* gs=Global(kGameStatus);
    if(!Prims() || !gs)return x;
    return At<float(__fastcall*)(void*,float)>(kDifficulty)(gs,x);
}
void SizeDurability(void* object) noexcept {
    char* o=static_cast<char*>(object);
    if(!object || !Rd(o+kBaseDurabilityAt,16,"object durability fields"))return;
    float base;
    std::memcpy(&base,o+kBaseDurabilityAt,4);
    const float max=Difficulty(base*LevelS(object));
    if(!(max>0.0f) || !std::isfinite(max))return;
    std::memcpy(o+kMaxDurabilityAt,&max,4);
    std::memcpy(o+kDurabilityAt,&max,4);
}

void* Target(void* object) noexcept {
    if(!Prims() || !object)return nullptr;
    return At<void*(__fastcall*)(void*)>(kGetTarget)(object);
}
bool AimPoint(const void* target,float* out) noexcept {
    const char* t=static_cast<const char*>(target);
    if(!target || !Rd(t+0x360,0x20,"target lock points") || !Rd(t+kPosAt,12,"target position"))return false;
    const std::size_t count=*reinterpret_cast<const std::size_t*>(t+0x378);
    const char* first=count ? *reinterpret_cast<char* const*>(t+0x368) : nullptr;
    const char* point=first && Readable(first,8) ? *reinterpret_cast<char* const*>(first) : nullptr;
    if(point && Rd(point+0x10,12,"lock point"))std::memcpy(out,point+0x10,12);
    else std::memcpy(out,t+kPosAt,12);
    return std::isfinite(out[0]+out[1]+out[2]);
}

int HitBody(void* ragdoll,const void* damageInfo) noexcept {
    const char* g=static_cast<const char*>(damageInfo);
    if(!Prims() || !ragdoll || !g || !Rd(g,0x90,"damage info"))return -1;
    const char* recs=*reinterpret_cast<char* const*>(g+0x78);
    const std::size_t n=*reinterpret_cast<const std::size_t*>(g+0x88);
    if(!recs || n==0 || n>256 || !Rd(recs,n*0x30,"damage sub-hits"))return -1;
    const float* c=reinterpret_cast<const float*>(g+kGdiCentre);
    std::uint32_t body=0;
    float best=3.4e38f;
    for(std::size_t i=0;i<n;++i) {
        const float* p=reinterpret_cast<const float*>(recs+i*0x30);
        const float d=(p[0]-c[0])*(p[0]-c[0])+(p[1]-c[1])*(p[1]-c[1])+(p[2]-c[2])*(p[2]-c[2]);
        if(d<best) { best=d; body=*reinterpret_cast<const std::uint32_t*>(recs+i*0x30+0x20); }
    }
    return At<int(__fastcall*)(void*,std::uint32_t)>(kBodyById)(ragdoll,body);
}
int RagdollBody(void* ragdoll,const wchar_t* name) noexcept {
    if(!Prims() || !ragdoll || !Rd(ragdoll,0x90,"ragdoll"))return -1;
    return At<int(__fastcall*)(void*,const wchar_t*)>(kBodyByName)(ragdoll,name);
}

bool PlayCue(const wchar_t* cue,const float* pos,SoundHandle* handle,const SeParams& params) noexcept {
    void* system=Global(kSoundSystem);
    if(!Prims() || !system || !cue || !*cue || ShuttingDown())return false;
    alignas(16) unsigned char preset[0x80];
    At<void*(__fastcall*)(void*)>(kPresetInit)(preset);
    struct { void* bank; std::int32_t cue; std::int32_t pad; } ref{nullptr,-1,0};
    if(!At<bool(__fastcall*)(void*,void*,int,const wchar_t*)>(kCueLookup)(system,&ref,0,cue) || !ref.bank) {
        static std::atomic<int> said{0};
        if(said.fetch_add(1)<16)Log("EDF41CLASS no sound cue %ls in the loaded banks",cue);
        return false;
    }
    for(std::size_t at=0x20;at<=0x60;at+=0x10)std::memcpy(preset+at,&ref,16);
    std::memcpy(preset+0x74,&params.volume,4);
    std::memcpy(preset+0x70,&params.pitch,4);
    std::memcpy(preset+0x10,&params.inner,4);
    std::memcpy(preset+0x14,&params.reference,4);
    std::memcpy(preset+0x18,&params.cull,4);
    alignas(16) float p[4]={pos[0],pos[1],pos[2],1.0f};
    At<void(__fastcall*)(void*,const float*,void*)>(kPresetPlay)(preset,p,handle);
    return true;
}
bool PlaySe(const void* table,const wchar_t* name,const float* pos,SoundHandle* handle) noexcept {
    const SgoNode* list=Key(table,L"edf41_se");
    const int n=Count(list);
    wchar_t text[96],cue[96];
    for(int i=0;i<n;++i) {
        const SgoNode* e=Child(list,i);
        if(Count(e)<7 || !Text(Child(e,0),text,96) || std::wcscmp(text,name)!=0)continue;
        SeParams p;
        if(!Text(Child(e,1),cue,96) || !Float(Child(e,2),&p.volume) || !Float(Child(e,3),&p.pitch) || !Float(Child(e,4),&p.inner)
           || !Float(Child(e,5),&p.reference) || !Float(Child(e,6),&p.cull))break;
        return PlayCue(cue,pos,handle,p);
    }
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<16)Log("EDF41CLASS no 4.1 sound preset %ls in the object's edf41_se",name);
    return false;
}
void MoveSound(SoundHandle& handle,const float* pos) noexcept {
    if(!Prims() || !handle.instance || ShuttingDown())return;
    alignas(16) float p[4]={pos[0],pos[1],pos[2],1.0f};
    At<void(__fastcall*)(SoundHandle*,const float*)>(kSoundMove)(&handle,p);
}
void StopSound(SoundHandle& handle,int fadeFrames) noexcept {
    // 0x7A8CD0 reads the instance (sound system memory) and releases the handle: never once the application destructor has
    // started (the sound system may be gone; the shutdown callbacks stopped every handle before)
    if(!Prims() || !handle.instance || ShuttingDown())return;
    At<void(__fastcall*)(SoundHandle*,int)>(kSoundStop)(&handle,fadeFrames);
}

void FireBurst(const float* pos,const float* dir,float size) noexcept {
    void* egu=Global(kEffects);
    if(!Prims() || !egu)return;
    alignas(16) float p[4]={pos[0],pos[1],pos[2],1.0f},d[4]={dir[0],dir[1],dir[2],1.0f};
    At<void(__fastcall*)(void*,const float*,const float*,float)>(kFlame)(egu,p,d,size);
}
void Blazing(const float* pos,const float* dir,float size) noexcept {
    void* egu=Global(kEffects);
    if(!Prims() || !egu)return;
    alignas(16) float p[4]={pos[0],pos[1],pos[2],1.0f},d[4]={dir[0],dir[1],dir[2],1.0f};
    At<void(__fastcall*)(void*,const float*,const float*,float,bool,int)>(kBlazing)(egu,p,d,size,false,0);
}
void Explosion(const float* pos,const float* dir,float size) noexcept {
    void* egu=Global(kEffects);
    if(!Prims() || !egu)return;
    alignas(16) float p[4]={pos[0],pos[1],pos[2],1.0f},d[4]={dir[0],dir[1],dir[2],1.0f};
    alignas(16) const float white[4]={1.0f,1.0f,1.0f,1.0f};
    At<void(__fastcall*)(void*,const float*,const float*,float,float,const float*,float)>(kExplosion)(egu,p,d,size,1.0f,white,1.0f);
}
void BigExplosion(const float* pos,const float* dir,float size,float k) noexcept {
    void* egu=Global(kEffects);
    if(!Prims() || !egu)return;
    alignas(16) float p[4]={pos[0],pos[1],pos[2],1.0f},d[4]={dir[0],dir[1],dir[2],1.0f};
    At<void(__fastcall*)(void*,const float*,const float*,float,float,bool)>(kExplosionBig)(egu,p,d,size,k,false);
}

void DropItems(void* object) noexcept {
    char* o=static_cast<char*>(object);
    if(!Prims() || !object || !Rd(o+0x590,1,"object items dropped") || o[0x590])return;
    alignas(16) float p[4]={0,0,0,1};
    std::memcpy(p,o+kPosAt,12);
    At<void(__fastcall*)(void*,const float*)>(kDropItems)(object,p);
}

void Activate(void* object) noexcept {
    if(Prims() && object)At<void(__fastcall*)(void*)>(kActivate)(object);
}
bool AnimDone(const void* object,float* length) noexcept {
    const char* o=static_cast<const char*>(object);
    if(!object || !Rd(o+kCasAt,0x68,"CAS players") || !Rd(o+kCasPlayerAt,4,"CAS player index"))return false;
    const int index=*reinterpret_cast<const std::int32_t*>(o+kCasPlayerAt);
    const char* players=*reinterpret_cast<char* const*>(o+kCasAt+0x60);
    if(index<0 || !players)return false;
    const char* p=players+static_cast<std::size_t>(index)*0x160;
    if(!Rd(p+0xE8,0x10,"CAS player"))return false;
    float t,len;
    std::memcpy(&t,p+0xE8,4);
    std::memcpy(&len,p+0xF0,4);
    if(length)*length=len;
    return t+0.5f>=len;
}

void AllowAnyTeam(Clone& c) noexcept {
    constexpr std::size_t kAnyTeam=0x54ABD0;   // mov al,1; ret
    static const unsigned char want[3]={0xB0,0x01,0xC3};
    if(Readable(image+kAnyTeam,3) && std::memcmp(image+kAnyTeam,want,3)==0)SetSlot(c,22,image+kAnyTeam);
    else Log("EDF41CLASS GameObjectBase's slot 22 is not what this was written for: %s keeps its team rule",c.tag);
}

void SetSlot(Clone& c,int slot,void* fn) noexcept {
    if(slot>=0 && slot<c.slots && fn)c.block[1+slot]=fn;
}

// ---- CAS player, the world, lock points, more effects, area damage (UfoRobo) -------------------------------------------------
namespace {
constexpr std::size_t kStateByName=0x11664F0,kPlayStateIndex=0x116BFC0;
constexpr std::size_t kCasPlayers=0x60,kCasPlayerSize=0x160,kCasPlayerTime=0xE8,kCasPlayerLength=0xF0,kCasPlayerState=0x104;
constexpr std::size_t kCastRay=0x11A7EE0,kHitReset=0xFDF00,kGroundCollector=0x179CBE8,kGroundAdd=0x208850;
constexpr std::uint32_t kMapLayer=0x16;
constexpr std::size_t kCameraSlots=0x4D8,kCameraSlotSize=0x188;
constexpr std::size_t kAddLockTarget=0x547A50,kWind=0x2E4D90,kQuake=0x2E9B30,kEffectGen=0x20B2980;
constexpr Print kRoboPrints[]={
    {kStateByName,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x7C}},{kPlayStateIndex,{0x40,0x53,0x41,0x54,0x41,0x56,0x41,0x57}},
    {kCastRay,{0x40,0x53,0x56,0x57,0x48,0x81,0xEC,0xA0}},{kHitReset,{0x33,0xD2,0xB8,0xFF,0xFF,0x00,0x00,0x89}},
    {kGroundAdd,{0xF6,0x42,0x6C,0x02,0x0F,0x85,0x26,0xB1}},{kAddLockTarget,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C}},
    {kWind,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x10,0x48}},{kQuake,{0xF3,0x0F,0x10,0x05,0xD0,0xEC,0x47,0x01}},
};
std::atomic<int> roboPrimState{0};
bool RoboPrims() noexcept {
    int s=roboPrimState.load();
    if(s)return s==1;
    for(const Print& p:kRoboPrints)
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41CLASS the code at EDF.dll+0x%zX is not what this was written for: CAS clocks, the ground ray and lock points are off",p.rva);
            roboPrimState=2;
            return false;
        }
    roboPrimState=1;
    return true;
}
const char* MainPlayer(const void* object) noexcept {
    const char* o=static_cast<const char*>(object);
    if(!object || !Rd(o+kCasPlayerAt,4,"CAS player index") || !Rd(o+kCasAt+kCasPlayers,8,"CAS player array"))return nullptr;
    const int index=*reinterpret_cast<const std::int32_t*>(o+kCasPlayerAt);
    const char* players=*reinterpret_cast<const char* const*>(o+kCasAt+kCasPlayers);
    if(index<0 || index>64 || !players)return nullptr;
    const char* p=players+static_cast<std::size_t>(index)*kCasPlayerSize;
    return Rd(p,kCasPlayerSize,"CAS player") ? p : nullptr;
}
const SgoNode kGrenadeCustom[7]={{0,6,12},{1,0,1},{2,0,0},{2,0,0},{2,0,0},{2,0,0},{1,0,0}};   // [1, 0, 0, 0, 0, 0]
}  // namespace

int StateIndex(void* object,const wchar_t* state) noexcept {
    char* o=static_cast<char*>(object);
    if(!RoboPrims() || !MainPlayer(object))return -1;
    const int casPlayer=*reinterpret_cast<const std::int32_t*>(o+kCasPlayerAt);
    return reinterpret_cast<int(__fastcall*)(void*,int,const wchar_t*,bool)>(image+kStateByName)(o+kCasAt,casPlayer,state,true);
}
bool PlayStateIndex(void* object,int state) noexcept {
    char* o=static_cast<char*>(object);
    if(state<0 || !RoboPrims() || !MainPlayer(object))return false;
    const int casPlayer=*reinterpret_cast<const std::int32_t*>(o+kCasPlayerAt);
    return reinterpret_cast<bool(__fastcall*)(void*,int,int,int)>(image+kPlayStateIndex)(o+kCasAt,casPlayer,state,0);
}
bool ClipNow(const void* object,int* state,float* time,float* length) noexcept {
    const char* p=MainPlayer(object);
    if(!p)return false;
    std::memcpy(time,p+kCasPlayerTime,4);
    std::memcpy(length,p+kCasPlayerLength,4);
    std::memcpy(state,p+kCasPlayerState,4);
    return std::isfinite(*time) && std::isfinite(*length);
}

bool ClipRewind(void* object) noexcept {
    char* p=const_cast<char*>(MainPlayer(object));
    if(!p)return false;
    const float zero=0.0f;
    std::memcpy(p+kCasPlayerTime,&zero,4);
    return true;
}

bool GroundHit(const float* from,const float* to,float* hit,void* skipRagdoll) noexcept {
    if(!RoboPrims() || (skipRagdoll && !Prims()))return false;
    char* g=static_cast<char*>(Manager());
    if(!g || !Rd(g,0x70,"physics world") || !*reinterpret_cast<void* const*>(g+0x68))return false;
    struct alignas(16) RayInput { float from[4],to[4]; std::uint32_t filter,unk24; std::uint64_t pad; };
    struct alignas(16) RayHits { unsigned char raw[0xA0]; };
    float a[3]={from[0],from[1],from[2]};
    // The ground collector keeps the nearest hit with flags & 2, the caller's own ragdoll bodies included (the UfoRobo M411 run,
    // 2026-10-15: each frame's ray met the robot's feet and lifted it a little more): a hit on one of `skipRagdoll`'s bodies
    // (collector +0x78 the body id, heli.cpp ObjectRay; 0x6EA430 maps it) is passed and the ray cast again from just below it.
    for(int pass=0;pass<8;++pass) {
        const RayInput in{{a[0],a[1],a[2],1.0f},{to[0],to[1],to[2],1.0f},kMapLayer,0,0};
        RayHits col{};
        *reinterpret_cast<const void**>(col.raw)=image+kGroundCollector;
        reinterpret_cast<void(*)(void*)>(image+kHitReset)(&col);
        reinterpret_cast<void(*)(void*,void*,const RayInput*)>(image+kCastRay)(g+0x10,&col,&in);
        if(*reinterpret_cast<const std::int32_t*>(col.raw+0x0C)==0)return false;
        const float f=*reinterpret_cast<const float*>(col.raw+0x50);
        if(!std::isfinite(f) || f<0.0f || f>1.0f)return false;
        std::memcpy(hit,col.raw+0x30,12);
        if(!std::isfinite(hit[0]+hit[1]+hit[2]))return false;
        // the body id as 0x6EA430 compares it (the body's +0xF0, the whole dword; heli.cpp masks it to 24 bits for its own use)
        const std::uint32_t body=*reinterpret_cast<const std::uint32_t*>(col.raw+0x78);
        const auto byId=reinterpret_cast<int(__fastcall*)(void*,std::uint32_t)>(image+kBodyById);
        int own=skipRagdoll ? byId(skipRagdoll,body) : -1;
        if(own<0 && skipRagdoll)own=byId(skipRagdoll,body&0xFFFFFFu);
        static std::atomic<int> said{0};
        if(skipRagdoll && said.fetch_add(1)<12)
            Log("EDF41CLASS ground ray pass %d from %.1f %.1f %.1f to y %.1f: hit %.1f %.1f %.1f body %06X flags %08X -> own body %d",pass,a[0],a[1],a[2],
                to[1],hit[0],hit[1],hit[2],body,*reinterpret_cast<const std::uint32_t*>(col.raw+0x9C),own);
        if(own<0)return true;
        const float d[3]={to[0]-a[0],to[1]-a[1],to[2]-a[2]};
        const float l=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(l<1e-3f)return false;
        for(int k=0;k<3;++k)a[k]=hit[k]+d[k]/l*0.25f;
    }
    return false;
}

bool CameraNow(float* eye,float* forward) noexcept {
    const char* scene=static_cast<const char*>(Manager());
    if(!scene)return false;
    for(int i=0;i<4;++i) {
        const char* slot=scene+kCameraSlots+static_cast<std::size_t>(i)*kCameraSlotSize;
        if(!Rd(slot,16,"camera slot"))return false;
        const char* obj=*reinterpret_cast<const char* const*>(slot);
        const char* ctrl=*reinterpret_cast<const char* const*>(slot+8);
        if(!obj || !ctrl || !Readable(ctrl,16) || *reinterpret_cast<const std::int32_t*>(ctrl+8)<=0)continue;
        if(!Readable(obj+0x70,0x40))continue;
        float m[16];
        std::memcpy(m,obj+0x70,sizeof(m));
        const float lf=std::sqrt(m[8]*m[8]+m[9]*m[9]+m[10]*m[10]);
        if(!std::isfinite(lf+m[12]+m[13]+m[14]) || lf<0.9f || lf>1.1f)continue;
        if(m[12]==0.0f && m[13]==0.0f && m[14]==0.0f && m[0]==1.0f && m[5]==1.0f && m[10]==1.0f)continue;   // not a view yet
        for(int k=0;k<3;++k) { eye[k]=m[12+k]; forward[k]=m[8+k]/lf; }
        return true;
    }
    return false;
}

void* AddLockPoint(void* object) noexcept {
    if(!object || !RoboPrims() || !Rd(static_cast<char*>(object)+0x360,0x20,"lock point list"))return nullptr;
    void* p=reinterpret_cast<void*(__fastcall*)(void*)>(image+kAddLockTarget)(object);
    return p && Readable(p,0x50) ? p : nullptr;
}
void SetLockPoint(void* lockPoint,const float* pos) noexcept {
    if(!lockPoint || !Readable(static_cast<char*>(lockPoint)+0x10,16))return;
    const float p[4]={pos[0],pos[1],pos[2],1.0f};
    std::memcpy(static_cast<char*>(lockPoint)+0x10,p,16);
}

void Smoke(const float* pos,float size) noexcept {
    if(!RoboPrims() || !Readable(image+kEffectGen,8))return;
    void* egu=*reinterpret_cast<void* const*>(image+kEffectGen);
    if(!egu)return;
    alignas(16) const float p[4]={pos[0],pos[1],pos[2],1.0f},d[4]={0.0f,1.0f,0.0f,1.0f};
    reinterpret_cast<void(__fastcall*)(void*,const float*,const float*,float)>(image+kWind)(egu,p,d,size);
}
void Quake(const float* pos,float strength,float radius) noexcept {
    if(!RoboPrims() || !Readable(image+kEffectGen,8))return;
    void* egu=*reinterpret_cast<void* const*>(image+kEffectGen);
    if(!egu)return;
    alignas(16) const float p[4]={pos[0],pos[1],pos[2],1.0f};
    reinterpret_cast<void(__fastcall*)(void*,const float*,float,float,int)>(image+kQuake)(egu,p,strength,radius,1);
}

bool Blast(void* owner,const float* pos,float radius,float damage) noexcept {
    Bullet b;
    b.factory=L"GrenadeBullet01";
    b.speed=0.001f;   // still; not 0, so its heading stays a direction
    b.life=1;
    b.damage=damage>0.0f ? damage : 0.0f;
    b.explosion=radius;
    b.size=1.0f;
    b.custom=kGrenadeCustom;
    const float dir[3]={0.0f,-1.0f,0.0f};
    float m[16];
    AimMatrix(pos,dir,m);
    return Fire(owner,m,b);
}
// ---- more EDF6 primitives (the 401 mothership; PRIMITIVES.md 1c, 7 and docs/view-distance-re.md) --------------------------
namespace {
constexpr std::size_t kSkeletonFromMatrix=0x1100B90,kSetFarRender=0x11B3020;
constexpr Print kMorePrints[]={
    {kSkeletonFromMatrix,{0xC6,0x81,0xB0,0x00,0x00,0x00,0x01,0x0F}},{kSetFarRender,{0x44,0x8B,0x41,0x20,0x41,0x8B,0xC0,0x0F}},
};
std::atomic<int> moreState{0};
bool More() noexcept {
    int s=moreState.load();
    if(s)return s==1;
    for(const Print& p:kMorePrints)
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41CLASS the code at EDF.dll+0x%zX is not what this was written for: model matrices, far rendering and activation are off",p.rva);
            moreState=2;
            return false;
        }
    moreState=1;
    return Prims();
}
bool NotDeleted(const void* object) noexcept {
    const char* o=static_cast<const char*>(object);
    return object && Readable(o+kFlagsAt,1) && !(o[kFlagsAt]&4);
}
}  // namespace

bool ModelMatrix(void* object,const float* matrix) noexcept {
    if(!More() || !NotDeleted(object))return false;
    alignas(16) float m[16];
    std::memcpy(m,matrix,sizeof(m));
    At<void(__fastcall*)(void*,const float*)>(kSkeletonFromMatrix)(static_cast<char*>(object)+kEaoSkeleton,m);
    return true;
}
bool FarRender(void* object) noexcept {
    constexpr std::uint32_t kFarBit=0x04000000u;
    if(!More() || !NotDeleted(object))return false;
    char* node=static_cast<char*>(object)+kEaoModel;
    if(!Rd(node,0x28,"model node"))return false;
    if(*reinterpret_cast<const std::uint32_t*>(node+0x20)&kFarBit)return true;
    if(!*reinterpret_cast<void* const*>(node+0x10))return false;   // its Umbra object is not there yet
    At<void(__fastcall*)(void*,bool)>(kSetFarRender)(node,true);
    return (*reinterpret_cast<const std::uint32_t*>(node+0x20)&kFarBit)!=0;
}

void WeakSet(WeakRef& w,const void* object) noexcept {
    WeakReset(w);
    const char* o=static_cast<const char*>(object);
    if(!object || !Rd(o+kWeakSelfAt,16,"weak self"))return;
    void* obj=*reinterpret_cast<void* const*>(o+kWeakSelfAt);
    void* ctrl=*reinterpret_cast<void* const*>(o+kWeakSelfAt+8);
    if(!ctrl || !Rd(ctrl,16,"weak control block"))return;
    reinterpret_cast<std::atomic<std::int32_t>*>(static_cast<char*>(ctrl)+0xC)->fetch_add(1);
    w.object=obj;
    w.control=ctrl;
}
void WeakReset(WeakRef& w) noexcept {
    if(w.control && Readable(w.control,16)) {
        auto* weak=reinterpret_cast<std::atomic<std::int32_t>*>(static_cast<char*>(w.control)+0xC);
        if(weak->fetch_sub(1)==1) {   // the last reference of any kind: the block deletes itself (vtable slot 1)
            void* const* vt=*reinterpret_cast<void* const* const*>(w.control);
            if(vt && Readable(vt,16))reinterpret_cast<void(__fastcall*)(void*)>(vt[1])(w.control);
        }
    }
    w=WeakRef{};
}
bool WeakAlive(const WeakRef& w) noexcept {
    if(!w.control || !Readable(w.control,16) || *reinterpret_cast<const std::int32_t*>(static_cast<char*>(w.control)+8)<=0)return false;
    return NotDeleted(w.object);
}
bool WeakOut(const void* object,void* out) noexcept {
    WeakRef w;
    WeakSet(w,object);
    if(!w.control || !Rd(out,16,"weak out")) { WeakReset(w); return false; }
    std::memcpy(out,&w.object,8);
    std::memcpy(static_cast<char*>(out)+8,&w.control,8);
    return true;   // the weak count WeakSet took goes with it
}

// ---- bullet hit sounds: FireSe (a 4.1 preset name in a data table: UfoRobo, UfoMother301), FireHit (a raw cue: the 401), FillSePreset ----
namespace {
struct CueRef { void* bank; std::int32_t cue; std::int32_t pad; };
// The cue and PlayCue's numbers into an initialised SePreset (the constructor's 0x7B3200 defaults stay).
void FillCue(unsigned char* preset,const CueRef& ref,const SeParams& p) noexcept {
    for(std::size_t at=0x20;at<=0x60;at+=0x10)std::memcpy(preset+at,&ref,16);
    std::memcpy(preset+0x74,&p.volume,4);
    std::memcpy(preset+0x70,&p.pitch,4);
    std::memcpy(preset+0x10,&p.inner,4);
    std::memcpy(preset+0x14,&p.reference,4);
    std::memcpy(preset+0x18,&p.cull,4);
}
// A raw cue by name (Fire's preset constructor ran already): not in a loaded bank / no sound system: the preset stays silent.
void FillRawCue(unsigned char* preset,const wchar_t* cue,const SeParams& p) noexcept {
    void* system=Global(kSoundSystem);
    CueRef ref{nullptr,-1,0};
    if(cue && *cue && Prims() && system && At<bool(__fastcall*)(void*,void*,int,const wchar_t*)>(kCueLookup)(system,&ref,0,cue) && ref.bank)
        FillCue(preset,ref,p);
}
// The SePreset `preset` (initialised) filled from 4.1's preset `name` in table's edf41_se: [name, cue, volume, pitch, three distances].
bool FillPreset(unsigned char* preset,const void* table,const wchar_t* name) noexcept {
    void* system=Global(kSoundSystem);
    if(!Prims() || !system || !table || !name || !*name)return false;
    const SgoNode* list=Key(table,L"edf41_se");
    wchar_t text[96],cue[96];
    for(int i=0;i<Count(list);++i) {
        const SgoNode* e=Child(list,i);
        if(Count(e)<7 || !Text(Child(e,0),text,96) || std::wcscmp(text,name)!=0)continue;
        SeParams p;
        if(!Text(Child(e,1),cue,96) || !Float(Child(e,2),&p.volume) || !Float(Child(e,3),&p.pitch) || !Float(Child(e,4),&p.inner)
           || !Float(Child(e,5),&p.reference) || !Float(Child(e,6),&p.cull))return false;
        CueRef ref{nullptr,-1,0};
        if(!At<bool(__fastcall*)(void*,void*,int,const wchar_t*)>(kCueLookup)(system,&ref,0,cue) || !ref.bank)return false;
        FillCue(preset,ref,p);
        return true;
    }
    return false;
}
}  // namespace

bool FillSePreset(unsigned char* preset,const void* table,const wchar_t* name) noexcept {
    if(!Prims() || !preset)return false;
    At<void*(__fastcall*)(void*)>(kPresetInit)(preset);
    return FillPreset(preset,table,name);
}


// ---- material animations (the 401, src/edf41mother401.cpp) ------------------------------------------------
namespace {
constexpr std::size_t kMatCtor=0x6DBE90,kMatBind=0x6DC7B0,kMatAdd=0x6DC050,kMatPlay=0x6DC830,kMatUpdate=0x6DC8B0,kMatFree=0x29D120;
constexpr std::size_t kEaoMaterials=0x720;   // the model's material set (model +0x5C0, +0x160)
constexpr Print k401Prints[]={
    {kMatCtor,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C}},{kMatBind,{0x48,0x89,0x11,0xC3,0xCC,0xCC,0xCC,0xCC}},
    {kMatAdd,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C}},{kMatPlay,{0x4C,0x8B,0xC9,0x0F,0x57,0xC9,0x8B,0xCA}},
    {kMatUpdate,{0x48,0x8B,0xC4,0x48,0x89,0x68,0x18,0x48}},{kMatFree,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C}},
};
std::atomic<int> m401PrimState{0};
bool M401Prims() noexcept {
    int s=m401PrimState.load();
    if(s)return s==1;
    for(const Print& p:k401Prints)
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41CLASS the code at EDF.dll+0x%zX is not what this was written for: material animations are off",p.rva);
            m401PrimState=2;
            return false;
        }
    m401PrimState=1;
    return true;
}
}  // namespace

bool FireSe(void* owner,const float* matrix,const Bullet& b,const void* seTable,const wchar_t* hitSe) noexcept {
    Bullet c=b;
    c.hitSeTable=seTable;
    c.hitSe=hitSe;
    return Fire(owner,matrix,c);
}

bool FireHit(void* owner,const float* matrix,const Bullet& b,const wchar_t* hitCue,const SeParams& hit) noexcept {
    Bullet c=b;
    c.hitCue=hitCue;
    c.hitCueParams=&hit;
    return Fire(owner,matrix,c);
}

int MaterialAnimsLoad(MaterialAnims& m,void* object,const SgoNode* list,const int* kinds,int kindCount) noexcept {
    const int n=Count(list);
    if(m.live || n<=0 || !object || !M401Prims() || !Rd(static_cast<char*>(object)+kEaoMaterials,8,"model material set"))return 0;
    void* comp=m.comp;
    reinterpret_cast<void*(__fastcall*)(void*)>(image+kMatCtor)(comp);
    reinterpret_cast<void(__fastcall*)(void*,void*)>(image+kMatBind)(comp,static_cast<char*>(object)+kEaoMaterials);
    m.live=true;
    for(int i=0;i<n && i<16;++i) {
        const SgoNode* e=Child(list,i);
        const SgoNode* blob=e;
        std::int32_t kind=i<kindCount && kinds ? kinds[i] : 0;
        if(e && e->type==0) { blob=Child(e,0); Int(Child(e,1),&kind); }   // EDF6's [blob, kind]
        if(!blob || blob->type!=4 || blob->a<0x14 || !Rd(blob,12,"material animation"))break;
        const char* data=reinterpret_cast<const char*>(blob)+blob->b;
        if(!Rd(data,static_cast<std::size_t>(blob->a),"material animation blob") || std::memcmp(data+0x10,"MTAB",4)!=0)break;
        reinterpret_cast<int(__fastcall*)(void*,const void*,int)>(image+kMatAdd)(comp,data,kind);
        ++m.count;
    }
    return m.count;
}
void MaterialAnimsPlay(MaterialAnims& m,int index,int mode) noexcept {
    if(m.live && index>=0 && index<m.count)reinterpret_cast<void(__fastcall*)(void*,unsigned,int)>(image+kMatPlay)(m.comp,static_cast<unsigned>(index),mode);
}
void MaterialAnimsUpdate(MaterialAnims& m,void* frameInfo) noexcept {
    if(m.live && m.count>0 && frameInfo && Readable(frameInfo,8))
        reinterpret_cast<void(__fastcall*)(void*,void*,float)>(image+kMatUpdate)(m.comp,frameInfo,1.0f);
}
void MaterialAnimsFree(MaterialAnims& m) noexcept {
    if(!m.live)return;
    m.live=false;
    m.count=0;
    // 0x29D120 gives back the entries' references into the game's material data: not once the application destructor has
    // started (the shutdown callback freed every live one while the game was whole; a later destructor leaves it)
    if(ShuttingDown())return;
    reinterpret_cast<void(__fastcall*)(void*)>(image+kMatFree)(m.comp+8);
}

void SmallBurst(const float* pos,const float* dir,float size) noexcept {
    static const unsigned char print[8]={0x48,0x8B,0xC4,0x48,0x89,0x58,0x08,0x48};
    void* egu=Global(kEffects);
    if(!Prims() || !egu || !Readable(image+0x2E7D00,8) || std::memcmp(image+0x2E7D00,print,8)!=0)return;
    alignas(16) float p[4]={pos[0],pos[1],pos[2],1.0f},d[4]={dir[0],dir[1],dir[2],1.0f};
    At<void(__fastcall*)(void*,const float*,const float*,float)>(0x2E7D00)(egu,p,d,size);
}

void StencilLight(const float* pos,const float* colour,float radius,int frames) noexcept {
    static const Print prints[]={{0x84880,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C}},{0x2EBC00,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x7C}}};
    static std::atomic<int> state{0};
    if(!state.load()) {
        bool ok=true;
        for(const Print& p:prints)ok=ok && Readable(image+p.rva,8) && std::memcmp(image+p.rva,p.bytes,8)==0;
        // the helper 0x2EBC00 is checked as the witness of the InitParam vtable it loads (lea rax, [EDF+0x1762108] at 0x2EBC3D)
        ok=ok && Readable(image+0x2EBC3D,7) && *reinterpret_cast<const std::int32_t*>(image+0x2EBC40)==static_cast<std::int32_t>(0x1762108-0x2EBC44);
        state=ok ? 1 : 2;
        if(!ok)Log("EDF41CLASS the StencilLight code is not what this was written for: no light flashes");
    }
    void* mgr=Manager();
    if(state.load()!=1 || !mgr || !pos || !colour)return;
    alignas(16) unsigned char param[0x70]={};
    void* vt=image+0x1762108;
    std::memcpy(param,&vt,8);
    const float p[4]={pos[0],pos[1],pos[2],1.0f};
    std::memcpy(param+0x30,p,16);
    std::memcpy(param+0x40,colour,16);
    std::memcpy(param+0x50,colour,16);
    std::memcpy(param+0x60,&radius,4);
    std::memcpy(param+0x64,&frames,4);
    alignas(16) static const float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    alignas(16) unsigned char out[16]={};
    reinterpret_cast<void(__fastcall*)(void*,void*,const float*,void*)>(image+0x84880)(mgr,out,identity,param);
    WeakRef w;   // the creator hands back a weak pointer: let it go
    std::memcpy(&w.object,out,8);
    std::memcpy(&w.control,out+8,8);
    WeakReset(w);
}

// ---- shutdown callbacks (the 401's charge glows; the 301's emitters) ----------------------------------------------------------
namespace {
constexpr std::size_t kAppDtor=0x7021F0,kAppDtorCall=0x70293F;
void(*shutdownFns[8])() noexcept={};
int shutdownCount=0;
void __fastcall ShutdownHook(void* self) {
    for(int i=0;i<shutdownCount;++i)shutdownFns[i]();   // the game is still whole: the callbacks release what needs it
    shuttingDown=true;   // from here an object destroyed (inside the application destructor) calls nothing into its subsystems
    reinterpret_cast<void(__fastcall*)(void*)>(image+kAppDtor)(self);
}
}  // namespace

bool ShuttingDown() noexcept { return shuttingDown.load(); }

void AtGameShutdown(void(*fn)() noexcept) noexcept {
    static std::mutex lock;
    std::lock_guard<std::mutex> hold(lock);
    if(!fn || shutdownCount>=8)return;
    if(shutdownCount==0) {
        static const unsigned char print[8]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74};
        if(!Readable(image+kAppDtor,8) || std::memcmp(image+kAppDtor,print,8)!=0 || !Readable(image+kAppDtorCall,5) || image[kAppDtorCall]!=0xE8) {
            Log("EDF41CLASS the application destructor is not what this was written for: no shutdown callbacks");
            return;
        }
        bool changed=false;
        if(!RedirectCall(image+kAppDtorCall,image+kAppDtor,reinterpret_cast<void*>(&ShutdownHook),changed)) { Log("EDF41CLASS the shutdown hook failed"); return; }
    }
    shutdownFns[shutdownCount++]=fn;
    static std::atomic<int> said{0};
    if(said.fetch_add(1)<3)Log("EDF41CLASS shutdown callback %d registered (the application destructor's call site is redirected)",shutdownCount);
}

}  // namespace crew::edf41c
