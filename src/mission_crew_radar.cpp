#include "mission_crew_radar.h"
#include "crew.h"
#include <windows.h>
#include <atomic>
#include <cstring>

namespace crew {
namespace {
using namespace crewradar;
// 0x82B220, the stock one-team walk (mission_crew_radar.h): the whole body, so the set layout, the row layout and the
// order of the walk this filter repeats are the ones the radar used.
constexpr unsigned kWalk=0x82B220;
constexpr unsigned char kWalkCode[]={
    0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x48,0xC7,0x42,0x18,0x00,0x00,0x00,0x00,0x48,0x8B,0xFA,0x49,0x63,
    0xC0,0x48,0x6B,0xD8,0x38,0x48,0x8B,0x05,0x35,0x77,0x88,0x01,0x48,0x8B,0x48,0x38,0x48,0x8B,0x1C,0x0B,0x48,0x8B,0x1B,
    0x80,0x7B,0x19,0x00,0x0F,0x85,0xA6,0x00,0x00,0x00,0x48,0x89,0x74,0x24,0x30,0x0F,0x1F,0x00,0x48,0x8B,0x47,0x18,0x48,
    0x8B,0x57,0x10,0x48,0x8B,0x73,0x20,0x48,0x3B,0xC2,0x72,0x14,0x48,0x8D,0x14,0x55,0x02,0x00,0x00,0x00,0x48,0x8B,0xCF,
    0xE8,0xCF,0x8A,0x91,0xFF,0x48,0x8B,0x47,0x18,0x48,0x8B,0xC8,0x48,0xC1,0xE1,0x04,0x48,0x03,0x4F,0x08,0x48,0xFF,0xC0,
    0x48,0x89,0x47,0x18,0x0F,0x28,0x86,0x90,0x00,0x00,0x00,0x66,0x0F,0x7F,0x01,0x48,0x8B,0x43,0x10,0x80,0x78,0x19,0x00,
    0x74,0x22,0x48,0x8B,0x43,0x08,0x80,0x78,0x19,0x00,0x75,0x13,0x48,0x3B,0x58,0x10,0x75,0x0D,0x48,0x8B,0xD8,0x48,0x8B,
    0x40,0x08,0x80,0x78,0x19,0x00,0x74,0xED,0x48,0x8B,0xD8,0xEB,0x21,0x48,0x8B,0x08,0x48,0x8B,0xD8,0x80,0x79,0x19,0x00,
    0x75,0x15,0x66,0x0F,0x1F,0x44,0x00,0x00,0x48,0x8B,0x01,0x48,0x8B,0xD9,0x48,0x8B,0xC8,0x80,0x78,0x19,0x00,0x74,0xF1,
    0x80,0x7B,0x19,0x00,0x0F,0x84,0x67,0xFF,0xFF,0xFF,0x48,0x8B,0x74,0x24,0x30,0x48,0x8B,0x5C,0x24,0x38,0x48,0x83,0xC4,
    0x20,0x5F,0xC3};
static_assert(sizeof(kWalkCode)==0xE9,"0x82B220..0x82B308");

std::atomic<bool> ready{false};
SRWLOCK lock=SRWLOCK_INIT;
const void* hidden[kMost]{};
unsigned hiddenCount=0;
std::atomic<unsigned> published{0};   // hiddenCount, readable without the lock (the radar's quick "nothing hidden")

template<class T> T Field(const void* p,std::size_t at) noexcept {T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+at,sizeof v);return v;}
bool Nil(const void* node) noexcept {return Field<unsigned char>(node,kIsNil)!=0;}
// The in-order successor, exactly as 0x82B2A2..0x82B2EF steps it.
const void* Next(const void* node) noexcept {
    const void* right=Field<const void*>(node,kRight);
    if(!Nil(right)) {
        const void* at=right;
        for(const void* left=Field<const void*>(at,kLeft);!Nil(left);left=Field<const void*>(at,kLeft))at=left;
        return at;
    }
    const void* parent=Field<const void*>(node,kParent);
    while(!Nil(parent) && node==Field<const void*>(parent,kRight)) {node=parent;parent=Field<const void*>(parent,kParent);}
    return parent;
}
bool HiddenLocked(const void* object) noexcept {
    for(unsigned i=0;i<hiddenCount;++i)if(hidden[i]==object)return true;
    return false;
}
void FilterLocked(const void* head,unsigned char* list) noexcept {
    const auto rows=Field<std::uint64_t>(list,kSize);
    auto* data=Field<unsigned char*>(list,kData);
    if(!rows || !data)return;
    // One row per node, in the set's order: a set changed since the walk (another count) is not matched row for row.
    std::uint64_t nodes=0;
    for(const void* n=Field<const void*>(head,kLeft);!Nil(n) && nodes<=rows;n=Next(n))++nodes;
    if(nodes!=rows)return;
    std::uint64_t kept=0,row=0;
    for(const void* n=Field<const void*>(head,kLeft);!Nil(n);n=Next(n),++row) {
        if(HiddenLocked(Field<const void*>(n,kValue)))continue;
        if(kept!=row)std::memmove(data+kept*kRow,data+row*kRow,kRow);
        ++kept;
    }
    if(kept!=rows)std::memcpy(list+kSize,&kept,sizeof kept);
}
}

bool InstallMissionCrewRadar() noexcept {
    if(ready.load(std::memory_order_acquire))return true;
    if(!image || !Matches(kWalk,kWalkCode,sizeof(kWalkCode))) {
        Log("Mission crew radar: the stock team walk 0x82B220 is not as expected; the crews keep their radar dots");
        return false;
    }
    ready.store(true,std::memory_order_release);
    return true;
}
void MissionCrewRadarPublish(const void* const* objects,unsigned count) noexcept {
    if(count>kMost)count=kMost;   // the registry holds no more (static_assert in mission_crew_support.cpp)
    AcquireSRWLockExclusive(&lock);
    for(unsigned i=0;i<count;++i)hidden[i]=objects[i];
    hiddenCount=count;published.store(count,std::memory_order_release);
    ReleaseSRWLockExclusive(&lock);
}
bool MissionCrewRadarHidden(const void* object) noexcept {
    if(!object || !published.load(std::memory_order_acquire))return false;
    AcquireSRWLockShared(&lock);
    const bool found=HiddenLocked(object);
    ReleaseSRWLockShared(&lock);
    return found;
}
void MissionCrewRadarFilterRows(const void* head,void* list) noexcept {
    if(!head || !list || !published.load(std::memory_order_acquire))return;
    AcquireSRWLockShared(&lock);
    __try {FilterLocked(head,static_cast<unsigned char*>(list));}
    __except(EXCEPTION_EXECUTE_HANDLER){}
    ReleaseSRWLockShared(&lock);
}
void MissionCrewRadarFilter(void* list,int team) noexcept {
    if(!ready.load(std::memory_order_acquire) || team<0 || !list || !published.load(std::memory_order_acquire))return;
    const void* head=nullptr;
    __try {
        // 0x82B23C..0x82B24B: [manager] +0x38, the team's 0x38-byte row, its first qword the set's head.
        const auto* manager=Field<const unsigned char*>(image,kTeamManager);
        if(!manager)return;
        const auto* teams=Field<const unsigned char*>(manager,kTeams);
        if(!teams)return;
        head=Field<const void*>(teams,static_cast<std::size_t>(team)*kTeamRow);
    } __except(EXCEPTION_EXECUTE_HANDLER){return;}
    MissionCrewRadarFilterRows(head,list);
}
}
