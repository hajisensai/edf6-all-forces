// The Air Raider's weapons the plugin still reads, and the map's flown supports and strikes (the user, 2026-10-26:
// 「所有插件支援改成m里面消耗pt召唤，空袭兵召唤的支援武器删掉」):
//  - The supports (tools/calls.py on_map: the flown calls kCalls, the strikes kStrikes) are entries of the map's support bar
//    (support_dispatch.cpp's catalog: kCalls first, kStrikes after the airdrops), each for its pt (support_points.h). Their
//    weapon rows are placeholders now (tools/call_weapons.py: EDF6VC_RETIRED_*, kMovedRows): their index kept (a save
//    refers to a weapon by its row), un-owned at every save load (Grant) so the game's own pass (0xDC550's last part)
//    puts any soldier that had one equipped back on its default weapon. No radio call of the plugin's is read any more:
//    the stock bombers, Vesta, Phobos and the missions' strafing runs keep their native init, payload and cleanup.
//  - A flown support (kCalls) uses the shared deployment transaction: verified entry, real crew, all-peer ACK, actual
//    boarding, then flight (support_dispatch.cpp); its spec here (SupportAircraftSpec), its second wave (a sortie's `then`:
//    SupportFollowUpQueue).
//  - A strike (kStrikes, SupportStrikeAt): a long-range barrage (barrage.cpp: this machine plans and fires it, and tells the
//    others, barrage_net_protocol.h) or the ultimate (ultimate.cpp, ultimate_net_protocol.h), called by the local player
//    from the bar onto the point clicked, heading from the player to it.
//  - The rows still installed (kCallRows: the vehicle requests, the thrown drones, the guns) are owned from the start
//    (docs/loadout-re.md section 8): before the game's own "grant the installed DLC weapons" step (0xDC550,
//    UnlockDownloadContents: after every save load, and in a new game's reset; its two entries, a call at 0xDC348 and the
//    script thunk's jump at 0x70FF87, are redirected) every weapon table row of kCallRows gets the owned bit, as that step
//    gives a DLC weapon its: NEW and 0 stars the first time, stars left alone after. Before it, so its "equipped but not
//    owned: back to the default weapon" pass keeps them on the soldiers that carry them.
//    The script's entry alone also marks / moves the mission pack's save (edf5campaign.cpp MigrateMissionPackSave):
//    MAINSCRIPT calls UnlockDownloadContents only after SetMode and a LoadSaveData_ModeSelect that succeeded
//    (MAINSCRIPT.AS 363, 852, 1691, 1801, 1851, 1889, 1913), so GS+0x57A4 is the new mode's save (read whole by
//    0x7BE970, or the reset template when it has no file yet) and GS+0x38 that mode. The call at 0xDC348 is the
//    reset's (0xDB580 bit 8, via 0xDB920 -> 0xDC170), which runs after bit 4 has put the template in GS+0x57A4 and,
//    inside SetMode (0xDC4B8), before 0xDC4C5 sets GS+0x38: the old mode then, a save that is not its.
//  - The thrown drones (tools/call_weapons.py: EDF6VC_CALL_THROW_*, Patroller clones in the Robot Bomb list, told
//    apart by their AmmoHitSizeAdjust's bits, kThrows): the Weapon_Sub's shot (its vtable slot 17, called with the
//    bullet it just made, 0x697DDD) of one of ours, a BombBullet01, is kept (bombs[]); at that bomb's update
//    (BombBullet01 slot 5), once it has landed (its landed byte, +0xD40, which its own update tests at 0x29637B) or
//    kThrowFuseMs after the throw, and if its owner is the local player, the drone is launched where the bomb is
//    (JetLaunchThrown) and the bomb deleted (0x118A1B0, as its own update deletes a spent bomb at 0x2962D5) before
//    its update runs, so the stock patroller never starts. A drone that cannot be launched leaves the stock bomb as
//    it is. Only the thrower's game: in an online game the others see the stock patroller (their copy's owner is no
//    local player). Kills are the drone's (an NPC friend's), as a flown support's jets' are.
// Without the plugin the thrown drones are plain Patrollers, the vehicle requests the stock requests of their vehicles.
#include "crew.h"
#include "hudtext.h"
#include "support_call.h"
#include "support_aircraft.h"
#include "support_entry.h"
#include "jet_internal.h"   // FaultLog
#include "memory.h"
#include "online_authority.h"
#include "unlock_ported.h"
#include "weapon_records.h"
#include <cmath>
#include <cstdarg>
#include <cwchar>
#include <optional>
#include <vector>

namespace crew {
namespace {
constexpr unsigned kDelete=0x118A1B0;
constexpr float kAboveTarget=150.0f;
// Ownership (docs/loadout-re.md 8): the game status, its weapon table (cfg = GS+0x130, the table loaded
// once cfg+0x188 is set) and per row a record of 12 bytes, u32 flags (bit0 owned, bit2 NEW) and 8 star
// bytes: 0x800 of them at GS+0xEB50 natively, wrec::kSlots relocated (weapon_records.h WeaponRecord).
constexpr unsigned kClampStars=0xD7930;   // (GS*): every star byte clamped to its weapon's cap (RowCount loop)
constexpr unsigned kUnlockDlc=0xDC550,kUnlockDlcCall=0xDC348,kUnlockDlcJump=0x70FF87;
constexpr unsigned kRowCount=0xE23F0,kGetRow=0xE1CF0;
constexpr std::size_t kCfg=0x130,kTableRef=0x188,kRecord=12;
constexpr unsigned char kUnlockDlcSig[]={0x48,0x89,0x5C,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7C,0x24,0x20,
                                         0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57};
constexpr unsigned char kUnlockThunkSig[]={0x48,0x8B,0x0D};   // 0x70FF80: mov rcx,[GS]; jmp 0xDC550
// A weapon's AmmoHitSizeAdjust (a thrown drone's marker).
constexpr std::size_t kWeaponHitSize=0x8C4;
using DeleteFn=void(*)(void*);

// The supports, made from one table (tools/calls.py): tools/gen_calls.py writes calls.inc below (CI checks it is
// current). Per role a guard support (round the point clicked) and a follow one (round the player), the follow one dearer
// (its pt). The stronger the support, the fewer and the dearer; the carrier is one, its drones do the work. What a support
// lasts is its ammo (jets and helis are not refilled, a carrier has kCarrierSorties launches): out of it, out of fuel
// (fuelSec) or badly damaged each leaves.
// What a support brings: jets (`role`), helis (`body`; both made by the support deployment) or the submarine carrier
// (a planned hull plus its real pilot); the field of what it does not bring is empty.
enum class Brings { jets, helis, sub };
// A sortie (docs/plan-2026-10-25-strikes.md §1.2): `sortie` its pass (crew.h SortiePass, 0 none), `then` the kCalls index of
// its second wave (-1 none), called onto the same mark `thenMs` after this one was accepted (SupportFollowUpQueue). `pt`
// its price on the map's bar (tools/calls.py Call.pt).
struct Call { float mark; Brings brings; std::optional<JetRole> role; std::optional<HeliBody> body; int count; DWORD fuelSec;
              bool follow; const char* name; const wchar_t* id; int sortie; int then; DWORD thenMs; std::uint32_t pt; };
// A weapon table row tools/call_weapons.py installs (a vehicle request, a thrown drone, a gun): its id, its SGO in
// Mods/WEAPON and the object SGO a vehicle request brings in Mods/OBJECT (nullptr: none).
struct CallRow { const wchar_t* id; const wchar_t* weaponFile; const wchar_t* objectFile; };
// A thrown drone's weapon (tools/calls.py brings 'throw'): its AmmoHitSizeAdjust's bits (calls.throw_mark), the
// drone its bomb releases and that drone's fuel.
struct Throw { std::uint32_t markBits; ThrownDrone drone; DWORD fuelSec; const char* name; const wchar_t* id; };
// A strike of the map's bar (tools/calls.py STRIKES): a barrage (`barrage` its barrage_plan.h Kind, `damage` each round's)
// or the ultimate (`damage` the show's, barrage -1); its pt; its key (SupportDisabled) and its name on the bar.
enum class StrikeKind { barrage, ultimate };
struct Strike { StrikeKind kind; int barrage; float damage; std::uint32_t pt; const char* name; const wchar_t* key;
                const wchar_t* label; };
#include "calls.inc"


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

// The caller's HUD line for its strike calls (PlayerStrikeCue; hud.cpp StrikeBanner reads it on the draw thread).
SRWLOCK strikeCueLock=SRWLOCK_INIT;
wchar_t strikeCueText[200]{};
ULONGLONG strikeCueAt=0;
void StrikeCue(const wchar_t* format,...) noexcept {
    wchar_t text[200];
    va_list a;va_start(a,format);_vsnwprintf_s(text,_countof(text),_TRUNCATE,format,a);va_end(a);
    AcquireSRWLockExclusive(&strikeCueLock);
    std::memcpy(strikeCueText,text,sizeof(text));strikeCueAt=GetTickCount64();
    ReleaseSRWLockExclusive(&strikeCueLock);
}
const wchar_t* BarrageWhy(BarrageResult r) noexcept {
    using hudtext::Tx;
    switch(r) {
    case BarrageResult::noHook: return hudtext::Tr(Tx::barrageNoHook);
    case BarrageResult::full: return hudtext::Tr(Tx::barrageFull);
    case BarrageResult::noRounds: return hudtext::Tr(Tx::barrageNoRounds);
    default: return hudtext::Tr(Tx::barrageNoTarget);
    }
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

// Whether a weapon table row's name is one of ours (kCallRows): exactly, so a retired row (EDF6VC_RETIRED_*,
// an uninstall's placeholder) or another mod's row is never made owned.
bool IsCallRow(const wchar_t* name) noexcept {
    for(const auto& r:kCallRows) {
        const std::size_t len=std::wcslen(r.id)+1;
        if(Readable(name,len*sizeof(wchar_t)) && std::wcsncmp(name,r.id,len)==0)return true;
    }
    return false;
}
// Whether it is the placeholder of a support moved to the map (kMovedRows), exactly.
bool IsMovedRow(const wchar_t* name) noexcept {
    for(const wchar_t* id:kMovedRows) {
        const std::size_t len=std::wcslen(id)+1;
        if(Readable(name,len*sizeof(wchar_t)) && std::wcsncmp(name,id,len)==0)return true;
    }
    return false;
}

// Every kCallRows row owned (see the file comment), and with PortedWeaponsUnlocked every ported weapon's row
// (unlock_ported.h PortedWeapon: EDF6VC_E5_*, EDF6VC_E41_*) as well; how many of each were not yet (-1: no table).
// Only ever sets bits there: switching the key off takes nothing back (a weapon a save has equipped must stay owned, and
// the weapons are in MAIN.GST, which every mode shares); EDF6's own rows are never touched. The one bit cleared: the owned
// one of a support moved to the map (kMovedRows; `moved` counts them), whose weapon no longer exists.
struct Granted { int calls=-1,ported=0,moved=0; };
Granted GrantCalls(unsigned char* gs,bool ported) noexcept {
    Granted out;
    const std::uint32_t most=WeaponRecordCapacity();
    unsigned char* const first=WeaponRecord(gs,0);
    if(!first || !Readable(first,std::size_t{most}*kRecord,true) || !Readable(gs+kCfg+kTableRef,8))return out;
    void* const table=gs+kCfg;
    if(!At<void*>(table,kTableRef))return out;             // no weapon table yet
    std::uint32_t n=reinterpret_cast<RowCountFn>(image+kRowCount)(table);
    WeaponRecordsCheckRows(n);
    if(n>most)n=most;
    alignas(8) unsigned char row[0x100];
    out.calls=0;
    for(std::uint32_t id=0;id<n;++id) {
        reinterpret_cast<GetRowFn>(image+kGetRow)(table,row,id);
        const auto name=At<const wchar_t*>(row,0);
        if(!name)continue;
        if(IsCallRow(name)) {
            if(ported::Own(WeaponRecord(gs,id)))++out.calls;
        } else if(IsMovedRow(name)) {
            if(ported::Disown(WeaponRecord(gs,id)))++out.moved;
        } else if(ported && Readable(name,ported::kLongestPrefix*sizeof(wchar_t)) && ported::PortedWeapon(name)) {
            if(ported::Own(WeaponRecord(gs,id)))++out.ported;
        }
    }
    return out;
}

// AllWeaponsUnlocked: every row of the table (the relocated records past 0x800 too) owned at max stars, then the game's
// own clamp to each weapon's cap (0xD7930, its loop runs to RowCount). A row whose cap is under 10 ends at its cap; a
// star byte of a weapon that has fewer parameters than the record's 8 stays 10 (as the stock unlock_weapon command
// leaves it). Returns the number of records that differ from before (-1: no table; -2: more rows than records, the
// native clamp would run past them: nothing written).
int GrantAll(unsigned char* gs) noexcept {
    const std::uint32_t most=WeaponRecordCapacity();
    unsigned char* const first=WeaponRecord(gs,0);
    if(!first || !Readable(first,std::size_t{most}*kRecord,true) || !Readable(gs+kCfg+kTableRef,8))return -1;
    void* const table=gs+kCfg;
    if(!At<void*>(table,kTableRef))return -1;
    const std::uint32_t n=reinterpret_cast<RowCountFn>(image+kRowCount)(table);
    WeaponRecordsCheckRows(n);
    if(n>most)return -2;
    std::vector<unsigned char> before(std::size_t{n}*kRecord);
    for(std::uint32_t id=0;id<n;++id)std::memcpy(&before[std::size_t{id}*kRecord],WeaponRecord(gs,id),kRecord);
    alignas(8) unsigned char row[0x100];
    for(std::uint32_t id=0;id<n;++id) {
        reinterpret_cast<GetRowFn>(image+kGetRow)(table,row,id);
        const auto name=At<const wchar_t*>(row,0);
        if(!name || !Readable(name,sizeof(wchar_t)*16) || ported::Placeholder(name))continue;
        ported::OwnMax(WeaponRecord(gs,id));
    }
    reinterpret_cast<UnlockFn>(image+kClampStars)(gs);
    int changed=0;
    for(std::uint32_t id=0;id<n;++id)
        if(std::memcmp(&before[std::size_t{id}*kRecord],WeaponRecord(gs,id),kRecord)!=0)++changed;
    return changed;
}

void Grant(unsigned char* gs) noexcept {
    __try {
        const bool ported=Cfg().portedWeaponsUnlocked;
        const Granted granted=GrantCalls(gs,ported);
        if(granted.calls>0)Log("CALLS %d call weapons owned now",granted.calls);
        if(granted.moved>0)Log("CALLS %d support call weapons are on the map's bar now: no longer owned (one equipped goes back "
                               "to the default weapon)",granted.moved);
        if(Cfg().allWeaponsUnlocked) {
            const int all=GrantAll(gs);
            Log("ALL weapons unlocked: %d records changed%s",all,all==-1 ? " (no weapon table yet)" : all==-2 ? " (more weapon rows than records: nothing written)" : "");
        }
        if(ported)Log("PORTED weapons unlocked: %d owned now%s",granted.ported,granted.calls<0 ? " (no weapon table yet)" : "");
    } __except(EXCEPTION_EXECUTE_HANDLER){Log("CALLS granting failed");}
}
// 0xDC348: the reset's (see the file comment): the call weapons only.
void __fastcall UnlockDlcHook(unsigned char* gs) {
    Grant(gs);
    reinterpret_cast<UnlockFn>(image+kUnlockDlc)(gs);
}
// 0x70FF87: the script's UnlockDownloadContents, right after a mode's save was loaded: that save first.
void __fastcall ScriptUnlockDlcHook(unsigned char* gs) {
    __try { MigrateMissionPackSave(gs); } __except(EXCEPTION_EXECUTE_HANDLER){Log("PACK save: moving the rows failed");}
    Grant(gs);
    reinterpret_cast<UnlockFn>(image+kUnlockDlc)(gs);
}

// Both entries of 0xDC550 redirected (0xDC348 to UnlockDlcHook, 0x70FF87 to ScriptUnlockDlcHook); whether both are.
// `script`: whether the script's is (the mission packs' save hook).
bool InstallOwnership(bool& script) noexcept {
    script=false;
    if(!Matches(kUnlockDlc,kUnlockDlcSig,sizeof(kUnlockDlcSig)) || !Matches(kUnlockDlcJump-7,kUnlockThunkSig,sizeof(kUnlockThunkSig))) {
        Log("CALLS ownership: profile mismatch");
        return false;
    }
    bool ok=true;
    const unsigned sites[]={kUnlockDlcCall,kUnlockDlcJump};
    void* const hooks[]={reinterpret_cast<void*>(&UnlockDlcHook),reinterpret_cast<void*>(&ScriptUnlockDlcHook)};
    for(unsigned i=0;i<2;++i) {
        bool changed=false;
        if(RedirectCall(image+sites[i],image+kUnlockDlc,hooks[i],changed)){script=script || sites[i]==kUnlockDlcJump;continue;}
        ok=false;
        Log("CALLS ownership: %s at %X",changed ? "half patched" : "not patched",sites[i]);
    }
    return ok;
}

}  // namespace

bool SupportSubCatalog(int catalog) noexcept {
    return catalog>=0 && catalog<kCallCount && kCalls[catalog].brings==Brings::sub;
}
bool SupportAircraftSpec(int catalog,SupportAircraft* out) noexcept {
    if(!out || catalog<0 || catalog>=kCallCount || kCalls[catalog].brings==Brings::sub)return false;
    const auto& c=kCalls[catalog];
    *out={c.role ? static_cast<int>(*c.role) : -1,c.body ? static_cast<int>(*c.body) : -1,
          c.count,c.fuelSec,c.follow};
    out->sortie=c.sortie;
    return true;
}

// A sortie's second wave (Call::then): queued when this machine's request for the first was accepted (support_dispatch.cpp
// SupportCallComposedAt), called onto the same mark by this machine, the caller's, once due (SupportFollowUpTick: SharedFrameTick).
// Due while the call before it is still being planned offline (SupportCallReadiness planning: the dispatcher takes one
// request at a time, often the first wave itself), it waits for that plan and is called the frame it is over. Any other
// refusal (the entry disabled in the ini, the mission's support rules, the plugin off, ...) would refuse it again: it is
// given up at once and the caller told why on the HUD. Online `accepted` is this machine's request queued for the host: a
// refusal there is the host's to say (support_net).
namespace {
struct FollowUp { int catalog=-1; float at[3]{}; ULONGLONG due=0; bool waited=false; };
FollowUp followUps[8]{};
}  // namespace
void SupportFollowUpQueue(int catalog,const float* target) noexcept {
    if(catalog<0 || catalog>=kCallCount || kCalls[catalog].then<0 || !target)return;
    for(auto& f:followUps)if(f.catalog<0) {
        f.catalog=kCalls[catalog].then;std::memcpy(f.at,target,12);f.due=GameMs()+kCalls[catalog].thenMs;
        Log("SUPPORT %s: its second wave (%s) in %lu s",kCalls[catalog].name,kCalls[f.catalog].name,kCalls[catalog].thenMs/1000);
        return;
    }
    Log("SUPPORT %s: no room for its second wave (8 waiting)",kCalls[catalog].name);
}
void SupportFollowUpTick() noexcept {
    const ULONGLONG ms=GameMs();
    for(auto& f:followUps) {
        if(f.catalog<0 || ms<f.due)continue;
        if(SupportCallReadiness().state==SupportReady::planning) {   // the call before it still planned: its turn comes
            if(!f.waited)Log("SUPPORT second wave %s: waiting for the call being planned",kCalls[f.catalog].name);
            f.waited=true;
            continue;
        }
        const FollowUp now=f;
        f=FollowUp{};
        wchar_t note[128]{};
        const bool ok=SupportCallUnpaidAt(now.catalog,now.at,note,_countof(note));   // paid with the first; no wave of its own
        Log("SUPPORT second wave %s: accepted=%d",kCalls[now.catalog].name,ok);
        if(!ok)StrikeCue(hudtext::Tr(hudtext::Tx::followUpRefused),note[0] ? note : L"-");
    }
}
void ResetSupportFollowUps() noexcept { for(auto& f:followUps)f=FollowUp{}; }
bool PlayerStrikeCue(wchar_t* out,std::size_t capacity) noexcept {
    constexpr ULONGLONG kShowMs=8000;
    AcquireSRWLockShared(&strikeCueLock);
    const bool fresh=strikeCueAt && GetTickCount64()-strikeCueAt<=kShowMs && out && capacity;
    if(fresh)_snwprintf_s(out,capacity,_TRUNCATE,L"%ls",strikeCueText);
    ReleaseSRWLockShared(&strikeCueLock);
    return fresh;
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
std::uint32_t SupportAirCallPt(int index) noexcept { return index>=0 && index<kCallCount ? kCalls[index].pt : 0u; }

// ---- The strikes (kStrikes): the map's bar calls them for the local player ----
int SupportStrikeCount() noexcept { return kStrikeCount; }
const wchar_t* SupportStrikeKey(int strike) noexcept { return strike>=0 && strike<kStrikeCount ? kStrikes[strike].key : nullptr; }
const wchar_t* SupportStrikeName(int strike) noexcept { return strike>=0 && strike<kStrikeCount ? kStrikes[strike].label : L"支援"; }
std::uint32_t SupportStrikePt(int strike) noexcept { return strike>=0 && strike<kStrikeCount ? kStrikes[strike].pt : 0u; }
int SupportStrikeBarrage(int strike) noexcept {
    return strike>=0 && strike<kStrikeCount && kStrikes[strike].kind==StrikeKind::barrage ? kStrikes[strike].barrage : -1;
}
// The heading a strike at `target` comes along: from the player toward it (a barrage's launchers stand behind it, its
// creeping walk goes along it), +z when the player is on the point or not known.
void SupportStrikeHeading(const float* target,float* out) noexcept {
    out[0]=0.0f;out[1]=0.0f;out[2]=1.0f;
    const unsigned char* const human=PlayerHuman();
    if(!human || !target || !Readable(human+0x90,12))return;
    const float* const by=reinterpret_cast<const float*>(human+0x90);   // its position (the matrix's last row)
    const float dx=target[0]-by[0],dz=target[2]-by[2],l=std::sqrt(dx*dx+dz*dz);
    if(l>1.0f){out[0]=dx/l;out[2]=dz/l;}
}
bool SupportStrikeAt(int strike,const float* target,wchar_t* note,std::size_t capacity) noexcept {
    if(!note || !capacity)return false;
    note[0]=0;
    if(strike<0 || strike>=kStrikeCount || !target || !std::isfinite(target[0]+target[1]+target[2])) {
        _snwprintf_s(note,capacity,_TRUNCATE,L"%ls",L"支援请求或目标无效");return false;
    }
    const Strike& k=kStrikes[strike];
    unsigned char* const caller=PlayerHuman();
    if(!caller){_snwprintf_s(note,capacity,_TRUNCATE,L"%ls",hudtext::Tr(hudtext::Tx::strikeNoCaller));return false;}
    float heading[3];SupportStrikeHeading(target,heading);
    bool started=false;
    __try {
        if(k.kind==StrikeKind::barrage) {
            const BarrageResult got=BarrageStart(k.barrage,caller,target,heading,k.damage);
            started=got==BarrageResult::started;
            if(!started)_snwprintf_s(note,capacity,_TRUNCATE,hudtext::Tr(hudtext::Tx::barrageRefused),BarrageWhy(got));
        } else {
            const std::uint64_t seed=GetTickCount64()*0x9E3779B97F4A7C15ull^reinterpret_cast<std::uintptr_t>(caller);
            started=UltimateStart(caller,target,k.damage,seed);
            if(!started)_snwprintf_s(note,capacity,_TRUNCATE,L"%ls",hudtext::Tr(hudtext::Tx::ultimateRefused));
        }
    } __except(FaultLog("AIRSTRIKE strike from the map",GetExceptionInformation())){started=false;}
    if(started)_snwprintf_s(note,capacity,_TRUNCATE,hudtext::Tr(hudtext::Tx::strikeStarted),k.label);
    Log("AIRSTRIKE strike %s from the map at (%.0f,%.0f,%.0f): %s",k.name,target[0],target[1],target[2],started ? "started" : "not started");
    return started;
}
namespace {
support::Refusal AirRouteFor(const SupportAircraft& spec,const float* target,const float* observer,support::Route* route,
                             bool logged) noexcept {
    // A spec that brings an aircraft: a jet row, a heli row, or the paratroop plane (no row of either).
    if(!route || !target || (spec.jet<0 && spec.heli<0 && !spec.transportPlane) || spec.count<1)return support::Refusal::unsupported;
    if(!OpenSky(target))return support::Refusal::noSky;
    float direction[3]={0,0,1};
    // Host chooses the entire plan once. Peers receive its explicit matrices.
    if(observer) {
        const float x=target[0]-observer[0],z=target[2]-observer[2],d=std::hypot(x,z);
        if(d>0.1f){direction[0]=x/d;direction[2]=z/d;}
    }
    // Air support is created in the air at the route's entry and flies in (2026-10-09, the user: it comes from off
    // the field; no takeoff). The route (support::AirRoute) stands its entry off the map: past the ground's edge, inside
    // the physical square (ArrivalHalf), past the caller's draw distance where that square holds such a place (2026-10-10:
    // made 180 m inside the ground and 900 m from the caller, the aircraft appeared in plain sight), over the highest
    // ground of the whole line plus the altitude; here every aircraft's formation slot (support::AirFormationSlot) must lie
    // inside that square, over its own ground (where it has any) by half the altitude, and have the full-size corridor
    // (EntryClear) clear to its own end over the target. The paratroop plane's line must also leave the map past the
    // target with room to turn round (its passes: SupportPassTurn, support::MakePassLine).
    const PlayArea area=MapPlayArea();
    const support::Reach reach{ArrivalHalf(),NearDrawDistance()};
    const float altitude=spec.heli>=0 ? std::fmax(Cfg().heliHeight,60.0f) : kAboveTarget;
    const float spacing=spec.heli>=0 ? 0.6f : 1.0f;
    const auto entry=[&](const float* from,const float* to) noexcept {
        const float dx=to[0]-from[0],dz=to[2]-from[2],d=std::hypot(dx,dz);
        if(d<1)return false;
        support::Route lead{{from[0],from[1],from[2]},{dx/d,0.0f,dz/d}};
        for(int i=0;i<spec.count;++i) {
            float at[3];support::AirFormationSlot(lead,i,spacing,at);
            if(std::fabs(at[0])>reach.half || std::fabs(at[2])>reach.half)return false;
            float ground;
            if(EntryHeight(at[0],at[2],target[1],ground) && ground+altitude*0.5f>at[1])return false;   // over its own ground too
            const float end[3]={at[0]+dx,at[1],at[2]+dz};
            if(!EntryClear(at,end))return false;
        }
        return true;
    };
    const auto refusal=support::AirRoute(area,reach,target,observer,direction,altitude,SupportPassTurn(spec),entry,EntryHeight,*route);
    if(logged && refusal==support::Refusal::none)
        Log("SUPPORT air entry (%.0f,%.0f,%.0f): %.0f m from the target, %.0f m from the caller (draws to %.0f m: %s), the physical "
            "square +-%.0f",route->from[0],route->from[1],route->from[2],support::FlatDistance(route->from,target),
            observer ? support::FlatDistance(route->from,observer) : -1.0f,reach.draw,route->beyond ? "past it" :
            "none past it in the square, the farthest taken",reach.half);
    return refusal;
}
}  // namespace
support::Refusal PlanAirSupport(const SupportAircraft& spec,const float* target,const float* observer,support::Route* route) noexcept {
    return AirRouteFor(spec,target,observer,route,true);
}
support::Refusal PreviewAirRoute(const SupportAircraft& spec,const float* target,const float* observer,support::Route* route) noexcept {
    return AirRouteFor(spec,target,observer,route,false);
}

support::Refusal PlanTakeoffSupport(const SupportAircraft& spec,const float* target,const float (*spots)[3],int count,
                                    support::Route* route) noexcept {
    if(!route || !target || spec.count!=1 || (spec.jet<0 && spec.heli<0 && !spec.transportPlane))return support::Refusal::unsupported;
    if(!OpenSky(target))return support::Refusal::noSky;
    return support::TakeoffRoute(MapPlayArea(),target,spots,count,EntryClear,*route);
}

bool InstallAirstrikes() noexcept {
    CheckCallTable();
    __try {
        bool script=false;
        const bool owned=InstallOwnership(script);
        SetMissionPackSaveHook(script);   // a mission pack is owned only while its saves are marked (edf5campaign.cpp Ours)
        // The stock radio calls (bombers, Vesta, Phobos, the satellites) are left wholly native: no support of the plugin's
        // is a weapon any more (they are the map's bar's).
        const bool throws=InstallThrows();
        Log("HOOK airstrikes owned=%d packSaves=%d stockCalls=native throws=%d",owned,script,throws);
        return owned || throws;
    } __except(FaultLog("AIRSTRIKE install",GetExceptionInformation())){return false;}
}
// A new mission (mission.cpp MissionStart): the thrown bombs were the last mission's (their
// addresses may be the new mission's objects'): forgotten, nothing of them touched.
void ResetAirstrikes() noexcept {
    for(auto& b:bombs)b=Bomb{};
    bombCount=0;
}
}  // namespace crew
