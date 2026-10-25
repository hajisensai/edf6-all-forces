// EDF6Coop's member attributes as every module reads them (src/coop_attr.h), over a fake v1 table, and the host's
// settings / the room's builds (src/host_settings_logic.h), offline:
//   - a table of another version, or one that ends before snapshot, is not used;
//   - a key is published only when its value changed, a refusal's reason told once, all of it again after Forget;
//   - a member's value by its id, the host's, a mission player's through the resolver; every member / every mission
//     player passing a test, the first failing one named;
//   - the word: formatted and parsed strictly; a guest plays by the host's values, its own ini without a host word or
//     outside a room; the builds counted (a member without the word, one with fewer features), the 408 gunner seat
//     open only when every member has it.
// cmake --build build --target coop_attr_test && build\coop_attr_test.exe
#include "coop_attr.h"
#include "host_settings_logic.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
using namespace crew;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

// --- A fake EDF6Coop: members, each with a value of one key (or none), a publish that may refuse.
struct Member { std::string id; bool host=false,self=false,has=false; std::string value; };
std::vector<Member> members;
std::string publishedKey,publishedValue;
int publishes=0;
bool refuse=false;
std::uint32_t refuseWhy=EDF6COOP_MA_VALUE_BUDGET;
std::uint32_t EDF6COOP_MA_CALL Publish(const char* key,const char* value) {
    ++publishes;
    if(refuse)return 0;
    publishedKey=key;publishedValue=value;
    return 1;
}
std::uint32_t EDF6COOP_MA_CALL Clear(const char*) { return 1; }
std::uint32_t EDF6COOP_MA_CALL RoomNow(EDF6CoopRoomState* out) { out->inRoom=1;return 1; }
std::uint32_t EDF6COOP_MA_CALL Snapshot(const char* key,EDF6CoopMemberValueFn each,void* context,std::uint64_t* gen) {
    if(!key || !each)return 0;
    if(gen)*gen=1;
    std::uint32_t ordinal=0;
    for(const Member& m:members) {
        EDF6CoopMemberValue v{m.id.c_str(),ordinal++,m.host ? 1u : 0u,m.self ? 1u : 0u,m.has ? 1u : 0u,m.has ? m.value.c_str() : "",
                              static_cast<std::uint32_t>(m.value.size())};
        each(context,&v);
    }
    return 1;
}
std::uint32_t EDF6COOP_MA_CALL LastError() { return refuseWhy; }
EDF6CoopMemberAttributeApi Table(std::uint32_t version=EDF6COOP_MEMBER_ATTRIBUTE_VERSION,bool lastError=true) {
    EDF6CoopMemberAttributeApi t{};
    t.size=lastError ? static_cast<std::uint32_t>(sizeof(t)) : static_cast<std::uint32_t>(offsetof(EDF6CoopMemberAttributeApi,lastError));
    t.version=version;t.publish=&Publish;t.clear=&Clear;t.room=&RoomNow;t.snapshot=&Snapshot;
    if(lastError)t.lastError=&LastError;
    return t;
}
// Mission index i -> "P<i>" (none past 3).
std::uint32_t EDF6COOP_CALL Resolve(std::int32_t index,EDF6CoopPeer* out) {
    if(index<0 || index>3)return 0;
    std::snprintf(out->id,sizeof(out->id),"P%d",index);
    return 1;
}
bool IsOne(const char* v,void*) { return v && std::strcmp(v,"1")==0; }

void Usable() {
    const auto t=Table();
    Expect(coopattr::Usable(&t)==&t,"a v1 table is used");
    const auto other=Table(2);
    Expect(!coopattr::Usable(&other),"another version's table is not");
    auto shortT=Table();
    shortT.size=static_cast<std::uint32_t>(offsetof(EDF6CoopMemberAttributeApi,snapshot));
    Expect(!coopattr::Usable(&shortT),"a table that ends before snapshot is not");
    Expect(!coopattr::Usable(nullptr),"none is not");
    coopattr::Beat beat;
    Expect(beat.Due(1000) && !beat.Due(1400) && beat.Due(1500) && !beat.Due(1999),"the 500 ms beat");
}

void Publishing() {
    const auto t=Table();
    coopattr::Publisher p{"EDF6X_TEST"};
    publishes=0;refuse=false;
    Expect(p.Publish(&t,"a")==coopattr::Publisher::Result::published && publishedValue=="a","the first value published");
    Expect(p.Publish(&t,"a")==coopattr::Publisher::Result::same && publishes==1,"the same value: not again");
    Expect(p.Publish(&t,"b")==coopattr::Publisher::Result::published && publishes==2,"a changed value: published");
    refuse=true;
    std::uint32_t why=0;
    Expect(p.Publish(&t,"c",&why)==coopattr::Publisher::Result::refused && why==EDF6COOP_MA_VALUE_BUDGET,"a refusal and its reason");
    Expect(p.Publish(&t,"c")==coopattr::Publisher::Result::refusedAgain,"the same refusal: not told again");
    refuse=false;
    Expect(p.Publish(&t,"c")==coopattr::Publisher::Result::published,"accepted later");
    p.Forget();
    Expect(p.Publish(&t,"c")==coopattr::Publisher::Result::published && publishes==6,"after Forget (another EDF6Coop): published again");
    const auto noError=Table(EDF6COOP_MEMBER_ATTRIBUTE_VERSION,false);
    coopattr::Publisher q{"EDF6X_TEST"};
    refuse=true;why=7;
    Expect(q.Publish(&noError,"x",&why)==coopattr::Publisher::Result::refused && why==0,"an older table without lastError: reason 0");
    refuse=false;
}

void Reading() {
    const auto t=Table();
    members={{"P0",true,false,true,"1"},{"P1",false,true,true,"1"},{"P2",false,false,false,""},{"P3",false,false,true,"2"}};
    coopattr::Value v;
    Expect(coopattr::MemberValue(&t,"EDF6X_K","P3",v) && v.found && v.has && !std::strcmp(v.text,"2"),"a member's value by its id");
    Expect(coopattr::MemberValue(&t,"EDF6X_K","P2",v) && v.found && !v.has,"a member without a value");
    Expect(coopattr::MemberValue(&t,"EDF6X_K","nobody",v) && !v.found,"a member not in the room");
    Expect(coopattr::HostValue(&t,"EDF6X_K",v) && v.found && v.isHost && !std::strcmp(v.id,"P0"),"the host's");
    char why[200]="";
    Expect(coopattr::MissionPlayerValue(&t,&Resolve,"EDF6X_K",1,v,why,sizeof(why)) && v.isSelf,"a mission player through the resolver");
    Expect(!coopattr::MissionPlayerValue(&t,&Resolve,"EDF6X_K",9,v,why,sizeof(why)) && std::strstr(why,"no member"),"an index EDF6Coop does not know");
    Expect(!coopattr::MissionPlayerValue(&t,nullptr,"EDF6X_K",0,v,why,sizeof(why)) && std::strstr(why,"older EDF6Coop"),"no resolver");
    Expect(coopattr::EveryMissionPlayer(&t,&Resolve,"EDF6X_K",2,&IsOne,nullptr,why,sizeof(why)),"mission players 1-2 all say 1");
    Expect(!coopattr::EveryMissionPlayer(&t,&Resolve,"EDF6X_K",3,&IsOne,nullptr,why,sizeof(why)) && std::strstr(why,"player 3") &&
           std::strstr(why,"older All Forces"),"player 3 says nothing: named");
    members[2].has=true;members[2].value="1";
    Expect(!coopattr::EveryMissionPlayer(&t,&Resolve,"EDF6X_K",4,&IsOne,nullptr,why,sizeof(why)) && std::strstr(why,"says otherwise"),
           "player 4 says another value: named");
    Expect(!coopattr::EveryMember(&t,"EDF6X_K",&IsOne,nullptr,why,sizeof(why)) && std::strstr(why,"1 of 4") && std::strstr(why,"P3"),
           "every member: the first failing one named");
    members[3].value="1";
    Expect(coopattr::EveryMember(&t,"EDF6X_K",&IsOne,nullptr,why,sizeof(why)),"every member says 1");
}

void WordAndRoom() {
    using namespace crew::hostcfg;
    hostcfg::Word w;
    strcpy_s(w.version,"0.8.0");w.features=kFeatures;w.vanillaRecruit=true;w.npcFreeSquadCover=false;
    char text[kWordMost];
    Expect(FormatWord(w,text,sizeof(text)) && !std::strcmp(text,"1;v=0.8.0;f=1FF;r=1;c=0"),"the word");
    hostcfg::Word back;
    Expect(ParseWord(text,back) && !std::strcmp(back.version,"0.8.0") && back.features==kFeatures && back.vanillaRecruit &&
           !back.npcFreeSquadCover,"parsed back");
    for(const char* bad:{"","2;v=1;f=1;r=0;c=0","1;v=;f=1;r=0;c=0","1;v=1;f=G;r=0;c=0","1;v=1;f=1;r=2;c=0","1;v=1;f=1;r=0;c=0;x=1",
                         "1;v=1;f=123456789;r=0;c=0","1;v=1;r=0;f=1;c=0"})
        Expect(!ParseWord(bad,back),"another version's word is no word");
    strcpy_s(w.version,"a;b");
    Expect(!FormatWord(w,text,sizeof(text)),"a version that would break the word is not sent");

    hostcfg::Word host;host.vanillaRecruit=true;host.npcFreeSquadCover=true;
    Effective e=Decide(true,false,true,host,false,false);
    Expect(e.source==Source::host && e.vanillaRecruit && e.npcFreeSquadCover,"a guest plays by the host's values");
    e=Decide(true,true,true,host,false,false);
    Expect(e.source==Source::local && !e.vanillaRecruit,"the host by its own ini");
    e=Decide(true,false,false,host,true,false);
    Expect(e.source==Source::localHostSilent && e.vanillaRecruit && !e.npcFreeSquadCover,"a silent host: this machine's ini, said");
    e=Decide(false,false,true,host,false,true);
    Expect(e.source==Source::local && e.npcFreeSquadCover,"no room: this machine's ini");

    Builds b;
    hostcfg::Word older;strcpy_s(older.version,"0.8.0");older.features=kFeatures&~kFeatBikeGunner;
    hostcfg::Word same;strcpy_s(same.version,"0.8.0");same.features=kFeatures;
    Count(b,true,true,&same,"0.8.0");
    Count(b,false,true,&same,"0.8.0");
    Expect(b.members==2 && !b.silent && !b.differing && !b.missing,"the same build everywhere");
    Count(b,false,true,&older,"0.8.0");
    Count(b,false,false,nullptr,"0.8.0");
    Expect(b.members==4 && b.silent==1 && b.differing==1 && (b.missing&kFeatBikeGunner) && (b.missing&kFeatHostSettings),
           "one older (no word: everything may differ), one without the gunner");
    Expect(EveryoneHas(kFeatures,kFeatBikeGunner) && !EveryoneHas(older.features,kFeatBikeGunner),"the gunner seat: every member must have it");
    // 2026-10-21: a build whose 408 data still holds the chassis in the air (gun keyframed) is named, nothing gated (data).
    Builds h;hostcfg::Word hover;strcpy_s(hover.version,"0.8.0");hover.features=kFeatures&~kFeatBikeGrounded;
    Count(h,true,true,&same,"0.8.0");Count(h,false,true,&hover,"0.8.0");
    Expect(h.members==2 && h.differing==1 && h.missing==kFeatBikeGrounded && (kFeatures&kFeatBikeGrounded),
           "a member whose 408 still hovers: named in the notice");
    // 2026-10-25: a build that shipped the keyframed gun / the driven sidecar wheel / the bench driver is named too.
    Builds r;hostcfg::Word ride;strcpy_s(ride.version,"0.8.0");ride.features=kFeatures&~kFeatBikeRide;
    Count(r,true,true,&same,"0.8.0");Count(r,false,true,&ride,"0.8.0");
    Expect(r.members==2 && r.differing==1 && r.missing==kFeatBikeRide && (kFeatures&kFeatBikeRide),
           "a member whose shipped 408 still creeps (pinned, sidecar wheel driven): named in the notice");
    // 2026-10-21: a member with the gunner seat but not its built gun (the #219 / #234-first builds) closes the seat.
    Expect(!BikeGunnerAtSync(true,true,0,kFeatures&~kFeatBikeGunMount) && BikeGunnerAtSync(true,true,0,kFeatures),
           "the gunner seat needs every member's built gun as well");
    Expect(std::strlen(kKey)<=EDF6COOP_MEMBER_ATTRIBUTE_MAX_KEY && !std::strncmp(kKey,EDF6COOP_MEMBER_ATTRIBUTE_KEY_PREFIX,6),"the key's form");
}
// The 408's gunner seat over missions (2026-10-20 review): a closed seat of an online mission does not follow into an
// offline one, whether the player left the room first (the room / HQ frame) or went straight to an offline mission.
void GunnerSeat() {
    using namespace crew::hostcfg;
    bool seat=kBikeGunnerLocal;
    Expect(seat,"this machine's own: open");
    seat=BikeGunnerAtSync(true,true,1,kFeatures);
    Expect(!seat,"an online mission with an older member: closed");
    Expect(!BikeGunnerAtSync(true,true,0,kFeatures&~kFeatBikeGunner) && !BikeGunnerAtSync(true,false,0,kFeatures),
           "a member without it, or the room not read: closed");
    Expect(BikeGunnerAtSync(true,true,0,kFeatures) && BikeGunnerAtSync(false,false,3,0),"every member has it, or offline: open");
    Expect(!BikeGunnerOutOfMission(true,seat),"still in the room: the last sync's say kept");
    Expect(BikeGunnerOutOfMission(false,seat),"the room left: this machine's again");
    seat=BikeGunnerAtSync(true,true,1,kFeatures);
    seat=BikeGunnerOffline();
    Expect(seat,"an offline mission straight after a closed online one: open");
    Expect(BikeGunnerOutOfMission(true,true) && BikeGunnerOutOfMission(false,true),"an open seat stays open");
}
}  // namespace

// The room protocol's EDF6Coop: 2.7.0 and later (coop_attr.h kRoomCoop*).
void RoomVersion() {
    using crew::coopattr::CoopVersion;
    int a=-1,b=-1,c=-1;
    Expect(crew::coopattr::RoomCoopVersion("EDF6COOP_VERSION=2.7.0",&a,&b,&c)==CoopVersion::ok && a==2 && b==7 && c==0,"2.7.0 is enough");
    Expect(crew::coopattr::RoomCoopVersion("EDF6COOP_VERSION=2.10.0",nullptr,nullptr,nullptr)==CoopVersion::ok,"2.10.0 is later than 2.7.0");
    Expect(crew::coopattr::RoomCoopVersion("EDF6COOP_VERSION=3.0.0",nullptr,nullptr,nullptr)==CoopVersion::ok,"3.0.0 too");
    Expect(crew::coopattr::RoomCoopVersion("EDF6COOP_VERSION=2.6.1",&a,&b,&c)==CoopVersion::old && a==2 && b==6 && c==1,"2.6.1 is too old, read back");
    Expect(crew::coopattr::RoomCoopVersion("EDF6COOP_VERSION=2.5.0",nullptr,nullptr,nullptr)==CoopVersion::old,"2.5.0 too old");
    Expect(crew::coopattr::RoomCoopVersion(nullptr,nullptr,nullptr,nullptr)==CoopVersion::unreadable,"no marker: unreadable");
    Expect(crew::coopattr::RoomCoopVersion("EDF6COOP_VERSION=2.7",nullptr,nullptr,nullptr)==CoopVersion::unreadable,"another shape: unreadable");
}

int main() {
    Usable();
    RoomVersion();
    Publishing();
    Reading();
    WordAndRoom();
    GunnerSeat();
    std::printf("coop_attr_test: %d/%d checks passed\n",checks-failures,checks);
    return failures ? 1 : 0;
}
