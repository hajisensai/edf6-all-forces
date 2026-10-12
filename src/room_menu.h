// The room / HQ support loadout menu's pure logic (docs/feature-2026-10-12-room-loadout-menu.md, stage 3): the model of
// every support unit's SupportPreset_<KEY> / SupportVehicle_<KEY> (src/support_loadout.h, the one grammar), the
// navigation state machine driven by abstract edge-triggered keys (the src/debug_spawn.h Menu / Press / Step pattern), the
// values a save writes (each checked by the real parsers first) and a render-agnostic snapshot the drawing layer (stage 4)
// consumes. No game memory, no Windows: tests/room_menu_test.cpp runs it offline.
//
// It edits what tools/support_loadout.py edit() / edit_vehicle() edits (the installer's menu 8), with the same rules:
//   a squad: its soldiers in seat order (a leader every four, i % 4 == 0), each a kind, a primary and a secondary colour
//       (or stock); up to the entry's seats (at most kSupportLoadoutMost). No soldiers: the entry's own load (value empty).
//   a tank: its main gun HE / AP, then up to 4 pylons AP / HE / GLM; a jet: up to 5 pylons AAM_S / AAM_M / AAM_L / AGM /
//       AGM_L / MK82 / RKT, none = GUNS; rounds from kRoundCounts. Stock: the value empty (the vehicle as it always came).
// A value the parser refused when it was read is shown stock and left alone unless the player changes that unit (the
// plugin keeps naming it, as before); a value the player did not change is never rewritten.
//
// Keys (Keys, each a press: down now, up last frame): toggle opens / closes (closing discards like cancel); up / down move
// the cursor (wrapping); right / confirm go in, left / cancel come out (cancel on the unit list closes, discarding);
// left / right change the field being edited (wrapping); add / remove a soldier or pylon; reset puts the unit stock.
// Levels: the unit list (its last row is "save"), a unit's rows (soldiers; a vehicle's gun row then its pylons), the
// fields of one row (kind / primary / secondary; store / rounds; the tank's gun).
#pragma once
#include "support_loadout.h"
#include <cstddef>
#include <cstdint>
#include <cwchar>

namespace crew::roommenu {

// --- Colours ---
// The palette a colour field cycles through (stock first, then these). The drawing layer may replace it with the game's
// HQ palette (the ColorChipBox colours, 0x880010) through Menu::palette; the values are 0xRRGGBB, the ini's spelling.
inline constexpr int kPaletteMost=32;
struct Palette { int count=0; std::int32_t rgb[kPaletteMost]{}; };
inline constexpr Palette kDefaultPalette{16,{
    0xFFFFFF,0xB0B0B0,0x505050,0x101010,0xC0272D,0xE87722,0xF2C230,0x4B5320,
    0x2E8B57,0x1F7A7A,0x4FA3E0,0x1E3A8A,0x6A3FA0,0xE07AB0,0x6B4423,0xC3B091}};
// A colour as RGBA for the drawing layer (alpha opaque); stock is 0.
constexpr std::uint32_t Rgba(std::int32_t colour) noexcept {
    return colour==kStockColour ? 0u : (static_cast<std::uint32_t>(colour&0xFFFFFF)<<8)|0xFFu;
}
// The next (step +1) or previous (-1) colour of the cycle stock, palette[0..count). A colour not in the palette (written
// by hand in the ini) counts as stock's place.
inline std::int32_t CycleColour(const Palette& p,std::int32_t colour,int step) noexcept {
    const int n=p.count+1;
    int at=0;
    for(int i=0;i<p.count;++i)if(p.rgb[i]==colour)at=i+1;
    at=((at+step)%n+n)%n;
    return at==0 ? kStockColour : p.rgb[at-1];
}

// --- The units ---
// One catalog entry the menu shows: its key (support_dispatch SupportCallKey), label, and the seats a preset may fill
// (SupportCallSeats). A key with seats is a squad; a key in kVehicleKeys a vehicle (one key may give both).
struct UnitSpec { const wchar_t* key; const wchar_t* label; int seats; };
// The entries tools/support_loadout.py SEATS / LABELS and kVehicleKeys list (the installer's menu 8), for a caller
// without the live catalog and for the tests.
inline constexpr UnitSpec kDefaultUnits[]={
    {L"SQUAD",L"步兵小队",12},{L"PLATOON",L"步兵大队",12},{L"TRANSPORT_CREWED",L"装甲运兵车·有人（乘客）",4},
    {L"TRUCK_CREWED",L"民用轻卡·有人（乘客）",4},{L"SQUAD_HELI",L"直升机机降·小队",12},
    {L"PLATOON_HELI",L"直升机机降·大队",12},{L"SQUAD_AIRDROP",L"运输机空降·小队",12},
    {L"PLATOON_AIRDROP",L"运输机空降·大队",12},
    {L"TANK_CREWED",L"坦克·有人",0},{L"TANK_DELIVERY",L"坦克·空车交付",0},{L"STRIKE",L"对地攻击机·守点",0},
    {L"STRIKE_F",L"对地攻击机·跟随",0},{L"FIGHTER",L"制空战斗机·守点",0},{L"FIGHTER_F",L"制空战斗机·跟随",0},
    {L"INTERCEPTOR",L"截击机·守点",0},{L"INTERCEPTOR_F",L"截击机·跟随",0},{L"MULTIROLE",L"多用途机·守点",0},
    {L"MULTIROLE_F",L"多用途机·跟随",0},
};
inline constexpr int kDefaultUnitCount=static_cast<int>(sizeof(kDefaultUnits)/sizeof(kDefaultUnits[0]));

inline constexpr int kMostUnits=48;
struct Unit {
    const wchar_t* key=nullptr;
    const wchar_t* label=nullptr;
    int seats=0;                              // a squad's most soldiers (min of the entry's seats and 12)
    VehicleBody body=VehicleBody::none;       // none: a squad
    SupportPreset squad{},squadLoaded{};      // the working copy and what the ini held
    VehicleLoadout vehicle{},vehicleLoaded{};
    bool refused=false;                       // the ini's value was refused by the parser (shown stock)
};
constexpr bool IsSquad(const Unit& u) noexcept { return u.body==VehicleBody::none; }

// Reads one ini value (support_config.h SupportIniRead's shape: empty when absent); returns its length.
using IniRead=std::size_t(*)(void* context,const wchar_t* key,wchar_t* out,std::size_t capacity) noexcept;

// --- The menu ---
enum class Level : std::uint8_t { units, rows, fields };
inline constexpr int kPageMost=16;
struct Menu {
    bool open=false;
    Level level=Level::units;
    int unitCount=0;
    Unit unit[kMostUnits]{};
    int unitAt=0;        // the unit list's cursor (unitCount: the save row)
    int rowAt=0;         // the unit's row (a vehicle: 0 the gun row, 1.. the pylons)
    int fieldAt=0;
    int pageRows=8;      // rows a column shows (1..kPageMost)
    int unitFirst=0,rowFirst=0;   // each column's first row shown
    Palette palette=kDefaultPalette;
};
struct Keys { bool toggle,up,down,left,right,confirm,cancel,add,remove,reset; };
enum class Act : std::uint8_t { none, opened, closed, moved, changed, refused, save };

// A key's press this frame: down now, up last frame (debug_spawn.h Press).
inline bool Press(bool& held,bool down) noexcept {
    const bool press=down && !held;
    held=down;
    return press;
}
// This frame's presses from the keys down now; `held` keeps every key's state.
inline Keys Presses(Keys& held,const Keys& down) noexcept {
    return {Press(held.toggle,down.toggle),Press(held.up,down.up),Press(held.down,down.down),Press(held.left,down.left),
            Press(held.right,down.right),Press(held.confirm,down.confirm),Press(held.cancel,down.cancel),
            Press(held.add,down.add),Press(held.remove,down.remove),Press(held.reset,down.reset)};
}

// --- Serialising (the exact grammar the parsers accept; stock is the empty value) ---
inline constexpr std::size_t kValueMost=384;   // 12 tokens of "thunderbow@RRGGBB:RRGGBB*12," fit
namespace detail {
struct Text {
    wchar_t* out;std::size_t capacity,n=0;bool fits=true;
    void Put(wchar_t c) noexcept {if(n+1<capacity)out[n++]=c;else fits=false;}
    void Puts(const wchar_t* s) noexcept {for(;*s;++s)Put(*s);}
    void Int(int v) noexcept {if(v>=10)Int(v/10);Put(static_cast<wchar_t>(L'0'+v%10));}
    void Colour(std::int32_t v) noexcept {
        if(v==kStockColour){Put(L'X');return;}
        for(int s=20;s>=0;s-=4)Put(L"0123456789ABCDEF"[(v>>s)&0xF]);
    }
    std::size_t End() noexcept {out[n]=0;return fits ? n : 0;}
};
inline bool SameSoldier(const SupportPreset& p,int a,int b) noexcept {
    return p.kind[a]==p.kind[b] && p.look[a]==p.look[b];
}
inline void SoldierToken(Text& t,const SupportPreset& p,int i,int run) noexcept {
    t.Puts(SupportKindIniName(p.kind[i]));
    const SupportLook& l=p.look[i];
    if(l.On()) {
        t.Put(L'@');t.Colour(l.primary);
        if(l.secondary!=kStockColour){t.Put(L':');t.Colour(l.secondary);}
    }
    if(run>1){t.Put(L'*');t.Int(run);}
}
}  // namespace detail
// A preset as the shortest value (equal neighbours as *n, tools/support_loadout.py format_preset). Returns the length
// (0 for the empty value or when it does not fit; out[0] is 0 then).
inline std::size_t SerialisePreset(const SupportPreset& p,wchar_t* out,std::size_t capacity) noexcept {
    if(!out || !capacity)return 0;
    detail::Text t{out,capacity};
    for(int i=0;i<p.count;) {
        int j=i;
        while(j<p.count && detail::SameSoldier(p,i,j))++j;
        if(i)t.Put(L',');
        detail::SoldierToken(t,p,i,j-i);
        i=j;
    }
    const std::size_t n=t.End();
    if(!n)out[0]=0;
    return n;
}
// A loadout (format_vehicle): a tank's gun then its pylons; a jet's pylons or GUNS; stock the empty value.
inline std::size_t SerialiseVehicle(const VehicleLoadout& l,wchar_t* out,std::size_t capacity) noexcept {
    if(!out || !capacity)return 0;
    detail::Text t{out,capacity};
    const VehicleBodyRow* row=VehicleBodyOf(l.body);
    if(row && row->tank)t.Puts(l.apGun ? L"AP" : L"HE");
    for(int i=0;row && i<l.count;++i) {
        const LoadStore* s=LoadStoreOf(l.store[i]);
        if(t.n)t.Put(L',');
        t.Puts(s ? s->name : L"?");t.Put(L':');t.Int(kRoundCounts[l.rounds[i]&0xF]);
    }
    if(row && !row->tank && !l.count)t.Puts(L"GUNS");
    const std::size_t n=t.End();
    if(!n)out[0]=0;
    return n;
}
inline bool SamePreset(const SupportPreset& a,const SupportPreset& b) noexcept {
    if(a.count!=b.count)return false;
    for(int i=0;i<a.count;++i)if(a.kind[i]!=b.kind[i] || !(a.look[i]==b.look[i]))return false;
    return true;
}
inline bool SameVehicle(const VehicleLoadout& a,const VehicleLoadout& b) noexcept {
    if(a.body!=b.body || a.apGun!=b.apGun || a.count!=b.count)return false;
    for(int i=0;i<a.count;++i)if(a.store[i]!=b.store[i] || a.rounds[i]!=b.rounds[i])return false;
    return true;
}
inline bool UnitChanged(const Unit& u) noexcept {
    return IsSquad(u) ? !SamePreset(u.squad,u.squadLoaded) : !SameVehicle(u.vehicle,u.vehicleLoaded);
}
inline bool Dirty(const Menu& m) noexcept {
    for(int i=0;i<m.unitCount;++i)if(UnitChanged(m.unit[i]))return true;
    return false;
}

// --- Loading ---
namespace detail {
inline void IniKey(const Unit& u,wchar_t* out,std::size_t capacity) noexcept {
    Text t{out,capacity};
    t.Puts(IsSquad(u) ? L"SupportPreset_" : L"SupportVehicle_");t.Puts(u.key);t.End();
}
inline void LoadUnit(Unit& u,IniRead read,void* context) noexcept {
    wchar_t key[96],value[kValueMost+64]{},why[8];
    IniKey(u,key,96);
    if(read)read(context,key,value,sizeof(value)/sizeof(value[0]));
    u.refused=IsSquad(u) ? !ParseSupportPreset(value,u.seats,&u.squadLoaded,why,8)
                         : !ParseVehicleLoadout(value,u.body,&u.vehicleLoaded,why,8);
    u.squad=u.squadLoaded;u.vehicle=u.vehicleLoaded;
}
inline void AddUnit(Menu& m,const UnitSpec& s,VehicleBody body) noexcept {
    if(m.unitCount>=kMostUnits)return;
    Unit& u=m.unit[m.unitCount++];
    u=Unit{};u.key=s.key;u.label=s.label;u.body=body;
    u.seats=s.seats<kSupportLoadoutMost ? s.seats : kSupportLoadoutMost;
}
}  // namespace detail
// The units of `specs` (a squad for each with seats, a vehicle for each in kVehicleKeys) with the ini's values; the
// cursor back at the top. `open` is left as it is (call it on Act::opened).
inline void Load(Menu& m,const UnitSpec* specs,int count,IniRead read,void* context) noexcept {
    m.unitCount=0;
    for(int i=0;specs && i<count;++i) {
        if(!specs[i].key)continue;
        if(specs[i].seats>0)detail::AddUnit(m,specs[i],VehicleBody::none);
        const VehicleBody body=VehicleBodyOfKey(specs[i].key);
        if(body!=VehicleBody::none)detail::AddUnit(m,specs[i],body);
    }
    for(int i=0;i<m.unitCount;++i)detail::LoadUnit(m.unit[i],read,context);
    m.level=Level::units;m.unitAt=m.rowAt=m.fieldAt=m.unitFirst=m.rowFirst=0;
}

// --- Rows and fields of a unit ---
// A squad's rows: its soldiers (one "own load" row when none). A vehicle's: the gun row (or one "stock" row), its pylons.
inline int RowCount(const Unit& u) noexcept {
    if(IsSquad(u))return u.squad.count>0 ? u.squad.count : 1;
    return 1+(u.vehicle.On() ? u.vehicle.count : 0);
}
inline int MostRows(const Unit& u) noexcept {
    if(IsSquad(u))return u.seats;
    const VehicleBodyRow* row=VehicleBodyOf(u.body);
    return row ? row->most : 0;
}
// Fields of row `r`: a soldier 3 (kind, primary, secondary); a tank's gun 1; a pylon 2 (store, rounds); else 0.
inline int FieldCount(const Unit& u,int r) noexcept {
    if(IsSquad(u))return u.squad.count>0 ? 3 : 0;
    if(!u.vehicle.On())return 0;
    if(r==0)return VehicleBodyOf(u.body)->tank ? 1 : 0;
    return 2;
}
inline bool IsTank(const Unit& u) noexcept {
    const VehicleBodyRow* row=VehicleBodyOf(u.body);
    return row && row->tank;
}
// The stores this body may carry, kLoadStores order (tank: AP HE GLM; jet: the seven air stores).
inline int AllowedStores(const Unit& u,std::uint8_t* out) noexcept {
    int n=0;
    for(const auto& s:kLoadStores)if(IsTank(u) ? s.tank : s.jet)out[n++]=s.code;
    return n;
}
// A new pylon's store and rounds: a tank 20 APFSDS (the stock extra rounds, make_stock_stores), a jet 2 AAM_S.
inline void NewPylon(const Unit& u,std::uint8_t* store,std::uint8_t* rounds) noexcept {
    *store=IsTank(u) ? 8 : 1;
    *rounds=static_cast<std::uint8_t>(RoundCode(IsTank(u) ? 20 : 2));
}

// --- Editing (each keeps the unit a value its parser accepts) ---
namespace detail {
inline int Wrap(int v,int step,int n) noexcept { return n>0 ? ((v+step)%n+n)%n : 0; }
// A stock vehicle made explicit: a tank's howitzer alone, a jet's guns alone (edit_vehicle's Loadout(body)).
inline void Materialise(Unit& u) noexcept {
    if(u.vehicle.On())return;
    u.vehicle=VehicleLoadout{};u.vehicle.body=u.body;
}
inline Act AddSoldier(Menu& m,Unit& u) noexcept {
    SupportPreset& p=u.squad;
    if(p.count>=u.seats)return Act::refused;
    const int at=p.count ? m.rowAt+1 : 0;
    for(int i=p.count;i>at;--i){p.kind[i]=p.kind[i-1];p.look[i]=p.look[i-1];}
    p.kind[at]=p.count ? p.kind[m.rowAt] : SupportWeapon::rifle;
    p.look[at]=p.count ? p.look[m.rowAt] : SupportLook{};
    ++p.count;m.rowAt=at;
    return Act::changed;
}
inline Act AddPylon(Menu& m,Unit& u) noexcept {
    Materialise(u);
    VehicleLoadout& l=u.vehicle;
    if(l.count>=MostRows(u))return Act::refused;
    NewPylon(u,&l.store[l.count],&l.rounds[l.count]);
    ++l.count;m.rowAt=l.count;
    return Act::changed;
}
inline Act RemoveSoldier(Menu& m,Unit& u) noexcept {
    SupportPreset& p=u.squad;
    if(p.count<=0)return Act::refused;
    for(int i=m.rowAt;i+1<p.count;++i){p.kind[i]=p.kind[i+1];p.look[i]=p.look[i+1];}
    --p.count;p.kind[p.count]=SupportWeapon::rifle;p.look[p.count]=SupportLook{};
    if(m.rowAt>=RowCount(u))m.rowAt=RowCount(u)-1;
    return Act::changed;
}
inline Act RemovePylon(Menu& m,Unit& u) noexcept {
    VehicleLoadout& l=u.vehicle;
    if(!l.On() || m.rowAt<1 || m.rowAt>l.count)return Act::refused;
    for(int i=m.rowAt-1;i+1<l.count;++i){l.store[i]=l.store[i+1];l.rounds[i]=l.rounds[i+1];}
    --l.count;l.store[l.count]=l.rounds[l.count]=0;
    if(m.rowAt>l.count)m.rowAt=l.count;
    return Act::changed;
}
inline void ChangeSoldier(Menu& m,Unit& u,int step) noexcept {
    const int i=m.rowAt;
    SupportLook& l=u.squad.look[i];
    if(m.fieldAt==0)u.squad.kind[i]=static_cast<SupportWeapon>(Wrap(static_cast<int>(u.squad.kind[i]),step,kSupportWeaponCount));
    else if(m.fieldAt==1)l.primary=CycleColour(m.palette,l.primary,step);
    else l.secondary=CycleColour(m.palette,l.secondary,step);
}
inline void ChangePylon(Menu& m,Unit& u,int step) noexcept {
    const int i=m.rowAt-1;
    if(m.fieldAt==1){u.vehicle.rounds[i]=static_cast<std::uint8_t>(Wrap(u.vehicle.rounds[i],step,16));return;}
    std::uint8_t allowed[kLoadStoreCount];
    const int n=AllowedStores(u,allowed);
    int at=0;
    for(int k=0;k<n;++k)if(allowed[k]==u.vehicle.store[i])at=k;
    u.vehicle.store[i]=allowed[Wrap(at,step,n)];
}
inline Act Change(Menu& m,Unit& u,int step) noexcept {
    if(IsSquad(u))ChangeSoldier(m,u,step);
    else if(m.rowAt==0)u.vehicle.apGun=!u.vehicle.apGun;
    else ChangePylon(m,u,step);
    return Act::changed;
}
}  // namespace detail

// --- Scrolling: the first row a column of `rows` shows so that `cursor` is on it, moving as little as it can ---
inline int ScrollFirst(int first,int cursor,int total,int rows) noexcept {
    if(rows<1)rows=1;
    if(cursor<first)first=cursor;
    if(cursor>=first+rows)first=cursor-rows+1;
    const int last=total>rows ? total-rows : 0;
    return first<0 ? 0 : first>last ? last : first;
}
inline int PageRows(const Menu& m) noexcept { return m.pageRows<1 ? 1 : m.pageRows>kPageMost ? kPageMost : m.pageRows; }
inline void Scroll(Menu& m) noexcept {
    m.unitFirst=ScrollFirst(m.unitFirst,m.unitAt,m.unitCount+1,PageRows(m));
    const int rows=m.unitAt<m.unitCount ? RowCount(m.unit[m.unitAt]) : 0;
    m.rowFirst=ScrollFirst(m.rowFirst,m.rowAt,rows,PageRows(m));
}

// --- The state machine ---
// Throws away every edit since the ini was loaded (or last saved) and closes.
inline void Discard(Menu& m) noexcept {
    for(int i=0;i<m.unitCount;++i){m.unit[i].squad=m.unit[i].squadLoaded;m.unit[i].vehicle=m.unit[i].vehicleLoaded;}
    m.open=false;m.level=Level::units;
}
inline Act ResetUnit(Menu& m,Unit& u) noexcept {
    u.squad=SupportPreset{};u.vehicle=VehicleLoadout{};
    m.rowAt=m.fieldAt=0;
    if(m.level==Level::fields)m.level=Level::rows;
    return Act::changed;
}
namespace detail {
inline Act StepUnits(Menu& m,const Keys& k) noexcept {
    const int n=m.unitCount+1;   // the save row last
    const bool onSave=m.unitAt>=m.unitCount;
    if(k.cancel){Discard(m);return Act::closed;}
    if(k.down){m.unitAt=Wrap(m.unitAt,1,n);return Act::moved;}
    if(k.up){m.unitAt=Wrap(m.unitAt,-1,n);return Act::moved;}
    if(onSave && k.confirm){if(Dirty(m))return Act::save;Discard(m);return Act::closed;}
    if(onSave)return Act::none;
    if(k.reset)return ResetUnit(m,m.unit[m.unitAt]);
    if(k.confirm || k.right){m.level=Level::rows;m.rowAt=m.rowFirst=0;return Act::moved;}
    return Act::none;
}
// Confirm on a row: a stock squad's "own load" row adds a soldier, a stock vehicle's row makes it explicit; rows with
// fields open them.
inline Act EnterRow(Menu& m,Unit& u) noexcept {
    if(IsSquad(u) && !u.squad.count)return AddSoldier(m,u);
    if(!IsSquad(u) && !u.vehicle.On()){Materialise(u);return Act::changed;}
    if(!FieldCount(u,m.rowAt))return Act::none;
    m.level=Level::fields;m.fieldAt=0;
    return Act::moved;
}
inline Act StepRows(Menu& m,const Keys& k) noexcept {
    Unit& u=m.unit[m.unitAt];
    if(k.cancel || k.left){m.level=Level::units;return Act::moved;}
    if(k.reset)return ResetUnit(m,u);
    if(k.add)return IsSquad(u) ? AddSoldier(m,u) : AddPylon(m,u);
    if(k.remove)return IsSquad(u) ? RemoveSoldier(m,u) : RemovePylon(m,u);
    if(k.down){m.rowAt=Wrap(m.rowAt,1,RowCount(u));return Act::moved;}
    if(k.up){m.rowAt=Wrap(m.rowAt,-1,RowCount(u));return Act::moved;}
    if(k.confirm || k.right)return EnterRow(m,u);
    return Act::none;
}
inline Act StepFields(Menu& m,const Keys& k) noexcept {
    Unit& u=m.unit[m.unitAt];
    const int n=FieldCount(u,m.rowAt);
    if(k.cancel || k.confirm || !n){m.level=Level::rows;return Act::moved;}
    if(k.reset)return ResetUnit(m,u);
    if(k.down){m.fieldAt=Wrap(m.fieldAt,1,n);return Act::moved;}
    if(k.up){m.fieldAt=Wrap(m.fieldAt,-1,n);return Act::moved;}
    if(k.right)return Change(m,u,1);
    if(k.left)return Change(m,u,-1);
    return Act::none;
}
inline void Clamp(Menu& m) noexcept {
    if(m.unitAt<0 || m.unitAt>m.unitCount)m.unitAt=0;
    if(m.unitAt==m.unitCount && m.level!=Level::units)m.level=Level::units;
}
}  // namespace detail
// One frame of the menu (`k`: this frame's presses, Presses). Shut, only the toggle does anything (the other keys are
// the room's); open, the toggle closes it discarding (as cancel on the unit list). One action a frame.
inline Act Step(Menu& m,const Keys& k) noexcept {
    if(k.toggle) {
        if(m.open){Discard(m);return Act::closed;}
        m.open=true;m.level=Level::units;
        return Act::opened;
    }
    if(!m.open)return Act::none;
    detail::Clamp(m);
    Act a=m.level==Level::units ? detail::StepUnits(m,k) : m.level==Level::rows ? detail::StepRows(m,k) : detail::StepFields(m,k);
    Scroll(m);
    return a;
}

// --- Saving ---
struct SaveLine { wchar_t key[64]; wchar_t value[kValueMost]; };
struct SaveSet { int count=0; SaveLine line[kMostUnits]; };
namespace detail {
// The unit's value, checked by its real parser to read back exactly the working copy.
inline bool SaveUnit(const Unit& u,SaveLine* out) noexcept {
    IniKey(u,out->key,64);
    wchar_t why[8];
    if(IsSquad(u)) {
        SerialisePreset(u.squad,out->value,kValueMost);
        SupportPreset back;
        return ParseSupportPreset(out->value,u.seats,&back,why,8) && SamePreset(back,u.squad);
    }
    SerialiseVehicle(u.vehicle,out->value,kValueMost);
    VehicleLoadout back;
    return ParseVehicleLoadout(out->value,u.body,&back,why,8) && SameVehicle(back,u.vehicle);
}
}  // namespace detail
// The key = value lines a save writes: only the units the player changed. False (and out empty) when any value would
// not read back as edited: nothing is written then.
inline bool CollectSave(const Menu& m,SaveSet* out) noexcept {
    if(!out)return false;
    out->count=0;
    for(int i=0;i<m.unitCount;++i) {
        if(!UnitChanged(m.unit[i]))continue;
        if(!detail::SaveUnit(m.unit[i],&out->line[out->count])){out->count=0;return false;}
        ++out->count;
    }
    return true;
}
// After the caller wrote the lines: what was edited is now what the ini holds; the menu closes.
inline void MarkSaved(Menu& m) noexcept {
    for(int i=0;i<m.unitCount;++i) {
        Unit& u=m.unit[i];
        if(UnitChanged(u))u.refused=false;
        u.squadLoaded=u.squad;u.vehicleLoaded=u.vehicle;
    }
    m.open=false;m.level=Level::units;
}

// --- The snapshot the drawing layer consumes ---
enum class RowText : std::uint8_t { squadUnit, vehicleUnit, save, ownLoad, soldier, stockVehicle, gunHE, gunAP, guns, pylon };
struct ViewRow {
    RowText text=RowText::save;
    const wchar_t* label=nullptr;   // the unit's, the soldier kind's, the store's label; nullptr none
    int index=0;                    // the unit / soldier / pylon number (0-based)
    int kind=-1;                    // SupportWeapon, -1 none
    int soldierClass=-1;            // SupportSoldierClass: 0 Ranger, 1 Wing Diver, 2 Fencer
    bool leader=false;
    std::uint32_t primary=0,secondary=0;   // RGBA, 0 stock
    int store=0;                    // kLoadStores code, 0 none
    int rounds=0;                   // the pylon's rounds
    bool cursor=false;              // the column's cursor is on it
    bool editing=false;             // its fields are open; `field` the one being changed
    int field=-1;
    bool changed=false;             // a unit edited since it was loaded
    bool refused=false;             // a unit whose ini value the parser refused
};
struct ViewColumn { int count=0,first=0,total=0; ViewRow row[kPageMost]; };
struct View { bool open=false,dirty=false; Level level=Level::units; ViewColumn units,rows; };
namespace detail {
inline ViewRow UnitRow(const Menu& m,int i) noexcept {
    ViewRow r;r.index=i;r.cursor=i==m.unitAt;
    if(i>=m.unitCount){r.text=RowText::save;return r;}
    const Unit& u=m.unit[i];
    r.text=IsSquad(u) ? RowText::squadUnit : RowText::vehicleUnit;r.label=u.label;
    r.changed=UnitChanged(u);r.refused=u.refused;
    return r;
}
inline ViewRow SquadRow(const Unit& u,int i) noexcept {
    ViewRow r;r.index=i;
    if(!u.squad.count){r.text=RowText::ownLoad;return r;}
    const SupportWeapon k=u.squad.kind[i];
    r.text=RowText::soldier;r.kind=static_cast<int>(k);r.label=kSupportKindNames[r.kind].label;
    r.soldierClass=SupportSoldierClass(k);r.leader=i%4==0;
    r.primary=Rgba(u.squad.look[i].primary);r.secondary=Rgba(u.squad.look[i].secondary);
    return r;
}
inline ViewRow VehicleRow(const Unit& u,int i) noexcept {
    ViewRow r;r.index=i;
    if(!u.vehicle.On()){r.text=RowText::stockVehicle;return r;}
    if(i==0){r.text=IsTank(u) ? (u.vehicle.apGun ? RowText::gunAP : RowText::gunHE) : RowText::guns;return r;}
    const LoadStore* s=LoadStoreOf(u.vehicle.store[i-1]);
    r.text=RowText::pylon;r.index=i-1;r.store=u.vehicle.store[i-1];r.label=s ? s->label : nullptr;
    r.rounds=kRoundCounts[u.vehicle.rounds[i-1]&0xF];
    return r;
}
template<class RowOf> void Fill(ViewColumn& c,int first,int total,int rows,RowOf rowOf) noexcept {
    c.first=first;c.total=total;c.count=0;
    for(int i=first;i<total && c.count<rows;++i)c.row[c.count++]=rowOf(i);
}
}  // namespace detail
inline View Snapshot(const Menu& m) noexcept {
    View v;v.open=m.open;v.dirty=Dirty(m);v.level=m.level;
    const int rows=PageRows(m);
    detail::Fill(v.units,m.unitFirst,m.unitCount+1,rows,[&](int i) noexcept {return detail::UnitRow(m,i);});
    if(m.unitAt>=m.unitCount)return v;
    const Unit& u=m.unit[m.unitAt];
    const bool inside=m.level!=Level::units;
    detail::Fill(v.rows,m.rowFirst,RowCount(u),rows,[&](int i) noexcept {
        ViewRow r=IsSquad(u) ? detail::SquadRow(u,i) : detail::VehicleRow(u,i);
        r.cursor=inside && i==m.rowAt;
        r.editing=r.cursor && m.level==Level::fields;r.field=r.editing ? m.fieldAt : -1;
        return r;
    });
    return v;
}
}  // namespace crew::roommenu
