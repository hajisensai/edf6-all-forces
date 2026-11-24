// The physics determinism probe (test only, part of EDF6Autopilot): asked by the autopilot command
// "probe physics [TICKS]" (default 6000 scene ticks = 100 s of game time). net-10: does EDF6VehicleCrew's
// HavokMultithread change what the physics computes? Runs of one mission with it on and off are compared offline
// (tools/physics_probe_compare.py).
//
// Where it samples (all EDF.dll TimeDateStamp 0x678CCB46):
//  - Application::OnWorldStepBegin (vtable slot EDF+0x17E91E0, 0x705A60; the autopilot's "cycle" chains the same slot)
//    runs once per scene tick on the game thread: it calls MissionMgr::Update (0x1F6F40) with the fixed step 1/60
//    (0x705AAA, the float at 0x179F358). The wrapper calls the original first, then samples. The physics step of the
//    same tick (System::Update 0x1198DA0: 0x1199359 / 0x1199479 call 0x11AD780 with xmm3 = xmm6, loaded at 0x1198DF6
//    from the same 1/60) is not running then: it is called synchronously further on in System::Update.
//  - The scene tick: System (= *(EDF+0x20B2958), the object System::Update runs on) +0xCE0, incremented once per
//    tick at 0x1198FDA; +0xCE4 its game time (n * 1/60 a frame, 0x1198E0A).
//  - The bodies: hknpWorld at *(System+0x68)+0x20 (vtable 0x18BC9C0; slot 16 0xDAB170: bodies [+0x18], 0xB0 each,
//    their number [+0x20]; slot 31 0xDABA20: motions [+0x158], 0x80 each). hknpBody: +0x30 position (float4), +0x50 id
//    (low 24 bits its index), +0x54 flags (bit 0 static, bit 1 added as dynamic, bit 2 keyframed, bit 3 active), +0x80
//    motion id. hknpMotion +0x60 linear velocity (w: its cap). docs/gravity-re.md §2.0.2 / §2.0.3.
//  - The owner: BodyToObject (0x108260, the body's property key 0) -> SceneObject*, logged as its vtable's RVA.
//  - The mission's first tick: the first sample where MissionMgr (*(EDF+0x20B28A8)) answers "running" (0x1F68B0).
//    The scene tick is 0 there; ticks are counted from the first tick after it with a live non-static body (the
//    scene loaded), so two runs line up by scene tick, not by wall clock. Once the mission is left the probe arms
//    again, so a launch with the autopilot's `cycle` samples every mission it enters (one PHYS start .. end each).
// Rows (appended to the autopilot log by its own thread, one write per loop):
//   PHYS start pass=K tick=T world=... bodies=N live=L   the first sampled tick of the K-th mission (T: its scene tick)
//   PHYS H t=REL n=.. dyn=.. kf=.. act=.. hash=.. a=TICK game=SECONDS ms=WALL gaps=G   every kHashEvery ticks: an FNV-1a
//        hash of every live non-static body's index, motion bits, position and velocity bytes (bit for bit)
//   PHYS B t=REL i=INDEX f=FLAGS o=OWNER_VT p=x,y,z v=x,y,z   every kRowEvery ticks, each live non-static body
//   PHYS end ...                                     after the last tick, or when the mission stops running
// gaps counts the calls whose tick was not the last one + 1 (0: the wrapper saw every tick).
// Nothing about the game's own run changes, but the per-tick walk costs game-thread time (frame rate, not results:
// the step is fixed).
#include "physics_probe.h"
#include "edf/memory.h"
#include "edf/patch.h"
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace autopilot {
namespace {
constexpr std::size_t kSystem=0x20B2958,kTick=0xCE0,kGameTime=0xCE4,kWorldHolder=0x68,kWorldBase=0x20,kWorldVtable=0x18BC9C0;
constexpr std::size_t kBodies=0x18,kBodyCount=0x20,kMotions=0x158,kBodyStride=0xB0,kMotionStride=0x80;
constexpr std::size_t kPos=0x30,kId=0x50,kFlags=0x54,kMotionId=0x80,kLinVel=0x60;
constexpr std::size_t kBodyToObject=0x108260,kMissionMgr=0x20B28A8,kMissionRunning=0x1F68B0;
constexpr std::size_t kWorldStepSlot=0x17E91E0,kWorldStep=0x705A60;
constexpr unsigned kHashEvery=10,kRowEvery=60;
constexpr std::uint32_t kMostBodies=0x40000;
constexpr int kMostRows=8192;          // bodies written per row sample
constexpr std::size_t kRowBytes=192;

// sub rsp,48h; lea rdx,[rip+...] ("Application::OnWorldStepBegin")
const unsigned char kWorldStepSig[]={0x48,0x83,0xEC,0x48,0x48,0x8D,0x15,0x35,0x3B,0x0E,0x01};
// mov rcx,[rcx+40h]; test rcx,rcx; jne +3; xor al,al; ret
const unsigned char kMissionRunningSig[]={0x48,0x8B,0x49,0x40,0x48,0x85,0xC9,0x75,0x03,0x32,0xC0,0xC3};
// sub rsp,28h; mov rax,[rip+...]; mov edx,ecx; lea rcx,[rax+10h]; call 0x11AD2E0
const unsigned char kBodyToObjectSig[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x05,0xED,0xA6,0xFA,0x01,0x8B,0xD1,0x48,0x8D,0x48,0x10,
                                        0xE8,0x6A,0x50,0x0A,0x01};
// mov eax,edx; and eax,0FFFFFFh; imul rax,rax,0B0h; add rax,[rcx+18h]; ret
const unsigned char kBodyAtSig[]={0x8B,0xC2,0x25,0xFF,0xFF,0xFF,0x00,0x48,0x69,0xC0,0xB0,0x00,0x00,0x00,0x48,0x03,0x41,0x18,0xC3};
// movsxd rax,edx; shl rax,7; add rax,[rcx+158h]; ret
const unsigned char kMotionAtSig[]={0x48,0x63,0xC2,0x48,0xC1,0xE0,0x07,0x48,0x03,0x81,0x58,0x01,0x00,0x00,0xC3};

using WorldStepFn=void(__fastcall*)(void*);
using MissionRunningFn=bool(__fastcall*)(void*);
using BodyToObjectFn=void*(__fastcall*)(std::uint32_t);

unsigned char* image=nullptr;
LogFn say=nullptr;
wchar_t outPath[MAX_PATH]{};
WorldStepFn realWorldStep=nullptr;
unsigned maxTicks=6000;

// The game thread's.
enum Phase { kWaiting, kSampling, kDone };
Phase phase=kWaiting;
std::uint32_t baseTick=0,lastTick=0;
unsigned gaps=0;
unsigned passes=0;   // missions sampled so far (several in one launch with the autopilot's `cycle`)
ULONGLONG baseMs=0;
char rowBuf[kMostRows*kRowBytes];

// Handed to the autopilot's thread.
SRWLOCK lock=SRWLOCK_INIT;
std::string pending;

template<class T> T At(const unsigned char* p,std::size_t off) noexcept { T v{}; std::memcpy(&v,p+off,sizeof(T)); return v; }

void Queue(const char* text,std::size_t size) noexcept {
    AcquireSRWLockExclusive(&lock);
    try { pending.append(text,size); } catch(...) {}
    ReleaseSRWLockExclusive(&lock);
}
void QueueLine(const char* format,...) noexcept {
    char line[512];
    va_list args;va_start(args,format);
    const int n=std::vsnprintf(line,sizeof(line)-2,format,args);
    va_end(args);
    if(n<=0)return;
    std::size_t len=static_cast<std::size_t>(n)<sizeof(line)-2 ? static_cast<std::size_t>(n) : sizeof(line)-3;
    line[len++]='\r';line[len++]='\n';
    Queue(line,len);
}

struct Pass {
    std::uint32_t live=0,dynamic=0,keyframed=0,active=0;
    std::uint64_t hash=0xCBF29CE484222325ull;
    int rows=0;
    std::size_t bytes=0;
    bool ok=false;
};

void Mix(std::uint64_t& h,const void* data,std::size_t size) noexcept {
    const auto* p=static_cast<const unsigned char*>(data);
    for(std::size_t i=0;i<size;++i){h^=p[i];h*=0x100000001B3ull;}
}

// One walk of the body array: the hash always, the rows when `rows` (rel the tick they are for). SEH only, no
// objects with destructors here.
void Walk(const unsigned char* world,bool rows,unsigned rel,Pass& out) noexcept {
    __try {
        const unsigned char* const bodies=At<const unsigned char*>(world,kBodies);
        const std::uint32_t count=At<std::uint32_t>(world,kBodyCount);
        const unsigned char* const motions=At<const unsigned char*>(world,kMotions);
        if(!bodies || !motions || count>kMostBodies)return;
        const auto toObject=reinterpret_cast<BodyToObjectFn>(image+kBodyToObject);
        for(std::uint32_t i=0;i<count;++i) {
            const unsigned char* const b=bodies+std::size_t{i}*kBodyStride;
            const std::uint32_t flags=At<std::uint32_t>(b,kFlags);
            if(!(flags&3u) || (flags&1u))continue;                      // not live, or static
            const std::uint32_t id=At<std::uint32_t>(b,kId);
            if((id&0xFFFFFFu)!=i)continue;
            const std::uint32_t motionId=At<std::uint32_t>(b,kMotionId);
            if(motionId>=kMostBodies)continue;
            const unsigned char* const m=motions+std::size_t{motionId}*kMotionStride;
            float p[3],v[3];
            std::memcpy(p,b+kPos,sizeof p);
            std::memcpy(v,m+kLinVel,sizeof v);
            const std::uint32_t kind=flags&0xFu;
            ++out.live;
            if(flags&4u)++out.keyframed; else ++out.dynamic;
            if(flags&8u)++out.active;
            Mix(out.hash,&i,sizeof i);
            Mix(out.hash,&kind,sizeof kind);
            Mix(out.hash,p,sizeof p);
            Mix(out.hash,v,sizeof v);
            if(rows && out.rows<kMostRows) {
                const unsigned char* const owner=static_cast<const unsigned char*>(toObject(id));
                const unsigned char* const vt=owner ? At<const unsigned char*>(owner,0) : nullptr;
                const unsigned long long ovt=vt>=image && vt<image+0x22CE000 ? static_cast<unsigned long long>(vt-image) : 0ull;
                const int n=std::snprintf(rowBuf+out.bytes,kRowBytes,
                    "PHYS B t=%u i=%u f=%X o=%llX p=%.9g,%.9g,%.9g v=%.9g,%.9g,%.9g\r\n",rel,i,kind,ovt,
                    static_cast<double>(p[0]),static_cast<double>(p[1]),static_cast<double>(p[2]),
                    static_cast<double>(v[0]),static_cast<double>(v[1]),static_cast<double>(v[2]));
                if(n>0 && static_cast<std::size_t>(n)<kRowBytes){out.bytes+=static_cast<std::size_t>(n);++out.rows;}
            }
        }
        out.ok=true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { out.ok=false; }
}

const unsigned char* World(const unsigned char* system) noexcept {
    const unsigned char* const holder=At<const unsigned char*>(system,kWorldHolder);
    if(!holder || !edf::Readable(holder+kWorldBase,0x160))return nullptr;
    const unsigned char* const w=holder+kWorldBase;
    return At<const void*>(w,0)==image+kWorldVtable ? w : nullptr;
}

void Sample() noexcept {
    const unsigned char* const system=At<const unsigned char*>(image,kSystem);
    void* const mgr=At<void*>(image,kMissionMgr);
    if(!system || !edf::Readable(system+kTick,8))return;
    const bool running=mgr && reinterpret_cast<MissionRunningFn>(image+kMissionRunning)(mgr);
    if(phase==kDone) {
        // Armed again once the mission is left: the next mission of the same launch is sampled the same way.
        if(!running){phase=kWaiting;gaps=0;}
        return;
    }
    const std::uint32_t tick=At<std::uint32_t>(system,kTick);
    if(phase==kWaiting) {
        // The scene tick is 0 at the mission's start, but the world fills only once the mission's scene is loaded
        // (RM015 in the background: about 900 ticks later, and the loading's pace is the wall clock's). Ticks are
        // counted from the first one with a live non-static body.
        if(!running)return;
        const unsigned char* const world=World(system);
        if(!world)return;
        Pass first;
        Walk(world,false,0,first);
        if(!first.ok || !first.live)return;
        phase=kSampling;baseTick=tick;lastTick=tick-1;baseMs=GetTickCount64();
        QueueLine("PHYS start pass=%u tick=%u world=%p bodies=%u live=%u game=%.4f",++passes,tick,world,At<std::uint32_t>(world,kBodyCount),
                  first.live,static_cast<double>(At<float>(system,kGameTime)));
    }
    if(tick!=lastTick+1)++gaps;
    lastTick=tick;
    const unsigned rel=tick-baseTick;
    const unsigned char* const world=World(system);
    if(!running || !world || rel>maxTicks) {
        phase=kDone;
        QueueLine("PHYS end t=%u running=%d world=%d gaps=%u",rel,running ? 1 : 0,world ? 1 : 0,gaps);
        return;
    }
    const bool rows=rel%kRowEvery==0;
    Pass pass;
    Walk(world,rows,rel,pass);
    if(!pass.ok){QueueLine("PHYS H t=%u unreadable",rel);return;}
    if(rows && pass.bytes)Queue(rowBuf,pass.bytes);
    if(rel%kHashEvery==0)
        QueueLine("PHYS H t=%u n=%u dyn=%u kf=%u act=%u hash=%016llX a=%u game=%.4f ms=%llu gaps=%u",rel,pass.live,pass.dynamic,
                  pass.keyframed,pass.active,static_cast<unsigned long long>(pass.hash),tick,
                  static_cast<double>(At<float>(system,kGameTime)),GetTickCount64()-baseMs,gaps);
}

void __fastcall WorldStep(void* app) {
    realWorldStep(app);
    Sample();
}
}  // namespace

bool InstallPhysicsProbe(unsigned char* img,LogFn log,const wchar_t* out,unsigned ticks) noexcept {
    if(realWorldStep)return false;
    image=img;say=log;
    if(ticks)maxTicks=ticks;
    wcscpy_s(outPath,out);
    if(!edf::Matches(image,kWorldStep,kWorldStepSig,sizeof(kWorldStepSig)) ||
       !edf::Matches(image,kMissionRunning,kMissionRunningSig,sizeof(kMissionRunningSig)) ||
       !edf::Matches(image,kBodyToObject,kBodyToObjectSig,sizeof(kBodyToObjectSig)) ||
       !edf::Matches(image,0xDAB170,kBodyAtSig,sizeof(kBodyAtSig)) ||
       !edf::Matches(image,0xDABA20,kMotionAtSig,sizeof(kMotionAtSig)) ||
       At<const void*>(image,kWorldVtable+16*8)!=image+0xDAB170 || At<const void*>(image,kWorldVtable+31*8)!=image+0xDABA20)
        return false;
    void* next=nullptr;
    if(!edf::ChainVtableSlot(reinterpret_cast<void**>(image+kWorldStepSlot),reinterpret_cast<void*>(&WorldStep),&next))
        return false;
    realWorldStep=reinterpret_cast<WorldStepFn>(next);
    return true;
}

void PhysicsProbeTick() noexcept {
    if(!say)return;
    std::string out;
    AcquireSRWLockExclusive(&lock);
    out.swap(pending);
    ReleaseSRWLockExclusive(&lock);
    if(out.empty())return;
    const HANDLE f=CreateFileW(outPath,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,0,nullptr);
    if(f==INVALID_HANDLE_VALUE)return;
    DWORD wrote=0;
    WriteFile(f,out.data(),static_cast<DWORD>(out.size()),&wrote,nullptr);
    CloseHandle(f);
}
}  // namespace autopilot
