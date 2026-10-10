// The map's command card and support bar (src/map_buttons.h) checked offline. The card: laid out at 16:9, 21:9, 4:3 and a narrow split
// screen, with labels of every width the languages give: none overlapping, all on the screen (within the margin),
// in their order (left to right, row by row upwards), each row centred; a button wider than the row shrunk to it;
// a click on a button finds it and only it, a click in the gap between two finds none; no room for a row: fewer rows.
//   cmake --build build --target map_buttons_check && build\map_buttons_check.exe      (exit code 1 on a failure)
#include "../src/map_buttons.h"
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
    const float widths[][kCount]={
        {80,110,90,90,90,110,90,100,90,90,120,160,90,80,80,150,190,120},     // English-ish
        {70,90,70,70,70,70,70,70,70,70,90,90,80,70,70,120,170,100},         // Chinese-ish
        {300,40,40,40,40,40,40,40,40,40,40,40,40,40,40,700,150,150},        // odd ones: one very long
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
                                (1u<<static_cast<unsigned>(Order::move))|(1u<<static_cast<unsigned>(Order::attackMove));
    int shown=0,squadOnly=0;
    for(int i=0;i<kCount;++i) {
        const Id b=static_cast<Id>(i);
        shown+=Shown(b,vehicle,false);
        squadOnly+=Shown(b,vehicle,false) && (b==Id::recruit || b==Id::board || b==Id::formation || b==Id::split);
    }
    Check(shown==5 && !squadOnly,"a vehicle selected: move, attack-move, stop, the sweep and its switch; no squad order or tool",shown);
    // The card cut down (the user, 2026-10-09: "看看还有什么指令能砍一砍…感觉还是有点复杂了"): GUARD, ENGAGE AT WILL and
    // FOCUS FIRE have no button whatever the selection takes (G / the right button / H give them); ALL OUT has one.
    const std::uint32_t every=0xFFFFFFFFu;
    Check(!Shown(Id::guard,every,true) && !Shown(Id::engage,every,true) && !Shown(Id::focus,every,true),
          "no GUARD / ENGAGE / FOCUS button");
    Check(Shown(Id::dismountAll,1u<<static_cast<unsigned>(Order::dismountAll),false) && OrderOf(Id::dismountAll)==Order::dismountAll &&
          !Arms(Id::dismountAll) && OrderOf(Id::withdraw)==Order::withdraw,"ALL OUT: its button, acting at once");
    int card=0;
    for(int i=0;i<kCount;++i)card+=OnCard(static_cast<Id>(i));
    Check(card==kCardCount && card==kCount-3,"every button but the three cut is on the card once",card);
    int idle=0;
    for(int i=0;i<kCount;++i)idle+=Shown(static_cast<Id>(i),0u,false);
    Check(idle==2 && Shown(Id::sweep,0u,false) && Shown(Id::health,0u,false),"nothing selected: the sweep and its switch alone",idle);
    Check(Shown(Id::formation,0u,true) && Shown(Id::merge,0u,true),"squads selected: their tools");
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
    Check(ComposeKinds(0,ranger,16)==5 && ComposeKinds(1,wing,16)==5 && ComposeKinds(2,fencer,16)==4 &&
          wing[0]==static_cast<int>(SupportWeapon::wingLance) && fencer[3]==static_cast<int>(SupportWeapon::fencerShotgun),
          "the rows: five Rangers, five Wing Divers, four Fencers");
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
int main() {
    Layouts();Card();Support();Menu();Compose();
    std::printf(failures ? "map_buttons_check: %d of %d FAILED\n" : "map_buttons_check: all %d ok\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
