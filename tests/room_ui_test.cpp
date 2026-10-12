// The room / HQ loadout screen's pure side (src/room_ui_logic.h) offline: the element names (at most 7 characters, unique,
// the layout generator's spelling), the keyboard / pad mapping (the toggle combination's buttons do nothing else while
// held, nothing while the game is behind), the input gate (taken while open, and after closing until every menu key is up),
// the covered-screen plan, the rows' looks (chips only for a set colour, icons per kind / store / gun), the lines in each
// language, the catalog's units, the preview's soldier and the save's (field, unit, value) pairing. Exit code 1 when one fails.
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
    Expect(n==7,"seats or a vehicle key: 7 of 10 (no gunship, empty transport, rescue)");
    Expect(n==7 && Same(specs[0].key,L"STRIKE") && specs[0].seats==0 && Same(specs[0].label,L"对地攻击机·守点"),"jet entry");
    Expect(n==7 && Same(specs[2].key,L"SQUAD") && specs[2].seats==12,"squad entry");
    Expect(n==7 && Same(specs[6].key,L"TRANSPORT_CREWED") && specs[6].seats==4,"crewed APC riders");
    Expect(CatalogUnits(10,KeyAt,SeatsAt,NameAt,specs,3)==3,"capacity kept");
    roommenu::Menu m;
    roommenu::Load(m,specs,n,nullptr,nullptr);
    Expect(m.unitCount==7,"the menu loads every catalog unit");
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
    Expect(!PreviewFor(m,&p),"a vehicle: none");
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
}
}  // namespace

int main() {
    Names();
    Input();
    GateAndPlan();
    Looks();
    Texts();
    Catalog();
    Preview();
    Save();
    std::printf("room_ui: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
