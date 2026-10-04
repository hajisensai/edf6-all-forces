// Airstrike takeovers (docs/mission-airstrike-re.md): the bombers the game flies past on rails become jets
// the plugin flies (jet.cpp), which drop the bombers' own bombs, can be shot down, and stay on as strike
// jets for their sortie.
//  - Every bomber: both calls of BombingPlane_Init (0x5AABB0) are redirected, the Air Raider's bomber
//    call (from its IndirectFireControl's step, 0x2B924E) and the missions' strafing planes
//    (DemoAirStrike's ctor, 0x5B4423: RM034A/B, M116, M118). After the stock init the plugin launches a
//    bomber jet where the plane starts, along its heading, carrying its payload (JetLaunchBomber: the
//    same bombs, damage, spread, seed and owner, released from the jet). At its first update (its
//    vtable's slot 5), before it moves or drops a thing, the plane is hidden as its own last state hides
//    it (0x5AB9A0: stopped, drawn no more, off the radar), and from then on not updated; it is deleted once
//    its jet's bay is gone (JetHolds), as the stock plane deletes itself once its bombs are. The call's
//    target marker lasts as long as its planes (deleting the plane at once took it down before a bomb
//    fell), and the DemoAirStrike deletes itself once its plane is gone.
//  - The Air Raider's call weapons (tools/call_weapons.py: EDF6VC_CALL_*, KM6 bomber calls told apart by
//    their AmmoHitSizeAdjust, weapon+0x8C4, kCalls' marks): at the call (its one call of IFC_Start,
//    0x6A8DFB) the plugin launches that call's jets or helis instead of its bombers, which it keeps home
//    (the call's plane count, ifc+0x80, set to 0 after IFC_Start: the call is spent, its state machine
//    goes back to idle). A guard call works round its marker, a follow call round the player. Only the
//    caller's game does this: in an online game the others see the stock bombers.
//  - The call weapons are owned from the start (docs/loadout-re.md section 8): before the game's own
//    "grant the installed DLC weapons" step (0xDC550, UnlockDownloadContents: after every save load, and
//    in a new game's reset; its two entries, a call at 0xDC348 and the script thunk's jump at 0x70FF87,
//    are redirected) every weapon table row named EDF6VC_CALL_* gets the owned bit, as that step gives a
//    DLC weapon its: NEW and 0 stars the first time, stars left alone after. Before it, so its "equipped
//    but not owned: back to the default weapon" pass keeps them on the soldiers that carry them.
// When no jet can be launched (the jet SGOs missing or not preloaded this mission) the stock bombers fly;
// without the plugin the call weapons are plain KM6 calls (still owned: the bit is in the save).
// The other scripted strikes (DemoIndirectFire, gunship fire, missiles, satellite laser) are shells out
// of the sky with no plane to take over, and stay stock.
#include "crew.h"
#include "jet_internal.h"   // FaultLog
#include "memory.h"
#include <atomic>
#include <cmath>
#include <cwchar>

namespace crew {
namespace {
constexpr unsigned kIfcStart=0x2B5DA0,kRadioCall=0x6A8DFB;
constexpr std::size_t kIfcPlanes=0x80,kStartTarget=0x50;
constexpr unsigned kBomberInit=0x5AABB0,kRadioBomber=0x2B924E,kMissionBomber=0x5B4423;
constexpr unsigned kPlaneUpdateSlot=0x17D3A30+5*8,kPlaneUpdate=0x5AB240,kDelete=0x118A1B0;
constexpr std::size_t kPlaneVelocity=0xB80,kPlaneModel=0x660;   // model instance embedded (0x5AB2E3)
constexpr float kApproach=1000.0f,kAboveTarget=150.0f,kWingSpacing=70.0f,kWingStep=15.0f;
constexpr float kHeliApproach=300.0f,kHeliSpacing=40.0f;
constexpr float kSubAhead=1000.0f;   // the 1664 m hull (half 832) clear of the caller
// Ownership (docs/loadout-re.md 8): the game status, its weapon table (cfg = GS+0x130, the table loaded
// once cfg+0x188 is set) and per row a record of 12 bytes, u32 flags (bit0 owned, bit2 NEW) and 8 star
// bytes, 0x800 of them.
constexpr unsigned kUnlockDlc=0xDC550,kUnlockDlcCall=0xDC348,kUnlockDlcJump=0x70FF87;
constexpr unsigned kRowCount=0xE23F0,kGetRow=0xE1CF0;
constexpr std::size_t kCfg=0x130,kTableRef=0x188,kFlags=0xEB50,kStars=0xEB54,kRecord=12;
constexpr std::uint32_t kMaxRecords=0x800;
constexpr unsigned char kUnlockDlcSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,
                                         0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
constexpr unsigned char kUnlockThunkSig[]={0x48,0x8B,0x0D};   // 0x70FF80: mov rcx,[GS]; jmp 0xDC550
constexpr wchar_t kCallPrefix[]=L"EDF6VC_CALL_";
// The weapon a radio call's IndirectFireControl is in (ifc = weapon+0x1660), and its AmmoHitSizeAdjust.
constexpr std::size_t kWeaponIfc=0x1660,kWeaponHitSize=0x8C4;

const unsigned char kIfcStartSig[]={0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x60,0x48,0x8B,0x05};
// call IFC_Start; then the caller marks the call active and copies the plane count (+0x16E0 -> +0x16E4)
const unsigned char kRadioCallSig[]={0xE8,0xA0,0xCF,0xC0,0xFF,0xC6,0x87,0xEC,0x16,0x00,0x00,0x01,0x8B,0x87,0xE0,0x16};
const unsigned char kBomberInitSig[]={0x48,0x8B,0xC4,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x55,0x41};
const unsigned char kRadioBomberSig[]={0xE8,0x5D,0x19,0x2F,0x00,0x90,0x48,0x8B,0x4D,0x18};
const unsigned char kMissionBomberSig[]={0xE8,0x88,0x67,0xFF,0xFF,0x90,0xBB,0xFF,0xFF,0xFF};
const unsigned char kPlaneUpdateSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89};

using IfcStartFn=std::uintptr_t(__fastcall*)(void*,const void*);
// BombingPlane_Init(plane, &target, &owner weak, damage, spread, speed a frame, target_adjust,
// target_distance, &bombing_plane_param, seed)
using BomberInitFn=void(__fastcall*)(unsigned char*,const float*,const void*,float,float,float,float,float,const void*,std::int32_t);
using PlaneUpdateFn=void(__fastcall*)(unsigned char*,const void*);
using DeleteFn=void(*)(void*);
PlaneUpdateFn nextPlaneUpdate=nullptr;

// Launch sources (JetLaunch): an Air Raider's call (its jets and bombers), a mission's strike.
const char kRadioSource='r',kMissionSource='m';   // distinct values: identical constants may be folded

// Bombers whose jets fly instead, by their weak-this control block: hidden at their first update, deleted
// once the jet lets go of them (JetHolds), or kHoldMaxMs after. As many as jets can fly (jet.cpp
// kMaxJets); with none free the bomber is not taken over and flies stock. (A ring of 32 overwrote held
// planes with more than 32 jets bombing: the overwritten plane was updated again, opened its own bay and
// dropped its bombs a second time, unseen.)
struct Held { const void* ctrl; ULONGLONG since; bool hidden; };
constexpr int kMaxHeld=64;
Held held[kMaxHeld]{};
constexpr ULONGLONG kHoldMaxMs=180000;
// An entry past kHoldMaxMs (and a margin) is free: its plane, still updated, was deleted at kHoldMaxMs, or
// is gone (the mission ended) without an update to delete it.
constexpr ULONGLONG kHeldStaleMs=kHoldMaxMs+10000;

Held* FreeHeld(ULONGLONG ms) noexcept {
    for(auto& h:held)if(!h.ctrl || ms-h.since>kHeldStaleMs)return &h;
    return nullptr;
}
// The plane's last state's entry (0x5AB9A0): speed 0, its draw component off (0x6C04B0(plane+0x5C0, 0)),
// off the radar (0x54DDB0).
constexpr unsigned kPlaneHide=0x6C04B0,kPlaneUnlist=0x54DDB0;
constexpr std::size_t kPlaneSpeed=0xB90,kPlaneDraw=0x5C0;

// The call weapons (tools/call_weapons.py writes the same marks and counts): per role a guard call (round
// its marker) and a follow call (round the player), the follow one dearer (its reload). The stronger the
// call, the fewer and the longer it reloads; the carrier is one, its drones do the work. What a call lasts
// is its ammo (jets and helis are not refilled, a carrier has kCarrierSorties launches): out of it, out of
// fuel (fuelSec) or badly damaged each leaves.
// What a call brings: jets (JetLaunch), helis (HeliLaunch) or the submarine carrier (SubLaunch).
enum class Brings { jets, helis, sub };
struct Call { float mark; Brings brings; JetRole role; HeliBody body; int count; DWORD fuelSec; bool follow; const char* name;
              const wchar_t* id; };
const Call kCalls[]={
    {7101.0f,Brings::jets,JetRole::interceptor,HeliBody::eros506,2,240,false,"interceptors (guard)",L"EDF6VC_CALL_INTERCEPTOR"},
    {7102.0f,Brings::jets,JetRole::interceptor,HeliBody::eros506,2,240,true,"interceptors (follow)",L"EDF6VC_CALL_INTERCEPTOR_F"},
    {7103.0f,Brings::jets,JetRole::strike,HeliBody::eros506,3,240,false,"strike jets (guard)",L"EDF6VC_CALL_STRIKE"},
    {7104.0f,Brings::jets,JetRole::strike,HeliBody::eros506,3,240,true,"strike jets (follow)",L"EDF6VC_CALL_STRIKE_F"},
    {7105.0f,Brings::jets,JetRole::multirole,HeliBody::eros506,3,300,false,"multirole jets (guard)",L"EDF6VC_CALL_MULTIROLE"},
    {7106.0f,Brings::jets,JetRole::multirole,HeliBody::eros506,3,300,true,"multirole jets (follow)",L"EDF6VC_CALL_MULTIROLE_F"},
    {7107.0f,Brings::jets,JetRole::fighter,HeliBody::eros506,4,300,false,"fighters (guard)",L"EDF6VC_CALL_FIGHTER"},
    {7108.0f,Brings::jets,JetRole::fighter,HeliBody::eros506,4,300,true,"fighters (follow)",L"EDF6VC_CALL_FIGHTER_F"},
    {7109.0f,Brings::jets,JetRole::carrier,HeliBody::eros506,1,600,false,"carrier (guard)",L"EDF6VC_CALL_CARRIER"},
    {7110.0f,Brings::jets,JetRole::carrier,HeliBody::eros506,1,600,true,"carrier (follow)",L"EDF6VC_CALL_CARRIER_F"},
    {7111.0f,Brings::helis,JetRole::fighter,HeliBody::brute410,2,360,false,"Brute helis (guard)",L"EDF6VC_CALL_HELI"},
    {7112.0f,Brings::helis,JetRole::fighter,HeliBody::eros506,2,360,true,"Eros helis (follow)",L"EDF6VC_CALL_HELI_F"},
    {7113.0f,Brings::jets,JetRole::blastCarrier,HeliBody::eros506,1,600,false,"blast drone carrier (guard)",L"EDF6VC_CALL_BLAST_CARRIER"},
    {7114.0f,Brings::jets,JetRole::blastCarrier,HeliBody::eros506,1,600,true,"blast drone carrier (follow)",L"EDF6VC_CALL_BLAST_CARRIER_F"},
    {7115.0f,Brings::jets,JetRole::dollCarrier,HeliBody::eros506,1,600,false,"doll drone carrier (guard)",L"EDF6VC_CALL_DOLL_CARRIER"},
    {7116.0f,Brings::jets,JetRole::dollCarrier,HeliBody::eros506,1,600,true,"doll drone carrier (follow)",L"EDF6VC_CALL_DOLL_CARRIER_F"},
    // The submarine carrier surfaces kSubAhead past the marker (its 1664 m hull clear of the caller) and stays
    // the mission, following the player (subcarrier.cpp; three at most).
    {7117.0f,Brings::sub,JetRole::fighter,HeliBody::eros506,1,0,true,"submarine carrier",L"EDF6VC_CALL_SUB"},
    // The gunship (jet.cpp GunshipFire): a bomber401 circling its point and shelling the ground enemies in reach.
    {7118.0f,Brings::jets,JetRole::gunship,HeliBody::eros506,1,600,false,"gunship (guard)",L"EDF6VC_CALL_GUNSHIP"},
    {7119.0f,Brings::jets,JetRole::gunship,HeliBody::eros506,1,600,true,"gunship (follow)",L"EDF6VC_CALL_GUNSHIP_F"},
};
constexpr int kCallCount=static_cast<int>(sizeof(kCalls)/sizeof(kCalls[0]));
// kCalls' names on the in-mission pick's banner (tools/call_weapons.py KINDS' SC names).
const wchar_t* const kCallLabels[]={
    L"截击机·守点",L"截击机·跟随",L"对地攻击机·守点",L"对地攻击机·跟随",L"多用途机·守点",L"多用途机·跟随",
    L"制空战斗机·守点",L"制空战斗机·跟随",L"无人机母舰·守点",L"无人机母舰·跟随",L"武装直升机·守点",L"武装直升机·跟随",
    L"自爆无人机母舰·守点",L"自爆无人机母舰·跟随",L"人偶无人机母舰·守点",L"人偶无人机母舰·跟随",L"潜水母舰支援",
    L"炮舰机·守点",L"炮舰机·跟随",
};
static_assert(sizeof(kCallLabels)/sizeof(kCallLabels[0])==kCallCount,"a label per call");
// The in-mission pick (CallPick, overlay.cpp's keys): -1 = every call weapon brings its own call, else
// every call weapon brings kCalls[picked].
std::atomic<int> picked{-1};

// Whether `data` holds `id` as a whole NUL-terminated UTF-16LE string (the table's id column).
bool HoldsId(const unsigned char* data,std::size_t size,const wchar_t* id) noexcept {
    const std::size_t len=(std::wcslen(id)+1)*sizeof(wchar_t);
    for(std::size_t i=0;i+len<=size;i+=2)
        if((i<2 || (data[i-2]==0 && data[i-1]==0)) && std::memcmp(data+i,id,len)==0)return true;
    return false;
}

// The weapon table the game loads (Mods/WEAPON/WEAPONTABLE.SGO) must list the call weapons
// (tools/call_weapons.py install): a mod that writes its own table over it drops their rows, and the
// weapons are gone from the game. Checked once at load and logged with the fix (the table is a shared
// file and the game reads it while the plugin loads: the plugin never writes it).
void CheckCallTable() noexcept {
    wchar_t path[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,path,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(path,L'\\') : nullptr;
    if(!slash)return;
    *slash=0;
    if(wcscat_s(path,L"\\Mods\\WEAPON\\WEAPONTABLE.SGO")!=0)return;
    const HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,0,nullptr);
    if(f==INVALID_HANDLE_VALUE){Log("CALLS no Mods weapon table: the call weapons are not installed (python tools/call_weapons.py install)");return;}
    LARGE_INTEGER size{};
    unsigned char* data=nullptr;
    DWORD got=0;
    if(GetFileSizeEx(f,&size) && size.QuadPart>0 && size.QuadPart<(64<<20)) {
        data=static_cast<unsigned char*>(HeapAlloc(GetProcessHeap(),0,static_cast<SIZE_T>(size.QuadPart)));
        if(data && !ReadFile(f,data,static_cast<DWORD>(size.QuadPart),&got,nullptr))got=0;
    }
    CloseHandle(f);
    if(!data)return;
    int missing=0;
    for(const auto& c:kCalls)
        if(!HoldsId(data,got,c.id)){++missing;Log("CALLS %ls missing from the weapon table",c.id);}
    HeapFree(GetProcessHeap(),0,data);
    if(missing)Log("CALLS %d of %d call weapons missing: with the game closed run python tools/call_weapons.py install",
                   missing,static_cast<int>(sizeof(kCalls)/sizeof(kCalls[0])));
    else Log("CALLS all %d call weapons in the weapon table",static_cast<int>(sizeof(kCalls)/sizeof(kCalls[0])));
}

// The call weapon `ifc` is in, or nullptr (a stock call): what it brings is the picked call, if any.
const Call* CallOf(const void* ifc) noexcept {
    const auto w=static_cast<const unsigned char*>(ifc)-kWeaponIfc;
    if(!Readable(w+kWeaponHitSize,4))return nullptr;
    const float mark=At<float>(w,kWeaponHitSize);
    const int p=picked.load();
    for(const auto& c:kCalls)if(std::fabs(mark-c.mark)<0.5f)return p>=0 ? &kCalls[p] : &c;
    return nullptr;
}

// The call's jets or helis at `target`, coming from behind it as the player sees it (jets kApproach out
// and kAboveTarget up, helis kHeliApproach out), side by side; how many came.
int LaunchCall(const Call& c,const float* target) noexcept {
    float dir[3]={0,0,1};
    if(player.at && GameMs()-player.at<10000) {
        const float dx=target[0]-player.pos[0],dz=target[2]-player.pos[2],l=std::sqrt(dx*dx+dz*dz);
        if(l>5.0f){dir[0]=dx/l;dir[2]=dz/l;}
    }
    if(c.brings==Brings::sub) {
        const float at[3]={target[0]+dir[0]*kSubAhead,target[1],target[2]+dir[2]*kSubAhead};
        const bool ok=SubLaunch(at,dir)!=nullptr;
        Log("AIRSTRIKE call: %s %s at (%.0f,%.0f,%.0f)",c.name,ok ? "surfaced" : "failed",at[0],at[1],at[2]);
        return ok ? 1 : 0;
    }
    const bool heli=c.brings==Brings::helis;
    const float side[3]={dir[2],0,-dir[0]};
    const float back=heli ? kHeliApproach : kApproach,spacing=heli ? kHeliSpacing : kWingSpacing;
    const float up=heli ? Cfg().heliHeight : kAboveTarget;
    int launched=0;
    for(int i=0;i<c.count;++i) {
        const float off=(static_cast<float>(i)-static_cast<float>(c.count-1)*0.5f)*spacing;
        const float from[3]={target[0]-dir[0]*back+side[0]*off,target[1]+up+kWingStep*static_cast<float>(i),
                             target[2]-dir[2]*back+side[2]*off};
        if(!heli) {
            launched+=JetLaunch(c.role,from,dir,target,c.fuelSec,&kRadioSource,c.follow) ? 1 : 0;
            continue;
        }
        unsigned char* const v=HeliLaunch(c.body,from,dir);
        if(!v)continue;
        HeliCalled(v,!c.follow,target,c.fuelSec);
        ++launched;
    }
    Log("AIRSTRIKE call: %d/%d %s at (%.0f,%.0f,%.0f), fuel %lus",launched,c.count,c.name,target[0],target[1],target[2],c.fuelSec);
    return launched;
}

// The Air Raider's bomber call (redirected call at kRadioCall): IFC_Start(ifc = weapon+0x1660, params).
// A call weapon's: its jets or helis, and no bombers (when none could be launched the bombers fly).
std::uintptr_t __fastcall RadioStartHook(void* ifc,const void* params) {
    const auto result=reinterpret_cast<IfcStartFn>(image+kIfcStart)(ifc,params);
    if(!Cfg().enabled || !Cfg().jetAirRaider)return result;
    __try {
        const Call* const c=CallOf(ifc);
        const float* target=reinterpret_cast<const float*>(static_cast<const unsigned char*>(params)+kStartTarget);
        if(c && std::isfinite(target[0]+target[1]+target[2]) && LaunchCall(*c,target)>0)Put<std::int32_t>(ifc,kIfcPlanes,0);
    } __except(FaultLog("AIRSTRIKE radio call (its bombers fly)",GetExceptionInformation())) {}
    return result;
}

// After the stock init of `plane`: its jet, and the plane doomed (see the file comment).
void TakeOver(const char* who,unsigned char* plane,const float* target,const BombLoad& load,const void* source) noexcept {
    __try {
        const float* from=reinterpret_cast<const float*>(plane+kPosition);
        const float* heading=reinterpret_cast<const float*>(plane+kPlaneVelocity);
        const JetBody body=BomberBody(plane+kPlaneModel);
        const void* const ctrl=At<const void*>(plane,kSelfCtrl);
        const ULONGLONG ms=GameMs();
        Held* const h=FreeHeld(ms);
        if(!h){Log("AIRSTRIKE %s bomber %p: %d bombers held, it flies stock",who,plane,kMaxHeld);return;}
        if(!ctrl || !std::isfinite(target[0]+target[1]+target[2]) || !JetLaunchBomber(from,heading,target,load,Cfg().jetSortieSec,source,body,ctrl))return;
        *h=Held{ctrl,ms,false};
        Log("AIRSTRIKE %s bomber %p (%s model): its jet drops the bombs",who,plane,
            body==JetBody::bomber401 ? "bomber401" : body==JetBody::bomber501_2 ? "bomber501_2" : "bomber501 / unknown");
    } __except(FaultLog("AIRSTRIKE takeover (the bomber flies stock)",GetExceptionInformation())) {}
}

void __fastcall RadioBomberHook(unsigned char* plane,const float* target,const void* owner,float damage,float spread,
                                float speed,float adjust,float reach,const void* param,std::int32_t seed) {
    reinterpret_cast<BomberInitFn>(image+kBomberInit)(plane,target,owner,damage,spread,speed,adjust,reach,param,seed);
    if(Cfg().enabled && Cfg().jetAirRaider)TakeOver("air raider",plane,target,BombLoad{owner,damage,spread,speed,adjust,reach,param,seed},&kRadioSource);
}

void __fastcall MissionBomberHook(unsigned char* plane,const float* target,const void* owner,float damage,float spread,
                                  float speed,float adjust,float reach,const void* param,std::int32_t seed) {
    reinterpret_cast<BomberInitFn>(image+kBomberInit)(plane,target,owner,damage,spread,speed,adjust,reach,param,seed);
    if(Cfg().enabled && Cfg().jetMissionStrike)TakeOver("mission",plane,target,BombLoad{owner,damage,spread,speed,adjust,reach,param,seed},&kMissionSource);
}

// BombingPlane slot 5 (update), for a held plane: hidden and left as it is while its jet holds it, then
// deleted. Returns whether the plane is held (its stock update must not run). A fault in here counts as held:
// handing a taken-over plane back to its stock update would fly it again and drop its bombs a second time.
bool HeldStep(unsigned char* plane) noexcept {
    __try {
        const void* const ctrl=At<const void*>(plane,kSelfCtrl);
        for(auto& h:held) {
            if(!ctrl || h.ctrl!=ctrl)continue;
            if(!h.hidden) {
                h.hidden=true;
                Put<float>(plane,kPlaneSpeed,0.0f);
                reinterpret_cast<void(__fastcall*)(void*,std::uint8_t)>(image+kPlaneHide)(plane+kPlaneDraw,0);
                reinterpret_cast<void(__fastcall*)(void*)>(image+kPlaneUnlist)(plane);
            }
            const ULONGLONG ms=GameMs();
            if(JetHolds(ctrl) && ms-h.since<kHoldMaxMs)return true;
            Log("AIRSTRIKE bomber %p let go after %.1f s: deleted",plane,static_cast<float>(ms-h.since)*0.001f);
            h=Held{};
            reinterpret_cast<DeleteFn>(image+kDelete)(plane);
            return true;
        }
        return false;
    } __except(FaultLog("AIRSTRIKE plane update (kept as taken over)",GetExceptionInformation())) { return true; }
}

void __fastcall PlaneUpdateHook(unsigned char* plane,const void* frame) {
    if(HeldStep(plane))return;
    nextPlaneUpdate(plane,frame);
}

using UnlockFn=void(__fastcall*)(unsigned char*);
using RowCountFn=std::uint32_t(__fastcall*)(void*);
using GetRowFn=void*(__fastcall*)(void*,void*,std::uint32_t);

// Every EDF6VC_CALL_* row owned (see the file comment); how many were not yet.
int GrantCalls(unsigned char* gs) noexcept {
    if(!gs || !Readable(gs+kFlags,kMaxRecords*kRecord,true) || !Readable(gs+kCfg+kTableRef,8))return -1;
    void* const table=gs+kCfg;
    if(!At<void*>(table,kTableRef))return -1;             // no weapon table yet
    std::uint32_t n=reinterpret_cast<RowCountFn>(image+kRowCount)(table);
    if(n>kMaxRecords)n=kMaxRecords;
    alignas(8) unsigned char row[0x100];
    const std::size_t prefix=sizeof(kCallPrefix)/sizeof(wchar_t)-1;
    int granted=0;
    for(std::uint32_t id=0;id<n;++id) {
        reinterpret_cast<GetRowFn>(image+kGetRow)(table,row,id);
        const auto name=At<const wchar_t*>(row,0);
        if(!name || !Readable(name,prefix*sizeof(wchar_t)) || std::wcsncmp(name,kCallPrefix,prefix)!=0)continue;
        auto& flags=*reinterpret_cast<std::uint32_t*>(gs+kFlags+id*kRecord);
        if(!(flags&1)) {
            flags|=4;
            std::memset(gs+kStars+id*kRecord,0,8);
            ++granted;
        }
        flags|=1;
    }
    return granted;
}

void __fastcall UnlockDlcHook(unsigned char* gs) {
    __try {
        const int granted=GrantCalls(gs);
        if(granted>0)Log("CALLS %d call weapons owned now",granted);
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("CALLS granting failed");}
    reinterpret_cast<UnlockFn>(image+kUnlockDlc)(gs);
}

// Both entries of 0xDC550 redirected to UnlockDlcHook; whether both are.
bool InstallOwnership() noexcept {
    if(!Matches(kUnlockDlc,kUnlockDlcSig,sizeof(kUnlockDlcSig)) || !Matches(kUnlockDlcJump-7,kUnlockThunkSig,sizeof(kUnlockThunkSig))) {
        Log("CALLS ownership: profile mismatch");
        return false;
    }
    bool ok=true;
    const unsigned sites[]={kUnlockDlcCall,kUnlockDlcJump};
    for(const unsigned site:sites) {
        bool changed=false;
        if(RedirectCall(image+site,image+kUnlockDlc,reinterpret_cast<void*>(&UnlockDlcHook),changed))continue;
        ok=false;
        Log("CALLS ownership: %s at %X",changed ? "half patched" : "not patched",site);
    }
    return ok;
}

bool Redirect(unsigned site,const unsigned char* sig,std::size_t size,void* hook,const char* name) noexcept {
    if(!Matches(site,sig,size)){Log("AIRSTRIKE %s: profile mismatch",name);return false;}
    bool changed=false;
    const bool ok=RedirectCall(image+site,image+kBomberInit,hook,changed);
    if(!ok && changed)Log("AIRSTRIKE %s half patched",name);
    return ok;
}
}  // namespace

// One step through "each its own" and kCalls; `out` gets the banner text.
void CallPick(int step,wchar_t* out,std::size_t size) noexcept {
    const int n=kCallCount+1;
    const int p=((picked.load()+1+step)%n+n)%n-1;
    picked.store(p);
    if(p<0)swprintf_s(out,size,L"空袭呼叫：按各武器原样  [ / ]");
    else swprintf_s(out,size,L"空袭呼叫：%ls（%d/%d）  [ / ]",kCallLabels[p],p+1,kCallCount);
    Log("CALLS pick %d: %s",p,p<0 ? "each its own" : kCalls[p].name);
}

bool InstallAirstrikes() noexcept {
    CheckCallTable();
    __try {
        bool calls=false,radio=false,mission=false;
        const bool owned=InstallOwnership();
        if(Matches(kIfcStart,kIfcStartSig,sizeof(kIfcStartSig)) && Matches(kRadioCall,kRadioCallSig,sizeof(kRadioCallSig))) {
            bool changed=false;
            calls=RedirectCall(image+kRadioCall,image+kIfcStart,reinterpret_cast<void*>(&RadioStartHook),changed);
            if(!calls && changed)Log("AIRSTRIKE radio call half patched");
        } else Log("AIRSTRIKE bomber call: profile mismatch");
        // The plane update first: a bomber taken over must never fly.
        const auto slot=reinterpret_cast<void**>(image+kPlaneUpdateSlot);
        bool update=false;
        if(Matches(kBomberInit,kBomberInitSig,sizeof(kBomberInitSig)) && Matches(kPlaneUpdate,kPlaneUpdateSig,sizeof(kPlaneUpdateSig)) && *slot) {
            void* const current=*slot;
            if(current!=image+kPlaneUpdate)Log("AIRSTRIKE plane update: chaining onto %p (another plugin)",current);
            nextPlaneUpdate=reinterpret_cast<PlaneUpdateFn>(current);
            update=PatchVtableSlot(slot,current,reinterpret_cast<void*>(&PlaneUpdateHook));
        } else Log("AIRSTRIKE bomber: profile mismatch");
        if(update) {
            radio=Redirect(kRadioBomber,kRadioBomberSig,sizeof(kRadioBomberSig),reinterpret_cast<void*>(&RadioBomberHook),"air raider bomber");
            mission=Redirect(kMissionBomber,kMissionBomberSig,sizeof(kMissionBomberSig),reinterpret_cast<void*>(&MissionBomberHook),"mission bomber");
        }
        Log("HOOK airstrikes calls=%d owned=%d airRaiderBombers=%d missionBombers=%d",calls,owned,radio,mission);
        return calls || radio || mission;
    } __except(FaultLog("AIRSTRIKE install",GetExceptionInformation())){return false;}
}
// A new mission (mission.cpp MissionStart): the held bombers were the last mission's (their control blocks'
// addresses may be the new mission's objects'): forgotten, nothing of them touched. The call pick stays.
void ResetAirstrikes() noexcept {
    for(auto& h:held)h=Held{};
}
}  // namespace crew
