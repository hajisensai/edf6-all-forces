// The PvP unit catalogue (src/pvp_units.h) offline: each unit is a debug spawn enemy row, belongs to one side, the
// helpers (Row / Playable / DefaultRow / Next) agree with it, and the structures stay out.
// cmake --build build --target pvp_units_test && build\pvp_units_test.exe
#include "../src/pvp_units.h"
#include <cstdio>
#include <cstring>

namespace {
using namespace crew;
using namespace crew::pvp_units;
namespace ds=crew::debugspawn;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
constexpr Side kEnemySides[]={Side::monster,Side::primer,Side::foreigner};
bool PlayableAny(const char* id) {
    const int row=Row(id);
    for(Side s:kEnemySides)if(Playable(row,s))return true;
    return false;
}
bool PlayableAs(const char* id,Side s) { return Playable(Row(id),s); }
}

int main() {
    // Row: a log id to its index; unknown, null and empty give -1; the index names the same id back.
    Expect(Row("GIANTANT01")>=0,"GIANTANT01 is a row");
    Expect(Row("GIANTANT01")>=0 && std::strcmp(ds::kEntries[Row("GIANTANT01")].id,"GIANTANT01")==0,"Row indexes kEntries");
    Expect(Row("no_such_row")==-1 && Row(nullptr)==-1 && Row("")==-1,"unknown ids give -1");
    Expect(Row("EDF41MP01/GIANTANT04")>=0 && Row("EDF41MP01/GIANTANT04")!=Row("GIANTANT04"),"a folder id is its own row");

    // Every unit: a unique enemy / enemy41 row, on a known side; the units are in catalogue order.
    int previous=-1;
    for(const Unit& u:kUnits) {
        const int row=Row(u.id);
        Expect(row>previous,u.id);
        previous=row;
        const ds::Category c=row>=0 ? ds::kEntries[row].category : ds::Category::count;
        Expect(c==ds::Category::enemy || c==ds::Category::enemy41,u.id);
        Expect(Playable(row,u.side),u.id);
        for(Side s:kEnemySides)if(s!=u.side)Expect(!Playable(row,s),u.id);
        Expect(!Playable(row,Side::edf),u.id);
    }
    Expect(Count(Side::monster)+Count(Side::primer)+Count(Side::foreigner)==kUnitCount && Count(Side::edf)==0,"counts add up");
    for(Side s:kEnemySides)Expect(Count(s)>0,"every enemy side has units");

    // The sides, by the user's words.
    const char* monsters[]={"GIANTANT01","E651_SPIDER01","GIANTBEE01","YERROWSMALLDRAGON","E509_DEIROI","NEPHILA","MONSTER501",
        "E504_MONSTERA","E514_DANGO","E604_RADON","E606_SHELLFISH","E603_MERMAN","E618_SQUID_SMALL","E660_HEAVY_DANGO_L",
        "EDF41MP01/GIANTANT04","EDF41MP02/GIANTBEEQUEEN02","EDF41MP01/NEPHILA02L","EDF41_REDSMALLDRAGON","DRAGONBIG401","DEIROI401"};
    for(const char* id:monsters)Expect(PlayableAs(id,Side::monster),id);
    const char* primers[]={"E601_MARTIAN_GS","E661_MARTIAN_LEADER_CS","E503_FROG_AF","E506_BIGGREY_AF","E516_BIGGREYBOSS",
        "E616_VENUS_GS","E602_BERSERKER_LARGE","E602_BERSERKER","E513_SHIELDBEARER","E507_GOLDUFO","E515_IMPERIALUFO",
        "E605_SPINNERUFO","E508_CARRIER","E611_TIMESHIP","E512_DROPBOAT"};
    for(const char* id:primers)Expect(PlayableAs(id,Side::primer),id);
    const char* foreigners[]={"HECTOR301CC","HECTOR401SS","ALIENTRAILER401","UFO301","UFO401","EDF41MP02/UFO401ACE","UFOCARRIER301",
        "UFOCARRIER401","UFOROBO401","UFOMOTHER301","UFOMOTHER401"};
    for(const char* id:foreigners)Expect(PlayableAs(id,Side::foreigner),id);

    // Structures and what enemy control cannot take over stay out (the rows exist, so a rename shows here).
    const char* out[]={"E505_GENERATOR","E612_ANCHOR","E517_ANTHILL","HORNETNEST401","GIANTANT01EGG","E663_SPIDER_EGG_SAC",
        "SHOOTINGTARGET","E510_FORTRESS","E510_DLC_FORTRESS","E511_MOTHERSHIP","E511_MOTHERSHIP_EDF6","E609_JORMUNGAND",
        "E614_GRANDMOTHER","ANTHILL301","ALIEN4LEGTANK301","TRANSPORTER401_FORMATION"};
    for(const char* id:out) {
        Expect(Row(id)>=0,id);
        Expect(!PlayableAny(id),id);
    }
    // Other categories are never units.
    Expect(Row("VEHICLE403_TANK_MISSION")>=0 && Row("call_strike_f")>=0,"the other categories' rows exist");
    Expect(!Playable(Row("VEHICLE403_TANK_MISSION"),Side::monster) && !Playable(Row("call_strike_f"),Side::primer),"vehicles, aircraft are not units");
    Expect(!Playable(-1,Side::monster) && !Playable(ds::kEntryCount,Side::primer) && !Playable(1000000,Side::foreigner),"bad rows are not playable");

    // DefaultRow: playable on its side; the typical picks.
    for(Side s:kEnemySides)Expect(Playable(DefaultRow(s),s),"the default row is the side's");
    Expect(DefaultRow(Side::monster)==Row("GIANTANT01"),"monsters start as a giant ant");
    Expect(std::strncmp(ds::kEntries[DefaultRow(Side::primer)].id,"E601_MARTIAN",12)==0,"primers start as a Martian");
    Expect(std::strncmp(ds::kEntries[DefaultRow(Side::foreigner)].id,"HECTOR",6)==0,"foreigners start as a Hector");
    Expect(DefaultRow(Side::edf)==-1 && NthRow(Side::edf,0)==-1 && Next(0,Side::edf,1)==-1,"the EDF has none");

    // Next: cycles through exactly the side's rows, both ways, wrapping, any step size.
    for(Side s:kEnemySides) {
        const int n=Count(s);
        int row=NthRow(s,0);
        Expect(row>=0 && Playable(row,s),"the first row");
        static bool seen[ds::kEntryCount];
        std::memset(seen,0,sizeof(seen));
        int visited=0;
        for(int k=0;k<n;++k) {
            Expect(Playable(row,s),"every step stays on the side");
            Expect(!seen[row],"no row twice before the wrap");
            seen[row]=true;++visited;
            row=Next(row,s,1);
        }
        Expect(visited==n && row==NthRow(s,0),"+1 visits every row once and wraps to the first");
        Expect(Next(NthRow(s,0),s,-1)==NthRow(s,n-1),"-1 from the first wraps to the last");
        Expect(Next(NthRow(s,n-1),s,1)==NthRow(s,0),"+1 from the last wraps to the first");
        Expect(Next(NthRow(s,0),s,n)==NthRow(s,0) && Next(NthRow(s,0),s,-n)==NthRow(s,0),"a full turn is the same row");
        Expect(Next(NthRow(s,0),s,3*n+1)==NthRow(s,1 % n),"a large step wraps");
        Expect(Next(NthRow(s,1 % n),s,-(2*n+1))==NthRow(s,0),"a large negative step wraps");
        Expect(Next(NthRow(s,0),s,0)==NthRow(s,0),"step 0 stays");
        // A row that is not this side's enters at an end, never leaves the side.
        const int stranger=Row("VEHICLE403_TANK_MISSION");
        Expect(Next(stranger,s,1)==NthRow(s,0) && Next(stranger,s,-1)==NthRow(s,n-1),"a foreign row enters at an end");
        Expect(Next(-1,s,1)==NthRow(s,0) && Next(1000000,s,-1)==NthRow(s,n-1),"a bad row enters at an end");
        Expect(Next(stranger,s,0)==DefaultRow(s),"step 0 from a foreign row is the default");
    }
    Expect(Next(Row("GIANTANT01"),Side::primer,1)==NthRow(Side::primer,0),"another side's row enters at the first");

    std::printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
