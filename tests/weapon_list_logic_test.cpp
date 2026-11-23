// 武器页纯逻辑（src/weapon_list_logic.h）与生成数据（src/weapon_series.inc）离线测试：生成数据自洽（系列连续、按等级排、
// 槽位与 SoldierInit 规则一致、名字齐全）；小表上的折叠 / 展开、持有 / 上限 / 重复 / 槽位判定、搜索（全半角、大小写）、
// 来源与收藏筛选、光标移动与重建后归位、收藏读写、属性对比；以及在整份生成数据上随机筛选后列表的不变式。
#include "../src/weapon_list_logic.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <random>
#include <string>
#include <tuple>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
using namespace crew::wlist;

void Generated() {
    std::set<std::wstring> ids;
    std::map<std::pair<int,int>,const WeaponSlotRule*> rules;
    for(const auto& r:kWeaponSlotRules)rules[{r.cls,r.slot}]=&r;
    Check(rules.size()==std::size(kWeaponSlotRules),"slot rules unique");
    Check(std::size(kWeaponRows)>kWeaponSeriesStockRows,"generated rows include calls and ports");
    std::size_t next=0;
    for(std::size_t s=0;s<std::size(kWeaponSeries);++s){
        const auto& ser=kWeaponSeries[s];
        Check(ser.first==next && ser.count>0,"series contiguous and non-empty");
        next+=ser.count;
        for(int k=0;k<kNameLangs;++k)Check(ser.label[k] && *ser.label[k],"series label in every language");
        int last=-1;
        for(std::size_t i=ser.first;i<ser.first+ser.count;++i){
            const auto& r=kWeaponRows[i];
            Check(r.series==s && r.source==ser.source && r.cls==ser.cls && r.category==ser.category,"row matches series");
            Check(r.level>=last,"series rows by level");
            last=r.level;
            Check(ids.insert(r.id).second,"unique WEAPONTABLE names");
            for(int k=0;k<kNameLangs;++k)Check(r.name[k] && *r.name[k],"row name in every language");
            int mask=0;
            for(int slot=0;slot<kSlotsMost;++slot){
                const auto it=rules.find({r.cls,slot});
                if(it!=rules.end() && Catalog::Takes(*it->second,r.category))mask|=1<<slot;
            }
            Check(mask!=0 && mask==r.slots,"row slots are the SoldierInit rule's");
        }
        if(s)Check(std::make_tuple(kWeaponSeries[s-1].source,kWeaponSeries[s-1].cls,kWeaponSeries[s-1].category)<=
                   std::make_tuple(ser.source,ser.cls,ser.category),"series ordered by source, class, category");
    }
    Check(next==std::size(kWeaponRows),"series cover every row");
    std::set<int> idx;
    for(const auto& r:kWeaponRows)idx.insert(r.index);
    Check(idx.size()==std::size(kWeaponRows) && *idx.begin()==0 && *idx.rbegin()==int(std::size(kWeaponRows))-1,
          "table indices are 0..n-1");
    Check(ids.count(L"AssultRifle01") && ids.count(L"aWeapon001"),"stock rows present");
    // Fencer 主武器槽可重复，其余不可（SoldierInit 第 4 项）。
    Check(rules[{3,0}]->allowDuplicate && !rules[{3,4}]->allowDuplicate && !rules[{0,0}]->allowDuplicate,"dup rules");
}

// 小表：Ranger 突击步枪（类别 0）两个系列 + 火箭（类别 3）一个，EDF5 一把，空投（类别 20）一个。
const WeaponRowInfo kRows[]={
    {L"A1",0,0,0,0,5,0,3,0,{L"Stork 1",L"鹳鸟１",L"鸛鳥１",L"ストーク１"}},
    {L"A2",1,0,0,0,10,0,3,0,{L"Stork 2",L"鹳鸟２",L"鸛鳥２",L"ストーク２"}},
    {L"A3",2,0,0,0,30,0,3,0,{L"Stork 3",L"鹳鸟３",L"鸛鳥３",L"ストーク３"}},
    {L"B1",3,0,0,0,8,1,3,0,{L"PA-11",L"ＰＡ－１１",L"ＰＡ－１１",L"ＰＡ－１１"}},
    {L"R1",4,0,0,3,12,2,3,0,{L"Goliath",L"歌利亚",L"歌利亞",L"ゴリアス"}},
    {L"P1",5,1,0,0,20,3,3,0,{L"Slade",L"史莱德",L"史萊德",L"スレイド"}},
    {L"S1",6,0,0,20,1,4,4,0,{L"Drop",L"空投",L"空投",L"ドロップ"}},
    {L"GONE",7,0,0,0,1,5,3,0,{L"Gone",L"无",L"無",L"なし"}},
};
const WeaponSeriesInfo kSeries[]={
    {0,0,0,0,3,{L"Stork",L"鹳鸟",L"鸛鳥",L"ストーク"}},
    {0,0,0,3,1,{L"PA-11",L"ＰＡ－１１",L"ＰＡ－１１",L"ＰＡ－１１"}},
    {0,0,3,4,1,{L"Goliath",L"歌利亚",L"歌利亞",L"ゴリアス"}},
    {1,0,0,5,1,{L"Slade",L"史莱德",L"史萊德",L"スレイド"}},
    {0,0,20,6,1,{L"Drop",L"空投",L"空投",L"ドロップ"}},
    {0,0,0,7,1,{L"Gone",L"无",L"無",L"なし"}},
};
const WeaponSlotRule kRules[]={
    {0,0,false,{0,1,2,3,-1,-1,-1,-1}},
    {0,1,false,{0,1,2,3,-1,-1,-1,-1}},
    {0,2,false,{20,-1,-1,-1,-1,-1,-1,-1}},
    {3,0,true,{200,-1,-1,-1,-1,-1,-1,-1}},
    {3,1,true,{200,-1,-1,-1,-1,-1,-1,-1}},
};

Catalog Small() {
    // 运行时行号与生成下标不同（表里先有别的 mod 行）；GONE 不在表里。
    return Catalog::Build(kRows,std::size(kRows),kSeries,std::size(kSeries),kRules,std::size(kRules),
                          [](const wchar_t* id)->int{
                              const std::wstring s(id);
                              if(s==L"GONE")return -1;
                              for(int i=0;i<int(std::size(kRows));++i)
                                  if(s==kRows[i].id)return 100+i;
                              return -1;
                          });
}

void Lists() {
    Catalog c=Small();
    Check(c.entries.size()==7 && !c.ByRow(107),"rows absent from the table are not listed");
    std::set<int> owned{100,101,103,104,105,106};
    Loadout l;
    l.owned=[&](int row){return owned.count(row)!=0;};
    l.equipped={101,103,106,-1,-1,-1};
    Filter f;
    ExpandState x;
    auto lines=BuildList(c,l,f,x);
    // Stork（2 把持有：1、2）折叠成一行；PA-11、Goliath、Slade 单行；空投不在槽 0。
    Check(lines.size()==4,"folded list");
    Check(lines[0].kind==LineKind::series && lines[0].shown==2 && lines[0].owned==2 && lines[0].levelMin==5 &&
          lines[0].levelMax==10 && lines[0].equippedHere && !lines[0].expanded,"series header");
    Check(lines[1].kind==LineKind::weapon && lines[1].row==103 && lines[1].verdict==Verdict::duplicate,"dup elsewhere");
    Check(lines[2].row==104 && lines[2].verdict==Verdict::ok,"rocket ok");
    Check(lines[3].row==105 && c.entries[lines[3].entry].source==kSourceEdf5,"EDF5 row after EDF6 series");
    f.showUnowned=true;
    lines=BuildList(c,l,f,x);
    Check(lines[0].shown==3 && lines[0].owned==2 && lines[0].levelMax==30,"unowned shown");
    x.Toggle(lines[0].series);
    lines=BuildList(c,l,f,x);
    Check(lines.size()==7 && lines[0].expanded && lines[1].kind==LineKind::child && lines[3].row==102,"expanded");
    Check(lines[3].verdict==Verdict::notOwned && lines[2].verdict==Verdict::same && lines[1].verdict==Verdict::ok,
          "child verdicts");
    owned.insert(102);
    l.levelCap=20;
    Check(CheckEquip(c,l,0,0,102)==Verdict::capped,"level cap");
    Check(CheckEquip(c,l,0,0,105)==Verdict::ok && CheckEquip(c,l,0,0,104)==Verdict::ok,"cap is inclusive");
    l.levelCap=-1;
    Check(CheckEquip(c,l,0,0,102)==Verdict::ok,"no cap");
    Check(CheckEquip(c,l,0,2,100)==Verdict::wrongSlot && CheckEquip(c,l,0,0,106)==Verdict::wrongSlot,"wrong slot");
    Check(CheckEquip(c,l,3,0,100)==Verdict::wrongSlot && CheckEquip(c,l,0,0,999)==Verdict::absent,"class / absent");
    Check(CheckEquip(c,l,0,1,103)==Verdict::same && CheckEquip(c,l,0,1,101)==Verdict::duplicate,"same / dup");

    // 来源、类别筛选。
    f.sources=1u<<kSourceEdf5;
    lines=BuildList(c,l,f,x);
    Check(lines.size()==1 && lines[0].row==105,"source filter");
    f.sources=0xF;f.category=3;
    lines=BuildList(c,l,f,x);
    Check(lines.size()==1 && lines[0].row==104,"category filter");
    f.category=-1;

    // 搜索：中文名、全角半角、大小写；命中系列名时全部成员可见并展开。
    x=ExpandState();
    f.lang=1;f.search=L"pa11";
    lines=BuildList(c,l,f,x);
    Check(lines.size()==1 && lines[0].row==103,"search folds width, case and hyphen");
    f.search=L"鹳鸟";
    lines=BuildList(c,l,f,x);
    Check(lines.size()==4 && lines[0].expanded && lines[0].shown==3,"search on series label expands");
    f.search=L"３";
    lines=BuildList(c,l,f,x);
    Check(lines.size()==1 && lines[0].kind==LineKind::weapon && lines[0].row==102,"one hit in a series is a plain row");
    f.lang=3;f.search=L"すとーく";
    lines=BuildList(c,l,f,x);
    Check(!lines.empty() && lines[0].shown==3,"katakana matches hiragana");
    f.lang=0;f.search=L"zzz";
    Check(BuildList(c,l,f,x).empty(),"no hit");
    f.search.clear();

    // 收藏。
    ToggleFavourite(&l.favourites,L"A3");
    f.favouritesOnly=true;
    lines=BuildList(c,l,f,x);
    Check(lines.size()==1 && lines[0].row==102 && lines[0].favourite,"favourites only");
    f.favouritesOnly=false;
    std::set<std::wstring> favs;
    Check(ParseFavourites(" aWeapon001 ,, AssultRifle01",&favs) && favs.size()==2 && favs.count(L"aWeapon001"),"parse");
    Check(SerializeFavourites(favs)=="AssultRifle01,aWeapon001","serialize sorted");
    Check(!ParseFavourites("ok,bad name",&favs) && favs.size()==2,"bad favourites refused whole");
    Check(ParseFavourites("",&favs) && favs.empty(),"empty favourites");

    // 光标：移动、折叠后回到系列行、展开后回到同一武器。
    x=ExpandState();
    x.Open(0);
    lines=BuildList(c,l,f,x);
    const int at=FindRowLine(lines,101);
    Check(at==2 && lines[at].kind==LineKind::child,"cursor on equipped child");
    Check(MoveCursor(lines,0,-1,true)==int(lines.size())-1 && MoveCursor(lines,0,-1,false)==0,"move wrap / clamp");
    const LineKey key=KeyOf(lines[at]);
    x.Close(0);
    auto folded=BuildList(c,l,f,x);
    Check(RelocateCursor(folded,key,at)==0,"collapsed: cursor on the series");
    Check(FindRowLine(folded,101)==0,"equipped row inside a folded series");
    f.search=L"Stork 2";
    auto searched=BuildList(c,l,f,x);
    Check(RelocateCursor(searched,key,at)==0 && searched[0].kind==LineKind::weapon,"same weapon as a plain row");
    f.search.clear();
    Check(SeriesOfRow(c,101)==0u && !SeriesOfRow(c,5),"series of a row");
    Check(RelocateCursor({},key,3)==-1,"empty list");

    // 运行时额外行。
    f.showUnowned=false;
    Check(c.AddExtra(50,3,40,L"Mod Rocket") && !c.AddExtra(50,3,1,L"x") && !c.AddExtra(51,999,1,L"x"),"extra rows");
    f.category=3;
    lines=BuildList(c,l,f,x);
    Check(lines.size()==1,"extra row unowned hidden");
    owned.insert(50);
    lines=BuildList(c,l,f,x);
    Check(lines.size()==2 && lines[1].row==50 && c.entries[lines[1].entry].Name(2)==L"Mod Rocket","extra row listed");
    f.category=-1;
}

// 写入前的判定（weapon_equip.cpp 用的那一个），用生成的 SoldierInit 规则。
void RawRows() {
    const std::array<int,kSlotsMost> eq{10,11,12,13,14,15};
    const WeaponSlotRule* r=kWeaponSlotRules;
    const std::size_t n=std::size(kWeaponSlotRules);
    Check(CheckEquipRow(r,n,3,1,10,200,5,true,-1,eq)==Verdict::ok,"Fencer hand slots may repeat");
    Check(CheckEquipRow(r,n,3,5,14,206,5,true,-1,eq)==Verdict::duplicate,"Fencer support slots may not");
    Check(CheckEquipRow(r,n,0,1,10,0,5,true,-1,eq)==Verdict::duplicate,"Ranger main slots may not");
    Check(CheckEquipRow(r,n,0,0,99,7,5,true,-1,eq)==Verdict::wrongSlot,"support category in a main slot");
    Check(CheckEquipRow(r,n,0,3,99,7,5,true,-1,eq)==Verdict::ok,"support category in its slot");
    Check(CheckEquipRow(r,n,0,4,99,0,5,true,-1,eq)==Verdict::absent,"Ranger has four slots");
    Check(CheckEquipRow(r,n,0,0,99,0,101,true,100,eq)==Verdict::capped,"over the cap");
    Check(CheckEquipRow(r,n,0,0,99,0,100,true,100,eq)==Verdict::ok,"at the cap");
    Check(CheckEquipRow(r,n,0,0,99,0,0,false,-1,eq)==Verdict::notOwned,"not owned");
    Check(CheckEquipRow(r,n,0,0,10,0,0,false,-1,eq)==Verdict::same,"already there");
    Check(CheckEquipRow(r,n,0,0,-1,0,0,true,-1,eq)==Verdict::absent,"no row");
}

// 整备方案一次写一个兵种（PlanClassSlots）：互换、被拒留原值后的连锁重复、随机下的不变式。
void ClassSlots() {
    const WeaponSlotRule* r=kWeaponSlotRules;
    const std::size_t n=std::size(kWeaponSlotRules);
    // 行 100..199：类别 = 行 % 7（Ranger 主武器类别 0..6），等级 = 行 - 100，190 起没持有
    auto facts=[](int row,int* category,int* level,bool* owned){
        if(row<100 || row>=200)return false;
        *category=row%7;*level=row-100;*owned=row<190;
        return true;
    };
    const std::array<int,kSlotsMost> cur{100,101,-1,-1,-1,-1};
    ClassPlan p=PlanClassSlots(r,n,0,cur,{101,100,-1,-1,-1,-1},-1,facts);
    Check(p.slots[0]==101 && p.slots[1]==100 && p.changed==2,"two main slots swapped");
    Check(p.verdict[0]==Verdict::ok && p.verdict[1]==Verdict::ok && p.verdict[2]==Verdict::same,"swap verdicts");
    // 槽 1 要写没持有的 190 被拒、留 101；槽 0 要写 101 于是与它重复，也留原值
    p=PlanClassSlots(r,n,0,cur,{101,190,-1,-1,-1,-1},-1,facts);
    Check(p.slots==cur && p.changed==0,"a refusal leaves a duplicate: nothing written");
    Check(p.verdict[1]==Verdict::notOwned && p.verdict[0]==Verdict::duplicate,"chained refusal verdicts");
    p=PlanClassSlots(r,n,0,cur,{150,-1,-1,-1,-1,-1},40,facts);
    Check(p.slots==cur && p.verdict[0]==Verdict::capped,"over the cap left");
    p=PlanClassSlots(r,n,0,cur,{300,-1,-1,-1,-1,-1},-1,facts);
    Check(p.verdict[0]==Verdict::absent && p.slots==cur,"not in the table");
    p=PlanClassSlots(r,n,0,cur,{100,-1,-1,-1,-1,-1},-1,facts);
    Check(p.verdict[0]==Verdict::same && p.changed==0,"already there");
    // 随机：写完以后每个改了的槽都通过原版判定（以写完的样子），没改的要么 want -1 / 原值，要么有拒绝原因
    std::mt19937 rng(7);
    for(int run=0;run<20000;++run){
        const int cls=static_cast<int>(rng()%4);
        std::array<int,kSlotsMost> now{},want{};
        for(std::size_t s=0;s<now.size();++s){
            now[s]=rng()%3==0 ? -1 : 100+static_cast<int>(rng()%100);
            want[s]=rng()%3==0 ? -1 : 95+static_cast<int>(rng()%110);
        }
        auto anyFacts=[cls](int row,int* category,int* level,bool* owned){
            if(row<100 || row>=200)return false;
            const int base=cls==0 ? 0 : cls==1 ? 100 : cls==2 ? 310 : 200;
            *category=base+row%5;*level=row-100;*owned=row<190;
            return true;
        };
        const int cap=rng()%2 ? -1 : static_cast<int>(rng()%100);
        const ClassPlan q=PlanClassSlots(r,n,cls,now,want,cap,anyFacts);
        int changed=0;
        for(std::size_t s=0;s<now.size();++s){
            if(q.slots[s]==now[s]){
                Check(want[s]<0 || want[s]==now[s] ? q.verdict[s]==Verdict::same :
                      q.verdict[s]!=Verdict::ok && q.verdict[s]!=Verdict::same,"kept slots: same or a reason");
                continue;
            }
            ++changed;
            Check(q.slots[s]==want[s] && q.verdict[s]==Verdict::ok,"a changed slot is the wanted row");
            std::array<int,kSlotsMost> probe=q.slots;
            probe[s]=now[s];
            int c=0,l=0;bool o=false;
            Check(anyFacts(want[s],&c,&l,&o) &&
                  CheckEquipRow(r,n,cls,static_cast<int>(s),want[s],c,l,o,cap,probe)==Verdict::ok,
                  "a changed slot passes the rules against the result");
        }
        Check(changed==q.changed,"changed counted");
    }
}

void Stats() {
    WeaponStats a,b;
    a[Stat::level]=10;b[Stat::level]=20;
    a[Stat::reload]=2.0;b[Stat::reload]=1.5;
    b[Stat::damage]=100;
    const auto d=CompareStats(a,b);
    Check(d.size()==3,"stats present on either side");
    Check(d[0].stat==Stat::level && d[0].better==1,"higher level better");
    Check(d[1].stat==Stat::damage && d[1].better==0 && !d[1].equipped,"one-sided");
    Check(d[2].stat==Stat::reload && d[2].better==1,"shorter reload better");
    Check(CompareStats(a,a)[0].better==0,"equal");
}

// 整份生成数据，全部行都在表里，随机持有 / 筛选 / 展开：列表不变式。
void Fuzz() {
    const Catalog c=Catalog::BuildGenerated([](const wchar_t* id)->int{
        for(const auto& r:kWeaponRows)
            if(std::wcscmp(r.id,id)==0)return r.index;
        return -1;
    });
    Check(c.entries.size()==std::size(kWeaponRows),"generated catalog complete");
    std::mt19937 rng(1124);
    for(int round=0;round<300;++round){
        std::set<int> owned;
        for(const auto& e:c.entries)
            if(rng()%3==0)owned.insert(e.row);
        Loadout l;
        l.owned=[&](int row){return owned.count(row)!=0;};
        l.levelCap=(rng()%2)?int(rng()%60):-1;
        Filter f;
        f.cls=int(rng()%4);
        f.slot=int(rng()%unsigned(c.SlotCount(f.cls)));
        f.showUnowned=rng()%2==0;
        f.sources=1u+rng()%15u;
        f.lang=int(rng()%4);
        ExpandState x;
        for(std::uint32_t s=0;s<c.series.size();++s)
            if(rng()%2)x.Open(s);
        const auto lines=BuildList(c,l,f,x);
        const WeaponSlotRule* rule=c.Rule(f.cls,f.slot);
        std::set<int> rows;
        for(std::size_t i=0;i<lines.size();++i){
            const Line& ln=lines[i];
            const Series& s=c.series[ln.series];
            Check(s.cls==f.cls && Catalog::Takes(*rule,s.category) && ((f.sources>>s.source)&1u),"line in filter");
            if(ln.kind==LineKind::series){
                Check(ln.shown>=2 && ln.levelMin<=ln.levelMax,"header aggregates");
                int children=0;
                for(std::size_t j=i+1;j<lines.size() && lines[j].kind==LineKind::child;++j)++children;
                Check(children==(ln.expanded?ln.shown:0),"children follow an open header");
            }else{
                Check(rows.insert(ln.row).second,"a row once");
                Check(f.showUnowned || owned.count(ln.row),"unowned hidden");
                const Verdict v=CheckEquip(c,l,f.cls,f.slot,ln.row);
                Check(v==ln.verdict && v!=Verdict::wrongSlot && v!=Verdict::absent,"verdict");
                if(v==Verdict::ok)Check(owned.count(ln.row) &&
                                        (l.levelCap<0 || c.ByRow(ln.row)->level<=l.levelCap),"ok means equippable");
                if(ln.kind==LineKind::child)Check(i>0,"child after header");
            }
        }
    }
}
}  // namespace

int main() {
    Generated();
    Lists();
    RawRows();
    ClassSlots();
    Stats();
    Fuzz();
    std::printf("weapon_list_logic: %d checks passed\n",checks);
    return 0;
}
