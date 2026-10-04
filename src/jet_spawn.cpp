// The jets' bodies (jet.cpp): the SGOs (jet_internal.h kBodies), their preload for a mission, spawning a jet
// (JetLaunch and the airstrike takeovers) or a called heli, the body's fix-ups and its far rendering.
// Run-time spawning (docs/mission-airstrike-re.md §3): JetLaunch makes a jet exactly like the script's
// CreateFriend: CreateObject on the preloaded SGO, team friend, RideAi(true). Only RideAi with true reads the
// SGO's mission_setup (0x633063 -> slot 46), which is what writes the jet mark, the weapons and the heli
// parameters; it then flies at its strike point from the first frame.
// All addresses are RVAs into EDF.dll TimeDateStamp 0x678CCB46.
#include "jet_internal.h"
#include <cstdio>
#include <cwchar>

namespace crew {
namespace jet {
namespace {
// The preload manager *(image+kPreloadMgr), the object manager *(image+kObjectMgr), CreateObject(manager,
// &matrix, path, &InitParam) -> the object (the manager owns it), SetTeam(object, team, 1).
constexpr unsigned kPreload=0x7A3780;
constexpr std::size_t kPreloadMgr=0x20B29A8;
constexpr float kLaunchClear=100.0f;   // a launched jet starts at least this high over the ground
// The heli starts kHeliClear over the ground: high enough that it does not hit it while its rotor spins
// up (heli.cpp gives it the hover rotor at once), low enough that it is soon at its working height.
constexpr float kHeliClear=40.0f;
const unsigned char kPreloadSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48};
const unsigned char kCreateObjectSig[]={0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48,0x8D,0x6C,0x24,0xD9};
const unsigned char kSetTeamSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x20,0x41};
const unsigned char kDeleteSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xEC,0x60,0x48};
bool spawnOk=false;      // the spawn functions matched (InstallSpawn)
bool preloaded[kBodyCount]{};   // the body's SGO was preloaded for this mission (PreloadJets)
bool broken[kBodyCount]{};      // its spawn faulted in the game's init (CreateJet): off until the game restarts
using PreloadFn=void(*)(void*,const wchar_t*,std::int32_t,std::int32_t);
using RideAiFn=void(*)(void*,bool);

// The heli's "body" part: its init (0x64E9D1) looks the part up by that name in the vehicle's parts
// (vehicle+0x1320, 0x6EA4B0(parts, name) -> index or -1) and keeps the index at +0x1530, which slot 61
// mode 1 (0x650119: the crash step a downed heli runs from its update, slot 5) reads unchecked: -1 -> a
// null part -> crash at 0x650137 (2026-10-03, a fighter going down in an online mission; offline no jet
// had gone down yet). The parts are the V506 MAB's six nodes, then the model's bones (mdl 6, its body 7). The
// jets' fuselage bone is their model's own (bomber501 / bomber401; the carrier and the drone call theirs body),
// so it is looked up by that.
constexpr unsigned kFindPart=0x6EA4B0,kBodyPartUse=0x650119,kBodyPartInit=0x64E9C9;
constexpr std::size_t kParts=0x1320,kBodyPart=0x1530;
const unsigned char kFindPartSig[]={0x48,0x89,0x5C,0x24,0x18,0x48,0x89,0x6C,0x24,0x20,0x56,0x48,0x83,0xEC,0x50};
const unsigned char kBodyPartUseSig[]={0x8B,0x81,0x30,0x15,0x00,0x00};
const unsigned char kBodyPartInitSig[]={0x49,0x8D,0x8C,0x24,0x20,0x13,0x00,0x00};
bool bodyPartOk=false;
using FindPartFn=std::int32_t(__fastcall*)(void*,const wchar_t*);

// Far rendering (tmp/view-distance-re.md, docs/jet-model-re.md §8.4). The scene draws through two Umbra
// cameras: the near one 0.1 m to LightEnv FarClipZ (1000 m in every mission) for nodes with mask bit25|bit27,
// the far one 500 m to 20 km for bit26 only. A vehicle's render node is made with 0x12000000, so a jet circling
// past 1000 m stops being drawn. The game's own switch for that bit, the one SGO FarRender / use_far_render
// throw, is 0x11B3020(node,true): the jet's node (the model component at vehicle+0xE40, vtable 0x176B9A8) gets
// it, and the near pass is unchanged. Checked every frame, set only while the bit is missing, so a node the
// game rebuilds or resets gets it back. The switch's code is checked at install (kSetFarRenderSig: mov r8d,
// [rcx+20h]; btr/bts bit 26), as every other function the plugin calls.
constexpr unsigned kRenderNodeVtable=0x176B9A8,kSetFarRender=0x11B3020;
constexpr std::size_t kRenderNode=0xE40,kNodeMask=0x20;
constexpr unsigned kFarBit=0x04000000;
const unsigned char kSetFarRenderSig[]={0x44,0x8B,0x41,0x20,0x41,0x8B,0xC0,0x0F,0xBA,0xF0,0x1A,0x41,0x0F,0xBA,0xE8,0x1A};
bool farOk=false;

// Flights (jet_hooks.cpp): launched jets from one source (an Air Raider's call, a mission's strike) within
// kFlightGapMs of the last are one.
constexpr ULONGLONG kFlightGapMs=20000;
struct FlightSource { const void* source; unsigned flight; ULONGLONG at; };
FlightSource lastFlights[4]{};
unsigned nextFlight=kPlacedFlight+1;

// The flight a jet launched now from `source` joins (see kFlightGapMs).
unsigned FlightFor(const void* source,ULONGLONG ms) noexcept {
    for(auto& l:lastFlights)
        if(l.source==source && ms-l.at<kFlightGapMs){l.at=ms;return l.flight;}
    auto& l=lastFlights[nextFlight%4];
    l.source=source;l.flight=nextFlight++;l.at=ms;
    return l.flight;
}

// Raises `p` to at least `clear` over the ground (terrain or buildings) under it: the jet has a rigid
// body, unlike the rail planes whose start points it takes.
void ClearGround(float* p,float clear) noexcept {
    const float top[3]={p[0],p[1]+600.0f,p[2]},bottom[3]={p[0],p[1]-1500.0f,p[2]};
    float hit[3];
    if(MapRay(top,bottom,hit)>=0.0f && p[1]<hit[1]+clear)p[1]=hit[1]+clear;
}

// A jet SGO the game cannot build (its model lacks a bone the V506 parts hang on: the drone's root until
// 2026-10-03, testrange/gen.py JET_MAB_ROOT) faults inside CreateObject's init and leaves a half-made vehicle
// in the world, which crashes the game a moment later. Unseen, the input hook's handler swallowed the
// fault and the carrier tried again every 1.5 s. Now it is logged and that body is off until a restart.
int SpawnFault(Body b,const EXCEPTION_POINTERS* e) noexcept {
    const auto r=e->ExceptionRecord;
    const auto at=static_cast<const unsigned char*>(r->ExceptionAddress);
    Log("JET %ls: the game faulted building it (%08lX at EDF+%llX): this body is off until the game restarts",
        Row(b).file,r->ExceptionCode,static_cast<unsigned long long>(at-image));
    broken[static_cast<int>(b)]=true;preloaded[static_cast<int>(b)]=false;
    return EXCEPTION_EXECUTE_HANDLER;
}

unsigned char* CreateJet(Body b,const float* m,InitParam* param) noexcept {
    __try { return reinterpret_cast<CreateObjectFn>(image+kCreateObject)(At<void*>(image,kObjectMgr),m,Row(b).sgo,param); }
    __except(SpawnFault(b,GetExceptionInformation())) { return nullptr; }
}
}  // namespace

bool ModFileThere(const wchar_t* file) noexcept {
    wchar_t path[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(path,L'\\') : nullptr;
    if(!slash)return false;
    *slash=0;
    if(wcscat_s(path,L"\\Mods\\OBJECT\\")!=0 || wcscat_s(path,file)!=0)return false;
    return GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES;
}

bool Preloaded(Body b) noexcept { return preloaded[static_cast<int>(b)]; }
bool SpawnReady() noexcept { return spawnOk; }

void Facing(const float* heading,const float* at,float* m) noexcept {
    float fwd[3]={heading[0],0.0f,heading[2]};
    if(!Normalize(fwd)){fwd[0]=0;fwd[2]=1;}
    const float r[16]={fwd[2],0,-fwd[0],0, 0,1,0,0, fwd[0],0,fwd[2],0, at[0],at[1],at[2],1};
    std::memcpy(m,r,sizeof(r));
}

void SetJetTeam(unsigned char* v,std::int32_t team) noexcept {
    if(spawnOk)reinterpret_cast<SetTeamFn>(image+kSetTeam)(v,team,true);
}

// CreateFriend's steps (CreateObject, SetTeam, RideAi(true)); the object, deleted again when it is not what
// its body is (a jet SGO without its mark, a heli SGO that is a jet), or nullptr.
unsigned char* SpawnJet(Body b,const float* m) noexcept {
    InitParam param{image+kInitParamVtable,{}};
    unsigned char* v=CreateJet(b,m,&param);
    if(!v)return nullptr;
    if(bodyPartOk)FixBodyPart506(v,"JET");
    reinterpret_cast<SetTeamFn>(image+kSetTeam)(v,kTeamFriend,true);
    reinterpret_cast<RideAiFn*>(At<void**>(v,0))[kSlotRideAi](v,true);
    const BodyRow& row=Row(b);
    const bool jet=row.mark>0.0f;
    Role role=Role::fighter;
    if(jet ? IsJetVehicle(v,&role,nullptr) && role==row.role : IsHelicopter(v) && !IsJetVehicle(v,nullptr,nullptr))return v;
    Log("JET launch: %p (%ls) is no %s (mark %.0f): deleted",v,row.file,row.name,BodyMark(v));
    reinterpret_cast<DeleteFn>(image+kDelete)(v);
    return nullptr;
}

Jet* Launch(Body b,const float* from,const float* heading,const float* target,DWORD fuelSec,float speed,const void* source) noexcept {
    if(!spawnOk || !Cfg().jetPilot || !Preloaded(b) || !At<void*>(image,kObjectMgr) || !SlotFree())return nullptr;
    const ULONGLONG ms=GameMs();
    float start[3]={from[0],from[1],from[2]};
    ClearGround(start,kLaunchClear);
    alignas(16) float m[16];
    Facing(heading,start,m);
    unsigned char* const v=SpawnJet(b,m);
    if(!v)return nullptr;
    Jet* const j=NewEntry(v,ms);
    if(!j){reinterpret_cast<DeleteFn>(image+kDelete)(v);return nullptr;}
    j->launched=true;j->mode=Mode::patrol;
    std::memcpy(j->anchor,target,12);j->fuelMs=static_cast<ULONGLONG>(fuelSec)*1000;
    JoinFlight(*j,FlightFor(source,ms));
    for(int i=0;i<3;++i)j->m.vel[i]=m[8+i]*speed;
    Log("JET v=%p launched: %s (%ls) flight %u wing %d from (%.0f,%.0f,%.0f) at (%.0f,%.0f,%.0f) %.0f m/s fuel=%lus driver=%d hp=%.0f/%.0f",
        v,KindOf(*j).name,Row(b).file,j->flight,j->wing,start[0],start[1],start[2],target[0],target[1],target[2],speed,fuelSec,
        SeatCount(v)>0 && SeatRider(SeatAt(v,0))==Rider::dummy,At<float>(v,kHp),At<float>(v,kHpMax));
    Publish(true);
    return j;
}

void FarRender(Jet& j,unsigned char* v) noexcept {
    if(j.farOff || !farOk)return;
    unsigned char* const node=v+kRenderNode;
    __try {
        if(At<const void*>(node,0)!=image+kRenderNodeVtable) {
            Log("JET v=%p render node %p has vtable %p, not the model node's: far rendering off for it",v,node,
                At<const void*>(node,0));
            j.farOff=true;
            return;
        }
        if(At<unsigned>(node,kNodeMask)&kFarBit)return;
        reinterpret_cast<void(*)(void*,bool)>(image+kSetFarRender)(node,true);
        const unsigned mask=At<unsigned>(node,kNodeMask);
        Log("JET v=%p far rendering on: node mask %08x",v,mask);
        if(!(mask&kFarBit)){Log("JET v=%p far bit did not stick: far rendering off for it",v);j.farOff=true;}
    } __except(FaultLog("JET far rendering (off for that jet)",GetExceptionInformation())){j.farOff=true;}
}

bool InstallSpawn() noexcept {
    spawnOk=Matches(kDelete,kDeleteSig,sizeof(kDeleteSig)) && Matches(kPreload,kPreloadSig,sizeof(kPreloadSig)) &&
            Matches(kCreateObject,kCreateObjectSig,sizeof(kCreateObjectSig)) && Matches(kSetTeam,kSetTeamSig,sizeof(kSetTeamSig)) &&
            Readable(image+kInitParamVtable,8);
    bodyPartOk=spawnOk && Matches(kFindPart,kFindPartSig,sizeof(kFindPartSig)) &&
               Matches(kBodyPartUse,kBodyPartUseSig,sizeof(kBodyPartUseSig)) && Matches(kBodyPartInit,kBodyPartInitSig,sizeof(kBodyPartInitSig));
    if(!bodyPartOk)spawnOk=false;   // a jet without its body part crashes online: none at all
    return spawnOk;
}

bool InstallFarRender() noexcept {
    farOk=Matches(kSetFarRender,kSetFarRenderSig,sizeof(kSetFarRenderSig)) && Readable(image+kRenderNodeVtable,8);
    if(!farOk)Log("JET far rendering: profile mismatch (jets past 1000 m are not drawn)");
    return farOk;
}

void ResetFlights() noexcept {
    for(auto& l:lastFlights)l=FlightSource{};
}
}  // namespace jet

using namespace jet;

// Every flag is cleared first: a body not preloaded for this mission is never spawned (the stock planes come).
void PreloadJets() noexcept {
    for(auto& p:preloaded)p=false;
    ResetShells();
    if(!spawnOk)return;
    __try {
        const auto mgr=At<void*>(image,kPreloadMgr);
        if(!mgr)return;
        char line[512];
        int at=0;
        for(int k=0;k<kBodyCount;++k) {
            preloaded[k]=!broken[k] && ModFileThere(kBodies[k].file);
            if(preloaded[k])reinterpret_cast<PreloadFn>(image+kPreload)(mgr,kBodies[k].sgo,2,-1);
            const int n=sprintf_s(line+at,sizeof(line)-at,"%s%s=%d",k ? " " : "",kBodies[k].name,preloaded[k]);
            if(n>0)at+=n;
        }
        // The doll drones' dolls (as the Recruiter's weapon SGO has its doll preloaded, its `resource`).
        const bool dolls=PreloadDolls(mgr,Preloaded(Body::doll));
        Log("JET preload %s (dolls %d)",line,dolls);
        // The gunship's shells (GunshipFire), with its body; the impact charges.
        PreloadShells(mgr,Preloaded(Body::gunship));
    } __except(FaultLog("JET preload (nothing preloaded)",GetExceptionInformation())) {
        for(auto& p:preloaded)p=false;
        ResetShells();
    }
}

bool JetLaunch(JetRole as,const float* from,const float* heading,const float* target,DWORD fuelSec,const void* source,
               bool escort) noexcept {
    const int row=static_cast<int>(as);
    if(row<0 || row>=kLaunchCount)return false;
    // The body its role flies in; not there this mission, its kind's plain body (a blast or doll carrier: the
    // carrier, with gun drones), then the fighter.
    const Body want=kLaunchRows[row].body;
    Body b=want;
    if(!Preloaded(b))b=KindOf(Row(b).role).body;
    if(!Preloaded(b))b=Body::fighter;
    __try {
        Jet* const j=Launch(b,from,heading,target,fuelSec,KindOf(Row(b).role).cruise,source);
        if(!j)return false;
        j->escort=escort;
        if(b!=want)Log("JET v=%p launched as %s: %ls not preloaded",j->Vehicle(),Row(b).name,Row(want).file);
        return true;
    }
    __except(FaultLog("JET launch",GetExceptionInformation())){return false;}
}

unsigned char* JetLaunchDrone(const float* from,const float* heading,const float* target,DWORD fuelSec,const void* source,
                              bool escort) noexcept {
    __try {
        Jet* const j=Launch(Body::drone,from,heading,target,fuelSec,KindOf(Role::drone).cruise,source);
        if(!j)return nullptr;
        j->escort=escort;
        return j->Vehicle();
    }
    __except(FaultLog("JET drone launch",GetExceptionInformation())){return nullptr;}
}

unsigned char* HeliLaunch(HeliBody as,const float* from,const float* heading) noexcept {
    const Body b=as==HeliBody::brute410 ? Body::heli410 : Body::heli506;
    if(!spawnOk || !Preloaded(b) || !At<void*>(image,kObjectMgr))return nullptr;
    __try {
        float start[3]={from[0],from[1],from[2]};
        ClearGround(start,kHeliClear);
        alignas(16) float m[16];
        Facing(heading,start,m);
        unsigned char* const v=SpawnJet(b,m);
        if(v)Log("HELI v=%p launched: %ls at (%.0f,%.0f,%.0f)",v,Row(b).file,start[0],start[1],start[2]);
        return v;
    } __except(FaultLog("HELI launch",GetExceptionInformation())){return nullptr;}
}
}  // namespace crew
