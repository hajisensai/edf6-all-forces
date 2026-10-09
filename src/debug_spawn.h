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
//  - the plugin's aircraft: JetLaunch / JetLaunchDrone / HeliLaunch (jet_spawn.cpp), NPC-flown as when called;
//  - a stock enemy: the steps the script's CreateEnemy (0x1AD220 -> 0x1D8900) takes: CreateObject, SetTeam(enemy),
//    SetLevel, the active flag's 0x548D50;
//  - a friendly soldier: the support soldiers' stock Ranger (support_soldier.cpp), let go of at once.
#pragma once
#include <cmath>
#include <cstdint>

namespace crew {
namespace debugspawn {
enum class Category : std::uint8_t { vehicle, aircraft, enemy, soldier, count };
constexpr int kCategoryCount=static_cast<int>(Category::count);
// The categories' names are the HUD's words (src/hudtext.inc debugSpawnVehicles ...), in this order.

// How a row is made (debug_spawn.cpp Spawn). `arg`: jetRole the JetRole (crew.h, same order), heli the HeliBody,
// soldier the SupportWeapon (support_call.h); the others none.
enum class How : std::uint8_t { stockVehicle, jetRole, drone, heli, enemy, soldier };
struct Entry {
    Category category;
    How how;
    const char* id;          // the log's (ASCII: the log is narrow text)
    const wchar_t* name;     // the menu's
    const wchar_t* sgo;      // stockVehicle / enemy: the stock SGO this tool preloads and creates (nullptr otherwise)
    int arg;
    float lift;              // m over the ground point it starts at (a flier higher up)
};

// The rows. Stock SGOs are Root.cpk's own (tests/debug_spawn_resource_audit.py reads each: a ground vehicle's must carry
// mission_setup, every one must exist); the stock vehicles are the `_mission` ones the stock missions and the test range
// place (testrange/gen.py VEHICLES), the enemies the test range's (ENEMIES, CreateEnemy). The plugin's aircraft are
// the ones a call brings (their SGOs the installer's, preloaded by PreloadJets when installed).
inline constexpr Entry kEntries[]={
    {Category::vehicle,How::stockVehicle,"V505_TANK_MISSION",L"布莱克战车 505",L"app:/object/V505_TANK_MISSION.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"VEHICLE403_TANK_MISSION",L"坦克 403",L"app:/object/VEHICLE403_TANK_MISSION.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"VEHICLE404_BIGTANK",L"大型坦克 404",L"app:/object/VEHICLE404_BIGTANK.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V603_FLAK_MISSION",L"防空车 603",L"app:/object/V603_FLAK_MISSION.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V510_MASER_MISSION",L"EMC 510",L"app:/object/V510_MASER_MISSION.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V605_BARGA_CANNON_MISSION",L"巴尔加炮 605",L"app:/object/V605_BARGA_CANNON_MISSION.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V507_RESCUETANK_AI",L"救援装甲车 507",L"app:/object/V507_RESCUETANK_AI.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V512_KEITRUCK",L"轻卡车 512",L"app:/object/V512_KEITRUCK.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V503_BIKE",L"摩托 503",L"app:/object/V503_BIKE.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V613_BIKE",L"摩托 613",L"app:/object/V613_BIKE.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V504_BEGARUTA_MISSION",L"机甲 Begaruta 504",L"app:/object/V504_BEGARUTA_MISSION.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"VEHICLE407_BIGBEGARUTA_MISSION",L"大型机甲 407",L"app:/object/VEHICLE407_BIGBEGARUTA_MISSION.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V612_NIX_G_MISSION",L"机甲 Nix G 612",L"app:/object/V612_NIX_G_MISSION.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V614_PROTEUS_MK2_MISSION",L"机甲 Proteus 614",L"app:/object/V614_PROTEUS_MK2_MISSION.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V608_OLDROBOT_G_MISSION",L"旧型机甲 608",L"app:/object/V608_OLDROBOT_G_MISSION.sgo",0,0.5f},
    {Category::vehicle,How::stockVehicle,"V515_RETROBALAM_MISSION",L"巨型机甲 Balam 515",L"app:/object/V515_RETROBALAM_MISSION.sgo",0,0.5f},
    // jetRole: JetRole's order (crew.h): strike, fighter, interceptor, multirole, carrier, blastCarrier, dollCarrier, gunship.
    {Category::aircraft,How::jetRole,"jet_strike",L"对地攻击机",nullptr,0,150.0f},
    {Category::aircraft,How::jetRole,"jet_fighter",L"制空战斗机",nullptr,1,150.0f},
    {Category::aircraft,How::jetRole,"jet_interceptor",L"截击机",nullptr,2,150.0f},
    {Category::aircraft,How::jetRole,"jet_multirole",L"多用途战斗机",nullptr,3,150.0f},
    {Category::aircraft,How::jetRole,"jet_carrier",L"空中航母",nullptr,4,150.0f},
    {Category::aircraft,How::jetRole,"jet_blast_carrier",L"自爆无人机母舰",nullptr,5,150.0f},
    {Category::aircraft,How::jetRole,"jet_doll_carrier",L"人偶无人机母舰",nullptr,6,150.0f},
    {Category::aircraft,How::jetRole,"jet_gunship",L"炮舰机",nullptr,7,150.0f},
    {Category::aircraft,How::drone,"jet_drone",L"机炮无人机",nullptr,0,60.0f},
    // heli: HeliBody's order (crew.h): brute410, eros506, medic410.
    {Category::aircraft,How::heli,"heli_506",L"直升机 Eros 506",nullptr,1,0.0f},
    {Category::aircraft,How::heli,"heli_410",L"直升机 Brute 410",nullptr,0,0.0f},
    {Category::aircraft,How::heli,"heli_medic",L"医疗直升机",nullptr,2,0.0f},
    {Category::enemy,How::enemy,"GIANTANT01",L"巨蚁",L"app:/object/GIANTANT01.sgo",0,0.5f},
    {Category::enemy,How::enemy,"E650_GIANTANT01",L"巨蚁（EDF6）",L"app:/object/E650_GIANTANT01.sgo",0,0.5f},
    {Category::enemy,How::enemy,"GIANTSPIDER01",L"巨蜘蛛",L"app:/object/GIANTSPIDER01.sgo",0,0.5f},
    {Category::enemy,How::enemy,"E651_SPIDER01_LIGHT",L"蜘蛛（轻）",L"app:/object/E651_SPIDER01_LIGHT.sgo",0,0.5f},
    {Category::enemy,How::enemy,"GIANTANTQUEEN",L"母体（女王巨蚁）",L"app:/object/GIANTANTQUEEN.sgo",0,0.5f},
    {Category::enemy,How::enemy,"GIANTBEE01CHARGE",L"巨蜂（飞）",L"app:/object/GIANTBEE01CHARGE.sgo",0,20.0f},
    {Category::enemy,How::enemy,"E503_FROG_AF",L"蛙兵",L"app:/object/E503_FROG_AF.sgo",0,0.5f},
    {Category::enemy,How::enemy,"E503_ARMORFROG_AF",L"装甲蛙兵",L"app:/object/E503_ARMORFROG_AF.sgo",0,0.5f},
    {Category::enemy,How::enemy,"E601_MARTIAN_GS",L"火星人",L"app:/object/E601_MARTIAN_GS.sgo",0,0.5f},
    {Category::enemy,How::enemy,"E507_GOLDUFO",L"金色 UFO（飞）",L"app:/object/E507_GOLDUFO.sgo",0,30.0f},
    {Category::enemy,How::enemy,"E515_IMPERIALUFO",L"帝国 UFO（飞）",L"app:/object/E515_IMPERIALUFO.sgo",0,30.0f},
    {Category::enemy,How::enemy,"DRAGONSMALL401",L"小龙（飞）",L"app:/object/DRAGONSMALL401.sgo",0,30.0f},
    {Category::enemy,How::enemy,"SHOOTINGTARGET",L"训练靶子（不动）",L"app:/object/SHOOTINGTARGET.sgo",0,0.5f},
    // soldier: SupportWeapon's order (support_call.h): rifle, flame, rocket, shotgun, sniper.
    {Category::soldier,How::soldier,"ranger_rifle",L"游骑兵（步枪）",nullptr,0,0.2f},
    {Category::soldier,How::soldier,"ranger_flame",L"游骑兵（火焰）",nullptr,1,0.2f},
    {Category::soldier,How::soldier,"ranger_rocket",L"游骑兵（火箭）",nullptr,2,0.2f},
    {Category::soldier,How::soldier,"ranger_shotgun",L"游骑兵（霰弹）",nullptr,3,0.2f},
    {Category::soldier,How::soldier,"ranger_sniper",L"游骑兵（狙击）",nullptr,4,0.2f},
};
constexpr int kEntryCount=static_cast<int>(sizeof(kEntries)/sizeof(kEntries[0]));

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
}  // namespace debugspawn
}  // namespace crew
