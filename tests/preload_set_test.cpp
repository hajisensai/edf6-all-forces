// The mission preload's set (src/preload_set_logic.h): what a call weapon reaches, the room's set, what each machine
// takes at the sync, the set's text in the host-settings word and PreloadMode.
#include "preload_set_logic.h"
#include <cstdio>
#include <cstring>

namespace {
int failures=0;
void Expect(bool ok,const char* what) {
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
using namespace crew::preload;

void CallRows() {
    Set s;
    AddCallItems(L"EDF6VC_CALL_FLY_BLAST_CARRIER",s);
    Expect(s.Has(Body(body::blastCarrier)) && s.Has(Body(body::blast)) && s.Has(Body(body::drone)) && s.Count()==3,
           "a blast carrier brings its blast drones and the gun drone");
    s=Set{};
    AddCallItems(L"EDF6VC_CALL_DREADNOUGHT",s);
    Expect(s.Has(Body(body::dnFighter)) && s.Has(Body(body::dnDefender)) && s.Has(Body(body::dnBomber)) &&
           s.Has(Body(body::dnInterceptor)) && s.Has(Item::dnRounds) && s.Count()==5,"the Dreadnought: its hangar and rounds");
    s=Set{};
    AddCallItems(L"EDF6VC_CALL_XM_LARGE_BOMBER_CORE",s);
    Expect(s.Has(Body(body::xmBomber)) && s.Count()==1,"an X-Morph bomber");
    s=Set{};
    for(const wchar_t* none:{L"EDF6VC_CALL_GROUND_TANK",L"EDF6VC_CALL_PJET_FIGHTER",L"FLY_FIGHTER",L"",L"EDF6VC_CALL_",
                             L"EDF6VC_CALL_FLY_FIGHTERX",L"edf6vc_call_fly_fighter",L"SOMETHING_EDF6VC_CALL_FLY_FIGHTER"})
        AddCallItems(none,s);
    Expect(s.Empty(),"other rows (ground, player jets, not a call) reach nothing of the preload");
}

void Sets() {
    constexpr Set always=Always(),all=All();
    Set both=always;both.Join(all);
    Expect(both==all,"what every mission takes is part of everything");
    Expect(always.Has(Item::vehicleRounds) && always.Has(Item::chute) && always.Has(Soldier(0)) && always.Has(Soldier(kSoldierItems-1)),
           "every mission: the vehicle rounds, the canopy, every soldier kind");
    Expect(!always.Has(Body(body::strike)) && !always.Has(Item::sub) && !always.Has(Item::dnRounds) && !always.Has(Catch(0)),
           "no call's own content in every mission");
    Expect(all.Count()==kBodyItems+kCatchItems+10+kSoldierItems,"everything: every body, catch file, item and soldier");
    Expect(!all.Has(Item::end) && !Set{}.Has(Item::end),"nothing past the end");
    Set s;s.Add(Item::end);
    Expect(s.Empty(),"an out-of-range item is not added");

    Set needed;needed.Add(Item::sub);
    Expect(ByMode(ModeOf(0),needed)==needed,"PreloadMode=0: the needed set");
    Expect(ByMode(ModeOf(1),needed)==all,"PreloadMode=1: everything (the old way)");
    Expect(ByMode(ModeOf(2),needed).Empty(),"PreloadMode=2: nothing");
    Expect(ModeOf(7)==Mode::needed && ModeOf(-1)==Mode::needed,"another value: the needed set");
}

void Room() {
    Set host;host.Add(Item::sub);
    Set members[3];
    members[0].Add(Body(body::xmBomber));
    members[2].Add(Item::dnRounds);members[2].Add(Item::sub);
    const Set room=RoomSet(host,members,3);
    Expect(room.Has(Item::sub) && room.Has(Body(body::xmBomber)) && room.Has(Item::dnRounds) && room.Count()==3,
           "the room's set: the host's and every member's");
    Expect(RoomSet(host,nullptr,0)==host,"alone: the host's own");

    Set local;local.Add(Item::laser);
    Decision d=AtSync(false,false,false,room,local);
    Expect(d.source==Source::local && d.set==local,"no room: this machine's");
    d=AtSync(true,true,true,room,local);
    Expect(d.source==Source::local && d.set==local,"the host: its own decision (the caller passes its said room set)");
    d=AtSync(true,false,true,room,local);
    Expect(d.source==Source::host && d.set==room,"a guest: the host's set, not its own");
    d=AtSync(true,false,false,room,local);
    Expect(d.source==Source::hostSilent && d.set.Empty(),"a guest whose host said none: nothing of the plugin's");
}

void Text() {
    Set s;s.Add(Body(0));s.Add(Soldier(kSoldierItems-1));s.Add(Item::ultimate);
    char text[kTextSize];
    Expect(Format(s,text,sizeof(text)) && std::strlen(text)==32,"32 hex digits");
    Set back;
    Expect(Parse(text,back) && back==s,"round trip");
    Expect(Format(Set{},text,sizeof(text)) && !std::strcmp(text,"00000000000000000000000000000000"),"the empty set");
    Expect(Parse("00000000000000010000000000000001",back) && back.Has(Body(0)) && back.Has(Soldier(0)) && back.Count()==2,
           "the high word first");
    char small[32];
    Expect(!Format(s,small,sizeof(small)),"no room for the text: not written");
    for(const char* bad:{"","0000000000000000000000000000000","000000000000000000000000000000000","0000000000000000000000000000000a",
                         "000000000000000000000000000000G0","0000000000000000 000000000000000","-0000000000000000000000000000000"})
        Expect(!Parse(bad,back),"anything but 32 upper-case hex digits is no set");
}
}  // namespace

int main() {
    CallRows();
    Sets();
    Room();
    Text();
    if(failures){std::printf("%d failures\n",failures);return 1;}
    std::printf("preload_set: all passed\n");
    return 0;
}
