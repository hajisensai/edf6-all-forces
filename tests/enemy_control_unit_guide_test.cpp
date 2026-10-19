// Executes final Unit guide x64, native weak ownership and persistent failure paths.
#define ENEMY_CONTROL_UNIT_GUIDE_TEST
#include "../src/enemy_control_unit_guide.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>
namespace crew {unsigned char* image=nullptr;}
namespace {
using namespace crew;using namespace crew::unit_guide;using namespace crew::enemy_native;
constexpr NativeProfile profiles[]={
#include "../src/enemy_control_native_profiles.inc"
};
int checks=0,failed=0;unsigned lockQueries=0,moveCalls=0,deleteCalls=0;
bool active=true,contextKnown=true,copyReply=true,noFocus=false,copyFault=false,reenter=false,sawReady=false,restoreInside=true;
bool moveBefore=false,moveConsumed=false,movePublished=false,deleteFault=false;
unsigned char body[0x2000]{},unit[0x700]{},oldObject[0x50]{},chosenObject[0x50]{};
struct Control {void** table=nullptr;LONG uses=1,weaks=1;const void* object=nullptr;};
void* controlTable[2]{};
Control rootControl,chosenControl;Control* oldControl=nullptr;
const NativeProfile* currentProfile=nullptr;const void* actualMoveDestination=nullptr;
void Check(bool ok,const char* what,int line){++checks;if(!ok){++failed;std::printf("FAIL %d %s\n",line,what);}}
#define CHECK(x) Check((x),#x,__LINE__)
void __fastcall DeleteWeak(const void*){++deleteCalls;if(deleteFault){deleteFault=false;RaiseException(0xE0421021,0,0,nullptr);}}
void* __fastcall NativeMove(void* out,void* input) {
    ++moveCalls;actualMoveDestination=out;
    if(moveBefore){moveBefore=false;RaiseException(0xE0421022,0,0,nullptr);}
    auto* destination=static_cast<Weak*>(out);auto* source=static_cast<Weak*>(input);Weak was=*destination,next=*source;*source={};
    if(moveConsumed){moveConsumed=false;RaiseException(0xE0421023,0,0,nullptr);}
    *destination=next;
    if(movePublished){movePublished=false;RaiseException(0xE0421024,0,0,nullptr);}
    if(was.control&&InterlockedDecrement(reinterpret_cast<volatile LONG*>(const_cast<unsigned char*>(static_cast<const unsigned char*>(was.control))+12))==0)
        reinterpret_cast<void(__fastcall*)(const void*)>(At<void* const*>(was.control,0)[1])(was.control);
    return out;
}
void FinishWeak(void* weak){Weak empty{};NativeMove(weak,&empty);}
using Bytes=std::vector<unsigned char>;
void Add(Bytes& b,std::initializer_list<unsigned char> v){b.insert(b.end(),v);}
void U32(Bytes& b,std::size_t v){const auto x=static_cast<std::uint32_t>(v);const auto* p=reinterpret_cast<const unsigned char*>(&x);b.insert(b.end(),p,p+4);}
void Ptr(Bytes& b,const void* p){const auto* v=reinterpret_cast<const unsigned char*>(&p);b.insert(b.end(),v,v+8);}
void Jump(Bytes& b,const void* p){Add(b,{0xFF,0x25,0,0,0,0});Ptr(b,p);}
void Reg(Bytes& b,bool load,unsigned reg,unsigned base,std::size_t off){
    Add(b,{static_cast<unsigned char>(0x48|(reg>=8 ? 4 : 0)|(base>=8 ? 1 : 0)),
        static_cast<unsigned char>(load ? 0x8B : 0x89),static_cast<unsigned char>(0x80|((reg&7)<<3)|(base&7))});
    if((base&7)==4)Add(b,{0x24});U32(b,off);
}
void Xmm(Bytes& b,bool load,unsigned reg,unsigned base,std::size_t off){
    Add(b,{0xF3,static_cast<unsigned char>(0x40|(base>=8 ? 1 : 0)),0x0F,static_cast<unsigned char>(load ? 0x6F : 0x7F),
        static_cast<unsigned char>(0x80|(reg<<3)|(base&7))});
    if((base&7)==4)Add(b,{0x24});U32(b,off);
}
void* Exec(const Bytes& b){
    auto* p=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);if(!p)std::exit(2);
    std::memcpy(p,b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),p,b.size());return p;
}
void Leaf(unsigned rva,const void* fn){Bytes b;Jump(b,fn);std::memcpy(crew::image+rva,b.data(),b.size());}
constexpr unsigned vol[]={0,1,2,8,9,10,11},nvRegs[]={3,7,12,13};
struct alignas(16) In {
    std::uint64_t gpr[7]{},gprPadding=0;alignas(16) unsigned char xmm[6][16]{};
    const void* unit=nullptr;const void* owner=nullptr;const void* control=nullptr;std::uint64_t flags=0,nv[4]{};
    unsigned char descriptor[0x1E0]{};
};
struct alignas(16) Out {
    std::uint64_t gpr[7]{},gprPadding=0;alignas(16) unsigned char xmm[6][16]{};
    std::uint64_t flags=0,nv[4]{},frame=0,stack=0,expectedStack=0;
    unsigned char descriptor[0x1E0]{};unsigned path=0,tailPadding=0;std::uint64_t finalPadding=0;
};
using RunFn=void(*)(const In*,Out*);
void* Snapshot(unsigned path,const void* finish){
    Bytes b;for(unsigned i=0;i<7;++i)Reg(b,false,vol[i],15,offsetof(Out,gpr)+i*8);
    for(unsigned i=0;i<6;++i)Xmm(b,false,i,15,offsetof(Out,xmm)+i*16);
    Add(b,{0x9C,0x41,0x8F,0x87});U32(b,offsetof(Out,flags));
    for(unsigned i=0;i<4;++i)Reg(b,false,nvRegs[i],15,offsetof(Out,nv)+i*8);
    Reg(b,false,5,15,offsetof(Out,frame));Reg(b,false,4,15,offsetof(Out,stack));
    for(unsigned i=0;i<0x1E0/16;++i){Xmm(b,true,0,5,0x180+i*16);Xmm(b,false,0,15,offsetof(Out,descriptor)+i*16);}
    Add(b,{0x41,0xC7,0x87});U32(b,offsetof(Out,path));U32(b,path);
    Add(b,{0xFC,0x48,0x8D,0x8D,0xE0,0x01,0,0,0x48,0xB8});Ptr(b,reinterpret_cast<const void*>(&FinishWeak));Add(b,{0xFF,0xD0});
    Jump(b,finish);return Exec(b);
}
RunFn Harness(){
    auto* p=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!p)std::exit(2);
    Bytes b;Add(b,{0x53,0x56,0x57,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,
        0x48,0x81,0xEC,0x28,0x05,0,0,0x49,0x89,0xCE,0x49,0x89,0xD7,0x48,0x8D,0xAC,0x24,0,0x01,0,0});
    Reg(b,false,4,15,offsetof(Out,expectedStack));
    for(unsigned i=0;i<0x1E0/16;++i){Xmm(b,true,0,14,offsetof(In,descriptor)+i*16);Xmm(b,false,0,5,0x180+i*16);}
    Reg(b,true,0,14,offsetof(In,owner));Reg(b,false,0,5,static_cast<std::size_t>(-0x58));
    Reg(b,true,0,14,offsetof(In,control));Reg(b,false,0,5,static_cast<std::size_t>(-0x50));
    Reg(b,true,6,14,offsetof(In,unit));
    for(unsigned i=0;i<4;++i)Reg(b,true,nvRegs[i],14,offsetof(In,nv)+i*8);
    for(unsigned i=0;i<7;++i)Reg(b,true,vol[i],14,offsetof(In,gpr)+i*8);
    for(unsigned i=0;i<6;++i)Xmm(b,true,i,14,offsetof(In,xmm)+i*16);
    Add(b,{0x41,0xFF,0xB6});U32(b,offsetof(In,flags));Add(b,{0x9D});Jump(b,image+kSite);
    void* finish=p+b.size();Add(b,{0xFC,0x48,0x81,0xC4,0x28,0x05,0,0,0x41,0x5F,0x41,0x5E,0x41,0x5D,0x41,0x5C,0x5D,0x5F,0x5E,0x5B,0xC3});
    std::memcpy(p,b.data(),b.size());FlushInstructionCache(GetCurrentProcess(),p,b.size());
    Leaf(kSite+sizeof(kOriginal),Snapshot(1,finish));Leaf(0x6D3322,Snapshot(2,finish));return reinterpret_cast<RunFn>(p);
}
void Same(const In& in,const Out& out,bool blocked=false){
    CHECK(out.path==(blocked?2u:1u));CHECK(out.stack==out.expectedStack&&(out.stack&15)==0);
    for(unsigned i=0;i<4;++i)CHECK(out.nv[i]==in.nv[i]);
    for(unsigned i=0;i<7;++i)if(blocked?i!=0:i!=3&&i!=4)CHECK(out.gpr[i]==in.gpr[i]);
    for(unsigned i=0;i<6;++i)CHECK(!std::memcmp(out.xmm[i],in.xmm[i],16));
    if(!blocked){CHECK((out.flags&0xCD5)==(in.flags&0xCD5));CHECK(out.gpr[3]==At<std::uint64_t>(unit,0x2F0));CHECK(out.gpr[4]==out.frame+0x180);}
    else CHECK(out.gpr[0]==0);
    for(unsigned i=0;i<sizeof(in.descriptor);++i)if(i<0x60||i>=0x70)CHECK(out.descriptor[i]==in.descriptor[i]);
}
void ResetRefs(LONG oldWeak=3,LONG oldStrong=1){
    rootControl={controlTable,1,2,body};chosenControl={controlTable,1,1,chosenObject};
    *oldControl={controlTable,oldStrong,oldWeak,oldObject};
    active=contextKnown=copyReply=true;noFocus=false;copyFault=reenter=moveBefore=moveConsumed=movePublished=deleteFault=false;
}
}
namespace crew::enemy_native {
const NativeProfile* Profiles(std::size_t* count) noexcept {*count=std::size(profiles);return profiles;}
}
namespace crew {
bool EnemyControlTargetActive(const void* root) noexcept {return active&&root==body;}
bool EnemyControlNetworkContext(const void* root,bool* online) noexcept {if(!contextKnown||root!=body)return false;*online=false;return true;}
bool EnemyControlWeaponLock(const void* root,void* out) noexcept {
    ++lockQueries;if(reenter){reenter=false;sawReady=EnemyControlUnitGuideReady(currentProfile);restoreInside=RestoreEnemyControlUnitGuide(body,&rootControl);}
    if(copyFault){copyFault=false;RaiseException(0xE0421025,0,0,nullptr);}
    if(!copyReply||root!=body)return false;
    if(noFocus){*static_cast<unit_guide::Weak*>(out)={};return true;}
    InterlockedIncrement(&chosenControl.weaks);*static_cast<unit_guide::Weak*>(out)={chosenObject,&chosenControl};return true;
}
void* UnitGuideTestFunction(unsigned rva) noexcept {return rva==0x58EB40 ? reinterpret_cast<void*>(&NativeMove) : nullptr;}
}
LONG CALLBACK DebugException(EXCEPTION_POINTERS* e){
    if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
        std::printf("AV ip=%llx imageoff=%llx sp=%llx bp=%llx access=%llx\n",
            static_cast<unsigned long long>(e->ContextRecord->Rip),
            static_cast<unsigned long long>(e->ContextRecord->Rip-reinterpret_cast<std::uintptr_t>(image)),
            static_cast<unsigned long long>(e->ContextRecord->Rsp),static_cast<unsigned long long>(e->ContextRecord->Rbp),
            static_cast<unsigned long long>(e->ExceptionRecord->ExceptionInformation[1]));std::fflush(stdout);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
int main(){
    AddVectoredExceptionHandler(1,&DebugException);
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2100000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));if(!image)return 2;
    oldControl=static_cast<Control*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));if(!oldControl)return 2;
    controlTable[1]=reinterpret_cast<void*>(&DeleteWeak);
    for(unsigned i=0;i<std::size(profiles);++i)Put<void*>(image+profiles[i].vtableRva-8,0,image+0x100000+i*0x20);
    for(const auto& p:profiles)if(!std::strcmp(p.name,"EDF6_Merman"))currentProfile=&p;
    CHECK(currentProfile!=nullptr);
    void* privateTable[4]={At<void*>(image+currentProfile->vtableRva-8,0)};Put<void*>(body,0,privateTable+1);
    Put<void*>(body,0x28,body);Put<void*>(body,0x30,&rootControl);Put<void*>(unit,0x2B0,body);Put<void*>(unit,0x2B8,&rootControl);
    Put<std::uint64_t>(unit,0x2F0,0x445566778899AABB);Put<std::uint64_t>(unit,0x4E0,0xF1E2D3C4B5A69788);Put<std::uint64_t>(body,0x3E8,0x1122334455667788);
    std::memcpy(image+kSite,kOriginal,sizeof(kOriginal));std::memcpy(image+kMove,kMoveBytes,sizeof(kMoveBytes));RunFn run=Harness();
    In in{};in.unit=unit;in.owner=body;in.control=&rootControl;in.flags=0xED7;
    for(unsigned i=0;i<7;++i)in.gpr[i]=0x1234567800000000ull+i*29;
    for(unsigned i=0;i<4;++i)in.nv[i]=0xABCD123400000000ull+i*31;
    for(unsigned i=0;i<6;++i)for(unsigned j=0;j<16;++j)in.xmm[i][j]=static_cast<unsigned char>(i*23+j);
    for(unsigned i=0;i<sizeof(in.descriptor);++i)in.descriptor[i]=static_cast<unsigned char>(i*7+3);
    *reinterpret_cast<Weak*>(in.descriptor+0x60)={oldObject,oldControl};
    ResetRefs();image[kSite+3]^=1;CHECK(!InstallEnemyControlUnitGuide());image[kSite+3]^=1;
    CHECK(InstallEnemyControlUnitGuide());CHECK(InstallEnemyControlUnitGuide());CHECK(EnemyControlUnitGuideReady(currentProfile));
    NativeProfile forged=*currentProfile;CHECK(!EnemyControlUnitGuideReady(&forged));
    Out out{};reenter=true;run(&in,&out);Same(in,out);CHECK(sawReady&&!restoreInside);CHECK(At<const void*>(out.descriptor,0x60)==chosenObject&&At<const void*>(out.descriptor,0x68)==&chosenControl);
    CHECK(oldControl->weaks==2&&chosenControl.weaks==1&&rootControl.weaks==2);CHECK(!faulted);
    CHECK(At<std::uint64_t>(unit,0x4E0)==0xF1E2D3C4B5A69788&&At<std::uint64_t>(body,0x3E8)==0x1122334455667788);
    ResetRefs();copyReply=false;out={};run(&in,&out);Same(in,out);CHECK(!At<const void*>(out.descriptor,0x60)&&!At<const void*>(out.descriptor,0x68));CHECK(oldControl->weaks==2&&chosenControl.weaks==1);
    ResetRefs();noFocus=true;out={};run(&in,&out);Same(in,out);CHECK(!At<const void*>(out.descriptor,0x60)&&!At<const void*>(out.descriptor,0x68)&&oldControl->weaks==2&&chosenControl.weaks==1);
    ResetRefs();copyFault=true;out={};run(&in,&out);Same(in,out);CHECK(!At<const void*>(out.descriptor,0x68)&&oldControl->weaks==2&&!faulted);
    for(unsigned reason=0;reason<4;++reason){
        ResetRefs();if(reason==0)active=false;if(reason==1)contextKnown=false;if(reason==2)in.owner=chosenObject;if(reason==3)in.control=oldControl;
        const auto prior=lockQueries;out={};run(&in,&out);Same(in,out);CHECK(lockQueries==prior&&At<const void*>(out.descriptor,0x60)==oldObject&&oldControl->weaks==2);
        in.owner=body;in.control=&rootControl;
    }
    ResetRefs();DWORD oldProtection=0;CHECK(VirtualProtect(oldControl,4096,PAGE_READONLY,&oldProtection)!=0);
    out={};run(&in,&out);Same(in,out,true);CHECK(faulted&&oldControl->weaks==3&&chosenControl.weaks==1&&rootControl.weaks==3);
    CHECK(!RestoreEnemyControlUnitGuide(body,&rootControl)&&oldControl->weaks==3);CHECK(!EnemyControlUnitGuideReady(currentProfile));
    DWORD ignored=0;CHECK(VirtualProtect(oldControl,4096,oldProtection,&ignored)!=0);
    CHECK(RestoreEnemyControlUnitGuide(body,&rootControl)&&oldControl->weaks==2&&rootControl.weaks==2&&!faulted);
    for(unsigned phase=0;phase<3;++phase){
        ResetRefs();moveBefore=phase==0;moveConsumed=phase==1;movePublished=phase==2;out={};run(&in,&out);Same(in,out,true);
        CHECK(faulted&&oldControl->weaks==3&&rootControl.weaks==3);
        CHECK(chosenControl.weaks==(phase==2?1:2));
        CHECK(RestoreEnemyControlUnitGuide(body,&rootControl)&&oldControl->weaks==2&&chosenControl.weaks==1&&rootControl.weaks==2&&!faulted);
    }
    image[kSite+6]^=1;CHECK(!EnemyControlUnitGuideReady(currentProfile));CHECK(!InstallEnemyControlUnitGuide());
    const NativeProfile* ordinary=nullptr;for(const auto& p:profiles)if(!std::strcmp(p.name,"GiantAnt"))ordinary=&p;
    CHECK(EnemyControlUnitGuideReady(ordinary));image[kSite+6]^=1;CHECK(EnemyControlUnitGuideReady(currentProfile));
    DWORD protection=0;CHECK(VirtualProtect(thunk,installed.size,PAGE_EXECUTE_READWRITE,&protection)!=0);
    static_cast<unsigned char*>(thunk)[installed.size-1]^=1;CHECK(!EnemyControlUnitGuideReady(currentProfile));static_cast<unsigned char*>(thunk)[installed.size-1]^=1;
    CHECK(VirtualProtect(thunk,installed.size,protection,&ignored)!=0);CHECK(EnemyControlUnitGuideReady(currentProfile));
    ResetRefs(1,0);deleteFault=true;const auto deleted=deleteCalls;out={};run(&in,&out);Same(in,out,true);
    CHECK(faulted&&deleteCalls==deleted+1&&oldControl->weaks==0&&rootControl.weaks==3&&chosenControl.weaks==1);
    CHECK(!RestoreEnemyControlUnitGuide(body,&rootControl)&&!RestoreEnemyControlUnitGuide(body,&rootControl));
    CHECK(deleteCalls==deleted+1&&oldControl->weaks==0); // uncertain final delete is NEVER repeated
    std::printf("Unit guide: %d checks, %d failures\n",checks,failed);return failed?1:0;
}

