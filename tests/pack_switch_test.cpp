// The HQ's mission pack switch, its pure side (src/pack_switch_logic.h) offline: which mode Q / E go to (offline story
// modes the dialog lets the player pick, round the ModeList, never the online ones nor an unowned DLC), the routine's
// declaration and code, and the keys' edge. Exit code 1 when one fails.
// cmake --build build --target pack_switch_test && build\pack_switch_test.exe
#include "../src/pack_switch_logic.h"
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace {
using namespace crew::packswitch;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

// The ModeList every pack installed writes (tools/make_edf5_campaign.py): offline / online pairs, EDF6 0 / DLC 1 / 2,
// then 65..71. `owned`: which of the ten offline modes the dialog enables.
int Table(ModeView* m,const bool (&owned)[10]) {
    for(int i=0;i<20;++i){m[i].offlineStory=i%2==0;m[i].selectable=m[i].offlineStory && owned[i/2];}
    return 20;
}

void Steps() {
    ModeView m[kMaxModes]{};
    const bool all[10]={true,true,true,true,true,true,true,true,true,true};
    int n=Table(m,all);
    Expect(Step(m,n,0,1)==2 && Step(m,n,2,1)==4 && Step(m,n,4,1)==6,"E: the next offline mode in the list's order");
    Expect(Step(m,n,18,1)==0,"E on the last pack: round to EDF6's story");
    Expect(Step(m,n,0,-1)==18 && Step(m,n,6,-1)==4,"Q: the previous one, round the start");
    for(int i=0;i<n;++i)
        for(int d=-1;d<=1;d+=2) {
            const int to=Step(m,n,i,d);
            Expect(to>=0 && to%2==0 && to!=i,"never an online mode, never the mode itself");
        }
    const bool noDlc[10]={true,false,false,true,true,true,true,true,true,true};
    n=Table(m,noDlc);
    Expect(Step(m,n,0,1)==6 && Step(m,n,6,-1)==0,"EDF6's DLC not owned (the dialog greys them): skipped");
    const bool alone[10]={true,false,false,false,false,false,false,false,false,false};
    n=Table(m,alone);
    Expect(Step(m,n,0,1)==-1 && Step(m,n,0,-1)==-1,"nothing else owned: no switch");
    Expect(Step(m,n,1,1)==0,"from an online mode (never the HQ's, but defined): the next offline one");
    Expect(Step(m,n,-1,1)==0 && Step(m,n,99,-1)==0,"no current mode: the first / last offline one");
    Expect(Step(m,n,0,0)==-1 && Step(m,n,0,2)==-1 && Step(nullptr,n,0,1)==-1 && Step(m,0,0,1)==-1
           && Step(m,kMaxModes+1,0,1)==-1,"bad direction / table: none");
}

void Routines() {
    wchar_t wide[64];
    char narrow[64],code[256];
    Expect(Declaration(6,wide,64) && !std::wcscmp(wide,L"string AF_PackSwitch6()"),"the declaration main() would build");
    Expect(NarrowDeclaration(18,narrow,sizeof(narrow)) && !std::strcmp(narrow,"string AF_PackSwitch18()"),"its narrow form (GetFunctionByDecl)");
    Expect(Code(6,code,sizeof(code)) && !std::strcmp(code,
        "string AF_PackSwitch6() { if( ApplyModeNo(6) ) return \"HQMain\"; return \"StartToTitle\"; }"),
        "the code: ApplyModeNo, then the new HQ, else the title");
    Expect(NarrowDeclaration(6,narrow,sizeof(narrow)) && !std::strncmp(code,narrow,std::strlen(narrow)),
           "the code defines the declaration createCoRoutine is given");
    Expect(!Declaration(-1,wide,64) && !Declaration(kMaxModes,wide,64) && !Code(-1,code,sizeof(code)),"out of range: none");
    Expect(!Code(6,code,20) && !NarrowDeclaration(6,narrow,8) && !Declaration(6,wide,8),"a buffer too small: none");
}

void KeyEdges() {
    const Keys up{},q{true,false},e{false,true},both{true,true};
    Expect(Pressed(up,q)==-1 && Pressed(up,e)==1,"Q / E going down");
    Expect(Pressed(q,q)==0 && Pressed(e,e)==0,"held: once");
    Expect(Pressed(up,both)==0 && Pressed(q,both)==0,"both down: nothing");
    Expect(Pressed(q,e)==1,"Q released and E down in one frame: E");
    Expect(Pressed(both,q)==0,"one of both released: the other was already down");
}
}  // namespace

int main() {
    Steps();
    Routines();
    KeyEdges();
    std::printf("%d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
