// The bullets' candidate collector's verdict for the plugin's jets (src/ownround.h) checked without the game (the user,
// 2026-10-10: 「炮舰机的机炮有可能会打在自己身上，导致没打出去」):
//  - a jet's own round on its own airframe: left out, by its object (0x108260's answer the jet) and by any of its own body
//    ids when that body's object is something else or nothing (a ragdoll part); a jet's round on a body of another jet
//    that happens to share nothing with it: not own;
//  - wingmen as before: same flight left out, another flight stock, a bomber's bombs through its flight (itself included);
//  - everything not a plugin jet's: stock (friendly fire as it is), the owner test of the stock collector untouched.
// Before (the 2026-10-06 rule, kept here as `Before`): an own round on its own airframe went to the stock collector, whose
// owner test rests on the bullet's "may hit its owner" bit (docs/bullet-pass-re.md §3.2 #1). Exit code 1 on a failure.
// Built on request: cmake --build build --target own_round_check && build\own_round_check.exe
#include "../src/ownround.h"
#include <cstdio>

namespace {
int failures=0;

void Expect(bool ok,const char* what) {
    if(ok)return;
    ++failures;
    std::printf("FAIL %s\n",what);
}

// The 2026-10-06 rule (jet_hooks.cpp Passes before): only a wingman's or a flight's bomb left out; s == t not.
bool Before(const ownround::Craft* c,int n,const void* owner,const void* target) {
    const ownround::Craft* const t=ownround::Find(c,n,target);
    const ownround::Craft* const s=t ? ownround::Find(c,n,owner) : nullptr;
    return t && (s ? s!=t && s->flight==t->flight : ownround::BombOf(c,n,owner,t->flight));
}
}  // namespace

int main() {
    using ownround::Verdict;
    int objs[8]{};
    const void* const gunship=&objs[0];
    const void* const wing=&objs[1];
    const void* const other=&objs[2];
    const void* const bombOwner=&objs[3];
    const void* const soldier=&objs[4];
    const void* const part=&objs[5];   // an object a ragdoll part body might answer with that is not the jet
    ownround::Craft c[3]{
        {gunship,nullptr,1,false,true,3,{100,101,102}},
        {wing,bombOwner,1,true,false,2,{200,201}},
        {other,nullptr,2,false,false,1,{300}},
    };
    const int n=3;

    // Own: by object, by body id with another object, by body id with none.
    ownround::Judged j=ownround::Judge(c,n,gunship,gunship,100);
    Expect(j.verdict==Verdict::own && j.shooter==&c[0] && !j.byBody,"own: the gunship's round on its own object");
    j=ownround::Judge(c,n,gunship,part,101);
    Expect(j.verdict==Verdict::own && j.shooter==&c[0] && j.byBody,"own: on one of its bodies whose object is not the jet");
    j=ownround::Judge(c,n,gunship,nullptr,102);
    Expect(j.verdict==Verdict::own && j.byBody,"own: on one of its bodies with no object");
    j=ownround::Judge(c,n,gunship,gunship,999);
    Expect(j.verdict==Verdict::own,"own: its object whatever the body id");
    Expect(!Before(c,n,gunship,gunship),"before: the own round went to the stock collector (the gap this closes)");
    // A body id of another jet, its object a part: not the shooter's.
    j=ownround::Judge(c,n,gunship,part,300);
    Expect(j.verdict==Verdict::stock,"not own: another jet's body id");
    j=ownround::Judge(c,n,wing,wing,200);
    Expect(j.verdict==Verdict::own && j.shooter==&c[1],"own: a wingman's round on itself");

    // Wingmen and flights as before.
    j=ownround::Judge(c,n,gunship,wing,200);
    Expect(j.verdict==Verdict::wingman && Before(c,n,gunship,wing),"wingman: same flight passes");
    j=ownround::Judge(c,n,gunship,other,300);
    Expect(j.verdict==Verdict::stock && !Before(c,n,gunship,other),"another flight: stock");
    j=ownround::Judge(c,n,other,gunship,100);
    Expect(j.verdict==Verdict::stock,"another flight's round on the gunship: stock");
    j=ownround::Judge(c,n,bombOwner,gunship,100);
    Expect(j.verdict==Verdict::wingman && Before(c,n,bombOwner,gunship),"a bomb of the flight passes a wingman");
    j=ownround::Judge(c,n,bombOwner,wing,200);
    Expect(j.verdict==Verdict::wingman && Before(c,n,bombOwner,wing),"a bomb passes its own bomber");
    j=ownround::Judge(c,n,bombOwner,other,300);
    Expect(j.verdict==Verdict::stock,"a bomb on another flight: stock");
    c[1].bombs=false;
    j=ownround::Judge(c,n,bombOwner,gunship,100);
    Expect(j.verdict==Verdict::stock,"bombs gone: their owner's rounds stock");
    c[1].bombs=true;

    // Not a plugin jet's round: stock, whatever it hits (friendly fire as it is), and none on nothing.
    j=ownround::Judge(c,n,soldier,gunship,100);
    Expect(j.verdict==Verdict::stock,"a soldier's round on the gunship: stock");
    j=ownround::Judge(c,n,nullptr,gunship,100);
    Expect(j.verdict==Verdict::stock,"no owner: stock");
    j=ownround::Judge(c,n,gunship,soldier,0);
    Expect(j.verdict==Verdict::stock,"the gunship's round on a soldier: stock");
    j=ownround::Judge(c,0,gunship,gunship,100);
    Expect(j.verdict==Verdict::stock,"no jet flown: stock");

    // The body table: a count past kMaxBodies reads no further.
    ownround::Craft big{gunship,nullptr,1,false,true,ownround::kMaxBodies+5,{}};
    for(int i=0;i<ownround::kMaxBodies;++i)big.body[i]=static_cast<std::uint32_t>(1000+i);
    Expect(ownround::HasBody(big,1000+ownround::kMaxBodies-1) && !ownround::HasBody(big,7),"bodies: within the table");

    std::printf(failures ? "own_round_check: %d FAILED\n" : "own_round_check: all passed\n",failures);
    return failures ? 1 : 0;
}
