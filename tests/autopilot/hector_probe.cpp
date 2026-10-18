// The EDF4.1 Hector walk probe (test only, part of EDF6Autopilot): asked by the autopilot command "probe hector".
//
// Slot 5 of EDF6_Berserker_Large's vtable (0x17ADA78, the frame 0x34F280; EDF6VehicleCrew's edf41ai.cpp hooks the call
// of the base update inside it, so the two do not meet) is swapped for a wrapper that calls the original and then,
// for an object whose SGO has the key edf41_hector (tools/make_edf41_objects.py writes it into every Hector variant),
// reads (all EDF.dll TimeDateStamp 0x678CCB46, WalkingEnemyBase 0x53D560 / 0x540710 / 0x5405B0):
//   +0x90 position; +0x1444 yaw (0x53D756), +0x1454 the yaw it turns to (0x53D5F2), +0x1320 this frame's yaw change
//   (0x53D682); +0x1290 the walk vector MoveTo wrote (0x33E3B0); +0x1430 the eased velocity in units/s (0x53DBE2);
//   +0x12A4 FaceTarget's clamped angle (0x33E99B; the stand state turns only past +0xFA4 = 0.1, 0x540659); +0x12DC the
//   locomotion mode; the main CAS player (+0xBD0 players [+0x60], index +0xFD0, 0x160 each): state +0x104 (its name
//   from the state table [+0xE0], 0x24-byte entries, 0x116C031), rate +0x100 (0x116DF20), clip clock +0xE8;
//   the target 0x30C0F0 (when +0x4A0) and its horizontal distance.
// Rows (every kEvery frames per Hector, plus at once on a main state change and on a jump of the position by more than
// kJump units in one frame) are collected in the frame and written by the autopilot's loop. Nothing about the game's
// own run changes.
#include "hector_probe.h"
#include "edf/memory.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

namespace autopilot {
namespace {
constexpr std::size_t kVtable=0x17ADA78,kFrame=0x34F280,kFrameSlot=5;
constexpr std::size_t kChildByName=0xD7480,kGetTarget=0x30C0F0;
constexpr std::size_t kSelfCtrl=0x30,kTree=0xA8,kPos=0x90,kYaw=0x1444,kWantYaw=0x1454,kYawStep=0x1320,kWalk=0x1290,kVelocity=0x1430,
                      kTurnClamp=0x12A4,kMode=0x12DC,kCas=0xBD0,kMainPlayer=0xFD0,kHasTarget=0x4A0,kBattle=0x17A0;
constexpr std::size_t kPlayers=0x60,kPlayerSize=0x160,kPlayerState=0x104,kPlayerRate=0x100,kPlayerClock=0xE8,
                      kPlayerStates=0xE0,kStateEntry=0x24;
constexpr unsigned kEvery=4;            // frames between two sample rows of one Hector
constexpr float kJump=2.0f;             // units in one frame (120 u/s; 4.1 walks 0.1 a frame)
constexpr std::size_t kMaxRows=400000;  // the run's cap

struct Handle { void* tree; std::uint32_t index; std::uint32_t pad; };
using ChildByNameFn=Handle*(__fastcall*)(const Handle*,Handle*,const wchar_t*);
using GetTargetFn=void*(__fastcall*)(void*);
using FrameFn=std::uint64_t(__fastcall*)(void*,void*,void*,void*);

unsigned char* image=nullptr;
LogFn say=nullptr;
FrameFn realFrame=nullptr;
SRWLOCK lock=SRWLOCK_INIT;
std::vector<std::string> pending;
std::size_t rows=0;

struct Seen {
    const void* self=nullptr;
    const void* control=nullptr;       // a respawn at the same address is another instance
    int id=0;
    unsigned frames=0;
    float pos[3]{};
    int state=-2;
};
std::vector<Seen> seen;               // every address, including runs with more than 64 Hectors
int nextId=0;

template<class T> T At(const unsigned char* p,std::size_t off) noexcept { T v{}; std::memcpy(&v,p+off,sizeof(T)); return v; }

bool IsHector(void* self) noexcept {
    const Handle root{static_cast<char*>(self)+kTree,0,0};
    Handle out{nullptr,0xFFFFFFFFu,0};
    reinterpret_cast<ChildByNameFn>(image+kChildByName)(&root,&out,L"edf41_hector");
    return out.tree!=nullptr;
}

Seen& SeenOf(const void* self,const void* control) noexcept {
    for(Seen& e:seen)
        if(e.self==self) {
            if(e.control!=control) {
                e=Seen{};
                e.self=self; e.control=control; e.id=nextId++;
            }
            return e;
        }
    seen.emplace_back();
    Seen& e=seen.back();
    e.self=self; e.control=control; e.id=nextId++;
    return e;
}

// The main player's state name (or "?"), its index, rate and clip clock.
void MainState(const unsigned char* s,wchar_t* name,std::size_t cap,int& state,float& rate,float& clock) noexcept {
    name[0]=L'?'; name[1]=L'\0';
    state=-1; rate=0.0f; clock=0.0f;
    const unsigned char* players=At<const unsigned char*>(s,kCas+kPlayers);
    const std::uint32_t index=At<std::uint32_t>(s,kMainPlayer);
    if(!players || index>64)return;
    const unsigned char* p=players+static_cast<std::size_t>(index)*kPlayerSize;
    if(!edf::Readable(p,kPlayerSize))return;
    state=At<int>(p,kPlayerState);
    rate=At<float>(p,kPlayerRate);
    clock=At<float>(p,kPlayerClock);
    const unsigned char* table=At<const unsigned char*>(p,kPlayerStates);
    if(state<0 || !table || !edf::Readable(table,16))return;
    const unsigned char* entries=table+At<int>(table,8);
    const unsigned char* entry=entries+static_cast<std::size_t>(state)*kStateEntry;
    if(!edf::Readable(entry,4))return;
    const wchar_t* text=reinterpret_cast<const wchar_t*>(entry+At<int>(entry,0));
    if(!edf::Readable(text,2*cap))return;
    std::size_t i=0;
    for(;i+1<cap && text[i];++i)name[i]=text[i];
    name[i]=L'\0';
}

void Row(const char* kind,int id,unsigned frame,const unsigned char* s,float step) noexcept {
    wchar_t state[40];
    int index=0; float rate=0.0f,clock=0.0f;
    MainState(s,state,40,index,rate,clock);
    float dist=-1.0f,tx=0.0f,tz=0.0f;
    if(At<std::uint8_t>(s,kHasTarget)) {
        const unsigned char* t=static_cast<const unsigned char*>(reinterpret_cast<GetTargetFn>(image+kGetTarget)(
            const_cast<unsigned char*>(s)));
        if(t && edf::Readable(t+kPos,12)) {
            tx=At<float>(t,kPos); tz=At<float>(t,kPos+8);
            const float dx=tx-At<float>(s,kPos),dz=tz-At<float>(s,kPos+8);
            dist=std::sqrt(dx*dx+dz*dz);
        }
    }
    char line[480];
    std::snprintf(line,sizeof(line),
        "HEC %s f=%u id=%d p=%.2f,%.2f,%.2f step=%.3f yaw=%.4f want=%.4f dy=%.5f walk=%.3f,%.3f,%.3f vel=%.2f,%.2f,%.2f "
        "tc=%.4f mode=%d battle=%d st=%d:%ls r=%.3f t=%.1f d=%.1f tgt=%.1f,%.1f",kind,frame,id,
        At<float>(s,kPos),At<float>(s,kPos+4),At<float>(s,kPos+8),step,At<float>(s,kYaw),At<float>(s,kWantYaw),
        At<float>(s,kYawStep),At<float>(s,kWalk),At<float>(s,kWalk+4),At<float>(s,kWalk+8),At<float>(s,kVelocity),
        At<float>(s,kVelocity+4),At<float>(s,kVelocity+8),At<float>(s,kTurnClamp),At<int>(s,kMode),
        At<std::uint8_t>(s,kBattle),index,state,rate,clock,dist,tx,tz);
    AcquireSRWLockExclusive(&lock);
    if(rows<kMaxRows) { pending.emplace_back(line); ++rows; }
    ReleaseSRWLockExclusive(&lock);
}

std::uint64_t __fastcall Frame(void* self,void* a,void* b,void* c) noexcept {
    const std::uint64_t result=realFrame(self,a,b,c);
    __try {
        if(!IsHector(self))return result;
        const auto s=static_cast<const unsigned char*>(self);
        const void* control=At<const void*>(s,kSelfCtrl);
        if(!control)return result;
        Seen& e=SeenOf(self,control);
        const int id=e.id;
        const float p[3]={At<float>(s,kPos),At<float>(s,kPos+4),At<float>(s,kPos+8)};
        const float dx=p[0]-e.pos[0],dy=p[1]-e.pos[1],dz=p[2]-e.pos[2];
        const float step=e.frames ? std::sqrt(dx*dx+dy*dy+dz*dz) : 0.0f;
        const unsigned char* players=At<const unsigned char*>(s,kCas+kPlayers);
        const std::uint32_t main=At<std::uint32_t>(s,kMainPlayer);
        const int state=players && main<=64 ? At<int>(players+static_cast<std::size_t>(main)*kPlayerSize,kPlayerState) : -1;
        if(e.frames && step>kJump)Row("JUMP",id,e.frames,s,step);
        else if(e.state!=-2 && state!=e.state)Row("STATE",id,e.frames,s,step);
        else if(e.frames%kEvery==0)Row("S",id,e.frames,s,step);
        e.state=state;
        e.pos[0]=p[0]; e.pos[1]=p[1]; e.pos[2]=p[2];
        ++e.frames;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}

bool Swap(std::size_t slot,std::size_t expect,void* with) noexcept {
    auto** const entry=reinterpret_cast<void**>(image+kVtable+slot*8);
    if(*entry!=image+expect)return false;
    DWORD old=0;
    if(!VirtualProtect(entry,8,PAGE_READWRITE,&old))return false;
    // Publish only after InstallHectorProbe initialized the original and the image. The interlocked exchange is the
    // publication barrier, and also leaves another hook alone if it replaced the slot during VirtualProtect.
    const bool installed=InterlockedCompareExchangePointer(entry,with,image+expect)==image+expect;
    VirtualProtect(entry,8,old,&old);
    return installed;
}
}  // namespace

bool InstallHectorProbe(unsigned char* img,LogFn log) noexcept {
    image=img; say=log;
    realFrame=reinterpret_cast<FrameFn>(img+kFrame);
    return Swap(kFrameSlot,kFrame,reinterpret_cast<void*>(&Frame));
}

void HectorProbeTick() noexcept {
    if(!say)return;
    std::vector<std::string> out;
    AcquireSRWLockExclusive(&lock);
    out.swap(pending);
    ReleaseSRWLockExclusive(&lock);
    for(const std::string& line:out)say("%s",line.c_str());
}
}  // namespace autopilot
