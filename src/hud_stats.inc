// The damage statistics page (README 浼ゅ缁熻; damagestats.cpp books the hits, src/damage_stats.h lays the page out),
// drawn over the map in place of its marks while it is shown, worked with the map's pointer: the tabs (by weapon, by
// enemy, the enemies' attacks, the heals, the timeline), the scope (mine / everyone), a bar chart (or the timeline's
// columns) whose bar under the pointer has its figures in a box by it, and a click on a bar opening its breakdown on the
// right: whom a weapon hit (friendly fire marked) or healed (an enemy marked), which weapons hit an enemy, whom an enemy hit.
// Included by hud.cpp (its primitives: Rect, Seg, Tri, Label, MapUiBox, MapFitLabel), just before MapScreen.

alignas(16) const float kStatsShade[4]={0.0f,0.0f,0.0f,0.35f};
alignas(16) const float kStatsPanel[4]={0.03f,0.05f,0.08f,0.9f};
alignas(16) const float kStatsEdge[4]={0.3f,0.85f,1.0f,0.5f};
alignas(16) const float kStatsTrack[4]={1.0f,1.0f,1.0f,0.06f};
alignas(16) const float kStatsHover[4]={0.3f,0.85f,1.0f,0.14f};
alignas(16) const float kStatsPick[4]={1.0f,0.7f,0.15f,0.2f};
alignas(16) const float kStatsOn[4]={0.3f,0.85f,1.0f,0.32f};
alignas(16) const float kStatsOff[4]={1.0f,1.0f,1.0f,0.07f};
alignas(16) const float kStatsDim[4]={0.72f,0.78f,0.84f,0.9f};
alignas(16) const float kStatsTaken[4]={1.0f,0.35f,0.3f,0.95f};
alignas(16) const float kStatsFriendly[4]={1.0f,0.35f,0.75f,0.95f};   // friendly fire
alignas(16) const float kStatsHeal[4]={0.45f,1.0f,0.6f,0.95f};
// A side's colour (dmgstat::Side's order): me cyan, my vehicle blue, NPC soldiers green, NPC vehicles amber, the other
// players violet, the rest grey, an enemy red; a group's (dmgstat::Group's) for whom it hit.
alignas(16) const float kStatsSide[dmgstat::kSides][4]={{0.3f,0.85f,1.0f,0.95f},{0.4f,0.55f,1.0f,0.95f},{0.4f,0.9f,0.45f,0.95f},
                                                         {1.0f,0.7f,0.15f,0.95f},{0.78f,0.55f,1.0f,0.95f},{0.62f,0.66f,0.7f,0.95f},
                                                         {1.0f,0.35f,0.3f,0.95f}};
alignas(16) const float kStatsGroup[dmgstat::kGroups][4]={{0.3f,0.85f,1.0f,0.95f},{0.4f,0.55f,1.0f,0.95f},{0.78f,0.55f,1.0f,0.95f},
                                                           {0.4f,0.9f,0.45f,0.95f},{1.0f,0.82f,0.25f,0.95f},{1.0f,0.5f,0.2f,0.95f}};
const Tx kStatsSideName[dmgstat::kSides]={Tx::statsSideMe,Tx::statsSideMyVehicle,Tx::statsSideSquad,Tx::statsSideSupport,
                                          Tx::statsSideTeammate,Tx::statsSideAlly,Tx::statsSideEnemy};
const Tx kStatsGroupName[dmgstat::kGroups]={Tx::statsGroupMe,Tx::statsGroupMyVehicle,Tx::statsGroupTeammate,Tx::statsGroupAlly,
                                            Tx::statsGroupVehicle,Tx::statsGroupEnemy};
const Tx kStatsTabName[dmgstat::kTabs]={Tx::statsTabWeapons,Tx::statsTabEnemies,Tx::statsTabTaken,Tx::statsTabHeals,Tx::statsTabTimeline};

// The pointer (the map's: hidden with a pad).
struct StatsPointer { bool on; float x,y; };
bool Over(const StatsPointer& p,const dmgstat::Box& r) noexcept { return p.on && dmgstat::In(r,p.x,p.y); }

// The click targets drawn this frame, handed to damagestats.cpp at the end of the draw.
constexpr int kStatsTargets=200;
struct StatsTargets { int n=0; float rect[kStatsTargets*4]; int code[kStatsTargets]; };
void Target(StatsTargets& t,const dmgstat::Box& r,dmgstat::Ui what,int which) noexcept {
    if(t.n>=kStatsTargets)return;
    float* q=t.rect+4*t.n;q[0]=r.x0;q[1]=r.y0;q[2]=r.x1;q[3]=r.y1;
    t.code[t.n++]=dmgstat::UiCode(what,which);
}

// The box by the pointer: a title line and up to nine more (a column: its span, each side, friendly fire, heals, taken).
struct StatsTip { bool on; float x,y; int n; wchar_t line[10][100]; };
void TipLine(StatsTip& t,const wchar_t* format,...) noexcept {
    if(t.n>=10)return;
    va_list args;va_start(args,format);
    _vsnwprintf_s(t.line[t.n],_countof(t.line[t.n]),_TRUNCATE,format,args);
    va_end(args);
    ++t.n;
}
void TipAt(StatsTip& t,const StatsPointer& p) noexcept { t.on=true;t.n=0;t.x=p.x;t.y=p.y; }
void StatsTipDraw(void* drawer,void* ctx,Text* text,float width,float height,float s,const StatsTip& t,Line* lines,int* at) noexcept {
    if(!t.on || !t.n || *at+t.n>kMaxLines)return;
    const int first=*at;
    float w=0.0f,h=0.0f;
    for(int i=0;i<t.n;++i) {
        Line& l=lines[(*at)++];
        Format(l,L"%ls",t.line[i]);l.scale=kLineScale*(i ? 0.68f : 0.78f);l.rgba=i ? kWhite : kTitle;l.w=l.h=0.0f;
        if(text)MeasureAll(*text,&l,1);
        if(!(l.w>0.0f)){l.w=static_cast<float>(wcslen(l.text))*9.0f*s;l.h=18.0f*s;}
        w=std::fmax(w,l.w);h+=l.h+3.0f*s;
    }
    const float pad=8.0f*s;
    float x=t.x+20.0f*s,y=t.y+18.0f*s;   // under the pointer and to its right; turned back at the screen's edges
    if(x+w+2.0f*pad>width-4.0f*s)x=t.x-20.0f*s-w-2.0f*pad;
    if(x<4.0f*s)x=4.0f*s;
    if(y+h+2.0f*pad>height-4.0f*s)y=t.y-18.0f*s-h-2.0f*pad;
    if(y<4.0f*s)y=4.0f*s;
    MapUiBox(drawer,ctx,x,y,x+w+2.0f*pad,y+h+2.0f*pad,lines,first);
    Rect(drawer,ctx,x,y,x+w+2.0f*pad,y+1.5f*s,kStatsEdge);
    float ly=y+pad;
    for(int i=first;i<*at;++i){lines[i].x=x+pad;lines[i].y=ly;ly+=lines[i].h+3.0f*s;}
}

// A figure in the HUD's language (dmgstat::Num), a time in the mission (dmgstat::Clock).
struct Fig { wchar_t t[24]; };
Fig Amount(float v) noexcept { Fig f;dmgstat::Num(f.t,_countof(f.t),v,static_cast<int>(hudtext::InUse()));return f; }
Fig When(std::uint32_t ms) noexcept { Fig f;dmgstat::Clock(f.t,_countof(f.t),ms);return f; }
float Share(float part,float whole) noexcept { return whole>0.0f ? 100.0f*part/whole : 0.0f; }
bool ToFriend(dmgstat::Group g) noexcept { return dmgstat::Friendly(g); }
bool ToEnemy(dmgstat::Group g) noexcept { return !dmgstat::Friendly(g); }

// A bar tab's row (a source, on the enemies tab a target): its name and colour.
const wchar_t* RowName(const dmgstat::Book& b,dmgstat::Tab tab,int i) noexcept {
    return tab==dmgstat::Tab::enemies ? b.target[i].name.text : b.source[i].name.text;
}
const float* RowColour(const dmgstat::Book& b,dmgstat::Tab tab,int i) noexcept {
    if(tab==dmgstat::Tab::enemies)return kStatsGroup[static_cast<int>(dmgstat::Group::enemy)];
    return kStatsSide[static_cast<int>(b.source[i].side)];
}

// The figures of a bar tab's row `i` (the tooltip's lines and the breakdown's head); `value` its bar, `total` the tab's.
void RowFigures(StatsTip& t,const dmgstat::Book& b,const dmgstat::View& v,int i,float value,float total) noexcept {
    using dmgstat::Tab;
    if(v.tab==Tab::enemies) {
        const dmgstat::Cell k=dmgstat::ColumnSum(b,i,v.scope);
        TipLine(t,L"%ls",b.target[i].name.text);
        TipLine(t,Tr(Tx::statsTipDamage),Amount(k.damage).t,Share(value,total));
        TipLine(t,Tr(Tx::statsTipKind),k.hits,k.kills);
        if(k.heal>0.0f)TipLine(t,Tr(Tx::statsTipColumnHealed),Amount(k.heal).t);
        return;
    }
    const dmgstat::Source& src=b.source[i];
    TipLine(t,L"%ls   (%ls)",src.name.text,Tr(kStatsSideName[static_cast<int>(src.side)]));
    if(v.tab==Tab::heals) {
        TipLine(t,Tr(Tx::statsTipHeal),Amount(src.heal).t,Share(value,total),src.heals);
        TipLine(t,Tr(Tx::statsTipHealSplit),Amount(dmgstat::RowSum(b,i,ToFriend).heal).t,Amount(dmgstat::RowSum(b,i,ToEnemy).heal).t);
        TipLine(t,Tr(Tx::statsTipSpan),When(src.firstMs).t,When(src.lastMs).t);
        return;
    }
    if(v.tab==Tab::taken) {
        const dmgstat::Cell us=dmgstat::RowSum(b,i,ToFriend);
        TipLine(t,Tr(Tx::statsTipDamage),Amount(us.damage).t,Share(value,total));
        TipLine(t,Tr(Tx::statsTipTaken),us.hits,Amount(src.biggest).t,us.kills);
        return;
    }
    const dmgstat::Cell foe=dmgstat::RowSum(b,i,ToEnemy),own=dmgstat::RowSum(b,i,ToFriend);
    TipLine(t,Tr(Tx::statsTipDamage),Amount(src.damage).t,Share(value,total));
    if(own.damage>0.0f)TipLine(t,Tr(Tx::statsTipSplit),Amount(foe.damage).t,Amount(own.damage).t);
    TipLine(t,Tr(Tx::statsTipHits),src.hits,Amount(src.biggest).t,Amount(src.hits ? src.damage/static_cast<float>(src.hits) : 0.0f).t);
    TipLine(t,Tr(Tx::statsTipKills),foe.kills);
    if(src.heal>0.0f)TipLine(t,Tr(Tx::statsTipColumnHealed),Amount(src.heal).t);
    const float sec=static_cast<float>(src.lastMs-src.firstMs)*0.001f;
    TipLine(t,Tr(Tx::statsTipActive),When(src.firstMs).t,When(src.lastMs).t,Amount(sec>=1.0f ? src.damage/sec : src.damage).t);
}

// A bar across `r`, `share` of it filled: in `fill`, or split in `parts` by `split` (their colours `colours`).
void StatsBar(void* drawer,void* ctx,const dmgstat::Box& r,float share,const float* fill,const float* split=nullptr,
              const float* const* colours=nullptr,int parts=0) noexcept {
    Rect(drawer,ctx,r.x0,r.y0,r.x1,r.y1,kStatsTrack);
    const float w=(r.x1-r.x0)*Unit(share);
    float whole=0.0f;
    for(int i=0;i<parts;++i)whole+=split[i];
    if(!(whole>0.0f) && fill)Rect(drawer,ctx,r.x0,r.y0,r.x0+w,r.y1,fill);
    float x=r.x0;
    for(int i=0;i<parts && whole>0.0f;++i) {
        const float seg=w*split[i]/whole;
        Rect(drawer,ctx,x,r.y0,x+seg,r.y1,colours[i]);
        x+=seg;
    }
    Rect(drawer,ctx,r.x0,r.y0,r.x0+w,r.y0+(r.y1-r.y0)*0.35f,kBarShine);
}
// A bar tab's row's bar: a weapon's damage split enemies / its own side, an enemy's by whom it hit, else one colour.
void RowBar(void* drawer,void* ctx,const dmgstat::Box& track,float share,const dmgstat::Book& b,dmgstat::Tab tab,int i) noexcept {
    using dmgstat::Tab;
    if(tab==Tab::weapons) {
        const float split[2]={dmgstat::RowSum(b,i,ToEnemy).damage,dmgstat::RowSum(b,i,ToFriend).damage};
        const float* colours[2]={RowColour(b,tab,i),kStatsFriendly};
        StatsBar(drawer,ctx,track,share,nullptr,split,colours,2);
        return;
    }
    if(tab==Tab::taken) {
        float split[dmgstat::kGroups-1];
        const float* colours[dmgstat::kGroups-1];
        for(int g=0;g<dmgstat::kGroups-1;++g) {
            const auto group=static_cast<dmgstat::Group>(g);
            split[g]=dmgstat::RowSum(b,i,[group](dmgstat::Group x){return x==group;}).damage;
            colours[g]=kStatsGroup[g];
        }
        StatsBar(drawer,ctx,track,share,nullptr,split,colours,dmgstat::kGroups-1);
        return;
    }
    StatsBar(drawer,ctx,track,share,tab==Tab::heals ? kStatsHeal : RowColour(b,tab,i));
}

// One row of a breakdown list at `y` (`h` high) across [x0, x1): a colour chip, the name, a bar, the figure.
void ListRow(void* drawer,void* ctx,Text* text,float x0,float x1,float y,float h,float s,const float* colour,const wchar_t* name,
             float value,float most,const wchar_t* tail,Line* lines,int* at) noexcept {
    const float nameW=(x1-x0)*0.42f,figW=(x1-x0)*0.24f,mid=y+h*0.5f;
    Rect(drawer,ctx,x0,mid-4.0f*s,x0+8.0f*s,mid+4.0f*s,colour);
    Label(text,lines,at,x0+14.0f*s,mid,0,kLineScale*0.62f,kWhite,L"%ls",name);
    if(*at>0)MapFitLabel(text,lines[*at-1],x0+14.0f*s,x0+nameW-6.0f*s);
    StatsBar(drawer,ctx,dmgstat::Box{x0+nameW,y+h*0.25f,x1-figW,y+h*0.75f},most>0.0f ? value/most : 0.0f,colour);
    Label(text,lines,at,x1,mid,2,kLineScale*0.6f,kStatsDim,L"%ls%ls",Amount(value).t,tail ? tail : L"");
}

// The breakdown's frame: its back, the way back (the same row clicked again does it too); the y its first line goes at.
float DetailFrame(void* drawer,void* ctx,Text* text,const dmgstat::Box& d,float s,StatsTargets& tg,Line* lines,int* at) noexcept {
    Rect(drawer,ctx,d.x0-8.0f*s,d.y0-6.0f*s,d.x1+4.0f*s,d.y1,kStatsTrack);
    const dmgstat::Box backBox{d.x1-76.0f*s,d.y0,d.x1,d.y0+26.0f*s};
    Rect(drawer,ctx,backBox.x0,backBox.y0,backBox.x1,backBox.y1,kStatsOff);
    Label(text,lines,at,(backBox.x0+backBox.x1)*0.5f,(backBox.y0+backBox.y1)*0.5f,1,kLineScale*0.6f,kWhite,L"%ls",Tr(Tx::statsBack));
    Target(tg,backBox,dmgstat::Ui::back,0);
    return d.y0+14.0f*s;
}
// The breakdown's head: the figures, a line each; the y under them.
float DetailHead(Text* text,const dmgstat::Box& d,float y,float s,const StatsTip& head,Line* lines,int* at) noexcept {
    for(int k=0;k<head.n;++k) {
        Label(text,lines,at,d.x0,y,0,kLineScale*(k ? 0.66f : 0.82f),k ? kWhite : kTitle,L"%ls",head.line[k]);
        if(*at>0)MapFitLabel(text,lines[*at-1],d.x0,d.x1-(k ? 0.0f : 84.0f*s));
        y+=(k ? 22.0f : 28.0f)*s;
    }
    return y+8.0f*s;
}

// A breakdown row's name, colour and kills tail: a weapon on the enemies tab, else a target (a weapon's friendly fire and
// a heal on an enemy said so on the row).
const float* DetailRow(const dmgstat::Book& b,const dmgstat::View& v,int k,wchar_t* name,std::size_t nameSize,wchar_t* tail,
                       std::size_t tailSize) noexcept {
    using dmgstat::Tab;
    const int i=v.pick;
    const std::uint32_t kills=v.tab==Tab::enemies ? b.cell[k][i].kills : v.tab==Tab::heals ? 0u : b.cell[i][k].kills;
    tail[0]=0;
    if(kills){tail[0]=tail[1]=L' ';swprintf_s(tail+2,tailSize-2,Tr(Tx::statsTipKills),kills);}
    if(v.tab==Tab::enemies) {
        swprintf_s(name,nameSize,L"%ls",b.source[k].name.text);
        return kStatsSide[static_cast<int>(b.source[k].side)];
    }
    const dmgstat::Target& tgt=b.target[k];
    const bool mark=v.tab==Tab::heals ? !dmgstat::Friendly(tgt.group) : v.tab==Tab::weapons && dmgstat::Friendly(tgt.group);
    if(mark)swprintf_s(name,nameSize,Tr(v.tab==Tab::heals ? Tx::statsTagEnemy : Tx::statsTagFriendly),tgt.name.text);
    else swprintf_s(name,nameSize,L"%ls",tgt.name.text);
    return kStatsGroup[static_cast<int>(tgt.group)];
}

// The breakdown of the bar tab's picked row in `d`: its figures, then the list that splits it.
void BarDetail(void* drawer,void* ctx,Text* text,const dmgstat::Box& d,float s,const dmgstat::Book& b,const dmgstat::View& v,
               float value,float total,StatsTargets& tg,Line* lines,int* at) noexcept {
    using dmgstat::Tab;
    const int i=v.pick;
    float y=DetailFrame(drawer,ctx,text,d,s,tg,lines,at);
    StatsTip head{};
    RowFigures(head,b,v,i,value,total);
    y=DetailHead(text,d,y,s,head,lines,at);
    const float rowH=24.0f*s,shape=v.tab==Tab::weapons ? 96.0f*s : 0.0f;
    const int room=static_cast<int>((d.y1-y-18.0f*s-shape)/rowH);
    // The list: whom a weapon hit, which weapons hit an enemy, whom an enemy hit, whom a heal mended.
    float val[dmgstat::kRows];
    int order[dmgstat::kRows];
    int n=b.targets;
    if(v.tab==Tab::enemies) {
        for(int src=0;src<b.sources;++src)val[src]=dmgstat::InScope(v.scope,b.source[src].side) ? b.cell[src][i].damage : 0.0f;
        n=b.sources;
    } else for(int t=0;t<b.targets;++t) {
        const dmgstat::Cell& c=b.cell[i][t];
        val[t]=v.tab==Tab::heals ? c.heal : v.tab==Tab::taken && !dmgstat::Friendly(b.target[t].group) ? 0.0f : c.damage;
    }
    const int rows=dmgstat::Rank(val,n,order);
    const Tx heading=v.tab==Tab::enemies ? Tx::statsDetailSources : v.tab==Tab::heals ? Tx::statsDetailHealed : Tx::statsDetailTargets;
    Label(text,lines,at,d.x0,y,0,kLineScale*0.66f,kStatsDim,L"%ls",Tr(heading));
    y+=18.0f*s;
    wchar_t name[96],tail[32];
    for(int r=0;r<rows && r<room;++r) {
        const int k=order[r];
        const float* colour=DetailRow(b,v,k,name,_countof(name),tail,_countof(tail));
        ListRow(drawer,ctx,text,d.x0,d.x1,y,rowH,s,colour,name,val[k],val[order[0]],tail,lines,at);
        y+=rowH;
    }
    if(v.tab!=Tab::weapons)return;
    // A weapon's shape over the mission: its damage per timeline column, small.
    const float base=d.y1-14.0f*s,top=base-60.0f*s;
    Label(text,lines,at,d.x0,top-12.0f*s,0,kLineScale*0.6f,kStatsDim,L"%ls",Tr(Tx::statsDetailShape));
    const int used=b.time.used>0 ? b.time.used : 1;
    float most=0.0f;
    for(int c=0;c<used;++c)most=std::fmax(most,b.sourceColumn[i][c]);
    const float w=(d.x1-d.x0)/static_cast<float>(used);
    for(int c=0;c<used && most>0.0f;++c) {
        const float h=(base-top)*b.sourceColumn[i][c]/most;
        Rect(drawer,ctx,d.x0+w*static_cast<float>(c),base-h,d.x0+w*(static_cast<float>(c)+0.85f),base,RowColour(b,v.tab,i));
    }
    Rect(drawer,ctx,d.x0,base,d.x1,base+1.0f*s,kStatsDim);
}

void StatsEmpty(Text* text,const dmgstat::Frame& f,Line* lines,int* at) noexcept {
    Label(text,lines,at,(f.chart.x0+f.chart.x1)*0.5f,(f.chart.y0+f.chart.y1)*0.5f,1,kLineScale*0.8f,kStatsDim,L"%ls",Tr(Tx::statsEmpty));
}

// A bar tab: the rows biggest first from the scroll, the picked one's breakdown on the right. Returns the rows.
int StatsBars(void* drawer,void* ctx,Text* text,const dmgstat::Frame& f,float s,const dmgstat::Book& b,const dmgstat::View& v,
              const StatsPointer& p,StatsTargets& tg,StatsTip& tip,Line* lines,int* at) noexcept {
    float value[dmgstat::kRows];
    int order[dmgstat::kRows];
    const int n=dmgstat::BarValues(b,v.tab,v.scope,value);
    const int rows=dmgstat::Rank(value,n,order);
    if(!rows){StatsEmpty(text,f,lines,at);return 0;}
    float total=0.0f;
    for(int r=0;r<rows;++r)total+=value[order[r]];
    const float most=value[order[0]];
    const float labelW=std::fmin(std::fmax((f.chart.x1-f.chart.x0)*0.34f,150.0f*s),330.0f*s);
    const int last=rows>f.visible ? rows-f.visible : 0,first=v.scroll<last ? v.scroll : last;
    for(int r=0;r<f.visible && first+r<rows;++r) {
        const int i=order[first+r];
        const dmgstat::BarRow row=dmgstat::BarRowOf(f,r,labelW);
        const bool over=Over(p,row.row);
        if(v.pick==i)Rect(drawer,ctx,row.row.x0,row.row.y0,row.row.x1,row.row.y1,kStatsPick);
        else if(over)Rect(drawer,ctx,row.row.x0,row.row.y0,row.row.x1,row.row.y1,kStatsHover);
        const float mid=(row.row.y0+row.row.y1)*0.5f;
        Rect(drawer,ctx,row.label.x0+2.0f*s,mid-5.0f*s,row.label.x0+12.0f*s,mid+5.0f*s,RowColour(b,v.tab,i));
        Label(text,lines,at,row.label.x0+18.0f*s,mid,0,kLineScale*0.7f,kWhite,L"%ls",RowName(b,v.tab,i));
        if(*at>0)MapFitLabel(text,lines[*at-1],row.label.x0+18.0f*s,row.label.x1-4.0f*s);
        RowBar(drawer,ctx,row.track,value[i]/most,b,v.tab,i);
        Label(text,lines,at,row.track.x1+8.0f*s,mid,0,kLineScale*0.64f,kWhite,L"%ls  %.0f%%",Amount(value[i]).t,Share(value[i],total));
        Target(tg,row.row,dmgstat::Ui::row,i);
        if(over){TipAt(tip,p);RowFigures(tip,b,v,i,value[i],total);}
    }
    if(rows>f.visible) {   // the scroll, in the footer under the bars: which rows show, a step up and down
        const float x1=f.chart.x1,y=(f.footer.y0+f.footer.y1)*0.5f;
        Label(text,lines,at,x1-60.0f*s,y,2,kLineScale*0.6f,kStatsDim,Tr(Tx::statsRows),first+1,first+f.visible,rows);
        const dmgstat::Box up{x1-52.0f*s,y-11.0f*s,x1-28.0f*s,y+11.0f*s},down{x1-24.0f*s,y-11.0f*s,x1,y+11.0f*s};
        Rect(drawer,ctx,up.x0,up.y0,up.x1,up.y1,Over(p,up) ? kStatsOn : kStatsOff);
        Rect(drawer,ctx,down.x0,down.y0,down.x1,down.y1,Over(p,down) ? kStatsOn : kStatsOff);
        Tri(drawer,ctx,(up.x0+up.x1)*0.5f,y+5.0f*s,(up.x0+up.x1)*0.5f,y-5.0f*s,6.0f*s,kWhite);
        Tri(drawer,ctx,(down.x0+down.x1)*0.5f,y-5.0f*s,(down.x0+down.x1)*0.5f,y+5.0f*s,6.0f*s,kWhite);
        Target(tg,up,dmgstat::Ui::scrollUp,0);Target(tg,down,dmgstat::Ui::scrollDown,0);
    }
    if(v.pick>=0 && v.pick<n && value[v.pick]>0.0f)BarDetail(drawer,ctx,text,f.detail,s,b,v,value[v.pick],total,tg,lines,at);
    return rows;
}

// A timeline column's amount of `per` (a column's [kSides] row) from the sides in scope.
float ScopeSum(const float* per,dmgstat::Scope scope) noexcept {
    float d=0.0f;
    for(int side=0;side<dmgstat::kSides;++side)if(dmgstat::InScope(scope,static_cast<dmgstat::Side>(side)))d+=per[side];
    return d;
}
// The figures of the timeline's column `c`.
void ColumnFigures(StatsTip& t,const dmgstat::Book& b,const dmgstat::View& v,int c) noexcept {
    const std::uint32_t from=static_cast<std::uint32_t>(c)*b.time.spanMs;
    TipLine(t,Tr(Tx::statsTipSpan),When(from).t,When(from+b.time.spanMs).t);
    for(int side=0;side<dmgstat::kSides;++side)
        if(dmgstat::InScope(v.scope,static_cast<dmgstat::Side>(side)) && b.time.dealt[c][side]>0.0f)
            TipLine(t,Tr(Tx::statsTipSide),Tr(kStatsSideName[side]),Amount(b.time.dealt[c][side]).t);
    const float ff=ScopeSum(b.time.friendlyFire[c],v.scope),healed=ScopeSum(b.time.healed[c],v.scope);
    if(ff>0.0f)TipLine(t,Tr(Tx::statsTipColumnFriendly),Amount(ff).t);
    if(healed>0.0f)TipLine(t,Tr(Tx::statsTipColumnHealed),Amount(healed).t);
    TipLine(t,Tr(Tx::statsTipColumnTaken),Amount(b.time.taken[c]).t);
}
// The timeline: a column a span, the sides' damage to the enemies stacked in their colours and their friendly fire on top,
// the damage taken a red line and the heals a green one over them; the picked column's weapons on the right.
void StatsTimeline(void* drawer,void* ctx,Text* text,const dmgstat::Frame& f,float s,const dmgstat::Book& b,const dmgstat::View& v,
                   const StatsPointer& p,StatsTargets& tg,StatsTip& tip,Line* lines,int* at) noexcept {
    const int used=b.time.used;
    float most=0.0f;
    for(int c=0;c<used;++c) {
        const float stack=ScopeSum(b.time.dealt[c],v.scope)+ScopeSum(b.time.friendlyFire[c],v.scope);
        most=std::fmax(most,std::fmax(stack,std::fmax(b.time.taken[c],ScopeSum(b.time.healed[c],v.scope))));
    }
    if(!(most>0.0f)){StatsEmpty(text,f,lines,at);return;}
    const int over=p.on ? dmgstat::ColumnAt(f,used,p.x,p.y) : -1;
    const dmgstat::Box full=dmgstat::ColumnOf(f,0,used,most,most);   // a column at the scale's top: the full height
    const float fullH=full.y1-full.y0,slice=(f.chart.x1-f.chart.x0)/static_cast<float>(used);
    float taken[2]{},healed[2]{};   // the lines' last points (x, y)
    for(int c=0;c<used;++c) {
        const dmgstat::Box col=dmgstat::ColumnOf(f,c,used,0.0f,most);
        const dmgstat::Box whole{f.chart.x0+slice*static_cast<float>(c),f.chart.y0,f.chart.x0+slice*static_cast<float>(c+1),col.y1};
        if(v.pick==c)Rect(drawer,ctx,whole.x0,whole.y0,whole.x1,whole.y1,kStatsPick);
        else if(over==c)Rect(drawer,ctx,whole.x0,whole.y0,whole.x1,whole.y1,kStatsHover);
        float y=col.y1;
        for(int side=0;side<dmgstat::kSides;++side) {
            if(!dmgstat::InScope(v.scope,static_cast<dmgstat::Side>(side)))continue;
            const float h=fullH*b.time.dealt[c][side]/most;
            Rect(drawer,ctx,col.x0,y-h,col.x1,y,kStatsSide[side]);
            y-=h;
        }
        const float ff=fullH*ScopeSum(b.time.friendlyFire[c],v.scope)/most;
        Rect(drawer,ctx,col.x0,y-ff,col.x1,y,kStatsFriendly);
        const float x=(col.x0+col.x1)*0.5f;
        const float ty=col.y1-fullH*b.time.taken[c]/most,hy=col.y1-fullH*ScopeSum(b.time.healed[c],v.scope)/most;
        if(c){Seg(drawer,ctx,taken[0],taken[1],x,ty,2.0f*s,kStatsTaken);Seg(drawer,ctx,healed[0],healed[1],x,hy,2.0f*s,kStatsHeal);}
        taken[0]=healed[0]=x;taken[1]=ty;healed[1]=hy;
        Target(tg,whole,dmgstat::Ui::column,c);
    }
    // The axes: the scale's top, a time under every few columns.
    const float base=full.y1;
    Rect(drawer,ctx,f.chart.x0,base,f.chart.x1,base+1.0f*s,kStatsDim);
    Label(text,lines,at,f.chart.x0,f.chart.y0-6.0f*s,0,kLineScale*0.58f,kStatsDim,L"%ls",Amount(most).t);
    const int step=(used+5)/6;
    for(int c=0;c<=used;c+=step) {
        const float x=f.chart.x0+slice*static_cast<float>(c);
        Label(text,lines,at,x,base+12.0f*s,c ? (c==used ? 2 : 1) : 0,kLineScale*0.58f,kStatsDim,L"%ls",
              When(static_cast<std::uint32_t>(c)*b.time.spanMs).t);
    }
    if(over>=0){TipAt(tip,p);ColumnFigures(tip,b,v,over);}
    if(v.pick<0 || v.pick>=used)return;
    // The picked column: its figures, its weapons.
    const dmgstat::Box& d=f.detail;
    float y=DetailFrame(drawer,ctx,text,d,s,tg,lines,at);
    StatsTip head{};
    ColumnFigures(head,b,v,v.pick);
    y=DetailHead(text,d,y,s,head,lines,at);
    Label(text,lines,at,d.x0,y,0,kLineScale*0.66f,kStatsDim,L"%ls",Tr(Tx::statsDetailTop));
    y+=18.0f*s;
    float val[dmgstat::kSources];
    int order[dmgstat::kSources];
    for(int src=0;src<b.sources;++src)val[src]=dmgstat::InScope(v.scope,b.source[src].side) ? b.sourceColumn[src][v.pick] : 0.0f;
    const int rows=dmgstat::Rank(val,b.sources,order);
    const float rowH=24.0f*s;
    for(int r=0;r<rows && y+rowH<=d.y1;++r) {
        const int k=order[r];
        ListRow(drawer,ctx,text,d.x0,d.x1,y,rowH,s,kStatsSide[static_cast<int>(b.source[k].side)],b.source[k].name.text,val[k],
                val[order[0]],nullptr,lines,at);
        y+=rowH;
    }
}

// The page's head: the title, the tabs, the scope, the close button; the mission's totals and the hint under them.
void StatsHead(void* drawer,void* ctx,Text* text,const dmgstat::Frame& f,float s,const dmgstat::Book& b,const dmgstat::View& v,
               std::uint32_t missionMs,const StatsPointer& p,StatsTargets& tg,Line* lines,int* at) noexcept {
    const dmgstat::Box& t=f.tabs;
    const float mid=(t.y0+t.y1)*0.5f;
    Label(text,lines,at,t.x0,mid,0,kTitleScale,kTitle,L"%ls",Tr(Tx::statsTitle));
    const float titleW=*at>0 && lines[*at-1].w>0.0f ? lines[*at-1].w+24.0f*s : 190.0f*s;
    const float closeW=90.0f*s,chipW=104.0f*s,gap=6.0f*s;
    const bool scoped=v.tab!=dmgstat::Tab::taken;
    const float tabsRoom=t.x1-t.x0-titleW-closeW-(scoped ? 2.0f*chipW+gap : 0.0f)-6.0f*gap;
    const float tabW=std::fmin(140.0f*s,tabsRoom/static_cast<float>(dmgstat::kTabs)-gap);
    const auto button=[&](const dmgstat::Box& r,bool on,const wchar_t* word,dmgstat::Ui what,int which) {
        Rect(drawer,ctx,r.x0,r.y0,r.x1,r.y1,on ? kStatsOn : Over(p,r) ? kStatsHover : kStatsOff);
        if(on)Rect(drawer,ctx,r.x0,r.y1-2.0f*s,r.x1,r.y1,kCyan);
        Label(text,lines,at,(r.x0+r.x1)*0.5f,(r.y0+r.y1)*0.5f,1,kLineScale*0.66f,on ? kWhite : kStatsDim,L"%ls",word);
        if(*at>0)MapFitLabel(text,lines[*at-1],r.x0+4.0f*s,r.x1-4.0f*s);
        Target(tg,r,what,which);
    };
    float x=t.x0+titleW;
    for(int i=0;i<dmgstat::kTabs;++i,x+=tabW+gap)
        button(dmgstat::Box{x,t.y0,x+tabW,t.y1},static_cast<int>(v.tab)==i,Tr(kStatsTabName[i]),dmgstat::Ui::tab,i);
    const dmgstat::Box close{t.x1-closeW,t.y0,t.x1,t.y1};
    button(close,false,Tr(Tx::statsClose),dmgstat::Ui::close,0);
    if(scoped) {
        const float cx=close.x0-2.0f*gap-2.0f*chipW;
        button(dmgstat::Box{cx,t.y0,cx+chipW,t.y1},v.scope==dmgstat::Scope::mine,Tr(Tx::statsScopeMine),dmgstat::Ui::scope,0);
        button(dmgstat::Box{cx+chipW+gap,t.y0,cx+2.0f*chipW+gap,t.y1},v.scope==dmgstat::Scope::everyone,Tr(Tx::statsScopeAll),dmgstat::Ui::scope,1);
    }
    // The totals: the scope's damage to the enemies, kills, heals given and friendly fire; the damage the player's side took.
    float dealt=0.0f,healed=0.0f,ff=0.0f,taken=0.0f;
    std::uint32_t kills=0;
    for(int side=0;side<dmgstat::kSides;++side) {
        if(!dmgstat::InScope(v.scope,static_cast<dmgstat::Side>(side)))continue;
        dealt+=b.dealt[side];kills+=b.kills[side];healed+=b.healGiven[side];ff+=b.friendlyFire[side];
    }
    for(const float d:b.taken)taken+=d;
    Label(text,lines,at,t.x0,t.y1+20.0f*s,0,kLineScale*0.7f,kWhite,Tr(Tx::statsSummary),Amount(dealt).t,kills,Amount(taken).t,
          Amount(healed).t,Amount(ff).t,When(missionMs).t);
    if(*at>0)MapFitLabel(text,lines[*at-1],t.x0,t.x1);
    // The hint in the footer's left (the scroll, when there is one, at its right).
    Label(text,lines,at,f.footer.x0,(f.footer.y0+f.footer.y1)*0.5f,0,kLineScale*0.58f,kStatsDim,L"%ls",Tr(Tx::statsHint));
    if(*at>0)MapFitLabel(text,lines[*at-1],f.footer.x0,f.footer.x0+(f.footer.x1-f.footer.x0)*0.6f);
}

// The page over the map (MapScreen while it is shown): the screen shaded, the panel, its head and chart, the tooltip; its
// targets handed to damagestats.cpp for the clicks.
void StatsPage(void* drawer,void* ctx,Text* text,float width,float height,float s,const dmgstat::Book& b,const dmgstat::View& v,
               std::uint32_t missionMs,const StatsPointer& p,Line* lines,int* at) noexcept {
    static StatsTargets tg;   // the draw thread's (too big for its stack)
    tg.n=0;
    MapUiBox(drawer,ctx,0.0f,0.0f,width,height,lines,*at);   // the whole screen: no click falls through to the map
    Rect(drawer,ctx,0.0f,0.0f,width,height,kStatsShade);
    const dmgstat::Frame f=dmgstat::FrameOf(width,height,s,v.pick>=0);
    Rect(drawer,ctx,f.panel.x0,f.panel.y0,f.panel.x1,f.panel.y1,kStatsPanel);
    Rect(drawer,ctx,f.panel.x0,f.panel.y0,f.panel.x1,f.panel.y0+2.0f*s,kStatsEdge);
    StatsTip tip{};
    StatsHead(drawer,ctx,text,f,s,b,v,missionMs,p,tg,lines,at);
    int rows=0;
    if(v.tab==dmgstat::Tab::timeline)StatsTimeline(drawer,ctx,text,f,s,b,v,p,tg,tip,lines,at);
    else rows=StatsBars(drawer,ctx,text,f,s,b,v,p,tg,tip,lines,at);
    StatsTipDraw(drawer,ctx,text,width,height,s,tip,lines,at);
    DamageStatsUi(tg.rect,tg.code,tg.n,rows,f.visible);
    if(p.on)Tri(drawer,ctx,p.x+9.0f*s,p.y+13.0f*s,p.x,p.y,5.0f*s,kWhite);   // the pointer, over it all
}

// The map's STATS tab (the page shut): left of the legend's tab, the stats key in its tooltip.
void StatsMapTab(void* drawer,void* ctx,Text* text,float width,float height,float s,Line* lines,int* at) noexcept {
    MapCommandReadout c{};
    const bool pointer=PlayerMapCommands(&c) && c.pointer;
    const float x1=width-12.0f*s-110.0f*s-8.0f*s,x0=x1-100.0f*s,y1=height-110.0f*s,y0=y1-24.0f*s;
    const bool over=pointer && c.px>=x0 && c.px<x1 && c.py>=y0 && c.py<y1;
    MapUiBox(drawer,ctx,x0,y0,x1,y1,lines,*at);
    if(over)Rect(drawer,ctx,x0,y0,x1,y1,kStatsHover);
    Rect(drawer,ctx,x0+8.0f*s,y1-8.0f*s,x0+12.0f*s,y1-4.0f*s,kStatsSide[0]);   // a small bar chart for its icon
    Rect(drawer,ctx,x0+14.0f*s,y1-14.0f*s,x0+18.0f*s,y1-4.0f*s,kStatsSide[2]);
    Rect(drawer,ctx,x0+20.0f*s,y1-19.0f*s,x0+24.0f*s,y1-4.0f*s,kAmber);
    Label(text,lines,at,(x0+x1)*0.5f+10.0f*s,(y0+y1)*0.5f,1,kLineScale*0.7f,over ? kWhite : kMapOrder,L"%ls",Tr(Tx::statsMapTab));
    wchar_t key[32];
    KeyName(Cfg().damageStatsKey,key,32);
    MapTipSet(c,x0,y0,x1,y1,Tr(Tx::statsMapTabTip),key);
    const float rect[4]={x0,y0,x1,y1};
    const int code=dmgstat::UiCode(dmgstat::Ui::open,0);
    DamageStatsUi(rect,&code,1,0,0);
}
