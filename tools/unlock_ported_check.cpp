// The ported content unlock's parts that need no game (src/unlock_ported.h), checked offline:
//  - which content ids are the six ported story packs (not the test range, not EDF6's 0..2);
//  - the mission select's rows: only the list as its first loop leaves it is opened, every hidden row open (a branch
//    row as the scan would open it, 2), row 0 as it was; a list in any other shape left alone;
//  - the scan that follows (0x8A21C0..0x8A2390, replayed here from the disassembly) then shows every row of a save that
//    cleared nothing, where the stock list shows row 0 alone; cleared rows keep their cleared state;
//  - the weapon rows: EDF6VC_E5_* and EDF6VC_E41_* only (never the call weapons, a placeholder, EDF6's own rows);
//  - owning a record: NEW and 0 stars the first time, stars kept after, no bit ever cleared.
// Exit code 1 when one fails. cmake --build build --target unlock_ported_check && build\unlock_ported_check.exe
#include "../src/unlock_ported.h"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
using namespace crew::ported;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s\n",what);
}

// The list as 0x8A2160..0x8A219D leaves it.
std::vector<ListRow> Fresh(std::size_t n) {
    std::vector<ListRow> rows(n);
    for(std::size_t i=0;i<n;++i){rows[i]=ListRow{};rows[i].row=static_cast<std::int32_t>(i);rows[i].state=kHidden;}
    rows[0].state=kOpen;
    return rows;
}
// The scan (0x8A21C0..0x8A2390) over the rows, a chain i -> i+1 (field 3), offline (it stops). cleared(i): 0x8A4C00.
// What it leaves shown (states 0..3, 0x8A23C1..0x8A241A).
template<class Cleared,class Branch> std::vector<int> Scan(std::vector<ListRow>& rows,Cleared cleared,Branch branch) {
    const auto n=static_cast<std::uint32_t>(rows.size());
    int stop=-1;
    std::uint32_t last=0;   // r15d: the last cleared row (0x8A227F)
    for(std::uint32_t r=0;r<n;++r) {
        if(cleared(r)) {
            rows[r].state=kCleared;
            last=r;
            if(r<n-1 && stop==-1 && rows[r+1].state==kHidden)rows[r+1].state=branch(r+1)==1 ? kOpenBranch : kOpen;
        } else if(stop==-1 && rows[r].state==kHidden)stop=static_cast<int>(r);
        if(stop!=-1)break;   // offline: the mode's +0x68 is 0
    }
    for(std::uint32_t r=0;r<last;++r)if(rows[r].state==kHidden)rows[r].state=3;
    std::vector<int> shown;
    for(std::uint32_t r=0;r<n;++r)if(rows[r].state>=0 && rows[r].state<=3)shown.push_back(static_cast<int>(r));
    return shown;
}
}  // namespace

int main() {
    // The packs: EDF5 3..5, the test range 6, EDF4.1 7..9 (the installer's order).
    for(int id=0;id<12;++id)Expect(StoryPack(id,3,7)==((id>=3 && id<=5) || (id>=7 && id<=9)),"the six story packs, nothing else");
    Expect(!StoryPack(3,0,0) && !StoryPack(7,0,0),"nothing installed: no pack");
    Expect(StoryPack(7,0,7) && !StoryPack(3,0,7),"EDF4.1 alone");
    for(int id=0;id<12;++id)Expect(Edf41Pack(id,7)==(id>=7 && id<=9),"the 4.1 HQ background: the three 4.1 packs only");
    Expect(!Edf41Pack(7,0) && !Edf41Pack(0,0),"no 4.1 packs installed: never");

    const auto none=[](unsigned){ return false; };
    const auto noBranch=[](unsigned){ return 0; };
    {   // stock: a save that cleared nothing shows row 0 only
        auto rows=Fresh(5);
        Expect(Scan(rows,none,noBranch)==std::vector<int>{0},"stock: row 0 alone");
    }
    {   // unlocked: every row shown, open
        auto rows=Fresh(5);
        Expect(OpenAll(rows.data(),rows.size(),noBranch)==4,"four rows opened");
        Expect(Scan(rows,none,noBranch)==std::vector<int>({0,1,2,3,4}),"unlocked: every row shown");
        bool open=true;
        for(const auto& r:rows)open=open && r.state==kOpen;
        Expect(open,"every row open, none cleared");
    }
    {   // cleared rows stay cleared (the scan writes them), a branch row as the scan opens one
        auto rows=Fresh(6);
        Expect(OpenAll(rows.data(),rows.size(),[](unsigned r){ return r==4 ? 1 : 0; })==5,"five rows opened");
        Expect(rows[4].state==kOpenBranch && rows[3].state==kOpen && rows[0].state==kOpen,"a branch row is 2, others 1");
        const auto shown=Scan(rows,[](unsigned r){ return r==0 || r==2; },noBranch);
        Expect(shown.size()==6 && rows[0].state==kCleared && rows[2].state==kCleared && rows[1].state==kOpen && rows[5].state==kOpen,
               "cleared rows cleared, the rest open, all shown");
    }
    {   // stock again with cleared rows: the chain, and the stop
        auto rows=Fresh(5);
        Expect(Scan(rows,[](unsigned r){ return r<2; },noBranch)==std::vector<int>({0,1,2}),"stock: cleared rows and the next one");
    }
    {   // anything but the fresh list: untouched
        auto rows=Fresh(4);
        rows[2].state=kOpen;
        const auto before=rows;
        Expect(OpenAll(rows.data(),rows.size(),noBranch)==-1,"a row already open: not the fresh list");
        bool same=true;
        for(std::size_t i=0;i<rows.size();++i)same=same && rows[i].state==before[i].state;
        Expect(same,"...and left as it was");
        auto shifted=Fresh(4);
        shifted[3].row=7;
        Expect(OpenAll(shifted.data(),shifted.size(),noBranch)==-1 && shifted[1].state==kHidden,"rows out of order: untouched");
        auto closed=Fresh(3);
        closed[0].state=kHidden;
        Expect(OpenAll(closed.data(),closed.size(),noBranch)==-1,"row 0 not open: untouched");
        Expect(OpenAll(nullptr,3,noBranch)==-1 && OpenAll(rows.data(),0,noBranch)==-1,"no rows");
        auto one=Fresh(1);
        Expect(OpenAll(one.data(),one.size(),noBranch)==0 && one[0].state==kOpen,"a list of one row: nothing to open");
    }

    // The weapon rows.
    Expect(PortedWeapon(L"EDF6VC_E5_ASSULTRIFLE01") && PortedWeapon(L"EDF6VC_E41_ASSULTRIFLE01"),"EDF5's and EDF4.1's rows");
    Expect(PortedWeapon(L"EDF6VC_E5_DLC_VEHICLE_BIKE") && PortedWeapon(L"EDF6VC_E41_WEAPON555"),"the models' and the calls' registries");
    Expect(!PortedWeapon(L"EDF6VC_CALL_INTERCEPTOR") && !PortedWeapon(L"EDF6VC_RETIRED_E5_ASSULTRIFLE01")
           && !PortedWeapon(L"EDF6VC_RETIRED_E41_WEAPON555"),"call weapons and placeholders are not ported weapons");
    Expect(!PortedWeapon(L"eWeapon051") && !PortedWeapon(L"EDF6VC_E5") && !PortedWeapon(L"EDF6VC_E4_X")
           && !PortedWeapon(L"edf6vc_e5_assultrifle01") && !PortedWeapon(L"") && !PortedWeapon(nullptr),
           "EDF6's rows, a bare prefix, another case: no (the game compares names case-sensitively, 0xE1BA0)");

    // Owning a record.
    unsigned char record[12]={0};
    record[4]=7;
    Expect(Own(record) && record[0]==5 && record[4]==0,"first time: owned, NEW, 0 stars");
    record[4]=3;record[0]=1|2;   // used, NEW cleared by a sortie (SetUsedWeapon), a star picked up
    Expect(!Own(record) && record[0]==3 && record[4]==3,"owned already: no NEW again, stars kept, used kept");
    unsigned char up[12]={0};
    up[0]=8|1;
    Expect(!Own(up) && up[0]==9,"no bit cleared");


    // AllWeaponsUnlocked: a table past the native 0x800 (the relocated 0x2000 records), every named row owned at max
    // stars, a placeholder / nameless row untouched, a second pass changes nothing, no bit cleared.
    {
        constexpr std::size_t kRows=0x2000,kPlaceholder=100,kNameless=101;
        std::vector<unsigned char> table(kRows*12,0);
        table[200*12]=3;table[200*12+4]=4;                 // row 200: owned and used, 4 stars
        table[300*12]=1;std::memset(&table[300*12+4],10,8); // row 300: owned at max already
        table[kPlaceholder*12]=8;
        const auto names=[&](std::size_t id)->const wchar_t* {
            return id==kNameless ? nullptr : id==kPlaceholder ? L"EDF6VC_RETIRED_E5_X" : L"eWeapon051";
        };
        const auto pass=[&]{
            int changed=0;
            for(std::size_t id=0;id<kRows;++id)
                if(!Placeholder(names(id)) && OwnMax(&table[id*12]))++changed;
            return changed;
        };
        Expect(pass()==static_cast<int>(kRows-2)-1,"every row but the placeholder, the nameless one and the maxed one changed");
        int owned=0,maxed=0;
        for(std::size_t id=0;id<kRows;++id) {
            owned+=table[id*12]&1;
            bool all=true;
            for(int i=4;i<12;++i)all=all && table[id*12+i]==kMaxStar;
            maxed+=all;
        }
        Expect(owned==static_cast<int>(kRows-2) && maxed==static_cast<int>(kRows-2),"8190 records owned at 10 stars");
        Expect(table[kPlaceholder*12]==8 && table[kNameless*12]==0 && table[kNameless*12+4]==0,"placeholder and nameless rows untouched");
        Expect(table[200*12]==3 && (table[201*12]&4),"used bit kept, NEW on a first grant");
        Expect(table[300*12]==1,"an owned maxed record is not marked NEW");
        Expect(pass()==0,"a second pass changes nothing");
    }

    std::printf("unlock_ported_check: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
