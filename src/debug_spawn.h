// The debug spawn tool's decisions (debug_spawn.cpp), apart from the game so tests/debug_spawn_test.cpp checks them off it:
// what can be spawned (the catalog), the menu (its keys, its selection) and where a spawn lands.
//
// The user, 2026-10-09: 「给我加一个调试工具，在地图里面凭空召唤载具、敌人之类的，默认关闭这个工具」. Off by default
// (ini DebugSpawn=0): with it off nothing of it runs (no preload, no key read, nothing drawn). It installs no hook of its
// own either way: its preload rides the mission's start (mission.cpp), its frame the player's frame (map.cpp / crew.cpp),
// its menu the plugin's HUD pass (hud.cpp).
//
// Every spawn goes through a path the plugin already uses (docs/debug-spawn.md):
//  - a stock ground vehicle: CreateObject + its mission_setup (support_spawn.cpp ApplyMissionSetup), empty, friend team;
//  - the plugin's crewed aircraft (jets, helis): a support call (support_dispatch.cpp SupportCallAt, the map's air support):
//    planned from the map's edge to the crosshair's point, made in the air with its real crew seated aboard. Never
//    JetLaunch / HeliLaunch: since the real crews (#88) an aircraft made there has no pilot and falls (2026-10-09 log);
//  - the gun drone: JetLaunchDrone (jet_spawn.cpp; a drone flies with no one aboard, #103);
//  - a stock enemy: the steps the script's CreateEnemy (0x1AD220 -> 0x1D8900) takes: CreateObject, SetTeam(enemy),
//    SetLevel, the active flag's 0x548D50;
//  - a friendly soldier: the support soldiers' stock Ranger (support_soldier.cpp), let go of at once.
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include "debug_spawn_files.h"

namespace crew {
namespace debugspawn {
// vehicle: EDF6's stock vehicles and EDF4.1's; ours: the range's and the installer's vehicles (Mods files); aircraft: the
// plugin's crewed aircraft (support calls) and the drone; enemy: EDF6's stock enemies and EDF5's DLC fortress; enemy41:
// EDF4.1's ported enemies; soldier: the support soldiers.
enum class Category : std::uint8_t { vehicle, ours, aircraft, enemy, enemy41, soldier, count };
constexpr int kCategoryCount=static_cast<int>(Category::count);
// The categories' names are the HUD's words (src/hudtext.inc debugSpawnVehicles ...), in this order.

// How a row is made (debug_spawn.cpp MakeRow). `arg`: supportCall its kSupportCalls index, soldier the SupportWeapon
// (support_call.h); the others none. vehicle: CreateObject on its SGO + its mission_setup, empty, friend team (a stock
// SGO or one in Mods); enemy: the script's CreateEnemy steps.
enum class How : std::uint8_t { vehicle, supportCall, drone, enemy, soldier };
// The support calls (support_dispatch.cpp SupportCallKey: the map's air support catalog) the crewed aircraft rows ask for.
// The follow variants (they escort the player) where there is one; the Brute is the guard call (it guards the point).
inline constexpr const wchar_t* kSupportCalls[]={L"STRIKE_F",L"FIGHTER_F",L"INTERCEPTOR_F",L"MULTIROLE_F",L"CARRIER_F",
    L"BLAST_CARRIER_F",L"DOLL_CARRIER_F",L"GUNSHIP_F",L"HELI_F",L"HELI",L"MEDIC_HELI_F"};
constexpr int kSupportCallCount=static_cast<int>(sizeof(kSupportCalls)/sizeof(kSupportCalls[0]));
struct Entry {
    Category category;
    How how;
    const char* id;          // the log's (ASCII: the log is narrow text)
    const wchar_t* name;     // the menu's
    const wchar_t* sgo;      // vehicle / enemy: the SGO this tool preloads and creates (nullptr otherwise)
    int arg;
    float lift;              // m over the ground point it starts at (a flier higher up)
    bool mod=false;          // its SGO is a Mods file (the range's, the installer's, EDF4.1's): there only when that is
    bool preload=true;       // preloaded by default (else only with ini RangePlaceAll=1: a heavy model of its own)
    // A Mods row's files (testrange/catalog.py files_of, what catalog.available checks): bare OBJECT names or WEAPON/name, each
    // NUL-ended, the list ended by an empty name; its SGO among them. Made only when every one is there (debug_spawn.cpp ModRowLacks).
    // nullptr: none (a stock row, Root.cpk's; the plugin's own rows).
    const wchar_t* files=nullptr;
};

// The rows. The object rows (vehicles, enemies) are src/spawn_catalog.inc, written by tools/gen_spawn_catalog.py from
// testrange/catalog.py (the test range's own lists come from there too): EDF6's stock ones are Root.cpk's
// (tests/debug_spawn_resource_audit.py reads each: a vehicle's must carry mission_setup, every one must exist), the others
// Mods files the range's install or the installer writes (`mod`: made only when the file is there). The plugin's crewed
// aircraft are the map's air support calls; the drone is the carriers' (its SGO the installer's, preloaded by PreloadJets).
inline constexpr Entry kEntries[]={
#include "spawn_catalog.inc"
    // supportCall: kSupportCalls' index. A support call: as many as the call brings (support config), from the map's edge.
    {Category::aircraft,How::supportCall,"call_strike_f",L"对地攻击机（支援呼叫）",nullptr,0,0.0f},
    {Category::aircraft,How::supportCall,"call_fighter_f",L"制空战斗机（支援呼叫）",nullptr,1,0.0f},
    {Category::aircraft,How::supportCall,"call_interceptor_f",L"截击机（支援呼叫）",nullptr,2,0.0f},
    {Category::aircraft,How::supportCall,"call_multirole_f",L"多用途战斗机（支援呼叫）",nullptr,3,0.0f},
    {Category::aircraft,How::supportCall,"call_carrier_f",L"空中航母（支援呼叫）",nullptr,4,0.0f},
    {Category::aircraft,How::supportCall,"call_blast_carrier_f",L"自爆无人机母舰（支援呼叫）",nullptr,5,0.0f},
    {Category::aircraft,How::supportCall,"call_doll_carrier_f",L"人偶无人机母舰（支援呼叫）",nullptr,6,0.0f},
    {Category::aircraft,How::supportCall,"call_gunship_f",L"炮舰机（支援呼叫）",nullptr,7,0.0f},
    {Category::aircraft,How::supportCall,"call_heli_f",L"直升机 Eros 506（支援呼叫·跟随）",nullptr,8,0.0f},
    {Category::aircraft,How::supportCall,"call_heli",L"直升机 Brute 410（支援呼叫·守点）",nullptr,9,0.0f},
    {Category::aircraft,How::supportCall,"call_medic_heli_f",L"医疗直升机（支援呼叫）",nullptr,10,0.0f},
    {Category::aircraft,How::drone,"jet_drone",L"机炮无人机（无人驾驶）",nullptr,0,60.0f},
    // soldier: SupportWeapon's order (support_call.h): every kind the support soldiers make (support_soldier.cpp: each
    // class's body is preloaded every mission), the Wing Divers and Fencers too (the user, 2026-10-19: 「载具与npc少了」).
    {Category::soldier,How::soldier,"ranger_rifle",L"游骑兵（步枪）",nullptr,0,0.2f},
    {Category::soldier,How::soldier,"ranger_flame",L"游骑兵（火焰）",nullptr,1,0.2f},
    {Category::soldier,How::soldier,"ranger_rocket",L"游骑兵（火箭）",nullptr,2,0.2f},
    {Category::soldier,How::soldier,"ranger_shotgun",L"游骑兵（霰弹）",nullptr,3,0.2f},
    {Category::soldier,How::soldier,"ranger_sniper",L"游骑兵（狙击）",nullptr,4,0.2f},
    {Category::soldier,How::soldier,"wing_lance",L"飞翼潜行兵（长矛）",nullptr,5,0.2f},
    {Category::soldier,How::soldier,"wing_laser",L"飞翼潜行兵（激光）",nullptr,6,0.2f},
    {Category::soldier,How::soldier,"wing_monster",L"飞翼潜行兵（狙击）",nullptr,7,0.2f},
    {Category::soldier,How::soldier,"wing_izuna",L"飞翼潜行兵（雷链）",nullptr,8,0.2f},
    {Category::soldier,How::soldier,"wing_thunderbow",L"飞翼潜行兵（雷弓）",nullptr,9,0.2f},
    {Category::soldier,How::soldier,"fencer_cannon",L"重装兵（重炮）",nullptr,10,0.2f},
    {Category::soldier,How::soldier,"fencer_middle_cannon",L"重装兵（中炮）",nullptr,11,0.2f},
    {Category::soldier,How::soldier,"fencer_pile_banker",L"重装兵（打桩）",nullptr,12,0.2f},
    {Category::soldier,How::soldier,"fencer_shotgun",L"重装兵（霰弹）",nullptr,13,0.2f},
};
constexpr int kEntryCount=static_cast<int>(sizeof(kEntries)/sizeof(kEntries[0]));
// Every Mods row lists its files and nothing else does; a list is not empty.
constexpr bool FilesAgree() noexcept {
    for(const auto& e:kEntries) {
        if(e.mod!=(e.files!=nullptr) || (e.files && !e.files[0]))return false;
        if(e.files)for(const wchar_t* f=e.files;*f;) {
            if(!ParseModFile(f))return false;
            while(*f)++f;
            ++f;
        }
    }
    return true;
}
static_assert(FilesAgree(),"a Mods row (mod) and only it names its Mods files");

constexpr int CountIn(Category c) noexcept {
    int n=0;
    for(const auto& e:kEntries)n+=e.category==c;
    return n;
}
// The `index`th row of category `c` (its place in kEntries), -1 past them.
constexpr int RowOf(Category c,int index) noexcept {
    for(int i=0;i<kEntryCount;++i)
        if(kEntries[i].category==c && index--==0)return i;
    return -1;
}

// The menu: shut or open, the category shown and the row picked in each category (kept while another is looked at).
struct Menu {
    bool open=false;
    int category=0;
    int pick[kCategoryCount]{};
};
// This frame's key presses (each down now, up last frame: Press).
struct Keys { bool toggle,prev,next,category,spawn; };
enum class Act : std::uint8_t { none, opened, closed, moved, spawn };

// A key's press this frame: down now, up last frame. `held` is the key's own state, kept every frame.
inline bool Press(bool& held,bool down) noexcept {
    const bool press=down && !held;
    held=down;
    return press;
}

// One frame of the menu. Shut, only the toggle does anything (the other keys are the game's then); open, the toggle shuts
// it, prev / next go round the category's rows, the category key to the next category (each keeps its pick), spawn asks
// for the row picked. One action a frame: the toggle first, then spawn, then the moves.
inline Act Step(Menu& m,const Keys& k) noexcept {
    if(k.toggle){m.open=!m.open;return m.open ? Act::opened : Act::closed;}
    if(!m.open)return Act::none;
    if(m.category<0 || m.category>=kCategoryCount)m.category=0;
    if(k.spawn)return Act::spawn;
    if(k.category){m.category=(m.category+1)%kCategoryCount;return Act::moved;}
    const int n=CountIn(static_cast<Category>(m.category));
    int& p=m.pick[m.category];
    if(n<=0){p=0;return Act::none;}
    if(p<0 || p>=n)p=0;
    if(k.next){p=(p+1)%n;return Act::moved;}
    if(k.prev){p=(p+n-1)%n;return Act::moved;}
    return Act::none;
}
// The row the menu points at (kEntries index), -1 none.
inline int Picked(const Menu& m) noexcept {
    if(m.category<0 || m.category>=kCategoryCount)return -1;
    return RowOf(static_cast<Category>(m.category),m.pick[m.category]);
}

// Where a spawn lands, before the ground under it is found. `me` the player's position, `dir` the camera's look (unit),
// `hit` where that ray first meets the ground or a building within the tool's range (`hasHit` false: none). The aimed
// point when it is at least `minDist` m from the player (horizontally): what the crosshair is on. Else (aiming at the
// sky, past the range, or at the player's feet, where a tank would come out on top of them) `ahead` m in front of the
// player along the camera's heading; `snap` true then: the caller puts it on the ground. False only with no heading
// (looking straight up or down with nothing hit).
struct Spot { float at[3]; bool snap; };
// Closer than this (horizontally) the crosshair's point is the player's own feet: the spawn goes `ahead` instead.
constexpr float kMinSpawnDistance=10.0f;
inline bool PlaceSpawn(const float* me,const float* dir,const float* hit,bool hasHit,float minDist,float ahead,Spot* out) noexcept {
    if(hasHit) {
        const float dx=hit[0]-me[0],dz=hit[2]-me[2];
        if(std::sqrt(dx*dx+dz*dz)>=minDist){out->at[0]=hit[0];out->at[1]=hit[1];out->at[2]=hit[2];out->snap=false;return true;}
    }
    const float h=std::sqrt(dir[0]*dir[0]+dir[2]*dir[2]);
    if(!(h>1e-3f))return false;
    out->at[0]=me[0]+dir[0]/h*ahead;out->at[1]=me[1];out->at[2]=me[2]+dir[2]/h*ahead;out->snap=true;
    return true;
}
// The horizontal heading (unit x, z in [0], [2]) from `from` to `to`; `fallback` when they are on one spot.
inline void HeadingTo(const float* from,const float* to,const float* fallback,float* out) noexcept {
    float x=to[0]-from[0],z=to[2]-from[2];
    float n=std::sqrt(x*x+z*z);
    if(!(n>1e-3f)){x=fallback[0];z=fallback[2];n=std::sqrt(x*x+z*z);}
    if(!(n>1e-3f)){x=0.0f;z=1.0f;n=1.0f;}
    out[0]=x/n;out[1]=0.0f;out[2]=z/n;
}
// The world matrix of an object at `at` facing `heading` (unit, horizontal): rows right, up, forward, position, as the
// support spawns build theirs (support_spawn.cpp): orthonormal, determinant 1.
inline void FacingMatrix(const float* heading,const float* at,float* m) noexcept {
    const float x=heading[0],z=heading[2];
    const float r[16]={z,0.0f,-x,0.0f, 0.0f,1.0f,0.0f,0.0f, x,0.0f,z,0.0f, at[0],at[1],at[2],1.0f};
    for(int i=0;i<16;++i)m[i]=r[i];
}
// Why a row cannot be made now (debug_spawn.cpp SpawnRowState; the panels dim it and say so).
enum class RowState : std::uint8_t {
    ready,
    missing,        // a Mods row with a file not in Mods: not installed (the installer's EDF4.1 objects, the range's vehicles)
    notPreloaded,   // not loaded yet this mission: its first spawn loads it (debug_spawn.cpp RequestLoad), then makes it
    broken,         // the game faulted making it: off until the game restarts
    unavailable,    // its module is not up now (the support calls' cooldown, JetPilot=0, ...)
    online,         // a session: the plugin's objects would be this machine's alone
    loading,        // its load is queued: made as soon as the game reports it complete
};
}  // namespace debugspawn

// debug_spawn.cpp, the M map's 「摆放」 panel in the test range (range_place.h, mapcmd.cpp; ini RangePlace): whether it is
// on this mission (the mission is the test range pack's, edf5campaign.cpp TestRangeMode), a row's state now, and a row
// made at a ground point the map picked (its DebugSpawnStatus; a support call's own answer in `note`). Game thread.
bool RangePlaceOn() noexcept;
debugspawn::RowState SpawnRowState(int row) noexcept;
int PlaceRow(int row,const float* ground,wchar_t* note,std::size_t size) noexcept;
// edf5campaign.cpp: the current mode is the test range pack's (its content id TestRangeContent, as the game's own
// GetContentID reads it). Any thread; false when there is none to read.
bool TestRangeMode() noexcept;
}  // namespace crew
