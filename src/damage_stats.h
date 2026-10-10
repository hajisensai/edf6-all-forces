// The damage statistics (the user, 2026-10-10: "增加伤害统计功能。最好是图表，可以点击的那种…详细到武器…敌怪能无损做的话也
// 可以详细一点…敌怪的伤害统计能做的话也行"; "游戏内做可悬停交互的图表"; "恢复血量也在里面吧"; "武器统计治疗对象（这个敌军
// 甚至也会在里面）伤害也是，有可能会打到队友的"). The pure part: the mission's book and the page's layout, read by
// damagestats.cpp (the game thread fills the book from the damage hook) and hud.cpp (the draw thread lays the page out and
// draws it). tools/damage_stats_check.cpp runs it offline.
//
// The book is one table: every source (a side's weapon; an enemy's kind is its weapon) against every target (an enemy by
// its kind, the object's SGO name, so a red ant and a black ant are two; the player's side by group), each cell the
// damage and the heals it took from that source, its hits and kills. A friendly hit on a friend (friendly fire) and a heal
// on an enemy are cells like any other: nothing is sorted out before it is booked, the page's tabs are views of the table.
// Nothing is dropped when a table is full: its last row is "other" and takes the rest (Slot), and the timeline keeps the
// whole mission by doubling its column's span when it runs out of columns (Column).
#pragma once
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace dmgstat {
// Who dealt or gave it: a side of the player's, or an enemy.
enum class Side : std::uint8_t { me, myVehicle, squad, support, teammate, ally, enemy, count };
constexpr int kSides=static_cast<int>(Side::count);
// Who took it: the player's side by group, or an enemy.
enum class Group : std::uint8_t { me, myVehicle, teammate, ally, vehicle, enemy, count };
constexpr int kGroups=static_cast<int>(Group::count);
inline bool Friendly(Side s) noexcept { return s!=Side::enemy; }
inline bool Friendly(Group g) noexcept { return g!=Group::enemy; }

constexpr int kNameLen=40;
struct Name { wchar_t text[kNameLen]; };
inline void SetName(Name& n,const wchar_t* text) noexcept { wcsncpy_s(n.text,text ? text : L"",_TRUNCATE); }
// Whether `n` is `text` as SetName keeps it: a longer text is kept cut, so only its first kNameLen-1 characters count
// (compared whole, a long weapon name would open a new row with every hit).
inline bool SameName(const Name& n,const wchar_t* text) noexcept { return std::wcsncmp(n.text,text ? text : L"",kNameLen-1)==0; }

constexpr int kSources=96,kTargets=96;   // each table's last row is "other"
constexpr int kBuckets=120;              // the timeline's columns
constexpr std::uint32_t kFirstBucketMs=5000;   // 5 s each to start with (10 min); doubled as often as it fills

// A source: one weapon of one side (the same weapon in my hands and a squad's is two sources), or an enemy kind. Its
// damage and heals to every target, its biggest hit, its first and last.
struct Source { Name name; Side side; float damage,heal,biggest; std::uint32_t hits,heals,kills,firstMs,lastMs; };
// A target: an enemy kind, or a group of the player's side.
struct Target { Name name; Group group; float damage,heal; std::uint32_t hits,kills; };
// A source against a target.
struct Cell { float damage,heal; std::uint32_t hits,kills; };

// The timeline: `used` columns of `spanMs` from the mission's start; each column's damage each side dealt to the enemies,
// each side's damage to its own side (friendly fire), each side's HP given back to the player's side, and the enemies'
// damage to the player's side.
struct Timeline {
    std::uint32_t spanMs=kFirstBucketMs;
    int used=0;
    float dealt[kBuckets][kSides],friendlyFire[kBuckets][kSides],healed[kBuckets][kSides];
    float taken[kBuckets];
    // Each two columns made one, the span doubled (Book's Column, which halves the sources' rows with it).
    void Halve() noexcept {
        for(int i=0;i<kBuckets/2;++i) {
            for(int s=0;s<kSides;++s) {
                dealt[i][s]=dealt[2*i][s]+dealt[2*i+1][s];
                friendlyFire[i][s]=friendlyFire[2*i][s]+friendlyFire[2*i+1][s];
                healed[i][s]=healed[2*i][s]+healed[2*i+1][s];
            }
            taken[i]=taken[2*i]+taken[2*i+1];
        }
        std::memset(dealt[kBuckets/2],0,sizeof(dealt)/2);
        std::memset(friendlyFire[kBuckets/2],0,sizeof(friendlyFire)/2);
        std::memset(healed[kBuckets/2],0,sizeof(healed)/2);
        std::memset(&taken[kBuckets/2],0,sizeof(taken)/2);
        spanMs*=2;
        used=(used+1)/2;
    }
};

// One hit as the hook saw it: who (side, source: the weapon's name or the enemy's kind) on whom (group, target: the
// enemy's kind or the group's name), the HP it really took (`heal`: gave back; the difference before and after) and
// whether it took the last of it.
struct Hit {
    Side side;
    const wchar_t* source;
    Group group;
    const wchar_t* target;
    float amount;
    bool heal,killed;
    std::uint32_t ms;        // since the mission's start
};

// The mission's book.
struct Book {
    std::uint32_t serial=0;  // bumped by every hit (the draw copies it again only when it moved)
    int sources=0,targets=0;
    Source source[kSources];
    Target target[kTargets];
    Cell cell[kSources][kTargets];
    float sourceColumn[kSources][kBuckets];  // a source's damage per timeline column (Timeline's span)
    Timeline time;
    float dealt[kSides],friendlyFire[kSides],healGiven[kSides];   // each side's to the enemies, to its own, HP back to it
    float taken[kGroups],healed[kGroups];    // each group's from the enemies, back from anyone
    std::uint32_t kills[kSides],deaths[kGroups];   // enemies killed by each friendly side, the player's side's dead
};

inline void Clear(Book& b) noexcept {
    std::memset(&b,0,sizeof(b));
    b.time.spanMs=kFirstBucketMs;
}

// The row of `rows` (`n` in use of `most`) `match` finds, a new one (`fill` names it) while there is room, else the last
// row, "other" (filled once by `other`).
template<class Row,class Match,class Fill,class Other>
int Slot(Row* rows,int& n,int most,Match&& match,Fill&& fill,Other&& other) noexcept {
    for(int i=0;i<n && i<most-1;++i)if(match(rows[i]))return i;
    if(n<most-1){fill(rows[n]);return n++;}
    if(n==most-1)other(rows[n++]);
    return most-1;
}
// An "other" source counts with the whole friendly side, never with mine alone; an "other" target as an enemy.
inline int SourceOf(Book& b,Side side,const wchar_t* name,const wchar_t* other) noexcept {
    return Slot(b.source,b.sources,kSources,[&](const Source& s){return s.side==side && SameName(s.name,name);},
                [&](Source& s){SetName(s.name,name);s.side=side;},[&](Source& s){SetName(s.name,other);s.side=Side::ally;});
}
inline int TargetOf(Book& b,Group group,const wchar_t* name,const wchar_t* other) noexcept {
    return Slot(b.target,b.targets,kTargets,[&](const Target& t){return t.group==group && SameName(t.name,name);},
                [&](Target& t){SetName(t.name,name);t.group=group;},[&](Target& t){SetName(t.name,other);t.group=Group::enemy;});
}

// A source's timeline row follows the timeline's span: when it doubles, every row is halved with it.
inline void HalveSources(Book& b) noexcept {
    for(int s=0;s<b.sources;++s) {
        float* row=b.sourceColumn[s];
        for(int i=0;i<kBuckets/2;++i)row[i]=row[2*i]+row[2*i+1];
        std::memset(row+kBuckets/2,0,sizeof(float)*(kBuckets/2));
    }
}
// The timeline column `ms` falls in, the span doubled until it fits.
inline int Column(Book& b,std::uint32_t ms) noexcept {
    while(ms/b.time.spanMs>=static_cast<std::uint32_t>(kBuckets)){b.time.Halve();HalveSources(b);}
    const int c=static_cast<int>(ms/b.time.spanMs);
    if(c+1>b.time.used)b.time.used=c+1;
    return c;
}

// `other` names the rows a full table puts the rest in.
inline void Add(Book& b,const Hit& h,const wchar_t* other) noexcept {
    if(!(h.amount>0.0f))return;
    const int c=Column(b,h.ms),s=SourceOf(b,h.side,h.source,other),t=TargetOf(b,h.group,h.target,other);
    ++b.serial;
    Source& src=b.source[s];
    Target& tg=b.target[t];
    Cell& cell=b.cell[s][t];
    if(!src.hits && !src.heals)src.firstMs=h.ms;
    src.lastMs=h.ms;
    const int side=static_cast<int>(h.side),group=static_cast<int>(h.group);
    if(h.heal) {
        src.heal+=h.amount;++src.heals;tg.heal+=h.amount;cell.heal+=h.amount;
        if(Friendly(h.group)){b.time.healed[c][side]+=h.amount;b.healGiven[side]+=h.amount;b.healed[group]+=h.amount;}
        return;
    }
    src.damage+=h.amount;++src.hits;
    if(h.amount>src.biggest)src.biggest=h.amount;
    tg.damage+=h.amount;++tg.hits;
    cell.damage+=h.amount;++cell.hits;
    b.sourceColumn[s][c]+=h.amount;
    if(Friendly(h.side) && !Friendly(h.group)){b.time.dealt[c][side]+=h.amount;b.dealt[side]+=h.amount;}
    else if(Friendly(h.side)){b.time.friendlyFire[c][side]+=h.amount;b.friendlyFire[side]+=h.amount;}
    else if(Friendly(h.group)){b.time.taken[c]+=h.amount;b.taken[group]+=h.amount;}
    if(!h.killed)return;
    ++src.kills;++tg.kills;++cell.kills;
    if(Friendly(h.side) && !Friendly(h.group))++b.kills[side];
    if(Friendly(h.group))++b.deaths[group];
}

// --- What the page shows ---
// The tabs: the friendly side's weapons, the enemies by kind, the enemies' attacks on the player's side, the heals by
// weapon (whoever gave them, whomever they mended), the timeline.
enum class Tab : std::uint8_t { weapons, enemies, taken, heals, timeline, count };
constexpr int kTabs=static_cast<int>(Tab::count);
// Whose the weapons, enemies, heals and timeline tabs count: mine alone (on foot and in my vehicle), or everyone's (every
// friendly side; the heals tab: the enemies' heals too).
enum class Scope : std::uint8_t { mine, everyone, count };
inline bool InScope(Scope scope,Side side) noexcept {
    return scope==Scope::everyone ? Friendly(side) : side==Side::me || side==Side::myVehicle;
}

// A source's row summed over the targets `want` takes (by group), a target's column over the sources in `scope`.
template<class Want> Cell RowSum(const Book& b,int s,Want&& want) noexcept {
    Cell sum{};
    for(int t=0;t<b.targets;++t) {
        if(!want(b.target[t].group))continue;
        const Cell& c=b.cell[s][t];
        sum.damage+=c.damage;sum.heal+=c.heal;sum.hits+=c.hits;sum.kills+=c.kills;
    }
    return sum;
}
inline Cell ColumnSum(const Book& b,int t,Scope scope) noexcept {
    Cell sum{};
    for(int s=0;s<b.sources;++s) {
        if(!InScope(scope,b.source[s].side))continue;
        const Cell& c=b.cell[s][t];
        sum.damage+=c.damage;sum.heal+=c.heal;sum.hits+=c.hits;sum.kills+=c.kills;
    }
    return sum;
}

// The rows of a bar tab, biggest first: `order` the row indices, `value` each one's bar; how many.
inline int Rank(const float* value,int n,int* order) noexcept {
    int m=0;
    for(int i=0;i<n;++i)if(value[i]>0.0f)order[m++]=i;
    for(int i=1;i<m;++i)   // insertion sort: at most 96 rows, stable for equal values
        for(int j=i;j>0 && value[order[j]]>value[order[j-1]];--j){const int t=order[j];order[j]=order[j-1];order[j-1]=t;}
    return m;
}
// The bar values of a tab under `scope`, by the table's row: the weapons tab's sources (their damage to anyone), the
// enemies tab's targets (the damage the scope did them), the attacks tab's enemy sources (their damage to the player's
// side), the heals tab's sources (the HP they gave anyone). How many rows the values cover.
constexpr int kRows=kSources>kTargets ? kSources : kTargets;
inline int BarValues(const Book& b,Tab tab,Scope scope,float* value) noexcept {
    if(tab==Tab::enemies) {
        for(int t=0;t<b.targets;++t)value[t]=Friendly(b.target[t].group) ? 0.0f : ColumnSum(b,t,scope).damage;
        return b.targets;
    }
    for(int s=0;s<b.sources;++s) {
        const Source& src=b.source[s];
        if(tab==Tab::weapons)value[s]=InScope(scope,src.side) ? src.damage : 0.0f;
        else if(tab==Tab::taken)value[s]=Friendly(src.side) ? 0.0f : RowSum(b,s,[](Group g){return Friendly(g);}).damage;
        else if(tab==Tab::heals)value[s]=InScope(scope,src.side) || (scope==Scope::everyone && !Friendly(src.side)) ? src.heal : 0.0f;
        else value[s]=0.0f;
    }
    return b.sources;
}

// A damage figure, short: under 10 000 as it is; else in ten-thousands (万 / 萬) and hundred-millions (亿 / 億) for
// the Chinese and Japanese (`lang` hudtext::Lang: 0 en, 1 zh-CN, 2 zh-TW, 3 ja), thousands (k) and millions (M) for English.
inline void Num(wchar_t* out,std::size_t size,float v,int lang) noexcept {
    if(!(v>=0.0f))v=0.0f;
    if(v<10000.0f){swprintf_s(out,size,L"%.0f",v);return;}
    if(lang>=1 && lang<=3) {
        const wchar_t* const wan=lang==2 ? L"萬" : L"万";
        const wchar_t* const yi=lang==1 ? L"亿" : L"億";
        if(v<1e8f)swprintf_s(out,size,v<1e6f ? L"%.1f%ls" : L"%.0f%ls",v/1e4f,wan);
        else swprintf_s(out,size,L"%.2f%ls",v/1e8f,yi);
        return;
    }
    if(v<1e6f)swprintf_s(out,size,v<1e5f ? L"%.1fk" : L"%.0fk",v/1e3f);
    else swprintf_s(out,size,L"%.2fM",v/1e6f);
}
// A time in the mission as m:ss (h:mm:ss past the hour).
inline void Clock(wchar_t* out,std::size_t size,std::uint32_t ms) noexcept {
    const std::uint32_t t=ms/1000u;
    if(t>=3600u)swprintf_s(out,size,L"%u:%02u:%02u",t/3600u,t/60u%60u,t%60u);
    else swprintf_s(out,size,L"%u:%02u",t/60u,t%60u);
}

// --- The page's layout (px on the HUD's screen) ---
struct Box { float x0,y0,x1,y1; };
inline bool In(const Box& r,float x,float y) noexcept { return x>=r.x0 && x<r.x1 && y>=r.y0 && y<r.y1; }

// A click's target on the page: what (the high byte) and which (the rest).
enum class Ui : std::uint8_t { none, tab, scope, row, column, scrollUp, scrollDown, open, close, back };
constexpr int UiCode(Ui what,int which) noexcept { return (static_cast<int>(what)<<16)|(which&0xFFFF); }
constexpr Ui UiWhat(int code) noexcept { return static_cast<Ui>((code>>16)&0xFF); }
constexpr int UiWhich(int code) noexcept { return code&0xFFFF; }

// The page's state, the clicks' (damagestats.cpp ApplyClick: the game thread takes them) and read by the draw.
struct View {
    bool open=false;   // the page shown over the map (the map's STATS tab, or the stats key opens it with the map)
    Tab tab=Tab::weapons;
    Scope scope=Scope::mine;
    int pick=-1;       // the row (bar tabs: a table index; timeline: a column) whose breakdown is open; -1 none
    int scroll=0;      // the first bar row shown
};
// A click (a UiCode) on the page.
inline void Click(View& v,int code,int rows,int visible) noexcept {
    const Ui what=UiWhat(code);
    const int which=UiWhich(code);
    if(what==Ui::tab && which<kTabs){if(static_cast<Tab>(which)!=v.tab){v.tab=static_cast<Tab>(which);v.pick=-1;v.scroll=0;}return;}
    if(what==Ui::scope && which<static_cast<int>(Scope::count)){v.scope=static_cast<Scope>(which);v.pick=-1;v.scroll=0;return;}
    if(what==Ui::row || what==Ui::column){v.pick=v.pick==which ? -1 : which;return;}
    if(what==Ui::back){v.pick=-1;return;}
    if(what==Ui::open || what==Ui::close){v.open=what==Ui::open;return;}
    const int most=rows>visible ? rows-visible : 0;
    if(what==Ui::scrollUp)v.scroll=v.scroll>0 ? v.scroll-1 : 0;
    if(what==Ui::scrollDown)v.scroll=v.scroll<most ? v.scroll+1 : most;
}
// The wheel over the page: `notches` up (+) or down (-).
inline void Scroll(View& v,int notches,int rows,int visible) noexcept {
    const int most=rows>visible ? rows-visible : 0;
    int s=v.scroll-notches;
    v.scroll=s<0 ? 0 : s>most ? most : s;
}

// The page's frame on a `width` x `height` screen at the HUD's scale `s`: the panel, its tab strip, its chart area,
// (with a row picked) the breakdown on the chart's right, and the footer under the chart (the hint, the scroll).
struct Frame { Box panel,tabs,chart,detail,footer; float rowH; int visible; };
inline Frame FrameOf(float width,float height,float s,bool detail) noexcept {
    Frame f{};
    const float margin=40.0f*s,top=56.0f*s,bottom=56.0f*s;
    f.panel=Box{margin,top,width-margin,height-bottom};
    f.tabs=Box{f.panel.x0+16.0f*s,f.panel.y0+12.0f*s,f.panel.x1-16.0f*s,f.panel.y0+48.0f*s};
    const float chartTop=f.tabs.y1+44.0f*s,chartBottom=f.panel.y1-44.0f*s;
    const float split=detail ? f.panel.x0+(f.panel.x1-f.panel.x0)*0.56f : f.panel.x1-16.0f*s;
    f.chart=Box{f.panel.x0+16.0f*s,chartTop,split,chartBottom};
    f.detail=detail ? Box{split+16.0f*s,chartTop,f.panel.x1-16.0f*s,chartBottom} : Box{0,0,0,0};
    f.footer=Box{f.panel.x0+16.0f*s,chartBottom+8.0f*s,f.panel.x1-16.0f*s,f.panel.y1-8.0f*s};
    f.rowH=30.0f*s;
    const int rows=static_cast<int>((f.chart.y1-f.chart.y0)/f.rowH);
    f.visible=rows>1 ? rows : 1;
    return f;
}
// The bar of a visible row `i` (0 the first shown): the label's column on the left, the bar's track right of it.
struct BarRow { Box row,label,track; };
inline BarRow BarRowOf(const Frame& f,int i,float labelW) noexcept {
    BarRow r{};
    const float y0=f.chart.y0+static_cast<float>(i)*f.rowH;
    r.row=Box{f.chart.x0,y0,f.chart.x1,y0+f.rowH};
    r.label=Box{f.chart.x0,y0,f.chart.x0+labelW,y0+f.rowH};
    r.track=Box{f.chart.x0+labelW+8.0f,y0+f.rowH*0.18f,f.chart.x1-110.0f,y0+f.rowH*0.82f};
    return r;
}
// The timeline's column `c` of `used` across the chart (its height scaled to `value / most`).
inline Box ColumnOf(const Frame& f,int c,int used,float value,float most) noexcept {
    const int n=used>0 ? used : 1;
    const float w=(f.chart.x1-f.chart.x0)/static_cast<float>(n);
    const float x0=f.chart.x0+static_cast<float>(c)*w;
    const float share=most>0.0f ? value/most : 0.0f;
    const float base=f.chart.y1-24.0f,top=f.chart.y0+8.0f;
    return Box{x0+w*0.1f,base-(base-top)*share,x0+w*0.9f,base};
}
// The timeline's column under (x, y) (anywhere in its slice of the chart's height), -1 none.
inline int ColumnAt(const Frame& f,int used,float x,float y) noexcept {
    if(used<=0 || !In(f.chart,x,y))return -1;
    const int c=static_cast<int>((x-f.chart.x0)/((f.chart.x1-f.chart.x0)/static_cast<float>(used)));
    return c<0 ? -1 : c>=used ? used-1 : c;
}
}  // namespace dmgstat
