#pragma once
// 整备界面的「武器」页（docs/plan-2026-11-24-ui-rewrite.md 阶段 5），框架 armory_logic.h 上的一页，纯逻辑：游戏侧
// （读装备 / 写一格 / 3D 预览 / 存收藏）经 Game 接口，运行时 src/armory.cpp 接 wequip / roompreview / ini，离线测试
// tests/weapon_page_logic_test.cpp 接假的。列表、判定、搜索、收藏、光标归位都是 weapon_list_logic.h 的。
//
// 布局（三列）：
//   列表   上方兵种分页（Z / C、LT / RT），每个槽一行，值是这一槽当前装备的名字。
//   中间   头两行页面自己画：类别标签（A / D、右摇杆左右，或点），来源开关 EDF6 / EDF5 / EDF4.1 与搜索框（点）。
//          下面是本槽的武器列表：同系列两把以上折叠成「▸ 系列名 持有 n/m Lv a–b」，确认键展开 / 折叠；单把或展开后的
//          成员确认键直接装备（像原版一样立即写进 GameStatus，没有「保存」一步）；超过等级上限、未持有、本兵种别的槽
//          已装（不可重复的槽）的灰显，确认时在提示栏说原因。◆ 本槽装着，★ 收藏；值前的 NEW / UP 与原版选择界面同一判定
//          （WeaponRecord flags，只读不清，wlist::BadgeOf）。
//   详情   上方 7 行留给 3D 预览（Hole：玩家拿着光标所指的武器，只能显示当前兵种），下面是操作与筛选：装备 / 展开、
//          收藏、来源、显示未持有、只看收藏、换成这个兵种（不是当前兵种的页；原版兵种选择入口已去掉），以及与本槽当前装备的对比：等级，和原版说明栏的每一行属性（数据与格式同
//          0xD8A90 / 0xD9B80，按标签配对；▲ 光标这把更好 ▼ 更差 = 相同，说不出方向的不标）。
// 搜索不做屏幕键盘：键盘按 Tab（或点搜索框）直接打字，Enter 保留、Esc 清空；手柄用首字跳转（PgUp / PgDn、右摇杆
// 上下）在列表里按名字首字循环跳。搜索的匹配不分大小写与全半角，片假名与平假名等同（wlist::Fold）。
#include "armory_logic.h"
#include "weapon_category_names.h"
#include "weapon_list_logic.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace crew::armory::wpage {

// 页面最多几个兵种页（GS 兵种号）：原版 0 Ranger、1 Wing Diver、2 Air Raider、3 Fencer，插件 4 载具手、5 刻印剑士
// （docs/plan-2026-11-28-new-classes-impl.md §1）。实际显示几个按 Game::Classes()（游戏的 SoldierInit 现有几个）。
inline constexpr int kClasses=6;
inline constexpr int kStockClasses=4;
inline constexpr std::size_t kSearchMost=32;

// 游戏侧。只在 UI 线程调。
class Game {
public:
    virtual bool Ready() const noexcept=0;                           // 武器表与 GameStatus 可读
    virtual int CurrentClass() const noexcept=0;                     // 玩家当前兵种，-1 读不到
    virtual int Classes() const noexcept=0;                          // 游戏现有兵种数（SoldierInit），-1 读不到
    virtual bool Read(int cls,wlist::Loadout* out) noexcept=0;       // 装备、持有、上限（favourites 不用）
    // 写一格：判定同 wlist::CheckEquipRow，ok 才写。返回判定。
    virtual wlist::Verdict Equip(int cls,int slot,int row) noexcept=0;
    // 3D 预览让玩家拿这一把（cls 是当前兵种时才有意义）；row -1 空手。
    virtual void Preview(int cls,int slot,int row) noexcept=0;
    // 把玩家的当前兵种换成 cls（GS+0x6E90，原版兵种选择界面写的同一格）；false：没写成。
    virtual bool UseClass(int cls) noexcept=0;
    virtual void SaveFavourites(const std::set<std::wstring>& ids) noexcept=0;
    // 原版说明栏的属性（wlist::WeaponSpec，按该行当前星级）；false：读不到。
    virtual bool ReadSpec(int row,wlist::WeaponSpec* out) noexcept=0;
protected:
    ~Game()=default;
};

// 本地化文字（hudtext），运行时给。
struct Texts {
    const wchar_t* name=L"Weapons";
    const wchar_t* cls[kClasses]{L"R",L"W",L"A",L"F",L"V",L"S"};
    const wchar_t* slotFormat=L"Slot %d";       // 一个 %d：槽号（1 起）
    const wchar_t* empty=L"-";
    const wchar_t* notReady=L"";                // 武器表未载入
    const wchar_t* noWeapons=L"";               // 列表为空（筛掉了 / 没持有）
    const wchar_t* all=L"All";                  // 类别「全部」
    const wchar_t* search=L"Search";            // 搜索框空时的提示
    const wchar_t* owned=L"Owned";              // 系列行「持有 n/m」
    const wchar_t* equip=L"Equip";
    const wchar_t* expand=L"Expand";
    const wchar_t* collapse=L"Collapse";
    const wchar_t* favourite=L"Favourite";
    const wchar_t* source=L"Source";
    const wchar_t* showUnowned=L"Show unowned";
    const wchar_t* favouritesOnly=L"Favourites only";
    const wchar_t* on=L"On";
    const wchar_t* off=L"Off";
    const wchar_t* level=L"Level";
    const wchar_t* badgeNew=L"NEW";             // 原版 WeaponNew
    const wchar_t* badgeUp=L"UP";               // 原版 WeaponUp
    const wchar_t* previewOther=L"";            // 不是当前兵种：3D 预览只显示当前兵种
    const wchar_t* useClass=L"Use this class";  // 详情列「换成这个兵种」（不是当前兵种的页才有）
    const wchar_t* useClassFailed=L"";          // 换兵种没写成
    const wchar_t* verdict[8]{};                // wlist::Verdict 顺序：装备不了的原因（ok / same / header 不用）
    const wchar_t* hints[3]{};                  // 三列的提示栏
    const wchar_t* typingHint=L"";              // 打字时的提示栏
    int lang=0;                                 // hudtext::Lang（名字的语言）
};

// 详情列的一行是什么。
enum class Field : std::uint8_t { act, favourite, source, unowned, favouritesOnly, level, spec, useClass };

// 来源开关：EDF6 / EDF5 / EDF4.1（别的 mod 追加的行跟 EDF6 一起开关）。
inline constexpr unsigned kSourceBits[3]={(1u<<wlist::kSourceEdf6)|(1u<<wlist::kSourceOther),1u<<wlist::kSourceEdf5,
                                          1u<<wlist::kSourceEdf41};
inline constexpr const wchar_t* kSourceNames[3]={L"EDF6",L"EDF5",L"EDF4.1"};
// 来源字段左右切换的组合：全部、只 6、只 5、只 4.1。
inline constexpr unsigned kSourceCycle[4]={0xFu,kSourceBits[0],kSourceBits[1],kSourceBits[2]};

// 中间列头部的几何（Draw 与鼠标共用）。
namespace geo {
inline skin::Rect CategoryTab(int i,int n) noexcept {
    const skin::Rect r=skin::layout::Row(1,0);
    const float w=r.W()/static_cast<float>(n>0 ? n : 1);
    return skin::Rect{r.x0+w*static_cast<float>(i),r.y0,r.x0+w*static_cast<float>(i+1)-4,r.y1};
}
inline skin::Rect SourceChip(int i) noexcept {
    const skin::Rect r=skin::layout::Row(1,1);
    const float x=r.x0+static_cast<float>(i)*104.0f;
    return skin::Rect{x,r.y0+4,x+98,r.y1-4};
}
inline skin::Rect SearchBox() noexcept {
    const skin::Rect r=skin::layout::Row(1,1);
    return skin::Rect{r.x0+3*104.0f+8,r.y0,r.x1,r.y1};
}
// 3D 预览的洞：详情列头 7 行（Row(2,0..6)）。
inline skin::Rect Preview() noexcept {
    const skin::Rect a=skin::layout::Row(2,0), b=skin::layout::Row(2,6);
    return skin::Rect{a.x0,a.y0,a.x1,b.y1};
}
}  // namespace geo
inline constexpr int kHeaderCentre=2, kHeaderDetails=7;

class WeaponsPage final : public Page {
public:
    void Attach(Game* game,const wlist::Catalog* catalog,const Texts* texts) noexcept {
        game_=game;catalog_=catalog;texts_=texts;
        Refresh();
    }
    void SetFavourites(std::set<std::wstring> ids) { favourites_=std::move(ids); ++gen_; }
    const std::set<std::wstring>& Favourites() const noexcept { return favourites_; }
    // 重读各兵种的装备（打开整备、切方案之后：GameStatus 可能被别处改过）。兵种数也在这里重取。
    void Refresh() noexcept {
        shown_=kStockClasses;
        if(game_){
            const int n=game_->Classes();
            if(n>kStockClasses)shown_=(std::min)(n,kClasses);
        }
        for(int c=0;c<kClasses;++c){
            have_[c]=c<shown_ && Ready() && game_->Read(c,&loadout_[c]);
            if(!have_[c])loadout_[c]=wlist::Loadout{};
        }
        specs_.clear();   // 星级可能变了
        ++gen_;
    }

    // ---- Page ----
    const wchar_t* Name() const noexcept override { return texts_ ? texts_->name : L""; }
    int Subs() const noexcept override { return shown_; }
    const wchar_t* SubName(int sub) const noexcept override {
        return texts_ && sub>=0 && sub<shown_ ? texts_->cls[sub] : L"";
    }
    int Rows(int sub) const noexcept override {
        return Ready() && sub>=0 && sub<kClasses && have_[sub] ? (std::min)(catalog_->SlotCount(sub),wlist::kSlotsMost) : 0;
    }
    void RowLine(int sub,int row,Line* out) const noexcept override {
        std::swprintf(out->label,kTextMost,texts_->slotFormat,row+1);
        const int r=Equipped(sub,row);
        const wlist::Entry* e=r>=0 ? catalog_->ByRow(r) : nullptr;
        if(e)CopyName(out->value,e->Name(texts_->lang));
        else CopyText(out->value,kTextMost,r>=0 ? L"?" : texts_->empty);
    }
    int Items(const Pos& p) const noexcept override {
        if(Rows(p.sub)<=0)return 0;
        const int n=static_cast<int>(LinesOf(p).size());
        return n>0 ? n : 1;   // 空列表也留一行（说明），详情列的筛选还能进得去
    }
    void ItemLine(const Pos& p,Line* out) const noexcept override {
        const wlist::Line* ln=LineAt(p);
        if(!ln){CopyText(out->label,kTextMost,texts_->noWeapons);out->dim=true;return;}
        const wlist::Entry& e=catalog_->entries[ln->entry];
        std::wstring label;
        if(ln->kind==wlist::LineKind::series){
            label=ln->expanded ? L"\x25BE " : L"\x25B8 ";
            if(ln->equippedHere)label+=L"\x25C6";
            label+=catalog_->series[ln->series].Label(texts_->lang,catalog_->entries);
            if(ln->levelMin==ln->levelMax)
                std::swprintf(out->value,kTextMost,L"%ls %d/%d  Lv%d",texts_->owned,ln->owned,ln->shown,ln->levelMin);
            else
                std::swprintf(out->value,kTextMost,L"%ls %d/%d  Lv%d\x2013%d",texts_->owned,ln->owned,ln->shown,
                              ln->levelMin,ln->levelMax);
            Prefix(out->value,ln->badge);
            const int cap=loadout_[p.sub].levelCap;
            out->dim=ln->owned==0 || (cap>=0 && static_cast<unsigned>(ln->levelMin)>static_cast<unsigned>(cap));
        }else{
            if(ln->kind==wlist::LineKind::child)label=L"\x3000";
            if(ln->equippedHere)label+=L"\x25C6";
            if(ln->favourite)label+=L"\x2605";
            label+=e.Name(texts_->lang);
            std::swprintf(out->value,kTextMost,L"Lv%d",e.level);
            Prefix(out->value,ln->badge);
            out->dim=ln->verdict!=wlist::Verdict::ok && ln->verdict!=wlist::Verdict::same;
            out->acts=true;
        }
        CopyName(out->label,label);
    }
    int Fields(const Pos& p) const noexcept override { return static_cast<int>(FieldsOf(p).size()); }
    void FieldLine(const Pos& p,Line* out) const noexcept override {
        const std::vector<Field> fs=FieldsOf(p);
        if(p.field<0 || p.field>=static_cast<int>(fs.size()))return;
        const wlist::Line* ln=LineAt(p);
        switch(fs[static_cast<std::size_t>(p.field)]){
        case Field::act:
            if(ln->kind==wlist::LineKind::series)
                CopyText(out->label,kTextMost,ln->expanded ? texts_->collapse : texts_->expand);
            else{
                CopyText(out->label,kTextMost,texts_->equip);
                out->dim=ln->verdict!=wlist::Verdict::ok;
            }
            out->acts=true;
            break;
        case Field::favourite:
            CopyText(out->label,kTextMost,texts_->favourite);
            CopyText(out->value,kTextMost,ln->favourite ? texts_->on : texts_->off);
            out->steps=out->acts=true;
            break;
        case Field::source:
            CopyText(out->label,kTextMost,texts_->source);
            SourceText(out->value);
            out->steps=true;
            break;
        case Field::unowned:
            CopyText(out->label,kTextMost,texts_->showUnowned);
            CopyText(out->value,kTextMost,showUnowned_ ? texts_->on : texts_->off);
            out->steps=out->acts=true;
            break;
        case Field::favouritesOnly:
            CopyText(out->label,kTextMost,texts_->favouritesOnly);
            CopyText(out->value,kTextMost,favouritesOnly_ ? texts_->on : texts_->off);
            out->steps=out->acts=true;
            break;
        case Field::useClass:
            CopyText(out->label,kTextMost,texts_->useClass);
            CopyText(out->value,kTextMost,SubName(p.sub));
            out->acts=true;
            break;
        case Field::level:{
            const wlist::Entry* e=catalog_->ByRow(Equipped(p.sub,p.row));
            CopyText(out->label,kTextMost,texts_->level);
            std::optional<std::wstring> a,b;
            if(e)a=std::to_wstring(e->level);
            if(ln)b=std::to_wstring(catalog_->entries[ln->entry].level);
            const int better=a && b && e->level!=catalog_->entries[ln->entry].level ?
                             (catalog_->entries[ln->entry].level>e->level ? 1 : -1) : 0;
            CopyName(out->value,Versus(a,b,better));
            out->dim=true;
            break;
        }
        case Field::spec:{
            const std::vector<wlist::SpecDiff>& ds=Diffs(p);
            const std::size_t at=static_cast<std::size_t>(p.field-SpecStart(fs));
            if(at>=ds.size())break;
            CopyName(out->label,ds[at].label);
            CopyName(out->value,Versus(ds[at].equipped,ds[at].candidate,ds[at].better));
            out->dim=true;
            break;
        }
        }
    }
    bool Step(const Pos& p,int delta) noexcept override {
        const std::vector<Field> fs=FieldsOf(p);
        if(p.field<0 || p.field>=static_cast<int>(fs.size()))return false;
        switch(fs[static_cast<std::size_t>(p.field)]){
        case Field::favourite:return ToggleFavourite(p);
        case Field::source:{
            int at=-1;
            for(int i=0;i<4;++i)if(sources_==kSourceCycle[i])at=i;
            sources_=kSourceCycle[detail::Wrap((at<0 ? 0 : at)+(delta>0 ? 1 : -1),4)];
            ++gen_;
            return true;
        }
        case Field::unowned:showUnowned_=!showUnowned_;++gen_;return true;
        case Field::favouritesOnly:favouritesOnly_=!favouritesOnly_;++gen_;return true;
        default:return false;
        }
    }
    bool Activate(const Pos& p) noexcept override {
        const std::vector<Field> fs=FieldsOf(p);
        if(p.field<0 || p.field>=static_cast<int>(fs.size()))return false;
        switch(fs[static_cast<std::size_t>(p.field)]){
        case Field::act:return Act(p);
        case Field::useClass:return UseClass(p);
        case Field::source:return false;
        case Field::level:case Field::spec:return false;
        default:return Step(p,1);
        }
    }
    void Draw(const Pos& p,int column,Layer* l) const noexcept override {
        if(column==0 && !Ready()){
            const skin::Rect r=skin::layout::Row(0,1);
            l->Say(texts_ ? texts_->notReady : L"",r.x0+22,r.y0+12,24,skin::kTextDim,Align::left,false,r.W()-40);
        }
        if(column==1 && Rows(p.sub)>0)DrawCentreHeader(p,l);
        if(column==2 && Rows(p.sub)>0){
            skin::Rect hole{};
            if(!Hole(p,&hole)){
                const skin::Rect r=geo::Preview();
                l->Say(texts_->previewOther,(r.x0+r.x1)/2,(r.y0+r.y1)/2-14,24,skin::kTextDim,Align::centre,false,
                       r.W()-40);
            }
        }
    }
    int Header(int column) const noexcept override {
        return column==1 ? kHeaderCentre : column==2 ? kHeaderDetails : 0;
    }
    bool Hole(const Pos& p,skin::Rect* out) const noexcept override {
        if(!Ready() || Rows(p.sub)<=0 || game_->CurrentClass()!=p.sub)return false;
        *out=geo::Preview();
        return true;
    }
    bool Key(Pos& p,int column,int key,const Input& in) noexcept override {
        if(Rows(p.sub)<=0)return false;
        if(typing_){
            switch(key){
            case kConfirm:typing_=false;return true;
            case kCancel:typing_=false;search_.clear();++gen_;return true;
            case kBackspace:if(!search_.empty()){search_.pop_back();++gen_;}return true;
            default:return false;
            }
        }
        switch(key){
        case kAltPrev:case kAltNext:StepCategory(p,key==kAltNext ? 1 : -1);return true;
        case kJumpPrev:case kJumpNext:{
            const int to=JumpTarget(p,key==kJumpNext ? 1 : -1);
            if(to>=0)p.item=to;
            return true;
        }
        case kSearch:typing_=true;return true;
        case kConfirm:
            if(column!=1 || !LineAt(p))return false;
            Act(p);
            return true;
        case kMouseLeft:return in.pointer && ClickHeader(p,in.x,in.y);
        default:return false;
        }
    }
    bool Typing() const noexcept override { return typing_; }
    void Type(const wchar_t* chars,int n) noexcept override {
        for(int i=0;i<n;++i){
            const wchar_t c=chars[i];
            if(c<0x20 || c==0x7F || search_.size()>=kSearchMost)continue;
            search_.push_back(c);
            ++gen_;
        }
    }
    void Relocate(Pos* p,int /*column*/) noexcept override {
        if(Rows(p->sub)<=0){lastSub_=-1;return;}
        if(p->sub!=lastSub_ || p->row!=lastRow_){
            // 换了槽：类别回到全部，光标放到这一槽当前装备上（展开它的系列）
            if(lastSub_>=0)category_=-1;
            const int here=Equipped(p->sub,p->row);
            if(const auto s=wlist::SeriesOfRow(*catalog_,here))expand_.Open(*s);
            ++gen_;
            const int at=wlist::FindRowLine(LinesOf(*p),here);
            p->item=at>=0 ? at : 0;
        }else if(gen_!=placedGen_){
            const std::vector<wlist::Line>& lines=LinesOf(*p);
            const int at=lastKnown_ ? wlist::RelocateCursor(lines,lastKey_,lastItem_) : p->item;
            p->item=at>=0 ? at : 0;
            // 详情列的字段随行的种类变（系列行没有收藏 / 对比）：光标留在同一种字段上
            const std::vector<Field> fs=FieldsOf(*p);
            for(std::size_t i=0;i<fs.size();++i)
                if(fs[i]==lastField_)p->field=static_cast<int>(i);
        }
        lastSub_=p->sub;lastRow_=p->row;placedGen_=gen_;
        const wlist::Line* ln=LineAt(*p);
        lastKnown_=ln!=nullptr;
        if(ln)lastKey_=wlist::KeyOf(*ln);
        lastItem_=p->item;
        const std::vector<Field> fs=FieldsOf(*p);
        if(p->field>=0 && p->field<static_cast<int>(fs.size()))lastField_=fs[static_cast<std::size_t>(p->field)];
    }
    bool TakeNote(wchar_t* out,std::size_t cap) noexcept override {
        if(!note_)return false;
        CopyText(out,cap,note_);
        note_=nullptr;
        return true;
    }
    const wchar_t* Hint(const Pos&,int column) const noexcept override {
        if(!texts_)return nullptr;
        if(typing_)return texts_->typingHint;
        return column>=0 && column<3 && texts_->hints[column] ? texts_->hints[column] : nullptr;
    }

    // 每帧（Step 之后）：3D 预览。active 假（整备关了 / 不在本页）时把试拿的武器换回装备。
    void Tick(const Pos& p,int column,bool active) noexcept {
        if(!Ready())return;
        const int cur=game_->CurrentClass();
        int slot=-1,row=-1;
        if(active && cur==p.sub && Rows(p.sub)>0){
            slot=p.row;
            row=Equipped(cur,slot);
            const wlist::Line* ln=column>=1 ? LineAt(p) : nullptr;
            if(ln && ln->kind!=wlist::LineKind::series)row=ln->row;
        }
        // 先把别的槽上试拿的换回去
        if(shownSlot_>=0 && (shownClass_!=cur || shownSlot_!=slot)){
            if(shownClass_==cur && shownRow_!=Equipped(cur,shownSlot_))game_->Preview(cur,shownSlot_,Equipped(cur,shownSlot_));
            shownSlot_=-1;
        }
        if(slot<0)return;
        if(shownSlot_!=slot || shownRow_!=row){
            game_->Preview(cur,slot,row);
            shownClass_=cur;shownSlot_=slot;shownRow_=row;
        }
    }

    // 给测试看的状态。
    int Category() const noexcept { return category_; }
    unsigned Sources() const noexcept { return sources_; }
    const std::wstring& Search() const noexcept { return search_; }
    std::vector<wlist::Line> Lines(const Pos& p) const { return LinesOf(p); }
    int Equipped(int cls,int slot) const noexcept {
        if(cls<0 || cls>=kClasses || slot<0 || slot>=wlist::kSlotsMost || !have_[cls])return -1;
        return loadout_[cls].equipped[static_cast<std::size_t>(slot)];
    }
    // 本槽的类别标签：-1（全部）然后是槽规则里的类别。
    std::vector<int> Categories(const Pos& p) const {
        std::vector<int> out{-1};
        const WeaponSlotRuleRef r=catalog_ ? catalog_->Rule(p.sub,p.row) : nullptr;
        if(r)
            for(int k=0;k<wlist::kSlotCategoriesMost && r->categories[k]>=0;++k)out.push_back(r->categories[k]);
        return out;
    }

private:
    using WeaponSlotRuleRef=const wlist::WeaponSlotRule*;
    Game* game_=nullptr;
    const wlist::Catalog* catalog_=nullptr;
    const Texts* texts_=nullptr;
    std::array<wlist::Loadout,kClasses> loadout_{};
    std::array<bool,kClasses> have_{};
    int shown_=kStockClasses;   // 兵种页数（Refresh）：原版 4，插件兵种注入后 6
    std::set<std::wstring> favourites_;
    wlist::ExpandState expand_;
    int category_=-1;
    unsigned sources_=0xFu;
    bool showUnowned_=false, favouritesOnly_=false;
    std::wstring search_;
    bool typing_=false;
    const wchar_t* note_=nullptr;
    unsigned gen_=1;
    // 列表缓存（Items / ItemLine 每帧调很多次）
    mutable std::vector<wlist::Line> cache_;
    mutable int cacheSub_=-1,cacheRow_=-1;
    mutable unsigned cacheGen_=0;
    // 光标归位
    int lastSub_=-1,lastRow_=-1,lastItem_=0;
    unsigned placedGen_=0;
    bool lastKnown_=false;
    wlist::LineKey lastKey_{};
    Field lastField_=Field::act;
    // 属性：每行一份（Refresh 清）；当前对比
    mutable std::map<int,std::optional<wlist::WeaponSpec>> specs_;
    mutable std::vector<wlist::SpecDiff> diffs_;
    mutable int diffHad_=-2,diffWant_=-2;
    mutable unsigned diffGen_=0;
    // 3D 预览当前让玩家拿着的
    int shownClass_=-1,shownSlot_=-1,shownRow_=-1;

    bool Ready() const noexcept { return game_ && catalog_ && texts_ && game_->Ready(); }

    static void CopyName(wchar_t* to,std::wstring_view s) noexcept {
        std::size_t i=0;
        for(;i+1<kTextMost && i<s.size();++i)to[i]=s[i];
        to[i]=0;
    }
    const std::vector<wlist::Line>& LinesOf(const Pos& p) const {
        if(cacheSub_!=p.sub || cacheRow_!=p.row || cacheGen_!=gen_){
            cache_.clear();
            if(Ready() && p.sub>=0 && p.sub<kClasses && have_[p.sub]){
                wlist::Loadout l=loadout_[p.sub];
                l.favourites=favourites_;
                wlist::Filter f;
                f.cls=p.sub;f.slot=p.row;f.sources=sources_;f.category=category_;f.search=search_;
                f.showUnowned=showUnowned_;f.favouritesOnly=favouritesOnly_;f.lang=texts_->lang;
                cache_=wlist::BuildList(*catalog_,l,f,expand_);
            }
            cacheSub_=p.sub;cacheRow_=p.row;cacheGen_=gen_;
        }
        return cache_;
    }
    const wlist::Line* LineAt(const Pos& p) const {
        const std::vector<wlist::Line>& lines=LinesOf(p);
        return p.item>=0 && p.item<static_cast<int>(lines.size()) ? &lines[static_cast<std::size_t>(p.item)] : nullptr;
    }
    std::vector<Field> FieldsOf(const Pos& p) const {
        std::vector<Field> fs;
        if(Rows(p.sub)<=0)return fs;
        const wlist::Line* ln=LineAt(p);
        if(ln){
            fs.push_back(Field::act);
            if(ln->kind!=wlist::LineKind::series)fs.push_back(Field::favourite);
        }
        fs.push_back(Field::source);
        fs.push_back(Field::unowned);
        fs.push_back(Field::favouritesOnly);
        // 原版的兵种选择入口已经去掉（整备是唯一入口）：不是当前兵种的页可以在这里换过去。
        if(const int cur=game_->CurrentClass();cur>=0 && cur!=p.sub)fs.push_back(Field::useClass);
        if(ln && ln->kind!=wlist::LineKind::series){
            fs.push_back(Field::level);
            fs.insert(fs.end(),Diffs(p).size(),Field::spec);
        }
        return fs;
    }
    static int SpecStart(const std::vector<Field>& fs) noexcept {
        for(std::size_t i=0;i<fs.size();++i)if(fs[i]==Field::spec)return static_cast<int>(i);
        return static_cast<int>(fs.size());
    }
    // 一行武器的属性（读一次，Refresh 时清）；读不到为空。
    const wlist::WeaponSpec* SpecOf(int row) const {
        if(row<0 || !Ready())return nullptr;
        auto it=specs_.find(row);
        if(it==specs_.end()){
            wlist::WeaponSpec spec;
            std::optional<wlist::WeaponSpec> got;
            if(game_->ReadSpec(row,&spec))got=std::move(spec);
            it=specs_.emplace(row,std::move(got)).first;
        }
        return it->second ? &*it->second : nullptr;
    }
    // 本槽装着的 vs 光标所在的（光标是武器行时）。
    const std::vector<wlist::SpecDiff>& Diffs(const Pos& p) const {
        const wlist::Line* ln=LineAt(p);
        const int had=Equipped(p.sub,p.row);
        const int want=ln && ln->kind!=wlist::LineKind::series ? ln->row : -1;
        if(had!=diffHad_ || want!=diffWant_ || gen_!=diffGen_){
            diffs_=want>=0 ? wlist::CompareSpecs(SpecOf(had),SpecOf(want)) : std::vector<wlist::SpecDiff>{};
            diffHad_=had;diffWant_=want;diffGen_=gen_;
        }
        return diffs_;
    }
    static std::wstring Versus(const std::optional<std::wstring>& a,const std::optional<std::wstring>& b,int better) {
        std::wstring v=a ? *a : L"\x2014";
        v+=L" \x2192 ";
        v+=b ? *b : L"\x2014";
        if(a && b)v+=better>0 ? L" \x25B2" : better<0 ? L" \x25BC" : *a==*b ? L" =" : L"";
        return v;
    }
    void Prefix(wchar_t* value,wlist::Badge badge) const noexcept {
        const wchar_t* tag=badge==wlist::Badge::fresh ? texts_->badgeNew : badge==wlist::Badge::up ? texts_->badgeUp : nullptr;
        if(!tag || !tag[0])return;
        std::wstring v(tag);
        v+=L"  ";
        v+=value;
        CopyName(value,v);
    }
    void SourceText(wchar_t* out) const noexcept {
        if(sources_==0xFu){CopyText(out,kTextMost,texts_->all);return;}
        std::wstring s;
        for(int i=0;i<3;++i)
            if((sources_&kSourceBits[i])==kSourceBits[i]){
                if(!s.empty())s+=L" + ";
                s+=kSourceNames[i];
            }
        CopyName(out,s.empty() ? std::wstring(L"\x2014") : s);
    }
    // 确认：系列行展开 / 折叠，武器行装备。
    bool Act(const Pos& p) noexcept {
        const wlist::Line* ln=LineAt(p);
        if(!ln)return false;
        if(ln->kind==wlist::LineKind::series){
            expand_.Toggle(ln->series);
            ++gen_;
            return true;
        }
        const int row=ln->row;
        const wlist::Verdict v=game_->Equip(p.sub,p.row,row);
        if(v==wlist::Verdict::ok){
            have_[p.sub]=game_->Read(p.sub,&loadout_[p.sub]);
            if(!have_[p.sub])loadout_[p.sub]=wlist::Loadout{};
            ++gen_;
            return true;
        }
        if(v!=wlist::Verdict::same){
            const int i=static_cast<int>(v);
            note_=texts_->verdict[i] && texts_->verdict[i][0] ? texts_->verdict[i] : L"!";
        }
        return false;
    }
    // 换成这一页的兵种。写成了 3D 预览下一帧（Tick）就跟着换。
    bool UseClass(const Pos& p) noexcept {
        if(game_->CurrentClass()==p.sub)return false;
        if(!game_->UseClass(p.sub)){
            note_=texts_->useClassFailed && texts_->useClassFailed[0] ? texts_->useClassFailed : L"!";
            return false;
        }
        ++gen_;
        return true;
    }
    bool ToggleFavourite(const Pos& p) {
        const wlist::Line* ln=LineAt(p);
        if(!ln || ln->kind==wlist::LineKind::series)return false;
        const std::wstring_view id=catalog_->entries[ln->entry].Id();
        if(id.empty())return false;   // 别的 mod 追加的行没有生成数据里的名字：收藏存不住
        wlist::ToggleFavourite(&favourites_,id);
        game_->SaveFavourites(favourites_);
        ++gen_;
        return true;
    }
    void StepCategory(Pos& p,int delta) {
        const std::vector<int> cats=Categories(p);
        int at=0;
        for(std::size_t i=0;i<cats.size();++i)if(cats[i]==category_)at=static_cast<int>(i);
        category_=cats[static_cast<std::size_t>(detail::Wrap(at+delta,static_cast<int>(cats.size())))];
        ++gen_;
    }
    // 名字首字（折叠后的）：系列行用系列名，武器行用武器名。
    wchar_t Initial(const Pos& p,const wlist::Line& ln) const {
        const std::wstring_view name=ln.kind==wlist::LineKind::series
            ? catalog_->series[ln.series].Label(texts_->lang,catalog_->entries)
            : catalog_->entries[ln.entry].Name(texts_->lang);
        (void)p;
        const std::wstring f=wlist::Fold(name);
        for(const wchar_t c:f)if(c!=L' ')return c;
        return 0;
    }
    // 首字跳转：列表（顶层行：系列与单把，不算展开的成员）里出现的首字排好序，从光标这一行的首字往前 / 后一个，
    // 跳到列表里第一条这个首字的行；绕回。-1：没有行。
    int JumpTarget(const Pos& p,int delta) const {
        const std::vector<wlist::Line>& lines=LinesOf(p);
        std::set<wchar_t> initials;
        for(const wlist::Line& ln:lines)
            if(ln.kind!=wlist::LineKind::child)initials.insert(Initial(p,ln));
        if(initials.empty())return -1;
        const std::vector<wchar_t> order(initials.begin(),initials.end());
        const wlist::Line* here=LineAt(p);
        int at=-1;
        if(here){
            const wchar_t c=Initial(p,*here);
            for(std::size_t i=0;i<order.size();++i)if(order[i]==c)at=static_cast<int>(i);
        }
        const int n=static_cast<int>(order.size());
        const wchar_t want=order[static_cast<std::size_t>(at<0 ? (delta>0 ? 0 : n-1) : detail::Wrap(at+delta,n))];
        for(std::size_t i=0;i<lines.size();++i)
            if(lines[i].kind!=wlist::LineKind::child && Initial(p,lines[i])==want)return static_cast<int>(i);
        return -1;
    }
    bool ClickHeader(Pos& p,float x,float y) {
        const std::vector<int> cats=Categories(p);
        const int n=static_cast<int>(cats.size());
        for(int i=0;i<n;++i)
            if(geo::CategoryTab(i,n).Contains(x,y)){category_=cats[static_cast<std::size_t>(i)];++gen_;return true;}
        for(int i=0;i<3;++i)
            if(geo::SourceChip(i).Contains(x,y)){
                const unsigned next=sources_^kSourceBits[i];
                if(next&0x7u)sources_=next;   // 不许全关
                ++gen_;
                return true;
            }
        if(geo::SearchBox().Contains(x,y)){typing_=true;return true;}
        return skin::layout::Row(1,0).Contains(x,y) || skin::layout::Row(1,1).Contains(x,y);
    }
    void DrawCentreHeader(const Pos& p,Layer* l) const {
        const std::vector<int> cats=Categories(p);
        const int n=static_cast<int>(cats.size());
        for(int i=0;i<n;++i){
            const skin::Rect r=geo::CategoryTab(i,n);
            const int c=cats[static_cast<std::size_t>(i)];
            const bool at=c==category_;
            skin::Tab(&l->quads,r,at);
            wchar_t name[kTextMost]{};
            const wchar_t* known=c<0 ? texts_->all : wlist::CategoryShortName(c,texts_->lang);
            if(known)CopyText(name,kTextMost,known);
            else std::swprintf(name,kTextMost,L"#%d",c);
            l->Say(name,(r.x0+r.x1)/2,r.y0+14,20,skin::TextOn(at),Align::centre,at,r.W()-8);
        }
        for(int i=0;i<3;++i){
            const skin::Rect r=geo::SourceChip(i);
            const bool on=(sources_&kSourceBits[i])!=0;
            skin::Chip(&l->quads,r,on,false);
            l->Say(kSourceNames[i],(r.x0+r.x1)/2,r.y0+10,20,skin::TextOn(on),Align::centre,on,r.W()-8);
        }
        const skin::Rect box=geo::SearchBox();
        skin::Cell(&l->quads,box);
        if(typing_)skin::Cursor(&l->quads,box);
        wchar_t shown[kTextMost]{};
        if(search_.empty() && !typing_){
            CopyText(shown,kTextMost,texts_->search);
            l->Say(shown,box.x0+16,box.y0+12,22,skin::kTextDim,Align::left,false,box.W()-32);
        }else{
            CopyName(shown,search_+(typing_ ? L"_" : L""));
            l->Say(shown,box.x0+16,box.y0+12,22,skin::kText,Align::left,false,box.W()-32);
        }
    }
};

}  // namespace crew::armory::wpage
