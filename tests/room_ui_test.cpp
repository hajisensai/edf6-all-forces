// The room / HQ screen frame's pure side (src/room_ui_logic.h) offline: the layout marker's name, the native 整备
// button's click lifecycle, the keyboard / pad mapping (the toggle combination's buttons do nothing else while held,
// nothing while the game is behind), the input gate (taken while open, and after closing until every menu key is up),
// the covered-screen plan, the font colour, the button's texts in each language, the catalog's units (squads, vehicles,
// crews, the default and the mission crews), a crew's kind and the colours it wears in the preview, a vehicle's preview
// and the save's (field, unit, value) pairing including a vehicle and its crew written for one unit. Exit code 1 when one
// fails.
// cmake --build build --target room_ui_test && build\room_ui_test.exe
#include "../src/room_ui_logic.h"
#include <cstdio>
#include <cwchar>
#include <set>

namespace {
using namespace crew;
using namespace crew::roomui;
using roommenu::Keys;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
bool Same(const wchar_t* a,const wchar_t* b) { return a && b && !std::wcscmp(a,b); }
bool Has(const wchar_t* s,const wchar_t* part) { return s && part && std::wcsstr(s,part)!=nullptr; }

void Names() {
    Expect(Same(kRootName,L"vcRoot") && std::wcslen(kRootName)<=7,"the marker: tools/room_layout.py ROOT, fits the in-place wstring");
}

void NativeButton() {
    NativeEntry e;
    NativeEntryInput(e,true);
    Expect(!e.enabled && !NativeEntryClick(e,true),"an uncreated native button cannot open the menu");
    e.ready=true;
    NativeEntryInput(e,true);
    Expect(!NativeEntryClick(e,false) && !TakeNativeEntry(e,true),"another screen cannot request this screen's menu");
    Expect(NativeEntryClick(e,true) && !NativeEntryClick(e,true),"native Click queues at most one open during dispatch");
    Expect(TakeNativeEntry(e,true) && !TakeNativeEntry(e,true),"the UI consumes the native action exactly once after dispatch");
    Expect(NativeEntryClick(e,true),"next independent Click can queue");
    NativeEntryInput(e,false);
    Expect(!e.requested && !TakeNativeEntry(e,true) && !NativeEntryClick(e,true),"covered, disabled or closing input drops a pending action");
    NativeEntryInput(e,true);
    Expect(NativeEntryClick(e,true) && !TakeNativeEntry(e,false) && !TakeNativeEntry(e,true),"screen destruction/replacement discards an already queued action");
    e=NativeEntry{};
    Expect(!e.ready && !e.enabled && !e.attempted,"a new screen at a reused address has no inherited button state");
    roommenu::Menu m;
    Keys click;click.toggle=true;
    Expect(roommenu::Step(m,click)==roommenu::Act::opened && m.open && m.level==roommenu::Level::units,
           "native activation opens the same unit page as the hotkey");
    Keys heldConfirm;heldConfirm.confirm=true;
    const auto press=roommenu::Presses(heldConfirm,heldConfirm);
    Expect(roommenu::Step(m,press)==roommenu::Act::none && m.level==roommenu::Level::units,
           "the native activation's held confirm does not enter a loadout row");
}

void Input() {
    Raw r;r.foreground=true;
    Keys k=KeysDown(r,0x00C0);
    Expect(!AnyDown(k),"nothing held");
    r.up=true;r.enter=true;
    k=KeysDown(r,0x00C0);
    Expect(k.up && k.confirm && !k.down && !k.toggle,"arrow up + enter");
    r=Raw{};r.foreground=true;r.padOk=true;r.pad=kPadDown|kPadA|kPadX;
    k=KeysDown(r,0x00C0);
    Expect(k.down && k.confirm && k.add && !k.remove,"pad down / A / X");
    r.pad=kPadB|kPadY|kPadBack|kPadLeft|kPadRight;
    k=KeysDown(r,0x00C0);
    Expect(k.cancel && k.remove && k.reset && k.left && k.right,"pad B / Y / Back / left / right");
    // The combination: all of it held toggles, its buttons nothing else.
    r.pad=static_cast<std::uint16_t>(kPadBack|kPadY);
    k=KeysDown(r,static_cast<std::uint16_t>(kPadBack|kPadY));
    Expect(k.toggle && !k.reset && !k.remove,"combo toggles, its buttons nothing else");
    r.pad=kPadBack;
    k=KeysDown(r,static_cast<std::uint16_t>(kPadBack|kPadY));
    Expect(!k.toggle && k.reset,"half the combo: the button's own key");
    r.pad=static_cast<std::uint16_t>(0x00C0|kPadUp);
    k=KeysDown(r,0x00C0);
    Expect(k.toggle && k.up,"combo plus another button: both");
    r.padOk=false;
    k=KeysDown(r,0x00C0);
    Expect(!AnyDown(k),"no pad: its stale bits ignored");
    r=Raw{};r.toggleKey=true;r.escape=true;r.padOk=true;r.pad=0xFFFF;
    k=KeysDown(r,0x00C0);
    Expect(!AnyDown(k),"the game behind: nothing held");
    r.foreground=true;
    k=KeysDown(r,0);
    Expect(k.toggle && k.cancel,"toggle key; combo 0 never toggles by pad");
    r=Raw{};r.foreground=true;r.padOk=true;r.pad=0x00C0;
    Expect(!KeysDown(r,0).toggle,"combo 0: the pad never toggles");
    wchar_t name[32];
    PadComboName(0x00C0,name,32);Expect(Same(name,L"L3+R3"),"combo name L3+R3");
    PadComboName(static_cast<std::uint16_t>(kPadBack|kPadY),name,32);Expect(Same(name,L"Back+Y"),"combo name Back+Y");
    PadComboName(0,name,32);Expect(!name[0],"no combo: no name");
    Expect(PadToggleAvailable(true,0x00C0) && PadToggleAvailable(true,0x0180),"default and custom pad bindings available");
    Expect(!PadToggleAvailable(false,0x00C0) && !PadToggleAvailable(true,0) && !PadToggleAvailable(true,0x0400) &&
           !PadToggleAvailable(true,0x04C0),"missing pad, disabled and unreadable bits never advertised as usable");
    wchar_t allButtons[96];
    PadComboName(kPadKnownBits,allButtons,96);
    Expect(Has(allButtons,L"LB+RB+L3+R3+Back+Start+A+B+X+Y+Up+Down+Left+Right"),"even the full supported custom chord is named without truncation");
    PadComboName(0xFFFF,name,8);Expect(std::wcslen(name)<8,"a long combination cut to the buffer");
}

// The game's pad table as room_ui.cpp reads it: two slots, a channel object per button, +0x4C held, +0x4D suppressed.
void PadTable() {
    static unsigned char channels[2][0x100][0x50]{};
    const unsigned char* table[2][0x100]{};
    for(int s=0;s<2;++s)for(const PadChannel& c:kPadChannels)table[s][c.offset]=channels[s][c.offset];
    const auto read=[&](int slot,std::uint32_t offset) -> const unsigned char* { return table[slot][offset]; };
    Expect(PadBits(read)==0,"nothing held");
    channels[0][0x70][kChannelHeld]=1;
    Expect(PadBits(read)==kPadA,"slot 0 channel 0x70: A (XInput 0x1000)");
    channels[1][0x58][kChannelHeld]=1;
    Expect(PadBits(read)==(kPadA|kPadDown),"slot 1's down joins slot 0's A");
    channels[0][0x70][kChannelSuppressed]=1;
    Expect(PadBits(read)==kPadDown,"a suppressed channel (the game's own input off) is not held");
    channels[0][0xB0][kChannelHeld]=channels[0][0xB8][kChannelHeld]=1;
    Expect(PadBits(read)==(kPadDown|0x00C0),"L3 + R3: the default toggle combination");
    table[0][0x78]=nullptr;channels[0][0x78][kChannelHeld]=1;
    Expect(!(PadBits(read)&kPadB),"a channel not made: not held");
    std::set<std::uint32_t> offsets;std::uint16_t all=0;
    for(const PadChannel& c:kPadChannels){offsets.insert(c.offset);Expect(!(all&c.bit),"one channel per bit");all=static_cast<std::uint16_t>(all|c.bit);}
    Expect(offsets.size()==14 && all==0xF3FF,"14 channels: the d-pad, A B X Y, Back Start, LB RB, L3 R3");
}

void Colours() {
    float f[4];
    RgbaFloat(0xFF8000FFu,f);
    Expect(f[0]==1.0f && f[1]>0.50f && f[1]<0.51f && f[2]==0.0f && f[3]==1.0f,"font colour RGBA 0..1");
    RgbaFloat(kWhite,f);
    Expect(f[0]==1.0f && f[1]==1.0f && f[2]==1.0f && f[3]==1.0f,"the default white");
}

void GateAndPlan() {
    Gate g;
    Expect(!TakeInput(g,false,false),"shut, nothing held: the room reads");
    Expect(!TakeInput(g,false,true),"shut, a key held (never opened): the room reads it");
    Expect(TakeInput(g,true,true),"open: taken");
    Expect(TakeInput(g,true,false),"open, nothing held: taken");
    Expect(TakeInput(g,false,true),"closed this frame, the closing key still down: taken");
    Expect(TakeInput(g,false,true),"...still down: taken");
    Expect(!TakeInput(g,false,false),"released: the room reads again");
    Expect(!TakeInput(g,false,true),"a later press after release: the room's");
    Plan p=PlanFrame(true,true);
    Expect(p.step && p.show,"input, open: stepped and shown");
    p=PlanFrame(false,true);
    Expect(!p.step && !p.show,"covered: hidden and frozen");
    p=PlanFrame(true,false);
    Expect(p.step && !p.show,"shut: stepped (the toggle), nothing of ours drawn (the entry is the native button)");
    p=PlanFrame(false,false);
    Expect(!p.step && !p.show,"covered closed page: nothing stepped, nothing drawn");
    // A whole close sequence through the menu: Esc on the unit list closes, the room gets nothing while Esc is held.
    roommenu::Menu m;Gate gate;Keys held{};
    Raw r;r.foreground=true;r.toggleKey=true;
    Keys down=KeysDown(r,0);
    roommenu::Act a=roommenu::Step(m,roommenu::Presses(held,down));
    Expect(a==roommenu::Act::opened && TakeInput(gate,m.open,AnyDown(down)),"the toggle opens: taken the same frame");
    r=Raw{};r.foreground=true;r.escape=true;down=KeysDown(r,0);
    a=roommenu::Step(m,roommenu::Presses(held,down));
    Expect(a==roommenu::Act::closed && !m.open && TakeInput(gate,m.open,AnyDown(down)),"Esc closes: the room does not see it");
    a=roommenu::Step(m,roommenu::Presses(held,down));
    Expect(a==roommenu::Act::none && TakeInput(gate,m.open,AnyDown(down)),"Esc held: still not the room's");
    r.escape=false;down=KeysDown(r,0);
    roommenu::Step(m,roommenu::Presses(held,down));
    Expect(!TakeInput(gate,m.open,AnyDown(down)),"Esc up: the room reads again");
}

// The switches: off passes the screen through and never ticks the pre-sync; the pre-sync stops (withdrawn once) when a
// switch turns off, on the HQ, or when the screen goes.
void SwitchesPlan() {
    Switches s;
    SwitchPlan p=PlanSwitches(s,false,true,true);
    Expect(p.pass && !p.tick && !p.leave,"RoomLoadout / Enabled off from the start: passed through, no pre-sync, nothing to withdraw");
    p=PlanSwitches(s,true,true,false);
    Expect(!p.pass && !p.tick && !p.leave,"the HQ: the menu, never the pre-sync");
    p=PlanSwitches(s,true,false,true);
    Expect(!p.pass && !p.tick && !p.leave,"RoomPreSync=0 in the room: the menu without the pre-sync");
    p=PlanSwitches(s,true,true,true);
    Expect(!p.pass && p.tick && !p.leave,"both on in the room: the pre-sync ticks");
    p=PlanSwitches(s,true,true,true);
    Expect(p.tick && !p.leave,"...every frame");
    p=PlanSwitches(s,false,true,true);
    Expect(p.pass && !p.tick && p.leave,"RoomLoadout / Enabled turned off: passed through, the pre-sync withdrawn");
    p=PlanSwitches(s,false,true,true);
    Expect(p.pass && !p.tick && !p.leave,"...once");
    PlanSwitches(s,true,true,true);
    p=PlanSwitches(s,true,false,true);
    Expect(!p.pass && !p.tick && p.leave,"RoomPreSync turned off: withdrawn, the menu stays");
    p=PlanSwitches(s,true,false,true);
    Expect(!p.leave,"...once");
    PlanSwitches(s,true,true,true);
    Expect(LeaveOnForget(s) && !LeaveOnForget(s),"the screen gone while syncing: withdrawn once");
    Switches never;
    PlanSwitches(never,true,true,false);
    Expect(!LeaveOnForget(never),"the HQ screen gone: nothing was published, nothing withdrawn");
}

void Texts() {
    wchar_t out[kLineMost];
    for(hudtext::Lang lang:{hudtext::Lang::en,hudtext::Lang::zhCN,hudtext::Lang::zhTW,hudtext::Lang::ja}) {
        hudtext::Use(lang);
        EntryLine(L"F7 / LB+R3",out,kLineMost);
        Expect(Has(out,L"F7 / LB+R3"),"the native button's description uses the configured keys");
        EntryLine(L"",out,kLineMost);
        Expect(Has(out,L"-"),"no key configured: a dash");
        Expect(Same(Title(true),hudtext::Tr(Tx::roomTitleUnsaved)) && Same(Title(false),hudtext::Tr(Tx::roomTitle)),"title");
    }
    hudtext::Use(hudtext::Lang::zhCN);
    Expect(Same(Title(false),L"整备"),"the native button reads 整备");
    Expect(Same(UnitLabel(L"tank_crewed",L"x"),L"坦克·有人"),"unit label zh, any case");
    Expect(Same(UnitLabel(L"NEW_KEY",L"新单位"),L"新单位"),"unknown key: the catalog's name");
    hudtext::Use(hudtext::Lang::ja);
    Expect(Same(UnitLabel(L"OWN_MECH",L"x"),L"自分の要請・コンバットフレーム"),"the mech's label in the HUD's language");
}

// The catalog as support_dispatch.cpp lists it (keys, seats).
const wchar_t* kKeys[]={L"STRIKE",L"STRIKE_F",L"GUNSHIP",L"SQUAD",L"PLATOON",L"TANK_CREWED",L"TANK_DELIVERY",L"TRANSPORT_CREWED",
                        L"TRANSPORT_DELIVERY",L"RESCUE"};
const int kSeats[]={0,0,0,12,12,0,0,4,0,0};
const wchar_t* KeyAt(int i) noexcept { return i>=0 && i<10 ? kKeys[i] : nullptr; }
int SeatsAt(int i) noexcept { return i>=0 && i<10 ? kSeats[i] : 0; }
const wchar_t* NameAt(int) noexcept { return L"名字"; }

void Catalog() {
    hudtext::Use(hudtext::Lang::zhCN);
    roommenu::UnitSpec specs[roommenu::kMostUnits];
    const int n=CatalogUnits(10,KeyAt,SeatsAt,NameAt,specs,roommenu::kMostUnits);
    const int own=kPlayerVehicleCount;   // a player's own summons (support_loadout.h kPlayerVehicles), after the catalog's
    Expect(n==12+own,"every entry with seats, a vehicle or a crew (10), the player's own summons, then the default and the mission crews");
    for(int i=0;i<own;++i)
        Expect(n==12+own && Same(specs[10+i].key,kPlayerVehicles[i].key) && !specs[10+i].crew && specs[10+i].seats==0 &&
               VehicleBodyOfKey(specs[10+i].key)==kPlayerVehicles[i].body,"a player's own summon: a vehicle unit, no crew");
    Expect(Same(specs[10].label,L"我召唤的·玩家战斗机"),"labelled as the player's own");
    Expect(n==12+own && Same(specs[0].key,L"STRIKE") && specs[0].seats==0 && specs[0].crew && Same(specs[0].label,L"对地攻击机·守点"),"jet entry: a crew");
    Expect(n==12+own && Same(specs[2].key,L"GUNSHIP") && specs[2].crew && Same(specs[2].label,L"名字"),"gunship: a crew, the catalog's name");
    Expect(n==12+own && Same(specs[3].key,L"SQUAD") && specs[3].seats==12 && !specs[3].crew,"squad entry: no crew (HasCrew)");
    Expect(n==12+own && !specs[4].crew,"platoon: no crew");
    Expect(n==12+own && Same(specs[7].key,L"TRANSPORT_CREWED") && specs[7].seats==4 && specs[7].crew,"crewed APC: riders and a crew");
    Expect(n==12+own && Same(specs[10+own].key,L"") && specs[10+own].crew && Same(specs[10+own].label,hudtext::Tr(Tx::roomUnitCrewDefault)),"the default crew");
    Expect(n==12+own && Same(specs[11+own].key,kCrewMissionKey) && specs[11+own].crew && Same(specs[11+own].label,hudtext::Tr(Tx::roomUnitCrewMission)),"the mission crews");
    Expect(CatalogUnits(10,KeyAt,SeatsAt,NameAt,specs,3)==3,"capacity kept");
    roommenu::Menu m;
    roommenu::Load(m,specs,n,nullptr,nullptr);
    Expect(m.unitCount==13+own,"units: 2 jets, gunship crew, 2 squads, 2 tanks, APC riders + APC crew, 2 crews, the player's 6, default, mission");
    int crews=0,vehiclesWithCrew=0;
    for(int i=0;i<m.unitCount;++i){crews+=roommenu::IsCrewUnit(m.unit[i]);vehiclesWithCrew+=m.unit[i].kind==roommenu::UnitKind::vehicle && m.unit[i].hasCrew;}
    int playerVehicles=0;
    for(int i=0;i<m.unitCount;++i)playerVehicles+=roommenu::IsPlayerVehicleUnit(m.unit[i]) && !m.unit[i].hasCrew;
    Expect(crews==6 && vehiclesWithCrew==4 && playerVehicles==own,"6 crew units; every support vehicle has its crew row, the player's own none");
    // Without a live catalog: room_menu.h's list, labelled in the HUD's language.
    hudtext::Use(hudtext::Lang::en);
    const int d=DefaultUnits(specs,roommenu::kMostUnits);
    Expect(d==roommenu::kDefaultUnitCount && Same(specs[0].label,L"Infantry squad") && Same(specs[d-2].label,L"All support crews (default)") &&
           Same(specs[d-1].label,L"Mission vehicles' support crews") && specs[d-1].crew,"the default list relabelled");
    // The real catalog's size fits the menu (support_dispatch.cpp: 37 entries, 2 crewless squads, 10 vehicles).
    Expect(37+kPlayerVehicleCount+2<=roommenu::kMostUnits && 8+10+(37-2-10)+kPlayerVehicleCount+2<=roommenu::kMostUnits,
           "37 entries and the player's own: specs and units fit kMostUnits");
}

// A crew's kind from the crew settings; the preview of a crew (a member in the colours it wears: its own, else the
// default's; X stock) and of a vehicle (its body).
void Crews() {
    const CrewKinds kinds{SupportWeapon::sniper,SupportWeapon::shotgun};
    Expect(CrewKindOf(L"TANK_CREWED",kinds)==SupportWeapon::sniper && CrewKindOf(L"transport_delivery",kinds)==SupportWeapon::sniper,
           "a ground vehicle's crew: SupportVehicleCrewWeapon");
    Expect(CrewKindOf(L"STRIKE",kinds)==SupportWeapon::shotgun && CrewKindOf(L"GUNSHIP",kinds)==SupportWeapon::shotgun,
           "an aircraft's crew: SupportAircraftCrewWeapon");
    Expect(CrewKindOf(L"MISSION",kinds)==SupportWeapon::rifle,"the mission crews: riflemen");
    Expect(CrewKindOf(L"TANK_AIRDROP",kinds)==SupportWeapon::shotgun && CrewKindOf(L"TRANSPORT_AIRDROP",kinds)==SupportWeapon::shotgun &&
           CrewKindOf(L"truck_airdrop",kinds)==SupportWeapon::shotgun,
           "a container delivery's crew is the transport helicopter's pilot: SupportAircraftCrewWeapon, not the ground kind");
    Expect(CrewKindOf(L"",kinds)==SupportWeapon::sniper,"the default's row: the ground kind");
}

void Preview() {
    roommenu::Menu m;
    roommenu::UnitSpec crewed[]={{L"TANK_CREWED",L"t",0,true},{L"STRIKE",L"j",0,true},{L"",L"d",0,true},{kCrewMissionKey,L"m",0,true}};
    roommenu::Load(m,crewed,4,nullptr,nullptr);
    const CrewKinds kinds{SupportWeapon::flame,SupportWeapon::rocket};
    Expect(!DefaultCrew(m).set,"the default not set: stock");
    m.unit[2].crew=CrewColour{true,SupportLook{0xABCDEF,kStockColour}};
    Expect(DefaultCrew(m).set && DefaultCrew(m).look.primary==0xABCDEF,"the default: the \"\" crew unit's colours");
    roompreview::PreviewSpec p=CrewPreview(m,m.unit[0],kinds);
    Expect(p.kind==SupportWeapon::flame && !p.leader && p.look.primary==0xABCDEF,"the tank's crew: a flamethrower member in the default's colours");
    m.unit[0].crew=CrewColour{true,SupportLook{0x010203,0x040506}};
    p=CrewPreview(m,m.unit[0],kinds);
    Expect(p.look.primary==0x010203 && p.look.secondary==0x040506,"its own colours once set");
    m.unit[0].crew=CrewColour{true,SupportLook{}};
    Expect(!CrewPreview(m,m.unit[0],kinds).look.On(),"X: stock, not the default");
    Expect(CrewPreview(m,m.unit[1],kinds).kind==SupportWeapon::rocket,"the jet's crew: the aircraft kind");
    p=CrewPreview(m,m.unit[2],kinds);
    Expect(p.kind==SupportWeapon::flame && p.look.primary==0xABCDEF,"the default's own row: its own colours");
    p=CrewPreview(m,m.unit[3],kinds);
    Expect(p.kind==SupportWeapon::rifle && !p.leader && p.look.primary==0xABCDEF,"the mission crews: a rifle member");
    roommenu::UnitSpec vehicles[]={{L"TANK_CREWED",L"t",0},{L"STRIKE",L"j",0},{L"OWN_MECH",L"m",0}};
    roommenu::Load(m,vehicles,3,nullptr,nullptr);
    Expect(VehiclePreview(m.unit[0]).body==VehicleBody::tank && VehiclePreview(m.unit[1]).body==VehicleBody::strike &&
           VehiclePreview(m.unit[2]).body==VehicleBody::mech,"a vehicle's preview: its body");
}

void Save() {
    roommenu::Menu m;
    roommenu::Load(m,roommenu::kDefaultUnits,roommenu::kDefaultUnitCount,nullptr,nullptr);
    m.unit[0].squad.count=2;m.unit[0].squad.kind[1]=SupportWeapon::sniper;
    int tank=-1;
    for(int i=0;i<m.unitCount;++i)if(!roommenu::IsSquad(m.unit[i]) && Same(m.unit[i].key,L"TANK_CREWED"))tank=i;
    Expect(tank>=0,"tank unit");
    m.unit[tank].vehicle.body=VehicleBody::tank;m.unit[tank].vehicle.apGun=true;
    roommenu::SaveSet set;
    Expect(roommenu::CollectSave(m,&set) && set.count==2,"two changed");
    SaveItem items[roommenu::kMostUnits];
    const int n=SaveItems(m,set,items,roommenu::kMostUnits);
    Expect(n==2,"paired");
    Expect(n==2 && items[0].field==LoadoutField::preset && Same(items[0].unit,L"SQUAD") && Same(items[0].value,L"rifle,sniper"),"squad item");
    Expect(n==2 && items[1].field==LoadoutField::vehicle && Same(items[1].unit,L"TANK_CREWED") && Same(items[1].value,L"AP"),"tank item");
    set.count=1;
    Expect(SaveItems(m,set,items,roommenu::kMostUnits)==-1,"a short set does not pair");
    roommenu::CollectSave(m,&set);
    wcscpy_s(set.line[0].key,L"SupportPreset_PLATOON");
    Expect(SaveItems(m,set,items,roommenu::kMostUnits)==-1,"another unit's key does not pair");

    // One unit, two lines: the tank's loadout and its crew's colours; then a crew alone, the default and the mission crews.
    roommenu::Load(m,roommenu::kDefaultUnits,roommenu::kDefaultUnitCount,nullptr,nullptr);
    int def=-1,mission=-1,heliCrew=-1;
    for(int i=0;i<m.unitCount;++i) {
        if(Same(m.unit[i].key,L"TANK_CREWED"))tank=i;
        if(Same(m.unit[i].key,L""))def=i;
        if(Same(m.unit[i].key,kCrewMissionKey))mission=i;
        if(Same(m.unit[i].key,L"SQUAD_HELI") && roommenu::IsCrewUnit(m.unit[i]))heliCrew=i;
    }
    Expect(def>=0 && mission>=0 && heliCrew>=0,"the crew units");
    m.unit[tank].vehicle.body=VehicleBody::tank;m.unit[tank].vehicle.apGun=true;
    m.unit[tank].crew=CrewColour{true,SupportLook{0x1E3A8A,0xFFFFFF}};
    m.unit[heliCrew].crew=CrewColour{true,SupportLook{}};
    m.unit[def].crew=CrewColour{true,SupportLook{0xC0272D,kStockColour}};
    m.unit[mission].crew=CrewColour{true,SupportLook{kStockColour,0x101010}};
    Expect(roommenu::CollectSave(m,&set) && set.count==5,"five lines: the heli crew, the tank twice, the default, the mission crews");
    SaveItem many[2*roommenu::kMostUnits];
    const int k=SaveItems(m,set,many,2*roommenu::kMostUnits);
    Expect(k==5,"paired");
    Expect(k==5 && many[0].field==LoadoutField::crew && Same(many[0].unit,L"SQUAD_HELI") && Same(many[0].value,L"X") &&
           many[0].unitIndex==heliCrew,"the heli crew (units in menu order): X");
    Expect(k==5 && many[1].field==LoadoutField::vehicle && Same(many[1].unit,L"TANK_CREWED") && Same(many[1].value,L"AP") &&
           many[1].unitIndex==tank,"the tank's loadout");
    Expect(k==5 && many[2].field==LoadoutField::crew && Same(many[2].unit,L"TANK_CREWED") && Same(many[2].value,L"1E3A8A:FFFFFF") &&
           many[2].unitIndex==tank,"then the same unit's crew");
    Expect(k==5 && many[3].field==LoadoutField::crew && Same(many[3].unit,L"") && Same(many[3].value,L"C0272D") &&
           LoadoutKey(many[3].field,many[3].unit)==L"SupportCrewColour","the default: SupportCrewColour");
    Expect(k==5 && Same(many[4].unit,kCrewMissionKey) && Same(many[4].value,L"X:101010") &&
           LoadoutKey(many[4].field,many[4].unit)==L"SupportCrewColour_MISSION","the mission crews");
    // Every item's value is one SaveSupportLoadout accepts (loadout_ini.h, the writer's own check).
    for(int i=0;i<k;++i){wchar_t why[80];Expect(ValidLoadoutValue(many[i].field,many[i].unit,12,many[i].value,why,80),"each value valid for its field");}
    // Written one by one (room_ui.cpp Save): after the first line only the tank's loadout is saved, its crew still edited.
    MarkItemSaved(m,many[0]);
    Expect(!roommenu::UnitChanged(m.unit[heliCrew]) && roommenu::UnitChanged(m.unit[tank]),"the heli crew written, the tank not yet");
    MarkItemSaved(m,many[1]);
    Expect(!roommenu::PartChanged(m.unit[tank]) && roommenu::CrewChanged(m.unit[tank]),"the loadout written, the crew still to go");
    MarkItemSaved(m,many[2]);
    Expect(!roommenu::UnitChanged(m.unit[tank]),"both written: the tank unchanged");
    for(int i=3;i<k;++i)MarkItemSaved(m,many[i]);
    Expect(!roommenu::Dirty(m),"all written: nothing left to save");
    // A crew alone (its vehicle unchanged) is one crew line; the crew's key for its unit only.
    m.unit[tank].crew=CrewColour{};
    Expect(roommenu::CollectSave(m,&set) && set.count==1 && SaveItems(m,set,many,2*roommenu::kMostUnits)==1 &&
           many[0].field==LoadoutField::crew && Same(many[0].value,L""),"a crew put back to the default: one empty crew line");
    wcscpy_s(set.line[0].key,L"SupportVehicle_TANK_CREWED");
    Expect(SaveItems(m,set,many,2*roommenu::kMostUnits)==-1,"a crew line under the vehicle's key does not pair");
}
}  // namespace

// The HQ's mission select opened by itself: once per HQ object, only with the HQ the input owner and the F2 menu down.
void AutoOpen() {
    Expect(AutoOpenDue(true,false,true,false),"a fresh HQ the input owner, the menu down: opened");
    Expect(!AutoOpenDue(false,false,true,false),"HqAutoMissionSelect / Enabled off: never");
    Expect(!AutoOpenDue(true,true,true,false),"this HQ object tried already (opened, or a step failed): never again");
    Expect(!AutoOpenDue(true,false,false,false),"something of the game's over the HQ (opening, notice, sub-screen, transition): waited");
    Expect(!AutoOpenDue(true,false,true,true),"the F2 menu up or its key still held: waited");
    Expect(!AutoOpenDue(true,false,false,true) && !AutoOpenDue(false,true,false,true),"nothing due: never");
}

int main() {
    Names();
    NativeButton();
    Input();
    PadTable();
    Colours();
    GateAndPlan();
    SwitchesPlan();
    AutoOpen();
    Texts();
    Catalog();
    Crews();
    Preview();
    Save();
    std::printf("room_ui: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
