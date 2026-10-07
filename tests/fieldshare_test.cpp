// Production hooks with recording init/destruction and per-instance chunks. Optional EDF.dll mode executes the
// real empty chunk constructor, shared_ptr destructor and deactivation in a DONT_RESOLVE_DLL_REFERENCES mapping.
// No game process, GPU resources, render/culling, terrain decode or game-directory writes are involved.
#include "../src/crew.h"
#include <atomic>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
DWORD TestWait(HANDLE,DWORD);
#define WaitForSingleObject TestWait
#include "../src/fieldshare.cpp"
#undef WaitForSingleObject

namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
}
namespace {
using namespace crew;
std::atomic<int> allocations{0},deallocations{0},stockTasks{0},starts{0};
int checks=0;
bool failAllocation=false,failCtor=false,emptyInit=false,native=false;
unsigned char* destroyDuringWait=nullptr;
HANDLE donorGate=nullptr,donorStarted=nullptr;
unsigned char* blockedRender=nullptr;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
void* __fastcall Allocate(std::size_t size) {
    if(failAllocation)return nullptr;
    ++allocations;
    return std::calloc(1,size);
}
void __fastcall Delete(void* p,std::size_t) { if(p){++deallocations;std::free(p);} }
void __fastcall Ctor(unsigned char* c) {
    if(failCtor)RaiseException(0xE0000079,0,0,nullptr);
    std::memset(c,0,0x190);
}
void __fastcall DestroyChunk(unsigned char*) {}
void __fastcall DestroyBlock(unsigned char* b) { Delete(b,kChunkBlock); }
void __fastcall Deactivate(unsigned char*) {}
void Jump(std::size_t rva,const void* to) {
    unsigned char code[12]={0x48,0xB8};
    std::memcpy(code+2,&to,8);code[10]=0xFF;code[11]=0xE0;
    DWORD old=0;
    Check(VirtualProtect(image+rva,sizeof(code),PAGE_EXECUTE_READWRITE,&old)!=0,"patch private fixture code");
    std::memcpy(image+rva,code,sizeof(code));
    DWORD ignored=0;VirtualProtect(image+rva,sizeof(code),old,&ignored);
    FlushInstructionCache(GetCurrentProcess(),image+rva,sizeof(code));
}
Ref Make(int type) {
    auto b=static_cast<unsigned char*>(Allocate(kChunkBlock));
    *reinterpret_cast<const void**>(b)=image+kChunkBlockVtbl;
    *reinterpret_cast<long*>(b+8)=1;*reinterpret_cast<long*>(b+12)=1;
    reinterpret_cast<ChunkCtorFn>(image+kEmptyChunk)(b+16);
    auto c=b+16;
    *reinterpret_cast<int*>(c)=type;
    float bounds[8]={1,2,3,0,4,5,6,0};std::memcpy(c+0x10,bounds,sizeof(bounds));
    *reinterpret_cast<unsigned char**>(c+0xF8)=c;
    return {c,b};
}
void** __fastcall StockTask(void**,void** out,int index) {
    ++stockTasks;
    const Ref r=Make(index%3);out[0]=r.chunk;out[1]=r.block;return out;
}
std::uint64_t __fastcall StockInit(unsigned char* fr,void*,const void*,unsigned char* param) {
    ++starts;
    if(fr==blockedRender){SetEvent(donorStarted);Check(::WaitForSingleObject(donorGate,5000)==WAIT_OBJECT_0,"release donor gate");}
    if(emptyInit)return 0;
    auto chunks=new Ref[3]{};
    // The game's task wrapper takes a closure (vtable followed by four captured words) and an index pointer.
    // In native mode run that wrapper too, so the hook sees its real adjusted RCX and dereferenced R8.
    void* closure[5]={nullptr,nullptr,fr,nullptr,param};
    for(int i=0;i<3;++i) {
        if(native)reinterpret_cast<void**(__fastcall*)(void**,void**,int*)>(image+kTaskSigAt)(closure,
            reinterpret_cast<void**>(&chunks[i]),&i);
        else TaskHook(closure+1,reinterpret_cast<void**>(&chunks[i]),i);
    }
    *reinterpret_cast<Ref**>(fr+kChunks)=chunks;
    *reinterpret_cast<std::size_t*>(fr+kChunkCount)=3;
    return 42;
}
void* __fastcall StockDtor(unsigned char* self,unsigned) {
    auto fr=self+kFieldRender;
    auto chunks=*reinterpret_cast<Ref**>(fr+kChunks);
    const auto count=*reinterpret_cast<std::size_t*>(fr+kChunkCount);
    for(std::size_t i=0;i<count;++i)Release(chunks[i].block);
    delete[] chunks;
    *reinterpret_cast<Ref**>(fr+kChunks)=nullptr;
    *reinterpret_cast<std::size_t*>(fr+kChunkCount)=0;
    return self;
}
struct Piece {
    alignas(16) unsigned char bytes[0x1350]{};
    unsigned char* fr(){return bytes+kFieldRender;}
    Ref* chunks(){return *reinterpret_cast<Ref**>(fr()+kChunks);}
};
void Clean() {
    Check(byFmb.empty() && byRender.empty() && pending.empty(),"all cache indices released");
    Check(allocations==deallocations,"all chunk blocks released exactly once");
}
}
DWORD TestWait(HANDLE h,DWORD ms) {
    if(destroyDuringWait) {
        auto self=destroyDuringWait;destroyDuringWait=nullptr;
        crew::DtorHook(self,0);
        return WAIT_FAILED;
    }
    return ::WaitForSingleObject(h,ms);
}
int main(int argc,char** argv) {
    using namespace crew;
    if(argc==2) {
        const int n=MultiByteToWideChar(CP_UTF8,0,argv[1],-1,nullptr,0);
        std::wstring path(static_cast<std::size_t>(n),L'\0');
        MultiByteToWideChar(CP_UTF8,0,argv[1],-1,path.data(),n);
        image=reinterpret_cast<unsigned char*>(LoadLibraryExW(path.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES));
        if(!image){std::puts("SKIP: EDF.dll unavailable");return 77;}
        const auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);
        const auto pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
        Check(pe->FileHeader.TimeDateStamp==0x678CCB46 && pe->OptionalHeader.SizeOfImage==0x22CE000,"supported native image");
        Check(Matches(kEmptyChunk,kEmptyChunkSig,sizeof(kEmptyChunkSig)),"native constructor profile");
        Check(Matches(kInitSigAt,kInitSig,sizeof(kInitSig)) && Matches(kTaskSigAt,kTaskSig,sizeof(kTaskSig)),"native call-site ABI profile");
        Check(Matches(kDtor,kDtorSig,sizeof(kDtorSig)),"native destruction profile");
        // Only the CRT memset import is needed by the native empty constructor. Sized allocation/free are recorders.
        DWORD old=0;Check(VirtualProtect(image+0x1756068,8,PAGE_READWRITE,&old)!=0,"private memset import writable");
        *reinterpret_cast<void**>(image+0x1756068)=reinterpret_cast<void*>(&std::memset);
        DWORD ignored=0;VirtualProtect(image+0x1756068,8,old,&ignored);
        native=true;
    } else {
        image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1800000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        Check(image!=nullptr,"fixture image");
        Jump(kEmptyChunk,reinterpret_cast<void*>(&Ctor));
        Jump(kDeactivate,reinterpret_cast<void*>(&Deactivate));
        auto vt=reinterpret_cast<void**>(image+kChunkBlockVtbl);
        vt[0]=reinterpret_cast<void*>(&DestroyChunk);vt[1]=reinterpret_cast<void*>(&DestroyBlock);
        for(int i=0;i<4;++i)reinterpret_cast<float*>(image+kObbW)[i]=1;
    }
    Jump(kNew,reinterpret_cast<void*>(&Allocate));Jump(kDelete,reinterpret_cast<void*>(&Delete));
    if(native)Check(InstallTerrainShare(),"installs native task/init/destructor hooks in private mapping");
    realInit=&StockInit;realTask=&StockTask;realDtor=&StockDtor;config.terrainShare=true;
    unsigned char param[0x80]{};
    const float world[16]={0,0,-1,0,0,2,0,0,1,0,0,0,100,200,300,1};
    std::memcpy(param+kParamWorld,world,sizeof(world));
    const int fmb=79;
    for(int pass=0;pass<2;++pass) {
        Piece donor,copy;
        Check(InitHook(donor.fr(),nullptr,&fmb,param)==42,"donor init result preserved");
        const int before=stockTasks;
        Check(InitHook(copy.fr(),nullptr,&fmb,param)==42,"copy init result preserved");
        Check(stockTasks==before,"copy avoids stock decode");
        Check(copy.chunks()[0].chunk==donor.chunks()[0].chunk,"type 0 node shared");
        for(int i=1;i<3;++i) {
            auto c=copy.chunks()[i].chunk;
            Check(c!=donor.chunks()[i].chunk,"polygon has instance-local chunk");
            Check(*reinterpret_cast<unsigned char**>(c+0xF8)==donor.chunks()[i].chunk,"geometry points to donor");
            Check(*reinterpret_cast<unsigned char**>(c+0xA8)==copy.fr(),"owner belongs to copy");
            Check(std::memcmp(c+0x100,world,sizeof(world))==0,"instance world transform retained");
            auto box=reinterpret_cast<float*>(c+0xB0);
            Check(box[2]==-4 && box[5]==10 && box[8]==6 && box[12]==103 && box[13]==204 && box[14]==299,
                  "rotated/scaled world OBB correct");
        }
        const Ref kept=donor.chunks()[1];
        if(pass==0) {
            DtorHook(donor.bytes,0);
            Check(*reinterpret_cast<long*>(kept.block+8)==1,"geometry remains pinned after donor destroyed");
            Check(*reinterpret_cast<unsigned char**>(copy.chunks()[1].chunk+0xF8)==kept.chunk,"copy geometry remains valid");
            DtorHook(copy.bytes,0);
        } else {DtorHook(copy.bytes,0);DtorHook(donor.bytes,0);}
        Clean();
    }
    // Real waiter/donor concurrency: exactly one decoder, independent instance-local chunks.
    Piece first,second;donorGate=CreateEventW(nullptr,TRUE,FALSE,nullptr);donorStarted=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    blockedRender=first.fr();const int before=stockTasks;
    std::thread a([&]{InitHook(first.fr(),nullptr,&fmb,param);});
    Check(::WaitForSingleObject(donorStarted,5000)==WAIT_OBJECT_0,"donor entered init");
    std::thread b([&]{InitHook(second.fr(),nullptr,&fmb,param);});
    bool waiting=false;
    for(int i=0;i<5000 && !waiting;++i) {
        AcquireSRWLockShared(&lock);waiting=byFmb.at(&fmb)->waiting==1;ReleaseSRWLockShared(&lock);
        if(!waiting)Sleep(1);
    }
    Check(waiting,"second initializer pins donor while waiting");SetEvent(donorGate);a.join();b.join();
    blockedRender=nullptr;CloseHandle(donorGate);CloseHandle(donorStarted);
    Check(stockTasks==before+3,"concurrent instances decode only once");
    DtorHook(first.bytes,0);DtorHook(second.bytes,0);Clean();
    // The last waiter drops the final cache pin even if its wait fails after the donor is destroyed.
    Piece dying,fallback;InitHook(dying.fr(),nullptr,&fmb,param);destroyDuringWait=dying.bytes;
    InitHook(fallback.fr(),nullptr,&fmb,param);
    Check(byFmb.empty(),"failed last waiter releases orphaned entry");DtorHook(fallback.bytes,0);Clean();
    emptyInit=true;Piece empty;InitHook(empty.fr(),nullptr,&fmb,param);emptyInit=false;Clean();
    // Both allocation and post-construction faults fall back without leaking the attempted copy.
    const Ref donor=Make(1);void* out[2]{};
    failAllocation=true;Check(!CopyChunk(first.fr(),param,1,donor,out),"allocation failure falls back");failAllocation=false;
    const int live=allocations-deallocations;
    Check(!CopyChunk(first.fr(),nullptr,1,donor,out),"fault after construction falls back");
    Check(allocations-deallocations==live,"post-construction fault frees chunk");
    if(!native) {
        failCtor=true;Check(!CopyChunk(first.fr(),param,1,donor,out),"constructor fault falls back");failCtor=false;
        Check(allocations-deallocations==live,"constructor fault frees allocation");
    }
    Release(donor.block);Clean();
    config.terrainShare=false;Piece disabled;InitHook(disabled.fr(),nullptr,&fmb,param);
    Check(byFmb.empty(),"disabled option does not cache");DtorHook(disabled.bytes,0);Clean();
    std::printf("fieldshare: %d checks passed (%s chunks); no game launched\n",checks,native ? "native empty" : "recording");
    return 0;
}
