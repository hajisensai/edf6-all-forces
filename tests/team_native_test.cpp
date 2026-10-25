// The team manager's capacity sites and the relation overlay (src/team_native_logic.h) offline: every site changes only
// its 7 / 0x188 immediate, to the capacity; the stock relations are the ones docs/team-manager-re.md reads from the
// constructor; an overlay keeps every diagonal friendly and the empty-vehicle / environment rows stock.
// cmake --build build --target team_native_test && build\team_native_test.exe
#include "../src/team_native_logic.h"
#include <cstdio>
#include <cstring>

namespace {
using namespace crew::team_native;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
}  // namespace

int main() {
    static_assert(kTeamCapacity>kNativeTeams && kTeamCapacity<=127,"an imm8 holds the capacity");
    static_assert(kTeamCapacity*kRowStride<0x10000,"the rows loop's imm32 is written as two bytes");
    unsigned rowLoops=0;
    for(const Site& s:kSites) {
        unsigned changed=0;
        for(unsigned i=0;i<s.size;++i)if(s.stock[i]!=s.patched[i])++changed;
        const bool seven=changed==1 && [&]{for(unsigned i=0;i<s.size;++i)if(s.stock[i]!=s.patched[i])return s.stock[i]==7 && s.patched[i]==kTeamCapacity;return false;}();
        const bool rows=s.size==7 && s.stock[3]==0x88 && s.stock[4]==0x01 && s.patched[3]==(kTeamCapacity*kRowStride&0xFF) &&
            s.patched[4]==(kTeamCapacity*kRowStride>>8) && !std::memcmp(s.stock,s.patched,3);
        rowLoops+=rows;
        Expect(seven || rows,"a site changes only its 7 or its 7 rows");
        Expect(s.size>=4 && s.size<=8,"a site is one whole instruction");
    }
    Expect(rowLoops==2,"two row loops: the constructor's and the every-team walk's");
    Expect(kSiteCount==15,"fifteen capacity sites");
    for(std::size_t i=0;i<kSiteCount;++i)for(std::size_t j=i+1;j<kSiteCount;++j)
        Expect(kSites[i].rva+kSites[i].size<=kSites[j].rva || kSites[j].rva+kSites[j].size<=kSites[i].rva,"sites do not overlap");

    // The stock table (ctor 0x5E0600..0x5E06D6).
    const int enemies[][2]={{0,1},{1,0},{1,2},{2,1},{1,4},{4,1}};
    for(auto& e:enemies)Expect(StockRelation(e[0],e[1])==kEnemy,"stock enemies");
    Expect(StockRelation(0,2)==kFriend && StockRelation(2,0)==kFriend,"0 and 2 friends");
    for(int t=0;t<kTeamCapacity;++t)Expect(StockRelation(t,t)==kFriend,"stock diagonal");
    Expect(StockRelation(3,1)==kNeutral && StockRelation(5,2)==kNeutral && StockRelation(6,1)==kNeutral && StockRelation(4,2)==kNeutral,"stock neutrals");
    for(int t=kFirstFactionTeam;t<kTeamCapacity;++t)for(int o=0;o<kTeamCapacity;++o)
        if(o!=t)Expect(StockRelation(t,o)==kNeutral && StockRelation(o,t)==kNeutral,"faction rows start neutral");

    Overlay o{};
    o.Stock();
    Expect(o.Valid(),"stock overlay valid");
    o.rel[7][8]=o.rel[8][7]=kEnemy;o.rel[7][1]=o.rel[1][7]=kEnemy;o.rel[9][1]=o.rel[1][9]=kFriend;
    Expect(o.Valid(),"faction relations valid");
    Overlay bad=o;bad.rel[7][7]=kEnemy;Expect(!bad.Valid(),"a diagonal stays friendly");
    bad=o;bad.rel[5][7]=kEnemy;Expect(!bad.Valid(),"the empty-vehicle row stays stock");
    bad=o;bad.rel[8][6]=kFriend;Expect(!bad.Valid(),"the environment column stays stock");
    bad=o;bad.rel[2][3]=3;Expect(!bad.Valid(),"only 0, 1, 2");
    Expect(kResetSites[0].target==kResetDefaults && kResetSites[4].target==kResetIdentity,"reset sites");

    std::printf("team_native: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
