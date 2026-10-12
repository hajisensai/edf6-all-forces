// The room / HQ loadout screen's pure side (src/room_ui_logic.h) offline: the element names (at most 7 characters, unique,
// the layout generator's spelling), the keyboard / pad mapping (the toggle combination's buttons do nothing else while
// held, nothing while the game is behind), the input gate (taken while open, and after closing until every menu key is up),
// the covered-screen plan, the rows' looks (chips only for a set colour, icons per kind / store / gun), the lines in each
// language, the catalog's units (squads, vehicles, crews, the default and the mission crews), the crew rows (kind, the
// default's colours worn, lines, chips, icon), the preview's soldier or crew member and the save's (field, unit, value)
// pairing including a vehicle and its crew written for one unit. Exit code 1 when one fails.
// cmake --build build --target room_ui_test && build\room_ui_test.exe
#include "../src/room_ui_logic.h"
#include <cstdio>
#include <cwchar>
#include <set>
#include <string>

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
    std::set<std::wstring> seen;
    wchar_t n[kNameMost];
    const auto add=[&](const wchar_t* s) {
        Expect(std::wcslen(s)<=7,"a name fits the game's in-place wstring (7 characters)");
        Expect(seen.insert(s).second,"every name unique");
        Expect(s[0]==L'v' && s[1]==L'c',"every name starts vc");
    };
    for(const wchar_t* s:{kRootName,kBackName,kHeadName,kHintName})add(s);
    for(int i=0;i<kRows;++i) {
        Expect(UnitName(i,n),"unit name");add(n);
        for(Part p:{Part::box,Part::text,Part::primary,Part::secondary,Part::icon,Part::note}){Expect(RowName(i,p,n),"row name");add(n);}
    }
    for(int i=0;i<kStatusLines;++i){Expect(StatusName(i,n),"status name");add(n);}
    Expect(UnitName(3,n) && Same(n,L"vcU03"),"vcU03");
    Expect(RowName(12,Part::secondary,n) && Same(n,L"vcR12B"),"vcR12B");
    Expect(RowName(0,Part::box,n) && Same(n,L"vcR00"),"vcR00");
    Expect(StatusName(2,n) && Same(n,L"vcS2"),"vcS2");
    Expect(!UnitName(kRows,n) && !RowName(kRows,Part::text,n) && !StatusName(kStatusLines,n),"no name past the layout");
    Expect(seen.size()==4u+static_cast<std::size_t>(kRows)*7u+kStatusLines,"element count");
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
    ViewRow r;
    Expect(RowColour(nullptr)==kWhite && RowColour(&r)==kWhite,"plain row white");
    r.cursor=true;Expect(RowColour(&r)==kCursorColour,"cursor row highlighted");
    r.cursor=false;r.refused=true;Expect(RowColour(&r)==kRefusedColour,"refused unit marked");
    r.changed=true;Expect(RowColour(&r)==kWhite,"refused but changed: the edit is what counts");
    Expect(StatusColour(true)==kWarnColour && StatusColour(false)==kWhite,"status colours");
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
    Expect(p.step && !p.show,"shut: stepped (the toggle), not shown");
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

void Looks() {
    Expect(!LookOf(nullptr).shown,"no row: hidden");
    ViewRow r;r.text=RowText::soldier;r.kind=static_cast<int>(SupportWeapon::wingLaser);r.primary=0xC0272DFFu;r.secondary=0;
    Look l=LookOf(&r);
    Expect(l.shown && l.primary && !l.secondary && l.primaryRgba==0xC0272DFFu,"a soldier's set colour: its chip; stock: none");
    Expect(Same(l.icon,L"app:/weapon/icon/LAZR.dds"),"wing laser icon");
    r.kind=0;Expect(Same(LookOf(&r).icon,L"app:/weapon/icon/AF14.dds"),"rifle icon");
    r.kind=static_cast<int>(SupportWeapon::fencerPileBanker);Expect(Same(LookOf(&r).icon,L"app:/weapon/icon/pilebunker.dds"),"pile banker icon");
    r=ViewRow{};r.text=RowText::pylon;r.store=7;
    l=LookOf(&r);
    Expect(!l.primary && !l.secondary && Same(l.icon,L"app:/weapon/icon/hornet01.dds"),"RKT pylon: hornet icon, no chips");
    r.store=0;Expect(LookOf(&r).icon==nullptr,"unknown store: no icon");
    r=ViewRow{};r.text=RowText::gunAP;Expect(Same(LookOf(&r).icon,L"app:/weapon/icon/h_cannon_apfsds01.dds"),"AP gun icon");
    r.text=RowText::gunHE;Expect(Same(LookOf(&r).icon,L"app:/weapon/icon/tank01.dds"),"HE gun icon");
    r.text=RowText::guns;Expect(LookOf(&r).icon==nullptr,"guns: no icon");
    r.text=RowText::ownLoad;l=LookOf(&r);Expect(l.shown && !l.icon && !l.primary,"own load: text only");
    for(int k=0;k<kSupportWeaponCount;++k)Expect(KindIcon(k)!=nullptr,"every kind has an icon");
    for(int c=1;c<=kLoadStoreCount;++c)Expect(StoreIcon(c)!=nullptr,"every store has an icon");
    Expect(KindIcon(-1)==nullptr && KindIcon(kSupportWeaponCount)==nullptr && StoreIcon(kLoadStoreCount+1)==nullptr,"out of range");
    float f[4];
    ChipColour(0xFF8000FFu,f);
    Expect(f[0]==1.0f && f[1]>0.50f && f[1]<0.51f && f[2]==0.0f && f[3]==1.0f,"chip colour RGBA 0..1");
}

void Texts() {
    wchar_t out[kLineMost];
    for(hudtext::Lang lang:{hudtext::Lang::en,hudtext::Lang::zhCN,hudtext::Lang::zhTW,hudtext::Lang::ja}) {
        hudtext::Use(lang);
        ViewRow u;u.text=RowText::squadUnit;u.label=UnitLabel(L"SQUAD",L"x");u.cursor=true;
        UnitLine(u,out,kLineMost);
        Expect(Has(out,kCursor) && Has(out,hudtext::Tr(Tx::roomUnitSquad)),"unit line: cursor and label");
        u.changed=true;UnitLine(u,out,kLineMost);
        Expect(std::wcslen(out)>std::wcslen(hudtext::Tr(Tx::roomUnitSquad))+2,"edited mark");
        u.text=RowText::save;u.cursor=false;UnitLine(u,out,kLineMost);
        Expect(Has(out,hudtext::Tr(Tx::roomSave)) && !Has(out,kCursor),"save row");
        ViewRow r;r.text=RowText::soldier;r.kind=static_cast<int>(SupportWeapon::fencerShotgun);r.index=4;r.leader=true;
        RowLine(r,out,kLineMost);
        Expect(Has(out,L"5. ") && Has(out,KindLabel(r.kind)),"soldier line: number and kind");
        NoteLine(r,out,kLineMost);
        Expect(Same(out,hudtext::Tr(Tx::roomStockColour)),"stock colours noted");
        r.editing=true;r.field=1;NoteLine(r,out,kLineMost);
        Expect(Same(out,hudtext::Tr(Tx::roomFieldPrimary)),"editing colour 1");
        r=ViewRow{};r.text=RowText::pylon;r.index=1;r.store=2;r.rounds=4;
        RowLine(r,out,kLineMost);
        Expect(Has(out,L"2. ") && Has(out,L"AIM-120"),"pylon line");
        NoteLine(r,out,kLineMost);Expect(Same(out,L"x4"),"rounds note");
        r.editing=true;r.field=1;NoteLine(r,out,kLineMost);Expect(Has(out,L"x4") && Has(out,L"<"),"editing rounds");
        HintLine(L"F2",out,kLineMost);Expect(Has(out,L"F2"),"hint names the toggle");
        GenerateStatus s;s.state=GenerateState::failed;wcscpy_s(s.message,L"boom");
        Expect(GenerateLine(s,out,kLineMost) && Has(out,L"boom"),"generator failure with its message");
        s.state=GenerateState::idle;Expect(!GenerateLine(s,out,kLineMost) && !out[0],"idle: no line");
        Expect(Same(Title(true),hudtext::Tr(Tx::roomTitleUnsaved)) && Same(Title(false),hudtext::Tr(Tx::roomTitle)),"title");
    }
    hudtext::Use(hudtext::Lang::zhCN);
    Expect(Same(UnitLabel(L"tank_crewed",L"x"),L"坦克·有人"),"unit label zh, any case");
    Expect(Same(UnitLabel(L"NEW_KEY",L"新单位"),L"新单位"),"unknown key: the catalog's name");
    Expect(Same(KindLabel(0),L"游骑兵·步枪") && Same(StoreLabel(10),L"LAHAT 炮射导弹"),"zh labels");
    hudtext::Use(hudtext::Lang::en);
    Expect(Same(KindLabel(13),L"Fencer, shotgun") && Same(StoreLabel(1),L"AIM-9X short-range AAM"),"en labels");
    // Status order and the four-line cap.
    roomsync::SyncLine sync[3]{};
    wcscpy_s(sync[0].text,L"A");wcscpy_s(sync[1].text,L"B");sync[1].warn=true;wcscpy_s(sync[2].text,L"C");
    GenerateStatus g;g.state=GenerateState::running;
    Status st=ComposeStatus(true,L"saved",false,&g,sync,3);
    Expect(st.count==kStatusLines && Same(st.text[0],hudtext::Tr(Tx::roomClientNote)) && Same(st.text[1],L"saved") &&
           Same(st.text[2],hudtext::Tr(Tx::roomGenRunning)) && Same(st.text[3],L"A"),"status order, capped");
    st=ComposeStatus(false,nullptr,false,nullptr,sync,3);
    Expect(st.count==3 && Same(st.text[1],L"B") && st.warn[1] && !st.warn[0],"sync lines alone, warn kept");
    g.state=GenerateState::missing;
    st=ComposeStatus(false,L"",false,&g,nullptr,0);
    Expect(st.count==1 && st.warn[0],"missing generator warns; an empty save note is no line");
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
    Expect(n==12,"every entry with seats, a vehicle or a crew (10), then the default and the mission crews");
    Expect(n==12 && Same(specs[0].key,L"STRIKE") && specs[0].seats==0 && specs[0].crew && Same(specs[0].label,L"对地攻击机·守点"),"jet entry: a crew");
    Expect(n==12 && Same(specs[2].key,L"GUNSHIP") && specs[2].crew && Same(specs[2].label,L"名字"),"gunship: a crew, the catalog's name");
    Expect(n==12 && Same(specs[3].key,L"SQUAD") && specs[3].seats==12 && !specs[3].crew,"squad entry: no crew (HasCrew)");
    Expect(n==12 && !specs[4].crew,"platoon: no crew");
    Expect(n==12 && Same(specs[7].key,L"TRANSPORT_CREWED") && specs[7].seats==4 && specs[7].crew,"crewed APC: riders and a crew");
    Expect(n==12 && Same(specs[10].key,L"") && specs[10].crew && Same(specs[10].label,hudtext::Tr(Tx::roomUnitCrewDefault)),"the default crew");
    Expect(n==12 && Same(specs[11].key,kCrewMissionKey) && specs[11].crew && Same(specs[11].label,hudtext::Tr(Tx::roomUnitCrewMission)),"the mission crews");
    Expect(CatalogUnits(10,KeyAt,SeatsAt,NameAt,specs,3)==3,"capacity kept");
    roommenu::Menu m;
    roommenu::Load(m,specs,n,nullptr,nullptr);
    Expect(m.unitCount==13,"units: 2 jets, gunship crew, 2 squads, 2 tanks, APC riders + APC crew, 2 crews, default, mission");
    int crews=0,vehiclesWithCrew=0;
    for(int i=0;i<m.unitCount;++i){crews+=roommenu::IsCrewUnit(m.unit[i]);vehiclesWithCrew+=m.unit[i].kind==roommenu::UnitKind::vehicle && m.unit[i].hasCrew;}
    Expect(crews==6 && vehiclesWithCrew==4,"6 crew units; every vehicle has its crew row");
    // Without a live catalog: room_menu.h's list, labelled in the HUD's language.
    hudtext::Use(hudtext::Lang::en);
    const int d=DefaultUnits(specs,roommenu::kMostUnits);
    Expect(d==roommenu::kDefaultUnitCount && Same(specs[0].label,L"Infantry squad") && Same(specs[d-2].label,L"All support crews (default)") &&
           Same(specs[d-1].label,L"Mission vehicles' support crews") && specs[d-1].crew,"the default list relabelled");
    // The real catalog's size fits the menu (support_dispatch.cpp: 37 entries, 2 crewless squads, 10 vehicles).
    Expect(37+2<=roommenu::kMostUnits && 8+10+(37-2-10)+2<=roommenu::kMostUnits,"37 entries: specs and units fit kMostUnits");
}

// A crew row: its kind from the crew settings, the default's colours shown when inheriting, its lines, chips and icon.
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
    roommenu::Menu m;
    roommenu::Load(m,roommenu::kDefaultUnits,roommenu::kDefaultUnitCount,nullptr,nullptr);
    int tank=-1,def=-1,mission=-1;
    for(int i=0;i<m.unitCount;++i) {
        if(Same(m.unit[i].key,L"TANK_CREWED"))tank=i;
        if(Same(m.unit[i].key,L""))def=i;
        if(Same(m.unit[i].key,kCrewMissionKey))mission=i;
    }
    Expect(tank>=0 && def>=0 && mission>=0,"tank, default, mission units");
    m.unit[def].crew=CrewColour{true,SupportLook{0x112233,0x445566}};
    m.open=true;m.unitAt=tank;m.level=roommenu::Level::rows;m.rowAt=roommenu::CrewRow(m.unit[tank]);
    roommenu::View v=roommenu::Snapshot(m);
    DressCrewRows(v,m,kinds);
    const ViewRow* r=nullptr;
    for(int i=0;i<v.rows.count;++i)if(v.rows.row[i].text==RowText::crew)r=&v.rows.row[i];
    Expect(r && r->inherit && r->kind==static_cast<int>(SupportWeapon::sniper) && r->soldierClass==0,"the tank's crew row: inherits, a sniper");
    Expect(r && r->primary==0x112233FFu && r->secondary==0x445566FFu,"inheriting: the default's colours shown");
    Look l=LookOf(r);
    Expect(l.primary && l.secondary && l.primaryRgba==0x112233FFu && Same(l.icon,L"app:/weapon/icon/srMMF.dds"),"crew chips and the kind's icon");
    wchar_t out[kLineMost];
    for(hudtext::Lang lang:{hudtext::Lang::en,hudtext::Lang::zhCN,hudtext::Lang::zhTW,hudtext::Lang::ja}) {
        hudtext::Use(lang);
        if(!r)break;
        RowLine(*r,out,kLineMost);
        Expect(Has(out,KindLabel(r->kind)) && std::wcslen(out)>std::wcslen(KindLabel(r->kind))+4,"crew line: names the kind");
        NoteLine(*r,out,kLineMost);
        Expect(Same(out,hudtext::Tr(Tx::roomCrewDefault)),"inheriting: the note says the default");
        ViewRow u;u.text=RowText::crewUnit;u.key=L"GUNSHIP";u.label=L"Gunship";
        UnitLine(u,out,kLineMost);
        wchar_t want[kLineMost];_snwprintf_s(want,_TRUNCATE,hudtext::Tr(Tx::roomUnitCrewOf),L"Gunship");
        Expect(Has(out,want),"a catalog entry's crew unit: \"<label>: crew\"");
        u.key=L"";u.label=hudtext::Tr(Tx::roomUnitCrewDefault);UnitLine(u,out,kLineMost);
        Expect(Has(out,hudtext::Tr(Tx::roomUnitCrewDefault)) && !Has(out,L"Gunship"),"the default's unit: its own label");
        u.key=kCrewMissionKey;u.label=hudtext::Tr(Tx::roomUnitCrewMission);u.changed=true;UnitLine(u,out,kLineMost);
        Expect(Has(out,hudtext::Tr(Tx::roomUnitCrewMission)),"the mission crews' unit, edited");
    }
    // Set and both stock (X): no chips, stock noted; editing names the colour field.
    m.unit[tank].crew=CrewColour{true,SupportLook{}};
    v=roommenu::Snapshot(m);DressCrewRows(v,m,kinds);
    r=nullptr;
    for(int i=0;i<v.rows.count;++i)if(v.rows.row[i].text==RowText::crew)r=&v.rows.row[i];
    l=LookOf(r);
    Expect(r && !r->inherit && !l.primary && !l.secondary && l.icon,"X: no chips, the icon stays");
    hudtext::Use(hudtext::Lang::en);
    if(r){NoteLine(*r,out,kLineMost);Expect(Same(out,hudtext::Tr(Tx::roomStockColour)),"X: stock noted");}
    m.level=roommenu::Level::fields;m.fieldAt=1;
    v=roommenu::Snapshot(m);DressCrewRows(v,m,kinds);
    for(int i=0;i<v.rows.count;++i)if(v.rows.row[i].text==RowText::crew){NoteLine(v.rows.row[i],out,kLineMost);Expect(Same(out,hudtext::Tr(Tx::roomFieldSecondary)),"editing the crew's colour 2");}
    // The default's own row: not set is stock, not "as the default".
    m.unitAt=def;m.level=roommenu::Level::rows;m.rowAt=0;m.unit[def].crew=CrewColour{};
    v=roommenu::Snapshot(m);DressCrewRows(v,m,kinds);
    Expect(v.rows.count==1 && v.rows.row[0].text==RowText::crew && v.rows.row[0].inherit && !v.rows.row[0].primary,"the default's row: stock");
    NoteLine(v.rows.row[0],out,kLineMost);
    Expect(Same(out,hudtext::Tr(Tx::roomStockColour)),"the default's row noted stock");
}

void Preview() {
    roommenu::Menu m;
    roommenu::UnitSpec specs[]={{L"SQUAD",L"s",12},{L"TANK_CREWED",L"t",0}};
    roommenu::Load(m,specs,2,nullptr,nullptr);
    roompreview::PreviewSpec p;
    Expect(!PreviewFor(m,&p),"shut: none");
    m.open=true;
    Expect(!PreviewFor(m,&p),"the unit list: none");
    m.level=roommenu::Level::rows;
    Expect(!PreviewFor(m,&p),"a stock squad (own load row): none");
    m.unit[0].squad.count=5;m.unit[0].squad.kind[4]=SupportWeapon::wingIzuna;m.unit[0].squad.look[4]=SupportLook{0x123456,kStockColour};
    m.rowAt=4;
    Expect(PreviewFor(m,&p) && p.kind==SupportWeapon::wingIzuna && p.leader && p.look.primary==0x123456,"the cursor's soldier (4: a leader)");
    m.rowAt=1;
    Expect(PreviewFor(m,&p) && !p.leader,"soldier 1: not a leader");
    roompreview::PreviewSpec q=p;
    Expect(SamePreview(p,q),"same spec");
    q.look.secondary=0xFFFFFF;Expect(!SamePreview(p,q),"another colour");
    m.unitAt=1;m.rowAt=0;
    Expect(!PreviewFor(m,&p),"a vehicle's gun row: none");
    // A crew row: the crew's kind, a member, in the colours it wears (its own, else the default's).
    roommenu::UnitSpec crewed[]={{L"TANK_CREWED",L"t",0,true},{L"STRIKE",L"j",0,true},{L"",L"d",0,true},{kCrewMissionKey,L"m",0,true}};
    roommenu::Load(m,crewed,4,nullptr,nullptr);
    m.open=true;m.level=roommenu::Level::rows;
    const CrewKinds kinds{SupportWeapon::flame,SupportWeapon::rocket};
    m.unit[2].crew=CrewColour{true,SupportLook{0xABCDEF,kStockColour}};
    m.unitAt=0;m.rowAt=roommenu::CrewRow(m.unit[0]);
    Expect(m.rowAt==1 && PreviewFor(m,&p,kinds) && p.kind==SupportWeapon::flame && !p.leader && p.look.primary==0xABCDEF,
           "the tank's crew: a flamethrower member in the default's colours");
    m.unit[0].crew=CrewColour{true,SupportLook{0x010203,0x040506}};
    Expect(PreviewFor(m,&p,kinds) && p.look.primary==0x010203 && p.look.secondary==0x040506,"its own colours once set");
    m.unit[0].crew=CrewColour{true,SupportLook{}};
    Expect(PreviewFor(m,&p,kinds) && !p.look.On(),"X: stock, not the default");
    m.unitAt=1;m.rowAt=roommenu::CrewRow(m.unit[1]);
    Expect(PreviewFor(m,&p,kinds) && p.kind==SupportWeapon::rocket,"the jet's crew: the aircraft kind");
    m.unitAt=1;m.rowAt=0;
    Expect(!PreviewFor(m,&p,kinds),"the jet's guns row: none");
    m.unitAt=2;m.rowAt=0;
    Expect(PreviewFor(m,&p,kinds) && p.kind==SupportWeapon::flame && p.look.primary==0xABCDEF,"the default's own row");
    m.unitAt=3;m.rowAt=0;
    Expect(PreviewFor(m,&p,kinds) && p.kind==SupportWeapon::rifle && !p.leader && p.look.primary==0xABCDEF,"the mission crews: a rifle member");
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

int main() {
    Names();
    Input();
    PadTable();
    Colours();
    GateAndPlan();
    SwitchesPlan();
    Looks();
    Texts();
    Catalog();
    Crews();
    Preview();
    Save();
    std::printf("room_ui: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
