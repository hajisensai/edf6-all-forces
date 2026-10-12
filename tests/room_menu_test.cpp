// The room / HQ support loadout menu's pure logic (src/room_menu.h) offline: every grammar example of
// docs/feature-2026-10-09-loadout-editor.md survives serialise(parse(s)) (the canonical ones byte for byte), the menu
// loads the ini's values (a refused one shown stock and left alone), goes round as it says (shut it reads only its toggle,
// one press one step, wrap both ways, in and out of the levels), keeps every limit (a squad's seats, a tank's 4 and a
// jet's 5 pylons, the stores each body may carry), resets to stock, discards on cancel / toggle, and a save writes only
// what changed, each value read back by the real parsers (also after thousands of random key presses). The snapshot
// shows leaders, colours, stores, the cursor and a scroll window. Exit code 1 when one fails.
// cmake --build build --target room_menu_test && build\room_menu_test.exe
#include "../src/room_menu.h"
#include <cstdio>
#include <cwchar>

namespace {
using namespace crew;
using namespace crew::roommenu;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

// A fake ini: key = value pairs.
struct Ini { const wchar_t* key[32]; const wchar_t* value[32]; int count=0; };
void Set(Ini& ini,const wchar_t* key,const wchar_t* value) {
    for(int i=0;i<ini.count;++i)if(!std::wcscmp(ini.key[i],key)){ini.value[i]=value;return;}
    ini.key[ini.count]=key;ini.value[ini.count]=value;++ini.count;
}
std::size_t Read(void* context,const wchar_t* key,wchar_t* out,std::size_t capacity) noexcept {
    const auto* ini=static_cast<const Ini*>(context);
    out[0]=0;
    for(int i=0;i<ini->count;++i)
        if(!std::wcscmp(ini->key[i],key)){wcsncpy_s(out,capacity,ini->value[i],_TRUNCATE);return std::wcslen(out);}
    return 0;
}

Keys K() { return Keys{}; }
Act Tap(Menu& m,bool Keys::*key) { Keys k=K();k.*key=true;return Step(m,k); }
Act TapN(Menu& m,bool Keys::*key,int n) { Act a=Act::none;for(int i=0;i<n;++i)a=Tap(m,key);return a; }
int UnitIndex(const Menu& m,const wchar_t* key,bool squad) {
    for(int i=0;i<m.unitCount;++i)if(!std::wcscmp(m.unit[i].key,key) && IsSquad(m.unit[i])==squad)return i;
    return -1;
}
// Opens a fresh menu on `ini` and moves to unit `key`, into its rows.
void OpenAt(Menu& m,Ini& ini,const wchar_t* key,bool squad) {
    m=Menu{};
    Expect(Tap(m,&Keys::toggle)==Act::opened && m.open,"toggle opens");
    Load(m,kDefaultUnits,kDefaultUnitCount,Read,&ini);
    TapN(m,&Keys::down,UnitIndex(m,key,squad));
    Expect(m.unitAt==UnitIndex(m,key,squad),"down reaches the unit");
    Tap(m,&Keys::confirm);
    Expect(m.level==Level::rows,"confirm goes into the unit");
}
const SaveLine* Line(const SaveSet& s,const wchar_t* key) {
    for(int i=0;i<s.count;++i)if(!std::wcscmp(s.line[i].key,key))return &s.line[i];
    return nullptr;
}
bool SaysValue(const SaveSet& s,const wchar_t* key,const wchar_t* value) {
    const SaveLine* l=Line(s,key);
    return l && !std::wcscmp(l->value,value);
}

void PresetRoundTrip(const wchar_t* text,int seats,const wchar_t* canonical) {
    SupportPreset p,back;wchar_t why[160],out[kValueMost];
    Expect(ParseSupportPreset(text,seats,&p,why,160),"the example preset parses");
    SerialisePreset(p,out,kValueMost);
    Expect(ParseSupportPreset(out,seats,&back,why,160) && SamePreset(p,back),"serialise(parse(preset)) parses the same");
    Expect(!std::wcscmp(out,canonical),"the preset's canonical spelling");
}
void VehicleRoundTrip(const wchar_t* text,VehicleBody body,const wchar_t* canonical) {
    VehicleLoadout l,back;wchar_t why[200],out[kValueMost];
    Expect(ParseVehicleLoadout(text,body,&l,why,200),"the example loadout parses");
    SerialiseVehicle(l,out,kValueMost);
    Expect(ParseVehicleLoadout(out,body,&back,why,200) && SameVehicle(l,back),"serialise(parse(loadout)) parses the same");
    Expect(!std::wcscmp(out,canonical),"the loadout's canonical spelling");
}

void GrammarExamples() {
    // docs/feature-2026-10-09-loadout-editor.md §2 and src/support_loadout.h's header.
    PresetRoundTrip(L"lance@1E3A8A*2,cannon,rifle@X:FFFFFF",12,L"lance@1E3A8A*2,cannon,rifle@X:FFFFFF");
    PresetRoundTrip(L"rifle@1E3A8A*2,rocket,sniper",12,L"rifle@1E3A8A*2,rocket,sniper");
    PresetRoundTrip(L"lance@1E3A8A*4,cannon*4,rifle@X:FFFFFF*4",12,L"lance@1E3A8A*4,cannon*4,rifle@X:FFFFFF*4");
    PresetRoundTrip(L"rifle@1E3A8A*2, rocket ，sniper@x:ffffff",4,L"rifle@1E3A8A*2,rocket,sniper@X:FFFFFF");
    PresetRoundTrip(L"rifle,rifle,rifle@X:X",12,L"rifle*3");
    PresetRoundTrip(L"",12,L"");
    VehicleRoundTrip(L"AP,AP:30",VehicleBody::tank,L"AP,AP:30");
    VehicleRoundTrip(L"HE,AP:25",VehicleBody::tank,L"HE,AP:25");
    VehicleRoundTrip(L"HE,AP:25,GLM:4",VehicleBody::tank,L"HE,AP:25,GLM:4");
    VehicleRoundTrip(L"AP,AP:20,GLM:4",VehicleBody::tank,L"AP,AP:20,GLM:4");
    VehicleRoundTrip(L"he",VehicleBody::tank,L"HE");
    VehicleRoundTrip(L"MK82:6,MK82:6",VehicleBody::fighter,L"MK82:6,MK82:6");
    VehicleRoundTrip(L"AGM:6,AAM_S:2",VehicleBody::strike,L"AGM:6,AAM_S:2");
    VehicleRoundTrip(L"AAM_M:4,AAM_S:2",VehicleBody::interceptor,L"AAM_M:4,AAM_S:2");
    VehicleRoundTrip(L"GUNS",VehicleBody::multirole,L"GUNS");
    VehicleRoundTrip(L"",VehicleBody::fighter,L"");
}

// Every kind, colour mix, store and round count the menu can make reads back as itself.
void ExhaustiveRoundTrip() {
    const std::int32_t colours[]={kStockColour,0x000000,0xFFFFFF,0x1E3A8A};
    int bad=0;
    for(int k=0;k<kSupportWeaponCount;++k)
        for(std::int32_t p:colours)for(std::int32_t s:colours) {
            SupportPreset a{};a.count=2;a.kind[0]=a.kind[1]=static_cast<SupportWeapon>(k);a.look[1].primary=p;a.look[1].secondary=s;
            wchar_t out[kValueMost],why[8];SupportPreset back;
            SerialisePreset(a,out,kValueMost);
            bad+=!(ParseSupportPreset(out,12,&back,why,8) && SamePreset(a,back));
        }
    for(const auto& body:kVehicleBodies)
        for(const auto& store:kLoadStores)for(int r=0;r<16;++r) {
            if(!(body.tank ? store.tank : store.jet))continue;
            VehicleLoadout a{};a.body=body.body;a.apGun=body.tank && r%2;a.count=body.most;
            for(int i=0;i<a.count;++i){a.store[i]=store.code;a.rounds[i]=static_cast<std::uint8_t>(r);}
            wchar_t out[kValueMost],why[8];VehicleLoadout back;
            SerialiseVehicle(a,out,kValueMost);
            bad+=!(ParseVehicleLoadout(out,body.body,&back,why,8) && SameVehicle(a,back));
        }
    Expect(bad==0,"every kind / colour / store / round count the menu can make reads back as itself");
    SupportPreset full{};full.count=12;
    for(int i=0;i<12;++i){full.kind[i]=SupportWeapon::wingThunderBow;full.look[i].primary=i;full.look[i].secondary=0xABCDEF;}
    wchar_t out[kValueMost];
    Expect(SerialisePreset(full,out,kValueMost)>0,"twelve distinct longest soldiers fit the value");
}

void PressAndShut() {
    Keys held{},down{};down.down=true;
    Expect(Presses(held,down).down && !Presses(held,down).down,"a key held is one press");
    down.down=false;Presses(held,down);down.down=true;
    Expect(Presses(held,down).down,"released and pressed again: a press");
    Menu m;
    Keys all{false,true,true,true,true,true,true,true,true,true};
    Expect(Step(m,all)==Act::none && !m.open,"shut, every key but the toggle is the room's");
}

void LoadsTheIni() {
    Ini ini;
    Set(ini,L"SupportPreset_SQUAD",L"lance@1E3A8A*2,cannon,rifle@X:FFFFFF");
    Set(ini,L"SupportPreset_TRANSPORT_CREWED",L"rifle*5");   // past its 4 seats: refused
    Set(ini,L"SupportVehicle_TANK_CREWED",L"HE,AP:25");
    Set(ini,L"SupportVehicle_FIGHTER",L"MK82:6,MK82:6");
    Set(ini,L"SupportVehicle_STRIKE",L"AP:4");   // a tank round on a jet: refused
    Menu m;Load(m,kDefaultUnits,kDefaultUnitCount,Read,&ini);
    Expect(m.unitCount==18,"8 squads and 10 vehicles");
    const Unit& squad=m.unit[UnitIndex(m,L"SQUAD",true)];
    Expect(squad.squad.count==4 && squad.squad.kind[0]==SupportWeapon::wingLance && !squad.refused,"the squad's preset loaded");
    const Unit& apc=m.unit[UnitIndex(m,L"TRANSPORT_CREWED",true)];
    Expect(apc.refused && apc.squad.count==0 && apc.seats==4,"a refused preset shown stock, its seats kept");
    const Unit& tank=m.unit[UnitIndex(m,L"TANK_CREWED",false)];
    Expect(tank.vehicle.On() && tank.vehicle.count==1 && !tank.vehicle.apGun,"the tank's pylons loaded");
    Expect(m.unit[UnitIndex(m,L"STRIKE",false)].refused,"a jet with a tank round refused");
    Expect(UnitIndex(m,L"TANK_CREWED",true)<0,"a tank has no squad (no seats)");
    SaveSet s;
    Expect(!Dirty(m) && CollectSave(m,&s) && s.count==0,"nothing changed: nothing to write (the refused values left alone)");
}

void Navigation() {
    Ini ini;Menu m;
    Tap(m,&Keys::toggle);Load(m,kDefaultUnits,kDefaultUnitCount,Read,&ini);
    Expect(Tap(m,&Keys::up)==Act::moved && m.unitAt==m.unitCount,"up from the top wraps to the save row");
    Expect(Tap(m,&Keys::down)==Act::moved && m.unitAt==0,"down from the save row wraps to the top");
    Expect(Tap(m,&Keys::left)==Act::none && m.open,"left on the unit list does nothing");
    Expect(Tap(m,&Keys::right)==Act::moved && m.level==Level::rows,"right goes in like confirm");
    Expect(Tap(m,&Keys::left)==Act::moved && m.level==Level::units,"left comes back out");
    // A stock squad: one "own load" row; confirm adds a stock rifle; confirm opens its fields.
    Tap(m,&Keys::confirm);
    Expect(RowCount(m.unit[0])==1 && Tap(m,&Keys::confirm)==Act::changed && m.unit[0].squad.count==1,"confirm on the own load adds a soldier");
    Expect(m.unit[0].squad.kind[0]==SupportWeapon::rifle && !m.unit[0].squad.look[0].On(),"a stock rifle");
    Expect(Tap(m,&Keys::confirm)==Act::moved && m.level==Level::fields && m.fieldAt==0,"confirm opens the fields");
    Tap(m,&Keys::right);
    Expect(m.unit[0].squad.kind[0]==SupportWeapon::flame,"right: the next kind");
    TapN(m,&Keys::left,2);
    Expect(m.unit[0].squad.kind[0]==SupportWeapon::fencerShotgun,"left past the first kind wraps to the last");
    Tap(m,&Keys::down);
    Expect(m.fieldAt==1,"down: the primary colour");
    Tap(m,&Keys::right);
    Expect(m.unit[0].squad.look[0].primary==kDefaultPalette.rgb[0],"right from stock: the palette's first");
    TapN(m,&Keys::left,2);
    Expect(m.unit[0].squad.look[0].primary==kDefaultPalette.rgb[kDefaultPalette.count-1],"left past stock wraps to the palette's last");
    Tap(m,&Keys::up);Tap(m,&Keys::up);
    Expect(m.fieldAt==2,"up from the kind wraps to the secondary");
    Tap(m,&Keys::left);
    Expect(m.unit[0].squad.look[0].secondary==kDefaultPalette.rgb[kDefaultPalette.count-1],"left: the secondary from stock");
    Tap(m,&Keys::cancel);
    Expect(m.level==Level::rows,"cancel closes the fields, edits kept");
    Tap(m,&Keys::cancel);
    TapN(m,&Keys::up,1);
    Expect(m.unitAt==m.unitCount,"to the save row");
    Expect(Tap(m,&Keys::confirm)==Act::save && m.open,"save asks the caller to write");
    SaveSet s;
    Expect(CollectSave(m,&s) && s.count==1 && SaysValue(s,L"SupportPreset_SQUAD",L"fshotgun@C3B091:C3B091"),"the one changed key, its value");
    MarkSaved(m);
    Expect(!m.open && !Dirty(m),"saved: shut and clean");
    // Open again (the caller reloads on Act::opened): save with nothing changed just closes.
    Tap(m,&Keys::toggle);Load(m,kDefaultUnits,kDefaultUnitCount,Read,&ini);
    Tap(m,&Keys::up);
    Expect(Tap(m,&Keys::confirm)==Act::closed && !m.open,"save with nothing changed closes");
}

void SquadLimits() {
    Ini ini;Menu m;
    OpenAt(m,ini,L"TRANSPORT_CREWED",true);
    for(int i=0;i<4;++i)Expect(Tap(m,&Keys::add)==Act::changed,"add up to the seats");
    Expect(Tap(m,&Keys::add)==Act::refused && m.unit[m.unitAt].squad.count==4,"no fifth soldier on four seats");
    OpenAt(m,ini,L"SQUAD",true);
    TapN(m,&Keys::add,12);
    Expect(Tap(m,&Keys::add)==Act::refused && m.unit[m.unitAt].squad.count==kSupportLoadoutMost,"twelve at most");
    View v=Snapshot(m);
    Expect(v.rows.total==12,"twelve soldier rows");
    // Remove from the middle, then all of them: back to the own load (the empty value).
    Tap(m,&Keys::up);   // row 10
    Expect(Tap(m,&Keys::remove)==Act::changed && m.unit[m.unitAt].squad.count==11 && m.rowAt==10,"remove at the cursor");
    TapN(m,&Keys::remove,11);
    Expect(m.unit[m.unitAt].squad.count==0 && Tap(m,&Keys::remove)==Act::refused,"none left to remove");
    Expect(Snapshot(m).rows.row[0].text==RowText::ownLoad,"none: the entry's own load");
    // Add copies the soldier at the cursor and lands after it.
    Tap(m,&Keys::confirm);Tap(m,&Keys::confirm);Tap(m,&Keys::right);Tap(m,&Keys::cancel);   // a flame soldier
    Tap(m,&Keys::add);
    const SupportPreset& p=m.unit[m.unitAt].squad;
    Expect(p.count==2 && p.kind[1]==SupportWeapon::flame && m.rowAt==1,"add copies the soldier at the cursor");
}

void VehicleLimits() {
    Ini ini;Menu m;
    OpenAt(m,ini,L"TANK_CREWED",false);
    Expect(Snapshot(m).rows.row[0].text==RowText::stockVehicle,"a stock tank: one stock row");
    for(int i=0;i<4;++i)Expect(Tap(m,&Keys::add)==Act::changed,"a tank takes 4 pylons");
    Expect(Tap(m,&Keys::add)==Act::refused,"not a fifth");
    // Each pylon's store goes round AP / HE / GLM only.
    TapN(m,&Keys::up,4);Tap(m,&Keys::down);   // pylon 1
    Tap(m,&Keys::confirm);
    std::uint8_t seen[16]{};
    for(int i=0;i<6;++i){Tap(m,&Keys::right);seen[m.unit[m.unitAt].vehicle.store[0]]=1;}
    Expect(seen[8] && seen[9] && seen[10] && !seen[1] && !seen[6],"a tank's stores: AP HE GLM, no jet store");
    Tap(m,&Keys::down);
    TapN(m,&Keys::right,16);
    Expect(kRoundCounts[m.unit[m.unitAt].vehicle.rounds[0]]==20,"rounds go round all sixteen counts");
    Tap(m,&Keys::left);
    Expect(kRoundCounts[m.unit[m.unitAt].vehicle.rounds[0]]==19,"left: the count before");
    Tap(m,&Keys::cancel);
    // The gun row: HE <-> AP.
    Tap(m,&Keys::up);Tap(m,&Keys::confirm);Tap(m,&Keys::right);
    Expect(m.unit[m.unitAt].vehicle.apGun,"the gun field toggles to AP");
    Tap(m,&Keys::cancel);
    Expect(Tap(m,&Keys::remove)==Act::refused,"the gun row cannot be removed");
    // A jet: 5 pylons, only air stores, none = GUNS.
    OpenAt(m,ini,L"FIGHTER",false);
    Expect(Tap(m,&Keys::confirm)==Act::changed && m.unit[m.unitAt].vehicle.On(),"confirm on a stock jet: guns alone");
    for(int i=0;i<5;++i)Expect(Tap(m,&Keys::add)==Act::changed,"a jet takes 5 pylons");
    Expect(Tap(m,&Keys::add)==Act::refused,"not a sixth");
    Tap(m,&Keys::confirm);
    std::uint8_t jet[16]{};
    for(int i=0;i<8;++i){Tap(m,&Keys::right);jet[m.unit[m.unitAt].vehicle.store[4]]=1;}
    int stores=0;for(int c=1;c<=7;++c)stores+=jet[c];
    Expect(stores==7 && !jet[8] && !jet[9] && !jet[10],"a jet's stores: the seven air stores, no tank round");
    Tap(m,&Keys::cancel);
    TapN(m,&Keys::remove,5);
    Expect(m.unit[m.unitAt].vehicle.count==0 && Snapshot(m).rows.row[0].text==RowText::guns,"all removed: the guns alone");
    SaveSet s;
    Expect(CollectSave(m,&s) && SaysValue(s,L"SupportVehicle_FIGHTER",L"GUNS"),"guns alone is written GUNS");
}

void ResetAndCancel() {
    Ini ini;
    Set(ini,L"SupportPreset_SQUAD",L"lance@1E3A8A*2,cannon");
    Set(ini,L"SupportVehicle_TANK_CREWED",L"HE,AP:25");
    Menu m;
    OpenAt(m,ini,L"TANK_CREWED",false);
    Expect(Tap(m,&Keys::reset)==Act::changed && !m.unit[m.unitAt].vehicle.On(),"reset: the stock tank");
    Tap(m,&Keys::cancel);
    Tap(m,&Keys::up);   // back to the list, onto another unit
    Tap(m,&Keys::reset);
    SaveSet s;
    Expect(CollectSave(m,&s) && s.count==1 && SaysValue(s,L"SupportVehicle_TANK_CREWED",L""),"reset writes the empty value");
    // Reset from the unit list, on the squad.
    m.unitAt=UnitIndex(m,L"SQUAD",true);
    Tap(m,&Keys::reset);
    Expect(CollectSave(m,&s) && s.count==2 && SaysValue(s,L"SupportPreset_SQUAD",L""),"the squad reset too");
    Expect(Tap(m,&Keys::cancel)==Act::closed && !m.open && !Dirty(m),"cancel discards and closes");
    Expect(m.unit[UnitIndex(m,L"SQUAD",true)].squad.count==3,"the loaded preset is back");
    // The toggle discards the same way.
    OpenAt(m,ini,L"SQUAD",true);
    Tap(m,&Keys::add);
    Expect(Dirty(m) && Tap(m,&Keys::toggle)==Act::closed && !Dirty(m),"toggle closes discarding");
    // A refused value changed by the player is rewritten, valid.
    Set(ini,L"SupportPreset_TRUCK_CREWED",L"bogus");
    OpenAt(m,ini,L"TRUCK_CREWED",true);
    Tap(m,&Keys::add);
    Expect(CollectSave(m,&s) && SaysValue(s,L"SupportPreset_TRUCK_CREWED",L"rifle"),"a refused value replaced once edited");
    MarkSaved(m);
    Expect(!m.unit[UnitIndex(m,L"TRUCK_CREWED",true)].refused,"saved: no longer refused");
}

// Thousands of random presses: every save the menu collects reads back through the real parsers.
void RandomPresses() {
    Ini ini;
    Set(ini,L"SupportPreset_PLATOON",L"lance@1E3A8A*4,cannon*4,rifle@X:FFFFFF*4");
    Set(ini,L"SupportVehicle_STRIKE",L"AGM:6,AAM_S:2");
    Menu m;m.pageRows=5;
    Tap(m,&Keys::toggle);Load(m,kDefaultUnits,kDefaultUnitCount,Read,&ini);
    std::uint32_t seed=12345;
    int bad=0,saves=0;
    bool Keys::* keys[]={&Keys::up,&Keys::down,&Keys::left,&Keys::right,&Keys::confirm,&Keys::cancel,&Keys::add,
                          &Keys::remove,&Keys::reset};
    for(int i=0;i<20000;++i) {
        seed=seed*1664525u+1013904223u;
        const int pick=static_cast<int>((seed>>16)%9);
        if(pick==5 && (seed>>8)%4)continue;   // fewer cancels: deeper edits
        const Act a=Tap(m,keys[pick]);
        if(!m.open){Tap(m,&Keys::toggle);continue;}
        SaveSet s;
        if(!CollectSave(m,&s)){++bad;continue;}
        saves+=a==Act::save;
        const View v=Snapshot(m);
        bad+=v.units.count>m.pageRows || v.rows.count>m.pageRows;
        if(a==Act::save)MarkSaved(m);
    }
    Expect(bad==0,"every value collected after random presses reads back, the window never overflows");
    Expect(saves>0,"the random walk saved at least once");
}

void SnapshotAndScroll() {
    Expect(ScrollFirst(0,5,19,4)==2,"a cursor below the window scrolls it down just enough");
    Expect(ScrollFirst(2,0,19,4)==0,"a cursor above scrolls it up");
    Expect(ScrollFirst(10,5,6,8)==0,"fewer rows than the window: from the top");
    Ini ini;
    Set(ini,L"SupportPreset_SQUAD",L"lance@1E3A8A*2,cannon,rifle@X:FFFFFF*9");
    Set(ini,L"SupportVehicle_TANK_CREWED",L"HE,AP:25,GLM:4");
    Menu m;m.pageRows=4;
    Tap(m,&Keys::toggle);Load(m,kDefaultUnits,kDefaultUnitCount,Read,&ini);
    TapN(m,&Keys::down,5);
    View v=Snapshot(m);
    Expect(v.units.first==2 && v.units.count==4 && v.units.total==19 && v.units.row[3].cursor,"the unit window follows the cursor");
    Tap(m,&Keys::up);Tap(m,&Keys::up);Tap(m,&Keys::up);
    Expect(Snapshot(m).units.first==2,"moving inside the window leaves it");
    Tap(m,&Keys::up);
    Expect(Snapshot(m).units.first==1,"past its top it scrolls one");
    TapN(m,&Keys::up,2);
    v=Snapshot(m);
    Expect(m.unitAt==m.unitCount && v.units.first==15 && v.units.row[3].text==RowText::save && v.units.row[3].cursor,
           "wrapped to the save row: the last page");
    // The squad: leaders every four, colours as RGBA, stock as 0.
    m.unitAt=UnitIndex(m,L"SQUAD",true);
    Tap(m,&Keys::confirm);
    TapN(m,&Keys::down,9);
    v=Snapshot(m);
    Expect(v.rows.total==12 && v.rows.first==6 && v.rows.row[3].cursor && v.rows.row[2].leader && !v.rows.row[1].leader,
           "the soldier window, the leader of the third four");
    m.rowAt=0;m.rowFirst=0;Tap(m,&Keys::confirm);
    v=Snapshot(m);
    const ViewRow& lead=v.rows.row[0];
    Expect(lead.text==RowText::soldier && lead.leader && lead.kind==static_cast<int>(SupportWeapon::wingLance) && lead.soldierClass==1,
           "the first soldier: a Wing Diver leader");
    Expect(lead.primary==0x1E3A8AFFu && lead.secondary==0 && lead.editing && lead.field==0 && lead.label,"its colours, label, fields open");
    Expect(v.rows.row[3].secondary==0xFFFFFFFFu && v.rows.row[3].primary==0 && v.rows.row[2].soldierClass==2,"a stock primary, a white secondary, a Fencer");
    Expect(v.units.row[0].text==RowText::squadUnit || v.units.first>0,"unit rows name their kind");
    // The tank: the gun row then its pylons.
    Tap(m,&Keys::cancel);Tap(m,&Keys::cancel);
    m.unitAt=UnitIndex(m,L"TANK_CREWED",false);
    Tap(m,&Keys::confirm);
    v=Snapshot(m);
    Expect(v.rows.total==3 && v.rows.row[0].text==RowText::gunHE && v.rows.row[1].text==RowText::pylon && v.rows.row[1].store==8 &&
           v.rows.row[1].rounds==25 && v.rows.row[2].store==10 && v.rows.row[2].rounds==4,"the tank's rows: gun, AP 25, GLM 4");
    Tap(m,&Keys::reset);
    v=Snapshot(m);
    Expect(v.dirty && v.units.row[UnitIndex(m,L"TANK_CREWED",false)-v.units.first].changed,"a changed unit is marked, the menu dirty");
    // A replaced palette (the HQ's) is what the colour fields cycle.
    Palette hq{};hq.count=2;hq.rgb[0]=0x123456;hq.rgb[1]=0x654321;
    Expect(CycleColour(hq,kStockColour,1)==0x123456 && CycleColour(hq,0x654321,1)==kStockColour && CycleColour(hq,0xABCDEF,-1)==0x654321,
           "a replaced palette; a hand-written colour counts as stock's place");
}
}

int main() {
    GrammarExamples();
    ExhaustiveRoundTrip();
    PressAndShut();
    LoadsTheIni();
    Navigation();
    SquadLimits();
    VehicleLimits();
    ResetAndCancel();
    RandomPresses();
    SnapshotAndScroll();
    std::printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
