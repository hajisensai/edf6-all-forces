// The map terrain decoded once per FMB (the user, 2026-10-07: an 8 GB machine ran out of memory on the 3 x 3 big map;
// "optimise it, the terrain must stay the same for everyone"). Every placement of an .fmb piece (Preload_Fmex, ctor
// 0x1527D0) builds its own FieldRender (obj+0x10D8), whose Initialize (0x10E8B0) CMPL-decodes the FMB again and makes a
// vertex buffer, index buffers and the 1 m splat buffer for every chunk (task 0x10C7B0 -> 0x10D8A0): about 699 MB for
// the plain's ground, 1.3 GB a copy of its block; measured 16.7 GB committed on the big test range against 4.7 GB stock.
// The FMB bytes themselves are shared by name; the decoded chunks are not.
//
// A chunk (FieldRender::PolygonChunk, a 0x1A0 make_shared block, data L at +0x10) holds per-instance state: its render
// object at L+0x30 (an umbra object registered by Initialize: chunk index L+0xA0 / +0xF4, owner FieldRender L+0xA8,
// world OBB L+0xB0..0xEF, world matrix L+0x100..0x13F) and the geometry (VB L+0x140, meshes L+0x168..0x187). The draw
// (render object slot 1, 0x10EE90) reads the geometry only through L+0xF8, which the game sets to L itself. So a later
// instance of the same FMB gets small chunks of its own (empty geometry) whose L+0xF8 is the first instance's chunk:
// the same terrain drawn at its own place, with its own culling, its own materials (the FieldRender's, from the FMB).
//  - the first instance of an FMB to finish Initialize is the donor: its chunk array (FieldRender+8, count +0x18, 16-byte
//    {chunk, control block}) is kept, each chunk with one more reference, while any instance of the FMB lives;
//  - an instance whose Initialize starts after that gets, from the chunk task (call 0x1106B0), a fresh chunk for each
//    polygon chunk (types 1 and 2) instead of a decode: the empty chunk constructor 0x10C130, then the fields the task
//    writes after its own build (0x10CA9B..0x10CC44), the geometry pointer at the donor's chunk; a type 0 node (no
//    geometry, no render object) is shared as it is;
//  - when the donor's piece is destroyed (vtable 0x176BF88 slot 2, 0x153E50) its render objects are taken out of umbra
//    first (0x1104E0, the game's own deactivation): its chunks outlive it for the other instances and must not be drawn
//    with an owner that is gone; the kept references go when the FMB's last instance does.
// ini TerrainShare=0 turns it off. All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include <cstdint>
#include <cstring>
#include <atomic>
#include <unordered_map>
#include <vector>

namespace crew {
namespace {
constexpr std::size_t kInitSite=0x152E1A,kInitSigAt=0x152E01,kInit=0x10E8B0;
constexpr std::size_t kTaskSite=0x1106B0,kTaskSigAt=0x1106A0,kTask=0x10C7B0;
constexpr std::size_t kVtable=0x176BF88,kDtorSlot=2,kDtor=0x153E50;
constexpr std::size_t kNew=0x12D85B0,kEmptyChunk=0x10C130,kChunkBlockVtbl=0x1769868,kDeactivate=0x1104E0,kObbW=0x1765B90;
constexpr std::size_t kDelete=0x12D85EC;
constexpr std::size_t kFieldRender=0x10D8,kChunks=8,kChunkCount=0x18;
constexpr std::size_t kChunkBlock=0x1A0,kParamWorld=0x40;
const unsigned char kInitSig[]={0x4C,0x8D,0x8D,0xE0,0x00,0x00,0x00,0x4D,0x8B,0x45,0x40,0x48,0x8D,0x15,0xDD,0x41,0xFE,0x01,
                                0x49,0x8D,0x8F,0xD8,0x10,0x00,0x00,0xE8,0x91,0xBA,0xFB,0xFF};
const unsigned char kTaskSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x45,0x8B,0x00,0x48,0x83,0xC1,0x08,0x48,0x8B,0xDA,0xE8};
const unsigned char kDtorSig[]={0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83,0xEC,0x20,0x8B,0xDA,0x48,0x8B,0xF9,0x48,0x81,0xC1,0x40,0x12,0x00,0x00};
const unsigned char kEmptyChunkSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57,0x48,0x83,0xEC,0x20,0x48,0x8B,0xF9};

using InitFn=std::uint64_t(__fastcall*)(unsigned char*,void*,const void*,unsigned char*);
using TaskFn=void**(__fastcall*)(void**,void**,int);
using DtorFn=void*(__fastcall*)(unsigned char*,unsigned);
using NewFn=void*(__fastcall*)(std::size_t);
using DeleteFn=void(__fastcall*)(void*,std::size_t);
using ChunkCtorFn=void(__fastcall*)(unsigned char*);
using DeactivateFn=void(__fastcall*)(unsigned char*);

struct Ref { unsigned char* chunk; unsigned char* block; };
struct Entry {
    const void* fmb=nullptr;
    unsigned char* donor=nullptr;   // the donor's FieldRender
    std::vector<Ref> chunks;        // the donor's chunks, one reference each held here
    int users=0;                    // live instances on it, the donor's included
    int copies=0;                   // instances that took its chunks (this mission, for the log)
    int waiting=0;                  // instances waiting on its donor's decode
    bool ready=false;               // the donor's chunks are in
    bool failed=false;              // the donor had none: nothing shared
    HANDLE done=nullptr;            // set when the donor's Initialize is over
};
SRWLOCK lock=SRWLOCK_INIT;
std::unordered_map<const void*,Entry*> byFmb;               // an FMB's entry while any instance of it lives
std::unordered_map<const unsigned char*,Entry*> byRender;   // an instance's FieldRender -> the entry it uses
std::unordered_map<const unsigned char*,Entry*> pending;    // a FieldRender inside Initialize -> the entry to copy
bool installed=false;
std::atomic<int> logged{0};
constexpr int kMostLogged=24;

void AddRef(unsigned char* block) noexcept { if(block)_InterlockedIncrement(reinterpret_cast<volatile long*>(block+8)); }

// The game's shared_ptr release: the object at the last use, the block at the last weak.
void Release(unsigned char* block) noexcept {
    if(!block)return;
    auto vtbl=*reinterpret_cast<void(__fastcall***)(unsigned char*)>(block);
    if(_InterlockedDecrement(reinterpret_cast<volatile long*>(block+8))==0) {
        vtbl[0](block);
        if(_InterlockedDecrement(reinterpret_cast<volatile long*>(block+0xC))==0)vtbl[1](block);
    }
}

// A chunk of `fr` for chunk `index`, drawing `donor`'s geometry: as the task builds one (0x10D8A0's block, then the
// fields 0x10CA9B..0x10CC44 write), its geometry pointer at the donor's chunk.
bool CopyChunk(unsigned char* fr,const unsigned char* param,int index,const Ref& donor,void** out) noexcept {
    unsigned char* block=nullptr;
    bool constructed=false;
    __try {
        block=static_cast<unsigned char*>(reinterpret_cast<NewFn>(image+kNew)(kChunkBlock));
        if(!block)return false;
        std::memset(block,0,0x10);
        *reinterpret_cast<const void**>(block)=image+kChunkBlockVtbl;
        *reinterpret_cast<int*>(block+8)=1;
        *reinterpret_cast<int*>(block+0xC)=1;
        unsigned char* const c=block+0x10;
        reinterpret_cast<ChunkCtorFn>(image+kEmptyChunk)(c);
        constructed=true;
        const unsigned char* const d=donor.chunk;
        *reinterpret_cast<int*>(c)=*reinterpret_cast<const int*>(d);
        std::memcpy(c+0x10,d+0x10,0x20);   // the chunk's model-space box: centre, half extents (clamped)
        const float* w=reinterpret_cast<const float*>(param+kParamWorld);
        const float* centre=reinterpret_cast<const float*>(d+0x10);
        const float* half=reinterpret_cast<const float*>(d+0x20);
        const float* k=reinterpret_cast<const float*>(image+kObbW);
        float obb[16];
        for(int i=0;i<4;++i) {
            obb[i]=half[0]*w[i];
            obb[4+i]=half[1]*w[4+i];
            obb[8+i]=half[2]*w[8+i];
            obb[12+i]=centre[0]*w[i]+centre[1]*w[4+i]+centre[2]*w[8+i]+k[i]*w[12+i];
        }
        *reinterpret_cast<std::int64_t*>(c+0xA0)=index;
        *reinterpret_cast<unsigned char**>(c+0xA8)=fr;
        std::memcpy(c+0xB0,obb,sizeof(obb));
        *reinterpret_cast<int*>(c+0xF0)=0;
        *reinterpret_cast<int*>(c+0xF4)=index;
        *reinterpret_cast<const unsigned char**>(c+0xF8)=d;   // the geometry: the donor's
        std::memcpy(c+0x100,w,0x40);
        out[0]=c;out[1]=block;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        // A failed copy must not leave its allocation behind when the stock task takes over.
        if(constructed)Release(block);
        else if(block)reinterpret_cast<DeleteFn>(image+kDelete)(block,kChunkBlock);
        return false;
    }
}

// Called under the exclusive lock. A completed entry can also lose its last owner while a waiter is waking up;
// a failed wait then drops the final pin, and must release the cache just as the last destructor would.
Entry* UnusedEntry(Entry* e) {
    if(e->users || e->waiting || (!e->ready && !e->failed))return nullptr;
    if(auto it=byFmb.find(e->fmb);it!=byFmb.end() && it->second==e)byFmb.erase(it);
    return e;
}

void DeleteEntry(Entry* e) noexcept {
    if(!e)return;
    for(const Ref& ref:e->chunks)Release(ref.block);
    if(e->done)CloseHandle(e->done);
    delete e;
}

InitFn realInit=nullptr;
TaskFn realTask=nullptr;
DtorFn realDtor=nullptr;

// The first instance of an FMB to start Initialize is its donor (registered at once, "not ready"); one starting after
// it waits for the donor's decode and then takes its chunks. The wait is safe: each Initialize decodes on worker threads
// of its own (0x65A70 / 0x65B90 build them, 0x65F10 waits on their events), so a waiting loader thread starves nothing,
// and nothing of this file is held while it waits. Without the wait, the loader's four parallel tasks let half the copies
// start before the donor was done, each decoding all of it again (2026-10-08: 4 of the ground's 8 copies).
constexpr DWORD kWaitMs=60000;   // a donor never ending its Initialize: the copy decodes by itself (logged)

std::uint64_t __fastcall InitHook(unsigned char* fr,void* device,const void* fmb,unsigned char* param) {
    if(!Cfg().terrainShare)return realInit(fr,device,fmb,param);
    Entry* e=nullptr;
    bool donor=false;
    AcquireSRWLockExclusive(&lock);
    if(auto it=byFmb.find(fmb);it!=byFmb.end())e=it->second;
    else {
        e=new Entry{};
        e->fmb=fmb;e->donor=fr;
        e->done=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        byFmb[fmb]=e;
        donor=true;
    }
    if(!donor)++e->waiting;   // keeps the entry (and its event) while this one waits on it
    ReleaseSRWLockExclusive(&lock);
    if(donor) {
        const std::uint64_t r=realInit(fr,device,fmb,param);
        AcquireSRWLockExclusive(&lock);
        const auto first=*reinterpret_cast<unsigned char**>(fr+kChunks);
        const auto count=*reinterpret_cast<std::size_t*>(fr+kChunkCount);
        if(first && count && count<100000) {
            e->chunks.reserve(count);
            for(std::size_t i=0;i<count;++i) {
                const Ref ref{*reinterpret_cast<unsigned char**>(first+i*16),*reinterpret_cast<unsigned char**>(first+i*16+8)};
                AddRef(ref.block);
                e->chunks.push_back(ref);
            }
            e->ready=true;
            ++e->users;
            byRender[fr]=e;
        } else {
            byFmb.erase(fmb);   // nothing to share: the instances after it decode by themselves
            e->failed=true;
        }
        SetEvent(e->done);
        Entry* const orphan=UnusedEntry(e);
        ReleaseSRWLockExclusive(&lock);
        DeleteEntry(orphan);
        return r;
    }
    const bool signalled=WaitForSingleObject(e->done,kWaitMs)==WAIT_OBJECT_0;
    AcquireSRWLockExclusive(&lock);
    --e->waiting;
    const bool share=signalled && e->ready;
    if(share){pending[fr]=e;++e->users;++e->copies;byRender[fr]=e;}
    Entry* const orphan=UnusedEntry(e);
    const int copies=e->copies;
    const std::size_t n=e->chunks.size();
    ReleaseSRWLockExclusive(&lock);
    DeleteEntry(orphan);
    if(!signalled)Log("TERRAIN %p: the donor of FMB %p not done in %lu ms: decoded by itself",fr,fmb,kWaitMs);
    const std::uint64_t r=realInit(fr,device,fmb,param);
    if(share) {
        AcquireSRWLockExclusive(&lock);
        pending.erase(fr);
        ReleaseSRWLockExclusive(&lock);
        if(logged.fetch_add(1)<kMostLogged)Log("TERRAIN %p: FMB %p drawn from %zu shared chunks (copy %d)",fr,fmb,n,copies);
    }
    return r;
}

void** __fastcall TaskHook(void** cap,void** out,int index) {
    unsigned char* const fr=static_cast<unsigned char*>(cap[1]);
    Ref donor{};
    AcquireSRWLockShared(&lock);
    if(auto it=pending.find(fr);it!=pending.end() && index>=0 && static_cast<std::size_t>(index)<it->second->chunks.size())
        donor=it->second->chunks[index];
    ReleaseSRWLockShared(&lock);
    if(!donor.chunk)return realTask(cap,out,index);
    const int type=*reinterpret_cast<const int*>(donor.chunk);
    if(type!=1 && type!=2) {   // a node without geometry or render object: the same one
        AddRef(donor.block);
        out[0]=donor.chunk;out[1]=donor.block;
        return out;
    }
    if(CopyChunk(fr,static_cast<const unsigned char*>(cap[3]),index,donor,out))return out;
    return realTask(cap,out,index);
}

void* __fastcall DtorHook(unsigned char* self,unsigned flags) {
    unsigned char* const fr=self+kFieldRender;
    Entry* e=nullptr;
    AcquireSRWLockExclusive(&lock);
    if(auto it=byRender.find(fr);it!=byRender.end()){e=it->second;byRender.erase(it);}
    ReleaseSRWLockExclusive(&lock);
    if(e && e->donor==fr) {
        // Its chunks stay for the other instances: out of umbra now, while their owner is still alive.
        __try { reinterpret_cast<DeactivateFn>(image+kDeactivate)(fr); } __except(EXCEPTION_EXECUTE_HANDLER){}
    }
    void* const r=realDtor(self,flags);
    if(!e)return r;
    Entry* gone=nullptr;
    AcquireSRWLockExclusive(&lock);
    if(e->donor==fr)e->donor=nullptr;
    --e->users;
    gone=UnusedEntry(e);
    ReleaseSRWLockExclusive(&lock);
    DeleteEntry(gone);
    return r;
}
}  // namespace

bool InstallTerrainShare() noexcept {
    __try {
        if(!Matches(kInitSigAt,kInitSig,sizeof(kInitSig)) || !Matches(kTaskSigAt,kTaskSig,sizeof(kTaskSig)) ||
           !Matches(kDtor,kDtorSig,sizeof(kDtorSig)) || !Matches(kEmptyChunk,kEmptyChunkSig,sizeof(kEmptyChunkSig))) {
            Log("TERRAIN share: unexpected EDF.dll code: every placement decodes its own terrain");
            return false;
        }
        realInit=reinterpret_cast<InitFn>(image+kInit);
        realTask=reinterpret_cast<TaskFn>(image+kTask);
        realDtor=reinterpret_cast<DtorFn>(image+kDtor);
        bool changed=false;
        const bool task=edf::RedirectCall(image+kTaskSite,image+kTask,reinterpret_cast<void*>(&TaskHook),changed);
        const bool dtor=task && PatchVtableSlot(reinterpret_cast<void**>(image+kVtable)+kDtorSlot,image+kDtor,
                                                reinterpret_cast<void*>(&DtorHook));
        // Initialize last: nothing is shared before the task and the destructor take part.
        const bool init=dtor && edf::RedirectCall(image+kInitSite,image+kInit,reinterpret_cast<void*>(&InitHook),changed);
        installed=task && dtor && init;
        Log("HOOK terrain share=%d (task=%d dtor=%d init=%d; ini TerrainShare=%d)",installed,task,dtor,init,Cfg().terrainShare);
        return installed;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}

void ResetTerrainShare() noexcept {
    AcquireSRWLockShared(&lock);
    const std::size_t live=byFmb.size();
    ReleaseSRWLockShared(&lock);
    if(live && Cfg().debug)Log("TERRAIN %zu FMB(s) still shared at the mission's start (their pieces live on)",live);
    logged=0;
}
}  // namespace crew
