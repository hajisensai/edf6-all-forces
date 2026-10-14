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
    {0x4F8A70,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // UfoCarrier508 slot 0
    {0x4F9540,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // slot 5
    {0x4F93E0,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74}},   // slot 9
    {kCreateFromPath,{0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41}},
    {kSgoKey,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},
    {kDeleteObject,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kSetCasState,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kEaoCtor,{0x48,0x89,0x5C,0x24,0x18,0x55,0x56,0x57}},
    {0x5B6BB0,{0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74}},   // EventAnimationObject slot 0
    {0x5B6D20,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},   // slot 5
    {0x5B6540,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57}},   // slot 9
};

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
    return reinterpret_cast<CreateFromPathFn>(image+kCreateFromPath)(manager,matrix,sgoPath,context);
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
}  // namespace crew::edf41c
