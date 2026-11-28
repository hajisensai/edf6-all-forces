// 整备武器页（src/weapon_page_logic.h）离线测试：在框架（armory_logic.h）里用键盘 / 鼠标驱动，假的游戏侧接小表。
// 覆盖：槽行显示当前装备、换槽时光标落到装备上（系列自动展开）、系列折叠 / 展开、类别标签（A / D 与点击）、来源开关
// （芯片与详情字段）、搜索（Tab 打字、Enter 保留、Esc 清空、打字时框架按键不动）、首字跳转、确认键装备（立即写、
// 重读后列表与槽行跟着变）、装备不了时提示原因（上限 / 重复 / 未持有）、收藏与只看收藏、NEW / UP 标记、属性对比（等级与说明栏属性 ▲▼，按标签配对、读一次缓存）、3D 预览
// （光标试拿、换槽与关闭时换回、只在当前兵种留洞）、以及随机操作下的不变式与 View 容量。
#include "../src/weapon_page_logic.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
using namespace crew;
using namespace crew::armory;

// 小表（Ranger）：突击步枪 Stork 系列三把（Lv5/10/30）、PA-11 一把、火箭 Goliath、EDF5 的 Slade、EDF4.1 的 Blazer、
// 空投（槽 2）一个。运行时行号 = 生成下标 + 10。
const wlist::WeaponRowInfo kRows[]={
    {L"A1",0,0,0,0,5,0,3,0,{L"Stork 1",L"鹳鸟１",L"鸛鳥１",L"ストーク１"}},
    {L"A2",1,0,0,0,10,0,3,0,{L"Stork 2",L"鹳鸟２",L"鸛鳥２",L"ストーク２"}},
    {L"A3",2,0,0,0,30,0,3,0,{L"Stork 3",L"鹳鸟３",L"鸛鳥３",L"ストーク３"}},
    {L"B1",3,0,0,0,8,1,3,0,{L"PA-11",L"ＰＡ－１１",L"ＰＡ－１１",L"ＰＡ－１１"}},
    {L"R1",4,0,0,3,12,2,3,0,{L"Goliath",L"歌利亚",L"歌利亞",L"ゴリアス"}},
    {L"P1",5,1,0,0,20,3,3,0,{L"Slade",L"史莱德",L"史萊德",L"スレイド"}},
    {L"Q1",6,2,0,0,15,4,3,0,{L"Blazer",L"火焰",L"火焰",L"ブレイザー"}},
    {L"S1",7,0,0,20,1,5,4,0,{L"Drop",L"空投",L"空投",L"ドロップ"}},
};
const wlist::WeaponSeriesInfo kSeries[]={
    {0,0,0,0,3,{L"Stork",L"鹳鸟",L"鸛鳥",L"ストーク"}},
    {0,0,0,3,1,{L"PA-11",L"ＰＡ－１１",L"ＰＡ－１１",L"ＰＡ－１１"}},
    {0,0,3,4,1,{L"Goliath",L"歌利亚",L"歌利亞",L"ゴリアス"}},
    {1,0,0,5,1,{L"Slade",L"史莱德",L"史萊德",L"スレイド"}},
    {2,0,0,6,1,{L"Blazer",L"火焰",L"火焰",L"ブレイザー"}},
    {0,0,20,7,1,{L"Drop",L"空投",L"空投",L"ドロップ"}},
};
const wlist::WeaponSlotRule kRules[]={
    {0,0,false,{0,1,2,3,-1,-1,-1,-1}},
    {0,1,false,{0,1,2,3,-1,-1,-1,-1}},
    {0,2,false,{20,-1,-1,-1,-1,-1,-1,-1}},
    {3,0,true,{200,-1,-1,-1,-1,-1,-1,-1}},
    {5,0,false,{500,-1,-1,-1,-1,-1,-1,-1}},   // the plugin's 刻印剑士 (its page only when the game has 6 classes)
    {5,1,false,{501,-1,-1,-1,-1,-1,-1,-1}},
    {5,2,false,{502,-1,-1,-1,-1,-1,-1,-1}},
};
int Row(const wchar_t* id) {
    for(const auto& r:kRows)if(std::wcscmp(r.id,id)==0)return r.index+10;
    return -1;
}

wlist::SpecLine SpecRow(const wchar_t* label,float value,int curve) {
    wlist::SpecLine ln;
    ln.label=label;ln.format=L"$0";ln.nodes=3;
    wlist::SpecParam p;
    p.value=value;p.curve=curve;
    ln.params.push_back(p);
    return ln;
}

class FakeGame final : public wpage::Game {
public:
    bool ready=true;
    int current=0;
    int classes=4;   // the game's SoldierInit: 4 stock, 6 with the plugin's classes
    int cap=-1;
    std::set<int> owned;
    std::array<std::array<int,wlist::kSlotsMost>,wpage::kClasses> equipped{};
    const wlist::Catalog* catalog=nullptr;
    std::vector<std::array<int,3>> previews;
    std::set<std::wstring> saved;
    int saves=0,equips=0;
    std::map<int,wlist::Badge> badges;
    std::map<int,wlist::WeaponSpec> specs;
    int specReads=0;
    FakeGame() {
        for(auto& c:equipped)c.fill(-1);
        specs[Row(L"A1")]={SpecRow(L"Damage",100.0f,0),SpecRow(L"Reload",2.0f,1)};
        specs[Row(L"B1")]={SpecRow(L"Reload",1.5f,1),SpecRow(L"Damage",90.0f,0),SpecRow(L"Zoom",2.0f,-1)};
    }
    bool Ready() const noexcept override { return ready; }
    int CurrentClass() const noexcept override { return current; }
    int Classes() const noexcept override { return classes; }
    bool Read(int cls,wlist::Loadout* out) noexcept override {
        out->owned=[this](int row){ return owned.count(row)!=0; };
        out->levelCap=cap;
        out->badge=[this](int row){
            const auto it=badges.find(row);
            return it==badges.end() ? wlist::Badge::none : it->second;
        };
        out->equipped=equipped[static_cast<std::size_t>(cls)];
        return true;
    }
    wlist::Verdict Equip(int cls,int slot,int row) noexcept override {
        ++equips;
        wlist::Loadout l;
        Read(cls,&l);
        const wlist::Verdict v=wlist::CheckEquip(*catalog,l,cls,slot,row);
        if(v==wlist::Verdict::ok)equipped[static_cast<std::size_t>(cls)][static_cast<std::size_t>(slot)]=row;
        return v;
    }
    void Preview(int cls,int slot,int row) noexcept override { previews.push_back({cls,slot,row}); }
    void SaveFavourites(const std::set<std::wstring>& ids) noexcept override { saved=ids; ++saves; }
    bool ReadSpec(int row,wlist::WeaponSpec* out) noexcept override {
        ++specReads;
        const auto it=specs.find(row);
        if(it==specs.end())return false;
        *out=it->second;
        return true;
    }
};

wpage::Texts MakeTexts() {
    wpage::Texts t;
    t.name=L"武器";
    t.cls[0]=L"游骑兵";t.cls[1]=L"翼装";t.cls[2]=L"空袭";t.cls[3]=L"剑士";t.cls[4]=L"载具手";t.cls[5]=L"刻印剑士";
    t.slotFormat=L"槽位 %d";
    t.empty=L"（空）";t.notReady=L"未载入";t.noWeapons=L"没有武器";t.all=L"全部";t.search=L"搜索";
    t.owned=L"持有";t.previewOther=L"只显示当前兵种";
    for(int i=0;i<8;++i)t.verdict[i]=L"?";
    t.verdict[static_cast<int>(wlist::Verdict::capped)]=L"超过等级上限";
    t.verdict[static_cast<int>(wlist::Verdict::duplicate)]=L"别的槽已装";
    t.verdict[static_cast<int>(wlist::Verdict::notOwned)]=L"未持有";
    t.hints[0]=L"h0";t.hints[1]=L"h1";t.hints[2]=L"h2";t.typingHint=L"typing";
    t.lang=1;
    return t;
}

struct Rig {
    wlist::Catalog catalog;
    FakeGame game;
    wpage::Texts texts=MakeTexts();
    wpage::WeaponsPage page;
    Page* pages[1]{&page};
    State s;
    Presets ps;
    Words w;
    Input in;
    Rig() {
        catalog=wlist::Catalog::Build(kRows,std::size(kRows),kSeries,std::size(kSeries),kRules,std::size(kRules),
                                     [](const wchar_t* id){ return Row(id); });
        game.catalog=&catalog;
        for(const auto& r:kRows)game.owned.insert(r.index+10);
        game.owned.erase(Row(L"A3"));
        game.equipped[0][0]=Row(L"A1");
        game.equipped[0][1]=Row(L"R1");
        page.Attach(&game,&catalog,&texts);
        Open(s,in);
        Frame();
    }
    Effects Frame() {
        const Effects fx=Step(s,pages,1,ps,w,in);
        page.Tick(s.pos,s.column,s.open);
        return fx;
    }
    Effects Tap(int k) {
        in.held[k]=true;
        const Effects fx=Frame();
        in.held[k]=false;
        Frame();
        return fx;
    }
    Effects Click(const skin::Rect& r) {
        in.pointer=true;in.x=(r.x0+r.x1)/2;in.y=(r.y0+r.y1)/2;
        return Tap(kMouseLeft);
    }
    void Type(const wchar_t* t) {
        in.ntyped=0;
        for(;*t && in.ntyped<8;++t)in.typed[in.ntyped++]=*t;
        Frame();
        in.ntyped=0;
    }
    std::wstring Item(int i) {
        Pos p=s.pos;p.item=i;
        Line l;
        page.ItemLine(p,&l);
        return l.label;
    }
    Line ItemL(int i) {
        Pos p=s.pos;p.item=i;
        Line l;
        page.ItemLine(p,&l);
        return l;
    }
    const wlist::Line* Cur() {
        static std::vector<wlist::Line> keep;
        keep=page.Lines(s.pos);
        return s.pos.item>=0 && s.pos.item<static_cast<int>(keep.size()) ? &keep[static_cast<std::size_t>(s.pos.item)]
                                                                         : nullptr;
    }
    int Field(wpage::Field f) {
        // 详情列里某种字段的下标（按 FieldLine 的标签认）
        const wchar_t* want=f==wpage::Field::act ? nullptr : f==wpage::Field::favourite ? texts.favourite :
                            f==wpage::Field::source ? texts.source : f==wpage::Field::unowned ? texts.showUnowned :
                            f==wpage::Field::favouritesOnly ? texts.favouritesOnly : texts.level;
        if(!want)return 0;
        for(int i=0;i<page.Fields(s.pos);++i){
            Pos p=s.pos;p.field=i;
            Line l;
            page.FieldLine(p,&l);
            if(std::wcscmp(l.label,want)==0)return i;
        }
        return -1;
    }
};

bool Has(const std::wstring& s,const wchar_t* part) { return s.find(part)!=std::wstring::npos; }

void Basics() {
    Rig r;
    Check(r.page.Subs()==4 && r.page.Rows(0)==3 && r.page.Rows(1)==0 && r.page.Rows(3)==1,"slots per class from the rules");
    Check(r.page.Rows(5)==0 && std::wcscmp(r.page.SubName(4),L"")==0,"4 classes in the game: no page for the plugin's");
    // the plugin's classes in the game (soldier_classes): 6 pages, 刻印剑士 with its 3 slots, 载具手 without rules none
    r.game.classes=6;
    r.page.Refresh();
    Check(r.page.Subs()==6 && r.page.Rows(5)==3 && r.page.Rows(4)==0 && std::wcscmp(r.page.SubName(5),L"刻印剑士")==0,
          "6 classes: the plugin's pages");
    r.game.classes=9;   // more than the page knows: its 6
    r.page.Refresh();
    Check(r.page.Subs()==6,"at most kClasses pages");
    r.game.classes=-1;  // not readable: the stock 4
    r.page.Refresh();
    Check(r.page.Subs()==4 && r.page.Rows(5)==0,"class count not readable: the stock pages");
    r.game.classes=4;
    r.page.Refresh();
    Line row;
    r.page.RowLine(0,0,&row);
    Check(std::wcscmp(row.label,L"槽位 1")==0 && std::wcscmp(row.value,L"鹳鸟１")==0,"slot line: number and equipped");
    r.page.RowLine(0,2,&row);
    Check(std::wcscmp(row.value,L"（空）")==0,"empty slot");
    // 打开时光标在槽 1 的装备上：Stork 系列展开，光标在成员 A1
    const wlist::Line* c=r.Cur();
    Check(c && c->kind==wlist::LineKind::child && c->row==Row(L"A1"),"cursor on the equipped weapon");
    Check(Has(r.Item(0),L"\x25BE") && Has(r.Item(0),L"鹳鸟") && Has(r.Item(0),L"\x25C6"),"series open, marked equipped");
    Line head=r.ItemL(0);
    Check(Has(head.value,L"持有 2/2") && Has(head.value,L"Lv5\x2013" L"10"),"series: owned count and level range");
    Check(r.page.Items(r.s.pos)==1+2+1+1+1+1,"Stork(2 owned) + PA-11 + Goliath + Slade + Blazer");
    Check(r.page.Header(1)==2 && r.page.Header(2)==7,"header lines");
    skin::Rect hole{};
    Check(r.page.Hole(r.s.pos,&hole) && hole.x0==skin::layout::Row(2,0).x0 && hole.y1==skin::layout::Row(2,6).y1,
          "hole over the details' first seven lines");
    // 只在当前兵种留洞
    r.game.current=3;
    Check(!r.page.Hole(r.s.pos,&hole),"no hole for another class");
    r.game.current=0;
    // 折叠：光标到系列行，确认
    r.Tap(kConfirm);   // 进中间列
    Check(r.s.column==1,"into the list");
    r.Tap(kUp);
    Check(r.s.pos.item==0,"on the series line");
    r.Tap(kConfirm);
    Check(r.s.column==1 && r.page.Items(r.s.pos)==5 && Has(r.Item(0),L"\x25B8"),"confirm folds the series");
    r.Tap(kConfirm);
    Check(r.page.Items(r.s.pos)==7,"confirm opens it again");
    // 未持有显示
    r.Tap(kRight);
    Check(r.s.column==2,"details");
    const int uf=r.Field(wpage::Field::unowned);
    Check(uf>=0,"show-unowned field");
    while(r.s.pos.field<uf)r.Tap(kDown);
    r.Tap(kRight);
    Check(r.page.Items(r.s.pos)==8 && Has(r.ItemL(0).value,L"持有 2/3"),"show unowned: A3 listed");
    Check(r.ItemL(3).dim,"unowned weapon greyed");
    r.Tap(kConfirm);
    Check(r.page.Items(r.s.pos)==7,"activate toggles it back");
}

void CategoriesSourcesSearch() {
    Rig r;
    r.Tap(kConfirm);
    // 类别 A / D：全部 -> 0 -> 1 -> 2 -> 3 -> 全部
    r.Tap(kAltNext);
    Check(r.page.Category()==0 && r.page.Items(r.s.pos)==1+2+1+1+1,"category 0: assault rifles");
    r.Tap(kAltNext);
    Line l=r.ItemL(0);
    Check(r.page.Category()==1 && r.page.Items(r.s.pos)==1 && l.dim && std::wcscmp(l.label,L"没有武器")==0,
          "empty category: one placeholder line");
    r.Tap(kRight);
    Check(r.s.column==2 && r.page.Fields(r.s.pos)==3,"placeholder: only the filters");
    r.Tap(kCancel);
    r.Tap(kAltPrev);r.Tap(kAltPrev);
    Check(r.page.Category()==-1,"back to all");
    // 点类别标签
    const std::vector<int> cats=r.page.Categories(r.s.pos);
    Check(cats.size()==5,"all + four categories");
    r.Click(wpage::geo::CategoryTab(4,5));
    Check(r.page.Category()==3 && r.page.Items(r.s.pos)==1,"click a category tab");
    Check(r.s.column==1,"a header click does not move the column");
    r.Click(wpage::geo::CategoryTab(0,5));
    // 来源芯片
    r.Click(wpage::geo::SourceChip(1));
    Check((r.page.Sources()&(1u<<wlist::kSourceEdf5))==0 && r.page.Items(r.s.pos)==6,"EDF5 off: Slade gone");
    r.Click(wpage::geo::SourceChip(0));
    Check(r.page.Sources()==(1u<<wlist::kSourceEdf41) && r.page.Items(r.s.pos)==1,"EDF6 off too: only Blazer");
    r.Click(wpage::geo::SourceChip(2));
    Check(r.page.Sources()==(1u<<wlist::kSourceEdf41),"never all sources off");
    r.Click(wpage::geo::SourceChip(0));r.Click(wpage::geo::SourceChip(1));
    Check(r.page.Sources()==0xFu && r.page.Items(r.s.pos)==7,"all back on");
    // 详情的来源字段：全部 -> EDF6 -> EDF5 -> EDF4.1 -> 全部
    r.Tap(kRight);
    const int sf=r.Field(wpage::Field::source);
    while(r.s.pos.field<sf)r.Tap(kDown);
    r.Tap(kRight);
    Check(r.page.Sources()==wpage::kSourceBits[0],"source field: EDF6 only");
    r.Tap(kRight);
    Check(r.page.Sources()==wpage::kSourceBits[1] && r.page.Items(r.s.pos)==1,"EDF5 only: Slade");
    Check(r.s.pos.field==r.Field(wpage::Field::source),"the field under the cursor kept when the lines change");
    r.Tap(kRight);r.Tap(kRight);
    Check(r.page.Sources()==0xFu,"back to all");
    r.Tap(kCancel);
    // 搜索：Tab 打字，框架按键不动
    r.Tap(kSearch);
    Check(r.page.Typing(),"Tab types");
    std::wstring hint=r.page.Hint(r.s.pos,r.s.column);
    Check(hint==L"typing","typing hint");
    r.Type(L"ｐａ");
    Check(r.page.Search()==L"ｐａ" && r.page.Items(r.s.pos)==1 && Has(r.Item(0),L"ＰＡ"),"search: full-width folded");
    r.Tap(kTabNext);r.Tap(kDigit1);r.Tap(kDown);r.Tap(kStore);
    Check(r.page.Typing() && r.s.column==1,"framework keys idle while typing");
    r.Tap(kBackspace);
    Check(r.page.Search()==L"ｐ","backspace");
    r.Tap(kConfirm);
    Check(!r.page.Typing() && r.page.Search()==L"ｐ","enter keeps the text");
    r.Click(wpage::geo::SearchBox());
    Check(r.page.Typing(),"click the box types");
    r.Tap(kCancel);
    Check(!r.page.Typing() && r.page.Search().empty() && r.page.Items(r.s.pos)==7,"esc clears");
    // 搜系列名命中整系列（展开）
    r.Tap(kSearch);r.Type(L"鹳");r.Tap(kConfirm);
    Check(r.page.Items(r.s.pos)==3,"series name hit lists its members");
    r.Tap(kSearch);r.Tap(kCancel);
}

void Jump() {
    Rig r;
    r.Tap(kConfirm);
    // 顶层首字（zhCN）：鹳 Ｐ->p 歌 史 火；排序后循环
    const std::vector<wlist::Line> lines=r.page.Lines(r.s.pos);
    std::set<int> seen;
    for(int i=0;i<8;++i){
        r.Tap(kJumpNext);
        const wlist::Line* c=r.Cur();
        Check(c && c->kind!=wlist::LineKind::child,"jump lands on a top-level line");
        seen.insert(r.s.pos.item);
    }
    Check(seen.size()==5,"jump visits every initial");
    const int at=r.s.pos.item;
    r.Tap(kJumpNext);r.Tap(kJumpPrev);
    Check(r.s.pos.item==at,"prev undoes next");
}

void Equipping() {
    Rig r;
    r.Tap(kConfirm);
    // PA-11 装到槽 1
    const std::vector<wlist::Line> lines=r.page.Lines(r.s.pos);
    int pa=-1;
    for(std::size_t i=0;i<lines.size();++i)if(lines[i].row==Row(L"B1"))pa=static_cast<int>(i);
    while(r.s.pos.item<pa)r.Tap(kDown);
    // 预览：光标试拿
    Check(!r.game.previews.empty() && r.game.previews.back()==std::array<int,3>{0,0,Row(L"B1")},"cursor previews");
    // 属性对比
    r.Tap(kRight);
    const int st=r.Field(wpage::Field::level);
    Pos sp=r.s.pos;sp.field=st;
    Line sl;
    r.page.FieldLine(sp,&sl);
    Check(st>=0 && Has(sl.value,L"5 \x2192 8 \x25B2"),"level compare: better");
    // 说明栏属性：候选的行序，标签配对，只在装着那把里有的接在后面。
    std::map<std::wstring,std::wstring> spec;
    std::vector<std::wstring> order;
    for(int i=st+1;i<r.page.Fields(r.s.pos);++i){
        Pos q=r.s.pos;q.field=i;
        Line l;
        r.page.FieldLine(q,&l);
        spec[l.label]=l.value;order.emplace_back(l.label);
    }
    Check(order==std::vector<std::wstring>{L"Reload",L"Damage",L"Zoom"},"spec rows after the level, candidate order");
    Check(spec[L"Reload"]==L"2.0 \x2192 1.5 \x25B2" && spec[L"Damage"]==L"100.0 \x2192 90.0 \x25BC","spec compare");
    Check(spec[L"Zoom"]==L"\x2014 \x2192 2.0","candidate-only row");
    const int reads=r.game.specReads;
    for(int i=0;i<10;++i)r.page.Fields(r.s.pos);
    Check(reads>=2 && r.game.specReads==reads,"each row's spec read once (cached)");
    r.Tap(kCancel);
    r.Tap(kConfirm);
    Check(r.game.equipped[0][0]==Row(L"B1") && r.s.column==1,"confirm equips at once");
    Line row;
    r.page.RowLine(0,0,&row);
    Check(std::wcscmp(row.value,L"ＰＡ－１１")==0,"slot line follows");
    Check(r.Cur() && r.Cur()->row==Row(L"B1") && r.Cur()->equippedHere,"cursor stays, marked equipped");
    Check(r.ItemL(r.s.pos.item).label[0]==L'\x25C6',"equipped mark");
    // 槽 2：R1 装在槽 2，换到槽 2 光标落在 Goliath
    r.Tap(kLeft);r.Tap(kDown);
    Check(r.s.pos.row==1 && r.Cur() && r.Cur()->row==Row(L"R1"),"slot change: cursor on its weapon");
    Check(r.game.previews.back()==std::array<int,3>{0,1,Row(L"R1")},"slot change previews its weapon");
    // 重复：PA-11 已在槽 1
    r.Tap(kConfirm);
    const std::vector<wlist::Line> l2=r.page.Lines(r.s.pos);
    for(std::size_t i=0;i<l2.size();++i)if(l2[i].row==Row(L"B1"))pa=static_cast<int>(i);
    while(r.s.pos.item!=pa)r.Tap(r.s.pos.item<pa ? kDown : kUp);
    Check(r.ItemL(pa).dim,"duplicate greyed");
    const int before=r.game.equips;
    r.Tap(kConfirm);
    Check(r.game.equips==before+1 && r.game.equipped[0][1]==Row(L"R1") && std::wcscmp(r.s.note,L"别的槽已装")==0,
          "duplicate refused with why");
    // 等级上限
    r.game.cap=9;
    r.page.Refresh();
    r.Frame();
    const std::vector<wlist::Line> l3=r.page.Lines(r.s.pos);
    int a2=-1;
    for(std::size_t i=0;i<l3.size();++i)if(l3[i].row==Row(L"A2"))a2=static_cast<int>(i);
    Check(a2>=0 && r.ItemL(a2).dim,"over the cap greyed");
    while(r.s.pos.item!=a2)r.Tap(r.s.pos.item<a2 ? kDown : kUp);
    r.Tap(kConfirm);
    Check(r.game.equipped[0][1]==Row(L"R1") && std::wcscmp(r.s.note,L"超过等级上限")==0,"capped refused");
    // 关闭：预览换回装备
    r.Tap(kToggle);
    Check(!r.s.open && r.game.previews.back()==std::array<int,3>{0,1,Row(L"R1")},"close restores the preview");
}

void Badges() {
    Rig r;
    r.game.badges[Row(L"A2")]=wlist::Badge::fresh;
    r.game.badges[Row(L"B1")]=wlist::Badge::up;
    r.page.Refresh();
    r.Tap(kConfirm);
    bool fresh=false,up=false;
    const std::vector<wlist::Line> lines=r.page.Lines(r.s.pos);
    for(int i=0;i<static_cast<int>(lines.size());++i){
        const Line l=r.ItemL(i);
        if(lines[static_cast<std::size_t>(i)].row==Row(L"B1"))up=std::wcsncmp(l.value,L"UP  ",4)==0;
        if(lines[static_cast<std::size_t>(i)].kind==wlist::LineKind::series)fresh=std::wcsncmp(l.value,L"NEW  ",5)==0;
    }
    Check(fresh && up,"NEW on the series holding A2, UP on PA-11");
}

void Favourites() {
    Rig r;
    r.Tap(kConfirm);
    r.Tap(kRight);
    const int ff=r.Field(wpage::Field::favourite);
    while(r.s.pos.field<ff)r.Tap(kDown);
    r.Tap(kConfirm);
    Check(r.game.saves==1 && r.game.saved.count(L"A1")==1,"favourite saved by name");
    Check(Has(r.ItemL(r.s.pos.item).label,L"\x2605"),"favourite mark");
    const int fo=r.Field(wpage::Field::favouritesOnly);
    while(r.s.pos.field<fo)r.Tap(kDown);
    r.Tap(kRight);
    Check(r.page.Items(r.s.pos)==1,"favourites only: one line");
    r.Tap(kRight);
}

void NotReady() {
    Rig r;
    r.game.ready=false;
    r.page.Refresh();
    r.Frame();
    Check(r.page.Rows(0)==0 && r.s.column==0,"not ready: no slots");
    skin::Rect hole{};
    Check(!r.page.Hole(r.s.pos,&hole),"not ready: no hole");
    static View v;
    Build(r.s,r.pages,1,r.ps,r.w,&v);
    bool said=false;
    for(int i=0;i<v.layer[0].nt;++i)said=said || std::wcscmp(v.layer[0].texts[i].s,L"未载入")==0;
    Check(said,"not ready said");
}

void RandomWalk() {
    std::mt19937 rng(20261124u);
    static View v;
    for(int run=0;run<20;++run){
        Rig r;
        r.game.cap=static_cast<int>(rng()%40)-1;
        r.page.Refresh();
        for(int f=0;f<3000;++f){
            if(!r.s.open){Open(r.s,r.in);r.page.Refresh();}
            for(int k=0;k<kKeyCount;++k)
                if(rng()%11==0)r.in.held[k]=!r.in.held[k];
            if(r.in.held[kToggle] && rng()%3)r.in.held[kToggle]=false;
            if(rng()%5==0)r.in.held[kCancel]=false;
            r.in.pointer=rng()%3!=0;
            r.in.x=static_cast<float>(rng()%1920);r.in.y=static_cast<float>(rng()%1080);
            r.in.wheel=static_cast<int>(rng()%5)-2;
            r.in.ntyped=static_cast<int>(rng()%2);
            for(int i=0;i<r.in.ntyped;++i)r.in.typed[i]=static_cast<wchar_t>(L'a'+rng()%26);
            if(rng()%400==0)r.game.current=static_cast<int>(rng()%4);
            r.Frame();
            if(!r.s.open)continue;
            const Page& p=r.page;
            for(int c=0;c<3;++c){
                const int n=detail::Count(p,r.s.pos,c), i=detail::IndexOf(r.s.pos,c);
                Check(n>0 ? (i>=0 && i<n) : i==0,"walk: cursor inside its column");
            }
            // 每个槽的装备都过得了判定（假游戏只在 ok 时写）
            for(int slot=0;slot<3;++slot){
                const int row=r.game.equipped[0][static_cast<std::size_t>(slot)];
                if(row<0)continue;
                for(int o=0;o<3;++o)
                    if(o!=slot && slot<2 && o<2)Check(r.game.equipped[0][static_cast<std::size_t>(o)]!=row,
                                                       "walk: no duplicate in non-duplicate slots");
            }
            Check(r.page.Search().size()<=wpage::kSearchMost,"walk: search bounded");
            if(f%25==0){
                Build(r.s,r.pages,1,r.ps,r.w,&v);
                Check(v.layer[0].quads.dropped==0 && v.layer[0].droppedTexts==0,"walk: view fits");
            }
        }
    }
}
}  // namespace

int main() {
    Basics();
    CategoriesSourcesSearch();
    Jump();
    Equipping();
    Favourites();
    Badges();
    NotReady();
    RandomWalk();
    std::printf("weapon_page_logic: %d checks passed\n",checks);
    return 0;
}
