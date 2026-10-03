// The submarine carrier (docs/subcarrier-re.md): the 潜水母艦 of the missions (Pandora / Epimetheus /
// Siren: the mission object EV603_MARINE, a FarEventObject with no HP, no collision and no weapons of its
// own) made a friendly NPC vehicle. Like the jets (jet.cpp) it is a Vehicle506_Helicopter body from a
// derived SGO (tools/make_sub.py EDF6VC_SUB_CARRIER.SGO, testrange/gen.py 'edf6tr_sub_carrier_mission'):
// the stock HP (+0x2F4 / +0x2F8), the crash and wreck, the seat weapons, and the hull's rigid box, which
// is what bullets hit and what soldiers stand on. Its model is EV603_MARINE at the missions' size (1664 m),
// told apart by its speed gain k (veh+0x162C) = kSubMark (the jets use 7001-7010).
// The plugin drives it, as jet.cpp drives a jet, in two stages a frame:
//  - input (slot 55, from HeliFrame, NPC pilot only): it sits surfaced (hull bottom kClear over the highest
//    ground under it), holds its post and follows the player at a ship's pace once they are kLeash away,
//    turns its bow (the turrets' and the missile bay's facing) onto the nearest enemy, fires the turret guns
//    (0x2020) when one is on the bow and the homing missiles (0x2021, the bay's チラン爆雷 / missiles) once
//    the game has locked a target; empty weapons are reloaded aboard (WEAPONTEXT: the missiles are loaded
//    inside the carrier) after kReloadMs;
//  - physics (506 slot 57, chained after jet.cpp's): its linear and angular velocity.
// Its HP is shown as the game's follower gauge (HudPlayer_FollowerDurability 0x8040E0 draws one over every
// follower of the player, 0x804300): the plugin calls that drawer once more with a stand-in owner whose
// follower list holds a stand-in object per carrier (only +0x90 position, +0x2F4 / +0x2F8 HP and +0x550,
// an empty follower list, are read).
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "crew.h"
#include "memory.h"
#include <cmath>
#include <cwchar>

namespace crew {
namespace {
constexpr unsigned kHeli506=0x17DB238;
constexpr std::size_t kSlotPhysics=57;
constexpr std::size_t kSpeedGain=0x162C,kBody=0x1650;
constexpr unsigned kSetLinearVelocity=0x11B18F0,kSetAngularVelocity=0x11B1760,kDelete=0x118A1B0;
constexpr std::size_t kInLateral=0x1540,kInForward=0x1548,kInYaw=0x1550;
constexpr std::size_t kFireGun=0x2020,kFireMissile=0x2021;
constexpr std::size_t kHpMax=0x2F4,kHp=0x2F8;
constexpr std::size_t kSeatWeapons=0xC8,kSeatWeaponCount=0xD8,kHolderWeapon=0x10;
constexpr std::size_t kWeaponLockon=0x6B0,kWeaponAmmo=0xBE8;
constexpr std::size_t kWeaponLockAngle=0x6C0,kWeaponLockRange=0x6D0,kWeaponLockSpeed=0x790,kWeaponLocked=0xC68;
constexpr std::int32_t kHoming=1;
constexpr std::size_t kAreaInset=0xE00;   // jet.cpp: the move-area clamp's inset; kNoInset = no clamp
constexpr float kNoInset=-1.0e6f;
constexpr std::size_t kObjFlags=0x18;
constexpr unsigned char kObjDeleted=4;
constexpr unsigned kPreload=0x7A3780,kCreateObject=0x11945E0,kSetTeam=0x54EE70,kInitParamVtable=0x1762068;
constexpr std::size_t kPreloadMgr=0x20B29A8,kObjectMgr=0x20B2958;
constexpr std::int32_t kTeamFriend=2;
constexpr unsigned kFindPart=0x6EA4B0;
constexpr std::size_t kParts=0x1320,kBodyPart=0x1530;
// The gauge (docs/subcarrier-re.md §4): the follower HUD's draw (vtable 0x17F6C08 slot 3) calls the
// drawer at kGaugeCall: (hud, view-projection, owner, r9, 5th) -> a gauge per object in owner+0x550's list.
constexpr unsigned kGaugeHud=0x17F6C08,kGaugeDraw=0x8040E0,kGaugeCall=0x8042AD,kGaugeFn=0x804300;
constexpr std::size_t kFollowers=0x550,kProxySize=0x560;

constexpr float kSubMark=7101.0f;            // testrange/gen.py JETS['edf6tr_sub_carrier_mission'].mark
const wchar_t* const kSubSgo=L"app:/object/edf6vc_sub_carrier.sgo";
// What SubLaunch needs in Mods (tools/make_sub.py): the SGO, its model, the turret guns.
const wchar_t* const kSubFiles[]={L"\\Mods\\OBJECT\\EDF6VC_SUB_CARRIER.SGO",L"\\Mods\\OBJECT\\EDF6VC_SUB.MRAB",
                                  L"\\Mods\\WEAPON\\EDF6VC_JET_GUN_L.SGO",L"\\Mods\\WEAPON\\EDF6VC_JET_GUN_R.SGO"};
constexpr int kMaxSubs=3;                     // M123: three carriers attack at once (BE151_157)

// The hull (the SGO's box, the model at its own size): its bottom kHullBottom under the body origin,
// kHalfLength fore and aft; the tower top kTop over it; the turrets' guns kGunHeight over it.
constexpr float kHullBottom=166.58f,kHalfLength=790.0f,kHalfWidth=116.0f,kTop=366.0f,kGunHeight=241.0f;
// Driving: metres, m/s, m/s^2, rad/s.
constexpr float kClear=0.6f;                 // hull bottom over the highest ground under it
// A player on its deck is up to 830 m from its post: it sets off only past the hull.
constexpr float kLeash=1500.0f,kStop=1000.0f;  // it sets off after a player this far from its post, stops this near
constexpr float kCruise=25.0f,kAccel=3.0f,kClimb=8.0f,kSink=4.0f,kClimbAccel=6.0f,kPosGain=0.2f;
constexpr float kTurnRate=0.05f,kTurnGain=0.8f,kRollGain=1.5f,kMaxPitch=0.05f;   // 3 deg/s, 3 deg
// Combat.
constexpr float kRange=2000.0f;              // it engages enemies this far from it
constexpr float kGunRange=580.0f,kGunCone=0.07f;   // the guns' reach (gen.JET_GUN_REACH 600), 4 deg
constexpr float kMissileRange=2000.0f,kMissileMin=60.0f;
constexpr float kLockMargin=1.15f,kLockAngle=1.2f,kLockSpeed=2.0f;
constexpr ULONGLONG kMissileMs=2500,kReloadMs=12000,kStaleMs=1500,kGaugeMs=1000;
constexpr float kPi=3.14159265f;

struct Sub {
    unsigned char* vehicle;
    const void* ctrl;
    ULONGLONG seen,bornAt,missileAt,logAt;
    ULONGLONG gaugeTick;          // GetTickCount64 of the last frame (the gauge may be drawn on another thread)
    LARGE_INTEGER last;
    float post[3];
    float lin[3],ang[3];
    bool moving,ready,launched;
    std::int32_t full[4];         // each seat weapon's ammo when first seen
    ULONGLONG emptyAt[4];
    float target[3];
    bool hasTarget;
};
Sub subs[kMaxSubs]{};
// The gauge's stand-in objects (see the top): one per carrier, its position and HP copied every frame.
alignas(16) unsigned char proxies[kMaxSubs][kProxySize]{};
struct Node { Node* next; Node* prev; void* object; };   // the game's list node: next, prev, value at +0x10
Node noFollowers{};

bool spawnOk=false,physicsOk=false,gaugeOk=false,bodyPartOk=false;
bool preloaded=false,broken=false;

struct alignas(16) InitParam { const void* vtable; unsigned char rest[0x28]; };
using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
using CreateObjectFn=unsigned char*(*)(void*,const float*,const wchar_t*,InitParam*);
using SetTeamFn=void(*)(void*,std::int32_t,bool);
using RideAiFn=void(*)(void*,bool);
using DeleteFn=void(*)(void*);
using SetVecFn=void(*)(void*,const float*);
using PhysicsFn=void(__fastcall*)(void*);
using FindPartFn=std::int32_t(__fastcall*)(void*,const wchar_t*);
using GaugeFn=void(__fastcall*)(void*,void*,void*,void*,void*);
PhysicsFn nextPhysics=nullptr;

float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
float Len(const float* a) noexcept { return std::sqrt(Dot(a,a)); }
float Clamp(float v,float lo,float hi) noexcept { return v<lo ? lo : v>hi ? hi : v; }
void Cross(const float* a,const float* b,float* out) noexcept {
    const float c[3]={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
    std::memcpy(out,c,12);
}
bool Normalize(float* a) noexcept {
    const float l=Len(a);
    if(!std::isfinite(l) || l<1e-4f)return false;
    a[0]/=l;a[1]/=l;a[2]/=l;
    return true;
}
// `cur` moved toward `want` by at most `step`.
float Approach(float cur,float want,float step) noexcept { return cur+Clamp(want-cur,-step,step); }

const void* SelfCtrl(const unsigned char* v) noexcept { return At<const void*>(v,kSelfCtrl); }

Sub* FindSub(const unsigned char* v,ULONGLONG ms) noexcept {
    for(auto& s:subs)
        if(s.vehicle==v && s.ctrl==SelfCtrl(v) && ms-s.seen<=kStaleMs)return &s;
    return nullptr;
}
// A free entry (empty, or not driven for kStaleMs); every stale entry of `v` cleared. nullptr: kMaxSubs live.
Sub* FreeSub(const unsigned char* v,ULONGLONG ms) noexcept {
    Sub* free=nullptr;
    for(auto& s:subs) {
        if(s.vehicle==v)s=Sub{};
        if(!free && (!s.vehicle || ms-s.seen>kStaleMs))free=&s;
    }
    return free;
}
int LiveSubs(ULONGLONG ms) noexcept {
    int n=0;
    for(const auto& s:subs)n+=s.vehicle && ms-s.seen<=kStaleMs ? 1 : 0;
    return n;
}

bool FilesThere() noexcept {
    wchar_t dir[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,dir,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(dir,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    for(const auto file:kSubFiles) {
        wchar_t path[MAX_PATH];
        if(wcscpy_s(path,dir)!=0 || wcscat_s(path,file)!=0)return false;
        if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)return false;
    }
    return true;
}

// Metres of the highest ground (terrain, buildings) under the hull's footprint at `pos` along `nose`, or
// false with none seen.
bool GroundUnder(const float* pos,const float* nose,float* ground) noexcept {
    const float side[3]={nose[2],0.0f,-nose[0]};
    const float along[5]={0.0f,kHalfLength,-kHalfLength,0.0f,0.0f},across[5]={0.0f,0.0f,0.0f,kHalfWidth,-kHalfWidth};
    bool any=false;
    for(int i=0;i<5;++i) {
        const float x=pos[0]+nose[0]*along[i]+side[0]*across[i],z=pos[2]+nose[2]*along[i]+side[2]*across[i];
        const float top[3]={x,pos[1]+kTop,z},bottom[3]={x,pos[1]-kHullBottom-900.0f,z};
        float hit[3];
        if(MapRay(top,bottom,hit)<0.0f)continue;
        if(!any || hit[1]>*ground)*ground=hit[1];
        any=true;
    }
    return any;
}

struct Nearest { float from[3]; float best; float aim[3]; bool found; };
void SeeEnemy(void* ctx,const void*,const float* aim) noexcept {
    auto& n=*static_cast<Nearest*>(ctx);
    const float d[3]={aim[0]-n.from[0],aim[1]-n.from[1],aim[2]-n.from[2]};
    const float dist=Len(d);
    if(!std::isfinite(dist) || dist>=n.best)return;
    n.best=dist;std::memcpy(n.aim,aim,12);n.found=true;
}

// The seat weapons: their homing ones lock out to kMissileRange (wider and faster than stock), an empty
// one is refilled to what it held at first kReloadMs after it ran dry. Out: gun ammo, missiles, locks.
void Arm(Sub& s,unsigned char* v,ULONGLONG ms,std::int32_t* guns,std::int32_t* missiles,std::int32_t* locked) noexcept {
    *guns=*missiles=*locked=0;
    if(SeatCount(v)==0)return;
    const auto seat=SeatAt(v,0);
    const auto holders=At<unsigned char* const*>(seat,kSeatWeapons);
    const auto count=At<std::uint64_t>(seat,kSeatWeaponCount);
    if(count>8 || !Readable(holders,count*8))return;
    for(std::uint64_t i=0;i<count && i<4;++i) {
        if(!Readable(holders[i],kHolderWeapon+8))continue;
        const auto w=At<unsigned char*>(holders[i],kHolderWeapon);
        if(!Readable(w,kWeaponLocked+8,true))continue;
        const bool homing=At<std::int32_t>(w,kWeaponLockon)==kHoming;
        if(i==3 || (!homing && i>=2))continue;        // the fuel tank (v_fuel01) after the missile
        std::int32_t ammo=At<std::int32_t>(w,kWeaponAmmo);
        if(ammo>s.full[i])s.full[i]=ammo;
        if(ammo>0)s.emptyAt[i]=0;
        else if(!s.emptyAt[i])s.emptyAt[i]=ms;
        else if(ms-s.emptyAt[i]>=kReloadMs && s.full[i]>0) {
            ammo=s.full[i];Put<std::int32_t>(w,kWeaponAmmo,ammo);s.emptyAt[i]=0;
            if(cfg.debug)Log("SUB v=%p weapon %llu reloaded aboard: %d",v,static_cast<unsigned long long>(i),ammo);
        }
        if(!homing){*guns+=ammo>0 ? ammo : 0;continue;}
        *missiles+=ammo>0 ? ammo : 0;
        const auto l=At<std::uint64_t>(w,kWeaponLocked);
        *locked+=l<64 ? static_cast<std::int32_t>(l) : 0;
        auto& range=*reinterpret_cast<float*>(w+kWeaponLockRange);
        if(range<kMissileRange*kLockMargin)range=kMissileRange*kLockMargin;
        for(int k=0;k<2;++k) {
            auto& a=*reinterpret_cast<float*>(w+kWeaponLockAngle+4*k);
            if(a<kLockAngle)a=kLockAngle;
        }
        auto& speed=*reinterpret_cast<float*>(w+kWeaponLockSpeed);
        if(speed<kLockSpeed)speed=kLockSpeed;
    }
}

// Its post follows the player at a ship's pace once they are kLeash from it, until kStop.
void Follow(Sub& s,float dt) noexcept {
    if(!player.at || GetTickCount64()-player.at>5000)return;
    const float d[3]={player.pos[0]-s.post[0],0.0f,player.pos[2]-s.post[2]};
    const float dist=Len(d);
    if(dist>kLeash)s.moving=true;
    if(dist<kStop)s.moving=false;
    if(!s.moving || dist<1.0f)return;
    const float step=kCruise*dt;
    s.post[0]+=d[0]/dist*step;s.post[2]+=d[2]/dist*step;
}

// The velocity toward its post (level, at most kCruise), holding the hull kClear over the ground.
void Drive(Sub& s,const float* pos,const float* nose,float dt) noexcept {
    float want[3]={(s.post[0]-pos[0])*kPosGain,0.0f,(s.post[2]-pos[2])*kPosGain};
    const float speed=Len(want);
    if(speed>kCruise){want[0]*=kCruise/speed;want[2]*=kCruise/speed;}
    s.lin[0]=Approach(s.lin[0],want[0],kAccel*dt);
    s.lin[2]=Approach(s.lin[2],want[2],kAccel*dt);
    float ground=0.0f;
    const float wantY=GroundUnder(pos,nose,&ground) ? Clamp((ground+kHullBottom+kClear-pos[1])*1.0f,-kSink,kClimb) : 0.0f;
    s.lin[1]=Approach(s.lin[1],wantY,kClimbAccel*dt);
}

// The spin that turns the bow onto `want` (at most kTurnRate) and keeps the deck level across.
void Steer(Sub& s,const float* m,const float* want) noexcept {
    float nose[3]={m[8],m[9],m[10]},right[3]={m[0],m[1],m[2]};
    if(!Normalize(nose) || !Normalize(right)){s.ang[0]=s.ang[1]=s.ang[2]=0.0f;return;}
    float axis[3];
    Cross(nose,want,axis);
    const float sinA=Len(axis),angle=std::atan2(sinA,Dot(nose,want));
    float rate=angle*kTurnGain;
    if(rate>kTurnRate)rate=kTurnRate;
    if(sinA>1e-4f) {
        for(int i=0;i<3;++i)s.ang[i]=axis[i]/sinA*rate;
    } else if(Dot(nose,want)<0.0f) {
        s.ang[0]=0.0f;s.ang[1]=kTurnRate;s.ang[2]=0.0f;   // dead astern: turn about up
    } else {
        s.ang[0]=s.ang[1]=s.ang[2]=0.0f;
    }
    // Roll: the right side down (right.y < 0) is undone by turning about the bow.
    for(int i=0;i<3;++i)s.ang[i]-=nose[i]*right[1]*kRollGain;
}

// Where the bow wants to point: the target (from the turrets, pitch within kMaxPitch), else the way it
// moves, else as it is (level).
void Heading(const Sub& s,const float* pos,const float* m,float* want) noexcept {
    if(s.hasTarget) {
        float d[3]={s.target[0]-pos[0],s.target[1]-(pos[1]+kGunHeight),s.target[2]-pos[2]};
        const float flat=std::sqrt(d[0]*d[0]+d[2]*d[2]);
        if(flat>1.0f) {
            const float pitch=Clamp(std::atan2(d[1],flat),-kMaxPitch,kMaxPitch);
            want[0]=d[0]/flat*std::cos(pitch);want[1]=std::sin(pitch);want[2]=d[2]/flat*std::cos(pitch);
            return;
        }
    }
    float flat[3]={s.lin[0],0.0f,s.lin[2]};
    if(s.moving && Len(flat)>2.0f && Normalize(flat)){std::memcpy(want,flat,12);return;}
    float nose[3]={m[8],0.0f,m[10]};
    if(!Normalize(nose)){nose[0]=0.0f;nose[2]=1.0f;}
    std::memcpy(want,nose,12);
}

void Fire(Sub& s,unsigned char* v,const float* pos,const float* m,ULONGLONG ms) noexcept {
    std::int32_t guns=0,missiles=0,locked=0;
    Arm(s,v,ms,&guns,&missiles,&locked);
    bool gun=false,missile=false;
    if(s.hasTarget && cfg.heliFire) {
        const float up[3]={m[4],m[5],m[6]};
        const float muzzle[3]={pos[0]+up[0]*kGunHeight,pos[1]+up[1]*kGunHeight,pos[2]+up[2]*kGunHeight};
        float nose[3]={m[8],m[9],m[10]};
        if(!Normalize(nose)){nose[0]=0.0f;nose[1]=0.0f;nose[2]=1.0f;}
        const float d[3]={s.target[0]-muzzle[0],s.target[1]-muzzle[1],s.target[2]-muzzle[2]};
        const float dist=Len(d);
        const float off=dist>1.0f ? std::acos(Clamp(Dot(d,nose)/dist,-1.0f,1.0f)) : 0.0f;
        const float path[3]={muzzle[0]+nose[0]*kGunRange,muzzle[1]+nose[1]*kGunRange,muzzle[2]+nose[2]*kGunRange};
        gun=guns>0 && dist<kGunRange && off<kGunCone && !FriendInLine(muzzle,path,v);
        missile=missiles>0 && locked>0 && dist>kMissileMin && dist<kMissileRange && ms-s.missileAt>kMissileMs &&
                !FriendInLine(muzzle,s.target,v);
        if(missile) {
            s.missileAt=ms;
            if(cfg.debug)Log("SUB v=%p missiles: %.0f m, %d locked, %d left",v,dist,locked,missiles);
        }
    }
    v[kFireGun]=gun;v[kFireMissile]=missile;
}

// The gauge's stand-in for `s`: over the tower, its HP.
void Proxy(int i,const unsigned char* v,const float* pos,const float* m) noexcept {
    unsigned char* p=proxies[i];
    Put<void*>(p,kFollowers,&noFollowers);
    const float at[4]={pos[0]+m[4]*kTop,pos[1]+m[5]*kTop,pos[2]+m[6]*kTop,1.0f};
    std::memcpy(p+kPosition,at,16);
    const float hpMax=At<float>(v,kHpMax);
    Put<float>(p,kHpMax,hpMax>1.0f ? hpMax : 1.0f);
    Put<float>(p,kHp,Clamp(At<float>(v,kHp),0.0f,hpMax>1.0f ? hpMax : 1.0f));
}

int SubFault(const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    Log("SUB the game faulted building it (%08lX at EDF+%llX): the carrier is off until the game restarts",r->ExceptionCode,
        static_cast<unsigned long long>(static_cast<const unsigned char*>(r->ExceptionAddress)-image));
    broken=true;preloaded=false;
    return EXCEPTION_EXECUTE_HANDLER;
}

unsigned char* CreateSub(const float* m,InitParam* param) noexcept {
    __try { return reinterpret_cast<CreateObjectFn>(image+kCreateObject)(At<void*>(image,kObjectMgr),m,kSubSgo,param); }
    __except(SubFault(GetExceptionInformation())) { return nullptr; }
}

// The heli's "body" part (jet.cpp FixBodyPart): the crash step reads its index unchecked.
void FixBodyPart(unsigned char* v) noexcept {
    if(!bodyPartOk || At<std::int32_t>(v,kBodyPart)!=-1)return;
    const auto i=reinterpret_cast<FindPartFn>(image+kFindPart)(v+kParts,L"body");
    if(i>=0)Put<std::int32_t>(v,kBodyPart,i);
    else Log("SUB v=%p has no body part (going down it would crash)",v);
}

void __fastcall PhysicsHook(void* vehicle) {
    nextPhysics(vehicle);
    __try {
        auto v=static_cast<unsigned char*>(vehicle);
        const ULONGLONG ms=GameMs();
        Sub* s=FindSub(v,ms);
        if(!s || !s->ready || v[kDead] || ms-s->seen>200 || !IsSub(v))return;
        const auto body=At<void*>(v,kBody);
        if(!body)return;
        alignas(16) float lin[4]={s->lin[0],s->lin[1],s->lin[2],0.0f},ang[4]={s->ang[0],s->ang[1],s->ang[2],0.0f};
        reinterpret_cast<SetVecFn>(image+kSetLinearVelocity)(body,lin);
        reinterpret_cast<SetVecFn>(image+kSetAngularVelocity)(body,ang);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// The follower gauges as the game draws them, then one per live carrier (see the top).
void __fastcall GaugeHook(void* hud,void* viewProj,void* owner,void* r9,void* fifth) {
    const auto draw=reinterpret_cast<GaugeFn>(image+kGaugeFn);
    draw(hud,viewProj,owner,r9,fifth);
    __try {
        alignas(16) unsigned char stand[kProxySize]{};
        Node head{};head.next=&head;head.prev=&head;
        Node nodes[kMaxSubs]{};
        const ULONGLONG tick=GetTickCount64();
        int n=0;
        for(int i=0;i<kMaxSubs;++i) {
            const Sub& s=subs[i];
            if(!s.vehicle || !s.ready || tick-s.gaugeTick>kGaugeMs)continue;
            Node& node=nodes[n++];
            node.object=proxies[i];
            node.prev=head.prev;node.next=&head;
            head.prev->next=&node;head.prev=&node;
        }
        if(!n)return;
        Put<void*>(stand,kFollowers,&head);
        draw(hud,viewProj,stand,r9,fifth);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

const unsigned char kPhysicsSig[]={0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B,0xD9,0xE8};
const unsigned char kSetLinSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xA8,0x00,0x00};
const unsigned char kSetAngSig[]={0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C,0x8B,0xC2,0x8B,0x91,0xF0,0x00,0x00,0x00,0x45,0x33,0xC9,0x4C,0x8B,0x50,0x58,0x49,0x8B,0x42,0x18,0x49,0x8D,0x4A,0x18,0x48,0xFF,0xA0,0xB0,0x00,0x00};
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
const unsigned char kCreateObjectSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
const unsigned char kSetTeamSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
const unsigned char kFindPartSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x48,0x83,0xEC,0x50};
struct Sig { unsigned rva; unsigned char bytes[8]; std::size_t size; };
// The drawer and what it reads of the owner and of each object (docs/subcarrier-re.md §4).
const Sig kGaugeSigs[]={
    {kGaugeFn,{0x4C,0x8B,0xDC,0x55,0x53,0x56,0x57,0x41},8},
    {0x804329,{0x49,0x8B,0xB8,0x50,0x05,0x00,0x00},7},          // mov rdi,[r8+550h]: the owner's followers
    {0x8043F4,{0x0F,0x10,0x90,0x90,0x00,0x00,0x00},7},          // movups xmm2,[rax+90h]: the object's position
    {0x804633,{0xF3,0x0F,0x10,0x90,0xF8,0x02,0x00,0x00},8},     // its HP
    {0x80463B,{0xF3,0x0F,0x5E,0x90,0xF4,0x02,0x00,0x00},8},     // over its max HP
    {kGaugeCall,{0xE8,0x4E,0x00,0x00,0x00},5},
    {kGaugeDraw,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74},8},
};
}  // namespace

bool IsSub(const void* vehicle) noexcept {
    const auto v=static_cast<const unsigned char*>(vehicle);
    return Readable(v,kSpeedGain+4) && At<const unsigned char*>(v,0)==image+kHeli506 && At<float>(v,kSpeedGain)==kSubMark;
}

void PreloadSub() noexcept {
    __try {
        preloaded=false;
        for(auto& s:subs)s=Sub{};
        if(!spawnOk || broken)return;
        const auto mgr=At<void*>(image,kPreloadMgr);
        preloaded=mgr && FilesThere();
        if(preloaded)reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kSubSgo,2,-1);
        Log("SUB preload carrier=%d",preloaded);
    } __except(EXCEPTION_EXECUTE_HANDLER){preloaded=false;}
}

unsigned char* SubLaunch(const float* pos,const float* heading) noexcept {
    __try {
        const ULONGLONG ms=GameMs();
        if(!spawnOk || !preloaded || !pos || !heading || !At<void*>(image,kObjectMgr))return nullptr;
        if(LiveSubs(ms)>=kMaxSubs){Log("SUB launch: %d carriers out already",kMaxSubs);return nullptr;}
        float fwd[3]={heading[0],0.0f,heading[2]};
        if(!Normalize(fwd)){fwd[0]=0.0f;fwd[2]=1.0f;}
        float start[3]={pos[0],pos[1]+kHullBottom+kClear,pos[2]};
        float ground=0.0f;
        if(GroundUnder(pos,fwd,&ground))start[1]=ground+kHullBottom+kClear;
        // Rows right, up, forward, position (jet.cpp Launch).
        alignas(16) const float m[16]={fwd[2],0,-fwd[0],0, 0,1,0,0, fwd[0],0,fwd[2],0, start[0],start[1],start[2],1};
        InitParam param{image+kInitParamVtable,{}};
        unsigned char* v=CreateSub(m,&param);
        if(!v)return nullptr;
        FixBodyPart(v);
        reinterpret_cast<SetTeamFn>(image+kSetTeam)(v,kTeamFriend,true);
        reinterpret_cast<RideAiFn*>(At<void**>(v,0))[kSlotRideAi](v,true);
        if(!IsSub(v)) {
            Log("SUB launch: %p is no carrier (mark %.0f): deleted",v,At<float>(v,kSpeedGain));
            reinterpret_cast<DeleteFn>(image+kDelete)(v);
            return nullptr;
        }
        Sub* s=FreeSub(v,ms);
        if(s) {
            *s=Sub{};s->vehicle=v;s->ctrl=SelfCtrl(v);s->seen=s->bornAt=ms;s->launched=true;
            QueryPerformanceCounter(&s->last);
            std::memcpy(s->post,start,12);
        }
        Log("SUB v=%p launched at (%.0f,%.0f,%.0f) heading (%.2f,%.2f) hp=%.0f driver=%d",v,start[0],start[1],start[2],fwd[0],fwd[2],
            At<float>(v,kHp),SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::dummy);
        return v;
    } __except(EXCEPTION_EXECUTE_HANDLER){return nullptr;}
}

void SubFrame(unsigned char* v) noexcept {
    if(!physicsOk)return;
    const ULONGLONG ms=GameMs();
    const float* pos=reinterpret_cast<const float*>(v+kPosition);
    const float* m=reinterpret_cast<const float*>(v+kMatrix);
    Sub* s=FindSub(v,ms);
    if(!s) {
        s=FreeSub(v,ms);
        if(!s)return;   // kMaxSubs out: this one is left to the game
        *s=Sub{};s->vehicle=v;s->ctrl=SelfCtrl(v);s->bornAt=ms;
        QueryPerformanceCounter(&s->last);
        std::memcpy(s->post,pos,12);
        Log("SUB v=%p crewed (placed by the mission): hp=%.0f/%.0f",v,At<float>(v,kHp),At<float>(v,kHpMax));
    }
    s->seen=ms;s->gaugeTick=GetTickCount64();
    LARGE_INTEGER now,freq;QueryPerformanceCounter(&now);QueryPerformanceFrequency(&freq);
    const float dt=Clamp(static_cast<float>(now.QuadPart-s->last.QuadPart)/static_cast<float>(freq.QuadPart),0.004f,0.1f);
    s->last=now;
    // The stock input stays out of it, the move-area clamp too (it would teleport the hull back).
    Put<float>(v,kInLateral,0.0f);Put<float>(v,kInForward,0.0f);Put<float>(v,kInYaw,0.0f);
    Put<float>(v,kAreaInset,kNoInset);
    float nose[3]={m[8],0.0f,m[10]};
    if(!Normalize(nose)){nose[0]=0.0f;nose[2]=1.0f;}
    Nearest prey{{pos[0],pos[1],pos[2]},kRange,{0,0,0},false};
    VisitEnemies(v,&SeeEnemy,&prey);
    s->hasTarget=prey.found;
    if(prey.found)std::memcpy(s->target,prey.aim,12);
    Follow(*s,dt);
    Drive(*s,pos,nose,dt);
    float want[3];
    Heading(*s,pos,m,want);
    Steer(*s,m,want);
    s->ready=true;
    Fire(*s,v,pos,m,ms);
    Proxy(static_cast<int>(s-subs),v,pos,m);
    if(cfg.debug && ms-s->logAt>2000) {
        s->logAt=ms;
        float ground=0.0f;
        const bool seen=GroundUnder(pos,nose,&ground);
        Log("SUB v=%p pos=(%.0f,%.0f,%.0f) clear=%.1f post=(%.0f,%.0f)%s vel=(%.1f,%.1f,%.1f) yawRate=%.1f target=%s%.0f hp=%.0f/%.0f fire=%d/%d",
            v,pos[0],pos[1],pos[2],seen ? pos[1]-kHullBottom-ground : -1.0f,s->post[0],s->post[2],s->moving ? " moving" : "",
            s->lin[0],s->lin[1],s->lin[2],s->ang[1]*180.0f/kPi,s->hasTarget ? "yes " : "no ",
            s->hasTarget ? prey.best : 0.0f,At<float>(v,kHp),At<float>(v,kHpMax),v[kFireGun],v[kFireMissile]);
    }
}

bool InstallSub() noexcept {
    __try {
        const bool sig=Matches(0x61B710,kPhysicsSig,sizeof(kPhysicsSig)) && Matches(kSetLinearVelocity,kSetLinSig,sizeof(kSetLinSig)) &&
                       Matches(kSetAngularVelocity,kSetAngSig,sizeof(kSetAngSig)) && Matches(kDelete,kDeleteSig,sizeof(kDeleteSig));
        if(!sig){Log("SUB profile mismatch: carriers off");return false;}
        // Slot 57 of the 506, after whatever is there (jet.cpp's hook: InstallJets runs first).
        const auto slot=reinterpret_cast<void**>(image+kHeli506)+kSlotPhysics;
        void* const current=*slot;
        nextPhysics=reinterpret_cast<PhysicsFn>(current);
        physicsOk=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&PhysicsHook));
        spawnOk=physicsOk && Matches(kPreload,kPreloadSig,sizeof(kPreloadSig)) && Matches(kCreateObject,kCreateObjectSig,sizeof(kCreateObjectSig)) &&
                Matches(kSetTeam,kSetTeamSig,sizeof(kSetTeamSig)) && Readable(image+kInitParamVtable,8);
        bodyPartOk=spawnOk && Matches(kFindPart,kFindPartSig,sizeof(kFindPartSig));
        if(!bodyPartOk)spawnOk=false;   // going down without its body part crashes (jet.cpp)
        bool gauge=Readable(image+kGaugeHud+3*8,8) && At<const unsigned char*>(image,kGaugeHud+3*8)==image+kGaugeDraw;
        for(const auto& g:kGaugeSigs)gauge=gauge && Matches(g.rva,g.bytes,g.size);
        noFollowers.next=&noFollowers;noFollowers.prev=&noFollowers;
        bool changed=false;
        gaugeOk=gauge && RedirectCall(image+kGaugeCall,image+kGaugeFn,reinterpret_cast<void*>(&GaugeHook),changed);
        Log("HOOK sub physics=%d spawn=%d gauge=%d (chained physics onto %p)",physicsOk,spawnOk,gaugeOk,current);
        return physicsOk;
    } __except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
}  // namespace crew
