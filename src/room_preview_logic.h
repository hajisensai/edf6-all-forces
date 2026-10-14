// The room / HQ 3D preview's pure logic (src/room_preview.cpp is the game glue; docs/room-3d-preview-re.md plan A).
// No game memory, no Windows: tests/room_preview_test.cpp runs it offline.
//   - what the menu shows for a soldier kind: the menu SGO, the player class whose renderer draws it, its weapons;
//   - its colours as the renderer's float4 (stock: the AI template's soldier_color);
//   - the rewrite of the async build's Desc copy (UiHQ_3DModelView, 0x70 bytes);
//   - the camera's x shift while the menu is open;
//   - the planner: what to send the view this frame, from what is wanted and what it shows.
#pragma once
#include "support_loadout.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace crew::roompreview {

// --- The menu model of a soldier kind ---
// The player class numbers (CONFIG.SGO SoldierInit[class][0]): the Desc's +0x20 and +0x4C, the msg 3 / 5 payloads.
inline constexpr int kRangerClass=0,kWingDiverClass=1,kFencerClass=3;
constexpr int MenuClassOf(SupportWeapon k) noexcept {
    const int c=SupportSoldierClass(k);
    return c==0 ? kRangerClass : c==1 ? kWingDiverClass : kFencerClass;
}
// The generated menu SGOs (tools/menu_preview.py, Mods/MENUOBJECT): the Ranger NPC body n601_common_ranger in the stock
// P505 menu SGO; _L colours the head only (the Flame / Rocket / Shotgun / Sniper leaders; the Rifle leader colours head
// and body, which is every colourable material, so it is the plain one). The Wing Diver / Fencer NPCs wear the player
// bodies p606 / p607: their stock menu SGOs (CONFIG.SGO SoldierInit outfit 3, spelled as there: one FNV cache entry).
inline constexpr const wchar_t* kMenuRanger=L"app:/MENUOBJECT/EDF6VC_MENU_RANGER.SGO";
inline constexpr const wchar_t* kMenuRangerLeader=L"app:/MENUOBJECT/EDF6VC_MENU_RANGER_L.SGO";
inline constexpr const wchar_t* kMenuRangerFile=L"MENUOBJECT\\EDF6VC_MENU_RANGER.SGO";      // under Mods
inline constexpr const wchar_t* kMenuRangerLeaderFile=L"MENUOBJECT\\EDF6VC_MENU_RANGER_L.SGO";
inline constexpr const wchar_t* kMenuRangerStock=L"APP:/MENUOBJECT/p505_ranger.SGO";        // no generated file
inline constexpr const wchar_t* kMenuWingDiver=L"APP:/MENUOBJECT/p606_WingDiver.SGO";
inline constexpr const wchar_t* kMenuFencer=L"APP:/MENUOBJECT/p607_fencer.SGO";
constexpr bool HeadOnlyLeader(SupportWeapon k,bool leader) noexcept {
    return leader && SupportSoldierClass(k)==0 && k!=SupportWeapon::rifle;
}
// The generated file the kind needs (Mods-relative), or nullptr (a stock menu SGO).
constexpr const wchar_t* MenuFileOf(SupportWeapon k,bool leader) noexcept {
    return SupportSoldierClass(k)!=0 ? nullptr : HeadOnlyLeader(k,leader) ? kMenuRangerLeaderFile : kMenuRangerFile;
}
// The path the Desc gets. `generated`: MenuFileOf's file is in Mods (a Ranger without it: the stock player Ranger).
constexpr const wchar_t* MenuPathOf(SupportWeapon k,bool leader,bool generated) noexcept {
    const int c=SupportSoldierClass(k);
    if(c==1)return kMenuWingDiver;
    if(c==2)return kMenuFencer;
    if(!generated)return kMenuRangerStock;
    return HeadOnlyLeader(k,leader) ? kMenuRangerLeader : kMenuRanger;
}

// The WeaponTable rows (WEAPON/WEAPONTABLE.SGO names, looked up at run time by 0xE1BA0) the menu hangs on the model in
// place of the AI weapons (docs/room-3d-preview-re.md §5; the leaders' LAZR02 / IZNR02 / TB02 look as the 01s). A
// Fencer's list is CONFIG's order, [0] left (the shield), [1] right; the menu shows the selected pair (Desc +0x50 = 0).
inline constexpr int kDescWeaponsMost=6;
struct MenuWeapons { int count; const wchar_t* name[2]; };
constexpr MenuWeapons MenuWeaponsOf(SupportWeapon k) noexcept {
    switch(k) {
        case SupportWeapon::rifle: return {1,{L"aWeapon003",nullptr}};
        case SupportWeapon::flame: return {1,{L"aWeapon197",nullptr}};
        case SupportWeapon::rocket: return {1,{L"RocketLauncher01",nullptr}};
        case SupportWeapon::shotgun: return {1,{L"aWeapon046",nullptr}};
        case SupportWeapon::sniper: return {1,{L"aWeapon077",nullptr}};
        case SupportWeapon::wingLance: return {1,{L"pWeapon001",nullptr}};
        case SupportWeapon::wingLaser: return {1,{L"LAZR01",nullptr}};
        case SupportWeapon::wingMonster: return {1,{L"pWeapon131",nullptr}};
        case SupportWeapon::wingIzuna: return {1,{L"pWeapon100",nullptr}};
        case SupportWeapon::wingThunderBow: return {1,{L"pWeapon102",nullptr}};
        case SupportWeapon::fencerCannon: return {2,{L"hShield01",L"hCannon01"}};
        case SupportWeapon::fencerMiddleCannon: return {2,{L"hShield01",L"hWeapon077"}};
        case SupportWeapon::fencerPileBanker: return {2,{L"hShield01",L"hPileBanker01"}};
        case SupportWeapon::fencerShotgun: return {2,{L"hShield01",L"hWeapon077"}};
        default: return {0,{nullptr,nullptr}};
    }
}

// --- Colours ---
// The AI templates' soldier_color (OBJECT/N60x_*.SGO, change_color0 / 1), the menu's stock look. The Ranger leaders
// colour the head (and the Rifle leader the body, in its own colour: the preview shows the body's); the Wing Diver
// leader's red face tint is not a channel the menu has.
struct Rgba { float v[4]; };
struct StockColours { Rgba c[2]; };
inline constexpr StockColours kRangerStock{{{{0.159f,0.215f,0.148f,1.f}},{{0.161f,0.139f,0.111f,1.f}}}};
inline constexpr StockColours kRangerLeaderHead{{{{0.167f,0.051f,0.010f,1.f}},{{0.222f,0.154f,0.100f,1.f}}}};
inline constexpr StockColours kRangerLeaderBody{{{{0.329f,0.262f,0.154f,1.f}},{{0.171f,0.082f,0.059f,1.f}}}};
inline constexpr StockColours kKindStock[kSupportWeaponCount]={
    kRangerStock,kRangerStock,kRangerStock,kRangerStock,kRangerStock,
    {{{{0.003f,0.031f,0.141f,1.f}},{{0.8f,0.8f,0.8f,1.f}}}},   // lance
    {{{{0.941f,0.098f,0.890f,1.f}},{{0.8f,0.8f,0.8f,1.f}}}},   // laser
    {{{{0.031f,0.482f,0.478f,1.f}},{{0.8f,0.8f,0.8f,1.f}}}},   // monster
    {{{{0.925f,0.537f,0.121f,1.f}},{{0.8f,0.8f,0.8f,1.f}}}},   // izuna
    {{{{0.274f,0.956f,0.274f,1.f}},{{0.8f,0.8f,0.8f,1.f}}}},   // thunderbow
    {{{{0.549f,0.360f,0.203f,1.f}},{{0.631f,0.631f,0.333f,1.f}}}},   // cannon
    {{{{0.255f,0.443f,0.451f,1.f}},{{0.302f,0.302f,0.549f,1.f}}}},   // midcannon
    {{{{0.278f,0.251f,0.251f,1.f}},{{0.251f,0.243f,0.251f,1.f}}}},   // pilebanker
    {{{{0.709f,0.286f,0.094f,1.f}},{{0.788f,0.0f,0.0f,1.f}}}},       // fshotgun
};
constexpr const StockColours& StockOf(SupportWeapon k,bool leader) noexcept {
    const int i=static_cast<int>(k);
    if(leader && SupportSoldierClass(k)==0)return k==SupportWeapon::rifle ? kRangerLeaderBody : kRangerLeaderHead;
    return kKindStock[i>=0 && i<kSupportWeaponCount ? i : 0];
}
// 0xRRGGBB as the generator writes it (tools/support_loadout.py recolour: each byte / 255, alpha 1); kStockColour: `stock`.
constexpr Rgba ColourOf(std::int32_t rgb,const Rgba& stock) noexcept {
    if(rgb==kStockColour)return stock;
    return Rgba{{static_cast<float>((rgb>>16)&0xFF)/255.f,static_cast<float>((rgb>>8)&0xFF)/255.f,
                 static_cast<float>(rgb&0xFF)/255.f,1.f}};
}
inline bool SameRgba(const Rgba& a,const float* b) noexcept { return std::memcmp(a.v,b,sizeof(a.v))==0; }

// --- The Desc (UiHQ_3DModelView +0x318; the async build gets a copy: 0x93F910's second argument) ---
inline constexpr std::size_t kDescPath=0x0,kDescModelType=0x20,kDescColour0=0x24,kDescColour1=0x34,kDescOutfit=0x44,
    kDescWeaponCount=0x48,kDescClass=0x4C,kDescSelected=0x50,kDescWeapons=0x54,kDescSize=0x70;
// The outfit the soldier's Desc carries: no player outfit, so the HQ's own colour messages (msg 5 for the player's
// class / outfit) and msg 6's outfit test never take the soldier for the player. It is only compared (0x93C21A).
inline constexpr std::int32_t kSentinelOutfit=0x56430000;   // 'VC'
struct DescOverride {
    std::int32_t cls=kRangerClass;
    Rgba colour[2]{};
    std::int32_t weaponCount=0;
    std::int32_t weapon[kDescWeaponsMost]{};
};
// Every field but the path (the caller assigns that with the game's wstring::assign); weapons past the count untouched.
inline void RewriteDesc(unsigned char* desc,const DescOverride& o) noexcept {
    const std::int32_t outfit=kSentinelOutfit,selected=0;
    const std::int32_t count=o.weaponCount<0 ? 0 : o.weaponCount>kDescWeaponsMost ? kDescWeaponsMost : o.weaponCount;
    std::memcpy(desc+kDescModelType,&o.cls,4);
    std::memcpy(desc+kDescColour0,o.colour[0].v,16);
    std::memcpy(desc+kDescColour1,o.colour[1].v,16);
    std::memcpy(desc+kDescOutfit,&outfit,4);
    std::memcpy(desc+kDescWeaponCount,&count,4);
    std::memcpy(desc+kDescClass,&o.cls,4);
    std::memcpy(desc+kDescSelected,&selected,4);
    std::memcpy(desc+kDescWeapons,o.weapon,static_cast<std::size_t>(count)*4);
}
// The override for `kind`: `row(name)` the WeaponTable row (<0: not in this game's table, left out).
template<class Row> DescOverride OverrideOf(SupportWeapon kind,bool leader,const SupportLook& look,Row row) noexcept {
    DescOverride o;
    o.cls=MenuClassOf(kind);
    const StockColours& stock=StockOf(kind,leader);
    o.colour[0]=ColourOf(look.primary,stock.c[0]);
    o.colour[1]=ColourOf(look.secondary,stock.c[1]);
    const MenuWeapons w=MenuWeaponsOf(kind);
    for(int i=0;i<w.count;++i){const int r=row(w.name[i]);if(r>=0)o.weapon[o.weaponCount++]=r;}
    return o;
}

// --- The camera (view +0x2C0 / +0x2D0 / +0x2E0 / +0x2F0: the two pairs it interpolates between; set once, by the
// view's constructor) moved along x while the menu is open, so the soldier stands beside the menu. Re-applied when the
// game rewrote a point (its new value is the new base); restored only where the point still holds what was written.
inline constexpr float kCameraShiftX=-0.8f;
struct CameraShift {
    bool on=false;
    float written[4]{};
    void Apply(float* const x[4],float dx) noexcept {
        for(int i=0;i<4;++i)if(!on || *x[i]!=written[i]){*x[i]+=dx;written[i]=*x[i];}
        on=true;
    }
    void Restore(float* const x[4],float dx) noexcept {
        if(!on)return;
        for(int i=0;i<4;++i)if(*x[i]==written[i])*x[i]-=dx;
        on=false;
    }
};

// --- The planner ---
// What the menu asks for, what the view shows, what to send it. One step a view frame (the view's own update: the
// view is alive there); every message goes to the index-0 view itself.
struct Want {
    bool on=false;
    SupportWeapon kind=SupportWeapon::rifle;
    bool leader=false;
    Rgba colour[2]{};
};
struct Shown {
    std::int32_t cls=-1,outfit=-1;   // view +0x180 / +0x178: the displayed model's
    const float* colour[2]{};        // view +0x158 / +0x168
    bool pending=false;              // a soldier build still to run (the worker has not taken the override yet)
};
inline constexpr int kNone=-1;
constexpr int ModelKey(SupportWeapon k,bool leader) noexcept { return static_cast<int>(k)*2+(leader ? 1 : 0); }
inline constexpr unsigned kMostWaitFrames=180;   // a soldier build never run: give up waiting, put the player back
struct Plan {
    bool locked=false;   // msg 8 sent (the room's outfit message 6 held), +0x3EC saved
    int posted=kNone;    // the model last asked for: ModelKey, kNone the player's own
    unsigned waited=0;
};
struct Actions {
    bool lock=false,buildSoldier=false,buildPlayer=false,unlock=false;
    bool recolour[2]{};
    bool Any() const noexcept { return lock || buildSoldier || buildPlayer || unlock || recolour[0] || recolour[1]; }
};
inline Actions Step(Plan& p,const Want& w,const Shown& s) noexcept {
    Actions a;
    if(w.on) {
        if(!p.locked){a.lock=true;p.locked=true;}
        const int key=ModelKey(w.kind,w.leader);
        if(p.posted!=key){a.buildSoldier=true;p.posted=key;p.waited=0;return a;}
        const bool soldierShown=s.cls==MenuClassOf(w.kind) && s.outfit==kSentinelOutfit;
        for(int ch=0;ch<2;++ch)a.recolour[ch]=soldierShown && s.colour[ch] && !SameRgba(w.colour[ch],s.colour[ch]);
        return a;
    }
    if(p.posted!=kNone) {
        if(s.pending && p.waited<kMostWaitFrames){++p.waited;return a;}
        a.buildPlayer=true;p.posted=kNone;p.waited=0;
    }
    if(p.locked){a.unlock=true;p.locked=false;}
    return a;
}
// TickPreview stopped (the room / HQ screen went without closing the menu): the view puts the player back by itself.
inline constexpr unsigned kOrphanFrames=30;

}  // namespace crew::roompreview
