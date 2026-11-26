// The Balam's impact marker (docs/feature-barga-aim.md; the pure part barga_aim.h): while this machine's player drives a
// 501_FortressRobo (V515 Retro Balam / V605 Barga Cannon), from the vehicle's own state each frame:
//  - each fist (LB left, RB right): a ring as wide as its hit sphere where the attack its button would start now
//    reaches, bright when one may start, dim (its opening jab) when none may;
//  - while hit groups are lit: where a sphere touches the ground or meets an enemy's lock point;
//  - V605, during ground_charge / attack_cannon and its burst: where the rounds still to come land, and their blast.
// Read on the game thread from the vehicle's input (crew.cpp InputHook, the 501's 0x60AEC0), published with the step it
// was read in (step_fresh.h) and drawn by hud.cpp BargaAimMarks. Local state only: host and guests alike read their own
// copy, nothing is sent.
#include "crew.h"
#include "barga_aim.h"
#include "heli.h"
#include "layout.h"
#include "memory.h"
#include "step_fresh.h"
#include "vhud.h"
#include "edf/weapon.h"
#include <cmath>
#include <cwchar>

namespace crew {
namespace {
constexpr unsigned kVtBarga=0x17D98C8;        // 501_FortressRobo (kClasses)
constexpr unsigned kVtSwingShoot=0x17E6320;   // Weapon_VehicleSwingShoot (V605's cannon)
// The combo (0x60F570 / 0x6058C0 / 0x605C90): the attack list, the one playing, whether a next may start, the flags.
constexpr std::size_t kAttacks=0x2390,kAttackCount=0x23A0,kAccept=0x23A8,kCurrent=0x23AC,kFlags=0x23BC;
constexpr std::size_t kAttackStride=0x40,kAttackPrev=0x20,kAttackPrevCount=0x30;
// The hit groups (0x6C8A50): a map (name -> group) in the melee component at +0x2350; its head at +0x10. A node: _Left
// +0, _Parent +8, _Right +0x10, _Isnil +0x19, its name (wstring) +0x20, the group in it: points +0x48 (count +0x58,
// 0x30 each), lit +0x138.
constexpr std::size_t kMelee=0x2350,kMeleeHead=0x10;
constexpr std::size_t kNodeLeft=0,kNodeParent=8,kNodeRight=0x10,kNodeNil=0x19,kNodeName=0x20,kNodeSize=0x140;
constexpr std::size_t kGroupPoints=0x48,kGroupPointCount=0x58,kGroupLit=0x138,kPointStride=0x30;
// A point: its record (+0; the offset at record + its +0xC, the radius +0x10), its bone (+8; world rows at +0xB0), where
// it was last tested (+0x10).
constexpr std::size_t kPointRecord=0,kPointBone=8,kPointPrev=0x10,kRecordOffset=0xC,kRecordRadius=0x10;
constexpr int kMostNodes=64,kMostPoints=64;
// The cannon (0x6B4100 / 0x6B4080 / 0x6B4190): the burst's count (FireBurstCount) and rounds left, the direction frozen
// at its start and the source it is copied from (*(+0x120) +0x80), the sweep's begin / end / pow, AmmoExplosion.
constexpr std::size_t kBurstCount=0x370,kBurstLeft=0xE18,kFrozen=0x1670,kSweepBegin=0x1680,kSweepEnd=0x1690,kSweepPow=0x16A0;
constexpr std::size_t kAimSource=0x120,kAimSourceDir=0x80,kExplosion=0x8B0;
constexpr std::size_t kMuzzleDir=0xD0;   // a muzzle's direction the sweep writes (w 1; mode +0xE0 = 2)
// The burst's state machine (bargaaim::SweepState): its handler (0x6B3FE0 stores it at the machine +0x18; the machine at
// +0x1650), and what 0x691080 reads for "the trigger held": +0x13D, or +0x13A for a burst of one not set to +0x143.
constexpr std::size_t kSweepHandler=0x1668;
constexpr unsigned kSweepWaiting=0x6B4100,kSweepRunning=0x6B4080;
constexpr std::size_t kHeld=0x13D,kHeldSingle=0x13A,kHeldBurstAlways=0x143;
constexpr std::uint64_t kMostMuzzles=8;
constexpr float kGroundBelow=60.0f;      // m under a sphere a ground ray looks
constexpr ULONGLONG kLogMs=500;
constexpr std::size_t kBoneEnd=edf::kBoneRows+64;   // a bone's world rows

bargaaim::Readout latest{};
stepfresh::Step latestStep=0;
const void* latestVehicle=nullptr;
const void* latestCannon=nullptr;   // the SwingShoot whose rounds `latest` marks (latest.cannon), for vhud.cpp

// The SwingShoot weapon on the seat, or nullptr.
const unsigned char* SwingShoot(const unsigned char* seat) noexcept {
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(!count || count>16 || !Readable(holders,count*8))return nullptr;
    for(std::uint64_t i=0;i<count;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<const unsigned char*>(holders[i],kHolderWeapon);
        if(Readable(w,kSweepPow+4) && At<const void*>(w,0)==image+kVtSwingShoot)return w;
    }
    return nullptr;
}

// The variant whose attack list the vehicle holds (bargaaim::MatchVariant), -1 none.
int ReadVariant(const unsigned char* v,int* count) noexcept {
    const auto entries=At<const unsigned char*>(v,kAttacks);
    const auto n=At<std::uint64_t>(v,kAttackCount);
    *count=static_cast<int>(n>64 ? 64 : n);
    if(!n || n>32 || !Readable(entries,n*kAttackStride))return -1;
    int prevCount[32],prev[32][2]={};
    for(std::uint64_t i=0;i<n;++i) {
        const auto e=entries+i*kAttackStride;
        const auto pc=At<std::uint64_t>(e,kAttackPrevCount);
        if(pc>2)return -1;   // the tables never follow more than two
        prevCount[i]=static_cast<int>(pc);
        const auto list=At<const std::uint64_t*>(e,kAttackPrev);
        if(pc && !Readable(list,pc*8))return -1;
        for(std::uint64_t k=0;k<pc;++k)prev[i][k]=static_cast<int>(list[k]);
    }
    return bargaaim::MatchVariant(static_cast<int>(n),prevCount,prev);
}

// The name of a group node (its key wstring), into `out` (at most `most` - 1 characters).
void NodeName(const unsigned char* node,wchar_t* out,std::size_t most) noexcept {
    out[0]=L'\0';
    const auto size=At<std::uint64_t>(node,kNodeName+0x10),cap=At<std::uint64_t>(node,kNodeName+0x18);
    const wchar_t* text=cap>=8 ? At<const wchar_t*>(node,kNodeName) : reinterpret_cast<const wchar_t*>(node+kNodeName);
    if(size>=most || !Readable(text,size*2))return;
    for(std::uint64_t i=0;i<size;++i)out[i]=text[i];
    out[size]=L'\0';
}
int LimbOf(const wchar_t* name) noexcept {
    for(const auto& g:bargaaim::kHitGroups)if(std::wcscmp(g.name,name)==0)return g.limb;
    return -1;
}
// The in-order successor of a map node (MSVC's _Tree): the head (its _Isnil set) after the last.
const unsigned char* NextNode(const unsigned char* n) noexcept {
    auto r=At<const unsigned char*>(n,kNodeRight);
    if(!Readable(r,kNodeSize))return nullptr;
    if(!At<unsigned char>(r,kNodeNil)) {
        for(int guard=0;guard<kMostNodes;++guard) {
            const auto l=At<const unsigned char*>(r,kNodeLeft);
            if(!Readable(l,kNodeSize))return nullptr;
            if(At<unsigned char>(l,kNodeNil))return r;
            r=l;
        }
        return nullptr;
    }
    auto p=At<const unsigned char*>(n,kNodeParent);
    for(int guard=0;guard<kMostNodes;++guard) {
        if(!Readable(p,kNodeSize))return nullptr;
        if(At<unsigned char>(p,kNodeNil) || At<const unsigned char*>(p,kNodeRight)!=n)return p;
        n=p;p=At<const unsigned char*>(p,kNodeParent);
    }
    return nullptr;
}

struct LitLog { wchar_t names[160]; };
// The lit groups' spheres into `r` (and their names, for the log).
void ReadSpheres(const unsigned char* v,bargaaim::Readout& r,LitLog* log) noexcept {
    r.spheres=0;
    if(log)log->names[0]=L'\0';
    const auto head=At<const unsigned char*>(v,kMelee+kMeleeHead);
    if(!Readable(head,kNodeSize))return;
    const unsigned char* node=At<const unsigned char*>(head,kNodeLeft);
    for(int guard=0;guard<kMostNodes && node && node!=head;++guard,node=NextNode(node)) {
        if(!Readable(node,kNodeSize) || At<unsigned char>(node,kNodeNil))return;
        if(!At<unsigned char>(node,kGroupLit))continue;
        wchar_t name[32];
        NodeName(node,name,32);
        if(log && std::wcslen(log->names)+std::wcslen(name)+2<160){
            if(log->names[0])wcscat_s(log->names,L",");
            wcscat_s(log->names,name);
        }
        const int limb=LimbOf(name);
        const auto points=At<const unsigned char*>(node,kGroupPoints);
        const auto count=At<std::uint64_t>(node,kGroupPointCount);
        if(!count || count>kMostPoints || !Readable(points,count*kPointStride))continue;
        for(std::uint64_t i=0;i<count && r.spheres<bargaaim::kMostSpheres;++i) {
            const auto p=points+i*kPointStride;
            const auto record=At<const unsigned char*>(p,kPointRecord);
            const auto bone=At<const unsigned char*>(p,kPointBone);
            if(!Readable(record,kRecordRadius+4) || !Readable(bone,kBoneEnd))continue;
            const auto offset=record+At<std::int32_t>(record,kRecordOffset);
            if(!Readable(offset,16))continue;
            auto& s=r.sphere[r.spheres++];
            s=bargaaim::Sphere{};
            bargaaim::SphereCentre(reinterpret_cast<const float*>(offset),reinterpret_cast<const float*>(bone+edf::kBoneRows),s.c);
            const auto prev=reinterpret_cast<const float*>(p+kPointPrev);
            for(int k=0;k<3;++k)s.prev[k]=prev[k];
            s.r=At<float>(record,kRecordRadius);
            s.limb=limb;
        }
    }
}

// The ground under each sphere and the enemies its sweep (prev -> now) passes within its radius.
struct EnemyHits { bargaaim::Readout* r; };
void EnemyHit(void* ctx,const void*,const float* aim) {
    auto& r=*static_cast<EnemyHits*>(ctx)->r;
    for(int i=0;i<r.spheres;++i) {
        auto& s=r.sphere[i];
        if(s.target)continue;
        const float dx=aim[0]-s.c[0],dy=aim[1]-s.c[1],dz=aim[2]-s.c[2];
        if(dx*dx+dy*dy+dz*dz<s.r*s.r || NearLine(s.prev,s.c,aim,s.r)) {
            s.target=true;
            for(int k=0;k<3;++k)s.targetAt[k]=aim[k];
        }
    }
}
void SphereContacts(const unsigned char* v,bargaaim::Readout& r) noexcept {
    if(MapRayReady()) {
        for(int i=0;i<r.spheres;++i) {
            auto& s=r.sphere[i];
            const float to[3]={s.c[0],s.c[1]-kGroundBelow,s.c[2]};
            float hit[3];
            const float d=MapRay(s.c,to,hit);
            if(d<0.0f)continue;
            s.ground=true;s.touch=d<=s.r;
            for(int k=0;k<3;++k)s.groundAt[k]=hit[k];
        }
    }
    EnemyHits ctx{&r};
    VisitEnemies(v,&EnemyHit,&ctx);
}

// The fists' marks: each side's attack that may start now, else its opening one.
void ReadFists(const unsigned char* v,const bargaaim::Variant& variant,unsigned flags,bargaaim::Readout& r) noexcept {
    const auto rows=reinterpret_cast<const float*>(v+kMatrix);
    for(int side=0;side<2;++side) {
        auto& a=r.fist[side];
        a=bargaaim::FistMark{};
        const auto pick=bargaaim::PickFist(variant,side,r.current,r.accept,flags);
        a.attack=pick.next>=0 ? pick.next : pick.base;
        if(a.attack<0)continue;
        a.on=true;a.next=pick.next>=0;
        const auto& attack=variant.attacks[a.attack];
        bargaaim::ModelToWorld(rows,attack.reach,a.at);
        a.r=attack.radius;
    }
}

bargaaim::SweepState SweepStateOf(const unsigned char* w) noexcept {
    const auto handler=At<const void*>(w,kSweepHandler);
    return handler==image+kSweepRunning ? bargaaim::SweepState::sweeping
         : handler==image+kSweepWaiting ? bargaaim::SweepState::waiting : bargaaim::SweepState::other;
}
bool TriggerHeld(const unsigned char* w) noexcept {   // 0x691080
    if(At<std::int32_t>(w,kBurstCount)<2 && !At<unsigned char>(w,kHeldBurstAlways))return At<unsigned char>(w,kHeldSingle)!=0;
    return At<unsigned char>(w,kHeld)!=0;
}

// The cannon's rounds to come. `frozen` the direction the burst sweeps from: the weapon's own copy while the machine
// sweeps (0x6B4080, a held trigger's next burst included), its source (what 0x6B4100 will copy) while it waits.
bool ReadCannon(const unsigned char* w,bargaaim::Readout& r,float* frozen,float* pos,float* nativeDir) noexcept {
    const int count=At<std::int32_t>(w,kBurstCount),left=At<std::int32_t>(w,kBurstLeft);
    const auto state=SweepStateOf(w);
    if(state==bargaaim::SweepState::other)return false;   // a handler the RE does not know: no marks
    r.bursting=bargaaim::FromFrozen(state);
    const float* source=reinterpret_cast<const float*>(w+kFrozen);
    if(!r.bursting) {
        const auto aim=At<const unsigned char*>(w,kAimSource);
        if(!Readable(aim,kAimSourceDir+16))return false;
        source=reinterpret_cast<const float*>(aim+kAimSourceDir);
    }
    for(int k=0;k<3;++k)frozen[k]=source[k];
    bargaaim::Sweep sweep{};
    for(int k=0;k<3;++k){sweep.begin[k]=At<float>(w,kSweepBegin+4*k);sweep.end[k]=At<float>(w,kSweepEnd+4*k);}
    sweep.pow=At<float>(w,kSweepPow);
    RoundModel model{};
    float muzzleDir[3];
    if(!ReadRound(w,&model) || !edf::MeanMuzzle(w,kMostMuzzles,pos,muzzleDir))return false;
    const auto muzzle=edf::MuzzleAt(w,0);
    if(muzzle && Readable(muzzle,kMuzzleDir+12))for(int k=0;k<3;++k)nativeDir[k]=At<float>(muzzle,kMuzzleDir+4*k);
    r.blast=At<float>(w,kExplosion);
    r.toCome=bargaaim::RoundsToCome(state,left,count,TriggerHeld(w));
    int lefts[bargaaim::kMostRounds];
    r.rounds=bargaaim::SampleRounds(r.toCome,bargaaim::kMostRounds,lefts);
    const float reach=model.speed*static_cast<float>(model.alive>0 ? model.alive : 60)*1.05f;
    for(int i=0;i<r.rounds;++i) {
        float dir[3],sec=0.0f;
        bargaaim::SweepDir(frozen,bargaaim::SweepAngle(sweep,bargaaim::BurstT(lefts[i],count)),dir);
        r.landed[i]=RoundLands(w,model,pos,dir,reach,r.at[i],&sec);
    }
    return true;
}

void HullLocal(const float* rows,const float* p,float* out) noexcept {
    const float d[3]={p[0]-rows[12],p[1]-rows[13],p[2]-rows[14]};
    for(int k=0;k<3;++k)out[k]=d[0]*rows[4*k]+d[1]*rows[4*k+1]+d[2]*rows[4*k+2];
}
}  // namespace

void BargaAimFrame(unsigned char* v) noexcept {
    const auto clear=[&] {
        if(latestVehicle==v){latest=bargaaim::Readout{};latestStep=0;latestVehicle=nullptr;latestCannon=nullptr;}
    };
    if(!Cfg().enabled || !Cfg().bargaAimMarker || At<const void*>(v,0)!=image+kVtBarga || v[kDead] || SeatCount(v)==0) {
        clear();return;
    }
    const auto seat=SeatAt(v,0);
    if(SeatRider(seat)!=Rider::player || At<const void*>(seat,kSeatRider)!=PlayerHuman()){clear();return;}
    bargaaim::Readout r{};
    int listed=0;
    r.variant=ReadVariant(v,&listed);
    static ULONGLONG logged=0;
    const ULONGLONG now=GetTickCount64();
    const bool log=Cfg().debug && now-logged>=kLogMs;
    if(log)logged=now;
    if(r.variant<0) {
        if(log)Log("BARGA v=%p variant none: %d attacks the tables do not know (marker off)",v,listed);
        clear();return;
    }
    const auto& variant=bargaaim::kVariants[r.variant];
    r.current=static_cast<int>(At<std::int32_t>(v,kCurrent));
    r.accept=At<unsigned char>(v,kAccept)!=0;
    const unsigned flags=At<std::uint32_t>(v,kFlags)&1u;
    LitLog lit{};
    ReadSpheres(v,r,log ? &lit : nullptr);
    if(r.spheres)SphereContacts(v,r);
    ReadFists(v,variant,flags,r);   // mid-attack too: the ring is where the combo's next blow of that button lands
    float frozen[3]={},pos[3]={},nativeDir[3]={};
    const unsigned char* w=nullptr;
    if(variant.cannon>=0 && (w=SwingShoot(seat))!=nullptr) {
        const bool charging=r.current==variant.cannon || r.current==variant.charge;
        if((charging || SweepStateOf(w)==bargaaim::SweepState::sweeping) && ReadCannon(w,r,frozen,pos,nativeDir))r.cannon=true;
    }
    latest=r;latestStep=stepfresh::Now();latestVehicle=v;latestCannon=r.cannon ? w : nullptr;
    if(!log)return;
    const auto rows=reinterpret_cast<const float*>(v+kMatrix);
    // The group names are the SGO's Japanese: UTF-8 for the log. A wide %ls the CRT's "C" locale cannot convert makes
    // vsnprintf fail and the whole line come out empty (the 2026-11-27 run: an empty line before each "BARGA sphere").
    char litNames[3*_countof(lit.names)]{};
    if(lit.names[0] && !WideCharToMultiByte(CP_UTF8,0,lit.names,-1,litNames,sizeof(litNames),nullptr,nullptr))litNames[0]='\0';
    Log("BARGA v=%p %s current=%d accept=%d flags=%u lit=[%s] spheres=%d fists L=%d%s R=%d%s fwd=(%.2f,%.2f,%.2f)",v,variant.file,
        r.current,r.accept,flags,litNames,r.spheres,r.fist[bargaaim::kLeft].attack,r.fist[bargaaim::kLeft].next ? "*" : "",
        r.fist[bargaaim::kRight].attack,r.fist[bargaaim::kRight].next ? "*" : "",rows[8],rows[9],rows[10]);
    for(int i=0;i<r.spheres && i<4;++i) {
        const auto& s=r.sphere[i];
        float local[3];
        HullLocal(rows,s.c,local);
        Log("BARGA sphere %d limb=%d r=%.2f c=(%.1f,%.1f,%.1f) hull=(%.2f,%.2f,%.2f) moved=%.2f ground=%d touch=%d target=%d",i,s.limb,s.r,
            s.c[0],s.c[1],s.c[2],local[0],local[1],local[2],
            std::sqrt((s.c[0]-s.prev[0])*(s.c[0]-s.prev[0])+(s.c[1]-s.prev[1])*(s.c[1]-s.prev[1])+(s.c[2]-s.prev[2])*(s.c[2]-s.prev[2])),
            s.ground,s.touch,s.target);
    }
    if(w) {
        const int count=At<std::int32_t>(w,kBurstCount),left=At<std::int32_t>(w,kBurstLeft);
        float predicted[3]={};
        bargaaim::Sweep sweep{};
        for(int k=0;k<3;++k){sweep.begin[k]=At<float>(w,kSweepBegin+4*k);sweep.end[k]=At<float>(w,kSweepEnd+4*k);}
        sweep.pow=At<float>(w,kSweepPow);
        // Sweeping: the round the weapon fired last (one more left than now: its muzzle holds that one's direction);
        // waiting: the burst's first.
        const auto state=SweepStateOf(w);
        const float t=bargaaim::BurstT(r.bursting ? left+1 : count,count);
        bargaaim::SweepDir(frozen,bargaaim::SweepAngle(sweep,t),predicted);
        const auto src=At<const unsigned char*>(w,kAimSource);
        const auto handler=At<const unsigned char*>(w,kSweepHandler);
        Log("BARGA cannon on=%d state=%s(+%llX) held=%d(+13D %d +13A %d) burst=%d left=%d count=%d toCome=%d t=%.3f frozen=(%.3f,%.3f,%.3f) src=%p%s native=(%.3f,%.3f,%.3f) "
            "predicted=(%.3f,%.3f,%.3f) muzzle=(%.1f,%.1f,%.1f) blast=%.1f rounds=%d first=(%.1f,%.1f,%.1f)%s last=(%.1f,%.1f,%.1f)%s",
            r.cannon,state==bargaaim::SweepState::sweeping ? "sweeping" : state==bargaaim::SweepState::waiting ? "waiting" : "other",
            static_cast<unsigned long long>(handler ? handler-image : 0),TriggerHeld(w),At<unsigned char>(w,kHeld),
            At<unsigned char>(w,kHeldSingle),r.bursting,left,count,r.toCome,t,frozen[0],frozen[1],frozen[2],src,src==v ? "(vehicle)" : "",nativeDir[0],nativeDir[1],nativeDir[2],
            predicted[0],predicted[1],predicted[2],pos[0],pos[1],pos[2],r.blast,r.rounds,
            r.rounds ? r.at[0][0] : 0.0f,r.rounds ? r.at[0][1] : 0.0f,r.rounds ? r.at[0][2] : 0.0f,r.rounds && r.landed[0] ? "" : "(air)",
            r.rounds ? r.at[r.rounds-1][0] : 0.0f,r.rounds ? r.at[r.rounds-1][1] : 0.0f,r.rounds ? r.at[r.rounds-1][2] : 0.0f,
            r.rounds && r.landed[r.rounds-1] ? "" : "(air)");
    }
}

bool PlayerBargaAim(bargaaim::Readout* out) noexcept {
    if(!Cfg().enabled || !stepfresh::Fresh(latestStep))return false;
    *out=latest;
    return true;
}

bool BargaOwnsCannon(const void* weapon) noexcept {
    return weapon && weapon==latestCannon && Cfg().enabled && stepfresh::Fresh(latestStep);
}

void ResetBargaAim() noexcept {
    latest=bargaaim::Readout{};latestStep=0;latestVehicle=nullptr;latestCannon=nullptr;
}
}  // namespace crew
