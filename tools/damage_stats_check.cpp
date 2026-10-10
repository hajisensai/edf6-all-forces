// The damage statistics' book and page (src/damage_stats.h) checked offline: hits add up per source, per target and per
// cell; friendly fire and heals (on friends and on enemies) are cells like any other and land in their own totals, never
// in the damage dealt to the enemies; the scopes split mine from the squad's; kills and deaths are counted once; a full
// table puts the rest in its "other" row (nothing lost); a long mission keeps every hit in its timeline (the span doubles,
// the totals stay); the ranking is biggest first; the clicks and the wheel move the page's state within bounds; the
// layout's hit tests find what was drawn.
//   cmake --build build --target damage_stats_check && build\damage_stats_check.exe      (exit code 1 on a failure)
#include "../src/damage_stats.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>

namespace {
using namespace dmgstat;
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
bool Near(float a,float b) { return std::fabs(a-b)<=1e-3f*(1.0f+std::fabs(b)); }

Hit Shot(Side side,const wchar_t* source,Group group,const wchar_t* target,float amount,bool killed,std::uint32_t ms) {
    return Hit{side,source,group,target,amount,false,killed,ms};
}
Hit Heal(Side side,const wchar_t* source,Group group,const wchar_t* target,float amount,std::uint32_t ms) {
    return Hit{side,source,group,target,amount,true,false,ms};
}
float Value(const Book& b,Tab tab,Scope scope,int row) {
    float value[kRows];
    BarValues(b,tab,scope,value);
    return value[row];
}

void Sums() {
    auto b=std::make_unique<Book>();
    Clear(*b);
    Add(*b,Shot(Side::me,L"AF99",Group::enemy,L"ant_black",100.0f,false,1000),L"other");
    Add(*b,Shot(Side::me,L"AF99",Group::enemy,L"ant_black",50.0f,true,2000),L"other");
    Add(*b,Shot(Side::me,L"AF99",Group::enemy,L"ant_red",70.0f,false,3000),L"other");
    Add(*b,Shot(Side::squad,L"AF99",Group::enemy,L"ant_red",30.0f,true,4000),L"other");   // the same weapon in a squad's hands
    Add(*b,Shot(Side::myVehicle,L"Blacker",Group::enemy,L"ant_red",500.0f,false,6000),L"other");
    Add(*b,Shot(Side::me,L"AF99",Group::enemy,L"ant_black",0.0f,false,7000),L"other");   // nothing taken: not a hit
    Add(*b,Shot(Side::enemy,L"ant_red",Group::me,L"me",20.0f,false,8000),L"other");
    Add(*b,Shot(Side::enemy,L"ant_red",Group::ally,L"ranger",40.0f,true,9000),L"other");
    Check(b->sources==4,"four sources (a weapon per side, the enemy kind)",b->sources);
    Check(b->targets==4,"four targets",b->targets);
    const Source& af=b->source[0];
    Check(af.hits==3 && Near(af.damage,220.0f) && Near(af.biggest,100.0f) && af.kills==1,"my AF99",af.hits,af.damage);
    Check(af.firstMs==1000 && af.lastMs==3000,"its first and last hit",af.firstMs,af.lastMs);
    Check(Near(b->cell[0][0].damage,150.0f) && Near(b->cell[0][1].damage,70.0f) && b->cell[0][0].kills==1,"my AF99 per kind");
    const Cell mine=ColumnSum(*b,1,Scope::mine),all=ColumnSum(*b,1,Scope::everyone);
    Check(Near(mine.damage,570.0f) && mine.kills==0 && mine.hits==2,"red ants, mine (on foot and my vehicle)",mine.damage,mine.hits);
    Check(Near(all.damage,600.0f) && all.kills==1 && all.hits==3,"red ants, every ally (not the enemies' own)",all.damage,all.hits);
    Check(Near(b->dealt[static_cast<int>(Side::me)],220.0f) && b->kills[static_cast<int>(Side::squad)]==1,"side totals");
    Check(Near(b->taken[static_cast<int>(Group::me)],20.0f) && Near(b->taken[static_cast<int>(Group::ally)],40.0f),"the enemies' damage to us");
    Check(b->deaths[static_cast<int>(Group::ally)]==1 && b->source[3].kills==1,"an ally they killed");
    Check(Near(Value(*b,Tab::taken,Scope::mine,3),60.0f) && Near(Value(*b,Tab::taken,Scope::mine,0),0.0f),"the attacks tab: enemies only");
    Check(Near(Value(*b,Tab::enemies,Scope::mine,2),0.0f),"the enemies tab: no friendly target");
    float dealt=0.0f,taken=0.0f;
    for(int c=0;c<b->time.used;++c){for(const float v:b->time.dealt[c])dealt+=v;taken+=b->time.taken[c];}
    Check(Near(dealt,750.0f) && Near(taken,60.0f),"the timeline holds every hit",dealt,taken);
}

void FriendlyFire() {
    auto b=std::make_unique<Book>();
    Clear(*b);
    Add(*b,Shot(Side::me,L"Rocket",Group::enemy,L"ant",900.0f,true,1000),L"other");
    Add(*b,Shot(Side::me,L"Rocket",Group::teammate,L"other players",300.0f,false,1000),L"other");
    Add(*b,Shot(Side::me,L"Rocket",Group::me,L"me",100.0f,false,1000),L"other");     // my own blast
    Add(*b,Shot(Side::squad,L"Rocket",Group::ally,L"ranger",80.0f,true,2000),L"other");
    const Source& r=b->source[0];
    Check(Near(r.damage,1300.0f) && r.hits==3,"a weapon's damage to anyone",r.damage);
    Check(Near(RowSum(*b,0,[](Group g){return !Friendly(g);}).damage,900.0f),"...of it to the enemies");
    Check(Near(RowSum(*b,0,[](Group g){return Friendly(g);}).damage,400.0f),"...and to its own side");
    Check(Near(b->dealt[static_cast<int>(Side::me)],900.0f),"friendly fire is no damage dealt",b->dealt[0]);
    Check(Near(b->friendlyFire[static_cast<int>(Side::me)],400.0f) && Near(b->friendlyFire[static_cast<int>(Side::squad)],80.0f),"the friendly fire per side");
    Check(Near(b->taken[static_cast<int>(Group::me)],0.0f),"friendly fire is no damage taken from the enemies");
    Check(b->kills[static_cast<int>(Side::me)]==1 && b->kills[static_cast<int>(Side::squad)]==0,"a teamkill is no kill");
    Check(b->deaths[static_cast<int>(Group::ally)]==1,"...but a death");
    float ff=0.0f;
    for(int c=0;c<b->time.used;++c)for(const float v:b->time.friendlyFire[c])ff+=v;
    Check(Near(ff,480.0f),"the timeline's friendly fire",ff);
}

void Heals() {
    auto b=std::make_unique<Book>();
    Clear(*b);
    Add(*b,Heal(Side::me,L"Life Vendor",Group::me,L"me",300.0f,1000),L"other");
    Add(*b,Heal(Side::me,L"Life Vendor",Group::ally,L"ranger",200.0f,2000),L"other");
    Add(*b,Heal(Side::me,L"Life Vendor",Group::enemy,L"ant",120.0f,2500),L"other");   // an enemy in its reach mended too
    Add(*b,Heal(Side::squad,L"Life Vendor",Group::me,L"me",50.0f,3000),L"other");
    Add(*b,Heal(Side::enemy,L"queen",Group::enemy,L"ant",70.0f,3500),L"other");       // an enemy's heal
    Add(*b,Heal(Side::me,L"Life Vendor",Group::me,L"me",0.0f,4000),L"other");         // nothing given: not a heal
    const Source& lv=b->source[0];
    Check(Near(lv.heal,620.0f) && lv.heals==3 && lv.hits==0 && Near(lv.damage,0.0f),"my vendor's heals, no damage",lv.heal);
    Check(Near(b->cell[0][2].heal,120.0f),"the enemy it mended");
    Check(Near(b->healed[static_cast<int>(Group::me)],350.0f) && Near(b->healed[static_cast<int>(Group::enemy)],0.0f),"HP given to us only",b->healed[0]);
    Check(Near(b->healGiven[static_cast<int>(Side::me)],500.0f) && Near(b->healGiven[static_cast<int>(Side::enemy)],0.0f),"HP each side gave us",b->healGiven[0]);
    float healed=0.0f,dealt=0.0f;
    for(int c=0;c<b->time.used;++c){for(const float h:b->time.healed[c])healed+=h;for(const float d:b->time.dealt[c])dealt+=d;}
    Check(Near(healed,550.0f) && Near(dealt,0.0f),"the timeline's heals (ours) apart from its damage",healed,dealt);
    Check(Near(Value(*b,Tab::heals,Scope::mine,0),620.0f) && Near(Value(*b,Tab::heals,Scope::mine,1),0.0f),"mine: my vendor alone");
    Check(Near(Value(*b,Tab::heals,Scope::everyone,1),50.0f) && Near(Value(*b,Tab::heals,Scope::everyone,2),70.0f),"everyone: the squad's and the enemies' too");
    Check(Near(Value(*b,Tab::weapons,Scope::everyone,0),0.0f),"a heal is no damage on the weapons tab");
}

void Overflow() {
    auto b=std::make_unique<Book>();
    Clear(*b);
    wchar_t name[16];
    float total=0.0f;
    for(int i=0;i<kSources+20;++i) {
        swprintf_s(name,L"gun%d",i);
        Add(*b,Shot(Side::me,name,Group::enemy,name,1.0f+static_cast<float>(i),false,static_cast<std::uint32_t>(i)),L"other");
        total+=1.0f+static_cast<float>(i);
    }
    Check(b->sources==kSources && b->targets==kTargets,"the tables full",b->sources,b->targets);
    Check(SameName(b->source[kSources-1].name,L"other") && SameName(b->target[kTargets-1].name,L"other"),"their last rows: other");
    float sum=0.0f,targets=0.0f,cells=0.0f;
    for(int s=0;s<b->sources;++s){sum+=b->source[s].damage;for(int t=0;t<b->targets;++t)cells+=b->cell[s][t].damage;}
    for(int t=0;t<b->targets;++t)targets+=b->target[t].damage;
    Check(Near(sum,total) && Near(targets,total) && Near(cells,total),"nothing lost to a full table",sum,total);
    Check(SameName(b->source[kSources-2].name,L"gun94"),"the last named row is a real weapon");
    Add(*b,Shot(Side::me,L"gun3",Group::enemy,L"gun3",10.0f,false,100),L"other");
    Check(Near(b->source[3].damage,14.0f),"a known weapon after the table filled",b->source[3].damage);
}

void LongNames() {
    // A name longer than a row keeps (the game's weapon names can be): kept cut, still one row hit after hit.
    auto b=std::make_unique<Book>();
    Clear(*b);
    const wchar_t* gun=L"Blacker A1 120mm Cannon With A Very Long Name That Goes On";
    const wchar_t* ant=L"E501_ANT_RED_WITH_A_RESOURCE_NAME_LONGER_THAN_A_ROW";
    for(int i=0;i<5;++i)Add(*b,Shot(Side::me,gun,Group::enemy,ant,10.0f,false,static_cast<std::uint32_t>(i)),L"other");
    Check(b->sources==1 && b->targets==1,"a long name: one row",b->sources,b->targets);
    Check(b->source[0].hits==5 && b->target[0].hits==5,"...all its hits in it",b->source[0].hits);
    Check(std::wcslen(b->source[0].name.text)==kNameLen-1,"...its name kept cut",static_cast<double>(std::wcslen(b->source[0].name.text)));
    // Two names alike in their kept part are one row (the page could not tell them apart either).
    std::wstring other(gun,kNameLen-1);other+=L" Mk2";
    Add(*b,Shot(Side::me,other.c_str(),Group::enemy,ant,10.0f,false,9),L"other");
    Check(b->sources==1,"names alike in the kept part share a row",b->sources);
}

void LongMission() {
    auto b=std::make_unique<Book>();
    Clear(*b);
    float total=0.0f;
    // Three hours, a hit every 7 s.
    for(std::uint32_t ms=0;ms<3u*3600u*1000u;ms+=7000){Add(*b,Shot(Side::me,L"AF99",Group::enemy,L"ant",2.0f,false,ms),L"other");total+=2.0f;}
    Check(b->time.spanMs>kFirstBucketMs && b->time.used<=kBuckets,"the span doubled to fit",b->time.spanMs,b->time.used);
    Check(static_cast<std::uint64_t>(b->time.spanMs)*kBuckets>=3ull*3600ull*1000ull,"the columns cover the mission",b->time.spanMs);
    float sum=0.0f,row=0.0f;
    for(int c=0;c<kBuckets;++c){sum+=b->time.dealt[c][0];row+=b->sourceColumn[0][c];}
    Check(Near(sum,total) && Near(row,total),"every hit still in the timeline and its source's row",sum,total);
    for(int c=0;c<b->time.used;++c)Check(Near(b->time.dealt[c][0],b->sourceColumn[0][c]),"a column and its source's agree",c);
}

void Ranking() {
    const float value[]={5.0f,0.0f,9.0f,5.0f,1.0f};
    int order[5];
    const int n=Rank(value,5,order);
    Check(n==4,"rows with damage only",n);
    Check(order[0]==2 && order[1]==0 && order[2]==3 && order[3]==4,"biggest first, ties in table order",order[0],order[1]);
}

void Clicks() {
    View v;
    Click(v,UiCode(Ui::row,4),20,8);
    Check(v.pick==4,"a bar opens its breakdown",v.pick);
    Click(v,UiCode(Ui::row,4),20,8);
    Check(v.pick==-1,"again: shut",v.pick);
    Click(v,UiCode(Ui::row,2),20,8);
    Click(v,UiCode(Ui::tab,static_cast<int>(Tab::taken)),20,8);
    Check(v.tab==Tab::taken && v.pick==-1 && v.scroll==0,"a new tab starts clean");
    for(int i=0;i<30;++i)Click(v,UiCode(Ui::scrollDown,0),20,8);
    Check(v.scroll==12,"scrolled to the last page, no further",v.scroll);
    Scroll(v,100,20,8);
    Check(v.scroll==0,"the wheel up to the top",v.scroll);
    Scroll(v,-3,20,8);
    Check(v.scroll==3,"the wheel down",v.scroll);
    Scroll(v,-3,5,8);
    Check(v.scroll==0,"rows that all fit never scroll",v.scroll);
    Click(v,UiCode(Ui::scope,static_cast<int>(Scope::everyone)),20,8);
    Check(v.scope==Scope::everyone,"the scope");
    Click(v,UiCode(Ui::tab,99),20,8);
    Check(v.tab==Tab::taken,"no such tab: ignored");
    Click(v,UiCode(Ui::open,0),20,8);
    Check(v.open && v.tab==Tab::taken && v.scope==Scope::everyone,"opened where it was left");
    Click(v,UiCode(Ui::close,0),20,8);
    Check(!v.open,"closed");
}

void Numbers() {
    struct Case { float v; int lang; const wchar_t* want; };
    const Case table[]={{0.0f,0,L"0"},{9999.4f,1,L"9999"},{12345.0f,0,L"12.3k"},{123456.0f,0,L"123k"},{2500000.0f,0,L"2.50M"},
                        {12345.0f,1,L"1.2万"},{1234567.0f,1,L"123万"},{123456789.0f,1,L"1.23亿"},{12345.0f,2,L"1.2萬"},
                        {123456789.0f,2,L"1.23億"},{123456789.0f,3,L"1.23億"},{-5.0f,0,L"0"}};
    for(const Case& c:table) {
        wchar_t out[32];
        Num(out,32,c.v,c.lang);
        Check(std::wcscmp(out,c.want)==0,"a number's short form",c.v,c.lang);
    }
    wchar_t t[32];
    Clock(t,32,65000);Check(std::wcscmp(t,L"1:05")==0,"m:ss");
    Clock(t,32,3725000);Check(std::wcscmp(t,L"1:02:05")==0,"h:mm:ss");
}

void Layout() {
    const float screens[][2]={{1920.0f,1080.0f},{2560.0f,1080.0f},{1280.0f,720.0f},{1024.0f,768.0f}};
    for(const auto& sc:screens)for(const bool detail:{false,true}) {
        const float s=sc[1]/1080.0f;
        const Frame f=FrameOf(sc[0],sc[1],s,detail);
        Check(f.panel.x0>=0 && f.panel.x1<=sc[0] && f.panel.y0>=0 && f.panel.y1<=sc[1],"the panel on the screen",sc[0]);
        Check(f.chart.x1>f.chart.x0+100.0f && f.chart.y1>f.chart.y0+100.0f,"room for the chart",f.chart.x1-f.chart.x0);
        Check(f.visible>=5,"at least five bars",f.visible);
        Check(!detail || f.detail.x0>=f.chart.x1,"the breakdown right of the chart");
        Check(f.footer.y0>=f.chart.y1 && f.footer.y1<=f.panel.y1 && f.footer.y1-f.footer.y0>=20.0f*s,"the footer under the chart, in the panel");
        // Every visible row inside the chart, the rows apart, a point in a row's middle in that row only.
        for(int i=0;i<f.visible;++i) {
            const BarRow r=BarRowOf(f,i,200.0f*s);
            Check(r.row.y1<=f.chart.y1+0.01f && r.track.x1>r.track.x0,"a row inside the chart",i);
            const float my=(r.row.y0+r.row.y1)*0.5f;
            for(int j=0;j<f.visible;++j)Check((j==i)==In(BarRowOf(f,j,200.0f*s).row,r.row.x0+5.0f,my),"one row under a point",i,j);
        }
        // The timeline: each column's middle finds it, the columns' bars stand in the chart.
        for(const int used:{1,7,120}) {
            for(int c=0;c<used;++c) {
                const Box col=ColumnOf(f,c,used,1.0f,1.0f);
                Check(col.y0>=f.chart.y0 && col.y1<=f.chart.y1,"a column in the chart",c);
                Check(ColumnAt(f,used,(col.x0+col.x1)*0.5f,col.y1-1.0f)==c,"its middle finds it",c,used);
            }
            Check(ColumnAt(f,used,f.chart.x0-1.0f,f.chart.y0+10.0f)==-1,"left of the chart: none");
        }
    }
}
}  // namespace

int main() {
    Sums();
    FriendlyFire();
    Heals();
    Overflow();
    LongNames();
    LongMission();
    Ranking();
    Clicks();
    Numbers();
    Layout();
    std::printf("damage_stats_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
