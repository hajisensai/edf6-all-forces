// EDF4.1's UfoRobo (OBJECT/UFOROBO401.SGO, the story's M007 "巨神船" / M411 "アルゴ", DLC M415 / M416 / M010) as the plugin's own
// class: a flying ship that turns into a walking robot, one object for both (the scripts keep one handle across the change:
// M007 registers its durability events on it after ObjectAction 0). Spec: docs/edf41-uforobo.md (EDF41.exe RE: jobs/14618f05/
// tmp/rerobo/SPEC.md). RVAs: EDF41.exe for 4.1 (the port's specification), EDF.dll TimeDateStamp 0x678CCB46 for EDF6.
//
// The object. Built by EventAnimationObject's constructor (EDF.dll 0x5B5FB0, 0xC90 bytes: a model, the CAS player "main"
// started in "default", a ragdoll, GameObjectBase's durability and damage) under a cloned vtable (src/edf41classes.h; slots 0
// dtor, 5 Update, 9 OnMessage ours). Nothing of EventAnimationObject moves or fights: every behaviour below is 4.1's, frame for
// frame, kept in the plugin's own record of the object (Robo):
//  - body state machine (4.1 +0x15B0, 12 states, EDF41.exe 0x278230 ..): S0 ship idle, S1/S2/S3 the ship's three volleys,
//    S4 transform (no damage taken: 4.1 slot 10), S5 fall, S6 landing, S7 robot idle (step / turn / attack chooser), S8 a step
//    or turn clip, S9 an attack clip, S10 dying, S11 wreck (deleted 10 s later);
//  - AI state machine (4.1 +0x1450): A0 waits, A1 the ship's flight (circle 300-500 m from the target, 250 m above it, facing
//    it; then a straight leg; sideways drifts; the three gun timers), A2/A3 the robot's (approach / hold / walk legs, turns
//    by +-36 / 72 / 126 degrees, the attack timer), A4/A5 the attack choice (robo_attack_table: distance and angle windows);
//  - motion (4.1 slot 1, 0x2766A0): the ship's velocity eased 0.25 % a frame to the command (<= 90 m/s), its yaw rate, the
//    roll; the robot's root motion from the clip (edf41_robo_motion, read off the CAS by tools/make_edf41_objects.py, the
//    bone's own tracks dropped so the model does not move twice), turned by the yaw it had when the step began, kept on the
//    ground by a map ray (from 100 m above to the feet, else falling at 0.008 m/frame^2);
//  - four gun groups (4.1 +0x3F0 / +0x800 / +0xC10 / +0x1020, each G0 idle - G1 charge - G2 fire): knees (BeamBullet01),
//    shoulders (RocketBullet01), hands (RocketBullet01, the buster and the robot's beam attacks), the genocide shot; numbers
//    of 4.1's 0x2744B0 with s = level / 10; muzzles by MAB locator, aimed at the target within the group's limit, a random
//    cone;
//  - the clips' MAB events, raised here as the main CAS player passes their times (edf41_robo_events): se (4.1's preset
//    names, played by cue: edf41_se), 攻撃判定 (a blast of damage x s at the locator: every footstep, the chops and stomps,
//    the landing), 砲撃開始 / 砲撃終了 (the hand guns' beam attacks), ジェノサイド砲発射;
//  - ObjectAction (message 0x1000000D): 0 transform (alive only), 1 robot at once + approach mode, 2 approach mode.
// What EDF6 does for it unchanged: durability and damage (GameObjectBase), the script's durability events (9200 / 9201), the
// kill count, the model's drawing and its animation clock.
// Every game pointer goes through Readable; every EDF.dll function is fingerprinted (edf41classes.cpp); a failure is logged
// once and the object is left as EventAnimationObject made it.
#include "crew.h"
#include "edf41classes.h"
#include "edf41net.h"
#include "memory.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace crew {
namespace {
using namespace edf41c;

constexpr int kSlots=45;
Clone roboClass{"EventAnimationObject",kEaoVtable,kSlots,kEaoSlots,{},{},{}};

constexpr float kPi=3.14159265f;
constexpr int kMostEvents=24;          // per clip (tools/make_edf41_objects.py ROBO_MOST_EVENTS)
constexpr int kMostMotionKeys=512;     // ROBO_MOST_MOTION_KEYS

// ---- once-per-kind log ------------------------------------------------------------------------------------------
void Fail(const char* what) noexcept {
    static std::mutex lock;
    static const char* said[48];
    static int count=0;
    std::lock_guard<std::mutex> hold(lock);
    for(int i=0;i<count;++i)
        if(said[i]==what)return;
    if(count<48)said[count++]=what;
    Log("EDF41ROBO check failed: %s",what);
}

void* Model(void* object) noexcept { return static_cast<char*>(object)+kEaoModel; }

// UTF-8 for the log (Log's %ls goes through the C locale and drops a line with Japanese in it: no 攻撃判定 line reached the log
// of the M411 run 2026-10-15 12:46).
struct U8 {
    char text[128]={};
    explicit U8(const std::wstring& w) noexcept { WideCharToMultiByte(CP_UTF8,0,w.c_str(),-1,text,sizeof(text),nullptr,nullptr); }
};

// ---- EDF6 functions this file calls itself (phase 3, 2026-10-15; static reading of EDF.dll, H unless noted) --------------
// Each is fingerprinted (first 8 bytes) once; a mismatch turns the part that needs it off and is logged.
//  - 0x6BADD0 FindLocator(model, out, list, name): `list` is the MAB locator list. ロックオン sits in list 1 (the lock-on list
//    UfoCarrier508's lock-point component 0x6C74E0 reads at MAB+8+[+0x14]); edf41c::FindLocator asks list 0 only, which is why
//    the lock point never moved (ds4_M007 real run: aimed at the spawn point 103 250 -361 all mission).
//  - 0x6E40B0 ragdoll activate(ragdoll): every body to motion mode 4 (0x6ED480; mode 4 = dynamic, gravity factor body +0x68)
//    and every constraint on (0x6E4A70), under the world lock. ShootingTarget's death (its slot 18 0x5CB220) calls it on the very
//    same layout (ragdoll at object +0xAA0, model +0x5C0, skeleton +0x660) and then pushes the bodies: 0x11B0CB0(body handle,
//    impulse, point) for the hit bodies, 0x11B0C80(handle, impulse) for the rest. Bodies: [ragdoll+0x78], count [+0x88], 0xC0
//    each, handle at +0x50; 0x11B15B0(handle) -> the body's position (float[4]).
//  - 0x542670 ApplyAreaDamage(GDI*, bool* hitAny, hitSet* (null: none, 0x5439CF checks), GameObject* skip): an AABB query of
//    radius GDI+0x58 at GDI+0x30 (filter 0x106070), a team filter on GDI+0x24 (none with flag 0x20 or team -1), a per-object
//    copy of the GDI (0x114210) sent as the damage message (0x543920: slots 10 / 9 / 11). DragonSmall's body hit calls it so
//    (0x32DBED, GDI kept at +0xA30: weak self +0x10, team +0x24, centre +0x30, damage +0x50, radius +0x58, +0x64 = 0.5);
//    UfoCarrier508's death builds one on the stack the same way (0x4F9C85..0x4F9DAA: +0x68 = 1.0, impulse (0,0,0,1)). Nothing is
//    drawn, as 4.1's 0x28B930.
//  - 0x2E8F20 EffectGenUtil "MetalHit" (egu, pos, normal, colour, size, bool): the spark the game shows where bullets hit metal
//    (debug table entry MetalHit, lambda 0xB3280; bullet hit 0x23C5C1).
//  - UfoRoboCanonChargeEffect (EDF6 kept the class, not its factory name: create 0x84A60(manager, out weak_ptr, matrix, param),
//    ctor 0x306B60 0x590 bytes, vtable 0x17A9338; its slot 5 is empty): 4.1 steps it from the gun each frame (EDF41 0x1C6280(effect, muzzle matrix)); EDF6's step is 0x307C40
//    (effect, matrix, dt) (reads +0x4E8 / +0x4E0 / +0x384 as 4.1's +0x498 / +0x490 / +0x314, copies the matrix to +0x4F0, steps
//    the particles 0x2F3B60(effect+0x190, dt) as HomingLaserHit01's update does). Its InitParam: InitParamBase (vtable
//    0x1762068, 0x30 bytes; +8 the matrix, filled by the spawn) then 4.1's template fields 8 bytes later (scalars 4.1 +0x28..+0x48
//    -> +0x30..+0x50, colours 4.1 +0x50 / +0x60 / +0x70 -> +0x60 / +0x70 / +0x80: 0x306B60 reads +0x3C/+0x40/+0x44/+0x48 where
//    4.1's 0x1C51C0 reads +0x34/+0x38/+0x3C/+0x40, and the colour at +0x70 where 4.1 reads +0x60).
//  - CAS variables (what EDF6's AlienTrailer does with the same SGO key ik_control_info, 0x30C56A / 0x30EA9C): index by name
//    0x1167170(cas = object+0x8F0, name, 0), set a matrix 0x116DF60(cas, index, const float[16]) (cas+0x80 records 0x60: +0x10
//    set, +0x20 matrix).
namespace p6 {
constexpr std::size_t kFindLocator=0x6BADD0,kRagdollOn=0x6E40B0,kPointImpulse=0x11B0CB0,kLinearImpulse=0x11B0C80,kBodyPos=0x11B15B0,
    kAreaDamage=0x542670,kMetalHit=0x2E8F20,kChargeStep=0x307C40,kChargeCtor=0x306B60,
    kCasVar=0x1167170,kCasVarSet=0x116DF60,kDelete=0x118A1B0,kBodyById=0x6EA430,kChargeCreate=0x84A60,kCasSpeed=0x116DF20,
    kMatCtor=0x6DBE90,kMatTarget=0x6DC7B0,kMatLoad=0x6DC6B0,kMatPlay=0x6DC830,kMatStep=0x6DC8B0,kMatFree=0x29D120,
    kRagdollLayer=0x6E9320,kParticleCreate=0x844C0,kParticleParamInit=0x300C80,kTexByName=0x2E8510,kTexAssign=0xF5E10,
    kTexFree=0x1120820,kParticleParamFree=0x8FDF0,kSetAngVel=0x11B1760,kContactSignal=0xDAB920,kContactAdd=0x6E3A60,kContactCb=0x6EC450;
constexpr std::size_t kInitParamBase=0x1762068,kEffects=0x20B2980,kManager=0x20B2958,kChargeVtable=0x17A9338,kParticleParamVtable=0x17620E8;
struct Print { std::size_t rva; unsigned char bytes[8]; };
constexpr Print kPrints[]={
    {kFindLocator,{0x40,0x53,0x55,0x56,0x57,0x41,0x56,0x41}},{kRagdollOn,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kPointImpulse,{0x48,0x83,0xEC,0x38,0x48,0x8B,0x81,0x00}},{kLinearImpulse,{0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C}},
    {kBodyPos,{0x48,0x83,0xEC,0x28,0x48,0x8B,0x81,0x00}},{kAreaDamage,{0x48,0x89,0x5C,0x24,0x20,0x55,0x56,0x57}},
    {kMetalHit,{0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41}},{kChargeStep,{0x48,0x8B,0xC4,0x53,0x56,0x57,0x41,0x56}},
    {kChargeCtor,{0x48,0x8B,0xC4,0x48,0x89,0x58,0x18,0x55}},{kCasVar,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},
    {kCasVarSet,{0x41,0x0F,0x28,0x00,0x8B,0xC2,0x48,0x8D}},{kDelete,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},
    {kBodyById,{0x4C,0x8B,0x41,0x78,0x45,0x33,0xC9,0x48}},{kChargeCreate,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C}},
    {kCasSpeed,{0x8B,0xC2,0x48,0x69,0xD0,0x60,0x01,0x00}},
    {kMatCtor,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C}},{kMatTarget,{0x48,0x89,0x11,0xC3,0xCC,0xCC,0xCC,0xCC}},
    {kMatLoad,{0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74}},{kMatPlay,{0x4C,0x8B,0xC9,0x0F,0x57,0xC9,0x8B,0xCA}},
    {kMatStep,{0x48,0x8B,0xC4,0x48,0x89,0x68,0x18,0x48}},{kMatFree,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C}},
    {kRagdollLayer,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C}},
    {kParticleCreate,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C}},{kParticleParamInit,{0x48,0x89,0x5C,0x24,0x10,0x57,0x48,0x83}},
    {kTexByName,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},{kTexAssign,{0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x6C}},
    {kTexFree,{0x40,0x53,0x48,0x83,0xEC,0x20,0x48,0x8B}},{kParticleParamFree,{0x48,0x89,0x5C,0x24,0x08,0x57,0x48,0x83}},
    {kSetAngVel,{0x48,0x8B,0x81,0x00,0x01,0x00,0x00,0x4C}},{kContactSignal,{0x48,0x8B,0x89,0x80,0x09,0x00,0x00,0x41}},
    {kContactAdd,{0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74}},{kContactCb,{0x40,0x53,0x56,0x57,0x48,0x81,0xEC,0xD0}},
};
std::atomic<int> state{0};   // 0 unchecked, 1 good, 2 refused
bool Ok() noexcept {
    int s=state.load();
    if(s)return s==1;
    for(const Print& p:kPrints)
        if(!Readable(image+p.rva,8) || std::memcmp(image+p.rva,p.bytes,8)!=0) {
            Log("EDF41ROBO the code at EDF.dll+0x%zX is not what this was written for: lock-on, ragdoll death, area damage, sparks, charge effects and IK are off",p.rva);
            state=2;
            return false;
        }
    state=1;
    return true;
}
template<class F> F At(std::size_t rva) noexcept { return reinterpret_cast<F>(image+rva); }
void* Global(std::size_t rva) noexcept { return Readable(image+rva,8) ? *reinterpret_cast<void* const*>(image+rva) : nullptr; }

bool FindLocatorIn(void* model,int list,const wchar_t* name,LocatorHandle* out) noexcept {
    *out={nullptr,nullptr};
    if(!Ok() || !model || !Readable(static_cast<char*>(model)+0x280,8))return false;
    return At<bool(__fastcall*)(void*,LocatorHandle*,int,const wchar_t*)>(kFindLocator)(model,out,list,name) && out->entry && out->bone;
}

// The ragdoll's bodies (0xC0 records) and their physics handles.
struct Bodies { char* data=nullptr; std::size_t count=0; };
Bodies RagdollBodies(void* object) noexcept {
    char* rag=static_cast<char*>(object)+kEaoRagdoll;
    Bodies b;
    if(!Readable(rag+0x78,0x18))return b;
    b.data=*reinterpret_cast<char**>(rag+0x78);
    b.count=*reinterpret_cast<const std::size_t*>(rag+0x88);
    if(!b.data || b.count==0 || b.count>256 || !Readable(b.data,b.count*0xC0))b=Bodies{};
    return b;
}
void* BodyHandle(const Bodies& b,std::size_t i) noexcept {
    if(i>=b.count)return nullptr;
    void* h=*reinterpret_cast<void* const*>(b.data+i*0xC0+0x50);
    return h && Readable(static_cast<char*>(h)+0xF0,0x18) ? h : nullptr;
}
bool BodyPosition(void* handle,float* out) noexcept {
    if(!Ok() || !handle)return false;
    const float* p=At<const float*(__fastcall*)(void*)>(kBodyPos)(handle);
    if(!p || !Readable(p,12))return false;
    std::memcpy(out,p,12);
    return std::isfinite(out[0]+out[1]+out[2]);
}
// The ragdoll body a Havok body id belongs to (0x6EA430; -1 none).
int BodyById(void* object,std::uint32_t id) noexcept {
    if(!Ok())return -1;
    return At<int(__fastcall*)(void*,std::uint32_t)>(kBodyById)(static_cast<char*>(object)+kEaoRagdoll,id);
}
// The ragdoll on, its bodies first put back on the ragdoll collision layer: LoadRagdoll gives every body layer 6 (0x106160 at
// 0x6EB9A7), but EventAnimationObject's constructor then moves them all to layer 0x11 (0x6E9320(ragdoll, 0x11) at 0x5B6401: the
// layer of things only bullets and rays meet), which the ground does not stop: the first real deaths (M007 / M411, 2026-10-15)
// fell through the map. 4.1 does the same at its death (0x275B30: every body's filter to (system << 16) | 0x426, layer 6).
bool RagdollOn(void* object) noexcept {
    if(!Ok() || !RagdollBodies(object).count)return false;
    char* rag=static_cast<char*>(object)+kEaoRagdoll;
    At<void(__fastcall*)(void*,int)>(kRagdollLayer)(rag,6);
    At<void(__fastcall*)(void*)>(kRagdollOn)(rag);
    return true;
}
void PointImpulse(void* handle,const float* impulse,const float* point) noexcept {
    if(!Ok() || !handle)return;
    alignas(16) const float i[4]={impulse[0],impulse[1],impulse[2],0.0f},p[4]={point[0],point[1],point[2],1.0f};
    At<void(__fastcall*)(void*,const float*,const float*)>(kPointImpulse)(handle,i,p);
}
void LinearImpulse(void* handle,const float* impulse) noexcept {
    if(!Ok() || !handle)return;
    alignas(16) const float i[4]={impulse[0],impulse[1],impulse[2],0.0f};
    At<void(__fastcall*)(void*,const float*)>(kLinearImpulse)(handle,i);
}

// 4.1's area damage (0x28B930) on EDF6's ApplyAreaDamage: `owner`'s team and weak pointer, centre, radius, damage, flags (GDI
// +0x60: 4.1's stomp sets 0x17 in its own numbering; EDF6's 0x02 is "explosive", what makes soldiers fall over).
bool AreaDamage(void* owner,const float* centre,float radius,float damage,std::uint16_t flags,bool* hitAny) noexcept {
    char* o=static_cast<char*>(owner);
    if(!Ok() || !owner || !Readable(o+kWeakSelfAt,16) || !Readable(o+kTeamAt,4))return false;
    alignas(16) unsigned char gdi[0x90]={};
    WeakRef self;
    WeakSet(self,owner);
    if(!self.control)return false;
    std::memcpy(gdi+0x10,&self.object,8);
    std::memcpy(gdi+0x18,&self.control,8);
    std::memcpy(gdi+0x24,o+kTeamAt,4);
    const float c[4]={centre[0],centre[1],centre[2],1.0f},imp[4]={0.0f,0.0f,0.0f,1.0f};
    std::memcpy(gdi+0x30,c,16);
    std::memcpy(gdi+0x40,imp,16);
    std::memcpy(gdi+0x50,&damage,4);
    std::memcpy(gdi+0x58,&radius,4);
    std::memcpy(gdi+0x60,&flags,2);
    const float half=0.5f,one=1.0f;
    std::memcpy(gdi+0x64,&half,4);
    std::memcpy(gdi+0x68,&one,4);
    bool any=false;
    At<void*(__fastcall*)(void*,bool*,void*,void*)>(kAreaDamage)(gdi,&any,nullptr,owner);
    WeakReset(self);   // the GDI's own reference (the game's per-object copies took and dropped theirs)
    if(hitAny)*hitAny=any;
    return true;
}

void MetalHit(const float* pos,const float* normal,float size) noexcept {
    void* egu=Global(kEffects);
    if(!Ok() || !egu)return;
    alignas(16) const float p[4]={pos[0],pos[1],pos[2],1.0f},n[4]={normal[0],normal[1],normal[2],0.0f},white[4]={1.0f,1.0f,1.0f,1.0f};
    At<void(__fastcall*)(void*,const float*,const float*,const float*,float,bool)>(kMetalHit)(egu,p,n,white,size,true);
}

// UfoRoboCanonChargeEffect has no factory name in EDF6 (no such string in EDF.dll: the class is kept, not registered), so its
// create function is called as 4.1 called its own (EDF41 0x27BED5: 0xC63D0(manager, matrix, param)); EDF6's takes an out
// weak_ptr second ({object, control}, one weak count for the caller: 0x84B8B..0x84B92) and puts the object in the scene itself
// (0x1195A20 / 0x1197050).
bool MakeCharge(const float* matrix,void* param,WeakRef& out) noexcept {
    WeakReset(out);
    void* mgr=Global(kManager);
    if(!Ok() || !mgr)return false;
    alignas(16) float m[16];
    std::memcpy(m,matrix,sizeof(m));
    alignas(16) void* weak[2]={nullptr,nullptr};
    At<void*(__fastcall*)(void*,void*,const float*,void*)>(kChargeCreate)(mgr,weak,m,param);
    out.object=weak[0];
    out.control=weak[1];
    return out.object && out.control;
}
// 4.1's muzzle flash (EDF41 0x27C3F7..0x27C5A7): a ParticleObject made from an InitParam@ParticleObject at the muzzle. EDF6 has the
// same class with the same parameter block (create 0x844C0(manager, out weak_ptr, matrix, param), ctor 0x301110, param vtable
// 0x17620E8 over InitParamBase, 0x1D0 bytes, its emitter part constructed by 0x300C80(param + 0x30) = 4.1's 0x1C1920, destroyed by
// 0x8FDF0): the fields 4.1 writes are the ones EDF6's EffectGenUtil::Explosion (0x2E5ED0, the only EDF6 maker of a ParticleObject
// besides its factory) writes at the same offsets: +0x1A0 = 1, +0x1A4 = 0xFFFF, +0x1A8 / +0x1B0 / +0x1B8 = the addresses of +0x90
// (the position), +0xA0 ((0, 0, 0, 1)) and +0x30 (0x20), +0x38 / +0x3C the life in frames, +0x50 0.98, +0xC8 0.1, +0xE8 0, +0xF4 /
// +0xF8 the size, +0xFC -size / life, +0x110 the colour, +0x130 its rgb with alpha 0. The texture (ParticleAlpha_6.dds) is a
// TextureRef (0x30 bytes) at +0x158 that 0x300C80 constructs (0x1120520): got by name from EffectGenUtil (0x2E8510, as
// UfoRoboCanonChargeEffect's constructor does at 0x306D4B), assigned with 0xF5E10, the temporary freed with 0x1120820.
bool MuzzleFlash(const float* pos,int life,float size,const float* colour) noexcept {
    void* mgr=Global(kManager);
    void* egu=Global(kEffects);
    if(!Ok() || !mgr || !egu || life<=0 || !(size>0.0f))return false;
    alignas(16) unsigned char param[0x1D0]={};
    const void* base=image+kInitParamBase;
    std::memcpy(param,&base,8);
    At<void*(__fastcall*)(void*)>(kParticleParamInit)(param+0x30);
    const void* vt=image+kParticleParamVtable;
    std::memcpy(param,&vt,8);
    const void* at90=param+0x90;
    const void* atA0=param+0xA0;
    const void* at30=param+0x30;
    std::memcpy(param+0x1A8,&at90,8);
    std::memcpy(param+0x1B0,&atA0,8);
    std::memcpy(param+0x1B8,&at30,8);
    const std::int32_t one=1,mask=0xFFFF,kind=0x20;
    std::memcpy(param+0x1A0,&one,4);
    std::memcpy(param+0x1A4,&mask,4);
    param[0x1C0]=0;
    std::memcpy(param+0x30,&kind,4);
    const float p4[4]={pos[0],pos[1],pos[2],1.0f},w4[4]={0.0f,0.0f,0.0f,1.0f};
    std::memcpy(param+0x90,p4,16);
    std::memcpy(param+0xA0,w4,16);
    std::memcpy(param+0x38,&life,4);
    std::memcpy(param+0x3C,&life,4);
    const float fade=0.98f,spread=0.1f,shrink=-size/static_cast<float>(life),zero=0.0f;
    std::memcpy(param+0x50,&fade,4);
    std::memcpy(param+0xC8,&spread,4);
    std::memset(param+0xE8,0,8);
    std::memcpy(param+0xF4,&size,4);
    std::memcpy(param+0xF8,&size,4);
    std::memcpy(param+0xFC,&shrink,4);
    std::memcpy(param+0x110,colour,16);
    std::memcpy(param+0x130,colour,12);
    std::memcpy(param+0x13C,&zero,4);
    alignas(16) unsigned char texture[0x40]={};
    void* ref=At<void*(__fastcall*)(void*,void*,const wchar_t*)>(kTexByName)(egu,texture,L"ParticleAlpha_6.dds");
    if(ref)At<void*(__fastcall*)(void*,void*)>(kTexAssign)(param+0x158,ref);
    At<void(__fastcall*)(void*)>(kTexFree)(texture);
    alignas(16) static const float identity[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    alignas(16) void* weak[2]={nullptr,nullptr};
    At<void*(__fastcall*)(void*,void*,const float*,void*)>(kParticleCreate)(mgr,weak,identity,param);
    WeakRef w;
    w.object=weak[0];
    w.control=weak[1];
    const bool made=w.object!=nullptr;
    WeakReset(w);
    At<void*(__fastcall*)(void*,unsigned)>(kParticleParamFree)(param,0);
    return made;
}

// A body's angular velocity set (the world part's slot 0xB0, 0x11B1760(body, vec): the Havok order setLinear 0xA8 / setAngular 0xB0 /
// applyLinear 0xC0 / applyAngular 0xC8 / applyAt 0xD0; 4.1's 0x275CEB is its angular-velocity call on the first body).
void SetAngularVelocity(void* handle,const float* v) noexcept {
    if(!Ok() || !handle)return;
    alignas(16) const float w[4]={v[0],v[1],v[2],0.0f};
    At<void(__fastcall*)(void*,const float*)>(kSetAngVel)(handle,w);
}

// 4.1's ragdoll_contact [sound, impulse, quake strength, quake radius, frames] (ragdoll 0x337F60) on EDF6's own contact component,
// the one HumanBase builds (0x6EBBC0, at +0xF70): 0x90 bytes {+0 quake strength, +4 quake radius, +8 impulse threshold, +0x10 a
// SePreset, +0x90 the cooldown in frames, +0x94 the frames to wait after a sound}; its callback 0x6EC450 (comp, signal, contact) is
// registered on each body's contact signal (0x6E3A60(signal, comp, callback); signal = 0xDAB920(world, 2, body id)) after the body's
// contact events are switched on (slot 0x158 of the world part, flag 0x80). The callback plays the preset at the contact point, shakes
// the camera and sets the cooldown when the contact impulse reaches the threshold. The component is never freed (the registrations
// live in the ragdoll, which dies with the object). The sound is the cue of the 4.1 preset (the SEPRESET name is not in EDF6's).
struct Contact { unsigned char* comp=nullptr; bool on=false; };
bool ContactOn(Contact& c,void* object,const SgoNode* node,const void* seTable) noexcept {
    if(c.on || !Ok() || Count(node)<5)return false;
    float strength,radius,threshold;
    std::int32_t frames=0;
    wchar_t name[96];
    if(!Text(Child(node,0),name,96) || !Float(Child(node,1),&threshold) || !Float(Child(node,2),&strength) || !Float(Child(node,3),&radius)
       || !Int(Child(node,4),&frames))return false;
    const Bodies b=RagdollBodies(object);
    if(!b.count)return false;
    c.comp=new unsigned char[0x100]();
    std::memcpy(c.comp,&strength,4);
    std::memcpy(c.comp+4,&radius,4);
    std::memcpy(c.comp+8,&threshold,4);
    std::memcpy(c.comp+0x94,&frames,4);
    const bool voiced=FillSePreset(c.comp+0x10,seTable,name);
    int added=0;
    for(std::size_t i=0;i<b.count;++i) {
        char* h=static_cast<char*>(BodyHandle(b,i));
        if(!h)continue;
        char* part=*reinterpret_cast<char**>(h+0x100);
        if(!part || !Readable(part+0x58,8))continue;
        char* world=*reinterpret_cast<char**>(part+0x58);
        if(!world || !Readable(world+0x18,0x28))continue;
        void* const* vt=*reinterpret_cast<void* const* const*>(world+0x18);
        if(!vt || !Readable(vt+0x2C,8))continue;
        const std::uint32_t id=*reinterpret_cast<const std::uint32_t*>(h+0xF0);
        reinterpret_cast<void(__fastcall*)(void*,std::uint32_t,std::uint32_t,std::uint64_t,std::int32_t)>(vt[0x158/8])(world+0x18,id,0x80,0,0);
        void* signal=At<void*(__fastcall*)(void*,int,std::uint32_t)>(kContactSignal)(world,2,id);
        if(!signal)continue;
        At<void(__fastcall*)(void*,void*,void*)>(kContactAdd)(signal,c.comp,image+kContactCb);
        ++added;
    }
    Log("EDF41ROBO %p ragdoll contact: sound %s, impulse %.0f, quake %.2f / %.0f, wait %d frames; callbacks on %d of %zu bodies",object,voiced ? "found" : "NOT found",
        threshold,strength,radius,frames,added,b.count);
    c.on=added>0;
    return c.on;
}
// The cooldown the callback sets (frames), counted down by the plugin; returns the value before this frame's step.
int ContactTick(Contact& c) noexcept {
    if(!c.on || !c.comp)return 0;
    std::int32_t cd;
    std::memcpy(&cd,c.comp+0x90,4);
    if(cd>0) { const std::int32_t next=cd-1; std::memcpy(c.comp+0x90,&next,4); }
    return cd;
}

void ChargeStep(void* effect,const float* matrix,void* dt) noexcept {
    if(!Ok() || !effect || !dt || !Readable(effect,8) || *static_cast<void* const*>(effect)!=image+kChargeVtable)return;
    alignas(16) float m[16];
    std::memcpy(m,matrix,sizeof(m));
    At<void(__fastcall*)(void*,const float*,void*)>(kChargeStep)(effect,m,dt);
}
void Delete(void* object) noexcept {
    const char* o=static_cast<const char*>(object);
    if(Ok() && object && Readable(o+0x18,1) && !(o[0x18]&4))At<void(__fastcall*)(void*)>(kDelete)(object);
}

int CasVar(void* object,const wchar_t* name) noexcept {
    char* cas=static_cast<char*>(object)+0x8F0;
    if(!Ok() || !Readable(cas+0x80,0x18) || !*reinterpret_cast<void* const*>(cas+0x80))return -1;
    const int i=At<int(__fastcall*)(void*,const wchar_t*,int)>(kCasVar)(cas,name,0);
    const std::size_t count=*reinterpret_cast<const std::size_t*>(cas+0x90);
    return i>=0 && static_cast<std::size_t>(i)<count ? i : -1;
}
// The material animation player (the lights), as UfoCarrier508 keeps one at +0x15C8 (and AlienTrailer at +0xAA0): 0x40 bytes,
// constructed by 0x6DBE90; its target the model's +0x160 (AlienTrailer 0x30A10C: object +0x720 on the same model layout as
// EventAnimationObject's); [[MTAB blob, slot], ...] loaded by 0x6DC6B0; entry i played with mode m by 0x6DC830 (the carrier plays
// 0 with 2 at birth, 0x4F88B5); stepped by 0x6DC8B0(player, dt, 1.0) every frame (its Update's last call, 0x4F957C); its vector
// (player +8) freed by 0x29D120 (the carrier's destructor, 0x4F8AAB).
struct Lights { alignas(16) unsigned char player[0x40]; bool on=false; };
bool LightsStart(Lights& l,void* object,const SgoNode* table) noexcept {
    if(l.on || !Ok() || !table || Count(table)<1)return false;
    std::memset(l.player,0,sizeof(l.player));
    At<void(__fastcall*)(void*)>(kMatCtor)(l.player);
    At<void(__fastcall*)(void*,void*)>(kMatTarget)(l.player,static_cast<char*>(object)+kEaoModel+0x160);
    At<int(__fastcall*)(void*,const SgoNode*)>(kMatLoad)(l.player,table);
    l.on=true;
    return true;
}
std::size_t LightsCount(const Lights& l) noexcept { return l.on ? *reinterpret_cast<const std::size_t*>(l.player+0x20) : 0; }
void LightsPlay(Lights& l,int index,int mode) noexcept {
    if(index>=0 && static_cast<std::size_t>(index)<LightsCount(l))At<void(__fastcall*)(void*,int,int)>(kMatPlay)(l.player,index,mode);
}
void LightsStep(Lights& l,void* dt) noexcept {
    if(l.on && dt)At<void(__fastcall*)(void*,void*,float)>(kMatStep)(l.player,dt,1.0f);
}
void LightsEnd(Lights& l) noexcept {
    if(!l.on)return;
    At<void(__fastcall*)(void*)>(kMatFree)(l.player+8);
    l.on=false;
}

// A CAS player's playback rate (player +0x100: 0x116DFB0 advances the clock by dt x it): EDF6_Martian's death (slot 60,
// 0x479677) sets 0 on its main player before its ragdoll goes (4.1 0x278EC0 puts the main player's weight to 0 likewise).
void CasRate(void* object,float rate) noexcept {
    char* o=static_cast<char*>(object);
    if(!Ok() || !Readable(o+0xC80,4) || !Readable(o+0x8F0+0x60,0x18))return;
    const std::int32_t main=*reinterpret_cast<const std::int32_t*>(o+0xC80);
    const std::size_t count=*reinterpret_cast<const std::size_t*>(o+0x8F0+0x70);
    if(main<0 || static_cast<std::size_t>(main)>=count)return;
    At<void(__fastcall*)(void*,int,float)>(kCasSpeed)(o+0x8F0,main,rate);
}
void CasVarSet(void* object,int index,const float* matrix) noexcept {
    char* cas=static_cast<char*>(object)+0x8F0;
    if(index<0 || !Ok() || !Readable(cas+0x80,0x18))return;
    alignas(16) float m[16];
    std::memcpy(m,matrix,sizeof(m));
    At<void(__fastcall*)(void*,int,const float*)>(kCasVarSet)(cas,index,m);
}
}  // namespace p6

// ---- small vector helpers ---------------------------------------------------------------------------------------
float Len3(const float* v) noexcept { return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
float WrapPi(float a) noexcept {
    while(a>kPi)a-=2.0f*kPi;
    while(a<-kPi)a+=2.0f*kPi;
    return a;
}
// World -> the object's yaw frame (4.1 0x2795C0: rotate by -yaw about +Y; +X is where a positive yaw turns +Z to).
void ToLocal(float yaw,const float* w,float* l) noexcept {
    const float c=std::cos(yaw),s=std::sin(yaw);
    l[0]=w[0]*c-w[2]*s;
    l[1]=w[1];
    l[2]=w[0]*s+w[2]*c;
}
void ToWorld(float yaw,const float* l,float* w) noexcept {
    const float c=std::cos(yaw),s=std::sin(yaw);
    w[0]=l[0]*c+l[2]*s;
    w[1]=l[1];
    w[2]=-l[0]*s+l[2]*c;
}

// ---- the object's data (its SGO, read once) ---------------------------------------------------------------------
enum EventKind : int { kEvSe=0, kEvSeStop=1, kEvHit=2, kEvBeamStart=3, kEvBeamEnd=4, kEvGenocide=5 };
struct Event { float t; int kind; std::wstring text; float a,b,c; };
struct ClipEvents { std::wstring state; int stateIndex=-1; int clip=0; std::vector<Event> events; };
struct Motion { std::wstring state; int stateIndex=-1; float length=0.0f; std::vector<float> keys; };   // x z yaw per key
struct AttackEntry { std::wstring state; int stateIndex=-1; float dmin,dmax,amin,amax; };

// ---- the four gun groups (4.1 0x27AE00 / 0x27B0A0) --------------------------------------------------------------
enum GunState : int { kG0=0, kG1=1, kG2=2 };
enum GunGroup : int { kSmall=0, kMiddle=1, kLarge=2, kGenocide=3 };
const char* const kGunNames[4]={"small","middle","large","genocide"};
struct Muzzle {
    std::wstring locator,aimBone;
    LocatorHandle loc{};
    bool found=false;
    // the aim bone (4.1 0x27AC20 / 0x27BB70): its index, the angles (pitch about X, yaw about Y) eased towards the target, and
    // the bone's local matrix as the CAS left it / as this file last wrote it (to tell whether the CAS wrote it again)
    int bone=-1;
    float pitch=0.0f,yaw=0.0f;
    float base[16]={},written[16]={};
    bool haveWritten=false;
    // the charge effect of this muzzle while the group charges (4.1 G1: one UfoRoboCanonChargeEffect per muzzle)
    void* charge=nullptr;
    WeakRef chargeRef;
};
// 4.1's charge effect template (group +0x2F0, set in 0x2744B0; the beam attack's in 0x277C10): nine numbers (4.1 +0x28..+0x48)
// and one colour (4.1 +0x50 / +0x60 / +0x70, the same three times).
struct ChargeLook { float k[9]; float colour[4]; };
constexpr ChargeLook kChargeSmall{{1.0f,10.0f,0.7853982f,20.0f,1.0f,0.05f,15.0f,3.0f,5.0f},{1.8f,0.8f,1.2f,1.0f}};
constexpr ChargeLook kChargeMiddle{{1.5f,30.0f,0.7853982f,60.0f,1.0f,0.075f,15.0f,4.0f,6.0f},{1.0f,1.5f,1.8f,1.0f}};
constexpr ChargeLook kChargeLarge{{3.0f,50.0f,0.7853982f,80.0f,3.0f,0.1f,30.0f,7.0f,10.0f},{1.0f,1.0f,1.8f,1.0f}};
constexpr ChargeLook kChargeBeam{{3.0f,70.0f,0.7853982f,90.0f,3.0f,0.2f,55.0f,7.0f,10.0f},{2.0f,2.0f,3.6f,1.0f}};
constexpr ChargeLook kChargeGenocide{{8.0f,80.0f,0.9666666f,100.0f,3.5f,0.2f,50.0f,10.0f,13.0f},{1.0f,2.0f,1.0f,1.0f}};
struct Gun {
    ChargeLook look{};
    Muzzle muzzle[2];
    int muzzles=0;
    int state=kG0;
    int frames=0;                // frames in the state
    bool autoFire=false;         // +0xB4
    int autoMin=0,autoMax=0;     // +0xB8 / +0xBC
    int autoCount=0;             // +0xC0
    int charge=0;                // +0x9C, frames
    int interval=0;              // +0xA0: a round every interval + 1 frames
    int counter=0;               // +0xA4
    std::int64_t duration=0;     // +0xA8, frames; -1 never ends
    float aimLimit=0.0f;         // +0x98, rad
    float spread=0.0f;           // +0xB0, rad
    Bullet bullet;
    wchar_t factory[32]={};
    const wchar_t* chargeSe=nullptr;
    const wchar_t* shotSe=nullptr;
    const wchar_t* hitSe=nullptr;   // the bullets' hit sound (4.1 0x275730 fills each group's InitParam +0xB0)
    int flashLife=0;             // the muzzle flash: life in frames (4.1 group +0x3F0), pushed out along the muzzle by
    float flashOffset=0.0f;      // +0x3F4 + the template's +0x38; its size is the charge effect's strength when the charge
    float flashSize=0.0f;        // ended (4.1 0x27C04D: the first effect's +0x494 -> group +0xE8; EDF6 effect +0x4E4)
    std::uint32_t shots=0;
};

// ---- the body / AI states -----------------------------------------------------------------------------------------
enum Body : int { kS0=0, kS1, kS2, kS3, kS4, kS5, kS6, kS7, kS8, kS9, kS10, kS11 };
const char* const kBodyNames[12]={"S0 ship","S1 small volley","S2 middle volley","S3 buster","S4 transform","S5 fall",
                                  "S6 landing","S7 robot","S8 step","S9 attack","S10 dying","S11 wreck"};
enum Ai : int { kA0=0, kA1, kA2, kA3, kA4, kA5 };
const char* const kAiNames[6]={"A0 wait","A1 ship","A2 robot reroll","A3 robot","A4 wait idle","A5 attack choice"};

struct Timer { int count=0,min=0,max=0; float dmin=0.0f,dmax=0.0f; };   // 4.1 0x271AB0

struct Robo {
    void* obj=nullptr;
    bool ready=false,broken=false;
    std::uint32_t ticks=0;
    std::uint64_t rng=0;
    float level=10.0f,s=1.0f;
    float scale=1.0f;
    // the data
    std::vector<ClipEvents> clipEvents;
    std::vector<Motion> motions;
    std::vector<AttackEntry> attacks;
    int stShip=-1,stTransform=-1,stStand=-1,stFall=-1,stFallLand=-1,stDead=-1;
    int stWalkF[5]={-1,-1,-1,-1,-1},stWalkB[5]={-1,-1,-1,-1,-1};   // nl lr rl ln rn
    int stSideL=-1,stSideR=-1,stTurnL[3]={-1,-1,-1},stTurnR[3]={-1,-1,-1};
    LocatorHandle footL{},footR{},lockOn{};
    void* lockPoint=nullptr;     // the lock point the plugin keeps on ロックオン (EventAnimationObject has none)
    SoundHandle drive;           // se_table[0], the drive hum, from birth (4.1 +0x1560)
    bool driveOn=false;
    Gun gun[4];
    // state machines
    int body=kS0,bodyFrames=0;
    int ai=kA0,aiFrames=0;
    int form=0;                  // +0x15F0: 0 ship, 1 robot
    unsigned flags=0;            // +0x15F4: bit0 no commands, guns hold fire; bit3 small / middle guns do not aim
    bool approach=false;         // +0x1538
    // the frame's command (+0x1600..+0x161C)
    float cmdX=0.0f,cmdZ=0.0f,cmdY=0.0f,yawRate=0.0f;
    int turn=0;
    bool cmdTransform=false,cmdSmall=false,cmdMiddle=false,cmdBuster=false;
    int cmdAttack=-1;
    // AI parameters (+0x1478..)
    float aiSpeed=90.0f;
    int phase1=0;                // +0x147C
    float radius=0.0f;           // +0x1480
    int sideFrames=0,sideInterval=0;   // +0x1484 / +0x1488
    float sideAmount=0.0f;       // +0x148C
    int phase2=0;                // +0x1490
    float cruise[3]={0.0f,0.0f,90.0f};   // +0x14A0
    int cruiseTurnFrames=0;      // +0x14B0
    float cruiseTurnRate=0.0f;   // +0x14B4
    Timer tSmall,tMiddle,tBuster,tAttack;
    int attackPick=-1;
    // pose
    float pos[3]={},yaw=0.0f,roll=0.0f,vel[3]={},fall=0.0f;
    bool landed=false;
    // a step (S8)
    int stepState=-1;
    const Motion* stepMotion=nullptr;
    float stepYaw=0.0f,stepPos[3]={};
    int chain=0;                 // +0x328: 1 after a backward step
    int pendingState=-1;         // the clip S8 / S9 is to play
    // the clip's events
    int evState=-2,evClip=0;
    float evTime=-1.0f;
    // dying
    int dieFrames=0;
    bool dead=false,deleted=false;
    bool ragdoll=false;          // the ragdoll took over (4.1 S10)
    std::uint32_t hitsTaken=0;
    p6::Contact contact;         // 4.1's ragdoll_contact (crash sound + quake), on from the death
    int contactSeen=0;
    float lastHp=-1.0f;          // the durability after the last hit (the damage a hit did = the drop)
    // foot IK (ik_control_info: [[bone, CAS variable], ...], 4.1 0x278190)
    int ikBone[2]={-1,-1},ikVar[2]={-1,-1};
    float lastMatrix[16]={};     // the +0x60 the plugin wrote for the frame now built
    bool haveLastMatrix=false;
    int lockBone=-1;             // hara1, when the ロックオン locator is missing
    void* dt=nullptr;            // this frame's Update(dt) argument (the charge effects' step takes it)
    p6::Lights lights;           // the material animation (4.1's two light blobs: the loop, the death)
    // target
    bool haveTarget=false;
    float target[3]={};
    // dev
    bool placed=false,forcedTransform=false;
    int logged=0;
    std::uint32_t events=0,hits=0,blasts=0;   // MAB events raised, 攻撃判定 among them, blasts let off
};

std::mutex lock;
std::unordered_map<const void*,Robo> robos;
std::atomic<int> created{0};

// ---- online (edf41net): declared here, defined in the block "online" at the end of this namespace ----
bool NetFollows(const Robo& r) noexcept;     // a guest copy (online: the host's object; or the development mirror): it shows, it does not decide
void NetBody(Robo& r) noexcept;              // the deciding copy: a body state change, to the guests
void NetGun(Robo& r,int group) noexcept;     // the deciding copy: a robot-form gun group started by its own timer
void NetIn(Robo& r) noexcept;                // every copy, before its frame: the role, the events that came (guests)
void NetPose(Robo& r) noexcept;              // a guest, after Move: its pose pulled to the host's
void NetOut(Robo& r) noexcept;               // the deciding copy, after its frame: the periodic state
void NetMirrorMake(void* self) noexcept;     // development (ini Edf41NetMirror): the mirror of `self`, made outside the lock
void NetForget(const void* self) noexcept;   // its destructor: the record goes


int RandInt(Robo& r,int n) noexcept { return n<=0 ? 0 : static_cast<int>(Random01(r.rng)*static_cast<float>(n))%n; }
float Rand(Robo& r) noexcept { return Random01(r.rng); }

// ---- sounds ---------------------------------------------------------------------------------------------------------
// 4.1's sound by its SEPRESET name, from the object's edf41_se (edf41classes.h PlaySe).
void PlaySe(Robo& r,const wchar_t* name,const float* at) noexcept {
    if(name && *name)edf41c::PlaySe(r.obj,name,at,nullptr);
}

// ---- reading the SGO ------------------------------------------------------------------------------------------------
std::wstring TextOf(const SgoNode* n) noexcept {
    wchar_t buf[128];
    return Text(n,buf,128) ? std::wstring(buf) : std::wstring();
}

bool ReadData(Robo& r) noexcept {
    void* o=r.obj;
    // edf41_robo_events: [[state, clip, [[t, kind, text, a, b, c], ...]], ...]
    const SgoNode* ev=Key(o,L"edf41_robo_events");
    for(int i=0;i<Count(ev);++i) {
        const SgoNode* e=Child(ev,i);
        ClipEvents ce;
        ce.state=TextOf(Child(e,0));
        std::int32_t clip=0;
        if(ce.state.empty() || !Int(Child(e,1),&clip))return false;
        ce.clip=clip;
        ce.stateIndex=StateIndex(o,ce.state.c_str());
        const SgoNode* keys=Child(e,2);
        if(Count(keys)<0 || Count(keys)>kMostEvents)return false;
        for(int k=0;k<Count(keys);++k) {
            const SgoNode* key=Child(keys,k);
            Event x{};
            std::int32_t kind=0;
            if(!Float(Child(key,0),&x.t) || !Int(Child(key,1),&kind) || !Float(Child(key,3),&x.a) || !Float(Child(key,4),&x.b)
               || !Float(Child(key,5),&x.c))return false;
            x.kind=kind;
            x.text=TextOf(Child(key,2));
            ce.events.push_back(std::move(x));
        }
        r.clipEvents.push_back(std::move(ce));
    }
    // edf41_robo_motion: [[state, length, [x, z, yaw, ...]], ...]
    const SgoNode* mo=Key(o,L"edf41_robo_motion");
    for(int i=0;i<Count(mo);++i) {
        const SgoNode* e=Child(mo,i);
        Motion m;
        m.state=TextOf(Child(e,0));
        const SgoNode* keys=Child(e,2);
        const int n=Count(keys);
        if(m.state.empty() || !Float(Child(e,1),&m.length) || n<3 || n%3 || n>kMostMotionKeys*3)return false;
        m.keys.resize(static_cast<std::size_t>(n));
        for(int k=0;k<n;++k)
            if(!Float(Child(keys,k),&m.keys[static_cast<std::size_t>(k)]))return false;
        m.stateIndex=StateIndex(o,m.state.c_str());
        r.motions.push_back(std::move(m));
    }
    // robo_attack_table: [[[state], [dmin, dmax], [amin, amax]], ...]
    const SgoNode* at=Key(o,L"robo_attack_table");
    for(int i=0;i<Count(at);++i) {
        const SgoNode* e=Child(at,i);
        AttackEntry a{};
        a.state=TextOf(Child(Child(e,0),0));
        float d[2],an[2];
        if(a.state.empty() || !Vec(Child(e,1),d,2) || !Vec(Child(e,2),an,2))return false;
        a.dmin=d[0]; a.dmax=d[1]; a.amin=an[0]; a.amax=an[1];
        a.stateIndex=StateIndex(o,a.state.c_str());
        r.attacks.push_back(a);
    }
    if(r.clipEvents.empty() || r.motions.empty() || r.attacks.empty() || !Key(o,L"edf41_se"))return false;
    // CAS states (4.1 0x2732B0)
    r.stShip=StateIndex(o,L"ship"); r.stTransform=StateIndex(o,L"transform"); r.stStand=StateIndex(o,L"stand");
    r.stFall=StateIndex(o,L"fall"); r.stFallLand=StateIndex(o,L"fall_land"); r.stDead=StateIndex(o,L"dead");
    const wchar_t* const f[5]={L"walk_f_nl",L"walk_f_lr",L"walk_f_rl",L"walk_f_ln",L"walk_f_rn"};
    const wchar_t* const b[5]={L"walk_b_nl",L"walk_b_lr",L"walk_b_rl",L"walk_b_ln",L"walk_b_rn"};
    for(int i=0;i<5;++i) { r.stWalkF[i]=StateIndex(o,f[i]); r.stWalkB[i]=StateIndex(o,b[i]); }
    r.stSideL=StateIndex(o,L"side_step_l"); r.stSideR=StateIndex(o,L"side_step_r");
    const wchar_t* const tl[3]={L"turn_l45",L"turn_l90",L"turn_l180"};
    const wchar_t* const tr[3]={L"turn_r45",L"turn_r90",L"turn_r180"};
    for(int i=0;i<3;++i) { r.stTurnL[i]=StateIndex(o,tl[i]); r.stTurnR[i]=StateIndex(o,tr[i]); }
    return r.stShip>=0 && r.stTransform>=0 && r.stStand>=0 && r.stFall>=0 && r.stFallLand>=0;
}

// A gun group's muzzles: [[locator, aim bone or 0], ...] (4.1 0x27B0A0; the aim bone binds only when it is a string).
bool ReadMuzzles(Robo& r,Gun& g,const wchar_t* key) noexcept {
    const SgoNode* m=Key(r.obj,key);
    g.muzzles=0;
    for(int i=0;i<Count(m) && i<2;++i) {
        Muzzle& z=g.muzzle[g.muzzles];
        z.locator=TextOf(Child(Child(m,i),0));
        z.aimBone=TextOf(Child(Child(m,i),1));
        if(z.locator.empty())return false;
        z.found=FindLocator(Model(r.obj),z.locator.c_str(),&z.loc);
        if(!z.found)Log("EDF41ROBO %p muzzle %ls of %ls: no such locator",r.obj,z.locator.c_str(),key);
        ++g.muzzles;
    }
    return g.muzzles>0;
}

// The bullet class and custom parameter of a group (keys <prefix>_bullet_class / _custom_param).
bool ReadBullet(Robo& r,Gun& g,const wchar_t* cls,const wchar_t* custom) noexcept {
    if(!Text(Key(r.obj,cls),g.factory,32))return false;
    g.bullet.factory=g.factory;
    g.bullet.custom=Persist(Key(r.obj,custom));
    return g.bullet.custom!=nullptr;
}

// ---- level and the bullets' numbers (4.1 0x2744B0 / 0x2790F0) ------------------------------------------------------
void SetLevel(Robo& r) noexcept {
    r.level=LevelS(r.obj)*10.0f;   // GameObjectBase +0x49C (SetLevel's difficulty-scaled level; 10 = level 1)
    if(Cfg().edf41RoboLevel>0.0f)r.level=Cfg().edf41RoboLevel;
    r.s=r.level/10.0f;
    const float s=r.s;
    // T_atk (4.1's difficulty table x players) is EDF6's own level here, as for the dragon and Hector: 1.
    Gun& sm=r.gun[kSmall];
    sm.bullet.speed=2.0f; sm.bullet.life=180; sm.bullet.damage=(std::max)(1.0f,0.5f*s); sm.bullet.size=3.2f;
    const float k=0.015f*r.level;
    const float smc[4]={1.0f*k,0.25f*k,0.5f*k,1.0f*k};
    std::memcpy(sm.bullet.color,smc,sizeof(smc));
    sm.aimLimit=0.5f; sm.spread=(std::max)(0.05f,0.25f/s); sm.interval=5; sm.duration=300; sm.charge=180;
    sm.chargeSe=L"ロボ小ビームチャージ"; sm.shotSe=L"ロボ小ビーム発射"; sm.look=kChargeSmall; sm.hitSe=L"ロボビーム着弾小"; sm.flashLife=10; sm.flashOffset=0.0f;
    Gun& md=r.gun[kMiddle];
    md.bullet.speed=2.0f; md.bullet.life=600; md.bullet.damage=(std::max)(1.0f,30.0f*s); md.bullet.explosion=9.0f+s; md.bullet.size=40.0f;
    const float mdc[4]={0.25f,0.5f,1.5f,1.0f};
    std::memcpy(md.bullet.color,mdc,sizeof(mdc));
    md.aimLimit=0.2f; md.spread=0.1f; md.interval=2; md.duration=1;
    md.charge=(std::max)(20,static_cast<int>(180.0f*(1.0f+0.1f*(s-1.0f))));
    md.chargeSe=L"ロボ飛行形態中型キャノンチャージ"; md.shotSe=L"ロボ肩ビーム発射"; md.look=kChargeMiddle; md.hitSe=L"ロボビーム着弾中"; md.flashLife=35; md.flashOffset=0.0f;
    Gun& lg=r.gun[kLarge];
    lg.bullet.speed=10.0f; lg.bullet.life=600; lg.bullet.damage=(std::max)(1.0f,40.0f*s); lg.bullet.explosion=(std::min)(40.0f,36.0f+4.0f*s);
    lg.bullet.size=40.0f;
    const float lgc[4]={0.5f,0.5f,2.0f,1.0f};
    std::memcpy(lg.bullet.color,lgc,sizeof(lgc));
    lg.aimLimit=0.0f; lg.spread=0.1f; lg.interval=31; lg.duration=600; lg.charge=300;
    lg.chargeSe=nullptr; lg.shotSe=L"ロボバスター砲発射"; lg.look=kChargeLarge; lg.hitSe=L"ロボビーム着弾大"; lg.flashLife=60; lg.flashOffset=5.0f;
    Gun& gn=r.gun[kGenocide];
    gn.bullet.speed=0.1f; gn.bullet.life=6000; gn.bullet.damage=(std::max)(1.0f,80.0f*s); gn.bullet.explosion=100.0f; gn.bullet.size=120.0f;
    const float gnc[4]={5.0f,50.0f,20.0f,1.0f};
    std::memcpy(gn.bullet.color,gnc,sizeof(gnc));
    gn.aimLimit=0.0f; gn.spread=0.0f; gn.interval=30; gn.duration=1; gn.charge=346;
    gn.chargeSe=nullptr; gn.shotSe=L"ロボバスター砲発射"; gn.look=kChargeGenocide; gn.hitSe=L"ロボビーム着弾大"; gn.flashLife=0; gn.flashOffset=0.0f;
    // the timers (4.1 0x2790F0: x / s truncated)
    auto per=[s](float x) { return static_cast<int>(x/s); };
    r.tSmall={RandInt(r,720),180,720,0.0f,500.0f};
    r.tMiddle={RandInt(r,per(480.0f)),per(300.0f),per(480.0f),150.0f,800.0f};
    r.tBuster={RandInt(r,per(3600.0f)),per(1800.0f),per(3600.0f),0.0f,1000.0f};
    r.tAttack={RandInt(r,per(1800.0f)),per(600.0f),per(1800.0f),-1.0f,-1.0f};
}

// 4.1 0x271AB0: counts down only while the distance is inside [dmin, dmax] (dmin < 0: always); true when it runs out (and
// starts again at min + rand(max - min)).
bool TickTimer(Robo& r,Timer& t,float distance) noexcept {
    if(t.dmin>=0.0f && (distance<t.dmin || distance>t.dmax))return false;
    if(--t.count>0)return false;
    t.count=t.min+RandInt(r,t.max-t.min);
    return true;
}

// ---- AI parameters (4.1 0x2797F0 / 0x279AC0) ---------------------------------------------------------------------
void RerollSide(Robo& r) noexcept {
    r.sideFrames=Rand(r)<0.5f ? 600+RandInt(r,600) : 0;
    r.sideAmount=(Rand(r)<0.5f ? 0.25f : -0.25f)*r.aiSpeed;
    r.sideInterval=r.form==1 && r.approach ? 0 : RandInt(r,600);
}
void Reroll(Robo& r) noexcept {
    r.phase1=1200+RandInt(r,600);
    r.radius=r.form==1 && r.approach ? 50.0f+150.0f*Rand(r) : 300.0f+200.0f*Rand(r);
    r.phase2=600+RandInt(r,300);
    r.cruise[0]=0.0f; r.cruise[1]=0.0f; r.cruise[2]=r.aiSpeed;
    if(r.form==1 && Rand(r)<(r.approach ? 0.5f : 1.0f/3.0f))r.cruise[2]=-r.aiSpeed;
    r.cruiseTurnFrames=RandInt(r,300);
    r.cruiseTurnRate=Rand(r)*0.015f-0.0075f;
}

// ---- guns (4.1 0x27BD70 / 0x27BE60 / 0x27C0F0 / 0x27B610) ------------------------------------------------------------
// The charge effects of a group: made on entering G1 (one per muzzle, EDF41 0x27BED5), stepped each frame at the muzzle
// (0x27BFEB), deleted on leaving G1 (0x27C07A).
void ChargeOff(Gun& g) noexcept {
    for(int i=0;i<g.muzzles;++i) {
        Muzzle& z=g.muzzle[i];
        if(z.charge && WeakAlive(z.chargeRef))p6::Delete(z.charge);
        z.charge=nullptr;
        WeakReset(z.chargeRef);
    }
}
void ChargeOn(Robo& r,Gun& g,int group) noexcept {
    ChargeOff(g);
    static std::atomic<int> made{0};
    for(int i=0;i<g.muzzles;++i) {
        Muzzle& z=g.muzzle[i];
        float m[16];
        if(!z.found || !LocatorMatrix(z.loc,m))continue;
        alignas(16) unsigned char param[0xA0]={};
        const void* vt=image+p6::kInitParamBase;
        std::memcpy(param,&vt,8);
        std::memcpy(param+0x30,g.look.k,sizeof(g.look.k));
        for(std::size_t at=0x60;at<=0x80;at+=0x10)std::memcpy(param+at,g.look.colour,16);
        z.charge=p6::MakeCharge(m,param,z.chargeRef) ? z.chargeRef.object : nullptr;
        if(!z.charge || !WeakAlive(z.chargeRef)) {
            static std::atomic<int> said{0};
            if(said.fetch_add(1)<4)Log("EDF41ROBO %p the charge effect UfoRoboCanonChargeEffect was not made: object %p, weak control %p",r.obj,z.charge,z.chargeRef.control);
            z.charge=nullptr;
            WeakReset(z.chargeRef);
            continue;
        }
        if(made.fetch_add(1)<6)Log("EDF41ROBO %p %s gun charge effect %p at %ls (%.0f %.0f %.0f)",r.obj,kGunNames[group],z.charge,z.locator.c_str(),m[12],m[13],m[14]);
    }
}
void ChargeTick(Robo& r,Gun& g) noexcept {
    for(int i=0;i<g.muzzles;++i) {
        Muzzle& z=g.muzzle[i];
        float m[16];
        if(!z.charge || !WeakAlive(z.chargeRef) || !LocatorMatrix(z.loc,m))continue;
        p6::ChargeStep(z.charge,m,r.dt);
    }
}

void GunEnter(Robo& r,Gun& g,int state) noexcept {
    if(g.state==kG1 && state==kG2) {   // the flash takes the charge's strength (4.1 0x27C043)
        const Muzzle& z=g.muzzle[0];
        const char* e=static_cast<const char*>(z.charge);
        if(e && WeakAlive(z.chargeRef) && Readable(e+0x4E4,4))std::memcpy(&g.flashSize,e+0x4E4,4);
        if(!std::isfinite(g.flashSize) || g.flashSize<0.0f)g.flashSize=0.0f;
    }
    if(g.state==kG1 && state!=kG1)ChargeOff(g);
    g.state=state;
    g.frames=0;
    if(state==kG1)ChargeOn(r,g,static_cast<int>(&g-r.gun));
    if(state==kG1 && g.chargeSe) {   // 4.1: the charge sound at the muzzles' centre
        float c[3]={r.pos[0],r.pos[1],r.pos[2]},m[16];
        int n=0;
        float sum[3]={};
        for(int i=0;i<g.muzzles;++i)
            if(g.muzzle[i].found && LocatorMatrix(g.muzzle[i].loc,m)) { sum[0]+=m[12]; sum[1]+=m[13]; sum[2]+=m[14]; ++n; }
        if(n) { c[0]=sum[0]/n; c[1]=sum[1]/n; c[2]=sum[2]/n; }
        PlaySe(r,g.chargeSe,c);
    }
    if(state==kG2)g.counter=0;
}

// A unit vector within `cone` rad of `axis` (4.1 0x51B7C0: angle uniform in [0, cone], azimuth uniform).
void Scatter(Robo& r,const float* axis,float cone,float* out) noexcept {
    float a[3]={axis[0],axis[1],axis[2]};
    const float l=Len3(a);
    if(l<1e-6f) { out[0]=0.0f; out[1]=0.0f; out[2]=1.0f; return; }
    a[0]/=l; a[1]/=l; a[2]/=l;
    if(cone<=0.0f) { std::memcpy(out,a,12); return; }
    float u[3]={0.0f,1.0f,0.0f};
    if(std::fabs(a[1])>0.9f) { u[0]=1.0f; u[1]=0.0f; }
    float x[3]={u[1]*a[2]-u[2]*a[1],u[2]*a[0]-u[0]*a[2],u[0]*a[1]-u[1]*a[0]};
    const float xl=Len3(x);
    x[0]/=xl; x[1]/=xl; x[2]/=xl;
    const float y[3]={a[1]*x[2]-a[2]*x[1],a[2]*x[0]-a[0]*x[2],a[0]*x[1]-a[1]*x[0]};
    const float t=cone*Rand(r),p=2.0f*kPi*Rand(r);
    const float st=std::sin(t),ct=std::cos(t),sp=std::sin(p),cp=std::cos(p);
    for(int k=0;k<3;++k)out[k]=a[k]*ct+(x[k]*cp+y[k]*sp)*st;
}

// One round from muzzle i: towards the target (no target: 1000 m ahead of the muzzle), turned from the muzzle's axis by at
// most aimLimit, then scattered within the group's cone.
void FireOne(Robo& r,Gun& g,int i,int group) noexcept {
    Muzzle& z=g.muzzle[i];
    float m[16];
    if(!z.found || !LocatorMatrix(z.loc,m)) { Fail("a muzzle locator gives no matrix"); return; }
    const float* p=m+12;
    float axis[3]={m[8],m[9],m[10]};
    const float al=Len3(axis);
    if(al<1e-6f)return;
    axis[0]/=al; axis[1]/=al; axis[2]/=al;
    float to[3]={axis[0],axis[1],axis[2]};
    if(r.haveTarget) {
        to[0]=r.target[0]-p[0]; to[1]=r.target[1]-p[1]; to[2]=r.target[2]-p[2];
        const float tl=Len3(to);
        if(tl>1e-3f) { to[0]/=tl; to[1]/=tl; to[2]/=tl; }
    }
    // turn the axis towards `to` by at most aimLimit
    float dir[3];
    const float cosA=(std::max)(-1.0f,(std::min)(1.0f,axis[0]*to[0]+axis[1]*to[1]+axis[2]*to[2]));
    const float angle=std::acos(cosA);
    if(angle<=g.aimLimit || angle<1e-5f)std::memcpy(dir,to,12);
    else {
        const float t=g.aimLimit/angle,so=std::sin(angle);
        const float a=std::sin((1.0f-t)*angle)/so,b=std::sin(t*angle)/so;
        for(int k=0;k<3;++k)dir[k]=axis[k]*a+to[k]*b;
    }
    float shot[3];
    Scatter(r,dir,g.spread,shot);
    float mm[16];
    AimMatrix(p,shot,mm);
    if(FireSe(r.obj,mm,g.bullet,r.obj,g.hitSe)) {
        ++g.shots;
        if(g.shots<=2 || g.shots%200==0)
            Log("EDF41ROBO %p %s gun round #%u from %ls at %.0f %.0f %.0f: %ls speed %.2f life %d damage %.1f blast %.1f size %.1f; flash %d frames size %.1f",r.obj,
                kGunNames[group],g.shots,z.locator.c_str(),p[0],p[1],p[2],g.factory,g.bullet.speed,g.bullet.life,g.bullet.damage,
                g.bullet.explosion,g.bullet.size,g.flashLife,g.flashSize);
    }
    // the flash, out along the muzzle's +Z by the group's offset + the template's +0x38 (4.1 0x27C313..0x27C3E5)
    const float out=g.flashOffset+g.look.k[4];
    const float at[3]={p[0]+m[8]*out,p[1]+m[9]*out,p[2]+m[10]*out};
    p6::MuzzleFlash(at,g.flashLife,g.flashSize,g.look.colour);
}

void GunTick(Robo& r,Gun& g,int group) noexcept {
    ++g.frames;
    switch(g.state) {
    case kG0:
        if(g.autoFire && !(r.flags&1) && !NetFollows(r) && --g.autoCount<=0) {   // online: a guest copy's start comes from the host (NetGun)
            g.autoCount=g.autoMin+RandInt(r,g.autoMax-g.autoMin);
            GunEnter(r,g,kG1);
            NetGun(r,group);
        }
        break;
    case kG1:
        ChargeTick(r,g);
        if(g.frames>=g.charge)GunEnter(r,g,kG2);
        break;
    case kG2:
        if(g.counter--==0) {
            g.counter=g.interval;
            float c[3]={},m[16];
            int n=0;
            for(int i=0;i<g.muzzles;++i) {
                FireOne(r,g,i,group);
                if(g.muzzle[i].found && LocatorMatrix(g.muzzle[i].loc,m)) { c[0]+=m[12]; c[1]+=m[13]; c[2]+=m[14]; ++n; }
            }
            if(n) { c[0]/=n; c[1]/=n; c[2]/=n; PlaySe(r,g.shotSe,c); }   // once a round, at the muzzles' centre (4.1 0x27B500)
        }
        if(g.duration>=0 && g.frames>g.duration)GunEnter(r,g,kG0);
        break;
    }
}

// ---- CAS ------------------------------------------------------------------------------------------------------------
bool Play(Robo& r,int state,const wchar_t* why) noexcept {
    if(state<0)return false;
    int before=-1;
    float t=0.0f,len=0.0f;
    ClipNow(r.obj,&before,&t,&len);
    const bool ok=PlayStateIndex(r.obj,state);
    if(ok && before==state)ClipRewind(r.obj);   // the same state again (a second walk_f_lr): from its start
    r.evState=-2;     // the events start over with the new clip
    if(r.logged<400) { ++r.logged; Log("EDF41ROBO %p plays %ls (state %d) %s",r.obj,why,state,ok ? "" : "REFUSED"); }
    return ok;
}
bool ClipEnded(Robo& r) noexcept {
    int st=0;
    float t=0.0f,len=0.0f;
    if(!ClipNow(r.obj,&st,&t,&len))return true;
    return t>=len-1e-3f;
}

// ---- the body state machine -------------------------------------------------------------------------------------------
void ToRobot(Robo& r) noexcept;
void BodyEnter(Robo& r,int s) noexcept;

void BodySet(Robo& r,int s) noexcept {
    // exits
    if(r.body==kS9)GunEnter(r,r.gun[kLarge],kG0);
    if(r.logged<400) { ++r.logged; Log("EDF41ROBO %p body %s -> %s (form %d, ai %s)",r.obj,kBodyNames[r.body],kBodyNames[s],r.form,kAiNames[r.ai]); }
    r.body=s;
    r.bodyFrames=0;
    BodyEnter(r,s);
    NetBody(r);   // online: the deciding copy tells the guests (edf41net)
}

const Motion* MotionOf(const Robo& r,int state) noexcept {
    for(const Motion& m:r.motions)
        if(m.stateIndex==state)return &m;
    return nullptr;
}

// S8: a step or turn clip, the pose it starts from kept (4.1 +0x324 / +0x328).
void Step(Robo& r,int state,int chain) noexcept {
    r.pendingState=state;
    r.chain=chain;
    BodySet(r,kS8);
}

void BodyEnter(Robo& r,int s) noexcept {
    switch(s) {
    case kS0:
        r.flags=0;
        Play(r,r.stShip,L"ship");
        for(int g=kSmall;g<=kLarge;++g)GunEnter(r,r.gun[g],kG0);
        break;
    case kS1:
        r.flags=0;
        if(r.gun[kSmall].state==kG0)GunEnter(r,r.gun[kSmall],kG1);
        break;
    case kS2:
        r.flags=0;
        if(r.gun[kMiddle].state==kG0)GunEnter(r,r.gun[kMiddle],kG1);
        break;
    case kS3: {
        r.flags=8;
        PlaySe(r,L"ロボ飛行形態バスター砲チャージ",r.pos);
        Gun& g=r.gun[kLarge];
        g.charge=300; g.interval=31; g.duration=600; g.aimLimit=0.0f; g.spread=0.1f; g.look=kChargeLarge;
        GunEnter(r,g,kG1);
        break;
    }
    case kS4:
        r.flags=9;
        Play(r,r.stTransform,L"transform");
        break;
    case kS5:
        r.flags=9;
        ToRobot(r);
        r.landed=false;
        Play(r,r.stFall,L"fall");
        break;
    case kS6:
        r.flags=9;
        Play(r,r.stFallLand,L"fall_land");
        break;
    case kS7:
        r.flags=0;
        break;
    case kS8:
        r.flags=0;
        r.stepYaw=r.yaw;
        std::memcpy(r.stepPos,r.pos,12);
        r.stepMotion=MotionOf(r,r.pendingState);
        r.stepState=r.pendingState;
        Play(r,r.pendingState,L"a step");
        break;
    case kS9:
        r.flags=0;
        r.stepMotion=nullptr;
        Play(r,r.pendingState,L"an attack");
        break;
    case kS10: {   // 4.1 0x278DD0: the ragdoll has it (OnDamaged turned it on), guns idle, 8 + 7 rand s
        r.flags=1;
        for(Gun& g:r.gun)GunEnter(r,g,kG0);
        r.dieFrames=static_cast<int>((8.0f+7.0f*Rand(r))*60.0f);
        r.stepMotion=nullptr;
        if(r.form==1 && !r.ragdoll)Play(r,r.stDead,L"dead");   // no ragdoll to hand it to: the clip instead
        if(r.ragdoll)p6::CasRate(r.obj,0.0f);   // the animation stops driving the bones
        p6::LightsPlay(r.lights,1,1);   // 4.1 0x278DEB: the second light animation, once
        break;
    }
    case kS11: {   // 4.1 0x278FB0: Explosion_Big at the ragdoll's first body, along the object's up row, 1.0 / 2.0; se_table[2]
        r.flags=1;
        float up[3]={0.0f,1.0f,0.0f},at[3]={r.pos[0],r.pos[1],r.pos[2]};
        const char* o=static_cast<const char*>(r.obj);
        if(Readable(o+0x70,12))std::memcpy(up,o+0x70,12);
        const p6::Bodies b=p6::RagdollBodies(r.obj);
        if(r.ragdoll && b.count)p6::BodyPosition(p6::BodyHandle(b,0),at);
        BigExplosion(at,up,1.0f,2.0f);
        PlaySe(r,L"ロボ死亡",at);
        break;
    }
    default:
        break;
    }
}

// Feet: 0 together, 1 the left ahead by more than 10 along the body's forward, 2 the right (4.1 0x277F20).
int FootPhase(Robo& r) noexcept {
    float l[16],rr[16];
    if(!LocatorMatrix(r.footL,l) || !LocatorMatrix(r.footR,rr))return 0;
    const float f[3]={std::sin(r.yaw),0.0f,std::cos(r.yaw)};
    const float d=(l[12]-rr[12])*f[0]+(l[14]-rr[14])*f[2];
    return d>10.0f ? 1 : d<-10.0f ? 2 : 0;
}

void BodyTick(Robo& r) noexcept {
    ++r.bodyFrames;
    switch(r.body) {
    case kS0:
        if(r.cmdTransform)BodySet(r,kS4);
        else if(r.cmdSmall)BodySet(r,kS1);
        else if(r.cmdMiddle)BodySet(r,kS2);
        else if(r.cmdBuster)BodySet(r,kS3);
        break;
    case kS1: if(r.gun[kSmall].state==kG0)BodySet(r,kS0); break;
    case kS2: if(r.gun[kMiddle].state==kG0)BodySet(r,kS0); break;
    case kS3: if(r.gun[kLarge].state==kG0)BodySet(r,kS0); break;
    case kS4: if(r.bodyFrames>2 && ClipEnded(r))BodySet(r,kS5); break;
    case kS5: if(r.landed)BodySet(r,kS6); break;
    case kS6: if(r.bodyFrames>2 && ClipEnded(r))BodySet(r,kS7); break;
    case kS7: {   // 4.1 0x278810
        // the move: 0 none (|x| and |z| under 0.5), 1 / 2 sideways (x >= 0 / x < 0), 3 / 4 forward / back (z >= 0 / z < 0)
        const float ax=std::fabs(r.cmdX),az=std::fabs(r.cmdZ);
        const int mv=(ax<0.5f && az<0.5f) ? 0 : ax>az ? (r.cmdX<0.0f ? 2 : 1) : (r.cmdZ<0.0f ? 4 : 3);
        const int phase=FootPhase(r);
        if(phase==1 || phase==2) {   // a foot ahead: carry on (lr / rl) or bring it back (ln / rn); +0x328 the backward chain
            const int on=phase==1 ? 1 : 2,home=phase==1 ? 3 : 4;
            if(mv==3 && r.chain==0)Step(r,r.stWalkF[on],0);
            else if(mv==4 && r.chain==1)Step(r,r.stWalkB[on],1);
            else if(r.chain==0)Step(r,r.stWalkF[home],0);
            else Step(r,r.stWalkB[home],1);
            break;
        }
        // feet together: the move's first step, then a turn replaces it, else an attack does (the same frame, in this order)
        if(mv==3)Step(r,r.stWalkF[0],0);
        else if(mv==4)Step(r,r.stWalkB[0],1);
        else if(mv==1)Step(r,r.stSideL,0);
        else if(mv==2)Step(r,r.stSideR,0);
        if(r.turn) {
            const int k=(std::min)(3,std::abs(r.turn))-1;
            if(r.body==kS8)r.body=kS7;   // the step just chosen is replaced
            Step(r,r.turn>0 ? r.stTurnL[k] : r.stTurnR[k],0);
        } else if(r.cmdAttack>=0 && r.cmdAttack<static_cast<int>(r.attacks.size())) {
            r.pendingState=r.attacks[static_cast<std::size_t>(r.cmdAttack)].stateIndex;
            BodySet(r,kS9);
        }
        break;
    }
    case kS8:
    case kS9:
        if(r.bodyFrames>2 && ClipEnded(r))BodySet(r,kS7);
        break;
    case kS10:
        if(r.ragdoll && !r.contact.on && r.bodyFrames==2)p6::ContactOn(r.contact,r.obj,Key(r.obj,L"ragdoll_contact"),r.obj);
        if(r.contact.on) {
            const int cd=p6::ContactTick(r.contact);
            if(cd>0 && r.contactSeen==0) { Log("EDF41ROBO %p ragdoll contact sound + quake fired at death frame %d (cooldown %d)",r.obj,r.bodyFrames,cd); r.contactSeen=1; }
        }
        if(r.ragdoll && r.bodyFrames%120==1) {   // where the ragdoll is: its first body, the lowest, the ground below the first
            const p6::Bodies b=p6::RagdollBodies(r.obj);
            float p0[3]={},low[3]={0.0f,1e9f,0.0f},q[3],hit[3]={};
            p6::BodyPosition(p6::BodyHandle(b,0),p0);
            std::size_t lowest=0;
            for(std::size_t i=0;i<b.count;++i)
                if(p6::BodyPosition(p6::BodyHandle(b,i),q) && q[1]<low[1]) { std::memcpy(low,q,12); lowest=i; }
            const float from[3]={p0[0],p0[1]+500.0f,p0[2]},to[3]={p0[0],p0[1]-500.0f,p0[2]};
            const bool ground=GroundHit(from,to,hit,static_cast<char*>(r.obj)+kEaoRagdoll);
            Log("EDF41ROBO %p ragdoll frame %d: %zu bodies, body 0 at %.0f %.0f %.0f, lowest #%zu at %.0f %.0f %.0f, ground below body 0 %s %.0f",r.obj,r.bodyFrames,b.count,
                p0[0],p0[1],p0[2],lowest,low[0],low[1],low[2],ground ? "at" : "none within 500 m:",hit[1]);
        }
        if(r.bodyFrames>=r.dieFrames)BodySet(r,kS11);
        break;
    case kS11:
        if(r.bodyFrames>=600 && !r.deleted) {   // 4.1 0x56BBC0 after 10 s
            r.deleted=true;
            Log("EDF41ROBO %p wreck removed",r.obj);
            DeleteObject(r.obj);
        }
        break;
    default:
        break;
    }
}

// 4.1 0x2780C0: robot form, the knee and shoulder guns firing on their own.
void ToRobot(Robo& r) noexcept {
    if(r.form==1)return;
    r.form=1;
    const float k=Cfg().edf41RoboGunEvery>0 ? 0.0f : 1.0f;   // dev: the guns' own short timers below
    Gun& sm=r.gun[kSmall];
    Gun& md=r.gun[kMiddle];
    sm.autoFire=true; sm.autoCount=0; sm.autoMin=static_cast<int>(900*k); sm.autoMax=static_cast<int>(1200*k);
    md.autoFire=true; md.autoCount=0; md.autoMin=static_cast<int>(300*k); md.autoMax=static_cast<int>(480*k);
    if(Cfg().edf41RoboGunEvery>0) { sm.autoMin=sm.autoMax=md.autoMin=md.autoMax=Cfg().edf41RoboGunEvery; }
    r.vel[0]=r.vel[1]=r.vel[2]=0.0f;
    r.roll=0.0f;
    Log("EDF41ROBO %p robot form",r.obj);
}

// ---- the AI ---------------------------------------------------------------------------------------------------------
// 4.1 0x2795C0: the ship turns at most 0.01 rad a frame towards the target; the robot asks for a turn clip by the angle.
void TurnTo(Robo& r,const float* point,float most) noexcept {
    const float d[3]={point[0]-r.pos[0],0.0f,point[2]-r.pos[2]};
    float l[3];
    ToLocal(r.yaw,d,l);
    const float a=(l[0]==0.0f && l[2]==0.0f) ? 0.0f : std::atan2(l[0],l[2]);
    if(r.form==0) { r.yawRate=(std::max)(-most,(std::min)(most,a)); return; }
    const float w=WrapPi(a);
    r.turn=w>2.19911f ? 3 : w<-2.19911f ? -3 : w>1.25664f ? 2 : w<-1.25664f ? -2 : w>0.628319f ? 1 : w<-0.628319f ? -1 : 0;
}
// 4.1 0x279420: the move command towards `point` + up, in the object's frame, at most `speed` long.
void MoveTo(Robo& r,const float* point,float up,float speed) noexcept {
    const float d[3]={point[0]-r.pos[0],point[1]+up-r.pos[1],point[2]-r.pos[2]};
    float l[3];
    ToLocal(r.yaw,d,l);
    const float n=Len3(l);
    const float k=n>speed ? speed/n : 1.0f;
    r.cmdX=l[0]*k; r.cmdY=l[1]*k; r.cmdZ=l[2]*k;
}

void Side(Robo& r) noexcept {   // 4.1 0x279FA1 (literally: a reroll every frame while the interval counter is above 0)
    if(r.sideFrames-->0)r.cmdX=r.sideAmount;
    else if(r.sideInterval-->0)RerollSide(r);
}

float TargetDistance(const Robo& r) noexcept {
    const float d[3]={r.target[0]-r.pos[0],r.target[1]-r.pos[1],r.target[2]-r.pos[2]};
    return Len3(d);
}

void AiSet(Robo& r,int a) noexcept {
    if(r.logged<400) { ++r.logged; Log("EDF41ROBO %p ai %s -> %s",r.obj,kAiNames[r.ai],kAiNames[a]); }
    r.ai=a;
    r.aiFrames=0;
    if(a==kA2) { Reroll(r); RerollSide(r); }
}

void ShipAi(Robo& r) noexcept {
    if(r.phase1-->0) {
        float p[3]={r.target[0],r.target[1],r.target[2]};
        float d[2]={r.pos[0]-r.target[0],r.pos[2]-r.target[2]};
        const float dl=std::sqrt(d[0]*d[0]+d[1]*d[1]);
        const float rad=r.radius>0.0f ? r.radius : 0.0f;
        if(dl>1e-4f) { p[0]+=d[0]/dl*rad; p[2]+=d[1]/dl*rad; }
        TurnTo(r,r.target,0.01f);
        MoveTo(r,p,250.0f,r.aiSpeed);
    } else if(r.phase2-->0) {
        r.cmdX=r.cruise[0]; r.cmdY=r.cruise[1]; r.cmdZ=r.cruise[2];
        if(r.cruiseTurnFrames-->0)r.yawRate=r.cruiseTurnRate;
    } else Reroll(r);
    Side(r);
    if(r.body!=kS0)return;
    const float dist=TargetDistance(r);
    const int every=Cfg().edf41RoboGunEvery;
    if(every>0) {   // dev: the three volleys in turn every `every` frames, whatever the distance
        if(r.aiFrames%every==0) {
            const int k=(r.aiFrames/every)%3;
            (k==0 ? r.cmdSmall : k==1 ? r.cmdMiddle : r.cmdBuster)=true;
        }
        return;
    }
    if(TickTimer(r,r.tSmall,dist))r.cmdSmall=true;
    if(TickTimer(r,r.tMiddle,dist))r.cmdMiddle=true;
    if(TickTimer(r,r.tBuster,dist))r.cmdBuster=true;
}

void RobotAi(Robo& r) noexcept {
    const float h[2]={r.target[0]-r.pos[0],r.target[2]-r.pos[2]};
    const float hd=std::sqrt(h[0]*h[0]+h[1]*h[1]);
    if(r.phase1-->0) {
        TurnTo(r,r.target,0.0f);
        if(r.approach)r.cmdZ=hd<r.radius ? -1.0f : 1.0f;
    } else if(r.phase2-->0) {
        r.cmdZ=r.cruise[2]>0.0f ? 1.0f : -1.0f;
    } else Reroll(r);
    Side(r);
    if(r.cmdX!=0.0f)r.cmdX=r.cmdX>0.0f ? 1.0f : -1.0f;
    const int forced=Cfg().edf41RoboAttack;
    if(forced>=0 && r.tAttack.count>600)r.tAttack.count=600;   // dev: an attack every 10 s
    if(TickTimer(r,r.tAttack,0.0f))AiSet(r,kA4);
}

// 4.1 0x27A770: the entries whose distance and angle windows hold the target, one of them at random.
void ChooseAttack(Robo& r) noexcept {
    const float d[3]={r.target[0]-r.pos[0],r.target[1]-r.pos[1],r.target[2]-r.pos[2]};
    float l[3];
    ToLocal(r.yaw,d,l);
    const float angle=WrapPi(std::atan2(l[0],l[2]));
    const float dist=Len3(d);
    int pick[16],n=0;
    for(std::size_t i=0;i<r.attacks.size() && n<16;++i) {
        const AttackEntry& a=r.attacks[i];
        if(dist>=a.dmin && dist<=a.dmax && angle>=a.amin && angle<=a.amax && a.stateIndex>=0)pick[n++]=static_cast<int>(i);
    }
    const int forced=Cfg().edf41RoboAttack;
    if(forced>=0 && forced<static_cast<int>(r.attacks.size()))pick[0]=forced, n=1;
    r.attackPick=n ? pick[RandInt(r,n)] : -1;
    Log("EDF41ROBO %p attack choice: target %.0f m at %.2f rad, %d entries fit -> %d %ls",r.obj,dist,angle,n,r.attackPick,
        r.attackPick>=0 ? r.attacks[static_cast<std::size_t>(r.attackPick)].state.c_str() : L"(none)");
}

void AiTick(Robo& r) noexcept {
    ++r.aiFrames;
    // the frame's command starts empty (4.1 0x279390), and stays so while the body holds it (0x276B42)
    r.cmdX=r.cmdY=r.cmdZ=r.yawRate=0.0f;
    r.turn=0;
    r.cmdTransform=r.cmdSmall=r.cmdMiddle=r.cmdBuster=false;
    r.cmdAttack=-1;
    float p[3];
    // GameObjectBase's target (the nearest hostile registered object, renewed every 60 frames: 4.1's base does the same at
    // 0x28D7F4), aimed at its first lock point; the player when the object has none yet.
    r.haveTarget=AimPoint(Target(r.obj),p) || NearestPlayer(r.pos,p);
    if(r.haveTarget)std::memcpy(r.target,p,12);
    switch(r.ai) {
    case kA0:
        AiSet(r,kA1);   // 4.1 waits for +0x260 (encountered); the script's CreateEnemy makes these active
        break;
    case kA1:
        if(r.form==1) { AiSet(r,kA2); break; }
        if(r.haveTarget)ShipAi(r);
        break;
    case kA2:
        AiSet(r,kA3);
        break;
    case kA3:
        if(r.haveTarget)RobotAi(r);
        break;
    case kA4:
        if(r.body==kS7) { ChooseAttack(r); AiSet(r,kA5); }
        break;
    case kA5:
        r.cmdAttack=r.attackPick;
        if(r.body!=kS7 || r.attackPick<0)AiSet(r,kA3);
        break;
    }
    if(r.flags&1) { r.cmdX=r.cmdY=r.cmdZ=r.yawRate=0.0f; r.turn=0; r.cmdAttack=-1; }
}

// ---- motion (4.1 slot 1, 0x2766A0) ------------------------------------------------------------------------------------
void Sample(const Motion& m,float t,float* out) noexcept {
    const std::size_t keys=m.keys.size()/3;
    const float f=m.length>0.0f ? (std::max)(0.0f,(std::min)(1.0f,t/m.length))*static_cast<float>(keys-1) : 0.0f;
    const std::size_t a=(std::min)(keys-1,static_cast<std::size_t>(f)),b=(std::min)(keys-1,a+1);
    const float w=f-static_cast<float>(a);
    for(int k=0;k<3;++k)out[k]=m.keys[a*3+k]*(1.0f-w)+m.keys[b*3+k]*w;
}

void Move(Robo& r) noexcept {
    if(r.body>=kS10 && r.ragdoll)return;   // the ragdoll has the bones; the object stays where it died (as ShootingTarget's does)
    if(r.form==0) {
        if(r.body==kS10) {   // a dead ship drops (4.1: its ragdoll falls; the plugin has no ragdoll to hand it to)
            r.fall-=0.008f;
            r.pos[1]+=r.fall;
            float hit[3];
            const float from[3]={r.pos[0],r.pos[1]+2.0f,r.pos[2]},to[3]={r.pos[0],r.pos[1],r.pos[2]};
            if(GroundHit(from,to,hit,static_cast<char*>(r.obj)+kEaoRagdoll)) { r.pos[1]=hit[1]; r.fall=0.0f; }
            return;
        }
        r.yaw=WrapPi(r.yaw+r.yawRate);
        const float l[3]={r.cmdX,r.cmdY,r.cmdZ};
        float w[3];
        ToWorld(r.yaw,l,w);
        for(int k=0;k<3;++k) { r.vel[k]+=(w[k]-r.vel[k])*0.0025f; r.pos[k]+=r.vel[k]/60.0f; }
        const float want=(std::max)(-0.9425f,(std::min)(0.9425f,-30.0f*r.yawRate));
        r.roll+=(want-r.roll)*0.1f;
        return;
    }
    // robot: root motion of the clip S8 plays, then the ground
    if(r.body==kS8 && r.stepMotion) {
        int st=0;
        float t=0.0f,len=0.0f;
        if(ClipNow(r.obj,&st,&t,&len) && st==r.stepState) {
            float k[3];
            Sample(*r.stepMotion,t,k);
            const float l[3]={k[0],0.0f,k[1]};
            float w[3];
            ToWorld(r.stepYaw,l,w);
            r.pos[0]=r.stepPos[0]+w[0];
            r.pos[2]=r.stepPos[2]+w[2];
            r.yaw=WrapPi(r.stepYaw+k[2]);
        }
    }
    float hit[3];
    // 4.1 casts from 100 m above the feet down to them (1 m below once landed). Its filter misses its own bodies; EDF6's map
    // ray does not, and a body the ray meets above the feet lifted the robot every frame (M411 real run 2026-10-15): the feet's
    // part (2 m above them down) first, then the rest of 4.1's segment (sunk into a slope: back up), own ragdoll bodies passed.
    void* ragdoll=static_cast<char*>(r.obj)+kEaoRagdoll;
    const float knee[3]={r.pos[0],r.pos[1]+2.0f,r.pos[2]},to[3]={r.pos[0],r.pos[1]-(r.landed ? 1.0f : 0.0f),r.pos[2]};
    const float top[3]={r.pos[0],r.pos[1]+100.0f,r.pos[2]};
    if(GroundHit(knee,to,hit,ragdoll) || GroundHit(top,knee,hit,ragdoll)) {
        r.pos[1]=hit[1];
        r.fall=0.0f;
        r.landed=true;
    } else {
        r.landed=false;
        r.fall-=0.008f;
        r.pos[1]+=r.fall;
    }
}

void WriteMatrix(Robo& r) noexcept {
    char* o=static_cast<char*>(r.obj);
    if(!Readable(o+kMatrixAt,64))return;
    const float cy=std::cos(r.yaw),sy=std::sin(r.yaw),cr=std::cos(r.roll),sr=std::sin(r.roll);
    // Rz(roll) * Ry(yaw), rows x y z, times the creation scale (CreateEnemy's size argument)
    const float x[3]={cy,0.0f,-sy},y[3]={0.0f,1.0f,0.0f},z[3]={sy,0.0f,cy};
    float m[16]={};
    for(int k=0;k<3;++k) {
        m[k]=(x[k]*cr+y[k]*sr)*r.scale;
        m[4+k]=(-x[k]*sr+y[k]*cr)*r.scale;
        m[8+k]=z[k]*r.scale;
    }
    m[12]=r.pos[0]; m[13]=r.pos[1]; m[14]=r.pos[2]; m[15]=1.0f;
    std::memcpy(o+kMatrixAt,m,sizeof(m));
}

// ---- the clips' MAB events -------------------------------------------------------------------------------------------
void RaiseEvent(Robo& r,const Event& e) noexcept {
    ++r.events;
    switch(e.kind) {
    case kEvSe:
        PlaySe(r,e.text.c_str(),r.pos);
        break;
    case kEvSeStop:
        break;   // 4.1 stops the channel's loop (ロボ３６０発射); the cues the plugin plays are one-shots
    case kEvHit: {   // 4.1 0x277790: damage x s at the ground under the locator, radius scale x the locator's size
        LocatorHandle l{};
        float m[16];
        if(!FindLocator(Model(r.obj),e.text.c_str(),&l) || !LocatorMatrix(l,m)) { Fail("an attack range locator is missing"); break; }
        // the locator's size: a kind-0 MAB record keeps its radius as a float at +0x10 (mab_legacy.Locator.b; 足歩行衝撃 20,
        // 手攻撃範囲 50); 4.1's 0x277B48 multiplies by it (the M411 run of 13:03 had a 1 m circle from the matrix's row length)
        float size=1.0f;
        const char* rec=static_cast<const char*>(l.entry);
        if(rec && Readable(rec+8,12) && *reinterpret_cast<const std::int32_t*>(rec+8)==0) {
            std::memcpy(&size,rec+0x10,4);
            if(!(size>0.0f && size<1000.0f))size=1.0f;
        }
        const float radius=e.b*size,damage=e.a*r.s;
        // 4.1 0x277A83: everything happens at the ground under the locator (a map ray from 50 m above it to 50 m below); no
        // ground there, nothing at all (0x277A8C)
        float c[3];
        const float from[3]={m[12],m[13]+50.0f,m[14]},to[3]={m[12],m[13]-50.0f,m[14]};
        const bool ground=GroundHit(from,to,c,static_cast<char*>(r.obj)+kEaoRagdoll);
        bool any=false,sent=false;
        if(ground) {
            Quake(c,e.c,radius*10.0f);           // 4.1 0x5689F0 (message 0x80000018), quake default 0.2
            Smoke(c,e.b*size*0.7f);              // 4.1 0x1B7140, the dust: scale x size x 0.7
            // The damage: EDF6's own area damage (ApplyAreaDamage 0x542670, team = the robot's, the robot itself skipped): every
            // hostile object in the circle (players, EDF NPCs, their vehicles) takes it; nothing is drawn, as 4.1's 0x28B930.
            // 2 m up: ApplyAreaDamage drops a target its occlusion ray (centre -> the target's nearest point, 0x542F47) finds
            // blocked, and a ray from a point on the ground meets the ground (no hit at all on the 13:33 M411 run)
            const float lifted[3]={c[0],c[1]+2.0f,c[2]};
            sent=p6::AreaDamage(r.obj,lifted,radius,damage,2,&any);
        }
        if(any)++r.blasts;
        if(++r.hits<=24 || (any && r.blasts<=24)) {
            float pl[3]={};
            const bool seen=NearestPlayer(c,pl);
            const float dp[3]={pl[0]-c[0],pl[1]-c[1],pl[2]-c[2]},dt[3]={r.target[0]-c[0],r.target[1]-c[1],r.target[2]-c[2]};
            if(ground)Log("EDF41ROBO %p hit %s at the ground %.0f %.0f %.0f radius %.1f damage %.1f: area damage %s%s (player %.0f m, target %.0f m away)",r.obj,U8(e.text).text,
                          c[0],c[1],c[2],radius,damage,sent ? "sent" : "NOT sent",any ? ", something was hit" : "",seen ? Len3(dp) : -1.0f,r.haveTarget ? Len3(dt) : -1.0f);
            else Log("EDF41ROBO %p hit %s at %.0f %.0f %.0f: no ground within 50 m, nothing (as 4.1)",r.obj,U8(e.text).text,m[12],m[13],m[14]);
        }
        break;
    }
    case kEvBeamStart: {   // 4.1 0x277C10: the hand guns fire until 砲撃終了
        Gun& g=r.gun[kLarge];
        g.aimLimit=0.0f; g.duration=-1; g.interval=30; g.spread=0.1f; g.charge=static_cast<int>(e.a); g.look=kChargeBeam;
        GunEnter(r,g,kG1);
        break;
    }
    case kEvBeamEnd:   // 4.1 0x277D90 (and 0x275680: the template back to the hands' own)
        GunEnter(r,r.gun[kLarge],kG0);
        r.gun[kLarge].look=kChargeLarge;
        break;
    case kEvGenocide: {   // 4.1 0x277DB0
        Gun& g=r.gun[kGenocide];
        g.aimLimit=0.0f; g.spread=0.0f; g.duration=1; g.interval=30; g.charge=static_cast<int>(e.a);
        GunEnter(r,g,kG1);
        break;
    }
    default:
        break;
    }
}

void Events(Robo& r) noexcept {
    int st=0;
    float t=0.0f,len=0.0f;
    if(!ClipNow(r.obj,&st,&t,&len))return;
    if(st!=r.evState) { r.evState=st; r.evClip=0; r.evTime=-1.0f; }
    else if(t+1e-3f<r.evTime)++r.evClip;   // the state's next clip (attack_fire_genocide: charge, then the shot)
    for(const ClipEvents& c:r.clipEvents) {
        if(c.stateIndex!=st || c.clip!=r.evClip)continue;
        for(const Event& e:c.events)
            if(e.t>r.evTime && e.t<=t)RaiseEvent(r,e);
    }
    r.evTime=t;
}

// ---- creation, update, messages ------------------------------------------------------------------------------------
bool Setup(Robo& r) noexcept {
    char* o=static_cast<char*>(r.obj);
    if(!Readable(o+kMatrixAt,64))return false;
    float m[16];
    std::memcpy(m,o+kMatrixAt,sizeof(m));
    std::memcpy(r.pos,m+12,12);
    r.scale=Len3(m+8);
    if(!(r.scale>0.01f && r.scale<100.0f))r.scale=1.0f;
    r.yaw=std::atan2(m[8],m[10]);
    r.rng=reinterpret_cast<std::uintptr_t>(r.obj)*0x9E3779B97F4A7C15ull^GetTickCount64();
    if(!ReadData(r)) { Log("EDF41ROBO %p: its SGO lacks the plugin's keys (edf41_robo_events / _motion / edf41_se / robo_attack_table) or CAS states: left as it is",r.obj); return false; }
    Gun& sm=r.gun[kSmall];
    Gun& md=r.gun[kMiddle];
    Gun& lg=r.gun[kLarge];
    Gun& gn=r.gun[kGenocide];
    const bool guns=ReadMuzzles(r,sm,L"robo_small_muzzle_setting") && ReadMuzzles(r,md,L"robo_middle_muzzle_setting")
        && ReadMuzzles(r,lg,L"robo_large_muzzle_setting") && ReadMuzzles(r,gn,L"robo_large_muzzle_setting")
        && ReadBullet(r,sm,L"small_bullet_class",L"small_bullet_custom_param") && ReadBullet(r,md,L"cannon_bullet_class",L"cannon_bullet_custom_param")
        && ReadBullet(r,lg,L"cannon_bullet_class",L"cannon_bullet_custom_param") && ReadBullet(r,gn,L"genocide_bullet_class",L"genocide_bullet_custom_param");
    if(!guns) { Log("EDF41ROBO %p: the gun keys are not what 4.1's SGO has: left as it is",r.obj); return false; }
    FindLocator(Model(r.obj),L"左足歩行衝撃",&r.footL);
    FindLocator(Model(r.obj),L"右足歩行衝撃",&r.footR);
    // ロックオン is in the MAB's locator list 1 (the lock-on list), not 0 (see p6)
    if(!p6::FindLocatorIn(Model(r.obj),1,L"ロックオン",&r.lockOn))FindLocator(Model(r.obj),L"ロックオン",&r.lockOn);
    void* skeleton=static_cast<char*>(r.obj)+kEaoSkeleton;
    if(!r.lockOn.entry) {
        r.lockBone=BoneIndex(skeleton,L"hara1");
        Log("EDF41ROBO %p: no ロックオン locator in list 1 or 0; the lock point follows the bone hara1 (%d)",r.obj,r.lockBone);
    }
    // the aim bones of the knee and shoulder guns (the second item of their muzzle settings)
    for(int k=kSmall;k<=kMiddle;++k)
        for(int i=0;i<r.gun[k].muzzles;++i) {
            Muzzle& z=r.gun[k].muzzle[i];
            if(!z.aimBone.empty())z.bone=BoneIndex(skeleton,z.aimBone.c_str());
        }
    // the foot IK: ik_control_info [[toe bone, CAS variable], ...]
    const SgoNode* ik=Key(o,L"ik_control_info");
    for(int i=0;i<Count(ik) && i<2;++i) {
        const std::wstring bone=TextOf(Child(Child(ik,i),0)),var=TextOf(Child(Child(ik,i),1));
        if(bone.empty() || var.empty())continue;
        r.ikBone[i]=BoneIndex(skeleton,bone.c_str());
        r.ikVar[i]=p6::CasVar(r.obj,var.c_str());
    }
    SetLevel(r);
    // The team. EventAnimationObject's constructor registers the object on team 6 (hostile to nobody, never a target, not
    // counted: PRIMITIVES 7); the real M415 run (2026-10-15) found the robots still on 6 at their first update, firing bullets
    // of team 6. 4.1's robot is an enemy whatever made it: team 6 becomes the enemy team 1 (registered); a team a script set
    // otherwise is kept.
    std::int32_t team0=0;
    if(Readable(o+kTeamAt,4))std::memcpy(&team0,o+kTeamAt,4);
    if(team0==6) {
        const bool ok=SetTeam(r.obj,1,true);
        Log("EDF41ROBO %p team 6 (EventAnimationObject's) -> enemy team 1 %s",r.obj,ok ? "set" : "NOT set");
    }
    r.lockPoint=AddLockPoint(r.obj);
    r.driveOn=edf41c::PlaySe(r.obj,L"ロボ駆動音",r.pos,&r.drive);   // se_table[0], looped (its cue loops)
    Reroll(r);
    RerollSide(r);
    BodyEnter(r,kS0);
    float durability=0.0f;
    if(Readable(o+kDurabilityAt,4))std::memcpy(&durability,o+kDurabilityAt,4);
    r.lastHp=durability;
    // the lights (4.1 0x273FB2: entry 0 played looping from the start)
    if(p6::LightsStart(r.lights,r.obj,Key(o,L"material_animation")))p6::LightsPlay(r.lights,0,2);
    std::int32_t team=0;
    if(Readable(o+kTeamAt,4))std::memcpy(&team,o+kTeamAt,4);
    Log("EDF41ROBO %p set up: pos %.0f %.0f %.0f yaw %.2f scale %.2f level %.2f (s %.2f) durability %.0f team %d lock point %p drive %d; %zu event clips, %zu motions, %zu sounds, %zu attacks; muzzles %d/%d/%d/%d feet %d/%d",
        r.obj,r.pos[0],r.pos[1],r.pos[2],r.yaw,r.scale,r.level,r.s,durability,team,r.lockPoint,r.driveOn ? 1 : 0,r.clipEvents.size(),r.motions.size(),static_cast<std::size_t>(Count(Key(r.obj,L"edf41_se"))),
        r.attacks.size(),sm.muzzles,md.muzzles,lg.muzzles,gn.muzzles,r.footL.entry ? 1 : 0,r.footR.entry ? 1 : 0);
    Log("EDF41ROBO %p lock-on locator %s; aim bones %d %d / %d %d; IK toes %d %d, CAS variables %d %d; light animations %zu",r.obj,r.lockOn.entry ? "ロックオン (list 1)" : "missing",
        sm.muzzle[0].bone,sm.muzzle[1].bone,md.muzzle[0].bone,md.muzzle[1].bone,r.ikBone[0],r.ikBone[1],r.ikVar[0],r.ikVar[1],p6::LightsCount(r.lights));
    return true;
}

// ---- being hit and dying (4.1 slot 11 0x275A30 = EDF6 slot 18 OnDamaged(object, GDI), after ApplyDamage took the HP) --------
// GDI: +0x30 centre, +0x40 impulse, +0x50 damage, +0x58 blast radius (> 0: an explosion), +0x78 / +0x88 the sub-hits (0x30 each:
// +0x00 point, +0x10 normal (M), +0x20 Havok body id).
struct SubHit { float point[3]; float normal[3]; std::uint32_t body; };
int SubHits(const void* gdi,SubHit* out,int most) noexcept {
    const char* g=static_cast<const char*>(gdi);
    if(!gdi || !Readable(g,0x90))return 0;
    const char* recs=*reinterpret_cast<const char* const*>(g+0x78);
    const std::size_t n=*reinterpret_cast<const std::size_t*>(g+0x88);
    if(!recs || n==0 || n>256 || !Readable(recs,n*0x30))return 0;
    int k=0;
    for(std::size_t i=0;i<n && k<most;++i) {
        SubHit& h=out[k];
        std::memcpy(h.point,recs+i*0x30,12);
        std::memcpy(h.normal,recs+i*0x30+0x10,12);
        std::memcpy(&h.body,recs+i*0x30+0x20,4);
        if(!std::isfinite(h.point[0]+h.point[1]+h.point[2]))continue;
        const float nl=Len3(h.normal);
        if(!std::isfinite(nl) || nl<0.5f || nl>1.5f) { h.normal[0]=0.0f; h.normal[1]=1.0f; h.normal[2]=0.0f; }
        ++k;
    }
    return k;
}
float GdiFloat(const void* gdi,std::size_t at) noexcept {
    float f=0.0f;
    if(gdi && Readable(static_cast<const char*>(gdi)+at,4))std::memcpy(&f,static_cast<const char*>(gdi)+at,4);
    return std::isfinite(f) ? f : 0.0f;
}

// 4.1 0x275A30's death: an explosion at a random hit point (0x1B2070, size 3), the ragdoll on (EDF6: 0x6E40B0, as
// ShootingTarget dies) and pushed at the hit bodies (ShootingTarget's numbers: a blast pushes away from its centre by
// max(radius - distance, 0.1) x 100, a bullet by its impulse x 2 x 0.5), the form's sound and Explosion_Big (ship: at the object
// towards -forward; robot: at the hit point; both 0.75 / 2.0), then S10. The kill count, the drop and the script's 9201 are
// ApplyDamage's (it marked the object destroyed before calling OnDamaged).
void Die(Robo& r,const void* gdi) noexcept {
    r.dead=true;
    SubHit hits[16];
    const int n=SubHits(gdi,hits,16);
    float p[3]={r.pos[0],r.pos[1],r.pos[2]},nrm[3]={0.0f,1.0f,0.0f};
    if(n) { const SubHit& h=hits[RandInt(r,n)]; std::memcpy(p,h.point,12); std::memcpy(nrm,h.normal,12); }
    Explosion(p,nrm,3.0f);
    r.ragdoll=p6::RagdollOn(r.obj);
    int pushed=0;
    if(r.ragdoll) {   // 4.1 0x275C3F..0x275CEB: the first body spun at up to 6 rad/s about a random axis
        const float up[3]={0.0f,1.0f,0.0f};
        float axis[3];
        Scatter(r,up,kPi,axis);
        const float speed=(Rand(r)*0.2f-0.1f)*60.0f;
        const float w[3]={axis[0]*speed,axis[1]*speed,axis[2]*speed};
        const p6::Bodies sb=p6::RagdollBodies(r.obj);
        if(sb.count)p6::SetAngularVelocity(p6::BodyHandle(sb,0),w);
        Log("EDF41ROBO %p death spin on body 0: %.2f %.2f %.2f rad/s",r.obj,w[0],w[1],w[2]);
    }
    if(r.ragdoll) {   // which bodies went dynamic (record +0xA4: may go ragdoll; +0x9C the mode now, 4 = dynamic)
        const p6::Bodies b=p6::RagdollBodies(r.obj);
        int able=0,dynamic=0;
        for(std::size_t i=0;i<b.count;++i) {
            able+=b.data[i*0xC0+0xA4]!=0;
            dynamic+=*reinterpret_cast<const std::int32_t*>(b.data+i*0xC0+0x9C)==4;
        }
        Log("EDF41ROBO %p ragdoll: %zu bodies, %d may go ragdoll, %d dynamic; body 0 mode %d",r.obj,b.count,able,dynamic,
            b.count ? *reinterpret_cast<const std::int32_t*>(b.data+0x9C) : -1);
    }
    if(r.ragdoll && gdi) {
        const float radius=GdiFloat(gdi,0x58);
        float centre[3]={GdiFloat(gdi,0x30),GdiFloat(gdi,0x34),GdiFloat(gdi,0x38)},imp[3]={GdiFloat(gdi,0x40),GdiFloat(gdi,0x44),GdiFloat(gdi,0x48)};
        const p6::Bodies b=p6::RagdollBodies(r.obj);
        for(int i=0;i<n;++i) {
            const int body=p6::BodyById(r.obj,hits[i].body);
            void* h=body>=0 ? p6::BodyHandle(b,static_cast<std::size_t>(body)) : nullptr;
            if(!h)continue;
            float v[3];
            if(radius>1e-6f) {
                float d[3]={hits[i].point[0]-centre[0],hits[i].point[1]-centre[1],hits[i].point[2]-centre[2]};
                const float l=Len3(d);
                const float k=(std::max)(radius-l,0.1f)*100.0f/(l>1e-4f ? l : 1.0f);
                for(int c=0;c<3;++c)v[c]=d[c]*k;
            } else for(int c=0;c<3;++c)v[c]=imp[c]*2.0f*0.5f;
            p6::PointImpulse(h,v,hits[i].point);
            ++pushed;
        }
    }
    const float back[3]={-std::sin(r.yaw),0.0f,-std::cos(r.yaw)};
    if(r.form) { PlaySe(r,L"ロボ死亡",r.pos); BigExplosion(p,nrm,0.75f,2.0f); }
    else { PlaySe(r,L"ロボ飛行形態死亡",r.pos); BigExplosion(r.pos,back,0.75f,2.0f); }
    StopSound(r.drive,60);
    for(Gun& g:r.gun)ChargeOff(g);
    float durability=0.0f;
    const char* o=static_cast<const char*>(r.obj);
    if(Readable(o+kDurabilityAt,4))std::memcpy(&durability,o+kDurabilityAt,4);
    Log("EDF41ROBO %p destroyed (%s form) at its frame %u, durability %.0f, %d sub-hits, explosion at %.0f %.0f %.0f; ragdoll %s, %d bodies pushed; destroyed flag %d",
        r.obj,r.form ? "robot" : "ship",r.ticks,durability,n,p[0],p[1],p[2],r.ragdoll ? "ON (0x6E40B0)" : "not available",pushed,
        Readable(o+kDestroyedAt,1) ? o[kDestroyedAt] : -1);
    BodySet(r,kS10);
}

// 4.1 0x275A30 alive: se_table[1] at the object for every hit; a bullet's hit (no blast radius) a spark at the first sub-hit
// (4.1 0x1B6AE0, size +0x1E10 x damage, +0x1E10 = 0.5 + 0.45 (1/s - 1); EDF6: MetalHit, the size clamped to what EDF6's own bullet
// hits use, M); the push on the ship's movement body (4.1 +0x1D08; the robot's walk sets its velocity every frame, so only the
// ship keeps it): a blast (r - 5) x 0.5 + 5 away from its centre (x 0.1), a bullet its impulse x 0.004 at most 200 x damage / max
// durability long and at most 10.
void HitReaction(Robo& r,const void* gdi,float taken) noexcept {
    ++r.hitsTaken;
    PlaySe(r,L"ロボ被弾",r.pos);
    SubHit hits[1];
    const int n=SubHits(gdi,hits,1);
    const float radius=GdiFloat(gdi,0x58),damage=taken>0.0f ? taken : GdiFloat(gdi,0x50);
    if(radius<=1e-6f) {
        const float k=0.5f+0.45f*(1.0f/(r.s>0.01f ? r.s : 1.0f)-1.0f);
        const float size=(std::max)(1.0f,(std::min)(20.0f,k*damage));
        const float at[3]={n ? hits[0].point[0] : GdiFloat(gdi,0x30),n ? hits[0].point[1] : GdiFloat(gdi,0x34),n ? hits[0].point[2] : GdiFloat(gdi,0x38)};
        const float up[3]={0.0f,1.0f,0.0f};
        p6::MetalHit(at,n ? hits[0].normal : up,size);
    }
    if(r.form==0 && r.body<kS4) {
        float v[3]={};
        if(radius>1e-6f) {
            const float d[3]={r.pos[0]-GdiFloat(gdi,0x30),r.pos[1]-GdiFloat(gdi,0x34),r.pos[2]-GdiFloat(gdi,0x38)};
            const float l=Len3(d),mag=((radius-5.0f)*0.5f+5.0f)*0.1f;
            if(l>1e-3f)for(int c=0;c<3;++c)v[c]=d[c]/l*mag;
        } else {
            float maxHp=1.0f;
            const char* o=static_cast<const char*>(r.obj);
            if(Readable(o+kMaxDurabilityAt,4))std::memcpy(&maxHp,o+kMaxDurabilityAt,4);
            float most=maxHp>0.0f ? 200.0f*damage/maxHp : 0.0f;
            most=(std::min)(10.0f,(std::max)(0.0f,most));
            const float imp[3]={GdiFloat(gdi,0x40)*0.004f,GdiFloat(gdi,0x44)*0.004f,GdiFloat(gdi,0x48)*0.004f};
            const float l=Len3(imp);
            if(l>1e-6f)for(int c=0;c<3;++c)v[c]=imp[c]/l*most;
        }
        for(int c=0;c<3;++c)r.vel[c]+=v[c];
    }
    if(r.hitsTaken<=3 || r.hitsTaken%100==0)
        Log("EDF41ROBO %p hit #%u: durability -%.1f (GDI +0x50 %.2f) radius %.1f, %d sub-hits%s",r.obj,r.hitsTaken,damage,GdiFloat(gdi,0x50),radius,n,radius<=1e-6f ? ", spark" : "");
}

// The dev placement (ini Edf41RoboPlace): the object put that far in front of the camera, once, high when a ship.
void Place(Robo& r) noexcept {
    const float d=Cfg().edf41RoboPlace;
    if(r.placed || d<=0.0f)return;
    float eye[3],fwd[3];
    if(!CameraNow(eye,fwd))return;
    const float h=std::sqrt(fwd[0]*fwd[0]+fwd[2]*fwd[2]);
    if(h<1e-3f)return;
    r.placed=true;
    r.pos[0]=eye[0]+fwd[0]/h*d;
    r.pos[2]=eye[2]+fwd[2]/h*d;
    r.pos[1]=eye[1]+(r.form==0 ? 150.0f : 50.0f);
    std::memcpy(r.stepPos,r.pos,12);   // a step under way goes on from here
    r.yaw=std::atan2(eye[0]-r.pos[0],eye[2]-r.pos[2]);
    Log("EDF41ROBO %p placed %.0f m in front of the camera (ini Edf41RoboPlace): %.0f %.0f %.0f",r.obj,d,r.pos[0],r.pos[1],r.pos[2]);
}

void Tick(Robo& r) noexcept {
    ++r.ticks;
    if(!r.placed && r.ticks<=600 && !NetFollows(r))Place(r);   // the camera may not be a view yet on the first frames
    // dev: transform this long after the creation, as the script's ObjectAction 0 would
    const float after=Cfg().edf41RoboTransformAfter;
    if(after>0.0f && !r.forcedTransform && r.form==0 && r.ticks>=static_cast<std::uint32_t>(after*60.0f) && !NetFollows(r)) {
        r.forcedTransform=true;
        Log("EDF41ROBO %p transform forced (ini Edf41RoboTransformAfter=%.0f)",r.obj,after);
        if(Alive(r.obj))BodySet(r,kS4);
    }
    if(r.body<kS10 && !r.dead) {   // a death OnDamaged did not see (the durability written by something else)
        float durability=1.0f;
        const char* o=static_cast<const char*>(r.obj);
        if(Readable(o+kDurabilityAt,4))std::memcpy(&durability,o+kDurabilityAt,4);
        if(!(durability>0.0f))Die(r,nullptr);
    }
    if(r.body<kS10 && !NetFollows(r))AiTick(r);   // online: a guest copy's decisions are the host's (NetIn)
    else { r.cmdX=r.cmdY=r.cmdZ=r.yawRate=0.0f; r.turn=0; }
    BodyTick(r);
    Move(r);
    NetPose(r);   // online: a guest copy pulled to the host's pose
    for(int g=0;g<4;++g)GunTick(r,r.gun[g],g);
    WriteMatrix(r);
    MoveSound(r.drive,r.pos);
    NetOut(r);    // online: the deciding copy's periodic state
    if(r.ticks%600==0) {
        float durability=0.0f;
        const char* o=static_cast<const char*>(r.obj);
        if(Readable(o+kDurabilityAt,4))std::memcpy(&durability,o+kDurabilityAt,4);
        Log("EDF41ROBO %p frame %u: %s / %s, form %d, pos %.0f %.0f %.0f yaw %.2f, target %s %.0f m, durability %.0f, guns %d%d%d%d rounds %u/%u/%u/%u, MAB events %u (hits %u, blasts %u)",
            r.obj,r.ticks,kBodyNames[r.body],kAiNames[r.ai],r.form,r.pos[0],r.pos[1],r.pos[2],r.yaw,r.haveTarget ? "seen" : "none",
            r.haveTarget ? TargetDistance(r) : 0.0f,durability,r.gun[0].state,r.gun[1].state,r.gun[2].state,r.gun[3].state,
            r.gun[0].shots,r.gun[1].shots,r.gun[2].shots,r.gun[3].shots,r.events,r.hits,r.blasts);
        const Muzzle* a[4]={&r.gun[kSmall].muzzle[0],&r.gun[kSmall].muzzle[1],&r.gun[kMiddle].muzzle[0],&r.gun[kMiddle].muzzle[1]};
        Log("EDF41ROBO %p aim (pitch / yaw rad): knees %.2f %.2f / %.2f %.2f, shoulders %.2f %.2f / %.2f %.2f; lights %zu",r.obj,a[0]->pitch,a[0]->yaw,
            a[1]->pitch,a[1]->yaw,a[2]->pitch,a[2]->yaw,a[3]->pitch,a[3]->yaw,p6::LightsCount(r.lights));
    }
}

// ---- aim bones and foot IK --------------------------------------------------------------------------------------------
// A 4x4 affine inverse (rows x, y, z, position; the 3x3 part any invertible matrix).
bool Inverse(const float* m,float* out) noexcept {
    const float a=m[0],b=m[1],c=m[2],d=m[4],e=m[5],f=m[6],g=m[8],h=m[9],i=m[10];
    const float A=e*i-f*h,B=-(d*i-f*g),C=d*h-e*g;
    const float det=a*A+b*B+c*C;
    if(!(std::fabs(det)>1e-12f))return false;
    const float k=1.0f/det;
    float r[16]={A*k,-(b*i-c*h)*k,(b*f-c*e)*k,0.0f, B*k,(a*i-c*g)*k,-(a*f-c*d)*k,0.0f, C*k,-(a*h-b*g)*k,(a*e-b*d)*k,0.0f, 0.0f,0.0f,0.0f,1.0f};
    for(int j=0;j<3;++j)r[12+j]=-(m[12]*r[0+j]+m[13]*r[4+j]+m[14]*r[8+j]);
    std::memcpy(out,r,sizeof(r));
    return true;
}

// 4.1 0x27AC20 (angles) + 0x27BB70 (bones), the knee and shoulder groups only, after the stock Update (the CAS has written this
// frame's local matrices, bone record +0x70; the skeleton is built from them at the start of the next Update, 0x1100010):
//  - the target, seen from the aim bone's position in its muzzle locator's frame as the animation alone would have it (4.1's
//    rows of 0x3232D0), gives a pitch -atan2(y, |xz|) and a yaw atan2(x, z) (0x51B490); the angles move 5 % of the way there a
//    frame, at most 0.01 rad (the step's length); no target / the S3 buster (+0x15F4 bit 3) / the dying: back to 0;
//  - the bone's local becomes Rx(pitch) Ry(yaw) x the CAS's local (0xE5810 on bone +0x60 in 4.1).
// muzzle_small_l sits on small_gun_r while its aim bone is small_gun_l (and the other way): 4.1's data, kept.
void AimBones(Robo& r) noexcept {
    char* skel=static_cast<char*>(r.obj)+kEaoSkeleton;
    if(!Readable(skel,0x28))return;
    char* recs=*reinterpret_cast<char**>(skel+0x10);
    const std::int64_t count=*reinterpret_cast<const std::int64_t*>(skel+0x20);
    if(!recs || count<=0 || count>512 || !Readable(recs,static_cast<std::size_t>(count)*0x110))return;
    const bool on=r.haveTarget && !(r.flags&8) && !r.ragdoll && r.body<kS10;
    // pass 1: the wanted angles, from the frames the skeleton now holds with last frame's base locals put back
    float want[2][2][2]={};
    for(int k=kSmall;k<=kMiddle;++k)
        for(int i=0;i<r.gun[k].muzzles;++i) {
            Muzzle& z=r.gun[k].muzzle[i];
            if(z.bone<0 || z.bone>=count || !on || !z.found)continue;
            float mz[16];
            if(!LocatorMatrix(z.loc,mz))continue;
            // the muzzle's own bone: its world without this file's rotation = base x parent world
            const std::ptrdiff_t mb=(static_cast<const char*>(z.loc.bone)-recs)/0x110;
            const Muzzle* owner=nullptr;
            for(int kk=kSmall;kk<=kMiddle && !owner;++kk)
                for(int ii=0;ii<r.gun[kk].muzzles;++ii)
                    if(r.gun[kk].muzzle[ii].bone==mb && r.gun[kk].muzzle[ii].haveWritten)owner=&r.gun[kk].muzzle[ii];
            float m0[16];
            std::memcpy(m0,mz,sizeof(m0));
            if(owner && mb>=0 && mb<count) {
                const char* rec=recs+mb*0x110;
                const std::int32_t parent=*reinterpret_cast<const std::int32_t*>(rec+0x10);
                float wc[16],inv[16],w0[16],tmp[16];
                std::memcpy(wc,rec+0xB0,64);
                if(parent>=0 && parent<count && Inverse(wc,inv)) {
                    float pw[16];
                    std::memcpy(pw,recs+static_cast<std::size_t>(parent)*0x110+0xB0,64);
                    Multiply(owner->base,pw,w0);
                    Multiply(mz,inv,tmp);
                    Multiply(tmp,w0,m0);
                }
            }
            const float* bp=reinterpret_cast<const float*>(recs+static_cast<std::size_t>(z.bone)*0x110+0xE0);
            const float d[3]={r.target[0]-bp[0],r.target[1]-bp[1],r.target[2]-bp[2]};
            float l[3];
            for(int row=0;row<3;++row) {
                const float* v=m0+row*4;
                const float n=Len3(v);
                l[row]=n>1e-6f ? (d[0]*v[0]+d[1]*v[1]+d[2]*v[2])/n : 0.0f;
            }
            want[k][i][0]=-std::atan2(l[1],std::sqrt(l[0]*l[0]+l[2]*l[2]));
            want[k][i][1]=(l[0]==0.0f && l[2]==0.0f) ? 0.0f : std::atan2(l[0],l[2]);
        }
    // pass 2: ease the angles, write the locals
    for(int k=kSmall;k<=kMiddle;++k)
        for(int i=0;i<r.gun[k].muzzles;++i) {
            Muzzle& z=r.gun[k].muzzle[i];
            if(z.bone<0 || z.bone>=count)continue;
            float dp=(want[k][i][0]-z.pitch)*0.05f,dy=(want[k][i][1]-z.yaw)*0.05f;
            const float step=std::sqrt(dp*dp+dy*dy);
            if(step*step>0.0001f) { dp*=0.01f/step; dy*=0.01f/step; }
            z.pitch+=dp; z.yaw+=dy;
            char* rec=recs+static_cast<std::size_t>(z.bone)*0x110;
            float cur[16];
            std::memcpy(cur,rec+0x70,64);
            if(!std::isfinite(cur[0]+cur[5]+cur[10]+cur[12]+cur[13]+cur[14]))continue;
            if(!(z.haveWritten && std::memcmp(cur,z.written,64)==0))std::memcpy(z.base,cur,64);   // the CAS wrote it again
            if(r.ragdoll) { std::memcpy(rec+0x70,z.base,64); z.haveWritten=false; continue; }
            const float cp=std::cos(z.pitch),sp=std::sin(z.pitch),cy=std::cos(z.yaw),sy=std::sin(z.yaw);
            const float rot[16]={cy,0.0f,-sy,0.0f, sp*sy,cp,sp*cy,0.0f, cp*sy,-sp,cp*cy,0.0f, 0.0f,0.0f,0.0f,1.0f};   // Rx(pitch) Ry(yaw)
            float out[16];
            Multiply(rot,z.base,out);
            std::memcpy(rec+0x70,out,64);
            std::memcpy(z.written,out,64);
            z.haveWritten=true;
        }
}

// 4.1 0x278190: in the robot form each foot's IK target (CAS variable v_ik_target_l / _r) is its toe's world matrix; EDF6's
// AlienTrailer feeds the same SGO key the same way (0x30EA9C: 0x116DF60 between the skeleton and the CAS step). Called before the
// stock Update: the toe as last built, carried by the change of the object's matrix from then to now.
void FootIk(Robo& r,const float* newMatrix) noexcept {
    if(r.form!=1 || r.ragdoll || !r.haveLastMatrix)return;
    char* skel=static_cast<char*>(r.obj)+kEaoSkeleton;
    float inv[16],move[16];
    if(!Inverse(r.lastMatrix,inv))return;
    Multiply(inv,newMatrix,move);
    for(int i=0;i<2;++i) {
        float toe[16],m[16];
        if(r.ikVar[i]<0 || !BoneMatrix(skel,r.ikBone[i],toe))continue;
        Multiply(toe,move,m);
        p6::CasVarSet(r.obj,r.ikVar[i],m);
    }
}

std::uint64_t __fastcall DtorWrap(void* self,unsigned flags) {
    {
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end()) {
            StopSound(it->second.drive,1);
            for(Gun& g:it->second.gun)ChargeOff(g);
            p6::LightsEnd(it->second.lights);
            robos.erase(it);
        }
        NetForget(self);   // online (edf41net)
    }
    Log("EDF41ROBO destructor %p",self);
    return reinterpret_cast<DtorFn>(roboClass.orig[kSlotDtor])(self,flags);
}

std::uint64_t __fastcall UpdateWrap(void* self,void* dt) {
    {
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end() && !it->second.broken) {
            Robo& r=it->second;
            if(!r.ready) {
                r.ready=true;
                r.broken=!Setup(r);
            }
            r.dt=dt;
            if(!r.broken) {
                NetIn(r);   // online (edf41net)
                Tick(r);
                // the foot IK targets for the skeleton the stock Update builds now from the matrix just written
                const char* o=static_cast<const char*>(self);
                if(Readable(o+kMatrixAt,64)) {
                    float now[16];
                    std::memcpy(now,o+kMatrixAt,sizeof(now));
                    FootIk(r,now);
                    std::memcpy(r.lastMatrix,now,sizeof(now));
                    r.haveLastMatrix=true;
                }
            }
        }
    }
    NetMirrorMake(self);   // development (ini Edf41NetMirror): made outside the lock (the factory takes it)
    const std::uint64_t result=reinterpret_cast<UpdateFn>(roboClass.orig[kSlotUpdate])(self,dt);
    {
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end() && it->second.ready && !it->second.broken) {
            Robo& r=it->second;
            Events(r);
            AimBones(r);
            p6::LightsStep(r.lights,dt);
            WriteMatrix(r);   // the class's update leaves the matrix alone; keep ours for the next frame's draw
            // the lock point on ロックオン (on hara1's origin when the model has no such locator)
            float m[16];
            if(r.lockPoint && r.lockOn.entry && LocatorMatrix(r.lockOn,m))SetLockPoint(r.lockPoint,m+12);
            else if(r.lockPoint && r.lockBone>=0 && BoneMatrix(static_cast<char*>(self)+kEaoSkeleton,r.lockBone,m))SetLockPoint(r.lockPoint,m+12);
            if(r.ticks%600==1 && r.lockPoint && Readable(static_cast<char*>(r.lockPoint)+0x10,12)) {
                const float* lp=reinterpret_cast<const float*>(static_cast<char*>(r.lockPoint)+0x10);
                Log("EDF41ROBO %p lock point at %.0f %.0f %.0f (object at %.0f %.0f %.0f)",self,lp[0],lp[1],lp[2],r.pos[0],r.pos[1],r.pos[2]);
            }
            float aim[3];
            if(Alive(self) && AimPoint(self,aim))DevShoot(self,aim);   // development (ini Edf41DevShoot)
        }
    }
    return result;
}

// ObjectAction (4.1 0x2758B0): 0 transform when alive, 1 robot at once (stand) + approach, 2 approach; damage while
// transforming is refused (4.1 slot 10 answers false in S4).
std::uint64_t __fastcall MessageWrap(void* self,std::uint32_t msg,void* data) {
    if(msg==kMsgAction && data && Readable(data,4)) {
        const std::int32_t action=*static_cast<const std::int32_t*>(data);
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end()) {
            Robo& r=it->second;
            Log("EDF41ROBO %p ObjectAction %d (form %d, %s)",self,action,r.form,kBodyNames[r.body]);
            if(!r.ready) { r.ready=true; r.broken=!Setup(r); }
            if(!r.broken) {
                if(action==0) {
                    if(Alive(self))BodySet(r,kS4);
                    return 1;
                }
                if(action==1) {
                    ToRobot(r);
                    BodySet(r,kS7);
                    Play(r,r.stStand,L"stand");
                    r.approach=true;
                } else if(action==2)r.approach=true;
            }
            // Never handed on: EventAnimationObject's OnMessage (0x5B6540) answers 0x1000000D from its SGO's `action` table,
            // which this object has not, and throws (EDF+0x5B6A01, M411's ObjectAction 1 on the real game 2026-10-15). 4.1's
            // base did nothing with 0 / 1 / 2 either.
            return 1;
        }
    }
    if(msg==kMsgDamage) {
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end() && it->second.body==kS4)return 0;
    }
    return reinterpret_cast<MessageFn>(roboClass.orig[kSlotMessage])(self,msg,data);
}

// Slot 6 (OnLevel, called by SetLevel 0x54E740 the first time: the script's CreateEnemy sets the level right after the creation,
// before a durability percentage, native 3019): EventAnimationObject's is `return true` (0x125270) and never scales the
// durability; 4.1's 0x2744B0 makes it 12000 x s x the difficulty: UfoCarrier508's formula (0x4F6D00) does the same in EDF6.
constexpr int kSlotOnLevel=6;
using OnLevelFn=bool(__fastcall*)(void*);
bool __fastcall OnLevelWrap(void* self) {
    const bool r=reinterpret_cast<OnLevelFn>(roboClass.orig[kSlotOnLevel])(self);
    float level=0.0f,max=0.0f;
    SizeDurability(self);
    level=LevelS(self)*10.0f;
    if(Readable(static_cast<char*>(self)+kMaxDurabilityAt,4))std::memcpy(&max,static_cast<char*>(self)+kMaxDurabilityAt,4);
    Log("EDF41ROBO %p level %.2f: durability sized to %.0f (UfoCarrier508's formula)",self,level,max);
    return r;
}

// Slot 18 (OnDamaged(object, GDI*), ApplyDamage 0x5485F9 after the HP change; EventAnimationObject's is GameObjectBase's empty
// 0x54A280): 4.1's slot 11 (0x275A30), the hit reaction or the death.
constexpr int kSlotOnDamaged=18;
using OnDamagedFn=void(__fastcall*)(void*,void*);
void __fastcall OnDamagedWrap(void* self,void* gdi) {
    reinterpret_cast<OnDamagedFn>(roboClass.orig[kSlotOnDamaged])(self,gdi);
    std::lock_guard<std::mutex> hold(lock);
    auto it=robos.find(self);
    if(it==robos.end() || !it->second.ready || it->second.broken)return;
    Robo& r=it->second;
    float durability=1.0f;
    const char* o=static_cast<const char*>(self);
    if(Readable(o+kDurabilityAt,4))std::memcpy(&durability,o+kDurabilityAt,4);
    const float taken=r.lastHp>durability ? r.lastHp-durability : 0.0f;   // (a script's durability change in between: 0)
    r.lastHp=durability;
    if(!(durability>0.0f)) { if(!r.dead)Die(r,gdi); return; }
    if(r.body<kS10)HitReaction(r,gdi,taken);
}

// Slot 16 (Accept(object, GDI*) -> bool, asked by ApplyDamage 0x547C30 before any HP change; EventAnimationObject's is
// GameObjectBase's `mov al,1` 0x54AA40): 4.1's slot 10 (0x2759F0) answers false while transforming (S4). Refusing in OnMessage
// alone missed hits (M416 2026-10-15 13:45: 26112 -> 12512 during the transform): bullets and area damage reach ApplyDamage
// through other messages (0x543920 sends 0x10000000); every path asks slot 16 (only forced damage, GDI flag 0x40, passes).
constexpr int kSlotAccept=16;
using AcceptFn=bool(__fastcall*)(void*,void*);
bool __fastcall AcceptWrap(void* self,void* gdi) {
    {
        std::lock_guard<std::mutex> hold(lock);
        auto it=robos.find(self);
        if(it!=robos.end() && it->second.body==kS4)return false;
    }
    return reinterpret_cast<AcceptFn>(roboClass.orig[kSlotAccept])(self,gdi);
}

void CreateRobo(void* info) noexcept {
    void* obj=CloneObject(roboClass,kEaoCtor,kEaoSize,info);
    if(!obj)return;
    // EventAnimationObject's constructor sets +0x380 bit 0 (0x5B6406: hits take no HP); 4.1's robot loses its durability.
    SetFlag(obj,kNoHpLoss,false);
    edf41net::Attach(obj);   // online: its NetworkObject takes the host's events (edf41net)
    {
        std::lock_guard<std::mutex> hold(lock);
        Robo& r=robos[obj];
        r=Robo{};
        r.obj=obj;
    }
    Log("EDF41ROBO create #%d: UfoRobo %p built by EventAnimationObject's constructor, vtable -> clone",++created,obj);
}

// ---- online (src/edf41net.h; docs/edf41-uforobo.md 2.4, docs/edf41-classes.md 联机) -------------------------------------------
// The robot is the script's own object (CreateEnemy: it has the game's network identity, the host's). Its copy on the host
// decides as before and tells the guests:
//  - kNetBody on each body state change (the state, the clip of a step / an attack, the backward chain, the dying frames, the
//    form, approach, where a step starts): the ship's volleys, the transform, every step, turn and attack choice;
//  - kNetGun when a robot-form gun group starts on its own timer (the one random start outside the body machine);
//  - kNetState every kNetEvery frames: the pose, the velocity, the target point.
// A guest copy runs no AI and no gun timer of its own: it enters the host's states (their clips, the clips' MAB events, the
// sounds, the blasts and the guns' G1 -> G2 -> G0 run here, aimed at the host's target point) and its pose is pulled to the
// host's. Its bullets and blasts are its own, as each machine's copy of a stock enemy fires its own: the hit on a player is
// judged on that player's machine (SoldierBase slot 34 0x5A3600), so each lands once. Durability is the game's (damage judged
// by the shooter's machine, the HP change broadcast): a guest sees the death from its own HP (Tick) or the host's kNetBody.
enum NetKind : std::uint8_t { kNetBody=1, kNetGun=2, kNetState=3 };
struct NetBodyMsg {
    std::int32_t body,pending,chain,dieFrames,form,approach;
    float stepPos[3],stepYaw,pos[3],yaw;
};
static_assert(sizeof(NetBodyMsg)==56,"fixed wire layout");
struct NetGunMsg { std::int32_t group,state,autoCount,reserved; };
static_assert(sizeof(NetGunMsg)==16,"fixed wire layout");
struct NetStateMsg {
    float pos[3],yaw,roll,vel[3],target[3];
    std::int32_t haveTarget,body,form,flags;
    std::uint32_t ticks;
};
static_assert(sizeof(NetStateMsg)==64,"fixed wire layout");
constexpr std::uint32_t kNetEvery=15;   // frames between two state messages (4 a second, 88 bytes each with the header)

struct RoboNet {
    bool follows=false;          // this frame: a guest copy
    bool mirror=false;           // the development mirror (ini Edf41NetMirror)
    const void* of=nullptr;      // the mirror: the robot it follows
    bool want=false,made=false;  // the original: its mirror asked for, made
    bool dressed=false;
    float offset[3]={};          // the mirror stands this far from what it follows
    bool snap=false;
    NetStateMsg last{};
    std::uint32_t age=0;
    std::uint32_t sent=0,applied=0,logged=0;
};
std::unordered_map<const void*,RoboNet> nets;   // under `lock`, as the robots

bool NetFollows(const Robo& r) noexcept {
    const auto it=nets.find(r.obj);
    return it!=nets.end() && it->second.follows;
}

void NetBody(Robo& r) noexcept {
    if(NetFollows(r))return;
    NetBodyMsg m{};
    m.body=r.body; m.pending=r.pendingState; m.chain=r.chain; m.dieFrames=r.dieFrames; m.form=r.form; m.approach=r.approach ? 1 : 0;
    std::memcpy(m.stepPos,r.stepPos,12); m.stepYaw=r.stepYaw;
    std::memcpy(m.pos,r.pos,12); m.yaw=r.yaw;
    if(edf41net::Send(r.obj,kNetBody,&m,sizeof(m)))++nets[r.obj].sent;
}

void NetGun(Robo& r,int group) noexcept {
    if(NetFollows(r) || group<0 || group>3)return;
    const NetGunMsg m{group,r.gun[group].state,r.gun[group].autoCount,0};
    if(edf41net::Send(r.obj,kNetGun,&m,sizeof(m)))++nets[r.obj].sent;
}

void NetOut(Robo& r) noexcept {
    if(NetFollows(r) || r.ticks%kNetEvery!=0)return;
    NetStateMsg m{};
    std::memcpy(m.pos,r.pos,12); m.yaw=r.yaw; m.roll=r.roll; std::memcpy(m.vel,r.vel,12); std::memcpy(m.target,r.target,12);
    m.haveTarget=r.haveTarget ? 1 : 0; m.body=r.body; m.form=r.form; m.flags=static_cast<std::int32_t>(r.flags); m.ticks=r.ticks;
    if(edf41net::Send(r.obj,kNetState,&m,sizeof(m))) {
        RoboNet& n=nets[r.obj];
        if(++n.sent<=3 || n.sent%2000==0) {
            const edf41net::Stats s=edf41net::Counters();
            Log("EDF41ROBO %p online: %u events sent (layer: sent %u, looped %u, received %u, refused %u)",r.obj,n.sent,s.sent,s.looped,s.received,s.refused);
        }
    }
}

void ApplyBody(Robo& r,RoboNet& n,const NetBodyMsg& m) noexcept {
    if(m.body<kS0 || m.body>kS11 || m.form<0 || m.form>1 || m.pending< -1 || m.pending>4096 || m.chain<0 || m.chain>1 || m.dieFrames<0
       || m.dieFrames>100000 || !edf41net::Sane(m.stepPos,3) || !edf41net::Sane(&m.stepYaw,1) || !edf41net::Sane(m.pos,3) || !edf41net::Sane(&m.yaw,1)) {
        Fail("a body event out of range (refused)");
        return;
    }
    const bool turned=m.form==1 && r.form==0 && m.body!=kS4 && m.body!=kS5;   // S5's entry turns it itself
    if(turned)ToRobot(r);
    if(m.body>=kS10) {
        // The host's copy died (S10 dying, S11 the wreck). One death path on every copy: Die (OnDamaged's, or Tick's when the HP
        // reached 0 only through the game's HP broadcast, which never calls slot 18): the explosion, the ragdoll turned on
        // (0x6E40B0) and spun, the form's sound, the drive stopped. `r.dead` keeps it to once: a guest copy a local player killed
        // (its own OnDamaged) or whose HP came to 0 first has done it already, and the host's S10 then only brings the dying time.
        // S11 (the wreck's explosion, the deletion) comes by each copy's own dying clock, not by the host's message.
        if(!r.dead)Die(r,nullptr);
        r.dieFrames=m.dieFrames;
        ++n.applied;
        if(n.logged<60) { ++n.logged; Log("EDF41ROBO %p %s copy follows: the host's %s (dying %d frames; dead here %s)",r.obj,n.mirror ? "mirror" : "guest",kBodyNames[m.body],m.dieFrames,r.dead ? "yes" : "no"); }
        return;
    }
    const bool same=r.body==m.body && ((m.body!=kS8 && m.body!=kS9) || r.pendingState==m.pending);
    r.approach=m.approach!=0;
    if(!same) {
        r.pendingState=m.pending;
        r.chain=m.chain;
        BodySet(r,m.body);
    }
    if(turned && m.body==kS7)Play(r,r.stStand,L"stand");   // ObjectAction 1 on the host: robot at once, standing
    if(m.body==kS8) {   // the step starts where the host's did (its root motion runs here from there)
        for(int k=0;k<3;k+=2)r.stepPos[k]=r.pos[k]=m.stepPos[k]+n.offset[k];
        r.stepYaw=r.yaw=m.stepYaw;
    }
    ++n.applied;
    if(n.logged<60) {
        ++n.logged;
        Log("EDF41ROBO %p %s copy follows: body %s%s (clip state %d, form %d)",r.obj,n.mirror ? "mirror" : "guest",kBodyNames[m.body],
            same ? " (already)" : "",m.pending,m.form);
    }
}

void ApplyGun(Robo& r,const NetGunMsg& m) noexcept {
    if(m.group<0 || m.group>3 || m.state!=kG1 || m.autoCount<0 || m.autoCount>100000) { Fail("a gun event out of range (refused)"); return; }
    Gun& g=r.gun[m.group];
    if(g.state==kG0)GunEnter(r,g,kG1);
    g.autoCount=m.autoCount;
}

void ApplyState(Robo& r,RoboNet& n,const NetStateMsg& m) noexcept {
    if(!edf41net::Sane(m.pos,3) || !edf41net::Sane(&m.yaw,1) || !edf41net::Sane(&m.roll,1) || !edf41net::Sane(m.vel,3,1.0e5f)
       || !edf41net::Sane(m.target,3)) { Fail("a state event out of range (refused)"); return; }
    n.last=m;
    n.snap=true;
    n.age=0;
    r.haveTarget=m.haveTarget!=0;
    for(int k=0;k<3;++k)r.target[k]=m.target[k]+n.offset[k];
}

void NetApply(void* context,std::uint8_t kind,const unsigned char* data,std::size_t size) noexcept {
    Robo& r=*static_cast<Robo*>(context);
    RoboNet& n=nets[r.obj];
    if(kind==kNetBody && size==sizeof(NetBodyMsg)) { NetBodyMsg m; std::memcpy(&m,data,sizeof(m)); ApplyBody(r,n,m); }
    else if(kind==kNetGun && size==sizeof(NetGunMsg)) { NetGunMsg m; std::memcpy(&m,data,sizeof(m)); ApplyGun(r,m); }
    else if(kind==kNetState && size==sizeof(NetStateMsg)) { NetStateMsg m; std::memcpy(&m,data,sizeof(m)); ApplyState(r,n,m); }
    else Fail("an event of a kind or size this version does not send (refused)");
}

// The mirror's line every 300 frames: what it shows against what it follows.
void MirrorReport(Robo& r,RoboNet& n) noexcept {
    const auto it=robos.find(n.of);
    if(it==robos.end()) { Log("EDF41ROBO mirror %p frame %u: the robot it follows is gone; body %s",r.obj,r.ticks,kBodyNames[r.body]); return; }
    const Robo& o=it->second;
    float d[3];
    for(int k=0;k<3;++k)d[k]=r.pos[k]-(o.pos[k]+n.offset[k]);
    int sm=-1,so=-1;
    float tm=0.0f,to=0.0f,lm=0.0f,lo=0.0f;
    ClipNow(r.obj,&sm,&tm,&lm);
    ClipNow(o.obj,&so,&to,&lo);
    Log("EDF41ROBO mirror %p frame %u: body %s / followed %s, form %d / %d, clip state %d at %.0f / %d at %.0f, pose off by %.1f m and %.3f rad, guns %d%d%d%d / %d%d%d%d, %u events applied",
        r.obj,r.ticks,kBodyNames[r.body],kBodyNames[o.body],r.form,o.form,sm,tm,so,to,Len3(d),WrapPi(r.yaw-o.yaw),r.gun[0].state,r.gun[1].state,
        r.gun[2].state,r.gun[3].state,o.gun[0].state,o.gun[1].state,o.gun[2].state,o.gun[3].state,n.applied);
}

void NetIn(Robo& r) noexcept {
    RoboNet& n=nets[r.obj];
    n.follows=edf41net::Guest(r.obj);
    if(n.mirror && !n.dressed) {   // the development mirror: on team 6 (its bullets and blasts hurt nobody), no target, no HP loss, not counted
        n.dressed=true;
        SetTeam(r.obj,6,true);
        SetFlag(r.obj,kNotTarget|kNotCounted|kNoHpLoss,true);
        Log("EDF41ROBO mirror %p follows %p: team 6, never a target, takes no damage, not counted (ini Edf41NetMirror)",r.obj,n.of);
    }
    if(!n.follows) {
        if(!n.mirror && !n.made && r.ticks>=30 && edf41net::Mirror())n.want=true;
        return;
    }
    r.cmdX=r.cmdY=r.cmdZ=r.yawRate=0.0f;
    r.turn=0;
    r.cmdTransform=r.cmdSmall=r.cmdMiddle=r.cmdBuster=false;
    r.cmdAttack=-1;
    ++n.age;
    edf41net::Drain(r.obj,&NetApply,&r);
    if(n.mirror && r.ticks%300==0)MirrorReport(r,n);
}

void NetPose(Robo& r) noexcept {
    const auto it=nets.find(r.obj);
    if(it==nets.end() || !it->second.follows || !it->second.snap || r.body>=kS10)return;   // a dying copy falls on its own
    const RoboNet& n=it->second;
    const NetStateMsg& s=n.last;
    float want[3],d[3];
    for(int k=0;k<3;++k)want[k]=s.pos[k]+n.offset[k]+(r.form==0 ? s.vel[k]*static_cast<float>(n.age)/60.0f : 0.0f);
    for(int k=0;k<3;++k)d[k]=want[k]-r.pos[k];
    const float dist=Len3(d);
    if(r.form==0) {   // the ship: velocity the host's, position pulled 8 % a frame (at once from 300 m off)
        const float k=dist>300.0f ? 1.0f : 0.08f;
        for(int i=0;i<3;++i) { r.pos[i]+=d[i]*k; r.vel[i]=s.vel[i]; }
        r.yaw=WrapPi(r.yaw+WrapPi(s.yaw-r.yaw)*0.08f);
        r.roll+=(s.roll-r.roll)*0.08f;
    } else if(r.body!=kS8 && r.body!=kS5 && r.landed) {   // the robot between steps: its feet slide slowly to the host's place
        const float k=dist>100.0f ? 1.0f : 0.03f;
        r.pos[0]+=d[0]*k;
        r.pos[2]+=d[2]*k;
        if(r.body!=kS9)r.yaw=WrapPi(r.yaw+WrapPi(s.yaw-r.yaw)*0.05f);
    }
}

void NetMirrorMake(void* self) noexcept {
    alignas(16) float m[16];
    float offset[3];
    {
        std::lock_guard<std::mutex> hold(lock);
        const auto it=nets.find(self);
        const auto rt=robos.find(self);
        if(it==nets.end() || !it->second.want || it->second.made || rt==robos.end())return;
        it->second.want=false;
        it->second.made=true;
        const Robo& r=rt->second;
        const float side=120.0f;   // to its right (the yaw frame's +X row: cos yaw, 0, -sin yaw)
        offset[0]=std::cos(r.yaw)*side; offset[1]=0.0f; offset[2]=-std::sin(r.yaw)*side;
        if(!Readable(static_cast<char*>(self)+kMatrixAt,64))return;
        std::memcpy(m,static_cast<char*>(self)+kMatrixAt,sizeof(m));
        for(int k=0;k<3;++k)m[12+k]+=offset[k];
    }
    void* o=CreateFromPath(m,L"app:/object/UfoRobo401.sgo");   // the factory runs here and takes `lock`
    std::lock_guard<std::mutex> hold(lock);
    if(!o || robos.find(o)==robos.end()) { Log("EDF41ROBO %p: the development mirror was not made (ini Edf41NetMirror)",self); return; }
    RoboNet& mn=nets[o];
    mn.mirror=true;
    mn.of=self;
    std::memcpy(mn.offset,offset,sizeof(offset));
    Log("EDF41ROBO %p: development mirror %p made 120 m to its right; it follows through edf41net's loopback (ini Edf41NetMirror)",self,o);
    edf41net::Loopback(self,o);
}

void NetForget(const void* self) noexcept { nets.erase(self); }
}  // namespace

bool InstallEdf41Robo() noexcept {
    if(!Cfg().edf41Robo) {
        Log("EDF41ROBO off (ini Edf41Robo=0)");
        return false;
    }
    if(!BuildClone(roboClass,&DtorWrap,&UpdateWrap,&MessageWrap)) {
        Log("EDF41ROBO the vtable clone could not be built: off");
        return false;
    }
    SetSlot(roboClass,kSlotOnLevel,reinterpret_cast<void*>(&OnLevelWrap));
    SetSlot(roboClass,kSlotOnDamaged,reinterpret_cast<void*>(&OnDamagedWrap));
    SetSlot(roboClass,kSlotAccept,reinterpret_cast<void*>(&AcceptWrap));
    // Slot 22 (CanChangeTeam, asked by SetTeam 0x54EE70): EventAnimationObject's 0x5B6BA0 allows team 6 only, so CreateEnemy's
    // SetTeam(1) left the robots on team 6 (M415 / M411 real runs 2026-10-15): GameObjectBase's (every team).
    AllowAnyTeam(roboClass);
    const bool ok=AddFactory(L"UfoRobo",&CreateRobo);
    Log("EDF41ROBO on: UfoRobo %s",ok ? "queued for the game's registration" : "NOT registered");
    return ok;
}
}  // namespace crew
