// 整备's 支援编成 / 载具 pages (src/support_pages.h): rows and items of the default units, every edit through room_menu.h's
// editors (each one a value its parser takes back), each page saving only its own units, the 3D viewport's hole and
// the squad ring, the mech callouts, the view buttons, the online panel, and the pages inside the framework's Build.
#include "../src/support_pages.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <random>

namespace {
namespace ar=crew::armory;
namespace rm=crew::roommenu;
namespace sp=crew::supportpages;
namespace skin=crew::armory::skin;
using hudtext::Tx;
using crew::SupportWeapon;using crew::VehicleBody;using crew::kMechFields;

int failures=0;
void Expect(bool ok,const char* what) {
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
std::size_t NoIni(void*,const wchar_t*,wchar_t* out,std::size_t) noexcept { out[0]=0; return 0; }

struct Saved { int calls=0,items=0; bool fail=false; bool squadOnly=true,vehicleOnly=true; };
Saved saved;
const rm::Menu* savedMenu=nullptr;
bool FakeSaver(void*,const crew::roomui::SaveItem* items,int n,wchar_t* why,std::size_t cap) noexcept {
    ++saved.calls;saved.items+=n;
    for(int i=0;i<n;++i){
        const rm::Unit& u=savedMenu->unit[items[i].unitIndex];
        if(rm::IsSquad(u))saved.vehicleOnly=false;else saved.squadOnly=false;
        Expect(items[i].value && items[i].unit,"a save item has its unit and value");
    }
    if(saved.fail){ar::CopyText(why,cap,L"disk full");return false;}
    return true;
}

sp::Model model;   // big: static
rm::Menu loaded;

void Fresh(bool previewOn=false,bool online=false) {
    loaded=rm::Menu{};
    rm::Load(loaded,rm::kDefaultUnits,rm::kDefaultUnitCount,&NoIni,nullptr);
    sp::Bind(model,&loaded);
    model.env=sp::Env{};
    model.env.previewOn=previewOn;model.env.online=online;
    model.saver=&FakeSaver;
    savedMenu=&model.menu;
    saved=Saved{};
}
int RowOf(const sp::UnitsPage& page,const wchar_t* key) {
    for(int r=0;r<page.Rows(0);++r){
        const int i=page.UnitIndex(r);
        if(i>=0 && model.menu.unit[i].key && std::wcscmp(model.menu.unit[i].key,key)==0)return r;
    }
    return -1;
}
const rm::Unit& UnitOf(const sp::UnitsPage& page,int row) { return model.menu.unit[page.UnitIndex(row)]; }
ar::Line LineOf(const ar::Page& p,const ar::Pos& at,int column) {
    ar::Line l;
    if(column==1)p.ItemLine(at,&l);else p.FieldLine(at,&l);
    return l;
}

void Rows() {
    Fresh();
    sp::SquadPage squads;sp::VehiclePage vehicles;
    squads.Bind(&model);vehicles.Bind(&model);
    int nSquad=0,nOther=0;
    for(int i=0;i<model.menu.unitCount;++i)(rm::IsSquad(model.menu.unit[i]) ? nSquad : nOther)++;
    Expect(squads.Rows(0)==nSquad && nSquad>0,"the squad page lists every squad");
    Expect(vehicles.Rows(0)==nOther && nOther>0,"the vehicle page lists every vehicle, the mech and the crews");
    Expect(RowOf(vehicles,L"OWN_MECH")>=0,"the mech is in the vehicle list");
    Expect(!squads.Dirty() && !vehicles.Dirty(),"nothing edited yet");
    for(int r=0;r<squads.Rows(0);++r){
        ar::Line l;squads.RowLine(0,r,&l);
        Expect(l.label[0]!=0,"a squad row is named");
    }
}

void SquadEdits() {
    Fresh();
    sp::SquadPage page;page.Bind(&model);
    const int row=RowOf(page,L"SQUAD");
    ar::Pos p{0,row,0,0};
    Expect(page.Items(p)==3,"an empty squad: its own load, add, reset");
    Expect(page.Fields(p)==0,"the own-load line has no fields (confirm adds)");
    Expect(LineOf(page,ar::Pos{0,row,2,0},1).dim,"reset is grey on a stock squad");
    Expect(page.Activate(p),"confirm on the own load adds a soldier");
    Expect(UnitOf(page,row).squad.count==1 && page.Dirty(),"one soldier, the page dirty");
    p.item=0;
    const int values=rm::FieldCount(UnitOf(page,row),0);
    Expect(page.Fields(p)==values+2,"a soldier: its values, copy, remove");
    const SupportWeapon before=UnitOf(page,row).squad.kind[0];
    p.field=0;
    Expect(page.Step(p,1) && UnitOf(page,row).squad.kind[0]!=before,"the kind steps");
    p.field=1;
    page.Step(p,1);
    const ar::Line colour=LineOf(page,p,2);
    Expect(colour.steps && colour.chip!=0,"a colour shows its chip");
    p.field=rm::FieldCount(UnitOf(page,row),0);   // 复制
    Expect(LineOf(page,p,2).acts,"copy acts");
    Expect(page.Activate(p) && UnitOf(page,row).squad.count==2,"copy adds a soldier");
    Expect(UnitOf(page,row).squad.kind[1]==UnitOf(page,row).squad.kind[0],"the copy is the same kind");
    p.field=rm::FieldCount(UnitOf(page,row),0)+1;   // 移除
    Expect(page.Activate(p) && UnitOf(page,row).squad.count==1,"remove takes one away");
    ar::Pos add{0,row,1,0};   // ＋添加士兵 (one soldier: item 1)
    Expect(page.Activate(add) && UnitOf(page,row).squad.count==2,"add soldier");
    ar::Pos reset{0,row,page.Items(p)-1,0};
    Expect(page.Activate(reset) && UnitOf(page,row).squad.count==0,"reset: back to the own load");
    Expect(!page.Dirty(),"reset to what the ini held: nothing to save");
    // fill it: the add line greys at the seats
    for(int i=0;i<20;++i)page.Activate(ar::Pos{0,row,page.Items(ar::Pos{0,row,0,0})-2,0});
    Expect(UnitOf(page,row).squad.count==UnitOf(page,row).seats,"adds stop at the seats");
    Expect(LineOf(page,ar::Pos{0,row,page.Items(p)-2,0},1).dim,"the add line is grey when full");
    Expect(page.SquadTabs(p)==3,"twelve soldiers: three squad tabs");
    ar::Pos click{0,row,0,0};
    const skin::Rect t=sp::SquadTab(2);
    Expect(page.Click(click,(t.x0+t.x1)/2,(t.y0+t.y1)/2) && click.item==8,"the third squad tab selects soldier 9");
    // the same tabs from the keyboard / pad (A/D, right stick): next / previous squad, wrapping; not in the unit list
    const ar::Input none{};
    ar::Pos key{0,row,5,2};
    Expect(page.Key(key,1,ar::kAltNext,none) && key.item==8 && key.field==0,"A/D: soldier 6 -> squad 3's first");
    Expect(page.Key(key,2,ar::kAltNext,none) && key.item==0,"past the last squad: back to the first");
    Expect(page.Key(key,1,ar::kAltPrev,none) && key.item==8,"before the first: the last");
    Expect(!page.Key(key,0,ar::kAltNext,none) && key.item==8,"the unit list: not a squad key");
    Expect(!page.Key(key,1,ar::kJumpNext,none),"other keys are the state's");
    Expect(page.Hint(key,1)!=nullptr && page.Hint(key,0)==nullptr,"the hint says the squad keys where they work");
    Expect(rm::CollectSave(model.menu,&model.set),"every squad edit reads back");
}

void Saving() {
    Fresh();
    sp::SquadPage squads;sp::VehiclePage vehicles;
    squads.Bind(&model);vehicles.Bind(&model);
    const int s=RowOf(squads,L"SQUAD"),v=RowOf(vehicles,L"FIGHTER");
    squads.Activate(ar::Pos{0,s,0,0});
    vehicles.Activate(ar::Pos{0,v,0,0});   // stock -> customised
    Expect(squads.Dirty() && vehicles.Dirty(),"both pages edited");
    wchar_t why[ar::kTextMost]{};
    Expect(squads.Commit(why,ar::kTextMost),"the squad page saves");
    Expect(saved.calls==1 && saved.items==1 && saved.squadOnly,"only the squad page's unit was written");
    Expect(!squads.Dirty() && vehicles.Dirty(),"the vehicle page's edit stays");
    Expect(squads.Commit(why,ar::kTextMost) && saved.calls==1,"nothing to save: the saver not called");
    saved.fail=true;
    Expect(!vehicles.Commit(why,ar::kTextMost),"a failing saver: not committed");
    Expect(std::wcsstr(why,L"disk full")!=nullptr,"the saver's reason is shown");
    Expect(vehicles.Dirty(),"still dirty after a failed save");
    saved.fail=false;saved.squadOnly=saved.vehicleOnly=true;
    Expect(vehicles.Commit(nullptr,0),"a commit without a reason buffer (the screen lost its input)");
    Expect(saved.vehicleOnly && !vehicles.Dirty(),"the vehicle page wrote its own unit");
    // discard
    squads.Activate(ar::Pos{0,s,1,0});
    Expect(squads.Dirty(),"edited again");
    squads.Discard();
    Expect(!squads.Dirty() && UnitOf(squads,s).squad.count==1,"discard: back to what was saved");
    // no saver: refused with a reason
    model.saver=nullptr;
    squads.Activate(ar::Pos{0,s,1,0});
    why[0]=0;
    Expect(!squads.Commit(why,ar::kTextMost) && why[0],"no saver: refused, said why");
}

void Vehicles() {
    Fresh();
    sp::VehiclePage page;page.Bind(&model);
    // a jet
    const int jet=RowOf(page,L"FIGHTER");
    ar::Pos p{0,jet,0,0};
    Expect(sp::VehiclePage::ItemOf(UnitOf(page,jet),0).kind==sp::It::stock,"a stock jet: its stock line first");
    Expect(page.Fields(p)==0 && page.Activate(p) && UnitOf(page,jet).vehicle.On(),"confirm customises it");
    Expect(sp::VehiclePage::ItemOf(UnitOf(page,jet),0).kind==sp::It::gun,"then its guns row");
    const int addAt=page.Items(p)-2;   // ＋添加挂架 before 恢复默认 (after the crew)
    Expect(sp::VehiclePage::ItemOf(UnitOf(page,jet),addAt).kind==sp::It::addPylon,"the add pylon line");
    Expect(page.Activate(ar::Pos{0,jet,addAt,0}) && UnitOf(page,jet).vehicle.count==1,"a pylon added");
    ar::Pos pylon{0,jet,1,0};
    Expect(sp::VehiclePage::ItemOf(UnitOf(page,jet),1).kind==sp::It::pylon && page.Fields(pylon)==4,"a pylon: 挂载 弹数 开火键 移除");
    // the fire keys as this machine binds them (the runtime fills them: armory.cpp FillFireKeys)
    wcscpy_s(model.env.switchKey,L"R / LB");
    wcscpy_s(model.env.ownKey[3],L"X / pad");
    for(auto& k:model.env.storeKey)k=3;   // a jet ignores the stores' own keys (jet_combat.cpp)
    const ar::Line fire=LineOf(page,ar::Pos{0,jet,1,2},2);
    wchar_t want[ar::kTextMost];
    std::swprintf(want,ar::kTextMost,hudtext::Tr(Tx::spFireWithPrimary),L"R / LB");
    Expect(!fire.steps && std::wcscmp(fire.value,want)==0,"a jet's store fires with the main weapon, its switch named");
    for(auto& k:model.env.storeKey)k=0;
    const std::uint8_t store=UnitOf(page,jet).vehicle.store[0];
    page.Step(pylon,1);
    Expect(UnitOf(page,jet).vehicle.store[0]!=store,"the store steps");
    Expect(!page.Step(ar::Pos{0,jet,1,2},1),"the fire key is shown, not stepped");
    Expect(page.Activate(ar::Pos{0,jet,1,3}) && UnitOf(page,jet).vehicle.count==0,"remove takes the pylon off");
    // a tank
    const int tank=RowOf(page,L"TANK_CREWED");
    page.Activate(ar::Pos{0,tank,0,0});
    ar::Pos gun{0,tank,0,0};
    Expect(page.Fields(gun)==1,"the tank's main gun steps");
    const bool ap=UnitOf(page,tank).vehicle.apGun;
    Expect(page.Step(gun,1) && UnitOf(page,tank).vehicle.apGun!=ap,"HE / AP");
    const int tankAdd=page.Items(gun)-2;
    Expect(page.Activate(ar::Pos{0,tank,tankAdd,0}),"rounds added");
    std::swprintf(want,ar::kTextMost,hudtext::Tr(Tx::spFireWithGun),L"R / LB");
    Expect(std::wcscmp(LineOf(page,ar::Pos{0,tank,1,2},2).value,want)==0,"a tank's rounds ride the main gun");
    // a store with its own key (StoreSpec::fireKey 3..6) on the tank: that key, as bound
    model.env.storeKey[UnitOf(page,tank).vehicle.store[0]]=3;
    std::swprintf(want,ar::kTextMost,hudtext::Tr(Tx::spFireOwnKey),3,L"X / pad");
    Expect(std::wcscmp(LineOf(page,ar::Pos{0,tank,1,2},2).value,want)==0,"a store on its own fire key: the key named");
    model.env.storeKey[UnitOf(page,tank).vehicle.store[0]]=2;   // the second trigger: read on the V603 alone
    std::swprintf(want,ar::kTextMost,hudtext::Tr(Tx::spFireWithGun),L"R / LB");
    Expect(std::wcscmp(LineOf(page,ar::Pos{0,tank,1,2},2).value,want)==0,"not an own key on the tank: the main gun's cycle");
    for(auto& k:model.env.storeKey)k=0;
    // the crew row
    const int crewAt=page.Items(gun)-3;
    Expect(sp::VehiclePage::ItemOf(UnitOf(page,tank),crewAt).kind==sp::It::crew,"the crew's colours");
    ar::Pos crewPrimary{0,tank,crewAt,0};
    Expect(page.Step(crewPrimary,1) && UnitOf(page,tank).crew.set,"the crew's colour steps");
    // the mech
    const int mech=RowOf(page,L"OWN_MECH");
    ar::Pos m{0,mech,0,0};
    Expect(page.Items(m)==2,"a stock mech: its stock line and reset");
    page.Activate(m);
    Expect(page.Items(m)==kMechFields+1,"a mech: request, frame, four slots, core, reset");
    for(int r=0;r<kMechFields;++r)
        Expect(sp::VehiclePage::ItemOf(UnitOf(page,mech),r).kind==sp::It::mech && page.Fields(ar::Pos{0,mech,r,0})==1,"a mech row");
    const int frame=UnitOf(page,mech).vehicle.mech.frame;
    page.Step(ar::Pos{0,mech,rm::kMechRowFrame,0},1);
    Expect(UnitOf(page,mech).vehicle.mech.frame!=frame,"the frame steps");
    for(int k=0;k<sp::kMechCallouts;++k){
        ar::Pos c{0,mech,0,0};
        const skin::Rect r=sp::Callout(k);
        Expect(page.Click(c,(r.x0+r.x1)/2,(r.y0+r.y1)/2) && c.item==sp::kCalloutRow[k],"a callout picks its slot");
    }
    Expect(rm::CollectSave(model.menu,&model.set),"every vehicle edit reads back");
    // reset
    const int resetAt=page.Items(m)-1;
    Expect(page.Activate(ar::Pos{0,mech,resetAt,0}) && !UnitOf(page,mech).vehicle.On(),"reset: the stock mech");
}

void Viewport() {
    Fresh(false);
    sp::SquadPage squads;sp::VehiclePage vehicles;
    squads.Bind(&model);vehicles.Bind(&model);
    const int s=RowOf(squads,L"SQUAD");
    squads.Activate(ar::Pos{0,s,0,0});
    skin::Rect hole{};
    Expect(!squads.Hole(ar::Pos{0,s,0,0},&hole),"preview off: no hole");
    model.env.previewOn=true;
    Expect(squads.Hole(ar::Pos{0,s,0,0},&hole) && hole.x0==sp::kView.x0 && hole.y1==sp::kView.y1,"a soldier: the viewport is open");
    Expect(!squads.Hole(ar::Pos{0,s,1,0},&hole),"the add line: no model");
    crew::roompreview::PreviewSpec spec{};
    Expect(squads.PreviewOf(ar::Pos{0,s,0,0},&spec) && spec.leader && spec.body==VehicleBody::none,"the soldier, a leader");
    const int jet=RowOf(vehicles,L"FIGHTER"),tank=RowOf(vehicles,L"TANK_CREWED"),mech=RowOf(vehicles,L"OWN_MECH");
    Expect(vehicles.PreviewOf(ar::Pos{0,jet,0,0},&spec) && spec.body!=VehicleBody::none,"a jet: its unified model");
    Expect(!vehicles.Hole(ar::Pos{0,tank,0,0},&hole),"a tank has no menu model");
    Expect(!vehicles.Hole(ar::Pos{0,mech,0,0},&hole),"the mech has no menu model");
    const int crewAt=vehicles.Items(ar::Pos{0,tank,0,0})-3;   // stock line, crew, add, reset
    Expect(sp::VehiclePage::ItemOf(UnitOf(vehicles,tank),crewAt).kind==sp::It::crew,"the tank's crew row");
    Expect(vehicles.Hole(ar::Pos{0,tank,crewAt,0},&hole),"its crew is a soldier: shown");
    // the reserved tops
    Expect(squads.ColumnTop(1)==sp::kViewTop && ar::detail::TopRows(squads,1)==7,"seven rows over the centre column");
    Expect(ar::detail::LineRect(squads,1,0).y0>=sp::kTabsY1,"the centre rows start under the squad tabs");
    Expect(squads.ColumnTop(2)==0,"offline: no rules panel");
    model.env.online=true;
    Expect(squads.ColumnTop(2)==sp::kPanelTop && ar::detail::LineRect(squads,2,0).y0>=sp::kPanel.y1,"online: the panel over the details");
    // the view buttons, Z / C
    Expect(squads.CameraIndex()==0,"the free view first");
    ar::Pos any{0,s,0,0};
    const skin::Rect b=sp::CamButton(3);
    Expect(squads.Click(any,(b.x0+b.x1)/2,(b.y0+b.y1)/2) && squads.CameraIndex()==3,"a view button picks its view");
    Expect(sp::CamButton(0).y0>=sp::kView.y0 && sp::CamButton(crew::roompreview::kCameraPresetCount-1).x1<=sp::kView.x1,
           "the view buttons inside the viewport");
    squads.Cycle(any,1);squads.Cycle(any,1);
    Expect(squads.CameraIndex()==0,"Z / C cycle the views");
    // the ring: the feet in the viewport, under its centre
    float feet[2];
    sp::FeetOnScreen(squads.Camera(),16.f/9.f,feet);
    Expect(sp::kView.Contains(feet[0],feet[1]) && feet[1]>(sp::kView.y0+sp::kView.y1)/2,"the ring is in the viewport's lower half");
    for(int k=0;k<sp::kMechCallouts;++k){
        const skin::Rect r=sp::Callout(k);
        Expect(r.x0>=sp::kView.x0 && r.x1<=sp::kView.x1 && r.y1<sp::CamButton(0).y0,"a callout inside the viewport, over the buttons");
    }
}

// The pages inside the framework: the hole, the ring, the panel, the unsaved mark.
bool HasText(const ar::Layer& l,const wchar_t* s) {
    for(int i=0;i<l.nt;++i)if(std::wcscmp(l.texts[i].s,s)==0)return true;
    return false;
}
ar::View built;
void InBuild() {
    Fresh(true,true);
    sp::SquadPage squads;sp::VehiclePage vehicles;
    squads.Bind(&model);vehicles.Bind(&model);
    const int s=RowOf(squads,L"SQUAD");
    model.env.rule[squads.UnitIndex(s)]=sp::HostRule{true,false,120,3000};
    model.env.host=false;
    ar::Page* pages[]={&squads,&vehicles};
    ar::State st;ar::Presets ps;ar::Words w;
    ar::Open(st,ar::Input{},0);
    st.pos=ar::Pos{0,s,0,0};
    ar::Line row;squads.RowLine(0,s,&row);
    Expect(row.dim,"online: a unit the host disabled is grey");
    ar::Build(st,pages,2,ps,w,&built);
    const ar::Layer& base=built.layer[0];
    Expect(base.holeQuads==0,"the own-load line: no model, no hole");
    Expect(HasText(base,hudtext::Tr(Tx::spPanelTitle)),"the rules panel");
    Expect(HasText(base,hudtext::Tr(Tx::spGuestRules)),"a guest reads the host's rules");
    Expect(HasText(base,hudtext::Tr(Tx::spOnlySelected)),"the box says the view shows the selected soldier");
    squads.Activate(ar::Pos{0,s,0,0});
    squads.Activate(ar::Pos{0,s,1,0});
    st.column=1;
    ar::Build(st,pages,2,ps,w,&built);
    Expect(base.holeQuads>0 && base.hole.x0==sp::kView.x0,"a soldier: the hole");
    Expect(HasText(base,w.unsaved),"the unsaved mark");
    Expect(HasText(base,hudtext::Tr(Tx::spMineEdited)),"my call: edited");
    int orange=0;
    for(int i=base.holeQuads;i<base.quads.n;++i)if(base.quads.q[i].argb[0]==sp::kRing)++orange;
    Expect(orange>=24,"the floor ring over the 3D");
    Expect(base.quads.dropped==0 && base.droppedTexts==0,"nothing dropped");
    // the vehicle page, a jet with pylons: the hardpoint list
    const int jet=RowOf(vehicles,L"FIGHTER");
    vehicles.Activate(ar::Pos{0,jet,0,0});
    vehicles.Activate(ar::Pos{0,jet,vehicles.Items(ar::Pos{0,jet,0,0})-2,0});
    st.tab=1;st.pos=ar::Pos{0,jet,1,0};st.column=1;
    ar::Build(st,pages,2,ps,w,&built);
    Expect(base.holeQuads>0,"a jet: the hole");
    Expect(HasText(base,hudtext::Tr(Tx::spPylonsOffModel)),"the pylons are said to be off the model");
    // the mech: callouts and no hole
    const int mech=RowOf(vehicles,L"OWN_MECH");
    vehicles.Activate(ar::Pos{0,mech,0,0});
    st.pos=ar::Pos{0,mech,2,0};
    ar::Build(st,pages,2,ps,w,&built);
    Expect(base.holeQuads==0 && HasText(base,hudtext::Tr(Tx::spNoModel)),"the mech: no model, said so");
}

// Random edits on both pages: every state one the parsers take back, the cursor maps never out of range.
void RandomWalk() {
    Fresh(true,true);
    sp::SquadPage squads;sp::VehiclePage vehicles;
    squads.Bind(&model);vehicles.Bind(&model);
    sp::UnitsPage* pages[]={&squads,&vehicles};
    std::mt19937 rng(1124);
    for(int step=0;step<20000;++step){
        sp::UnitsPage& page=*pages[rng()%2];
        ar::Pos p;
        p.row=static_cast<int>(rng()%static_cast<unsigned>(page.Rows(0)));
        const int items=page.Items(p);
        p.item=items>0 ? static_cast<int>(rng()%static_cast<unsigned>(items)) : 0;
        const int fields=page.Fields(p);
        p.field=fields>0 ? static_cast<int>(rng()%static_cast<unsigned>(fields)) : 0;
        ar::Line l;page.ItemLine(p,&l);
        if(fields>0)page.FieldLine(p,&l);
        switch(rng()%4){
        case 0:page.Step(p,1);break;
        case 1:page.Step(p,-1);break;
        default:page.Activate(p);break;
        }
        page.Hole(p,nullptr);
        if(step%500==0)Expect(rm::CollectSave(model.menu,&model.set),"random edits read back");
    }
    Expect(rm::CollectSave(model.menu,&model.set),"random edits read back at the end");
    Expect(squads.Commit(nullptr,0) && vehicles.Commit(nullptr,0) && !squads.Dirty() && !vehicles.Dirty(),"and save");
}

}  // namespace

int main() {
    Rows();
    SquadEdits();
    Saving();
    Vehicles();
    Viewport();
    InBuild();
    RandomWalk();
    if(failures){std::printf("%d failure(s)\n",failures);return 1;}
    std::printf("support_pages: all passed\n");
    return 0;
}
