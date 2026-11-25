// 整备界面武器页的纯逻辑（docs/plan-2026-11-24-ui-rewrite.md 阶段 5）：按（兵种, 槽）把武器表折叠成系列列表，
// 带持有、等级上限、来源筛选、游戏语言名搜索、收藏、NEW / UP 标记、展开 / 折叠、光标，以及装备判定与属性对比。
// 无游戏依赖：运行时数据（行号、持有位、上限、当前装备）由 weapon_equip.h 读出后传进来；离线测试
// tests/weapon_list_logic_test.cpp。
//
// 判定规则与原版选择界面 HUiHQWeaponSelect 一致（docs/weapon-select-re.md）：
//   - 只列持有（WeaponRecord flags bit0）的行；showUnowned 时也列未持有的，但不能装备。
//   - 等级上限生效（cap>=0）时 (unsigned)level > (unsigned)cap 的不能装备（0x8BC850）。
//   - 槽的 allowDuplicate 为假时，本兵种别的槽装着的行（且不是本槽当前那把）不能装备（0x8B5145..0x8B517B）。
//   - 类别必须在本槽的 SoldierInit 类别表里（列表本来就只按这些类别建）。
#pragma once
#include "weapon_series.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <iterator>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace crew::wlist {

inline constexpr int kSlotsMost=6;

// ---- NEW / UP 标记（原版 0x8B519A..0x8B51D5，docs/weapon-select-re.md §5） ----

enum class Badge : std::uint8_t { none, fresh, up };
// WeaponRecord flags → 原版条目状态：(flags&6)==4 → 1（WeaponNew）；bit1 → 2+bit3；否则 bit3 ? 4 : 0。3 / 4 显示 WeaponUp。
// 原版选择界面只读不清：bit2 / bit3 由出击时的 SetUsedWeapon（0x70FAF0 → 0xDF600 / 0xDF620）清掉。
inline Badge BadgeOf(std::uint32_t flags) noexcept {
    if((flags&6u)==4u)return Badge::fresh;
    return (flags&8u) ? Badge::up : Badge::none;
}

// ---- 目录：生成数据 + 运行时行号 ----

struct Entry {
    int row=-1;                   // 运行时表下标
    int category=0;
    int level=0;
    std::uint8_t source=kSourceEdf6;
    std::uint8_t cls=0;
    std::uint8_t flags=0;
    std::uint32_t series=0;       // Catalog::series 下标
    const WeaponRowInfo* info=nullptr;   // 生成数据里的行；运行时额外行为空
    std::wstring extraName;       // 额外行的名字（各语言同一个）
    std::wstring_view Name(int lang) const noexcept {
        if(info) return info->name[(lang>=0 && lang<kNameLangs)?lang:0];
        return extraName;
    }
    std::wstring_view Id() const noexcept { return info?std::wstring_view(info->id):std::wstring_view(); }
};

struct Series {
    std::uint8_t source=kSourceEdf6;
    std::uint8_t cls=0;
    int category=0;
    std::vector<std::uint32_t> entries;   // Catalog::entries 下标，按等级、表序
    const WeaponSeriesInfo* info=nullptr;
    std::wstring_view Label(int lang,const std::vector<Entry>& all) const noexcept {
        if(info) return info->label[(lang>=0 && lang<kNameLangs)?lang:0];
        return entries.empty()?std::wstring_view():all[entries.front()].Name(lang);
    }
};

struct Catalog {
    std::vector<Entry> entries;
    std::vector<Series> series;
    std::vector<WeaponSlotRule> rules;
    std::unordered_map<int,std::uint32_t> byRow;

    // rowOf(id) -> 运行时表下标，不在表里返回 -1（不列出：表里没有就是没装上）。
    template<class RowOf>
    static Catalog Build(const WeaponRowInfo* rows,std::size_t rowCount,const WeaponSeriesInfo* seriesInfo,
                         std::size_t seriesCount,const WeaponSlotRule* slotRules,std::size_t ruleCount,RowOf&& rowOf) {
        Catalog c;
        c.rules.assign(slotRules,slotRules+ruleCount);
        c.series.resize(seriesCount);
        for(std::size_t s=0;s<seriesCount;++s){
            auto& out=c.series[s];
            out.source=seriesInfo[s].source;out.cls=seriesInfo[s].cls;out.category=seriesInfo[s].category;
            out.info=seriesInfo+s;
        }
        for(std::size_t i=0;i<rowCount;++i){
            const auto& r=rows[i];
            if(r.series>=seriesCount)continue;
            const int row=rowOf(r.id);
            if(row<0 || c.byRow.count(row))continue;
            Entry e;
            e.row=row;e.category=r.category;e.level=r.level;e.source=r.source;e.cls=r.cls;e.flags=r.flags;
            e.series=r.series;e.info=&r;
            c.byRow.emplace(row,static_cast<std::uint32_t>(c.entries.size()));
            c.series[r.series].entries.push_back(static_cast<std::uint32_t>(c.entries.size()));
            c.entries.push_back(std::move(e));
        }
        return c;
    }
    template<class RowOf>
    static Catalog BuildGenerated(RowOf&& rowOf) {
        return Build(kWeaponRows,std::size(kWeaponRows),kWeaponSeries,std::size(kWeaponSeries),kWeaponSlotRules,
                     std::size(kWeaponSlotRules),std::forward<RowOf>(rowOf));
    }

    // 运行时表里有、生成数据里没有的行（别的 mod 追加）：来源 kSourceOther，自成一个系列。cls 由类别查槽规则；
    // 类别不在任何槽里的返回 false（不列出）。
    bool AddExtra(int row,int category,int level,std::wstring name) {
        if(row<0 || byRow.count(row))return false;
        int cls=-1;
        for(const auto& r:rules)
            for(int k=0;k<kSlotCategoriesMost && r.categories[k]>=0;++k)
                if(r.categories[k]==category)cls=r.cls;
        if(cls<0)return false;
        Entry e;
        e.row=row;e.category=category;e.level=level<0?0:level;e.source=kSourceOther;e.cls=static_cast<std::uint8_t>(cls);
        e.series=static_cast<std::uint32_t>(series.size());e.extraName=std::move(name);
        Series s;
        s.source=kSourceOther;s.cls=e.cls;s.category=category;
        s.entries.push_back(static_cast<std::uint32_t>(entries.size()));
        byRow.emplace(row,static_cast<std::uint32_t>(entries.size()));
        entries.push_back(std::move(e));
        series.push_back(std::move(s));
        return true;
    }

    const WeaponSlotRule* Rule(int cls,int slot) const noexcept {
        for(const auto& r:rules)
            if(r.cls==cls && r.slot==slot)return &r;
        return nullptr;
    }
    static bool Takes(const WeaponSlotRule& rule,int category) noexcept {
        for(int k=0;k<kSlotCategoriesMost && rule.categories[k]>=0;++k)
            if(rule.categories[k]==category)return true;
        return false;
    }
    int SlotCount(int cls) const noexcept {
        int n=0;
        for(const auto& r:rules)
            if(r.cls==cls)n=(std::max)(n,r.slot+1);
        return n;
    }
    const Entry* ByRow(int row) const noexcept {
        const auto it=byRow.find(row);
        return it==byRow.end()?nullptr:&entries[it->second];
    }
};

// ---- 运行时状态与筛选 ----

struct Loadout {
    std::function<bool(int row)> owned;   // WeaponRecord flags bit0
    int levelCap=-1;                      // -1：没有上限（GS+0x14FC8 为 0，或 0xD8850 返回 -1）
    std::array<int,kSlotsMost> equipped{-1,-1,-1,-1,-1,-1};   // 当前兵种各槽的行（-1 空）
    std::set<std::wstring> favourites;    // WEAPONTABLE 名
    std::function<Badge(int row)> badge;  // WeaponRecord flags 的 NEW / UP（BadgeOf）；空：不标
};

struct Filter {
    int cls=0;
    int slot=0;
    unsigned sources=0xF;        // bit Source
    int category=-1;             // -1：本槽全部类别
    std::wstring search;         // 游戏语言的名字 / 系列名，大小写与全半角不敏感
    bool showUnowned=false;
    bool favouritesOnly=false;
    int lang=0;                  // hudtext::Lang
};

enum class Verdict { ok, same, header, notOwned, capped, wrongSlot, duplicate, absent };

// 规则同原版（文件头），只用表里读到的事实：category / level 是表行的 +0x10 / +0x20，owned 是持有位，levelCap -1 为
// 无上限，equipped 是本兵种各槽当前的行。weapon_equip.cpp 写之前用它判一次，列表用 CheckEquip。
// same：本槽已经装着这一行。
inline Verdict CheckEquipRow(const WeaponSlotRule* rules,std::size_t ruleCount,int cls,int slot,int row,int category,
                             int level,bool owned,int levelCap,const std::array<int,kSlotsMost>& equipped) noexcept {
    const WeaponSlotRule* rule=nullptr;
    int slots=0;
    for(std::size_t i=0;i<ruleCount;++i){
        if(rules[i].cls!=cls)continue;
        slots=(std::max)(slots,rules[i].slot+1);
        if(rules[i].slot==slot)rule=rules+i;
    }
    if(!rule || row<0)return Verdict::absent;
    if(!Catalog::Takes(*rule,category))return Verdict::wrongSlot;
    if(slot<kSlotsMost && equipped[static_cast<std::size_t>(slot)]==row)return Verdict::same;
    if(!owned)return Verdict::notOwned;
    if(levelCap>=0 && static_cast<unsigned>(level)>static_cast<unsigned>(levelCap))return Verdict::capped;
    if(!rule->allowDuplicate)
        for(int s=0;s<(std::min)(slots,kSlotsMost);++s)
            if(s!=slot && equipped[static_cast<std::size_t>(s)]==row)return Verdict::duplicate;
    return Verdict::ok;
}
inline Verdict CheckEquip(const Catalog& c,const Loadout& l,int cls,int slot,int row) {
    const Entry* e=c.ByRow(row);
    if(!e)return Verdict::absent;
    if(e->cls!=cls)return c.Rule(cls,slot)?Verdict::wrongSlot:Verdict::absent;
    return CheckEquipRow(c.rules.data(),c.rules.size(),cls,slot,row,e->category,e->level,l.owned && l.owned(row),
                         l.levelCap,l.equipped);
}

// 一次写一个兵种的几个槽（整备方案切换，weapon_equip.cpp SetClassSlots）：want[s] 为 -1 的槽不动。重复按「写完以后」
// 判：两槽互换也能写。某槽被拒（不在表里 / 没持有 / 超上限 / 类别不符 / 重复）就留原值，再重判其它槽（留下的原值可能
// 和别的槽重复），直到不再变化；只会由「要写」退回「原值」，所以最多 kSlotsMost 轮。
// facts(row, &category, &level, &owned)：false = 表里没有这一行（absent）。
// verdict[s]：ok 要写，same 本来就是（want -1 的槽也记 same），其余是被拒的原因。
struct ClassPlan {
    std::array<int,kSlotsMost> slots{};              // 写完以后各槽的行
    std::array<Verdict,kSlotsMost> verdict{};
    int changed=0;                                   // slots 与 current 不同的槽数
};
template<class Facts>
ClassPlan PlanClassSlots(const WeaponSlotRule* rules,std::size_t ruleCount,int cls,const std::array<int,kSlotsMost>& current,
                         const std::array<int,kSlotsMost>& want,int levelCap,Facts&& facts) {
    ClassPlan p;
    p.slots=current;
    p.verdict.fill(Verdict::same);
    for(std::size_t s=0;s<want.size();++s)
        if(want[s]>=0)p.slots[s]=want[s];
    for(bool again=true;again;){
        again=false;
        for(std::size_t s=0;s<want.size();++s){
            if(want[s]<0 || want[s]==current[s] || p.slots[s]!=want[s])continue;
            std::array<int,kSlotsMost> probe=p.slots;
            probe[s]=current[s];
            int category=-1,level=0;
            bool owned=false;
            const Verdict v=facts(want[s],&category,&level,&owned)
                ? CheckEquipRow(rules,ruleCount,cls,static_cast<int>(s),want[s],category,level,owned,levelCap,probe)
                : Verdict::absent;
            p.verdict[s]=v;
            if(v!=Verdict::ok){p.slots[s]=current[s];again=true;}
        }
    }
    for(std::size_t s=0;s<want.size();++s)
        if(p.slots[s]!=current[s])++p.changed;
    return p;
}

// ---- 搜索 ----

inline wchar_t FoldChar(wchar_t ch) noexcept {
    if(ch>=0xFF01 && ch<=0xFF5E)ch=static_cast<wchar_t>(ch-0xFEE0);   // 全角 ASCII -> 半角
    if(ch==0x3000)ch=L' ';
    if(ch>=0x30A1 && ch<=0x30F6)ch=static_cast<wchar_t>(ch-0x60);     // 片假名 -> 平假名
    if(ch>=L'A' && ch<=L'Z')ch=static_cast<wchar_t>(ch-L'A'+L'a');
    return ch;
}
inline std::wstring Fold(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for(const wchar_t ch:s){
        const wchar_t f=FoldChar(ch);
        if(f==L' ' || f==L'-' || f==0x30FB || f==0x00B7)continue;   // 空格、连字符、中点不参与匹配
        out.push_back(f);
    }
    return out;
}
inline bool Matches(std::wstring_view text,const std::wstring& foldedNeedle) {
    return foldedNeedle.empty() || Fold(text).find(foldedNeedle)!=std::wstring::npos;
}

// ---- 列表 ----

enum class LineKind { series, weapon, child };

struct Line {
    LineKind kind=LineKind::weapon;
    std::uint32_t series=0;
    std::uint32_t entry=0;        // weapon / child：Catalog::entries 下标；series：第一个可见成员
    int row=-1;                   // weapon / child 的表下标；series 为 -1
    int shown=1;                  // series：可见成员数
    int owned=0;                  // series：可见成员里持有的数；weapon / child：0 或 1
    int levelMin=0,levelMax=0;
    bool expanded=false;          // series
    bool equippedHere=false;      // 本槽装着（series：某个成员）
    bool favourite=false;
    Badge badge=Badge::none;      // series：可见成员里有 NEW 取 NEW，否则有 UP 取 UP
    Verdict verdict=Verdict::header;
};

struct ExpandState {
    std::set<std::uint32_t> open;
    bool IsOpen(std::uint32_t s) const { return open.count(s)!=0; }
    void Toggle(std::uint32_t s) { if(!open.erase(s))open.insert(s); }
    void Open(std::uint32_t s) { open.insert(s); }
    void Close(std::uint32_t s) { open.erase(s); }
};

// 系列按目录顺序（来源、兵种、类别、最低等级、表序）；一个系列只有一个可见成员时直接是一行武器。
// 搜索非空时有命中的系列自动展开（命中系列名则全部成员可见）。
inline std::vector<Line> BuildList(const Catalog& c,const Loadout& l,const Filter& f,const ExpandState& x) {
    std::vector<Line> out;
    const WeaponSlotRule* rule=c.Rule(f.cls,f.slot);
    if(!rule)return out;
    const std::wstring needle=Fold(f.search);
    const int here=(f.slot>=0 && f.slot<kSlotsMost)?l.equipped[static_cast<std::size_t>(f.slot)]:-1;
    std::vector<std::uint32_t> shown;
    for(std::uint32_t s=0;s<c.series.size();++s){
        const Series& ser=c.series[s];
        if(ser.cls!=f.cls || !((f.sources>>ser.source)&1u) || !Catalog::Takes(*rule,ser.category))continue;
        if(f.category>=0 && ser.category!=f.category)continue;
        const bool labelHit=!needle.empty() && Matches(ser.Label(f.lang,c.entries),needle);
        shown.clear();
        for(const std::uint32_t ei:ser.entries){
            const Entry& e=c.entries[ei];
            const bool own=l.owned && l.owned(e.row);
            const bool fav=l.favourites.count(std::wstring(e.Id()))!=0;
            if(!own && !f.showUnowned && e.row!=here)continue;
            if(f.favouritesOnly && !fav)continue;
            if(!needle.empty() && !labelHit && !Matches(e.Name(f.lang),needle))continue;
            shown.push_back(ei);
        }
        if(shown.empty())continue;
        auto weaponLine=[&](std::uint32_t ei,LineKind kind){
            const Entry& e=c.entries[ei];
            Line ln;
            ln.kind=kind;ln.series=s;ln.entry=ei;ln.row=e.row;ln.levelMin=ln.levelMax=e.level;
            ln.owned=(l.owned && l.owned(e.row))?1:0;
            ln.equippedHere=e.row==here;
            ln.favourite=l.favourites.count(std::wstring(e.Id()))!=0;
            ln.badge=l.badge ? l.badge(e.row) : Badge::none;
            ln.verdict=CheckEquip(c,l,f.cls,f.slot,e.row);
            return ln;
        };
        if(shown.size()==1){
            out.push_back(weaponLine(shown.front(),LineKind::weapon));
            continue;
        }
        Line head;
        head.kind=LineKind::series;head.series=s;head.entry=shown.front();
        head.shown=static_cast<int>(shown.size());
        head.levelMin=c.entries[shown.front()].level;head.levelMax=head.levelMin;
        for(const std::uint32_t ei:shown){
            const Entry& e=c.entries[ei];
            head.owned+=(l.owned && l.owned(e.row))?1:0;
            head.levelMin=(std::min)(head.levelMin,e.level);
            head.levelMax=(std::max)(head.levelMax,e.level);
            head.equippedHere=head.equippedHere || e.row==here;
            head.favourite=head.favourite || l.favourites.count(std::wstring(e.Id()))!=0;
            const Badge b=l.badge ? l.badge(e.row) : Badge::none;
            if(b==Badge::fresh || (b==Badge::up && head.badge==Badge::none))head.badge=b;
        }
        head.expanded=x.IsOpen(s) || !needle.empty();
        out.push_back(head);
        if(head.expanded)
            for(const std::uint32_t ei:shown)out.push_back(weaponLine(ei,LineKind::child));
    }
    return out;
}

// ---- 光标 ----

struct LineKey {
    LineKind kind=LineKind::weapon;
    std::uint32_t series=0;
    int row=-1;
    bool operator==(const LineKey& o) const noexcept { return kind==o.kind && series==o.series && row==o.row; }
};
inline LineKey KeyOf(const Line& ln) noexcept { return {ln.kind,ln.series,ln.kind==LineKind::series?-1:ln.row}; }

inline int ClampCursor(const std::vector<Line>& lines,int cursor) noexcept {
    if(lines.empty())return -1;
    return std::clamp(cursor,0,static_cast<int>(lines.size())-1);
}
// 上下移动；wrap 为真时越过两端绕回。
inline int MoveCursor(const std::vector<Line>& lines,int cursor,int delta,bool wrap) noexcept {
    if(lines.empty())return -1;
    const int n=static_cast<int>(lines.size());
    const int to=ClampCursor(lines,cursor)+delta;
    if(wrap)return ((to%n)+n)%n;
    return std::clamp(to,0,n-1);
}
// 列表重建后把光标放回同一行；那一行没了（折叠、筛掉）时依次找：同一武器（换了 weapon / child 形态）、它的系列行、
// 原位置。
inline int RelocateCursor(const std::vector<Line>& lines,const LineKey& was,int oldIndex) noexcept {
    if(lines.empty())return -1;
    for(std::size_t i=0;i<lines.size();++i)
        if(KeyOf(lines[i])==was)return static_cast<int>(i);
    if(was.row>=0)
        for(std::size_t i=0;i<lines.size();++i)
            if(lines[i].kind!=LineKind::series && lines[i].row==was.row)return static_cast<int>(i);
    for(std::size_t i=0;i<lines.size();++i)
        if(lines[i].series==was.series && lines[i].kind==LineKind::series)return static_cast<int>(i);
    return ClampCursor(lines,oldIndex);
}
// 某一行（如本槽当前装备）所在的系列；要把光标放到它身上，先 ExpandState::Open 这个系列再建列表、再 FindRow。
inline std::optional<std::uint32_t> SeriesOfRow(const Catalog& c,int row) {
    const Entry* e=c.ByRow(row);
    if(!e)return std::nullopt;
    return e->series;
}
inline int FindRowLine(const std::vector<Line>& lines,int row) noexcept {
    for(std::size_t i=0;i<lines.size();++i)
        if(lines[i].kind!=LineKind::series && lines[i].row==row)return static_cast<int>(i);
    for(std::size_t i=0;i<lines.size();++i)
        if(lines[i].kind==LineKind::series && lines[i].equippedHere)return static_cast<int>(i);
    return -1;
}

// ---- 收藏（存成 WEAPONTABLE 名，逗号分隔；名字只许 [A-Za-z0-9_]） ----

inline bool ValidId(std::string_view s) noexcept {
    if(s.empty() || s.size()>64)return false;
    for(const char ch:s)
        if(!((ch>='0' && ch<='9') || (ch>='A' && ch<='Z') || (ch>='a' && ch<='z') || ch=='_'))return false;
    return true;
}
// 不合法的项整份拒绝（返回 false，out 不动）。
inline bool ParseFavourites(std::string_view text,std::set<std::wstring>* out) {
    std::set<std::wstring> got;
    while(!text.empty()){
        const std::size_t comma=text.find(',');
        std::string_view item=text.substr(0,comma);
        while(!item.empty() && item.front()==' ')item.remove_prefix(1);
        while(!item.empty() && item.back()==' ')item.remove_suffix(1);
        if(!item.empty()){
            if(!ValidId(item))return false;
            got.emplace(item.begin(),item.end());
        }
        if(comma==std::string_view::npos)break;
        text.remove_prefix(comma+1);
    }
    *out=std::move(got);
    return true;
}
inline std::string SerializeFavourites(const std::set<std::wstring>& favs) {
    std::string out;
    for(const auto& id:favs){
        std::string a;
        bool ok=true;
        for(const wchar_t ch:id){
            if(ch>0x7F){ok=false;break;}
            a.push_back(static_cast<char>(ch));
        }
        if(!ok || !ValidId(a))continue;
        if(!out.empty())out.push_back(',');
        out+=a;
    }
    return out;
}
inline void ToggleFavourite(std::set<std::wstring>* favs,std::wstring_view id) {
    std::wstring key(id);
    if(!favs->erase(key))favs->insert(std::move(key));
}

// ---- 属性（原版说明栏 0xD8A90 的数据与格式，docs/weapon-select-re.md §4） ----
//
// WEAPONTEXT.<语言>.SGO 的 text_table[row] = [名字, 说明, 属性表]；属性表每项 = [标签, 格式, 参数 0, 参数 1, ...]。
// 参数是数组 [基值, 曲线|显示类型<<2, 星级下标, 星级上限, 幅度, 指数, 是否小数] 时由 0xD7CE0 按该行武器记录的星级算值，
// 是标量时就是它本身（显示类型 0、小数）。运行时读取见 weapon_equip.h ReadSpec。

struct SpecParam {
    float value=0;               // 0xD7CE0 +0
    int type=0;                  // +4：显示类型（round(n[1])>>2）。3 精度等级，6 / 7 显示 60/值
    int star=0,maxStar=0;        // +8 / +9
    bool isFloat=true;           // +0xC：round(n[6])!=0；标量恒为真
    int curve=-1;                // round(n[1])&3：0 星级越高值越大，1 越高越小；-1 标量或别的曲线
};
struct SpecLine {
    std::wstring label;          // 子节点 0
    std::wstring format;         // 子节点 1
    int nodes=0;                 // 子节点数：<2 无值，==2 格式原样，>2 替换 $n
    std::vector<SpecParam> params;   // 子节点 2..
};
using WeaponSpec=std::vector<SpecLine>;

// 0x1767DE0：21 项 {标签, 阈值}，显示类型 3 取第一个阈值大于值的项（upper_bound），都不大于取最后一项。
struct SpecRank { const wchar_t* label; float below; };
inline constexpr SpecRank kSpecRanks[21]={
    {L"S++",0.0005f},{L"S+",0.0025f},{L"S",0.005f},{L"A+",0.01f},{L"A",0.015f},{L"A-",0.02f},{L"B+",0.03f},{L"B",0.05f},
    {L"B-",0.1f},{L"C+",0.15f},{L"C",0.2f},{L"C-",0.25f},{L"D",0.3f},{L"E",0.4f},{L"F",0.5f},{L"G",0.6f},{L"I",0.8f},
    {L"J",1.0f},{L"K",1.2f},{L"L",1.4f},{L"Z",10000000.0f}};

// 0xD9B80 的四舍五入：|x| 加 0.5 截断，符号跟 x。
inline int SpecRound(float v) noexcept { return static_cast<int>(v>=0.0f ? v+0.5f : v-0.5f); }
// 小数按大小取位数：>=1 "%.1f"，>=0.1 "%.2f"，否则 "%.3f"（0x1767FC0 / FB0 / FA0）。
inline std::wstring SpecDecimal(float v) {
    wchar_t buf[64];
    const wchar_t* fmt=v>=0.1f ? (v>=1.0f ? L"%.1f" : L"%.2f") : L"%.3f";
    std::swprintf(buf,std::size(buf),fmt,static_cast<double>(v));
    return buf;
}
inline std::wstring FormatSpecParam(const SpecParam& p) {
    if(p.type==3){
        const SpecRank* r=std::upper_bound(std::begin(kSpecRanks),std::end(kSpecRanks),p.value,
                                           [](float v,const SpecRank& k){return v<k.below;});
        return r==std::end(kSpecRanks) ? std::end(kSpecRanks)[-1].label : r->label;
    }
    if(p.type==6 || p.type==7)return SpecDecimal(60.0f/static_cast<float>(SpecRound(p.value)));
    if(!p.isFloat){
        wchar_t buf[16];
        std::swprintf(buf,std::size(buf),L"%d",SpecRound(p.value));
        return buf;
    }
    return SpecDecimal(p.value);
}
// 一行的值文字。"$d"（d 为数字）换成参数 d；"$" 后跟别的字符时两个字符都丢掉（原版同样跳过）。
// 原版对越界的 $d 不检查（0x52DE0 读到别的节点）；这里不输出。
inline std::wstring FormatSpecLine(const SpecLine& ln) {
    if(ln.nodes<2)return {};
    if(ln.nodes==2)return ln.format;
    std::wstring out;
    for(std::size_t i=0;i<ln.format.size();++i){
        const wchar_t ch=ln.format[i];
        if(ch!=L'$'){out.push_back(ch);continue;}
        if(++i>=ln.format.size())break;
        const wchar_t d=ln.format[i];
        if(d<L'0' || d>L'9')continue;
        const std::size_t at=static_cast<std::size_t>(d-L'0');
        if(at<ln.params.size())out+=FormatSpecParam(ln.params[at]);
    }
    return out;
}

// 对比一行：参数按下标配对，方向取有曲线的一边（0 大者好，1 小者好）；各参数结论一致才给方向。
struct SpecDiff {
    std::wstring label;
    std::optional<std::wstring> equipped,candidate;   // 值文字；缺一边为空
    int better=0;                 // +1 光标这把更好，-1 更差，0 相同、说不出或缺一边
};
inline int CompareSpecLine(const SpecLine& a,const SpecLine& b) noexcept {
    const std::size_t n=(std::min)(a.params.size(),b.params.size());
    int verdict=0;
    for(std::size_t i=0;i<n;++i){
        const SpecParam& x=a.params[i];
        const SpecParam& y=b.params[i];
        const int curve=y.curve>=0 ? y.curve : x.curve;
        if(curve<0 || x.value==y.value)continue;
        const int sign=((y.value>x.value)==(curve==0)) ? 1 : -1;
        if(verdict!=0 && verdict!=sign)return 0;
        verdict=sign;
    }
    return verdict;
}
// 按候选的行序，标签相同的配对；只在装着那把里有的行接在后面。
inline std::vector<SpecDiff> CompareSpecs(const WeaponSpec* equipped,const WeaponSpec* candidate) {
    std::vector<SpecDiff> out;
    std::vector<bool> used(equipped ? equipped->size() : 0,false);
    if(candidate)
        for(const SpecLine& c:*candidate){
            SpecDiff d;
            d.label=c.label;
            d.candidate=FormatSpecLine(c);
            if(equipped)
                for(std::size_t i=0;i<equipped->size();++i){
                    if(used[i] || (*equipped)[i].label!=c.label)continue;
                    used[i]=true;
                    d.equipped=FormatSpecLine((*equipped)[i]);
                    if(*d.equipped!=*d.candidate)d.better=CompareSpecLine((*equipped)[i],c);
                    break;
                }
            out.push_back(std::move(d));
        }
    if(equipped)
        for(std::size_t i=0;i<equipped->size();++i){
            if(used[i])continue;
            SpecDiff d;
            d.label=(*equipped)[i].label;
            d.equipped=FormatSpecLine((*equipped)[i]);
            out.push_back(std::move(d));
        }
    return out;
}

}  // namespace crew::wlist
