// The room / HQ menu as the game's own dialogs (src/room_dialog_logic.h) offline: every page's buttons, where each one
// goes, the edits it makes through room_menu.h (the same model the save writes), the game's cancel going back a page,
// long lists paged, the cursor on the current value, unsaved edits asked about before closing, and a random walk of
// thousands of picks that never leaves an index out of range or a value the real parsers refuse.
// cmake --build build --target room_dialog_test && build\room_dialog_test.exe
#include "../src/room_dialog_logic.h"
#include <cstdio>
#include <initializer_list>
#include <cwchar>

namespace {
using namespace crew;
using namespace crew::roomdialog;
namespace rm=crew::roommenu;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
std::size_t NoIni(void*,const wchar_t*,wchar_t* out,std::size_t) noexcept { out[0]=0; return 0; }
rm::Menu Fresh() {
    rm::Menu m;
    m.open=true;
    rm::Load(m,rm::kDefaultUnits,rm::kDefaultUnitCount,&NoIni,nullptr);
    return m;
}
int UnitOf(const rm::Menu& m,const wchar_t* key,rm::UnitKind kind) {
    for(int i=0;i<m.unitCount;++i)if(!std::wcscmp(m.unit[i].key,key) && m.unit[i].kind==kind)return i;
    return -1;
}
// The button doing `what` (with `arg` when given) on the page now; -1 none.
int Find(const Dialog& d,Do what,std::int32_t arg=-999) {
    for(int i=0;i<d.count;++i)if(d.item[i].choice.what==what && (arg==-999 || d.item[i].choice.arg==arg))return i;
    return -1;
}
// Picks the button, turning the pages (from the page shown, then from the first) until it shows, as a player would.
Result Pick(rm::Menu& m,Nav& nav,Do what,std::int32_t arg=-999) {
    bool rewound=false;
    for(int turn=0;turn<32;++turn) {
        const Dialog d=Build(m,nav,Context{});
        const int at=Find(d,what,arg);
        if(at>=0)return Apply(m,nav,d.item[at].choice);
        const int next=Find(d,Do::next);
        if(next>=0){Apply(m,nav,d.item[next].choice);continue;}
        if(rewound || nav.first==0)break;
        nav.first=0;rewound=true;
    }
    Expect(false,"the button is on the page");
    return Result::show;
}
bool HasText(const Dialog& d,const wchar_t* part) {
    for(int i=0;i<d.count;++i)if(std::wcsstr(d.item[i].text,part))return true;
    return false;
}

void PackRoundTrip() {
    for(int w=0;w<=static_cast<int>(Do::set);++w)
        for(std::int32_t a:{0,1,47,-2,-1,0x7FFFFF,0xFFFFFF}) {
            const Choice c{static_cast<Do>(w),a};
            const Choice back=Unpack(Pack(c));
            Expect(back.what==c.what && back.arg==c.arg,"Pack / Unpack");
            Expect((Pack(c)>>40)==0,"an id fits the low 40 bits (room_dialog.cpp puts the dialog's serial above)");
        }
}

void UnitsPage() {
    rm::Menu m=Fresh();Nav nav;Begin(m,nav);
    Dialog d=Build(m,nav,Context{});
    Expect(d.count<=kItemsMost,"a page fits the dialog");
    Expect(Find(d,Do::open,0)==0,"the first unit first");
    Expect(Find(d,Do::next)>=0,"20 units: a next page");
    Expect(Find(d,Do::save)<0,"nothing edited: no save button");
    Expect(d.item[d.count-1].choice.what==Do::close,"the last button closes");
    Expect(Apply(m,nav,{Do::close,0})==Result::close && !m.open,"close, nothing edited: closed at once");
    m=Fresh();Begin(m,nav);
    Apply(m,nav,{Do::next,0});
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::open,kPerPage)==0 && Find(d,Do::prev)>=0,"next page: the 11th unit first, a previous button");
    Apply(m,nav,{Do::prev,0});
    Expect(nav.first==0,"previous: back to the first");
    Context c;c.status[0]=L"FILES READY";c.statusCount=1;
    d=Build(m,nav,c);
    Expect(std::wcsstr(d.title,L"FILES READY")!=nullptr,"the status lines in the units page's message");
}

void Squad() {
    rm::Menu m=Fresh();Nav nav;Begin(m,nav);
    const int squad=UnitOf(m,L"SQUAD",rm::UnitKind::squad);
    Pick(m,nav,Do::open,squad);
    Expect(nav.page==Page::unit && nav.unit==squad && m.unitAt==squad && m.level==rm::Level::rows,"a unit opened (the preview follows)");
    Dialog d=Build(m,nav,Context{});
    Expect(Find(d,Do::row)<0 && Find(d,Do::add)>=0 && Find(d,Do::reset)<0,"its own soldiers: add one, nothing to reset");
    Pick(m,nav,Do::add);
    Expect(m.unit[squad].squad.count==1 && nav.page==Page::soldier && nav.row==0,"added: its soldier's page");
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::field,FieldArg(Page::kind))==0,"class first");
    Expect(Find(d,Do::field,FieldArg(Page::pack))>=0,"a Ranger: a backpack");
    Pick(m,nav,Do::copy);
    Expect(m.unit[squad].squad.count==2 && nav.row==1,"copied after itself");
    // the class: a Fencer carries no backpack (its pack dropped)
    m.unit[squad].squad.look[1].pack=1;
    Pick(m,nav,Do::field,FieldArg(Page::kind));
    Expect(nav.page==Page::kind && m.level==rm::Level::fields && m.rowAt==1,"the class page (the preview: soldier 2)");
    d=Build(m,nav,Context{});
    Expect(d.cursor==0,"the cursor on the current class");
    Pick(m,nav,Do::set,static_cast<int>(SupportWeapon::fencerCannon));
    Expect(m.unit[squad].squad.kind[1]==SupportWeapon::fencerCannon && m.unit[squad].squad.look[1].pack==0,"set: a Fencer, no pack");
    Expect(nav.page==Page::soldier && nav.row==1,"back on the soldier");
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::field,FieldArg(Page::pack))<0,"a Fencer: no backpack button");
    // a colour on the 2nd page of the palette: the dialog opens there with the cursor on it
    const std::int32_t far11=m.palette.rgb[12];
    m.unit[squad].squad.look[1].primary=far11;
    Pick(m,nav,Do::field,FieldArg(Page::primary));
    d=Build(m,nav,Context{});
    Expect(nav.first==kPerPage && d.item[d.cursor].choice.arg==far11,"opened on the current colour's page, the cursor on it");
    Expect(Find(d,Do::prev)>=0,"a previous button");
    Apply(m,nav,{Do::prev,0});
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::set,kStockColour)==0,"stock first");
    Pick(m,nav,Do::set,m.palette.rgb[0]);
    Expect(m.unit[squad].squad.look[1].primary==m.palette.rgb[0] && nav.page==Page::soldier,"colour set, back");
    // the game's cancel goes up a page, then out to the units, then asks about the edits
    Pick(m,nav,Do::field,FieldArg(Page::secondary));
    Expect(Cancel(m,nav)==Result::show && nav.page==Page::soldier,"cancel: back to the soldier");
    Expect(Cancel(m,nav)==Result::show && nav.page==Page::unit,"cancel: the unit");
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::row,0)==0 && Find(d,Do::row,1)==1 && Find(d,Do::reset)>=0,"its soldiers listed, reset offered");
    Expect(HasText(d,L"1."),"numbered");
    Expect(Cancel(m,nav)==Result::show && nav.page==Page::units,"cancel: the units");
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::save)>=0 && d.item[d.count-1].choice.what==Do::close,"edited: save, and close");
    Expect(Cancel(m,nav)==Result::show && nav.page==Page::discard && m.open,"close with edits: asked first");
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::discard)>=0 && Find(d,Do::save)>=0 && Find(d,Do::keep)>=0,"discard / save / keep editing");
    Expect(Pick(m,nav,Do::keep)==Result::show && nav.page==Page::units && rm::Dirty(m),"keep editing: the edits kept");
    Expect(Pick(m,nav,Do::save)==Result::save,"save");
    rm::SaveSet set;
    Expect(rm::CollectSave(m,&set) && set.count==1,"the save: one line, read back by the parsers");
    Apply(m,nav,{Do::close,0});
    Expect(Pick(m,nav,Do::discard)==Result::close && !m.open && !rm::Dirty(m),"discard: closed, edits dropped");
    // remove, reset
    m=Fresh();Begin(m,nav);
    Pick(m,nav,Do::open,squad);Pick(m,nav,Do::add);Pick(m,nav,Do::copy);
    Pick(m,nav,Do::remove);
    Expect(m.unit[squad].squad.count==1 && nav.page==Page::unit,"removed: the unit page");
    Pick(m,nav,Do::reset);
    Expect(m.unit[squad].squad.count==0 && !rm::UnitChanged(m.unit[squad]),"reset: the stock unit");
    // the seats limit
    for(int i=0;i<20;++i){Nav at=nav;at.page=Page::unit;const Dialog dd=Build(m,at,Context{});if(Find(dd,Do::add)<0)break;nav=at;Apply(m,nav,dd.item[Find(dd,Do::add)].choice);}
    Expect(m.unit[squad].squad.count==m.unit[squad].seats,"added up to the seats");
    nav.page=Page::unit;
    Expect(Find(Build(m,nav,Context{}),Do::add)<0,"full: no add button");
}

void Vehicle() {
    rm::Menu m=Fresh();Nav nav;Begin(m,nav);
    const int tank=UnitOf(m,L"TANK_CREWED",rm::UnitKind::vehicle);
    Pick(m,nav,Do::open,tank);
    Dialog d=Build(m,nav,Context{});
    Expect(Find(d,Do::customise)==0 && Find(d,Do::row,rm::CrewRow(m.unit[tank]))>=0,"stock: customise, the crew");
    Pick(m,nav,Do::customise);
    Expect(m.unit[tank].vehicle.On() && nav.page==Page::unit,"customised");
    Pick(m,nav,Do::field,FieldArg(Page::gun));
    Expect(nav.page==Page::gun,"the gun page");
    Pick(m,nav,Do::set,1);
    Expect(m.unit[tank].vehicle.apGun && nav.page==Page::unit,"AP");
    Pick(m,nav,Do::add);
    Expect(m.unit[tank].vehicle.count==1 && nav.page==Page::pylon && nav.row==1,"a pylon added: its page");
    Pick(m,nav,Do::field,FieldArg(Page::store));
    d=Build(m,nav,Context{});
    Expect(d.count==3+1,"a tank: AP HE GLM, back");
    Pick(m,nav,Do::set,10);
    Expect(m.unit[tank].vehicle.store[0]==10 && nav.page==Page::pylon,"GLM");
    Pick(m,nav,Do::field,FieldArg(Page::rounds));
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::next)>=0 || Find(d,Do::prev)>=0,"16 round counts: paged");
    Expect(d.item[d.cursor].choice.arg==m.unit[tank].vehicle.rounds[0],"opened on the current count");
    Pick(m,nav,Do::set,3);
    Expect(m.unit[tank].vehicle.rounds[0]==3,"rounds");
    rm::SaveSet set;
    Expect(rm::CollectSave(m,&set) && set.count==1,"the tank's loadout reads back");
    Pick(m,nav,Do::remove);
    Expect(m.unit[tank].vehicle.count==0 && nav.page==Page::unit,"pylon removed");
    // the crew
    Pick(m,nav,Do::row,rm::CrewRow(m.unit[tank]));
    Expect(nav.page==Page::crew && m.rowAt==rm::CrewRow(m.unit[tank]),"the crew page (the preview: the crew)");
    Pick(m,nav,Do::field,FieldArg(Page::crewPrimary));
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::set,-2)==0 && d.cursor==0,"the crew's primary: the default first, and current");
    Pick(m,nav,Do::set,kStockColour);
    Expect(m.unit[tank].crew.set && m.unit[tank].crew.look.primary==kStockColour && nav.page==Page::crew,"set to stock (X)");
    Pick(m,nav,Do::field,FieldArg(Page::crewPrimary));
    Pick(m,nav,Do::set,-2);
    Expect(!m.unit[tank].crew.set,"back to the default");
    // a crew unit: its colours on its own page
    const int def=UnitOf(m,L"",rm::UnitKind::crew);
    nav=Nav{};
    Pick(m,nav,Do::open,def);
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::field,FieldArg(Page::crewPrimary))>=0 && Find(d,Do::row)<0,"a crew unit: its colours");
    Pick(m,nav,Do::field,FieldArg(Page::crewSecondary));
    Pick(m,nav,Do::set,m.palette.rgb[1]);
    Expect(nav.page==Page::unit && m.unit[def].crew.set && m.unit[def].crew.look.secondary==m.palette.rgb[1],"back on the crew unit");
    Pick(m,nav,Do::field,FieldArg(Page::crewPrimary));
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::set,kStockColour)<0,"the default's own: not set is stock (no second stock)");
}

// A jet's weight (support_loadout.h WithinPayload): its pages say it, offer only what keeps it, and a store whose rounds
// would not fit takes as many as do.
void JetWeight() {
    rm::Menu m=Fresh();Nav nav;Begin(m,nav);
    const int jet=UnitOf(m,L"FIGHTER",rm::UnitKind::vehicle);   // 6400 kg
    Pick(m,nav,Do::open,jet);
    Pick(m,nav,Do::customise);
    Pick(m,nav,Do::add);   // AIM-9X x2: its pylon page
    Dialog d=Build(m,nav,Context{});
    Expect(std::wcsstr(d.title,L"170 / 6400 kg")!=nullptr,"the pylon page says the weight and the budget");
    Pick(m,nav,Do::field,FieldArg(Page::store));
    Pick(m,nav,Do::set,3);   // AIM-54
    Pick(m,nav,Do::field,FieldArg(Page::rounds));
    Pick(m,nav,Do::set,RoundCode(12));   // 5400 kg
    Expect(rm::UnitMassKg(m.unit[jet])==5400,"AIM-54 x12");
    Pick(m,nav,Do::back);
    Pick(m,nav,Do::add);     // 1000 kg left: the new pylon fits (AIM-9X x2)
    Expect(m.unit[jet].vehicle.count==2 && WithinPayload(m.unit[jet].vehicle),"a second pylon within the weight");
    Pick(m,nav,Do::field,FieldArg(Page::rounds));
    d=Build(m,nav,Context{});
    int listed=0;
    for(int turn=0;turn<4;++turn) {
        for(int i=0;i<d.count;++i)
            if(d.item[i].choice.what==Do::set) {
                ++listed;
                Expect(rm::PylonFits(m.unit[jet],1,m.unit[jet].vehicle.store[1],static_cast<std::uint8_t>(d.item[i].choice.arg)),
                       "every round count offered fits the weight");
            }
        const int next=Find(d,Do::next);
        if(next<0)break;
        Apply(m,nav,d.item[next].choice);d=Build(m,nav,Context{});
    }
    Expect(listed==8,"AIM-9X: up to 11 of them fit the 1000 kg left (the 8 counts 1..10)");
    nav.first=0;
    Pick(m,nav,Do::set,RoundCode(10));   // 850 kg
    Pick(m,nav,Do::field,FieldArg(Page::store));
    Pick(m,nav,Do::set,6);
    Expect(m.unit[jet].vehicle.store[1]==6 && kRoundCounts[m.unit[jet].vehicle.rounds[1]]==4 && WithinPayload(m.unit[jet].vehicle),
           "Mk 82 x10 would not fit: it takes as many as do, 4 (920 kg)");
    Pick(m,nav,Do::back);
    Pick(m,nav,Do::add);     // 80 kg left: no AIM-9X pair; the first store of which one fits, one AGM-114 (50 kg)
    Expect(m.unit[jet].vehicle.count==3 && m.unit[jet].vehicle.store[2]==5 && kRoundCounts[m.unit[jet].vehicle.rounds[2]]==1,
           "the third pylon: what still fits");
    Pick(m,nav,Do::field,FieldArg(Page::store));
    d=Build(m,nav,Context{});
    Expect(Find(d,Do::set,5)>=0 && Find(d,Do::set,7)>=0 && Find(d,Do::set,1)<0 && Find(d,Do::set,3)<0 && d.count==3,
           "with 80 kg left only AGM-114 and Hydra 70 are offered");
    rm::SaveSet set;
    Expect(rm::CollectSave(m,&set),"what the pages made saves");
}

// The description beside the list (the description dialog's right column): every button says what it is or does.
void Descriptions() {
    hudtext::Use(hudtext::Lang::zhCN);
    rm::Menu m=Fresh();Nav nav;Begin(m,nav);
    const int squad=UnitOf(m,L"SQUAD",rm::UnitKind::squad);
    m.unit[squad].squad.count=2;m.unit[squad].squad.kind[1]=SupportWeapon::fencerCannon;
    m.unit[squad].squad.look[0].primary=m.palette.rgb[0];
    Dialog d=Build(m,nav,Context{});
    for(int i=0;i<d.count;++i)Expect(d.item[i].desc[0]!=0,"every units-page button has a description");
    Pick(m,nav,Do::open,squad);nav=Nav{};Begin(m,nav);
    d=Build(m,nav,Context{});
    const int at=Find(d,Do::open,squad);
    Expect(at>=0 && std::wcsstr(d.item[at].desc,L"队员 2 / 12 座") && std::wcsstr(d.item[at].desc,KindName(static_cast<int>(SupportWeapon::fencerCannon))),
           "a squad's: its soldiers, their classes");
    // A jet's: each pylon with its weight, the weight against the budget.
    const int jet=UnitOf(m,L"FIGHTER",rm::UnitKind::vehicle);
    m.unit[jet].vehicle.body=VehicleBody::fighter;m.unit[jet].vehicle.count=1;m.unit[jet].vehicle.store[0]=6;
    m.unit[jet].vehicle.rounds[0]=static_cast<std::uint8_t>(RoundCode(12));
    d=Build(m,nav,Context{});
    const int j=Find(d,Do::open,jet);
    Expect(j<0 || (std::wcsstr(d.item[j].desc,L"2760 kg") && std::wcsstr(d.item[j].desc,L"2760 / 6400 kg")),
           "a jet's: its pylon's weight and its weight against the budget (when on this page)");
    Pick(m,nav,Do::open,jet);
    Pick(m,nav,Do::row,1);
    Pick(m,nav,Do::field,FieldArg(Page::store));
    d=Build(m,nav,Context{});
    const int bomb=Find(d,Do::set,6);
    Expect(bomb>=0 && std::wcsstr(d.item[bomb].desc,L"每发 230 kg") && std::wcsstr(d.item[bomb].desc,L"最多装 25 发"),
           "a store: its round's weight and how many fit this pylon (6400 kg: 25 Mk 82 of the counts)");
    // A player's own summon: says where it applies.
    const int mine=UnitOf(m,L"OWN_PFIGHTER",rm::UnitKind::vehicle);
    Expect(mine>=0 && rm::IsPlayerVehicleUnit(m.unit[mine]),"the player's own summons are units of the menu");
    nav=Nav{};Begin(m,nav);
    for(int turn=0;turn<8;++turn) {
        d=Build(m,nav,Context{});
        const int k=Find(d,Do::open,mine);
        if(k>=0){Expect(std::wcsstr(d.item[k].desc,hudtext::Tr(Tx::roomDescPlayerVehicle))!=nullptr,"its description says where it applies");break;}
        const int next=Find(d,Do::next);
        if(next<0)break;
        Apply(m,nav,d.item[next].choice);
    }
    hudtext::Use(hudtext::Lang::en);
}

// Thousands of random picks / cancels: never out of range, every page fits, every save reads back.
void Walk() {
    rm::Menu m=Fresh();Nav nav;Begin(m,nav);
    unsigned seed=12345;
    const auto rnd=[&](unsigned n){seed=seed*1103515245u+12345u;return n ? (seed>>16)%n : 0u;};
    int saves=0;
    for(int step=0;step<20000;++step) {
        if(!m.open){m=Fresh();Begin(m,nav);}
        const Dialog d=Build(m,nav,Context{});
        Expect(d.count>0 && d.count<=kItemsMost && d.cursor>=0 && d.cursor<d.count,"a page");
        for(int i=0;i<d.count;++i)Expect(d.item[i].desc[0]!=0 && std::wcslen(d.item[i].desc)<kDescMost,"every button described, within its room");
        if(nav.unit<0 || nav.unit>=m.unitCount)Expect(nav.page==Page::units || nav.page==Page::discard,"a unit page has its unit");
        Result r;
        if(rnd(8)==0)r=Cancel(m,nav);
        else r=Apply(m,nav,d.item[rnd(static_cast<unsigned>(d.count))].choice);
        if(r==Result::save) {
            rm::SaveSet set;
            Expect(rm::CollectSave(m,&set),"a save reads back");
            rm::MarkSaved(m);m.open=true;++saves;
        }
        Expect(nav.unit>=0 && nav.unit<m.unitCount,"the unit in range");
        Expect(!m.unit[nav.unit].vehicle.On() || WithinPayload(m.unit[nav.unit].vehicle),"no step leaves a vehicle over its weight");
        if(nav.page!=Page::units && nav.page!=Page::discard) {
            const rm::Unit& u=m.unit[nav.unit];
            if(nav.page==Page::soldier || nav.page==Page::kind || nav.page==Page::primary || nav.page==Page::secondary || nav.page==Page::pack)
                Expect(rm::IsSquad(u) && nav.row>=0 && nav.row<u.squad.count,"a soldier page's soldier exists");
            if(nav.page==Page::pylon || nav.page==Page::store || nav.page==Page::rounds)
                Expect(u.kind==rm::UnitKind::vehicle && nav.row>=1 && nav.row<=u.vehicle.count,"a pylon page's pylon exists");
        }
    }
    Expect(saves>0,"the walk saved");
}
}  // namespace

int main() {
    PackRoundTrip();
    UnitsPage();
    Squad();
    Vehicle();
    JetWeight();
    Descriptions();
    Walk();
    std::printf(failures ? "room_dialog_test: %d of %d FAILED\n" : "room_dialog_test: %d checks passed\n",failures ? failures : checks,checks);
    return failures ? 1 : 0;
}
