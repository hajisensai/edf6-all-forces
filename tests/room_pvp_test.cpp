// The room's PvP setup, the pure side (src/room_pvp_logic.h), offline: the attribute codecs for a member's pick and the
// host's settings, the start lock with its acknowledgement, and the protocol state the lock asks for.
// cmake --build build --target room_pvp_test && build\room_pvp_test.exe
#include "../src/room_pvp_logic.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {
using namespace crew::roompvp;
namespace pm=crew::pvp_match;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
bool SamePick(const pm::Pick& a,const pm::Pick& b){return a.faction==b.faction && a.role==b.role && a.unit==b.unit;}
std::string Id(char c,std::size_t n=64){return std::string(n,c);}
void SetId(char* out,const std::string& id){std::memset(out,0,65);std::memcpy(out,id.c_str(),id.size()>64 ? 64 : id.size());}

const pm::Pick kPicks[]={{0,0,pm::kNoUnit},{1,1,pm::kNoUnit},{3,0,pm::kNoUnit},{4,2,0},{4,2,7},{5,2,65534},{6,1,pm::kNoUnit},{6,2,300},{2,1,pm::kNoUnit}};

Settings PresetSettings(pm::Preset p,bool on) {
    Settings s;s.enabled=on;s.relations.FromPreset(p);return s;
}
Settings CustomSettings(bool on) {
    Settings s;s.enabled=on;s.relations.FromPreset(pm::Preset::aliensWar);
    s.relations.preset=static_cast<std::uint8_t>(pm::Preset::custom);
    s.relations.rel[0][4]=s.relations.rel[4][0]=pm::kNeutral;
    s.relations.rel[1][6]=s.relations.rel[6][1]=pm::kFriend;
    Expect(s.relations.Valid(),"fixture: the custom table is valid");
    return s;
}
Lock MakeLock(int count,const Settings& s,std::uint32_t serial,bool longIds=true) {
    Lock l;l.serial=serial;l.settings=s;l.count=count;
    for(int i=0;i<count;++i) {
        SetId(l.members[i].id,longIds ? Id(static_cast<char>('a'+i)) : std::string("m")+std::to_string(i));
        l.members[i].pick=kPicks[(i*3)%9];
    }
    return l;
}
std::string Replace(std::string text,const std::string& from,const std::string& to) {
    const auto at=text.find(from);
    if(at!=std::string::npos)text.replace(at,from.size(),to);
    else Expect(false,"fixture: the text to mutate is there");
    return text;
}

void Picks() {
    char text[64];
    for(const auto& pick:kPicks) {
        Expect(pm::PickValid(pick),"fixture: the pick is valid");
        EncodePick(pick,text,sizeof(text));
        pm::Pick got{9,9,9};
        Expect(DecodePick(text,&got) && SamePick(got,pick),"a pick round-trips");
        Expect(std::strlen(text)<kValueMost,"a pick fits an attribute");
    }
    EncodePick({4,2,7},text,sizeof(text));
    Expect(!std::strcmp(text,"3;4;2;7"),"a pick's text: version;faction;role;unit");
    EncodePick({0,0,pm::kNoUnit},text,sizeof(text));
    Expect(!std::strcmp(text,"3;0;0;65535"),"a soldier's pick has no unit");
    pm::Pick got{};
    const char* bad[]={
        "","3","3;0;0","3;0;0;65535;1","2;0;0;65535","4;0;0;65535","0;0;0;65535","x;0;0;65535","3;x;0;65535","3;0;x;65535","3;0;0;x",
        "3;0;0;","3;;0;65535","3;0;;65535",";0;0;65535","3;0;0;65536","3;16;0;65535","3;7;0;65535","3;0;3;65535","3;0;0;-1","3;-1;0;65535",
        " 3;0;0;65535","3;0;0;65535 ","3;0;0; 65535","3;0;0;0x10","3;0;0;65535;","3;+0;0;65535","3,0,0,65535",
        "3;4;0;65535",   // an alien faction as a soldier
        "3;4;2;65535",   // a unit role without a unit
        "3;0;0;5",       // a soldier with a unit
        "3;0;2;5",       // an EDF faction as a unit
        "3;0;1;5",       // a commander with a unit
        "3;0;0;00000000000000000065535",
    };
    for(const char* t:bad)Expect(!DecodePick(t,&got),"a malformed or invalid pick is rejected");
    Expect(!DecodePick(nullptr,&got),"no text is no pick");
    Expect(DecodePick("3;1;1;65535",&got) && got.faction==1 && got.role==1,"a commander of an EDF faction");
    Expect(DecodePick("3;6;1;65535",&got) && got.faction==6 && got.role==1,"a commander of an alien faction");
}

void SettingsCodec() {
    char text[kValueMost];
    for(pm::Preset p:{pm::Preset::freeForAll,pm::Preset::humansVsAliens,pm::Preset::edfCivilWar,pm::Preset::aliensWar})for(bool on:{false,true}) {
        const Settings s=PresetSettings(p,on);
        EncodeSettings(s,text,sizeof(text));
        Settings got;
        Expect(DecodeSettings(text,&got) && SameSettings(got,s) && got.enabled==on && got.relations.preset==static_cast<std::uint8_t>(p),"a preset's settings round-trip");
        Expect(std::strlen(text)<kValueMost,"settings fit an attribute");
        char want[32];std::snprintf(want,sizeof(want),"3;%d;%u;-",on ? 1 : 0,static_cast<unsigned>(p));
        Expect(!std::strcmp(text,want),"a preset's settings carry no table");
    }
    for(bool on:{false,true}) {
        const Settings s=CustomSettings(on);
        EncodeSettings(s,text,sizeof(text));
        Settings got;
        Expect(DecodeSettings(text,&got) && SameSettings(got,s) && got.relations.preset==static_cast<std::uint8_t>(pm::Preset::custom),"custom settings round-trip");
        Expect(got.relations.rel[0][4]==pm::kNeutral && got.relations.rel[1][6]==pm::kFriend && got.relations.rel[0][1]==pm::kFriend,"the custom table's pairs arrive");
        Expect(std::strlen(text)<kValueMost && std::strlen(text)>128,"custom settings carry the table in 128 hex digits");
        const std::string t=text;
        Expect(t.size()==std::string("3;1;4;").size()+128,"version;on;preset;128 digits");
    }
    EncodeSettings(PresetSettings(pm::Preset::humansVsAliens,true),text,sizeof(text));
    Settings got;
    const char* bad[]={"","3;1","3;1;1","3;1;1;-;","3;1;1;-;x","2;1;1;-","4;1;1;-","3;2;1;-","3;x;1;-","3;1;x;-","3;1;5;-","3;1;255;-","3;1;1;","3;1;1;--","3;1;1;x",
        "3;1;1;0000","3;1;4;-","3;1;3;00","3;-1;1;-","3;1;-1;-","3;1;1; -"," 3;1;1;-"};
    for(const char* t:bad)Expect(!DecodeSettings(t,&got),"malformed settings are rejected");
    Expect(!DecodeSettings(nullptr,&got),"no text is no settings");
    // a custom preset with a table that is not valid, or in lower case, or the wrong length
    EncodeSettings(CustomSettings(true),text,sizeof(text));
    std::string custom=text;
    Expect(DecodeSettings(custom.c_str(),&got),"fixture: the custom text decodes");
    std::string lower=custom;for(size_t i=6;i<lower.size();++i)if(lower[i]>='A' && lower[i]<='F')lower[i]=static_cast<char>(lower[i]+32);
    Expect(lower!=custom && !DecodeSettings(lower.c_str(),&got),"the hex digits are upper case");
    Expect(!DecodeSettings(custom.substr(0,custom.size()-1).c_str(),&got),"a table one digit short is rejected");
    Expect(!DecodeSettings((custom+"0").c_str(),&got),"a table one digit long is rejected");
    std::string nonhex=custom;nonhex[custom.size()-1]='G';Expect(!DecodeSettings(nonhex.c_str(),&got),"a non-hex digit is rejected");
    std::string asym=custom;   // flip a byte of the table so it is no longer symmetric
    asym[6+1]=asym[6+1]=='0' ? '1' : '0';
    Expect(!DecodeSettings(asym.c_str(),&got) || got.relations.Valid(),"a decoded table is always valid");
    Expect(!DecodeSettings(("3;1;0;"+custom.substr(6)).c_str(),&got),"a named preset does not take a table");
    EncodeSettings(PresetSettings(pm::Preset::edfCivilWar,true),text,sizeof(text));
    got=Settings{};got.enabled=false;
    Expect(DecodeSettings(text,&got) && got.enabled,"the result is the decoded one");
}

void LockCodec() {
    char text[kValueMost];
    for(int count:{1,2,5,8})for(int custom=0;custom<2;++custom) {
        const Lock l=MakeLock(count,custom ? CustomSettings(true) : PresetSettings(pm::Preset::edfCivilWar,true),4000000000u,true);
        std::memset(text,0,sizeof(text));
        Expect(EncodeLock(l,text,sizeof(text)),"a lock encodes");
        Expect(std::strlen(text)<=999,"a lock fits an attribute value");
        Lock got;
        Expect(DecodeLock(text,&got),"a lock decodes");
        Expect(got.serial==l.serial && got.count==l.count && SameSettings(got.settings,l.settings),"a lock's serial, count and settings round-trip");
        bool same=true;
        for(int i=0;i<count;++i)same=same && !std::strcmp(got.members[i].id,l.members[i].id) && SamePick(got.members[i].pick,l.members[i].pick);
        Expect(same,"a lock's members round-trip in order");
        Expect(SameMatch(got,l),"a decoded lock is the same match");
        Expect(LockHash(got)==LockHash(l) && LockHash(l)!=0,"a decoded lock has the same hash");
    }
    {   // the largest lock: eight 64-character ids with a custom table and the longest pick texts
        Lock l=MakeLock(8,CustomSettings(true),0xFFFFFFFFu,true);
        for(int i=0;i<8;++i)l.members[i].pick={5,2,static_cast<std::uint16_t>(65534-i)};
        Expect(EncodeLock(l,text,sizeof(text)) && std::strlen(text)<=999,"the largest lock fits 999 bytes");
        Lock got;Expect(DecodeLock(text,&got) && got.count==8,"the largest lock decodes");
    }
    {   // a buffer too small
        const Lock l=MakeLock(8,CustomSettings(true),9,true);
        char small[400];
        Expect(!EncodeLock(l,small,sizeof(small)),"a buffer too small refuses");
        Expect(!EncodeLock(l,nullptr,10) && !EncodeLock(l,small,1),"no buffer refuses");
        Lock bad=l;bad.count=9;Expect(!EncodeLock(bad,text,sizeof(text)),"more than eight members refuses");
        bad.count=-1;Expect(!EncodeLock(bad,text,sizeof(text)),"a negative count refuses");
    }
    // the text of a lock: version;serial;on;preset;relations;count;members
    Lock one=MakeLock(2,PresetSettings(pm::Preset::humansVsAliens,true),17,false);
    SetId(one.members[0].id,"hostid");SetId(one.members[1].id,"guestid");
    one.members[0].pick={0,0,pm::kNoUnit};one.members[1].pick={4,2,9};
    Expect(EncodeLock(one,text,sizeof(text)),"fixture: a short lock");
    Expect(!std::strcmp(text,"3;17;1;1;-;2;hostid,0,0,65535;guestid,4,2,9"),"a lock's text");
    const std::string good=text;
    Lock got;
    Expect(DecodeLock(good.c_str(),&got),"fixture: the short lock decodes");
    const std::string bad[]={
        "",
        Replace(good,"3;17","2;17"),                          // wrong version
        Replace(good,"3;17","4;17"),
        good+";extra,0,0,65535",                             // an extra field
        good+";",
        Replace(good,";2;hostid",";3;hostid"),                // the count says more than there are
        Replace(good,";2;hostid",";1;hostid"),                // the count says fewer
        Replace(good,";2;hostid",";0;hostid"),
        Replace(good,";2;hostid",";9;hostid"),
        Replace(good,"3;17;","3;0;"),                         // serial 0 is no lock
        Replace(good,"3;17;","3;x;"),
        Replace(good,"3;17;","3;4294967296;"),
        Replace(good,"3;17;","3;-1;"),
        Replace(good,"3;17;1;","3;17;2;"),                    // on is 0 or 1
        Replace(good,"3;17;1;1;","3;17;1;9;"),                // preset out of range
        Replace(good,"3;17;1;1;-","3;17;1;1;"),               // the preset without its '-'
        Replace(good,"3;17;1;1;-","3;17;1;4;-"),              // custom without a table
        Replace(good,"hostid,0,0,65535","hostid,0,0,x"),      // a bad number
        Replace(good,"hostid,0,0,65535","hostid,0,x,65535"),
        Replace(good,"hostid,0,0,65535","hostid,x,0,65535"),
        Replace(good,"hostid,0,0,65535","hostid,0,0"),        // a short member
        Replace(good,"hostid,0,0,65535","hostid,0,0,65535,1"),   // a long member
        Replace(good,"hostid,0,0,65535","hostid,0,0,65536"),
        Replace(good,"hostid,0,0,65535","hostid,0,0,5"),      // an invalid pick: a soldier with a unit
        Replace(good,"guestid,4,2,9","guestid,4,0,65535"),    // an alien soldier
        Replace(good,"guestid,4,2,9","guestid,4,2,65535"),    // a unit role without a unit
        Replace(good,"guestid,4,2,9","guestid,9,0,65535"),    // no such faction
        Replace(good,"guestid,","hostid,"),                   // a duplicate member id
        Replace(good,"hostid,0","hostid;0"),                  // a ';' where a ',' goes
        Replace(good,"hostid,","host,id,"),                   // a ',' in an id
        Replace(good,"hostid,","host;id,"),                   // a ';' in an id
        Replace(good,"hostid,",","),                          // no id
        Replace(good,"hostid,","host id,"),                   // a space in an id
        Replace(good,"hostid,",std::string("host\x7F")+"id,"),
        Replace(good,"hostid,",std::string("host\x80")+"id,"),
        Replace(good,"hostid,",Id('z',65)+","),               // an id of 65 characters
    };
    for(const auto& t:bad)Expect(!DecodeLock(t.c_str(),&got),"a malformed lock is rejected");
    Expect(!DecodeLock(nullptr,&got),"no text is no lock");
    Expect(DecodeLock(Replace(good,"hostid,","h,").c_str(),&got) && !std::strcmp(got.members[0].id,"h"),"a one-character id is fine");
    Expect(DecodeLock(Replace(good,"hostid,",Id('z',64)+",").c_str(),&got),"an id of 64 characters is fine");
    {   // ids that EncodeLock does not itself guard
        Lock odd=one;SetId(odd.members[0].id,"has;semi");
        Expect(EncodeLock(odd,text,sizeof(text)) && !DecodeLock(text,&got),"an id with ';' does not decode");
        SetId(odd.members[0].id,"has,comma");
        Expect(EncodeLock(odd,text,sizeof(text)) && !DecodeLock(text,&got),"an id with ',' does not decode");
        odd=one;SetId(odd.members[1].id,"hostid");
        Expect(EncodeLock(odd,text,sizeof(text)) && !DecodeLock(text,&got),"a duplicate id does not decode");
        odd=one;odd.members[1].pick={4,0,pm::kNoUnit};
        Expect(EncodeLock(odd,text,sizeof(text)) && !DecodeLock(text,&got),"an invalid pick does not decode");
    }
    {   // a decoded failure leaves the result alone
        Lock keep=MakeLock(1,PresetSettings(pm::Preset::freeForAll,false),5,false);
        Lock out=keep;
        Expect(!DecodeLock("3;1;1;1;-;2;a,0,0,65535",&out) && out.serial==5 && out.count==1,"a rejected lock leaves the result alone");
    }
}

void Building() {
    Member members[3];
    SetId(members[0].id,"host");members[0].host=true;members[0].hasPick=true;members[0].pick={1,1,pm::kNoUnit};
    SetId(members[1].id,"quiet");members[1].hasPick=false;members[1].pick={4,2,3};   // never published: its pick is not read
    SetId(members[2].id,"wrong");members[2].hasPick=true;members[2].pick={4,0,pm::kNoUnit};   // published, not usable
    const Settings s=PresetSettings(pm::Preset::humansVsAliens,true);
    const Lock l=BuildLock(s,members,3);
    Expect(l.count==3 && l.serial==0 && SameSettings(l.settings,s),"BuildLock keeps the settings and leaves the serial to the caller");
    Expect(!std::strcmp(l.members[0].id,"host") && SamePick(l.members[0].pick,{1,1,pm::kNoUnit}),"a member's usable pick is kept");
    Expect(SamePick(l.members[1].pick,DefaultPick()),"a member without a pick plays the default");
    Expect(SamePick(l.members[2].pick,DefaultPick()),"a member with an unusable pick plays the default");
    Expect(SamePick(DefaultPick(),{0,static_cast<std::uint8_t>(pm::Role::soldier),pm::kNoUnit}) && pm::PickValid(DefaultPick()),"the default is the first EDF faction's soldier");
    for(int i=0;i<l.count;++i)Expect(pm::PickValid(l.members[i].pick),"every locked pick is valid");
    Expect(BuildLock(s,members,0).count==0,"no members, no lock members");
    Member many[12];for(int i=0;i<12;++i){SetId(many[i].id,"m"+std::to_string(i));many[i].hasPick=true;many[i].pick={0,0,pm::kNoUnit};}
    Expect(BuildLock(s,many,12).count==static_cast<int>(kMaxMembers),"a lock holds at most eight members");
    char text[kValueMost];
    Lock withSerial=l;withSerial.serial=3;
    Expect(EncodeLock(withSerial,text,sizeof(text)),"a built lock encodes");
    Lock got;Expect(DecodeLock(text,&got),"a built lock, once numbered, decodes");

    // SameMatch ignores the serial and nothing else
    Lock a=l,b=l;a.serial=1;b.serial=99;
    Expect(SameMatch(a,b),"the same match under another serial");
    b=a;b.members[1].pick={1,0,pm::kNoUnit};Expect(!SameMatch(a,b),"another pick is another match");
    b=a;b.members[1].pick.unit=4;Expect(!SameMatch(a,b),"another unit is another match");
    b=a;b.members[1].pick.role=1;Expect(!SameMatch(a,b),"another role is another match");
    b=a;b.members[1].pick.faction=3;Expect(!SameMatch(a,b),"another faction is another match");
    b=a;SetId(b.members[2].id,"other");Expect(!SameMatch(a,b),"another member is another match");
    b=a;b.count=2;Expect(!SameMatch(a,b),"a member fewer is another match");
    b=a;b.settings.enabled=false;Expect(!SameMatch(a,b),"another mode is another match");
    b=a;b.settings=PresetSettings(pm::Preset::aliensWar,true);Expect(!SameMatch(a,b),"another preset is another match");
    b=a;b.settings=CustomSettings(true);Expect(!SameMatch(a,b),"a custom table is another match");
    Lock c1=a,c2=a;c1.settings=CustomSettings(true);c2.settings=CustomSettings(true);
    Expect(SameMatch(c1,c2),"the same custom table is the same match");
    c2.settings.relations.rel[2][3]=c2.settings.relations.rel[3][2]=pm::kEnemy;
    Expect(!SameMatch(c1,c2),"another custom table is another match");
}

void Acks() {
    Lock l=MakeLock(3,PresetSettings(pm::Preset::humansVsAliens,true),7,false);
    char ack[64];EncodeAck(l,ack,sizeof(ack));
    Expect(AckMatches(ack,l),"an acknowledgement matches its lock");
    Expect(std::strlen(ack)==std::strlen("3;7;00000000") && !std::strncmp(ack,"3;7;",4),"an acknowledgement is version;serial;hash");
    Lock other=l;other.serial=8;
    Expect(!AckMatches(ack,other),"another serial does not match");
    other=l;other.members[1].pick={1,0,pm::kNoUnit};
    Expect(!AckMatches(ack,other),"the same serial with other text does not match");
    other=l;other.settings=PresetSettings(pm::Preset::freeForAll,true);
    Expect(!AckMatches(ack,other),"the same serial with other settings does not match");
    Expect(!AckMatches(nullptr,l) && !AckMatches("",l) && !AckMatches("3;7",l) && !AckMatches("3;7;",l) && !AckMatches("2;7;00000000",l),"a malformed acknowledgement does not match");
    std::string lowered=ack;for(auto& ch:lowered)if(ch>='A' && ch<='F')ch=static_cast<char>(ch+32);
    Expect(lowered==ack || !AckMatches(lowered.c_str(),l),"the hash is upper case hex");
    Expect(!AckMatches((std::string(ack)+" ").c_str(),l),"trailing text does not match");
    Lock none=l;none.serial=0;char noneAck[64];EncodeAck(none,noneAck,sizeof(noneAck));
    Expect(!AckMatches(noneAck,none),"a lock without a serial is never acknowledged");

    Member members[3];
    SetId(members[0].id,l.members[0].id);members[0].host=true;
    SetId(members[1].id,l.members[1].id);
    SetId(members[2].id,l.members[2].id);
    const char* acks[3]={nullptr,ack,ack};
    Expect(AllAcknowledged(l,members,acks,3),"the host's own entry needs no acknowledgement");
    acks[0]="junk";Expect(AllAcknowledged(l,members,acks,3),"the host's own entry is not read at all");
    acks[0]=nullptr;
    acks[2]=nullptr;Expect(!AllAcknowledged(l,members,acks,3),"a member who has not acknowledged holds the start");
    acks[2]="3;7;DEADBEEF";Expect(!AllAcknowledged(l,members,acks,3),"a member who acknowledged other text holds the start");
    Lock old=l;old.serial=6;char oldAck[64];EncodeAck(old,oldAck,sizeof(oldAck));
    acks[2]=oldAck;Expect(!AllAcknowledged(l,members,acks,3),"an acknowledgement of the last serial holds the start");
    acks[2]=ack;Expect(AllAcknowledged(l,members,acks,3),"every other member acknowledged");
    Expect(!AllAcknowledged(l,members,acks,2),"the lock lists a member the room no longer has");
    Expect(!AllAcknowledged(l,members,acks,0),"no members are not acknowledged");
    Lock unlocked=l;unlocked.serial=0;Expect(!AllAcknowledged(unlocked,members,acks,3),"no lock is not acknowledged");
    Member solo[1];SetId(solo[0].id,l.members[0].id);solo[0].host=true;
    Lock alone=MakeLock(1,PresetSettings(pm::Preset::freeForAll,true),2,false);
    const char* noAcks[1]={nullptr};
    Expect(AllAcknowledged(alone,solo,noAcks,1),"the host alone has everyone's acknowledgement");
    Member twoGuests[2];SetId(twoGuests[0].id,"x");SetId(twoGuests[1].id,"y");
    Lock two=MakeLock(2,PresetSettings(pm::Preset::freeForAll,true),2,false);
    const char* none2[2]={nullptr,nullptr};
    Expect(!AllAcknowledged(two,twoGuests,none2,2),"without a host every member must acknowledge");
}

void MatchStates() {
    for(int custom=0;custom<2;++custom)for(bool on:{false,true}) {
        const Lock l=MakeLock(5,custom ? CustomSettings(on) : PresetSettings(pm::Preset::edfCivilWar,on),3,true);
        const auto state=MatchState(l);
        Expect(state.enabled==on && state.count==5,"the state has the lock's mode and members");
        Expect(state.relations.Same(l.settings.relations) && state.relations.Valid(),"the state has the lock's relations");
        for(int i=0;i<5;++i) {
            Expect(pvp_protocol::ValidEntry(state.entries[i]) && pvp_protocol::ValidPuid(state.entries[i].puid),"every entry is valid");
            Expect(!std::strcmp(state.entries[i].puid,l.members[i].id),"the entry is the lock's member");
            Expect(SamePick(state.entries[i].Pick(),l.members[i].pick),"the entry is the lock's pick");
            Expect(state.entries[i].team==pm::FactionTeam(l.members[i].pick.faction),"the entry's team is its faction's");
        }
    }
    Lock built;{
        Member members[2];SetId(members[0].id,"h");members[0].host=true;SetId(members[1].id,"g");
        built=BuildLock(PresetSettings(pm::Preset::humansVsAliens,true),members,2);built.serial=1;
    }
    const auto state=MatchState(built);
    Expect(state.count==2 && pvp_protocol::ValidEntry(state.entries[0]) && pvp_protocol::ValidEntry(state.entries[1]) &&
           state.entries[1].team==2,"a member without a pick is the default's team");
    Expect(MatchState(MakeLock(8,CustomSettings(true),1,true)).count==8,"eight members");
}
}  // namespace

int main() {
    Picks();SettingsCodec();LockCodec();Building();Acks();MatchStates();
    if(failures){std::printf("room_pvp_test: %d of %d checks FAILED\n",failures,checks);return 1;}
    std::printf("room_pvp_test: %d checks passed\n",checks);return 0;
}
