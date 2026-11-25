// The room / HQ 3D preview's pure logic (src/room_preview.cpp is the game glue; docs/room-3d-preview-re.md plan A).
// No game memory, no Windows: tests/room_preview_test.cpp runs it offline.
//   - what the menu shows for a soldier kind: the menu SGO, the player class whose renderer draws it, its weapons;
//   - its colours as the renderer's float4 (stock: the AI template's soldier_color);
//   - the rewrite of the async build's Desc copy (UiHQ_3DModelView, 0x70 bytes);
//   - the camera's x shift while the menu is open;
//   - the planner: what to send the view this frame, from what is wanted and what it shows.
#pragma once
#include "preview_camera.h"
#include "support_loadout.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace crew::roompreview {

// --- The menu model of a soldier kind ---
// The player class numbers (CONFIG.SGO SoldierInit[class][0]): the Desc's +0x20 and +0x4C, the msg 3 / 5 payloads.
inline constexpr int kRangerClass=0,kWingDiverClass=1,kFencerClass=3;
constexpr int MenuClassOf(SupportWeapon k) noexcept {
    const int c=SupportSoldierClass(k);
    return c==0 ? kRangerClass : c==1 ? kWingDiverClass : kFencerClass;
}
// --- The unified model file (docs/unified-model-re.md; pylib/menu_model.py) ---
// The preview shows a unit from the very file the battle loads: each carries the keys only the menu reads, and the
// hook on the menu's model load (room_preview.cpp MenuModelLoad) makes the menu read kMenuModelKey where the battle
// reads animation_model (the one key both read with different values). No menu-only model file exists.
inline constexpr const wchar_t* kMenuModelKey=L"menu_animation_model";
inline constexpr std::uint16_t kDocClassic=0,kDocDsgo=2;   // doc variant tags (+0x10) whose member lookup the hook asks
// The support soldiers' AI templates (tools/support_loadout.py KINDS; src/support_soldier.cpp kBodies), SupportWeapon
// order; tools/menu_preview.py writes each and its _LEADER into Mods/OBJECT as a unified file. The X-Morph infantry
// (its body the installer's own, no menu keys) previews as the rifle Ranger.
inline constexpr const wchar_t* kSoldierTemplates[kSupportWeaponCount]={
    L"N601_COMMON_RANGER_AF",L"N601_COMMON_RANGER_FL",L"N601_COMMON_RANGER_RL",L"N601_COMMON_RANGER_SG",
    L"N601_COMMON_RANGER_SN",L"N606_AIPALEWING_LANCE",L"N606_AIPALEWING_LR",L"N606_AIPALEWING_MS",L"N606_AIPALEWING_IZN",
    L"N606_AIPALEWING_TB",L"N607_AIHEAVYARMOR_SC",L"N607_AIHEAVYARMOR_SMC",L"N607_AIHEAVYARMOR_SP",L"N607_AIHEAVYARMOR_SSG",
    L"N601_COMMON_RANGER_AF",
};
namespace detail {
inline std::size_t Join(wchar_t* out,std::size_t capacity,std::initializer_list<const wchar_t*> parts,bool lower) noexcept {
    if(!out || !capacity)return 0;
    out[0]=0;
    std::size_t n=0;
    for(const wchar_t* part:parts)
        for(const wchar_t* c=part;*c;++c) {
            if(n+1>=capacity){out[0]=0;return 0;}
            const wchar_t w=*c;
            out[n++]=lower && w>=L'A' && w<=L'Z' ? static_cast<wchar_t>(w-L'A'+L'a') : w;
        }
    out[n]=0;
    return n;
}
}  // namespace detail
// A soldier kind's unified file: `path` false its Mods-relative file (OBJECT\N601_COMMON_RANGER_AF_LEADER.SGO), true
// the path the Desc gets (app:/object/n601_common_ranger_af_leader.sgo). The length, 0 when it does not fit / no kind.
inline std::size_t SoldierFileOf(SupportWeapon k,bool leader,bool path,wchar_t* out,std::size_t capacity) noexcept {
    const int i=static_cast<int>(k);
    if(i<0 || i>=kSupportWeaponCount){if(out && capacity)out[0]=0;return 0;}
    return path ? detail::Join(out,capacity,{L"app:/object/",kSoldierTemplates[i],leader ? L"_LEADER" : L"",L".SGO"},true)
                : detail::Join(out,capacity,{L"OBJECT\\",kSoldierTemplates[i],leader ? L"_LEADER" : L"",L".SGO"},false);
}
// Every support vehicle body's battle file is a unified one (docs/unified-model-re.md): the jets' bases as
// pylib/vcobjects.py jet_sgo makes them, the tank's base as AutoTurret's install writes it, the mech's base by
// tools/menu_preview.py, each loaded variant (EDF6VC_LO_*) by tools/support_loadout.py. So every body is previewed.
constexpr bool VehicleUnified(VehicleBody body) noexcept { return VehicleBodyOf(body)!=nullptr; }
// Whether `body` flies on a jet CAS (its menu file names the renamed menu copy kJetMenuCas, not its battle CAS).
constexpr bool VehicleJet(VehicleBody body) noexcept {
    const VehicleBodyRow* row=VehicleBodyOf(body);
    return row && !row->tank && body!=VehicleBody::mech;
}
// The jets' menu CAS (tools/menu_preview.py jet_cas_rel; pylib/menu_model.py menu_cas_name) under Mods: a jet's unified
// file names it, so a jet is previewed only when it is there (a CAS without `main` crashes the renderer).
inline constexpr const wchar_t* kJetMenuCas=L"MENUOBJECT\\EDF6VC_MENU_V506_HELI.CAS";
// The unified file the battle spawns for `body` with loadout `load` (as SoldierFileOf: `path` false the Mods-relative
// OBJECT\EDF6VC_LO_TANK_<hex>.SGO, true app:/object/edf6vc_lo_tank_<hex>.sgo): the loaded variant
// (support_loadout.h VehicleVariantFile) when `load` is on and names one, else the body's base. 0 when there is none.
inline std::size_t VehicleFileOf(VehicleBody body,const VehicleLoadout& load,bool path,wchar_t* out,std::size_t capacity) noexcept {
    const VehicleBodyRow* row=VehicleBodyOf(body);
    if(!row || (load.On() && load.body!=body)){if(out && capacity)out[0]=0;return 0;}
    wchar_t variant[96];
    const wchar_t* name=load.On() && VehicleVariantFile(load,variant,sizeof(variant)/sizeof(variant[0])) ? variant : row->base;
    return path ? detail::Join(out,capacity,{L"app:/object/",name},true) : detail::Join(out,capacity,{L"OBJECT\\",name},false);
}
// The body's base file (no loadout).
inline std::size_t VehicleFileOf(VehicleBody body,bool path,wchar_t* out,std::size_t capacity) noexcept {
    return VehicleFileOf(body,VehicleLoadout{},path,out,capacity);
}

// The WeaponTable rows (WEAPON/WEAPONTABLE.SGO names, looked up at run time by 0xE1BA0) the menu hangs on the model in
// place of the AI weapons (docs/room-3d-preview-re.md §5; the leaders' LAZR02 / IZNR02 / TB02 look as the 01s). The
// menu shows the selected pair (Desc +0x50 = 0).
inline constexpr int kDescWeaponsMost=6;
struct MenuWeapons { int count; const wchar_t* name[2]; };
constexpr MenuWeapons MenuWeaponsOf(SupportWeapon k) noexcept {
    switch(k) {
        case SupportWeapon::rifle:
        case SupportWeapon::xmInfantry: return {1,{L"aWeapon003",nullptr}};   // the X-Morph infantry previews as the rifle Ranger
        case SupportWeapon::flame: return {1,{L"aWeapon197",nullptr}};
        case SupportWeapon::rocket: return {1,{L"RocketLauncher01",nullptr}};
        case SupportWeapon::shotgun: return {1,{L"aWeapon046",nullptr}};
        case SupportWeapon::sniper: return {1,{L"aWeapon077",nullptr}};
        case SupportWeapon::wingLance: return {1,{L"pWeapon001",nullptr}};
        case SupportWeapon::wingLaser: return {1,{L"LAZR01",nullptr}};
        case SupportWeapon::wingMonster: return {1,{L"pWeapon131",nullptr}};
        case SupportWeapon::wingIzuna: return {1,{L"pWeapon100",nullptr}};
        case SupportWeapon::wingThunderBow: return {1,{L"pWeapon102",nullptr}};
        // A Fencer's in its unified file's order (the template's soldier_weapon_slot: [0] weapon_r, [1] weapon_l):
        // the right-hand weapon, then the shield.
        case SupportWeapon::fencerCannon: return {2,{L"hCannon01",L"hShield01"}};
        case SupportWeapon::fencerMiddleCannon: return {2,{L"hWeapon077",L"hShield01"}};
        case SupportWeapon::fencerPileBanker: return {2,{L"hPileBanker01",L"hShield01"}};
        case SupportWeapon::fencerShotgun: return {2,{L"hWeapon077",L"hShield01"}};
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
    kRangerStock,                                                     // xminfantry: previewed as the rifle Ranger
};
constexpr const StockColours& StockOf(SupportWeapon k,bool leader) noexcept {
    const int i=static_cast<int>(k);
    if(leader && SupportSoldierClass(k)==0)
        return k==SupportWeapon::rifle || k==SupportWeapon::xmInfantry ? kRangerLeaderBody : kRangerLeaderHead;
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

// A vehicle's override: the Ranger class number (the renderer's class only picks the weapons' grip, and there are none),
// no weapons; its colours are the menu's channels, which a vehicle's materials do not take (they stay as the model has
// them).
inline DescOverride VehicleOverride() noexcept {
    DescOverride o;
    o.cls=kRangerClass;
    o.colour[0]=kRangerStock.c[0];o.colour[1]=kRangerStock.c[1];
    return o;
}

// --- Where the model stands (view +0x2C0 / +0x2D0 / +0x2E0 / +0x2F0, float4 each: the view's update 0x93CF99 lerps
// +0x2C0 -> +0x2D0 and +0x2E0 -> +0x2F0 by the zoom +0x300, the two results by +0x308, into +0x2B0, the translation of the
// model's matrix (the yaw +0x2A4 its rotation; docs/room-3d-preview-re.md section 6): the model's place in front of the
// camera, MENUMODELSETTING camera_pos). While the menu is open each point's x moves by `dx` (the soldier beside the
// menu) and then the whole xyz scales by `scale` (a vehicle, larger than a soldier, stands that much farther away and
// so fits the frame; 1 a soldier). Re-applied when the game rewrote a point (its new value is the new base); restored
// only where the point still holds what was written. w is never touched.
inline constexpr float kCameraShiftX=-0.8f;
struct CameraShift {
    bool on=false;
    float base[4][3]{};
    float written[4][3]{};
    static bool Same(const float* p,const float* q) noexcept { return p[0]==q[0] && p[1]==q[1] && p[2]==q[2]; }
    void Apply(float* const p[4],float dx,float scale=1.f) noexcept {
        for(int i=0;i<4;++i) {
            if(on && !Same(p[i],written[i]))for(int c=0;c<3;++c)base[i][c]=p[i][c];   // the game's own new value
            else if(!on)for(int c=0;c<3;++c)base[i][c]=p[i][c];
            const float want[3]={(base[i][0]+dx)*scale,base[i][1]*scale,base[i][2]*scale};
            for(int c=0;c<3;++c){p[i][c]=want[c];written[i][c]=want[c];}
        }
        on=true;
    }
    void Restore(float* const p[4]) noexcept {
        if(!on)return;
        for(int i=0;i<4;++i)if(Same(p[i],written[i]))for(int c=0;c<3;++c)p[i][c]=base[i][c];
        on=false;
    }
};
// How much farther a vehicle stands than a soldier (its size against a soldier's ~1.8 m: the tank V505 about 7 m long,
// the jets 15-20 m; the soldier's own placement, camera_pos [0] = (1.1, -0.9, -3.5), fills the frame with ~2 m).
// The mech (a Nix stands about 5 m) is framed as the tank, not as a jet. Guessed from those sizes (L): to be tuned in the
// game.
inline constexpr float kTankScale=3.0f,kJetScale=6.0f;
constexpr float VehicleScale(VehicleBody b) noexcept {
    const VehicleBodyRow* row=VehicleBodyOf(b);
    return !row ? 1.f : row->tank || b==VehicleBody::mech ? kTankScale : kJetScale;
}
// A vehicle turns slowly on its stand (the yaw +0x2A4 the menu otherwise holds): every pylon comes round. rad a 60 Hz
// frame (0.6 rad/s); Hold takes the frame's length in such frames (the UI's dt * 60), so the turn keeps its speed at any
// frame rate.
inline constexpr float kVehicleTurn=0.01f;
inline constexpr float kTwoPi=6.28318531f;
constexpr float Turned(float yaw,float by) noexcept {
    float y=yaw+by;
    return y>=kTwoPi ? y-kTwoPi : y<0.f ? y+kTwoPi : y;
}
// The turntable's lifecycle: the yaw the stand had when a vehicle first turned is kept, and put back as soon as no
// vehicle turns any more (a soldier under the cursor again, the menu closed, the view forgotten), so the soldier and
// afterwards the player's own model face where they faced before (the menu otherwise never turns the model).
struct Turntable {
    bool on=false;
    float base=0.f;
    // The yaw to hold this frame (`yaw` the one held so far): turning, it advances; stopping, the saved yaw.
    float Hold(float yaw,bool turn,float frames=1.f) noexcept {
        if(turn) {
            if(!on){on=true;base=yaw;}
            return Turned(yaw,kVehicleTurn*frames);
        }
        if(on){on=false;return base;}
        return yaw;
    }
};

// --- The planner ---
// What the menu asks for, what the view shows, what to send it. One step a view frame (the view's own update: the
// view is alive there); every message goes to the index-0 view itself.
struct Want {
    bool on=false;
    SupportWeapon kind=SupportWeapon::rifle;
    bool leader=false;
    VehicleBody body=VehicleBody::none;   // not none: that vehicle (kind / leader unused)
    Rgba colour[2]{};
};
struct Shown {
    std::int32_t cls=-1,outfit=-1;   // view +0x180 / +0x178: the displayed model's
    const float* colour[2]{};        // view +0x158 / +0x168
    bool pending=false;              // a soldier build still to run (the worker has not taken the override yet)
};
inline constexpr int kNone=-1;
constexpr int ModelKey(SupportWeapon k,bool leader) noexcept { return static_cast<int>(k)*2+(leader ? 1 : 0); }
inline constexpr int kVehicleKeyBase=1000;
constexpr int ModelKey(const Want& w) noexcept {
    return w.body!=VehicleBody::none ? kVehicleKeyBase+static_cast<int>(w.body) : ModelKey(w.kind,w.leader);
}
// The class the shown model's Desc carries (view +0x180 once built).
constexpr int ShownClassOf(const Want& w) noexcept { return w.body!=VehicleBody::none ? kRangerClass : MenuClassOf(w.kind); }
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
        const int key=ModelKey(w);
        if(p.posted!=key){a.buildSoldier=true;p.posted=key;p.waited=0;return a;}
        if(w.body!=VehicleBody::none)return a;   // a vehicle has no colour channels to send
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
// TickPreview stopped (the room / HQ screen went without closing the menu): the view puts the player back by itself
// after kOrphanFrames 60 Hz frames of the view's own (counted by the UI's dt, so the same time at any frame rate).
inline constexpr unsigned kOrphanFrames=30;

// --- The squad's other soldiers (docs/room-3d-preview-re.md section 6.3): views the preview makes, one soldier each ---
// Such a view shows only ever its soldier (never the player): no msg 8 lock, no player rebuild. It stays hidden
// (+0x3EC 1) until its model is the soldier asked for: its constructor posts a build of player 0 before the preview can
// arm the override (the Async is made at the constructor's end, right before that msg 3), and the job queue's order
// against the preview's own build is not proven (two workers could finish them in either order), so a shown model
// that is not the soldier, with no build of ours still to run, is built again after kSquadRebuildFrames (not every
// frame: the finished build is taken by the view's update a frame or more after the worker took the override).
inline constexpr unsigned kSquadRebuildFrames=30;
struct SquadPlan {
    int posted=kNone;    // the soldier last asked for (ModelKey)
    unsigned waited=0;   // frames the shown model has not been it
};
struct SquadActions {
    bool build=false,reveal=false;
    bool recolour[2]{};
};
inline SquadActions SquadStep(SquadPlan& p,const Want& w,const Shown& s) noexcept {
    SquadActions a;
    const int key=ModelKey(w);
    if(p.posted!=key){a.build=true;p.posted=key;p.waited=0;return a;}
    const bool soldierShown=!s.pending && s.cls==MenuClassOf(w.kind) && s.outfit==kSentinelOutfit;
    if(!soldierShown) {
        if(!s.pending && ++p.waited>=kSquadRebuildFrames){a.build=true;p.waited=0;}
        return a;
    }
    p.waited=0;
    a.reveal=true;
    for(int ch=0;ch<2;++ch)a.recolour[ch]=s.colour[ch] && !SameRgba(w.colour[ch],s.colour[ch]);
    return a;
}
// How many views the preview keeps beside the game's own: the squad's soldiers past the first, while the menu shows a
// squad in a placed viewport (unplaced, every view would stand at MENUMODELSETTING's one camera_pos, on each other).
constexpr int ExtraViewsWanted(bool squadOn,int count,bool placed,bool mainShowsSoldier) noexcept {
    return !squadOn || !placed || !mainShowsSoldier || count<2 ? 0 : (count>kSquadMost ? kSquadMost : count)-1;
}

}  // namespace crew::roompreview
