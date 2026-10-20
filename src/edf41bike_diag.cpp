// Debug=1 only: what the first minute of a Vehicle_Car's physics looks like (docs/edf41-bike-re.md 「悬空」). The 408
// came out of every call and the debug spawn hanging in the air (2026-10-19 / 10-21 user logs), and no log line said
// why. For each Car (the 408 and the stock V512 with the same class) this writes, at its first frames and then every
// ten seconds: the vehicle's position, its speed from the last sample, and its clearance over the map below; at a
// few of those samples also every ragdoll proxy (CarBase mode +0x9C, the mode before +0xA0, may-simulate +0xA4, in
// the world 0x17E720, hknp motion type 0x11B14A0 0 static / 1 keyframed / 2 dynamic, hknpBody flags +0x54 and its
// height) and every proxy constraint (its two proxies, added to the world 0x11C1220).
// Read only: the natives called are the game's own queries, each signature checked first; every read is under SEH.
#include "crew.h"
#include "edf41bike.h"
#include "edf41classes.h"
#include "memory.h"
#include <cmath>
#include <cstdint>
#include <cstring>

namespace crew {
namespace {
constexpr std::size_t kRagdollAt=0x1320;
constexpr std::size_t kRecords=0x78,kRecordCount=0x88,kRecordStride=0xC0,kConstraints=0x98,kConstraintCount=0xA8,kConstraintStride=0x20;
constexpr std::size_t kRecHandle=0x50,kRecParent=0x70,kRecMode=0x9C,kRecPrevMode=0xA0,kRecAble=0xA4,kRagDefault=0xC8;
constexpr unsigned kBodyByName=0x6EA670,kBodyPos=0x11B15B0,kMotionType=0x11B14A0,kInWorld=0x17E720,kConstraintAdded=0x11C1220;
const unsigned char kBodyByNameCode[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x57,0x41,0x56,0x48,0x83};
// 0x11B15B0 and 0x11B14A0 share their first 0x1D bytes (both look the hknpBody up); their tails differ.
const unsigned char kBodyPosCode[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x8B,0x91,0xF0,0x00,0x00,0x00,0x48,0x8B,0x48,
    0x58,0x48,0x8B,0x41,0x20,0x48,0x83,0xC1,0x20,0xFF,0x90,0x80,0x00,0x00,0x00,0x48,0x83,0xC0,0x30};
const unsigned char kMotionTypeCode[]={0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x8B,0x91,0xF0,0x00,0x00,0x00,0x48,0x8B,0x48,
    0x58,0x48,0x8B,0x41,0x20,0x48,0x83,0xC1,0x20,0xFF,0x90,0x80,0x00,0x00,0x00,0x8B,0x48,0x54,0xF6,0xC1,0x01};
const unsigned char kInWorldCode[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x48,0x8B,0xD9};
const unsigned char kConstraintAddedCode[]={0x48,0x8B,0x41,0x10,0x8B,0x51,0x18,0x48,0x8B,0x48,0x58,0x48,0x8B,0x41,0x20,0x48};
using ByNameFn=int(__fastcall*)(void*,const wchar_t*);
using PosFn=const float*(__fastcall*)(void*);
using IntFn=int(__fastcall*)(void*);
using BoolFn=bool(__fastcall*)(void*);
// The 408's eight SHKT proxies (pylib/edf41bike.py PROXIES); V512's are named the same way (RagDollProxys.<bone>).
const wchar_t* const kNames[]={L"body",L"handle",L"front_sus",L"front_tire",L"rear_sus",L"rear_tire",L"side_tire",L"gun",
    L"tireF_l",L"tireF_r",L"tireB_l",L"tireB_r",L"door_l",L"door_r"};
constexpr int kNameCount=static_cast<int>(sizeof(kNames)/sizeof(kNames[0]));

int known=0;   // 0 unchecked, 1 the natives are this build's, 2 not (the diagnostic off, said once)
bool Known() noexcept {
    if(known)return known==1;
    known=Matches(kBodyByName,kBodyByNameCode,sizeof(kBodyByNameCode)) && Matches(kBodyPos,kBodyPosCode,sizeof(kBodyPosCode)) &&
          Matches(kMotionType,kMotionTypeCode,sizeof(kMotionTypeCode)) && Matches(kInWorld,kInWorldCode,sizeof(kInWorldCode)) &&
          Matches(kConstraintAdded,kConstraintAddedCode,sizeof(kConstraintAddedCode)) ? 1 : 2;
    if(known==2)Log("EDF41BIKE DIAG off: the ragdoll queries are not this build's");
    return known==1;
}

struct Watch { const void* v=nullptr; unsigned frames=0; ULONGLONG lastAt=0; float last[3]{}; };
constexpr int kWatches=8;
Watch watches[kWatches]{};
Watch& WatchFor(const void* v,ULONGLONG now) noexcept {
    int free=0;
    for(int i=0;i<kWatches;++i) {
        // a slot not sampled for 3 s belongs to a vehicle gone (a new one at the same address starts over)
        if(watches[i].v==v && now-watches[i].lastAt<3000)return watches[i];
        if(watches[i].lastAt<watches[free].lastAt)free=i;
    }
    watches[free]=Watch{};watches[free].v=v;
    return watches[free];
}
// The frames sampled: the first ones (the fall should start at once), then about every ten seconds for two minutes.
bool Sampled(unsigned f) noexcept {
    return f<=3 || f==6 || f==10 || f==20 || f==40 || f==80 || f==160 || f==320 || (f%600==0 && f<=7200);
}
bool Detailed(unsigned f) noexcept { return f==1 || f==40 || f==600; }

const wchar_t* NameOf(void* rag,int index) noexcept {
    wchar_t full[48];
    for(int i=0;i<kNameCount;++i) {
        _snwprintf_s(full,_TRUNCATE,L"RagDollProxys.%ls",kNames[i]);
        const auto byName=reinterpret_cast<ByNameFn>(image+kBodyByName);
        if(byName(rag,full)==index || byName(rag,kNames[i])==index)return kNames[i];
    }
    return L"?";
}
int IndexOf(const unsigned char* records,std::uint64_t count,const unsigned char* record) noexcept {
    if(!record || record<records)return -1;
    const auto at=static_cast<std::uint64_t>(record-records);
    return at%kRecordStride==0 && at/kRecordStride<count ? static_cast<int>(at/kRecordStride) : -1;
}

void Records(const char* kind,unsigned char* v,unsigned f) noexcept {
    auto* rag=v+kRagdollAt;
    if(!Readable(rag,0xD0))return;
    auto* records=At<unsigned char*>(rag,kRecords);
    const auto count=At<std::uint64_t>(rag,kRecordCount);
    if(!records || count==0 || count>64 || !Readable(records,count*kRecordStride))return;
    Log("EDF41BIKE DIAG v=%p %s f=%u ragdoll: %llu proxies, default mode %d",v,kind,f,static_cast<unsigned long long>(count),
        At<std::int32_t>(rag,kRagDefault));
    for(std::uint64_t i=0;i<count;++i) {
        auto* r=records+i*kRecordStride;
        void* h=At<void*>(r,kRecHandle);
        int world=-1,motion=-1;unsigned flags=0;float y=NAN;
        if(h && Readable(static_cast<unsigned char*>(h)+0xF0,0x18)) {
            world=reinterpret_cast<BoolFn>(image+kInWorld)(h) ? 1 : 0;
            motion=reinterpret_cast<IntFn>(image+kMotionType)(h);
            const float* p=reinterpret_cast<PosFn>(image+kBodyPos)(h);
            if(p && Readable(p-12,0x40)) {   // hknpBody: +0x30 the translation, +0x54 its flags (8 = active)
                y=p[1];
                flags=At<std::uint32_t>(p,0x24);
            }
        }
        Log("EDF41BIKE DIAG v=%p %s f=%u proxy %llu %ls parent=%d mode=%d prev=%d able=%d world=%d motion=%d flags=%X y=%.3f",v,kind,f,
            static_cast<unsigned long long>(i),NameOf(rag,static_cast<int>(i)),IndexOf(records,count,At<unsigned char*>(r,kRecParent)),
            At<std::int32_t>(r,kRecMode),At<std::int32_t>(r,kRecPrevMode),r[kRecAble],world,motion,flags,y);
    }
    auto* cons=At<unsigned char*>(rag,kConstraints);
    const auto cn=At<std::uint64_t>(rag,kConstraintCount);
    if(!cons || cn>64 || !Readable(cons,cn*kConstraintStride))return;
    for(std::uint64_t i=0;i<cn;++i) {
        auto* c=cons+i*kConstraintStride;
        void* handle=At<void*>(c,0);
        const int added=handle && Readable(handle,0x20) ? (reinterpret_cast<BoolFn>(image+kConstraintAdded)(handle) ? 1 : 0) : -1;
        Log("EDF41BIKE DIAG v=%p %s f=%u constraint %llu: proxy %d - proxy %d added=%d",v,kind,f,static_cast<unsigned long long>(i),
            IndexOf(records,count,At<unsigned char*>(c,0x10)),IndexOf(records,count,At<unsigned char*>(c,0x18)),added);
    }
}

void Sample(unsigned char* v) noexcept {
    if(!Known() || !Readable(v,kRagdollAt+0xD0))return;
    const ULONGLONG now=GetTickCount64();
    Watch& w=WatchFor(v,now);
    const unsigned f=++w.frames;
    const ULONGLONG before=w.lastAt;
    float last[3];std::memcpy(last,w.last,sizeof last);
    const float* p=reinterpret_cast<const float*>(v+kPosition);
    std::memcpy(w.last,p,sizeof w.last);w.lastAt=now;
    char riders[9]{};   // each seat: - empty, d dummy, P this machine's player, o another (crew.cpp's VEH letters)
    bool occupied=false;
    const unsigned seats=SeatCount(v);
    for(unsigned i=0;i<seats && i<8;++i) {
        riders[i]="-dPo"[static_cast<int>(SeatRider(SeatAt(v,i)))];
        occupied=occupied || riders[i]!='-';
    }
    if(!Sampled(f) && !(occupied && f%60==0 && f<=7200))return;   // ridden: once a second as well (the drive), first two minutes
    const char* kind=IsEdf41Bike(v) ? "408" : "car";
    const float dt=f>1 && now>before ? static_cast<float>(now-before)/1000.0f : 0.0f;
    float vel[3]{};
    if(dt>0.0f)for(int i=0;i<3;++i)vel[i]=(p[i]-last[i])/dt;
    float hit[3]{};
    const float from[3]={p[0],p[1]+3.0f,p[2]},to[3]={p[0],p[1]-60.0f,p[2]};
    const bool ground=edf41c::GroundHit(from,to,hit);
    // the driver's input as the seat holds it (heli.cpp §4: +0x2C0 / +0x2C4 left stick, +0x2E0 / +0x2E4 the triggers)
    const unsigned char* driver=seats ? SeatAt(v,0) : nullptr;
    float in[4]{};
    if(driver && Readable(driver+0x2C0,0x28)) {
        in[0]=At<float>(driver,0x2C0);in[1]=At<float>(driver,0x2C4);in[2]=At<float>(driver,0x2E0);in[3]=At<float>(driver,0x2E4);
    }
    Log("EDF41BIKE DIAG v=%p %s f=%u pos=(%.3f,%.3f,%.3f) vel=(%.2f,%.2f,%.2f) ground=%.3f clearance=%.3f seats=[%s] drive=(%.2f,%.2f,%.2f,%.2f) dead=%d",
        v,kind,f,p[0],p[1],p[2],vel[0],vel[1],vel[2],ground ? hit[1] : NAN,ground ? p[1]-hit[1] : NAN,riders,in[0],in[1],in[2],in[3],v[kDead]);
    if(Detailed(f))Records(kind,v,f);
}
}  // namespace

void Edf41BikeDiagFrame(unsigned char* v) noexcept {
    __try { Sample(v); }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        static int said=0;
        if(said++<4)Log("EDF41BIKE DIAG v=%p: unreadable native state, sample skipped",v);
    }
}
}  // namespace crew
