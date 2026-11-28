// 整备「外观」页（src/armory_look_logic.h）离线测试：在框架（armory_logic.h）里用键盘 / 鼠标驱动，假的游戏侧。
// 覆盖：GameStatus 偏移（与原版 0x882790 / 0x8817A3 / 0x8AD5DC 同一套）、色块 ARGB 换算、选项表（解锁 / 未解锁 /
// EDF4.1）、行值与 ◆、换行时光标落到现在的选择、确认键立即写外观 / 配色（写的是本兵种现在穿的外观那一块）、
// 没写成时提示、新兵种 4 / 5 的页、3D 预览只在当前兵种留洞、未就绪、以及随机操作下的不变式。
#include "../src/armory_look_logic.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
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
using namespace crew::armory::look;

struct Pick { int cls,outfit,ch,idx; };

struct FakeGame final : Game {
    bool ready=true;
    int current=0, classes=6, outfit=kMission, mission=1;
    bool unlocked=true, chosen41=false, fail=false;
    bool offered41[kClasses]{true,true,true,true,false,false};
    Palette pal[kClasses][kOutfits]{};
    std::vector<Option> outfits;
    std::vector<Pick> colours;
    FakeGame() {
        for(int c=0;c<kClasses;++c)
            for(int o=0;o<kOutfits;++o){
                for(int ch=0;ch<kChannels;++ch){
                    pal[c][o].selected[ch]=o;   // 原版默认：选中号 = 外观号（0x88177C）
                    for(int i=0;i<kSwatches;++i){
                        float* f=pal[c][o].rgba[ch][i];
                        f[0]=static_cast<float>(i)/11.0f;f[1]=static_cast<float>(c)/5.0f;f[2]=static_cast<float>(ch);
                        f[3]=1.0f;
                    }
                }
            }
    }
    bool Ready() const noexcept override { return ready; }
    int CurrentClass() const noexcept override { return current; }
    int Classes() const noexcept override { return classes; }
    int Outfit() const noexcept override { return outfit; }
    int WornOutfit() const noexcept override { return outfit>=0 ? outfit : mission; }
    bool Unlocked() const noexcept override { return unlocked; }
    bool Edf41Offered(int cls) const noexcept override { return cls>=0 && cls<kClasses && offered41[cls]; }
    bool Edf41Chosen() const noexcept override { return chosen41; }
    bool PickOutfit(const Option& o) noexcept override {
        outfits.push_back(o);
        if(fail)return false;
        if(o.edf41)chosen41=true;
        else{chosen41=false;outfit=o.outfit;}
        return true;
    }
    bool ReadColours(int cls,int o,Palette* out) noexcept override {
        if(!ValidClass(cls) || !ValidOutfit(o))return false;
        *out=pal[cls][o];
        return true;
    }
    bool PickColour(int cls,int o,int ch,int idx) noexcept override {
        colours.push_back(Pick{cls,o,ch,idx});
        if(fail)return false;
        pal[cls][o].selected[ch]=idx;
        return true;
    }
};

Texts MakeTexts() {
    Texts t;
    t.name=L"外观";
    t.outfit=L"外观";t.primary=L"主色";t.secondary=L"副色";
    t.mission=L"随任务";t.numbered=L"外观 %d";t.colour=L"颜色 %d";t.edf41=L"EDF4.1 外观";
    t.locked=L"未解锁";t.failed=L"没写成";t.notReady=L"未载入";t.previewOther=L"只显示当前兵种";
    return t;
}

struct Rig {
    FakeGame game;
    Texts texts=MakeTexts();
    LookPage page;
    Page* pages[1]{&page};
    State s;
    Presets ps;
    Words w;
    Input in;
    Rig() {
        page.Attach(&game,&texts);
        Open(s,in);
        Frame();
    }
    Effects Frame() { return Step(s,pages,1,ps,w,in); }
    Effects Tap(int k) {
        in.held[k]=true;
        const Effects fx=Frame();
        in.held[k]=false;
        Frame();
        return fx;
    }
    Line RowL(int row) { Line l; page.RowLine(s.pos.sub,row,&l); return l; }
    Line ItemL(int i) { Pos p=s.pos;p.item=i; Line l; page.ItemLine(p,&l); return l; }
    std::wstring Item(int i) { return ItemL(i).label; }
    // 列表列选第 row 行，进中间列。
    void Enter(int row) {
        while(s.column>0)Tap(kCancel);
        while(s.pos.row<row)Tap(kDown);
        while(s.pos.row>row)Tap(kUp);
        Tap(kRight);
    }
    void MoveTo(int item) {
        while(s.pos.item<item)Tap(kDown);
        while(s.pos.item>item)Tap(kUp);
    }
};

void Offsets() {
    // 0x882790：选中号在 +0x7070 + ((cls*4+outfit)*2+ch)*0xC4；0x8817A3：色块 idx*0x10；玩家 stride 0x3E60
    Check(SelectedOffset(0,0,0,0)==0x7070,"selected primary");
    Check(SelectedOffset(0,0,0,1)==0x7134,"selected secondary");
    Check(SelectedOffset(1,2,3,1)==0x3E60+0x7070+((2*4+3)*2+1)*0xC4,"selected p2 cls2 outfit3");
    Check(SwatchOffset(0,1,2,0,11)==0x6FB0+((1*4+2)*2)*0xC4+11*0x10,"swatch");
    // 新兵种 5 外观 3 的块还在 GameStatus 留的 10 兵种 × 4 外观里
    Check(ChannelOffset(0,5,3,1)+kChannelStride<=kGsColours+kColourClasses*kOutfits*kChannels*kChannelStride,
          "class 5 block inside");
    Check(kColourClasses*kOutfits*kChannels*kChannelStride<=kPlayerStride,"a player's blocks inside its stride");
    Check(kSwatches*kSwatchStride<=kSelected,"swatches before the selected index");
    Check(ValidColour(5,3,1,11) && !ValidColour(6,0,0,0) && !ValidColour(0,-1,0,0) && !ValidColour(0,0,2,0) &&
          !ValidColour(0,0,0,12),"valid colour");
}

void Colours() {
    const float red[4]{1,0,0,1}, mid[4]{0.5f,0.25f,1,0}, wild[4]{-1,2,0,1};
    Check(Argb(red)==0xFFFF0000u,"red");
    Check(Argb(mid)==0xFF8040FFu,"mid, alpha ignored");
    Check(Argb(wild)==0xFF00FF00u,"clamped");
    const float nanv[4]{std::nanf(""),0,0,1};
    Check(Argb(nanv)==0xFF000000u,"nan is 0");
}

void OptionTables() {
    Option o[kOptionMost];
    int n=Options(true,true,o);
    Check(n==6 && o[0].outfit==kMission && !o[0].edf41 && o[1].outfit==0 && o[4].outfit==3 && o[5].edf41,
          "unlocked with 4.1");
    Check(OptionIndex(o,n,2,false)==3 && OptionIndex(o,n,kMission,false)==0 && OptionIndex(o,n,1,true)==5,
          "option index");
    n=Options(true,false,o);
    Check(n==5 && OptionIndex(o,n,1,true)==2,"no 4.1 here: the stock outfit");
    n=Options(false,true,o);
    Check(n==2 && o[0].outfit==kMission && o[1].edf41,"locked: mission + 4.1");
    Check(OptionIndex(o,n,2,false)==-1,"locked: outfit 2 not offered");
    n=Options(false,false,o);
    Check(n==1,"locked, no 4.1");
}

void RowsAndValues() {
    Rig r;
    Check(r.page.Subs()==6,"6 class pages");
    Check(r.page.Rows(0)==3,"three rows");
    Line l=r.RowL(0);
    Check(std::wstring(l.value)==L"随任务","outfit value: mission");
    l=r.RowL(1);
    Check(std::wstring(l.value)==L"颜色 2" && l.chip==Argb(r.game.pal[0][1].rgba[0][1]),
          "primary of the worn (mission -> 1) outfit");
    r.game.classes=4;
    r.page.Refresh();
    Check(r.page.Subs()==4,"4 stock classes when the game has 4");
    r.game.classes=-1;
    r.page.Refresh();
    Check(r.page.Subs()==4,"unknown: 4");
}

void PickOutfit() {
    Rig r;
    r.Enter(0);
    Check(r.s.column==1 && r.s.pos.item==0,"cursor on the mission option");
    Check(r.Item(0)==L"\x25C6随任务" && r.Item(3)==L"外观 3" && r.Item(5)==L"EDF4.1 外观","outfit items");
    r.MoveTo(3);
    r.Tap(kConfirm);
    Check(r.game.outfits.size()==1 && r.game.outfits[0].outfit==2 && !r.game.outfits[0].edf41,"wrote outfit 2");
    Check(r.game.outfit==2 && r.Item(3)==L"\x25C6外观 3" && r.s.pos.item==3,"marked, cursor kept");
    r.Tap(kConfirm);
    Check(r.game.outfits.size()==1,"the same outfit again: nothing written");
    r.MoveTo(5);
    r.Tap(kConfirm);
    Check(r.game.chosen41 && r.game.outfits.back().edf41,"4.1 chosen");
    Check(std::wstring(r.RowL(0).value)==L"EDF4.1 外观","row value 4.1");
    // 4.1 不在这个兵种（4 载具手）：显示原版外观格
    r.Tap(kCancel);
    r.s.pos.sub=4;r.Frame();
    Check(std::wstring(r.RowL(0).value)==L"外观 3" && r.page.Items(Pos{4,0,0,0})==5,"class 4: no 4.1 option");
}

void PickColourWorn() {
    Rig r;
    r.game.outfit=kMission;r.game.mission=2;
    r.page.Refresh();
    r.Enter(2);   // 副色
    Check(r.s.pos.item==2,"cursor on the selected swatch (outfit 2's default)");
    Check(r.ItemL(2).label==std::wstring(L"\x25C6颜色 3") && r.ItemL(7).chip==Argb(r.game.pal[0][2].rgba[1][7]),
          "swatch items");
    r.MoveTo(7);
    r.Tap(kConfirm);
    Check(r.game.colours.size()==1 && r.game.colours[0].cls==0 && r.game.colours[0].outfit==2 &&
          r.game.colours[0].ch==1 && r.game.colours[0].idx==7,"wrote the worn outfit's secondary");
    Check(r.game.pal[0][2].selected[1]==7 && std::wstring(r.RowL(2).value)==L"颜色 8","row value follows");
    r.Tap(kConfirm);
    Check(r.game.colours.size()==1,"same swatch: nothing written");
    // 新兵种 5 的配色块
    r.Tap(kCancel);
    r.Tap(kSubNext);r.Tap(kSubNext);r.Tap(kSubNext);r.Tap(kSubNext);r.Tap(kSubNext);
    Check(r.s.pos.sub==5,"class 5 page");
    r.Enter(1);
    r.MoveTo(4);
    r.Tap(kConfirm);
    Check(r.game.colours.back().cls==5 && r.game.colours.back().outfit==2 && r.game.colours.back().ch==0,
          "class 5 primary");
}

void Locked() {
    Rig r;
    r.game.unlocked=false;
    r.game.offered41[0]=false;
    r.page.Refresh();
    r.Enter(0);
    Check(r.page.Items(r.s.pos)==2 && r.ItemL(1).dim && !r.ItemL(1).acts && r.Item(1)==L"未解锁","locked line");
    r.MoveTo(1);
    r.Tap(kConfirm);
    Check(r.game.outfits.empty(),"locked line does nothing");
}

void Failure() {
    Rig r;
    r.game.fail=true;
    r.Enter(1);
    r.MoveTo(5);
    r.Tap(kConfirm);
    Check(r.game.colours.size()==1 && r.game.pal[0][1].selected[0]==1,"failed: unchanged");
    Check(std::wcscmp(r.s.note,L"没写成")==0,"failed note shown");
}

void Preview() {
    Rig r;
    skin::Rect hole{};
    Check(r.page.Hole(r.s.pos,&hole) && hole.y0==skin::layout::Row(2,0).y0,"hole on the current class");
    Check(r.page.Header(2)==kHeaderDetails,"details header");
    r.s.pos.sub=1;
    Check(!r.page.Hole(r.s.pos,&hole),"no hole on another class");
    static View v;
    r.Frame();
    Build(r.s,r.pages,1,r.ps,r.w,&v);
    bool said=false;
    for(int i=0;i<v.layer[0].nt;++i)said=said || std::wcscmp(v.layer[0].texts[i].s,L"只显示当前兵种")==0;
    Check(said,"preview-other said");
}

void NotReady() {
    Rig r;
    r.game.ready=false;
    r.page.Refresh();
    r.Frame();
    Check(r.page.Rows(0)==0 && r.s.column==0,"not ready: no rows");
    skin::Rect hole{};
    Check(!r.page.Hole(r.s.pos,&hole),"not ready: no hole");
    r.game.ready=true;
    r.game.outfit=kMission;r.game.mission=-1;   // 现在穿的读不到：只有外观行
    r.page.Refresh();
    Check(r.page.Rows(0)==1,"worn unknown: outfit row only");
}

void RandomWalk() {
    std::mt19937 rng(20261128u);
    Rig r;
    const int keys[]={kUp,kDown,kLeft,kRight,kConfirm,kCancel,kSubPrev,kSubNext};
    for(int i=0;i<3000;++i){
        r.Tap(keys[rng()%std::size(keys)]);
        if(!r.s.open){Open(r.s,r.in);r.Frame();}
        if(rng()%97==0){r.game.unlocked=!r.game.unlocked;}
        if(rng()%89==0){r.game.current=static_cast<int>(rng()%6);}
        const int rows=r.page.Rows(r.s.pos.sub);
        Check(r.s.pos.sub>=0 && r.s.pos.sub<r.page.Subs(),"sub in range");
        Check(rows==0 || (r.s.pos.row>=0 && r.s.pos.row<rows),"row in range");
        Check(r.game.outfit>=kMission && r.game.outfit<kOutfits,"outfit in range");
        for(const Pick& p:r.game.colours)Check(ValidColour(p.cls,p.outfit,p.ch,p.idx),"colour pick valid");
        static View v;
        Build(r.s,r.pages,1,r.ps,r.w,&v);
    }
}
}  // namespace

int main() {
    Offsets();
    Colours();
    OptionTables();
    RowsAndValues();
    PickOutfit();
    PickColourWorn();
    Locked();
    Failure();
    Preview();
    NotReady();
    RandomWalk();
    std::printf("armory_look_logic: %d checks ok\n",checks);
    return 0;
}
