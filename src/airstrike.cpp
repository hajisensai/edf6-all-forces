// Aircraft support uses the shared deployment transaction: verified entry, real crew, all-peer ACK,
// actual boarding, then takeoff. Stock RadioContact / mission BombingPlane objects retain their native
// init, update, payload and cleanup. Replacing them through Launch() would create an airborne empty
// hull: recruiting nearby soldiers cannot staff an aircraft already outside the map at flight height.
//  - The Air Raider's call weapons (tools/call_weapons.py: EDF6VC_CALL_*, KM6 bomber calls told apart by
//    their AmmoHitSizeAdjust, weapon+0x8C4, kCalls' marks): at the call (its one call of IFC_Start,
//    0x6A8DFB) the plugin launches that call's jets or helis instead of its bombers, which it keeps home
//    (the call's plane count, ifc+0x80, set to 0 after IFC_Start: the call is spent, its state machine
//    goes back to idle). A guard call works round its marker, a follow call round the player. Each
//    caller submits once; the host sends the verified entry and actors through support_net. The local
//    picker changes only a local player's call; online the pick goes out in the call's seed (call_net.h,
//    SeedSendHook) and every machine, the caller's too, replays that pick (docs/online-re.md sections 1, 9, 11).
//  - The call weapons are owned from the start (docs/loadout-re.md section 8): before the game's own
//    "grant the installed DLC weapons" step (0xDC550, UnlockDownloadContents: after every save load, and
//    in a new game's reset; its two entries, a call at 0xDC348 and the script thunk's jump at 0x70FF87,
//    are redirected) every weapon table row named EDF6VC_CALL_* gets the owned bit, as that step gives a
//    DLC weapon its: NEW and 0 stars the first time, stars left alone after. Before it, so its "equipped
//    but not owned: back to the default weapon" pass keeps them on the soldiers that carry them.
//  - The thrown drones (tools/call_weapons.py: EDF6VC_CALL_THROW_*, Patroller clones in the Robot Bomb list, told
//    apart by their AmmoHitSizeAdjust's bits, kThrows): the Weapon_Sub's shot (its vtable slot 17, called with the
//    bullet it just made, 0x697DDD) of one of ours, a BombBullet01, is kept (bombs[]); at that bomb's update
//    (BombBullet01 slot 5), once it has landed (its landed byte, +0xD40, which its own update tests at 0x29637B) or
//    kThrowFuseMs after the throw, and if its owner is the local player, the drone is launched where the bomb is
//    (JetLaunchThrown) and the bomb deleted (0x118A1B0, as its own update deletes a spent bomb at 0x2962D5) before
//    its update runs, so the stock patroller never starts. A drone that cannot be launched leaves the stock bomb as
//    it is. Only the thrower's game: in an online game the others see the stock patroller (their copy's owner is no
//    local player). Kills are the drone's (an NPC friend's), as a call's jets' are.
// Refused custom support does not fall back to a stock strike that could violate mission restrictions.
// Without the plugin the call weapons are plain KM6 calls (still owned: the bit is in the save), the thrown drones
// plain Patrollers.
// The other scripted strikes (DemoIndirectFire, gunship fire, missiles, satellite laser) are shells out
// of the sky with no plane to take over, and stay stock.
#include "crew.h"
#include "call_net.h"
#include "support_call.h"
#include "support_aircraft.h"
#include "support_entry.h"
#include "jet_internal.h"   // FaultLog
#include "memory.h"
#include "online_authority.h"
#include <atomic>
#include <cmath>
#include <cwchar>
#include <optional>

namespace crew {
namespace {
constexpr unsigned kIfcStart=0x2B5DA0,kRadioCall=0x6A8DFB;
constexpr std::size_t kIfcPlanes=0x80,kStartForward=0x40,kStartTarget=0x50;   // IFC_Start's params: its matrix's rows
constexpr unsigned kDelete=0x118A1B0;
constexpr float kAboveTarget=150.0f;
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
// The weapon a radio call's IndirectFireControl is in (ifc = weapon+0x1660), and its AmmoHitSizeAdjust.
constexpr std::size_t kWeaponIfc=0x1660,kWeaponHitSize=0x8C4;
// RadioContact: its owner (+0x120, 0x6A938A), its random state (+0xBC8, message 9's seed), the seed a received call
// brought (+0x1958, written by the receive slot 30 at 0x6A8724, kept until the next one); the confirm state's send of the
// seed (0x6A936A: mov rdx,[rbx+0xBC8]; lea rcx,[rbp-0x60]; call 0x12B5690, rbx the weapon) and the message's u64 writer
// (0x12B5690: the value as it is). See call_net.h.
constexpr std::size_t kWeaponOwner=0x120,kWeaponSeed=0xBC8,kWeaponRxSeed=0x1958;
constexpr unsigned kSeedSend=0x6A936A,kSeedCall=0x6A9375,kWriteU64=0x12B5690;
const unsigned char kSeedSendSig[]={0x48,0x8B,0x93,0xC8,0x0B,0x00,0x00,0x48,0x8D,0x4D,0xA0,0xE8};
const unsigned char kWriteU64Sig[]={0x4C,0x8B,0xC1,0x48,0x8D,0x82,0xFF,0xFF,0xFF,0x7F,0xB9,0xFD,0xFF,0xFF,0xFF};

const unsigned char kIfcStartSig[]={0x48,0x89,0x5C,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83,0xEC,0x60,0x48,0x8B,0x05};
// call IFC_Start; then the caller marks the call active and copies the plane count (+0x16E0 -> +0x16E4)
const unsigned char kRadioCallSig[]={0xE8,0xA0,0xCF,0xC0,0xFF,0xC6,0x87,0xEC,0x16,0x00,0x00,0x01,0x8B,0x87,0xE0,0x16};
using IfcStartFn=std::uintptr_t(__fastcall*)(void*,const void*);
using DeleteFn=void(*)(void*);

// The call weapons, made from one table (tools/calls.py): tools/call_weapons.py writes their weapon rows and
// SGOs, tools/gen_calls.py writes calls.inc below (CI checks it is current). Per role a guard call (round its
// marker) and a follow call (round the player), the follow one dearer (its reload). The stronger the call,
// the fewer and the longer it reloads; the carrier is one, its drones do the work. What a call lasts is its
// ammo (jets and helis are not refilled, a carrier has kCarrierSorties launches): out of it, out of fuel
// (fuelSec) or badly damaged each leaves.
// What a call brings: jets (`role`), helis (`body`; both made by the support deployment) or the submarine carrier
// (SubLaunch); the field of what it does not bring is empty.
enum class Brings { jets, helis, sub };
struct Call { float mark; Brings brings; std::optional<JetRole> role; std::optional<HeliBody> body; int count; DWORD fuelSec;
              bool follow; const char* name; const wchar_t* id; };
// A weapon table row tools/call_weapons.py installs (a vehicle request too): its id, its SGO in Mods/WEAPON and
// the object SGO a vehicle request brings in Mods/OBJECT (nullptr: none).
struct CallRow { const wchar_t* id; const wchar_t* weaponFile; const wchar_t* objectFile; };
// A thrown drone's weapon (tools/calls.py brings 'throw'): its AmmoHitSizeAdjust's bits (calls.throw_mark), the
// drone its bomb releases and that drone's fuel.
struct Throw { std::uint32_t markBits; ThrownDrone drone; DWORD fuelSec; const char* name; const wchar_t* id; };
#include "calls.inc"
// The in-mission pick (CallPick, overlay.cpp's keys): -1 = each weapon's own call; otherwise only local players'
// call weapons bring kCalls[picked]. This UI preference is not carried in the native radio-call message.
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
// Whether <game>/Mods/<dir>/<file> is there.
bool ModFileThere(const wchar_t* game,const wchar_t* dir,const wchar_t* file) noexcept {
    wchar_t path[MAX_PATH];
    if(swprintf_s(path,L"%ls\\Mods\\%ls\\%ls",game,dir,file)<0)return false;
    const DWORD a=GetFileAttributesW(path);
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY);
}

// Every row tools/call_weapons.py installs (kCallRows, the vehicle requests too): in the weapon table, its
// weapon SGO in Mods/WEAPON and the vehicle SGO a request brings in Mods/OBJECT. A missing one is said, so a
// mod that overwrote the shared table or a half install shows in the log.
void CheckCallTable() noexcept {
    wchar_t game[MAX_PATH];
    const DWORD n=GetModuleFileNameW(nullptr,game,MAX_PATH);
    wchar_t* slash=n && n<MAX_PATH ? wcsrchr(game,L'\\') : nullptr;
    if(!slash)return;
    *slash=0;
    wchar_t path[MAX_PATH];
    if(swprintf_s(path,L"%ls\\Mods\\WEAPON\\WEAPONTABLE.SGO",game)<0)return;
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
    for(const auto& r:kCallRows) {
        const bool row=HoldsId(data,got,r.id),weapon=ModFileThere(game,L"WEAPON",r.weaponFile);
        const bool object=!r.objectFile || ModFileThere(game,L"OBJECT",r.objectFile);
        if(row && weapon && object)continue;
        ++missing;
        Log("CALLS %ls:%s%s%s",r.id,row ? "" : " not in the weapon table",weapon ? "" : " (its weapon SGO is missing)",
            object ? "" : " (the vehicle SGO it brings is missing)");
    }
    HeapFree(GetProcessHeap(),0,data);
    if(missing)Log("CALLS %d of %d rows incomplete: with the game closed run the installer (install) again",missing,kCallRowCount);
    else Log("CALLS all %d rows in the weapon table, their files there",kCallRowCount);
}

// The plugin's call weapon `w` is (its mark), or nullptr (a stock call).
const Call* OwnCall(const unsigned char* w) noexcept {
    if(!Readable(w+kWeaponHitSize,4))return nullptr;
    const float mark=At<float>(w,kWeaponHitSize);
    for(const auto& c:kCalls)if(std::fabs(mark-c.mark)<0.5f)return &c;
    return nullptr;
}

// The picks this machine sent with its calls (SeedSendHook), per weapon: the caller replays the pick it sent even if its
// picker moved during the confirm state. Game thread only.
struct SentPick { const unsigned char* weapon; int pick; };
SentPick sentPicks[8]{};
int sentNext=0;

int SentPickOf(const unsigned char* w) noexcept {
    for(const auto& s:sentPicks)if(s.weapon==w)return s.pick;
    return callnet::kNoMark;
}

// The call weapon `ifc` is in, or nullptr (a stock call). The owner comes from IFC_Start's parameter weak pair, populated
// by the native RadioContact call state.
//  - offline (no message is sent, nothing is decoded): a local player's call takes this machine's pick, any other its
//    weapon's own: origin/main's rule;
//  - online, a call of another machine's player (the only kind replayed from a received message): the pick its caller
//    sent (call_net.h, the received seed +0x1958), else the weapon's own;
//  - online, this machine's player: the pick it sent (SentPickOf), else this machine's pick.
const Call* CallOf(const void* ifc,const unsigned char* owner) noexcept {
    const auto w=static_cast<const unsigned char*>(ifc)-kWeaponIfc;
    const Call* const own=OwnCall(w);
    if(!own)return nullptr;
    int p=-1;
    if(InSession() && edf::RemoteRider(owner)) {
        const int sent=Readable(w+kWeaponRxSeed,8) ? callnet::Decode(At<std::uint64_t>(w,kWeaponRxSeed)) : callnet::kNoMark;
        p=sent!=callnet::kNoMark ? sent : -1;
    } else if(IsPlayer(owner)) {
        const int sent=InSession() ? SentPickOf(w) : callnet::kNoMark;
        p=sent!=callnet::kNoMark ? sent : picked.load();
    }
    return p>=0 && p<kCallCount ? &kCalls[p] : own;
}

// The confirm state's send of the call's seed (kSeedCall, through a stub: rcx the message, rdx the seed, r8 the weapon):
// a local player's call of one of ours goes out with this machine's pick in the seed, and the weapon keeps the seed sent
// (its random state goes on from the value every other machine replays from).
using WriteU64Fn=bool(__fastcall*)(void*,std::uint64_t);
bool __fastcall SeedSendHook(void* message,std::uint64_t seed,unsigned char* weapon) {
    std::uint64_t sent=seed;
    __try {
        if(Cfg().enabled && Cfg().jetAirRaider && OwnCall(weapon) && IsPlayer(At<const unsigned char*>(weapon,kWeaponOwner))) {
            const int pick=picked.load();
            sent=callnet::Encode(seed,pick);
            Put<std::uint64_t>(weapon,kWeaponSeed,sent);
            SentPick* slot=nullptr;
            for(auto& s:sentPicks)if(s.weapon==weapon)slot=&s;
            if(!slot){slot=&sentPicks[sentNext];sentNext=(sentNext+1)%8;}
            *slot=SentPick{weapon,pick};
        }
    } __except(FaultLog("AIRSTRIKE call pick send (the seed as it was)",GetExceptionInformation())) { sent=seed; }
    return reinterpret_cast<WriteU64Fn>(image+kWriteU64)(message,sent);
}

bool OpenSky(const float* target) noexcept {
    const float from[3]={target[0],target[1]+2.0f,target[2]},to[3]={target[0],target[1]+3000.0f,target[2]};
    float hit[3];return MapRay(from,to,hit)<0.0f;
}
bool EntryHeight(float x,float z,float level,float& out) noexcept {
    // The topmost actual surface: terrain, roof or cave roof. OpenSky excludes enclosed destinations.
    const float from[3]={x,level+3000.0f,z},to[3]={x,level-3000.0f,z};
    float hit[3];if(MapRay(from,to,hit)<0.0f)return false;
    out=hit[1];return true;
}
bool EntryClear(const float* from,const float* to) noexcept {
    // Five rays cover the centre and a 50 m envelope rather than a point-size aircraft.
    constexpr float offsets[7][3]={{0,0,0},{45,0,0},{-45,0,0},{0,45,0},{0,-45,0},{0,0,45},{0,0,-45}};
    for(const auto& offset:offsets) {
        const float a[3]={from[0]+offset[0],from[1]+offset[1],from[2]+offset[2]};
        const float b[3]={to[0]+offset[0],to[1]+offset[1],to[2]+offset[2]};
        float hit[3];if(MapRay(a,b,hit)>=0.0f)return false;
    }
    return true;
}

// The Air Raider's bomber call (redirected call at kRadioCall): IFC_Start(ifc = weapon+0x1660, params).
// A call weapon's: its jets or helis, and no bombers (when none could be launched the bombers fly).
std::uintptr_t __fastcall RadioStartHook(void* ifc,const void* params) {
    const auto result=reinterpret_cast<IfcStartFn>(image+kIfcStart)(ifc,params);
    if(!Cfg().enabled || !Cfg().jetAirRaider)return result;
    __try {
        const auto owner=At<const unsigned char*>(params,0);   // params+0: owner weak pointer (docs/airstrike-re.md section 2)
        const Call* const c=CallOf(ifc,owner);
        const float* target=reinterpret_cast<const float*>(static_cast<const unsigned char*>(params)+kStartTarget);
        if(c && std::isfinite(target[0]+target[1]+target[2])) {
            const online::CopyOwner was=SetSpawnOwner(CopyOwnerOfCaller(owner));   // its copies are the caller's (online_authority.h)
            wchar_t note[128]{};
            // The radio callback is replayed for remote players too. Only the actual caller submits
            // one request; the support protocol commits the host-selected entry on every peer.
            if(!InSession() || IsPlayer(owner))SupportCallAt(static_cast<int>(c-kCalls),target,note,_countof(note));
            SetSpawnOwner(was);
            // A refused custom support must not silently turn into stock bombers (especially underground).
            Put<std::int32_t>(ifc,kIfcPlanes,0);
            Log("AIRSTRIKE queued support: %ls",note);
        }
    } __except(FaultLog("AIRSTRIKE radio call (its bombers fly)",GetExceptionInformation())) { SetSpawnOwner(online::kCopyHost); }
    return result;
}

// --- Thrown drones (see the file comment) ---
// Weapon_Sub's vtable slot 17 (0x6AD370: it keeps the bullet in the weapon's list, weapon+0x1658), called by the
// weapons' fire step with the bullet just made (0x697DD7: rdx = the bullet, possibly null). BombBullet01's vtable
// (+0) and slot 5, its update (bullet, frame): +0x90 its position (the matrix's last row, copied from +0xC90 each
// update), +0xAE8 its owner (weak_ptr object, read so at 0x2972A5 for the blast's owner), +0xC34 bit 0 spent (its
// update deletes it then, 0x2962BE), +0xD40 landed (0x29637B).
constexpr unsigned kSubShotSlot=0x17E54C8,kSubShot=0x6AD370,kSubShotCall=0x697DD7;
constexpr unsigned kBombVtable=0x17A47D0,kBombStepSlot=0x17A47D0+5*8,kBombStep=0x2962A0;
constexpr unsigned kBombSpentTest=0x2962BE,kBombLandedTest=0x29637B,kBombOwnerRead=0x2972A5;
constexpr std::size_t kBombPos=0x90,kBombOwner=0xAE8,kBombFlags=0xC34,kBombLanded=0xD40;
const unsigned char kSubShotSig[]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x41,0x56,
                                   0x41,0x57,0x48,0x83,0xEC,0x60,0x4C,0x8B,0xC2,0x8B,0x81,0x78,0x16,0x00,0x00};
const unsigned char kSubShotCallSig[]={0x48,0x8B,0xD3,0x49,0x8B,0xCF,0xFF,0x90,0x88,0x00,0x00,0x00};   // mov rdx,rbx; call [rax+88h]
const unsigned char kBombStepSig[]={0x40,0x53,0x55,0x41,0x56,0x48,0x81,0xEC,0xA0,0x00,0x00,0x00,0x48,0x8B,0xD9,0x48};
const unsigned char kBombSpentSig[]={0xF6,0x83,0x34,0x0C,0x00,0x00,0x01};    // at 0x2962BE: test byte [rbx+0C34h],1
const unsigned char kBombLandedSig[]={0x80,0xBB,0x40,0x0D,0x00,0x00,0x00};   // at 0x29637B: cmp byte [rbx+0D40h],0
const unsigned char kBombOwnerSig[]={0x48,0x8B,0x8E,0xE8,0x0A,0x00,0x00};    // at 0x2972A5: mov rcx,[rsi+0AE8h]
using SubShotFn=std::uintptr_t(__fastcall*)(void*,unsigned char*);
using BombStepFn=void(__fastcall*)(unsigned char*,const void*);
SubShotFn nextSubShot=nullptr;
BombStepFn nextBombStep=nullptr;
const char kThrowSource='t';   // the thrown drones' flight source (JetLaunch's `source`)

// The bombs of ours thrown and not yet landed: the bullet, what it releases, when. Game thread only (the fire step
// and the bullets' updates). A bomb converts within kThrowFuseMs, so an entry older than kBombStaleMs is one whose
// bomb went without an update of its own (the mission's end, a delete from elsewhere). Whose bomb it is is read at
// its landing, not at the shot: where the bullet's owner is written in its making is not traced (static RE), and by
// its landing it is (the bomb's blast reads it, 0x2972A5); only the local player's becomes a drone. An entry is the
// bullet's ObjRef (its address and weak-this block): a bomb deleted without an update of its own leaves its entry, and
// a new bomb at the same address (a stock Patroller thrown alongside) is not taken for it; stale entries go at every
// throw and every bomb update.
struct Bomb { ObjRef bullet; const Throw* what; ULONGLONG at; };
constexpr int kMaxBombs=16;
constexpr ULONGLONG kThrowFuseMs=4000,kBombStaleMs=10000;
Bomb bombs[kMaxBombs]{};
int bombCount=0;   // entries in use: the bombs' update skips the table while 0

void Forget(Bomb& b) noexcept {
    b=Bomb{};
    --bombCount;
}

// The thrown drone weapon `weapon` is, or nullptr: its AmmoHitSizeAdjust's bits exactly (a stock weapon's are
// 0x3F800000 where near 1).
const Throw* ThrowOf(const unsigned char* weapon) noexcept {
    if(!Readable(weapon+kWeaponHitSize,4))return nullptr;
    const auto bits=At<std::uint32_t>(weapon,kWeaponHitSize);
    for(const auto& t:kThrows)if(bits==t.markBits)return &t;
    return nullptr;
}

// After the stock shot: a bomb of one of ours, kept for its landing.
void SeeThrow(const unsigned char* weapon,const unsigned char* bullet) noexcept {
    const Throw* const t=ThrowOf(weapon);
    if(!t || !bullet || !Readable(bullet,kBombLanded+1) || At<const void*>(bullet,0)!=image+kBombVtable)return;
    const ULONGLONG ms=GameMs();
    for(auto& b:bombs)if(b.bullet && ms-b.at>kBombStaleMs)Forget(b);
    const ObjRef ref=ObjRef::Of(bullet);
    if(!ref.ctrl)return;
    for(auto& b:bombs) {
        if(b.bullet)continue;
        b=Bomb{ref,t,ms};
        ++bombCount;
        if(Cfg().debug)Log("THROW %s: bomb %p",t->name,bullet);
        return;
    }
    Log("THROW %s: %d bombs in the air already, this one stays a Patroller",t->name,kMaxBombs);
}

std::uintptr_t __fastcall SubShotHook(unsigned char* weapon,unsigned char* bullet) {
    const auto result=nextSubShot(weapon,bullet);
    if(Cfg().enabled && Cfg().throwDrones) {
        __try { SeeThrow(weapon,bullet); } __except(FaultLog("THROW shot (the stock bomb)",GetExceptionInformation())) {}
    }
    return result;
}

// Where the drone heads off: away from the thrower (the player as last seen), else along +z.
void ThrowHeading(const float* at,float* out) noexcept {
    out[0]=0.0f;out[1]=0.0f;out[2]=1.0f;
    if(!player.at || GameMs()-player.at>10000)return;
    const float dx=at[0]-player.pos[0],dz=at[2]-player.pos[2],l=std::sqrt(dx*dx+dz*dz);
    if(l>1.0f){out[0]=dx/l;out[2]=dz/l;}
}

// At a bomb's update: one of ours landed (or kThrowFuseMs out) becomes its drone and is deleted. True when it is
// (its update must not run: the bomb is gone); false: the stock update runs (not ours, not yet, or no drone).
bool BombStep(unsigned char* bullet) noexcept {
    __try {
        const ULONGLONG ms=GameMs();
        const bool on=Cfg().enabled && Cfg().throwDrones;
        for(auto& b:bombs) {
            if(b.bullet && (!on || ms-b.at>kBombStaleMs)){Forget(b);continue;}   // switched off since, or long gone
            if(!b.bullet.Is(bullet))continue;
            if(At<const void*>(bullet,0)!=image+kBombVtable || (bullet[kBombFlags]&1)) {
                Forget(b);   // something else at its address, or spent: its own update deletes it
                return false;
            }
            if(!bullet[kBombLanded] && ms-b.at<kThrowFuseMs)return false;
            if(!IsPlayer(At<const unsigned char*>(bullet,kBombOwner))) {
                Forget(b);   // another machine's player's (online: their copy of it) or a soldier's: the stock bomb
                return false;
            }
            const Throw* const t=b.what;
            const bool landed=bullet[kBombLanded]!=0;
            Forget(b);
            float at[3],dir[3];
            std::memcpy(at,bullet+kBombPos,12);
            if(!std::isfinite(at[0]+at[1]+at[2]))return false;
            ThrowHeading(at,dir);
            const online::CopyOwner was=SetSpawnOwner(online::kCopyHere);   // the thrower is this machine's player (IsPlayer above)
            const bool thrown=JetLaunchThrown(t->drone,at,dir,t->fuelSec,&kThrowSource)!=nullptr;
            SetSpawnOwner(was);
            if(!thrown) {
                Log("THROW %s: no drone (not installed, not preloaded or too many out): it stays a Patroller",t->name);
                return false;
            }
            Log("THROW %s at (%.0f,%.0f,%.0f), %s: the bomb is its drone now",t->name,at[0],at[1],at[2],landed ? "landed" : "still falling");
            reinterpret_cast<DeleteFn>(image+kDelete)(bullet);
            return true;
        }
        return false;
    } __except(FaultLog("THROW bomb update (the stock bomb)",GetExceptionInformation())) { SetSpawnOwner(online::kCopyHost); return false; }
}

void __fastcall BombStepHook(unsigned char* bullet,const void* frame) {
    if(bombCount>0 && BombStep(bullet))return;
    nextBombStep(bullet,frame);
}

// What vtable slot `slotRva` holds now, its stock `original` or another plugin's hook (chained onto), or nullptr.
void* SlotNow(unsigned slotRva,unsigned original,const char* name) noexcept {
    void* const current=*reinterpret_cast<void**>(image+slotRva);
    if(current && current!=image+original)Log("THROW %s: chaining onto %p (another plugin)",name,current);
    return current;
}

// The bombs' update first (a bomb kept must always be seen at its landing), then the shot.
bool InstallThrows() noexcept {
    if(!Matches(kSubShot,kSubShotSig,sizeof(kSubShotSig)) || !Matches(kSubShotCall,kSubShotCallSig,sizeof(kSubShotCallSig)) ||
       !Matches(kBombStep,kBombStepSig,sizeof(kBombStepSig)) || !Matches(kBombSpentTest,kBombSpentSig,sizeof(kBombSpentSig)) ||
       !Matches(kBombLandedTest,kBombLandedSig,sizeof(kBombLandedSig)) || !Matches(kBombOwnerRead,kBombOwnerSig,sizeof(kBombOwnerSig)) ||
       !Readable(image+kBombVtable,8)) {
        Log("THROW thrown drones: profile mismatch (they stay Patrollers)");
        return false;
    }
    void* const step=SlotNow(kBombStepSlot,kBombStep,"bomb update");
    if(!step)return false;
    nextBombStep=reinterpret_cast<BombStepFn>(step);
    if(!PatchVtableSlot(reinterpret_cast<void**>(image+kBombStepSlot),step,reinterpret_cast<void*>(&BombStepHook)))return false;
    void* const shot=SlotNow(kSubShotSlot,kSubShot,"Weapon_Sub shot");
    if(!shot)return false;
    nextSubShot=reinterpret_cast<SubShotFn>(shot);
    return PatchVtableSlot(reinterpret_cast<void**>(image+kSubShotSlot),shot,reinterpret_cast<void*>(&SubShotHook));
}

using UnlockFn=void(__fastcall*)(unsigned char*);
using RowCountFn=std::uint32_t(__fastcall*)(void*);
using GetRowFn=void*(__fastcall*)(void*,void*,std::uint32_t);

// Every EDF6VC_CALL_* row owned (see the file comment); how many were not yet.
// Whether a weapon table row's name is one of ours (kCallRows): exactly, so a retired row (EDF6VC_RETIRED_*,
// an uninstall's placeholder) or another mod's row is never made owned.
bool IsCallRow(const wchar_t* name) noexcept {
    for(const auto& r:kCallRows) {
        const std::size_t len=std::wcslen(r.id)+1;
        if(Readable(name,len*sizeof(wchar_t)) && std::wcsncmp(name,r.id,len)==0)return true;
    }
    return false;
}

int GrantCalls(unsigned char* gs) noexcept {
    if(!gs || !Readable(gs+kFlags,kMaxRecords*kRecord,true) || !Readable(gs+kCfg+kTableRef,8))return -1;
    void* const table=gs+kCfg;
    if(!At<void*>(table,kTableRef))return -1;             // no weapon table yet
    std::uint32_t n=reinterpret_cast<RowCountFn>(image+kRowCount)(table);
    if(n>kMaxRecords)n=kMaxRecords;
    alignas(8) unsigned char row[0x100];
    int granted=0;
    for(std::uint32_t id=0;id<n;++id) {
        reinterpret_cast<GetRowFn>(image+kGetRow)(table,row,id);
        const auto name=At<const wchar_t*>(row,0);
        if(!name || !IsCallRow(name))continue;
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

// The call's pick into its message (SeedSendHook): the confirm state's call of the u64 writer at kSeedCall, through a stub
// that hands the hook the weapon (rbx there) as its third argument. Off (the calls replay each
// weapon's own on the other machines, as before) when either piece of code is not the one read.
bool InstallPickSend() noexcept {
    if(!Matches(kSeedSend,kSeedSendSig,sizeof(kSeedSendSig)) || !Matches(kWriteU64,kWriteU64Sig,sizeof(kWriteU64Sig))) {
        Log("AIRSTRIKE call pick send: profile mismatch (online, the others fly each call weapon's own)");
        return false;
    }
    // mov r8,rbx; mov rax,SeedSendHook; jmp rax
    unsigned char stub[]={0x49,0x89,0xD8,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    void* const hook=reinterpret_cast<void*>(&SeedSendHook);
    std::memcpy(stub+5,&hook,sizeof(hook));
    void* const code=AllocateNearCode(image+kSeedCall,stub,sizeof(stub));
    bool changed=false;
    const bool ok=code && RedirectCall(image+kSeedCall,image+kWriteU64,code,changed);
    if(!ok)Log("AIRSTRIKE call pick send %s",changed ? "half patched" : "not patched");
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

bool SupportAircraftSpec(int catalog,SupportAircraft* out) noexcept {
    if(!out || catalog<0 || catalog>=kCallCount || kCalls[catalog].brings==Brings::sub)return false;
    const auto& c=kCalls[catalog];
    *out={c.role ? static_cast<int>(*c.role) : -1,c.body ? static_cast<int>(*c.body) : -1,
          c.count,c.fuelSec,c.follow};
    return true;
}
int SupportAirCallCount() noexcept { return kCallCount; }
// The call's configuration key: its weapon row id without EDF6VC_CALL_ (INTERCEPTOR, HELI_F, ...).
const wchar_t* SupportAirCallKey(int index) noexcept {
    constexpr std::size_t prefix=12;   // L"EDF6VC_CALL_"
    return index>=0 && index<kCallCount && std::wcslen(kCalls[index].id)>prefix ? kCalls[index].id+prefix : nullptr;
}
const wchar_t* SupportAirCallName(int index) noexcept {
    return index>=0 && index<kCallCount ? kCallLabels[index] : L"支援";
}
support::Refusal PlanAirSupport(int catalog,const float* target,const float* observer,support::Route* route,int count) noexcept {
    SupportAircraft spec;
    if(!route || !SupportAircraftSpec(catalog,&spec))return support::Refusal::unsupported;
    return PlanAirSupportFor(spec,target,observer,route,count);
}
support::Refusal PlanAirSupportFor(SupportAircraft spec,const float* target,const float* observer,support::Route* route,int count) noexcept {
    if(!route || !target)return support::Refusal::unsupported;
    if(count>0)spec.count=count;
    if(!OpenSky(target))return support::Refusal::noSky;
    float direction[3]={0,0,1};
    // Host chooses the entire plan once. Peers receive its explicit matrices.
    if(observer) {
        const float x=target[0]-observer[0],z=target[2]-observer[2],d=std::hypot(x,z);
        if(d>0.1f){direction[0]=x/d;direction[2]=z/d;}
    }
    // Air support is created in the air at the route's entry and flies in (2026-10-09, the user: it comes from off
    // the field; no takeoff). The route (support::AirRoute) already stands its entry over the highest ground of the
    // whole line plus the altitude; here every aircraft's formation slot (support::AirFormationSlot) must lie inside
    // the measured area, over its own ground by that altitude, and have the full-size corridor (EntryClear) clear to
    // its own end over the target.
    const PlayArea area=MapPlayArea();
    const float altitude=spec.heli>=0 ? std::fmax(Cfg().heliHeight,60.0f) : kAboveTarget;
    const float spacing=spec.heli>=0 ? 0.6f : 1.0f;
    const auto entry=[&](const float* from,const float* to) noexcept {
        const float dx=to[0]-from[0],dz=to[2]-from[2],d=std::hypot(dx,dz);
        if(d<1)return false;
        support::Route lead{{from[0],from[1],from[2]},{dx/d,0.0f,dz/d}};
        for(int i=0;i<spec.count;++i) {
            float at[3];support::AirFormationSlot(lead,i,spacing,at);
            if(at[0]<area.lo[0] || at[0]>area.hi[0] || at[2]<area.lo[1] || at[2]>area.hi[1])return false;
            float ground;
            if(EntryHeight(at[0],at[2],target[1],ground) && ground+altitude*0.5f>at[1])return false;   // over its own ground too
            const float end[3]={at[0]+dx,at[1],at[2]+dz};
            if(!EntryClear(at,end))return false;
        }
        return true;
    };
    return support::AirRoute(area,target,observer,direction,altitude,entry,EntryHeight,*route);
}

support::Refusal PlanTakeoffSupport(const SupportAircraft& spec,const float* target,const float (*spots)[3],int count,
                                    support::Route* route) noexcept {
    if(!route || !target || spec.count!=1 || (spec.jet<0 && spec.heli<0))return support::Refusal::unsupported;
    if(!OpenSky(target))return support::Refusal::noSky;
    return support::TakeoffRoute(MapPlayArea(),target,spots,count,EntryClear,*route);
}

bool InstallAirstrikes() noexcept {
    CheckCallTable();
    __try {
        bool calls=false;
        const bool owned=InstallOwnership();
        if(Matches(kIfcStart,kIfcStartSig,sizeof(kIfcStartSig)) && Matches(kRadioCall,kRadioCallSig,sizeof(kRadioCallSig))) {
            bool changed=false;
            calls=RedirectCall(image+kRadioCall,image+kIfcStart,reinterpret_cast<void*>(&RadioStartHook),changed);
            if(!calls && changed)Log("AIRSTRIKE radio call half patched");
        } else Log("AIRSTRIKE bomber call: profile mismatch");
        // Native bombers are not crewed support vehicles. Leave both native callsites and the
        // update vtable untouched so their original payload/timeline cannot be suppressed.
        const bool throws=InstallThrows();
        const bool pickSend=calls && InstallPickSend();
        Log("HOOK airstrikes calls=%d owned=%d stockBombers=native throws=%d pickSend=%d",calls,owned,throws,pickSend);
        return calls || throws;
    } __except(FaultLog("AIRSTRIKE install",GetExceptionInformation())){return false;}
}
// A new mission (mission.cpp MissionStart): the thrown bombs were the last mission's (their
// addresses may be the new mission's objects'): forgotten, nothing of them touched. The call pick stays.
void ResetAirstrikes() noexcept {
    for(auto& b:bombs)b=Bomb{};
    bombCount=0;
}
}  // namespace crew
