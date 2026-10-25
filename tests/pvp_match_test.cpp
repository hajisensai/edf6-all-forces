// The PvP match as data (src/pvp_match.h) offline: the faction table and its teams, which role a faction may take, the
// relation presets and the host's own tables, and the native overlay a match writes (src/team_native_logic.h).
// cmake --build build --target pvp_match_test && build\pvp_match_test.exe
#include "../src/pvp_match.h"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {
using namespace crew::pvp_match;
namespace tn=crew::team_native;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
constexpr int kEdf=0,kEdf2=1,kEdf3=2,kEdf4=3,kMonster=4,kPrimer=5,kForeigner=6;
const Preset kPresets[]={Preset::freeForAll,Preset::humansVsAliens,Preset::edfCivilWar,Preset::aliensWar};

void Factions() {
    static_assert(kFactionCount==7,"seven factions");
    static_assert(kFactionCount<=kMaxFactions,"they fit the wire");
    static_assert(kRoleCount==3,"soldier, commander, unit");
    Expect(kFactions[kEdf].team==2 && kFactions[kEdf2].team==7 && kFactions[kEdf3].team==8 && kFactions[kEdf4].team==9,"the EDF factions' teams");
    Expect(kFactions[kMonster].team==1 && kFactions[kPrimer].team==10 && kFactions[kForeigner].team==11,"the alien factions' teams");
    Expect(!std::strcmp(kFactions[kEdf].id,"edf") && !std::strcmp(kFactions[kEdf2].id,"edf2") && !std::strcmp(kFactions[kEdf3].id,"edf3") &&
           !std::strcmp(kFactions[kEdf4].id,"edf4") && !std::strcmp(kFactions[kMonster].id,"monster") &&
           !std::strcmp(kFactions[kPrimer].id,"primer") && !std::strcmp(kFactions[kForeigner].id,"foreigner"),"the faction ids");
    Expect(kFactions[kEdf].side==Side::edf && kFactions[kEdf4].side==Side::edf && kFactions[kMonster].side==Side::monster &&
           kFactions[kPrimer].side==Side::primer && kFactions[kForeigner].side==Side::foreigner,"the faction sides");
    std::set<int> teams;std::set<std::string> ids;std::set<std::uint32_t> colors;
    for(int f=0;f<kFactionCount;++f) {
        teams.insert(kFactions[f].team);ids.insert(kFactions[f].id);colors.insert(kFactions[f].color);
        Expect(kFactions[f].id && kFactions[f].id[0],"a faction has an id");
        Expect((kFactions[f].color>>24)==0xFF,"a faction's colour is opaque");
        Expect(kFactions[f].team<tn::kTeamCapacity,"a faction's team is a row of the manager");
        Expect(FactionValid(f) && FactionTeam(f)==kFactions[f].team,"FactionTeam reads the table");
        Expect(TeamFaction(FactionTeam(f))==f,"TeamFaction inverts FactionTeam");
        Expect(PvpTeam(FactionTeam(f)),"a faction's team is a PvP team");
        Expect(Alien(kFactions[f].side)==(f>=kMonster),"the aliens are every faction but the EDF's");
    }
    Expect(static_cast<int>(teams.size())==kFactionCount,"each faction has a team of its own");
    Expect(static_cast<int>(ids.size())==kFactionCount,"the ids are unique");
    Expect(static_cast<int>(colors.size())==kFactionCount,"the colours are unique");
    Expect(!FactionValid(-1) && !FactionValid(kFactionCount) && !FactionValid(255),"the faction range");
    Expect(FactionTeam(-1)==-1 && FactionTeam(kFactionCount)==-1,"no team for a faction past the table");
    const std::set<int> want{1,2,7,8,9,10,11};
    for(int t=-3;t<300;++t) {
        Expect(PvpTeam(t)==(want.count(t)==1),"PvpTeam is exactly {1, 2, 7..11}");
        Expect((TeamFaction(t)>=0)==(want.count(t)==1),"TeamFaction finds exactly those teams");
        if(TeamFaction(t)>=0)Expect(FactionTeam(TeamFaction(t))==t,"FactionTeam inverts TeamFaction");
    }
    Expect(!PvpTeam(0) && !PvpTeam(3) && !PvpTeam(4) && !PvpTeam(5) && !PvpTeam(6),"the stock teams 0, 3..6 are nobody's faction");
    // The faction teams sit where the manager has rows for them.
    for(int f=0;f<kFactionCount;++f)Expect(kFactions[f].team==1 || kFactions[f].team==2 || kFactions[f].team>=tn::kFirstFactionTeam,"a new team is past the stock rows");
}

void Roles() {
    for(int f=-2;f<kFactionCount+3;++f)for(int r=-2;r<kRoleCount+3;++r) {
        bool want=false;
        if(FactionValid(f) && r>=0 && r<kRoleCount) {
            if(r==static_cast<int>(Role::commander))want=true;
            else if(kFactions[f].side==Side::edf)want=r==static_cast<int>(Role::soldier);
            else want=r==static_cast<int>(Role::unit);
        }
        Expect(RoleAllowed(f,r)==want,"RoleAllowed table");
    }
    for(int f=kEdf;f<=kEdf4;++f) {
        Expect(RoleAllowed(f,static_cast<int>(Role::soldier)) && RoleAllowed(f,static_cast<int>(Role::commander)) &&
               !RoleAllowed(f,static_cast<int>(Role::unit)),"an EDF faction takes a soldier or the commander");
        Expect(DefaultRole(f)==static_cast<int>(Role::soldier),"an EDF faction starts as a soldier");
    }
    for(int f=kMonster;f<=kForeigner;++f) {
        Expect(RoleAllowed(f,static_cast<int>(Role::unit)) && RoleAllowed(f,static_cast<int>(Role::commander)) &&
               !RoleAllowed(f,static_cast<int>(Role::soldier)),"an alien faction takes a unit or the commander");
        Expect(DefaultRole(f)==static_cast<int>(Role::unit),"an alien faction starts as a unit");
    }
    for(int f=0;f<kFactionCount;++f)Expect(RoleAllowed(f,DefaultRole(f)),"the default role is allowed");

    const std::uint16_t units[]={kNoUnit,0,5,0x1234,0xFFFE};
    for(int f=-1;f<=kFactionCount;++f)for(int r=-1;r<=kRoleCount;++r)for(std::uint16_t u:units) {
        const Pick p{static_cast<std::uint8_t>(f),static_cast<std::uint8_t>(r),u};
        const bool want=RoleAllowed(static_cast<std::uint8_t>(f),static_cast<std::uint8_t>(r)) &&
            (static_cast<std::uint8_t>(r)==static_cast<std::uint8_t>(Role::unit))==(u!=kNoUnit);
        Expect(PickValid(p)==want,"PickValid: an allowed role, and a unit exactly for the unit role");
    }
    Expect(PickValid({kEdf,0,kNoUnit}) && PickValid({kEdf2,1,kNoUnit}) && PickValid({kMonster,2,7}) && PickValid({kForeigner,1,kNoUnit}),"examples of valid picks");
    Expect(!PickValid({kMonster,0,kNoUnit}) && !PickValid({kMonster,2,kNoUnit}) && !PickValid({kEdf,0,3}) && !PickValid({kEdf,2,3}) &&
           !PickValid({kEdf,1,3}) && !PickValid({7,0,kNoUnit}) && !PickValid({0,3,kNoUnit}),"examples of invalid picks");
    Pick none;
    Expect(none.faction==0 && none.role==0 && none.unit==kNoUnit && PickValid(none),"a default pick is the first faction's soldier");
}

bool Symmetric(const Relations& r) {
    for(int a=0;a<kMaxFactions;++a)for(int b=0;b<kMaxFactions;++b)if(r.rel[a][b]!=r.rel[b][a])return false;
    return true;
}
void Presets() {
    for(Preset p:kPresets) {
        Relations r;r.FromPreset(p);
        Expect(r.preset==static_cast<std::uint8_t>(p),"FromPreset records the preset");
        Expect(r.Valid(),"a preset's table is valid");
        Expect(Symmetric(r),"a preset's table is symmetric");
        for(int a=0;a<kMaxFactions;++a)for(int b=0;b<kMaxFactions;++b) {
            if(a==b)Expect(r.rel[a][b]==tn::kFriend,"a faction is friendly to itself");
            else if(a>=kFactionCount || b>=kFactionCount)Expect(r.rel[a][b]==tn::kNeutral,"nothing past the catalogue is related");
            else Expect(r.rel[a][b]==PresetRelation(p,a,b),"the table is the preset's relation");
        }
    }
    auto Rel=[](Preset p,int a,int b){Relations r;r.FromPreset(p);return r.rel[a][b];};
    // free for all
    for(int a=0;a<kFactionCount;++a)for(int b=0;b<kFactionCount;++b)
        if(a!=b)Expect(Rel(Preset::freeForAll,a,b)==tn::kEnemy,"free for all: everyone against everyone");
    // humans against aliens
    Expect(Rel(Preset::humansVsAliens,kEdf,kEdf2)==tn::kFriend && Rel(Preset::humansVsAliens,kEdf3,kEdf4)==tn::kFriend,"humans vs aliens: the EDF factions together");
    Expect(Rel(Preset::humansVsAliens,kMonster,kPrimer)==tn::kFriend && Rel(Preset::humansVsAliens,kPrimer,kForeigner)==tn::kFriend &&
           Rel(Preset::humansVsAliens,kMonster,kForeigner)==tn::kFriend,"humans vs aliens: the aliens together");
    Expect(Rel(Preset::humansVsAliens,kEdf,kMonster)==tn::kEnemy && Rel(Preset::humansVsAliens,kEdf4,kForeigner)==tn::kEnemy,"humans vs aliens: the sides oppose");
    // EDF civil war
    Expect(Rel(Preset::edfCivilWar,kEdf,kEdf2)==tn::kEnemy && Rel(Preset::edfCivilWar,kEdf3,kEdf4)==tn::kEnemy,"civil war: the EDF factions oppose");
    Expect(Rel(Preset::edfCivilWar,kMonster,kPrimer)==tn::kFriend && Rel(Preset::edfCivilWar,kMonster,kForeigner)==tn::kFriend,"civil war: the aliens together");
    Expect(Rel(Preset::edfCivilWar,kEdf,kMonster)==tn::kEnemy && Rel(Preset::edfCivilWar,kEdf2,kPrimer)==tn::kEnemy,"civil war: the aliens against every EDF faction");
    // aliens at war
    Expect(Rel(Preset::aliensWar,kEdf,kEdf3)==tn::kFriend && Rel(Preset::aliensWar,kEdf2,kEdf4)==tn::kFriend,"aliens war: the EDF factions together");
    Expect(Rel(Preset::aliensWar,kMonster,kPrimer)==tn::kEnemy && Rel(Preset::aliensWar,kPrimer,kForeigner)==tn::kEnemy &&
           Rel(Preset::aliensWar,kMonster,kForeigner)==tn::kEnemy,"aliens war: the aliens against each other");
    Expect(Rel(Preset::aliensWar,kEdf,kMonster)==tn::kEnemy && Rel(Preset::aliensWar,kEdf3,kForeigner)==tn::kEnemy,"aliens war: the EDF against all");
    // the presets are different tables
    for(size_t i=0;i<4;++i)for(size_t j=i+1;j<4;++j) {
        Relations a,b;a.FromPreset(kPresets[i]);b.FromPreset(kPresets[j]);
        Expect(!a.Same(b),"each preset is a table of its own");
    }
    // a custom table starts from any valid one
    Relations custom;custom.FromPreset(Preset::custom);
    Expect(custom.preset==static_cast<std::uint8_t>(Preset::custom) && custom.Valid(),"the custom preset's first table is valid");

    Relations base;base.FromPreset(Preset::humansVsAliens);
    Relations same=base;Expect(same.Same(base),"a copy is the same");
    same.preset=static_cast<std::uint8_t>(Preset::custom);Expect(!same.Same(base),"another preset is another table");
    same=base;same.rel[kEdf][kMonster]=same.rel[kMonster][kEdf]=tn::kNeutral;Expect(!same.Same(base),"another relation is another table");
}

void Tables() {
    Relations r;r.FromPreset(Preset::edfCivilWar);
    r.preset=static_cast<std::uint8_t>(Preset::custom);
    Expect(r.Valid(),"a preset's table under the custom preset is valid");
    // the host edits pairs: neutral, friend and enemy, each both ways
    for(std::uint8_t v:{tn::kNeutral,tn::kFriend,tn::kEnemy}) {
        Relations c=r;c.rel[kEdf2][kPrimer]=c.rel[kPrimer][kEdf2]=v;
        Expect(c.Valid(),"a custom pair of any relation is valid");
    }
    Relations bad=r;bad.rel[kEdf][kEdf2]=tn::kFriend;
    Expect(!bad.Valid(),"a relation only one way is invalid");
    bad=r;bad.rel[kEdf][kEdf]=tn::kEnemy;Expect(!bad.Valid(),"a faction hostile to itself is invalid");
    bad=r;bad.rel[kMonster][kMonster]=tn::kNeutral;Expect(!bad.Valid(),"a faction neutral to itself is invalid");
    bad=r;bad.rel[kEdf][kEdf2]=bad.rel[kEdf2][kEdf]=3;Expect(!bad.Valid(),"a relation the manager has no value for is invalid");
    bad=r;bad.rel[kEdf][kFactionCount]=bad.rel[kFactionCount][kEdf]=tn::kEnemy;Expect(!bad.Valid(),"a relation to a faction past the catalogue is invalid");
    bad=r;bad.rel[kMaxFactions-1][kMaxFactions-1]=tn::kEnemy;Expect(!bad.Valid(),"a slot past the catalogue keeps its diagonal friendly too");
    bad=r;bad.rel[kFactionCount][kFactionCount+1]=bad.rel[kFactionCount+1][kFactionCount]=tn::kFriend;Expect(!bad.Valid(),"slots past the catalogue stay neutral");
    bad=r;bad.preset=static_cast<std::uint8_t>(kPresetCount);Expect(!bad.Valid(),"a preset past the count is invalid");
    bad=r;bad.preset=255;Expect(!bad.Valid(),"preset 255 is invalid");
    // a named preset has only its own table
    Relations named;named.FromPreset(Preset::humansVsAliens);
    bad=named;bad.rel[kEdf][kMonster]=bad.rel[kMonster][kEdf]=tn::kFriend;Expect(!bad.Valid(),"a preset with another table is invalid");
    bad=named;bad.preset=static_cast<std::uint8_t>(Preset::freeForAll);Expect(!bad.Valid(),"a table under another preset's name is invalid");
    Relations zero{};Expect(!zero.Valid(),"an all-zero table has no friendly diagonal");
    Relations dflt;Expect(dflt.preset==static_cast<std::uint8_t>(Preset::freeForAll),"the default preset");

    // the host's cycle never lands on the custom table
    int p=static_cast<int>(Preset::freeForAll);std::set<int> seen;
    for(int i=0;i<12;++i){p=NextPreset(p);Expect(p!=static_cast<int>(Preset::custom) && p>=0 && p<kPresetCount,"NextPreset is a named preset");seen.insert(p);}
    Expect(seen.size()==4,"NextPreset visits every named preset");
    p=0;for(int i=0;i<4;++i)p=NextPreset(p);Expect(p==0,"NextPreset cycles in four steps");
    Expect(NextPreset(0)==1 && NextPreset(1)==2 && NextPreset(2)==3 && NextPreset(3)==0,"NextPreset in order");
    Expect(NextPreset(static_cast<int>(Preset::custom))==0,"a custom table cycles back to the first preset");
}

tn::Overlay Overlay(const Relations& r){return TeamOverlay(r);}
void Overlays() {
    tn::Overlay stock;stock.Stock();
    for(Preset preset:kPresets) {
        Relations r;r.FromPreset(preset);
        const tn::Overlay o=Overlay(r);
        Expect(o.Valid(),"a match's overlay is a valid team_native overlay");
        for(int a=0;a<kFactionCount;++a)for(int b=0;b<kFactionCount;++b)
            Expect(o.rel[kFactions[a].team][kFactions[b].team]==r.rel[a][b],"a pair of faction teams has the factions' relation");
        // team 0 (the stock players' other team) follows the first EDF faction's team 2
        const int edf=kFactions[kEdf].team;
        for(int f=0;f<kFactionCount;++f) {
            const int t=kFactions[f].team;
            if(t==edf)continue;
            Expect(o.rel[0][t]==o.rel[edf][t] && o.rel[t][0]==o.rel[t][edf],"team 0 mirrors team 2");
        }
        Expect(o.rel[0][edf]==tn::kFriend && o.rel[edf][0]==tn::kFriend && o.rel[0][0]==tn::kFriend,"team 0 stays friend of team 2");
        // teams 3..6 keep their stock rows, both ways
        for(int t=3;t<=6;++t)for(int x=0;x<tn::kTeamCapacity;++x)
            Expect(o.rel[t][x]==stock.rel[t][x] && o.rel[x][t]==stock.rel[x][t],"teams 3..6 stay stock");
        // rows of teams no faction has and the faction teams' unlisted columns stay stock
        for(int t=0;t<tn::kTeamCapacity;++t)for(int x=0;x<tn::kTeamCapacity;++x) {
            if(PvpTeam(t) && PvpTeam(x))continue;
            if(t==0 || x==0)continue;
            Expect(o.rel[t][x]==stock.rel[t][x],"outside the factions the relations are stock");
        }
        for(int t=0;t<tn::kTeamCapacity;++t)Expect(o.rel[t][t]==tn::kFriend,"every diagonal is friendly");
        // a symmetric table gives a symmetric overlay
        for(int a=0;a<tn::kTeamCapacity;++a)for(int b=0;b<tn::kTeamCapacity;++b)Expect(o.rel[a][b]==o.rel[b][a],"the overlay is symmetric");
    }
    {   // humans against aliens, spelled out in teams
        Relations r;r.FromPreset(Preset::humansVsAliens);
        const tn::Overlay o=Overlay(r);
        Expect(o.rel[2][7]==tn::kFriend && o.rel[2][9]==tn::kFriend && o.rel[8][9]==tn::kFriend,"overlay: the EDF teams are friends");
        Expect(o.rel[1][10]==tn::kFriend && o.rel[10][11]==tn::kFriend && o.rel[1][11]==tn::kFriend,"overlay: the alien teams are friends");
        Expect(o.rel[2][1]==tn::kEnemy && o.rel[7][10]==tn::kEnemy && o.rel[9][11]==tn::kEnemy,"overlay: the sides are enemies");
        Expect(o.rel[0][7]==tn::kFriend && o.rel[0][1]==tn::kEnemy && o.rel[0][10]==tn::kEnemy,"overlay: team 0 follows team 2");
    }
    {   // free for all: the first EDF faction is the stock players' team 2, so team 0 is hostile to the others
        Relations r;r.FromPreset(Preset::freeForAll);
        const tn::Overlay o=Overlay(r);
        Expect(o.rel[2][7]==tn::kEnemy && o.rel[0][7]==tn::kEnemy && o.rel[7][0]==tn::kEnemy && o.rel[0][2]==tn::kFriend,"overlay: free for all");
        Expect(o.rel[7][8]==tn::kEnemy && o.rel[10][11]==tn::kEnemy,"overlay: the new teams oppose each other");
    }
    {   // a host's own table
        Relations r;r.FromPreset(Preset::aliensWar);r.preset=static_cast<std::uint8_t>(Preset::custom);
        r.rel[kEdf][kMonster]=r.rel[kMonster][kEdf]=tn::kNeutral;
        r.rel[kEdf2][kForeigner]=r.rel[kForeigner][kEdf2]=tn::kFriend;
        Expect(r.Valid(),"the host's table is valid");
        const tn::Overlay o=Overlay(r);
        Expect(o.Valid(),"the host's table makes a valid overlay");
        Expect(o.rel[2][1]==tn::kNeutral && o.rel[1][2]==tn::kNeutral,"overlay: the host made monsters and the first EDF neutral");
        Expect(o.rel[7][11]==tn::kFriend && o.rel[11][7]==tn::kFriend,"overlay: the host made EDF 2 and the Foreigners friends");
        Expect(o.rel[0][1]==tn::kNeutral && o.rel[0][11]==o.rel[2][11],"overlay: team 0 follows team 2 under the host's table");
    }
}
}  // namespace

int main() {
    Factions();Roles();Presets();Tables();Overlays();
    if(failures){std::printf("pvp_match_test: %d of %d checks FAILED\n",failures,checks);return 1;}
    std::printf("pvp_match_test: %d checks passed\n",checks);return 0;
}
