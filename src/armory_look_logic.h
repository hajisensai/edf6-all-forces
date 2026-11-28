#pragma once
// 整备界面的「外观」页：换外观（GS+0x6E94，含 EDF4.1 外观）与配色（原版 HUiHQColorSelect 写的同一块），框架
// armory_logic.h 上的一页，纯逻辑：游戏侧经 Game 接口，运行时 src/armory.cpp 接 GameStatus / outfit41 / roompreview，
// 离线测试 tests/armory_look_logic_test.cpp 接假的。原版整备里的外观 / 配色入口已随原版兵种选择一起去掉（c043e1f7）。
//
// 布局（三列）：
//   列表   上方兵种分页（Z / C、LT / RT），三行：外观（值是现在穿的）、主色、副色（值是选中的色号，带色块）。
//   中间   外观行：随任务、外观 1..4（未解锁时原版不让换，只剩随任务）、EDF4.1 外观（outfit41 装了且本兵种有）；
//          主色 / 副色行：12 个色块（原版配色界面的同一组，GameStatus 里每兵种每外观一块）。◆ 现在的选择，确认键
//          立即写（像原版一样没有「保存」一步），写完 3D 预览重建。
//   详情   上方 7 行留给 3D 预览（Hole：只能显示当前兵种），没有字段。
// 外观是每个玩家一格（不分兵种）；配色改的是本兵种「现在穿的外观」（随任务时按任务解析，0xD8700）那一块。
#include "armory_logic.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace crew::armory::look {

inline constexpr int kClasses=6, kStockClasses=4;   // 兵种号：原版 0..3，插件 4 载具手、5 刻印剑士
inline constexpr int kOutfits=4;                    // 每兵种外观 0..3（新兵种同样 4 个）；-1 随任务
inline constexpr int kMission=-1;
inline constexpr int kSwatches=12, kChannels=2;     // 原版配色界面：主色 / 副色各 12 个

// GameStatus 里的位置（相对 GS = [EDF+0x20B2890]；0x882790 / 0x8817A3 / 0x8AD5DC 读写的同一套）。
inline constexpr std::uintptr_t kPlayerStride=0x3E60;
inline constexpr std::uintptr_t kGsClass=0x6E90, kGsOutfit=0x6E94;
inline constexpr std::uintptr_t kGsColours=0x6FB0, kChannelStride=0xC4, kSelected=0xC0, kSwatchStride=0x10;
inline constexpr int kColourClasses=10;   // GameStatus 留了 10 个兵种 × 4 外观的块（每玩家 0x6FB0 起 0x3D40）

constexpr bool ValidClass(int cls) noexcept { return cls>=0 && cls<kClasses; }
constexpr bool ValidOutfit(int outfit) noexcept { return outfit>=0 && outfit<kOutfits; }
// 一个通道（主 0 / 副 1）的块，相对 GS。
constexpr std::uintptr_t ChannelOffset(int player,int cls,int outfit,int ch) noexcept {
    return static_cast<std::uintptr_t>(player)*kPlayerStride+kGsColours+
           static_cast<std::uintptr_t>((cls*kOutfits+outfit)*kChannels+ch)*kChannelStride;
}
constexpr std::uintptr_t SelectedOffset(int player,int cls,int outfit,int ch) noexcept {
    return ChannelOffset(player,cls,outfit,ch)+kSelected;
}
constexpr std::uintptr_t SwatchOffset(int player,int cls,int outfit,int ch,int idx) noexcept {
    return ChannelOffset(player,cls,outfit,ch)+static_cast<std::uintptr_t>(idx)*kSwatchStride;
}
constexpr bool ValidColour(int cls,int outfit,int ch,int idx) noexcept {
    return ValidClass(cls) && ValidOutfit(outfit) && ch>=0 && ch<kChannels && idx>=0 && idx<kSwatches;
}

// 色块 RGBA（0..1 浮点）→ 0xFFRRGGBB（越界的夹住；色块总是不透明画）。
inline std::uint32_t Argb(const float rgba[4]) noexcept {
    std::uint32_t out=0xFF000000u;
    for(int i=0;i<3;++i){
        float v=rgba[i];
        if(!(v>0.0f))v=0.0f;   // NaN 也归 0
        if(v>1.0f)v=1.0f;
        const auto b=static_cast<std::uint32_t>(v*255.0f+0.5f);
        out|=b<<(16-8*i);
    }
    return out;
}

// 外观行的一个选项。
struct Option { int outfit=kMission; bool edf41=false; };
inline constexpr int kOptionMost=1+kOutfits+1;
// 选项表：随任务，解锁了再加外观 0..3，本兵种有 EDF4.1 外观时最后加它。返回个数。
inline int Options(bool unlocked,bool edf41,Option* out) noexcept {
    int n=0;
    out[n++]=Option{kMission,false};
    if(unlocked)
        for(int o=0;o<kOutfits;++o)out[n++]=Option{o,false};
    if(edf41)out[n++]=Option{kMission,true};
    return n;
}
// 现在的选择在表里第几项：穿着 EDF4.1（且表里有）就是它，否则按 GS 外观格；不在表里 -1。
inline int OptionIndex(const Option* opts,int n,int outfit,bool edf41Chosen) noexcept {
    for(int i=0;i<n;++i)
        if(edf41Chosen ? opts[i].edf41 : (!opts[i].edf41 && opts[i].outfit==outfit))return i;
    if(edf41Chosen)   // 本兵种没有 4.1：按原版外观格
        for(int i=0;i<n;++i)
            if(!opts[i].edf41 && opts[i].outfit==outfit)return i;
    return -1;
}

// 一个兵种一个外观的配色（GameStatus 里的原样）。
struct Palette {
    float rgba[kChannels][kSwatches][4]{};
    int selected[kChannels]{-1,-1};
};

// 游戏侧。只在 UI 线程调。
class Game {
public:
    virtual bool Ready() const noexcept=0;                     // GameStatus 可读、地址核对过
    virtual int CurrentClass() const noexcept=0;               // -1 读不到
    virtual int Classes() const noexcept=0;                    // 游戏现有兵种数，-1 读不到
    virtual int Outfit() const noexcept=0;                     // GS 外观格（-1 随任务 .. 3）
    virtual int WornOutfit() const noexcept=0;                 // 现在穿的（随任务按任务解析，0xD8700），-1 读不到
    virtual bool Unlocked() const noexcept=0;                  // 原版的外观切换解锁（GS+0x300）
    virtual bool Edf41Offered(int cls) const noexcept=0;       // outfit41 装了、开着、本兵种有
    virtual bool Edf41Chosen() const noexcept=0;               // 现在穿着 EDF4.1 外观
    virtual bool PickOutfit(const Option& o) noexcept=0;       // 写外观（4.1 走 outfit41），false 没写成
    virtual bool ReadColours(int cls,int outfit,Palette* out) noexcept=0;
    virtual bool PickColour(int cls,int outfit,int ch,int idx) noexcept=0;
protected:
    ~Game()=default;
};

// 本地化文字（hudtext），运行时给。
struct Texts {
    const wchar_t* name=L"Look";
    const wchar_t* cls[kClasses]{L"R",L"W",L"A",L"F",L"V",L"S"};
    const wchar_t* outfit=L"Outfit";
    const wchar_t* primary=L"Primary colour";
    const wchar_t* secondary=L"Secondary colour";
    const wchar_t* mission=L"By mission";
    const wchar_t* numbered=L"Outfit %d";      // 一个 %d：外观号（1 起）
    const wchar_t* colour=L"Colour %d";        // 一个 %d：色号（1 起）
    const wchar_t* edf41=L"EDF4.1";
    const wchar_t* locked=L"";                 // 外观切换未解锁（灰显说明行）
    const wchar_t* failed=L"";                 // 没写成
    const wchar_t* notReady=L"";
    const wchar_t* previewOther=L"";           // 不是当前兵种：3D 预览只显示当前兵种
    const wchar_t* hints[3]{};
};

namespace geo {
// 3D 预览的洞：详情列头 7 行（Row(2,0..6)，与武器页同一处）。
inline skin::Rect Preview() noexcept {
    const skin::Rect a=skin::layout::Row(2,0), b=skin::layout::Row(2,6);
    return skin::Rect{a.x0,a.y0,a.x1,b.y1};
}
}  // namespace geo
inline constexpr int kHeaderDetails=7;
inline constexpr int kRowOutfit=0, kRowPrimary=1, kRowSecondary=2;

class LookPage final : public Page {
public:
    void Attach(Game* game,const Texts* texts) noexcept { game_=game;texts_=texts;Refresh(); }
    // 重读（打开整备、每帧 Relocate、写完之后）：GameStatus 可能被别处改过（武器页换兵种、房间同步）。
    void Refresh() noexcept {
        ready_=game_ && texts_ && game_->Ready();
        shown_=kStockClasses;
        if(ready_){
            const int n=game_->Classes();
            if(n>kStockClasses)shown_=n<kClasses ? n : kClasses;
            outfit_=game_->Outfit();
            worn_=game_->WornOutfit();
            unlocked_=game_->Unlocked();
            chosen41_=game_->Edf41Chosen();
        }
        for(int c=0;c<kClasses;++c){
            offered41_[c]=ready_ && c<shown_ && game_->Edf41Offered(c);
            havePalette_[c]=ready_ && c<shown_ && ValidOutfit(worn_) && game_->ReadColours(c,worn_,&palette_[c]);
        }
    }

    // ---- Page ----
    const wchar_t* Name() const noexcept override { return texts_ ? texts_->name : L""; }
    int Subs() const noexcept override { return shown_; }
    const wchar_t* SubName(int sub) const noexcept override {
        return texts_ && sub>=0 && sub<shown_ ? texts_->cls[sub] : L"";
    }
    int Rows(int sub) const noexcept override {
        if(!ready_ || sub<0 || sub>=shown_)return 0;
        return havePalette_[sub] ? 3 : 1;
    }
    void RowLine(int sub,int row,Line* out) const noexcept override {
        if(row==kRowOutfit){
            CopyText(out->label,kTextMost,texts_->outfit);
            Option opts[kOptionMost];
            const int n=OptionsOf(sub,opts);
            const int at=OptionIndex(opts,n,outfit_,chosen41_);
            if(at>=0)OptionName(opts[at],out->value);
            else NumberedName(outfit_,out->value);
            return;
        }
        const int ch=row-kRowPrimary;
        CopyText(out->label,kTextMost,ch==0 ? texts_->primary : texts_->secondary);
        const int sel=palette_[sub].selected[ch];
        if(sel>=0 && sel<kSwatches){
            std::swprintf(out->value,kTextMost,texts_->colour,sel+1);
            out->chip=Argb(palette_[sub].rgba[ch][sel]);
        }else CopyText(out->value,kTextMost,L"-");
    }
    int Items(const Pos& p) const noexcept override {
        if(Rows(p.sub)<=p.row || p.row<0)return 0;
        if(p.row==kRowOutfit){
            Option opts[kOptionMost];
            return OptionsOf(p.sub,opts)+(unlocked_ ? 0 : 1);   // 未解锁：末尾一行灰显说明
        }
        return kSwatches;
    }
    void ItemLine(const Pos& p,Line* out) const noexcept override {
        if(p.row==kRowOutfit){
            Option opts[kOptionMost];
            const int n=OptionsOf(p.sub,opts);
            if(p.item>=n){CopyText(out->label,kTextMost,texts_->locked);out->dim=true;return;}
            wchar_t name[kTextMost];
            OptionName(opts[p.item],name);
            Mark(out->label,p.item==OptionIndex(opts,n,outfit_,chosen41_),name);
            out->acts=true;
            return;
        }
        const int ch=p.row-kRowPrimary;
        if(ch<0 || ch>=kChannels || p.item<0 || p.item>=kSwatches)return;
        wchar_t name[kTextMost];
        std::swprintf(name,kTextMost,texts_->colour,p.item+1);
        Mark(out->label,palette_[p.sub].selected[ch]==p.item,name);
        out->chip=Argb(palette_[p.sub].rgba[ch][p.item]);
        out->acts=true;
    }
    int Fields(const Pos&) const noexcept override { return 0; }
    void FieldLine(const Pos&,Line*) const noexcept override {}
    bool Activate(const Pos& p) noexcept override {
        if(!ready_ || Rows(p.sub)<=p.row || p.row<0)return false;
        bool ok=false;
        if(p.row==kRowOutfit){
            Option opts[kOptionMost];
            const int n=OptionsOf(p.sub,opts);
            if(p.item<0 || p.item>=n)return false;
            if(p.item==OptionIndex(opts,n,outfit_,chosen41_))return false;   // 已经是它
            ok=game_->PickOutfit(opts[p.item]);
        }else{
            const int ch=p.row-kRowPrimary;
            if(!ValidColour(p.sub,worn_,ch,p.item))return false;
            if(palette_[p.sub].selected[ch]==p.item)return false;
            ok=game_->PickColour(p.sub,worn_,ch,p.item);
        }
        if(!ok)note_=texts_->failed;
        Refresh();
        placed_=false;   // 外观换了：配色行跟着换块，光标重放
        return ok;
    }
    void Draw(const Pos& p,int column,Layer* l) const noexcept override {
        if(column==0 && !ready_){
            const skin::Rect r=skin::layout::Row(0,1);
            l->Say(texts_ ? texts_->notReady : L"",r.x0+22,r.y0+12,24,skin::kTextDim,Align::left,false,r.W()-40);
        }
        // 详情列没有字段，框架不为它调 Draw：预览洞那里的说明随中间列一起画
        if(column==1 && Rows(p.sub)>0){
            skin::Rect hole{};
            if(!Hole(p,&hole)){
                const skin::Rect r=geo::Preview();
                l->Say(texts_->previewOther,(r.x0+r.x1)/2,(r.y0+r.y1)/2-14,24,skin::kTextDim,Align::centre,false,
                       r.W()-40);
            }
        }
    }
    int Header(int column) const noexcept override { return column==2 ? kHeaderDetails : 0; }
    bool Hole(const Pos& p,skin::Rect* out) const noexcept override {
        if(!ready_ || Rows(p.sub)<=0 || game_->CurrentClass()!=p.sub)return false;
        *out=geo::Preview();
        return true;
    }
    void Relocate(Pos* p,int /*column*/) noexcept override {
        Refresh();
        if(Rows(p->sub)<=0){lastSub_=-1;return;}
        if(!placed_ || p->sub!=lastSub_ || p->row!=lastRow_){
            // 换了行：光标放到现在的选择上
            const int at=Current(*p);
            if(at>=0)p->item=at;
            else if(p->sub!=lastSub_ || p->row!=lastRow_)p->item=0;
        }
        lastSub_=p->sub;lastRow_=p->row;placed_=true;
    }
    bool TakeNote(wchar_t* out,std::size_t cap) noexcept override {
        if(!note_)return false;
        CopyText(out,cap,note_);
        note_=nullptr;
        return true;
    }
    const wchar_t* Hint(const Pos&,int column) const noexcept override {
        if(!texts_)return nullptr;
        return column>=0 && column<3 && texts_->hints[column] && texts_->hints[column][0] ? texts_->hints[column] : nullptr;
    }

    bool Ready() const noexcept { return ready_; }

private:
    int OptionsOf(int sub,Option* out) const noexcept {
        return Options(unlocked_,sub>=0 && sub<kClasses && offered41_[sub],out);
    }
    // 本行现在的选择在中间列第几项，-1 没有。
    int Current(const Pos& p) const noexcept {
        if(p.row==kRowOutfit){
            Option opts[kOptionMost];
            const int n=OptionsOf(p.sub,opts);
            return OptionIndex(opts,n,outfit_,chosen41_);
        }
        const int ch=p.row-kRowPrimary;
        if(ch<0 || ch>=kChannels)return -1;
        const int sel=palette_[p.sub].selected[ch];
        return sel>=0 && sel<kSwatches ? sel : -1;
    }
    void NumberedName(int outfit,wchar_t* out) const noexcept {
        if(ValidOutfit(outfit))std::swprintf(out,kTextMost,texts_->numbered,outfit+1);
        else CopyText(out,kTextMost,texts_->mission);
    }
    void OptionName(const Option& o,wchar_t* out) const noexcept {
        if(o.edf41)CopyText(out,kTextMost,texts_->edf41);
        else NumberedName(o.outfit,out);
    }
    static void Mark(wchar_t* out,bool current,const wchar_t* name) noexcept {
        if(current)std::swprintf(out,kTextMost,L"\x25C6%ls",name);
        else CopyText(out,kTextMost,name);
    }

    Game* game_=nullptr;
    const Texts* texts_=nullptr;
    bool ready_=false;
    int shown_=kStockClasses;
    int outfit_=kMission, worn_=-1;
    bool unlocked_=false, chosen41_=false;
    bool offered41_[kClasses]{};
    bool havePalette_[kClasses]{};
    Palette palette_[kClasses]{};
    const wchar_t* note_=nullptr;
    int lastSub_=-1, lastRow_=-1;
    bool placed_=false;
};

}  // namespace crew::armory::look
