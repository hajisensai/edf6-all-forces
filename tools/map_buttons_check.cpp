// The map's command card and support bar (src/map_buttons.h) checked offline. The card: laid out at 16:9, 21:9, 4:3 and a narrow split
// screen, with labels of every width the languages give: none overlapping, all on the screen (within the margin),
// in their order (left to right, row by row upwards), each row centred; a button wider than the row shrunk to it;
// a click on a button finds it and only it, a click in the gap between two finds none; no room for a row: fewer rows.
//   cmake --build build --target map_buttons_check && build\map_buttons_check.exe      (exit code 1 on a failure)
#include "../src/map_buttons.h"
#include "../src/mapcmd_cue.h"
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <initializer_list>

namespace {
using namespace mapbtn;
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}

void Layouts() {
    const float screens[][2]={{1920.0f,1080.0f},{2520.0f,1080.0f},{1440.0f,1080.0f},{960.0f,1080.0f},{640.0f,1080.0f}};
    static_assert(kCount==26,"a width for every button");
    const float widths[][kCount]={
        {80,110,90,90,90,110,90,100,90,90,120,160,90,80,110,120,120,110,100,90,80,80,150,190,120,100},     // English-ish
        {70,90,70,70,70,70,70,70,70,70,90,90,80,70,80,80,80,80,70,70,70,70,120,170,100,80},           // Chinese-ish
        {300,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,40,700,150,150,40},           // odd ones: one very long
    };
    for(const auto& sc:screens)for(const auto& w:widths) {
        Rect r[kCount];
        const float margin=16.0f,gap=6.0f,rowH=30.0f,bottom=sc[1]-150.0f;
        const int rows=Flow(w,kCount,sc[0],bottom,rowH,gap,margin,r);
        Check(rows>=1,"at least a row",sc[0]);
        bool on=true,apart=true,order=true,shrunk=true;
        for(int i=0;i<kCount;++i) {
            on=on && r[i].x0>=margin-0.01f && r[i].x1<=sc[0]-margin+0.01f && r[i].y0>=0.0f && r[i].y1<=bottom+0.01f;
            shrunk=shrunk && r[i].x1-r[i].x0<=sc[0]-2.0f*margin+0.01f;
            for(int j=0;j<i;++j) {
                apart=apart && !(r[i].x0<r[j].x1 && r[j].x0<r[i].x1 && r[i].y0<r[j].y1 && r[j].y0<r[i].y1);
                // Order: a later button is on the same row to the right, or on a row above.
                order=order && ((r[i].y0==r[j].y0 && r[i].x0>r[j].x0) || r[i].y0<r[j].y0);
            }
        }
        Check(on,"every button on the screen",sc[0],rows);
        Check(apart,"no two overlapping",sc[0],rows);
        Check(order,"in their order, rows upwards",sc[0],rows);
        Check(shrunk,"none wider than the row",sc[0]);
        // Each row centred: its left and right margins equal.
        for(int i=0;i<kCount;) {
            int k=i;
            while(k+1<kCount && r[k+1].y0==r[i].y0)++k;
            Check(std::fabs(r[i].x0-(sc[0]-r[k].x1))<0.5f,"each row centred",r[i].x0,sc[0]-r[k].x1);
            i=k+1;
        }
        // Hits: the centre of each finds it; the gap after it (same row) finds none.
        for(int i=0;i<kCount;++i) {
            Check(Hit(r,kCount,(r[i].x0+r[i].x1)*0.5f,(r[i].y0+r[i].y1)*0.5f)==i,"a click on a button finds it",i);
            if(i+1<kCount && r[i+1].y0==r[i].y0)
                Check(Hit(r,kCount,r[i].x1+gap*0.5f,(r[i].y0+r[i].y1)*0.5f)==-1,"a click in the gap finds none",i);
        }
        Check(Hit(r,kCount,sc[0]*0.5f,bottom+5.0f)==-1,"under the bar: none");
    }
    // No room: the rows that fit, the rest unplaced.
    float w[kCount];
    for(float& x:w)x=500.0f;
    Rect r[kCount];
    Check(Flow(w,kCount,1920.0f,100.0f,30.0f,6.0f,16.0f,r)==2,"a short screen: as many rows as fit",Flow(w,kCount,1920.0f,100.0f,30.0f,6.0f,16.0f,r));
}
// The card's buttons: their orders, which arm a click, which are shown for a selection.
void Card() {
    using mapcmd::Order;
    Check(OrderOf(Id::move)==Order::move && OrderOf(Id::attackMove)==Order::attackMove && OrderOf(Id::release)==Order::none &&
          OrderOf(Id::recruit)==Order::recruit && OrderOf(Id::sweep)==Order::none,"each order button's order");
    Check(Arms(Id::move) && Arms(Id::attackMove) && Arms(Id::guard) && !Arms(Id::follow) && !Arms(Id::release) && !Arms(Id::sweep),
          "the point orders' buttons arm a click on the map, the others act at once");
    const std::uint32_t vehicle=(1u<<static_cast<unsigned>(Order::none))|(1u<<static_cast<unsigned>(Order::guard))|
                                (1u<<static_cast<unsigned>(Order::move))|(1u<<static_cast<unsigned>(Order::attackMove))|
                                (1u<<static_cast<unsigned>(Order::hold));
    int shown=0,squadOnly=0;
    for(int i=0;i<kCount;++i) {
        const Id b=static_cast<Id>(i);
        shown+=Shown(b,vehicle,false);
        squadOnly+=Shown(b,vehicle,false) && (b==Id::recruit || b==Id::board || b==Id::formation);
    }
    Check(shown==9 && !squadOnly && Shown(Id::guard,vehicle,false) && Shown(Id::smoke,vehicle,false) && !Available(Id::smoke,vehicle) &&
          !Shown(Id::pace,vehicle,false),
          "a vehicle selected: move, attack-move, guard, hold, stop, smoke / decoy (greyed), the sweep and its switch; no squad order or tool",shown);
    // HOLD (H, 2026-10-19): an order button that acts at once (each unit's own spot, no click on the map).
    Check(OrderOf(Id::hold)==Order::hold && !Arms(Id::hold) && OnCard(Id::hold),"HOLD: its button, acting at once");
    // The card cut down (the user, 2026-10-09: "看看还有什么指令能砍一砍…感觉还是有点复杂了"): ENGAGE AT WILL and FOCUS FIRE
    // have no button whatever the selection takes (the right button gives a focus); ALL OUT has one. GUARD is back on
    // the card with Broken Arrow's controls (2026-11-27), arming a click as the point orders do.
    const std::uint32_t every=0xFFFFFFFFu;
    Check(Shown(Id::guard,every,true) && Arms(Id::guard) && !Shown(Id::engage,every,true) && !Shown(Id::focus,every,true),
          "a GUARD button; no ENGAGE / FOCUS button");
    // Broken Arrow's orders: the moves arm a click, the switches act at once; the paced march a squad tool.
    Check(Arms(Id::fastMove) && Arms(Id::reverseMove) && Arms(Id::unloadAt) && !Arms(Id::holdFire) && !Arms(Id::altitude) &&
          !Arms(Id::sprint) && !Arms(Id::smoke) && !Arms(Id::decoy) && OrderOf(Id::holdFire)==Order::holdFire &&
          OrderOf(Id::decoy)==Order::decoy && OrderOf(Id::pace)==Order::none,"Broken Arrow's buttons: the moves arm a click, the rest at once");
    Check(Shown(Id::pace,0u,true) && !Shown(Id::pace,0u,false) && Available(Id::pace,0u),"the paced march's switch: a squad tool");
    Check(Shown(Id::dismountAll,1u<<static_cast<unsigned>(Order::dismountAll),false) && OrderOf(Id::dismountAll)==Order::dismountAll &&
          !Arms(Id::dismountAll) && OrderOf(Id::withdraw)==Order::withdraw,"ALL OUT: its button, acting at once");
    int card=0;
    for(int i=0;i<kCount;++i)card+=OnCard(static_cast<Id>(i));
    Check(card==kCardCount && card==kCount-2,"every button but the two cut is on the card once",card);
    int idle=0;
    for(int i=0;i<kCount;++i)idle+=Shown(static_cast<Id>(i),0u,false);
    Check(idle==2 && Shown(Id::sweep,0u,false) && Shown(Id::health,0u,false),"nothing selected: the sweep and its switch alone",idle);
    Check(Shown(Id::formation,0u,true) && !Shown(Id::formation,0u,false),"squads selected: their formation tool");
}

// The support bar: the catalog's names grouped by what is before their "·" (support_dispatch.cpp SupportCallName order),
// the rows laid out top-down with their chips at the right, a click on a chip its entry, on the row its own.
void Support() {
    const wchar_t* const names[]={L"截击机·守点",L"截击机·跟随",L"对地攻击机·守点",L"对地攻击机·跟随",L"潜水母舰支援",
                                  L"炮舰机·守点",L"炮舰机·跟随",L"步兵小队（4人）",L"步兵大队（12人）",L"坦克·有人",L"坦克·空车交付",
                                  L"装甲运兵车·有人",L"装甲运兵车·空车交付"};
    constexpr int n=static_cast<int>(sizeof(names)/sizeof(names[0]));
    Group g[n];
    const int rows=GroupSupport(names,n,g,n);
    Check(rows==8,"13 entries: 8 kinds of support",rows);
    Check(g[0].first==0 && g[0].count==2 && g[2].first==4 && g[2].count==1 && g[4].count==1 && g[5].count==1 && g[7].first==11 && g[7].count==2,
          "pairs by their name before the dot; a name with none alone");
    Check(BaseLength(names[0])==3 && std::wcscmp(VariantOf(names[1]),L"跟随")==0 && !VariantOf(names[4]),"base and variant words");
    const wchar_t* const lone[]={L"甲",L"甲"};   // no dot: never merged even when equal
    Group lg[2];
    Check(GroupSupport(lone,2,lg,2)==2,"names with no dot are rows of their own");
    Rect row[n]{},chip[n]{};
    const float x0=12.0f,x1=250.0f,top=300.0f,rowH=26.0f,gap=3.0f,inset=4.0f;
    const int placed=Column(g,rows,x0,x1,top,1000.0f,rowH,gap,inset,row,chip);
    Check(placed==rows,"every row placed with room",placed);
    bool apart=true,inside=true,order=true;
    for(int r=0;r<placed;++r) {
        inside=inside && row[r].x0==x0 && row[r].x1==x1 && row[r].y1-row[r].y0==rowH;
        if(r)order=order && row[r].y0>=row[r-1].y1+gap-0.01f;
        for(int e=g[r].first;e<g[r].first+g[r].count;++e) {
            inside=inside && chip[e].x0>=row[r].x0 && chip[e].x1<=row[r].x1 && chip[e].y0>=row[r].y0 && chip[e].y1<=row[r].y1;
            for(int f=g[r].first;f<e;++f)apart=apart && !(chip[e].x0<chip[f].x1 && chip[f].x0<chip[e].x1);
        }
    }
    Check(inside,"rows full width, chips inside their rows");Check(apart,"a row's chips apart");Check(order,"rows top-down, a gap apart");
    // The hit list as hud.cpp hands it over: the chips first, then each row's part left of its chips.
    Rect hits[2*n];int entry[2*n],k=0;
    for(int r=0;r<placed;++r)if(g[r].count>1)for(int e=g[r].first;e<g[r].first+g[r].count;++e){hits[k]=chip[e];entry[k++]=e;}
    for(int r=0;r<placed;++r){Rect body=row[r];if(g[r].count>1)body.x1=chip[g[r].first].x0-4.0f;hits[k]=body;entry[k++]=g[r].first;}
    const int onChip=Hit(hits,k,(chip[1].x0+chip[1].x1)*0.5f,(chip[1].y0+chip[1].y1)*0.5f);
    Check(onChip>=0 && entry[onChip]==1,"a click on the follow chip of the first row: that entry");
    const int onRow=Hit(hits,k,x0+20.0f,(row[2].y0+row[2].y1)*0.5f);
    Check(onRow>=0 && entry[onRow]==4,"a click on a lone row: its entry");
    Check(Hit(hits,k,x0+20.0f,row[0].y1+gap*0.5f)<0,"a click in the gap between rows: none");
    // Not enough room: the rows that fit.
    Check(Column(g,rows,x0,x1,top,top+3.0f*(rowH+gap),rowH,gap,inset,row,chip)==3,"a short column: the rows that fit");
    // A tab longer than its column (2026-11-27: every flown support in one tab): the window the wheel scrolls, kept
    // inside the rows, moved the least to show the row [ ] picked.
    Check(ColumnFit(0.0f,29.0f*3.0f,26.0f,3.0f)==3 && ColumnFit(0.0f,25.0f,26.0f,3.0f)==0 && ColumnFit(0.0f,26.0f,26.0f,3.0f)==1,
          "a column's rows that fit");
    Check(ColumnWindow(20,6,-3,-1)==0 && ColumnWindow(20,6,99,-1)==14 && ColumnWindow(4,6,2,-1)==0,"the window inside the rows");
    Check(ColumnWindow(20,6,0,9)==4 && ColumnWindow(20,6,12,9)==9 && ColumnWindow(20,6,5,7)==5,"the window moved the least to show a pick");
    // The bar's tabs in its hit list: codes below every entry and below -1 (none), each tab back from its code.
    Check(SupportTabHit(0)<-1 && SupportTabOfHit(SupportTabHit(3))==3 && SupportTabOfHit(-1)<0 && SupportTabOfHit(5)<0,
          "a tab's hit code, never an entry's or none");
}
// The formation menu (the user, 2026-10-09: "这个编队应该点击以后展开选择里面的东西"): its entries' codes, its column over
// the button (under it with no room above), on the screen.
void Menu() {
    Check(MenuGuard(MenuEntry(true,10)) && MenuShape(MenuEntry(true,10))==10 && !MenuGuard(MenuEntry(false,3)) &&
          MenuShape(MenuEntry(false,3))==3,"a menu entry: defence or march, and its shape");
    const Rect button{900.0f,940.0f,1000.0f,970.0f};
    Rect r[14]{};
    const int placed=MenuColumn(button,14,180.0f,26.0f,2.0f,1920.0f,1080.0f,r);
    bool above=placed==14,apart=true;
    for(int i=0;i<placed;++i) {
        above=above && r[i].y1<=button.y0 && r[i].x0==button.x0 && r[i].x1-r[i].x0==180.0f;
        if(i)apart=apart && r[i].y0>=r[i-1].y1;
    }
    Check(above && apart && r[13].y1==button.y0-2.0f,"14 rows over the button, top-down, its bottom row on the button");
    const Rect high{1850.0f,20.0f,1910.0f,50.0f};
    const int below=MenuColumn(high,4,180.0f,26.0f,2.0f,1920.0f,1080.0f,r);
    Check(below==4 && r[0].y0==high.y1+2.0f && r[0].x1<=1920.0f,"no room above: under the button, kept on the screen");
    Check(MenuColumn(button,0,180.0f,26.0f,2.0f,1920.0f,1080.0f,r)==0,"no entries: no column");
}
}  // namespace

// The support composition panel (the user, 2026-10-09: "支援栏是断剑那种，先点载具，然后选里面的人并且可以点多次，直到座位满";
// a click a whole squad): ComposeApply adds a squad of four of a kind while one fits and takes a squad out; ComposePanel
// lays its squad places, the kinds a class a row and the hint inside its box, apart, within the ceiling and the bottom.
void Compose() {
    using crew::SupportLoadout;using crew::SupportWeapon;
    SupportLoadout l{};
    Check(ComposeApply(l,12,static_cast<int>(SupportWeapon::wingLance)) && l.count==4 && l.soldier[0]==SupportWeapon::wingLance &&
          l.soldier[3]==SupportWeapon::wingLance,"a kind's click: a whole squad of it");
    Check(ComposeApply(l,12,static_cast<int>(SupportWeapon::fencerCannon)) && ComposeApply(l,12,0) && l.count==12,"three squads fill twelve seats");
    Check(!ComposeRoom(l,12) && !ComposeApply(l,12,0) && l.count==12,"no room for a fourth: refused, nothing changes");
    Check(ComposeSquads(l)==3 && ComposeSquadKind(l,1)==SupportWeapon::fencerCannon,"three squads, the second the Fencers");
    Check(ComposeApply(l,12,ComposeSquadCode(0)) && l.count==8 && l.soldier[0]==SupportWeapon::fencerCannon &&
          l.soldier[4]==SupportWeapon::rifle,"taking the first squad out moves the others up");
    Check(!ComposeApply(l,12,ComposeSquadCode(2)) && !ComposeApply(l,12,crew::kSupportWeaponCount),"no third squad now; no such kind");
    SupportLoadout apc{};
    Check(ComposeApply(apc,4,0) && !ComposeRoom(apc,4) && !ComposeApply(apc,4,1),"an APC's four seats: one squad");
    SupportLoadout tiny{};
    Check(!ComposeApply(tiny,3,0) && tiny.count==0,"three seats hold no squad");
    // A partial squad (an odd preset) is taken out whole.
    SupportLoadout odd{};odd.count=6;
    Check(ComposeSquads(odd)==2 && ComposeApply(odd,12,ComposeSquadCode(1)) && odd.count==4,"a partial second squad taken out whole");
    int ranger[16],wing[16],fencer[16];
    // Without the X-Morph model pack (support_call.h SupportKindInstalled false): not in the Rangers' row, its click refused.
    crew::SetSupportKindInstalled(SupportWeapon::xmInfantry,false);
    {
        SupportLoadout none{};
        Check(ComposeKinds(0,ranger,16)==5 && ranger[4]==static_cast<int>(SupportWeapon::sniper) && ComposeKinds(1,wing,16)==5 &&
              ComposeKinds(2,fencer,16)==4,"not installed: the five Rangers alone, the others as they are");
        Check(!ComposeApply(none,12,static_cast<int>(SupportWeapon::xmInfantry)) && none.count==0,"not installed: its squad never added");
        const ComposeLayout L=ComposePanel(250.0f,120.0f,100.0f,900.0f,12,1.0f);
        bool shown=false;
        for(int k=0;k<L.kinds;++k)shown=shown || L.kindCode[k]==static_cast<int>(SupportWeapon::xmInfantry);
        Check(L.kinds==crew::kSupportWeaponCount-1 && !shown,"not installed: the panel draws every kind but it");
    }
    crew::SetSupportKindInstalled(SupportWeapon::xmInfantry,true);   // installed: the rest below
    Check(ComposeKinds(0,ranger,16)==6 && ComposeKinds(1,wing,16)==5 && ComposeKinds(2,fencer,16)==4 &&
          ranger[5]==static_cast<int>(SupportWeapon::xmInfantry) &&
          wing[0]==static_cast<int>(SupportWeapon::wingLance) && fencer[3]==static_cast<int>(SupportWeapon::fencerShotgun),
          "the rows: five Rangers and the X-Morph infantry, five Wing Divers, four Fencers");
    Check(kComposeKindsMost==6,"the panel's width fits the longest row");
    for(float sc:{0.75f,1.0f,1.5f,2.0f})for(int seats:{4,12}) {
        const float ceiling=100.0f*sc,bottom=900.0f*sc;
        for(float top:{120.0f*sc,880.0f*sc}) {
            const ComposeLayout L=ComposePanel(250.0f*sc,top,ceiling,bottom,seats,sc);
            Check(L.box.y0>=ceiling-0.01f && L.box.y1<=bottom+0.01f,"the panel within the ceiling and the bottom",sc,seats);
            Check(L.slots==seats/kSquad && L.kinds==crew::kSupportWeaponCount,"a place a squad, every kind",L.slots,L.kinds);
            Rect all[kComposeSquadsMost+crew::kSupportWeaponCount+2];int n=0;
            for(int j=0;j<L.slots;++j)all[n++]=L.slot[j];
            for(int k=0;k<L.kinds;++k)all[n++]=L.kind[k];
            all[n++]=L.title;all[n++]=L.hint;
            bool inside=true,apart=true;
            for(int i=0;i<n;++i) {
                inside=inside && all[i].x0>=L.box.x0-0.01f && all[i].x1<=L.box.x1+0.01f && all[i].y0>=L.box.y0-0.01f && all[i].y1<=L.box.y1+0.01f;
                for(int j=i+1;j<n;++j)
                    apart=apart && (all[i].x1<=all[j].x0+0.01f || all[j].x1<=all[i].x0+0.01f || all[i].y1<=all[j].y0+0.01f || all[j].y1<=all[i].y0+0.01f);
            }
            Check(inside,"every place, kind, title and hint inside the box",sc,seats);
            Check(apart,"none of them overlapping",sc,seats);
            for(int k=0;k<L.kinds;++k)Check(Hit(L.kind,L.kinds,(L.kind[k].x0+L.kind[k].x1)*0.5f,(L.kind[k].y0+L.kind[k].y1)*0.5f)==k,"a click on a kind finds it",k);
            for(int c=0;c<kComposeClasses;++c)
                Check(L.classLabel[c].x1<=L.kind[0].x0+0.01f,"the class names left of the kinds",c);
        }
    }
}
// The order cues and the selection's altitude (mapcmd_cue.h): a refused order the low cue whatever it was, ALTITUDE up /
// down by its way, any other the short one; the button's state from the counts, another machine's alone unknown; V up
// unless every one this machine knows is high; a switched tag blinks, then stays lit.
void Cues() {
    using namespace mapcue;
    Check(ForOrder(false,false,0)==Cue::refuse && ForOrder(true,true,0)==Cue::refuse,"none took it: refused");
    Check(ForOrder(true,true,2)==Cue::up && ForOrder(true,false,1)==Cue::down,"altitude up / down");
    Check(ForOrder(false,true,3)==Cue::ack,"another order: the short cue");
    Check(StateOf({})==AltState::none,"no aircraft: none");
    Check(StateOf({2,0,0})==AltState::high && StateOf({0,1,0})==AltState::low,"all high / all low");
    Check(StateOf({1,1,0})==AltState::mixed && StateOf({1,1,3})==AltState::mixed,"mixed");
    Check(StateOf({0,0,2})==AltState::unknown && StateOf({1,0,2})==AltState::high,"another machine's alone: unknown");
    Check(VGoesHigh({0,1,0}) && VGoesHigh({1,1,0}) && !VGoesHigh({2,0,0}),"V: up unless all high");
    Check(!VGoesHigh({1,0,4}),"V: another machine's (not given it) do not keep it up");
    Check(VGoesHigh({0,0,2}),"V: none known: up");
    Check(TagLit(0) && !TagLit(kBlinkMs) && TagLit(2*kBlinkMs),"a switched tag blinks");
    Check(TagLit(kSwitchMs) && TagLit(0xFFFFFFFFu),"then stays lit");
}
// Broken Arrow's action grid and selection cards (Grid, Cards, GroupCards).
void Broken() {
    Rect r[32]{};
    // 25 cells 110 wide on 1920, the right 1500 px: 13 a row, two rows, right-aligned, the top row filled first.
    const int rows=Grid(25,110.0f,1920.0f,1526.0f/1920.0f,938.0f,30.0f,6.0f,16.0f,r);
    Check(rows==2,"grid: 25 in two rows",rows);
    Check(std::fabs(r[12].x1-(1920.0f-16.0f))<0.01f,"grid: the top row's last cell on the right margin",r[12].x1);
    Check(std::fabs(r[0].y1-(938.0f-36.0f))<0.01f && std::fabs(r[13].y1-938.0f)<0.01f,"grid: the top row first, the bottom row on `bottom`",r[0].y1,r[13].y1);
    Check(std::fabs(r[13].x0-r[0].x0)<0.01f,"grid: the rows' columns line up",r[13].x0,r[0].x0);
    bool apart=true,inside=true;
    for(int i=0;i<25;++i) {
        inside=inside && r[i].x0>=1920.0f-1526.0f-0.01f && r[i].x1<=1904.01f;
        for(int j=0;j<i;++j)apart=apart && !(r[i].x0<r[j].x1 && r[j].x0<r[i].x1 && r[i].y0<r[j].y1 && r[j].y0<r[i].y1);
    }
    Check(apart,"grid: no two cells overlap");
    Check(inside,"grid: within its share");
    Check(Hit(r,25,r[7].x0+1.0f,r[7].y0+1.0f)==7 && Hit(r,25,r[7].x1+3.0f,r[7].y0+1.0f)<0,"grid: a click finds its cell, the gap none");
    Check(Grid(25,110.0f,1920.0f,1526.0f/1920.0f,60.0f,30.0f,6.0f,16.0f,r)==1,"grid: no room for two rows: one",0);
    Check(Grid(0,110.0f,1920.0f,1.0f,938.0f,30.0f,6.0f,16.0f,r)==0,"grid: none");
    Check(Grid(3,110.0f,1920.0f,1.0f,938.0f,30.0f,6.0f,16.0f,r)==1 && std::fabs(r[2].x1-1904.0f)<0.01f,"grid: a few in one row, on the right");
    // Cards: left to right, the bottom row first, no higher than `top`.
    Rect c[8]{};
    const int placed=Cards(8,176.0f,66.0f,16.0f,400.0f,860.0f,938.0f,6.0f,c);
    Check(placed==2,"cards: two fit in one row of 66 under 78 px",placed);
    Check(std::fabs(c[0].x0-16.0f)<0.01f && std::fabs(c[1].x0-198.0f)<0.01f && std::fabs(c[0].y1-938.0f)<0.01f,"cards: from the left, on the bottom");
    Check(Cards(5,176.0f,30.0f,16.0f,400.0f,800.0f,938.0f,6.0f,c)==5 && c[2].y1<c[0].y0,"cards: a second row above the first");
    Check(Cards(3,176.0f,30.0f,16.0f,100.0f,800.0f,938.0f,6.0f,c)==0,"cards: narrower than one: none");
    // GroupCards: alike and of one owner stacked, in the order first seen.
    const char* kind[]={"Ranger x4","Ranger x2","Fencer x2","Ranger x6","Nix"};
    const int owner[]={0,0,0,1,0};
    CardGroup g[8]{};
    const int groups=GroupCards(kind,owner,5,g,8);
    Check(groups==4,"groups: rangers of one owner stacked, another owner's apart",groups);
    Check(g[0].first==0 && g[0].count==2 && g[1].first==2 && g[2].first==3 && g[3].first==4,"groups: in the order first seen");
    Check(GroupCards(kind,owner,5,g,2)==2,"groups: at most `most`");
}

int main() {
    Layouts();Card();Support();Menu();Compose();Cues();Broken();
    std::printf(failures ? "map_buttons_check: %d of %d FAILED\n" : "map_buttons_check: all %d ok\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
