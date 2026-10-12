// The room pre-sync's logic (src/room_sync_logic.h) offline, against a fake EDF6Coop member attribute table
// (src/coop_member_attribute_api.h) that keeps every member's published values the way Coop's lobby copy does:
//   - every soldier kind x leader x colour form and every body's loadouts survive name -> VariantRef -> name and
//     token -> VariantRef -> token; names and tokens that are not exactly what the naming writes are refused (paths,
//     lower case, stock-only looks, the applied bit, a pylon a body cannot carry, a short or long hex, junk);
//   - the host's list fits Coop's limits (printable ASCII, <= 999 bytes a value, at most kListParts keys), splits over
//     parts when it must, says what it left out, and comes back whole; a part missing or of another list waits, a
//     tampered or duplicated one refuses the whole list;
//   - readiness values round-trip and refuse contradictions; the generator is started once per missing set, never
//     while one runs or none is installed, and spaced;
//   - whole rooms: host + clients + a member without the plugin: the client lists exactly the missing files on its
//     pending list, generates once, reports; the host's lines follow (generating / failed / no pre-sync / all ready);
//     the 1 Hz throttle; the host changing its configuration; a long list over several parts; a hostile host value
//     (nothing noted, nothing started); host migration; leaving withdraws every key; no Coop table.
// Exit code 1 when one fails.
// cmake --build build --target room_sync_test && build\room_sync_test.exe
#include "../src/room_sync_logic.h"
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

namespace {
using namespace crew;
using namespace crew::roomsync;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}

// --- A fake EDF6Coop lobby ---
struct FakeMember {
    std::string id;
    bool present=true,host=false;
    std::uint32_t ordinal=0;
    std::vector<std::pair<std::string,std::string>> values;   // published, withdrawn removed
};
struct FakeLobby {
    std::vector<FakeMember> member;
    int current=0;            // whose calls these are
    bool inRoom=true;
    std::uint64_t generation=1;
    int publishes=0,clears=0,refused=0;
    // Coop's pacing (one merged extension update every 2 s, read a beat later): with `paced`, what other members read
    // is `seen`, copied from the members' values only by Send().
    bool paced=false;
    std::vector<std::vector<std::pair<std::string,std::string>>> seen;
    void Send() { seen.clear();for(const auto& m:member)seen.push_back(m.values); }
};
FakeLobby lobby;

bool ValidKey(const char* key) {
    if(!key || std::strncmp(key,"EDF6X_",6))return false;
    const std::size_t n=std::strlen(key);
    if(n<7 || n>32)return false;
    for(std::size_t i=6;i<n;++i){const char c=key[i];if(!((c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_'))return false;}
    return true;
}
bool ValidValue(const char* value) {
    if(!value)return false;
    const std::size_t n=std::strlen(value);
    if(n>EDF6COOP_MEMBER_ATTRIBUTE_MAX_VALUE)return false;
    for(std::size_t i=0;i<n;++i)if(value[i]<0x20 || value[i]>0x7E)return false;
    return true;
}
std::vector<std::string> keysSeen;   // distinct keys this process used (Coop allows 16)
void SeeKey(const char* key) {
    for(const auto& k:keysSeen)if(k==key)return;
    keysSeen.push_back(key);
}
thread_local uint32_t lastError=EDF6COOP_MA_OK;
uint32_t EDF6COOP_MA_CALL LastError() { return lastError; }
// Coop's budget: every value this process (member) publishes, the new one counted instead of the one it replaces.
std::size_t BudgetWith(const FakeMember& m,const char* key,const char* value) {
    std::size_t total=std::strlen(value);
    for(const auto& kv:m.values)if(kv.first!=key)total+=kv.second.size();
    return total;
}
uint32_t EDF6COOP_MA_CALL Publish(const char* key,const char* value) {
    if(!ValidKey(key) || !ValidValue(value)){++lobby.refused;lastError=ValidKey(key) ? EDF6COOP_MA_INVALID_VALUE : EDF6COOP_MA_INVALID_KEY;return 0;}
    SeeKey(key);
    auto& m=lobby.member[static_cast<std::size_t>(lobby.current)];
    if(BudgetWith(m,key,value)>EDF6COOP_MEMBER_ATTRIBUTE_MAX_TOTAL_VALUE){++lobby.refused;lastError=EDF6COOP_MA_VALUE_BUDGET;return 0;}
    lastError=EDF6COOP_MA_OK;
    ++lobby.publishes;++lobby.generation;
    for(auto& kv:m.values)if(kv.first==key){kv.second=value;return 1;}
    m.values.push_back({key,value});
    return 1;
}
uint32_t EDF6COOP_MA_CALL Clear(const char* key) {
    if(!ValidKey(key)){++lobby.refused;return 0;}
    auto& m=lobby.member[static_cast<std::size_t>(lobby.current)];
    ++lobby.clears;++lobby.generation;
    for(std::size_t i=0;i<m.values.size();++i)if(m.values[i].first==key){m.values.erase(m.values.begin()+static_cast<long long>(i));break;}
    return 1;
}
uint32_t EDF6COOP_MA_CALL Room(EDF6CoopRoomState* out) {
    if(!out || out->size<EDF6COOP_ROOM_STATE_V1_SIZE){lastError=EDF6COOP_MA_BAD_ARGUMENT;return 0;}
    const auto& me=lobby.member[static_cast<std::size_t>(lobby.current)];
    out->inRoom=lobby.inRoom;
    out->memberCount=0;
    for(const auto& m:lobby.member)if(m.present)++out->memberCount;
    out->isHost=me.host;
    out->generation=lobby.generation;
    std::snprintf(out->self,sizeof(out->self),"%s",me.id.c_str());
    for(const auto& m:lobby.member)if(m.present && m.host)std::snprintf(out->host,sizeof(out->host),"%s",m.id.c_str());
    return 1;
}
uint32_t EDF6COOP_MA_CALL Snapshot(const char* key,EDF6CoopMemberValueFn each,void* context,uint64_t* generation) {
    if(!ValidKey(key) || !each)return 0;
    SeeKey(key);
    for(std::size_t i=0;i<lobby.member.size();++i) {
        const auto& m=lobby.member[i];
        if(!m.present)continue;
        EDF6CoopMemberValue v{};
        v.memberId=m.id.c_str();v.ordinal=m.ordinal;v.isHost=m.host;v.isSelf=static_cast<int>(i)==lobby.current;
        v.value="";
        // Our own row at once; the others' as last sent when Coop's pacing is on.
        const auto& values=lobby.paced && static_cast<int>(i)!=lobby.current ?
            (i<lobby.seen.size() ? lobby.seen[i] : std::vector<std::pair<std::string,std::string>>{}) : m.values;
        for(const auto& kv:values)if(kv.first==key){v.hasValue=1;v.value=kv.second.c_str();v.valueBytes=static_cast<uint32_t>(kv.second.size());}
        each(context,&v);
    }
    if(generation)*generation=lobby.generation;
    return 1;
}
const EDF6CoopMemberAttributeApi kFakeApi={sizeof(EDF6CoopMemberAttributeApi),EDF6COOP_MEMBER_ATTRIBUTE_VERSION,Publish,Clear,Room,Snapshot,
                                          LastError};
// An EDF6Coop built before lastError was appended (still version 1): its table ends at snapshot.
const EDF6CoopMemberAttributeApi kOldApi={static_cast<uint32_t>(offsetof(EDF6CoopMemberAttributeApi,lastError)),
                                         EDF6COOP_MEMBER_ATTRIBUTE_VERSION,Publish,Clear,Room,Snapshot,nullptr};

const char* ValueOf(int member,const char* key) {
    for(const auto& kv:lobby.member[static_cast<std::size_t>(member)].values)if(kv.first==key)return kv.second.c_str();
    return nullptr;
}

// --- A fake machine: its Mods/OBJECT, pending list, generator and configuration ---
struct Machine {
    std::vector<std::wstring> files,pending,config;
    bool generatorInstalled=true,generatorFails=false;
    Gen state=Gen::idle;
    int starts=0;
    std::vector<std::string> log;
    Engine engine;
    bool Has(const std::wstring& f) const { for(const auto& x:files)if(x==f)return true; return false; }
    // The generator finishes: like installer.py loadout-generate, it makes what the ini (config) and the pending list name.
    void Finish() {
        if(state!=Gen::running)return;
        if(generatorFails){state=Gen::failed;return;}
        for(const auto& list:{config,pending})for(const auto& f:list)if(!Has(f))files.push_back(f);
        state=Gen::ok;
    }
};
bool FileThere(void* c,const wchar_t* file) noexcept { return static_cast<Machine*>(c)->Has(file); }
void NoteMissing(void* c,const wchar_t* file) noexcept { static_cast<Machine*>(c)->pending.push_back(file); }
bool StartGeneration(void* c) noexcept {
    auto* m=static_cast<Machine*>(c);
    if(!m->generatorInstalled || m->state==Gen::running)return false;
    ++m->starts;m->state=Gen::running;
    return true;
}
GenStatus Generation(void* c) noexcept {
    auto* m=static_cast<Machine*>(c);
    GenStatus g;
    g.state=!m->generatorInstalled ? Gen::missing : m->state;
    if(g.state==Gen::failed)wcsncpy_s(g.message,L"FileNotFoundError: EDF6VC_JET_FIGHTER.SGO",_TRUNCATE);
    return g;
}
void Configured(void* c,void (*each)(void*,const wchar_t*),void* set) noexcept {
    for(const auto& f:static_cast<Machine*>(c)->config)each(set,f.c_str());
}
void LogLine(void* c,const char* text) noexcept { static_cast<Machine*>(c)->log.push_back(text); }
Env EnvOf(Machine& m) {
    Env e;
    e.context=&m;e.fileThere=FileThere;e.noteMissing=NoteMissing;e.startGeneration=StartGeneration;e.generation=Generation;
    e.configured=Configured;e.log=LogLine;
    return e;
}
void Tick(Machine& m,int member,std::uint64_t now,const EDF6CoopMemberAttributeApi* api=&kFakeApi) {
    lobby.current=member;
    m.engine.Tick(api,now,EnvOf(m));
}
std::vector<std::wstring> Lines(const Machine& m,hudtext::Lang lang=hudtext::Lang::en) {
    SyncLine line[kLinesMost];
    const int n=m.engine.Lines(line,kLinesMost,lang);
    std::vector<std::wstring> out;
    for(int i=0;i<n;++i)out.push_back(line[i].text);
    return out;
}
bool HasLine(const Machine& m,const wchar_t* text,hudtext::Lang lang=hudtext::Lang::en) {
    for(const auto& l:Lines(m,lang))if(l==text)return true;
    return false;
}
void DumpLines(const Machine& m) { for(const auto& l:Lines(m))std::printf("    line: %ls\n",l.c_str()); }

std::wstring Name(const VariantRef& r) { wchar_t f[96];VariantFileOf(r,f,96);return f; }
VariantRef Soldier(SupportWeapon k,bool leader,std::int32_t p,std::int32_t s) {
    SupportLook look;look.primary=p;look.secondary=s;
    VariantRef r;r.sub=static_cast<std::uint8_t>(k);r.leader=leader;r.bits=LookVariant(look);
    return r;
}
VariantRef Vehicle(VehicleBody body,const wchar_t* text) {
    VehicleLoadout l;wchar_t why[160];
    ParseVehicleLoadout(text,body,&l,why,160);
    VariantRef r;r.vehicle=true;r.sub=static_cast<std::uint8_t>(body);r.bits=LoadoutVariant(l);
    return r;
}

void NamesAndTokens() {
    // Every kind, both templates, every colour form.
    const std::int32_t colours[]={kStockColour,0x000000,0x1E3A8A,0xFFFFFF,0x00FF00};
    int round=0;
    for(int k=0;k<kSupportWeaponCount;++k)for(int leader=0;leader<2;++leader)for(auto p:colours)for(auto s:colours) {
        if(p==kStockColour && s==kStockColour)continue;
        const VariantRef r=Soldier(static_cast<SupportWeapon>(k),leader!=0,p,s);
        wchar_t file[96];VariantRef back;char t[24];VariantRef fromToken;
        const bool ok=VariantFileOf(r,file,96) && ParseVariantFile(file,&back) && back==r &&
                      TokenOf(r,t,sizeof(t)) && ParseToken(t,std::strlen(t),&fromToken) && fromToken==r;
        if(ok)++round;
        else std::printf("  soldier round trip failed: kind %d leader %d %06X %06X\n",k,leader,p,s);
    }
    Expect(round==kSupportWeaponCount*2*24,"every soldier kind x leader x colour form round-trips name and token");
    // The examples the docs give, exactly.
    Expect(Name(Soldier(SupportWeapon::rifle,true,0x1E3A8A,kStockColour))==L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO","doc example: rifle leader name");
    char t[24];
    TokenOf(Soldier(SupportWeapon::rifle,false,0x1E3A8A,kStockColour),t,sizeof(t));
    Expect(!std::strcmp(t,"a1E3A8AX"),"token: rifle 1E3A8A X");
    TokenOf(Soldier(SupportWeapon::fencerShotgun,true,kStockColour,0xFFFFFF),t,sizeof(t));
    Expect(!std::strcmp(t,"NXFFFFFF"),"token: the Fencer shotgun's leader, stock primary");
    const VariantRef tank=Vehicle(VehicleBody::tank,L"HE,AP:25");
    Expect(Name(tank)==L"EDF6VC_LO_TANK_4000000000000C81.SGO","doc example: HE,AP:25 tank name");
    TokenOf(tank,t,sizeof(t));
    Expect(!std::strcmp(t,"14000000000000C81"),"token: the tank");
    // Every body's loadouts.
    const struct { VehicleBody body; const wchar_t* text; } loads[]={
        {VehicleBody::tank,L"AP,AP:20,GLM:4"},{VehicleBody::tank,L"HE,HE:40,AP:1,GLM:2,AP:38"},
        {VehicleBody::strike,L"MK82:6,MK82:6"},{VehicleBody::fighter,L"AAM_M:4,AAM_S:2"},
        {VehicleBody::interceptor,L"AAM_L:2,AAM_L:2,AAM_S:2,AAM_S:2,AAM_M:4"},{VehicleBody::multirole,L"GUNS"},
        {VehicleBody::multirole,L"AGM:2,AGM_L:4,RKT:19"}};
    int vehicles=0;
    for(const auto& l:loads) {
        const VariantRef r=Vehicle(l.body,l.text);
        wchar_t file[96];VariantRef back;VariantRef fromToken;
        if(VariantFileOf(r,file,96) && ParseVariantFile(file,&back) && back==r && TokenOf(r,t,sizeof(t)) &&
           ParseToken(t,std::strlen(t),&fromToken) && fromToken==r)++vehicles;
        else std::printf("  vehicle round trip failed: %ls\n",l.text);
    }
    Expect(vehicles==static_cast<int>(std::size(loads)),"every body's loadouts round-trip name and token");

    // Names that are not exactly what the naming writes.
    const wchar_t* badNames[]={
        L"..\\EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",L"C:\\Windows\\EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",L"OBJECT/EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",
        L"EDF6VC_NPC_RIFLE_X_X.SGO",L"edf6vc_npc_rifle_1e3a8a_x.sgo",L"EDF6VC_NPC_RIFLE_1e3a8a_X.SGO",L"EDF6VC_NPC_RIFLE_1E3A8A_X.sgo",
        L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO.SGO",L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO ",L" EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",
        L"EDF6VC_NPC_RIFLE__1E3A8A_X.SGO",L"EDF6VC_NPC_RIFLE_1E3A8_X.SGO",L"EDF6VC_NPC_RIFLE_1E3A8AA_X.SGO",
        L"EDF6VC_NPC_TANK_1E3A8A_X.SGO",L"EDF6VC_NPC_RIFLE_LL_1E3A8A_X.SGO",L"EDF6VC_NPC_RIFLE_L_1E3A8A.SGO",
        L"EDF6VC_NPC_RIFLE_1E3A8A_X",L"EDF6VC_NPC_RIFLE_G1E3A8_X.SGO",L"EDF6VC_LO_TANK_C000000000000C81.SGO",
        L"EDF6VC_LO_TANK_4000000000000C8.SGO",L"EDF6VC_LO_TANK_4000000000000C810.SGO",L"EDF6VC_LO_TANK_4000000000000c81.SGO",
        L"EDF6VC_LO_TANK_4000000000000011.SGO",L"EDF6VC_LO_STRIKE_4000000000000C81.SGO",L"EDF6VC_LO_HELI_4000000000000C81.SGO",
        L"EDF6VC_LO_FIGHTER_4000000000000008.SGO",L"EDF6VC_LO__4000000000000C81.SGO",L"EDF6VC_AP_25.SGO",L"V505_TANK_MISSION.SGO",
        L"",L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO\\..",
        L"EDF6VC_NPC_RIFLE_1E3A8A_XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX.SGO"};
    int refused=0;
    for(const wchar_t* n:badNames) {
        if(!ParseVariantFile(n,nullptr))++refused;
        else std::printf("  accepted bad name %ls\n",n);
    }
    Expect(refused==static_cast<int>(std::size(badNames)),"every malformed or foreign name is refused");
    Expect(!ParseVariantFile(nullptr,nullptr),"no name: refused");

    const char* badTokens[]={"a1E3A8A","aXX","o1E3A8AX","O1E3A8AX","a1e3a8aX","a1E3A8AXX","a1E3A8AX ","a 1E3A8AX","X1E3A8AX",
        "14000000000000C8","14000000000000C810","1C000000000000C81","14000000000000011","24000000000000C81","64000000000000C81",
        "04000000000000C81","14000000000000c81","..","a1E3A8AX/","","a0000001X\\"};
    refused=0;
    for(const char* s:badTokens) {
        if(!ParseToken(s,std::strlen(s),nullptr))++refused;
        else std::printf("  accepted bad token %s\n",s);
    }
    Expect(refused==static_cast<int>(std::size(badTokens)),"every malformed token is refused");
}

VariantSet BigSet(int soldiers) {
    VariantSet set;
    for(int i=0;i<soldiers;++i)
        set.Add(Soldier(static_cast<SupportWeapon>(i%kSupportWeaponCount),(i/kSupportWeaponCount)%2!=0,0x010101*(i+1),i%3 ? 0xABCDEF : kStockColour));
    set.Add(Vehicle(VehicleBody::tank,L"HE,AP:25"));set.Add(Vehicle(VehicleBody::fighter,L"AAM_M:4,AAM_S:2"));
    return set;
}
bool Printable(const char* v) { return ValidValue(v); }

void ListCoding() {
    VariantSet set;
    set.Add(Soldier(SupportWeapon::rifle,true,0x1E3A8A,kStockColour));
    set.Add(Soldier(SupportWeapon::rifle,false,0x1E3A8A,kStockColour));
    set.Add(Vehicle(VehicleBody::tank,L"HE,AP:25"));
    set.Add(Soldier(SupportWeapon::rifle,true,0x1E3A8A,kStockColour));   // twice: once
    Expect(set.count==3,"a set keeps each variant once");
    ListValues v;EncodeList(set,&v);
    Expect(v.parts==1 && v.listed==3,"a small list is one value");
    char expect[128];
    std::snprintf(expect,sizeof(expect),"1;%016llX;3;1;a1E3A8AX,A1E3A8AX,14000000000000C81",static_cast<unsigned long long>(v.hash));
    Expect(!std::strcmp(v.value[0],expect),"the value as documented (sorted: soldiers by kind, member before leader, then vehicles)");
    const char* values[kListParts]={v.value[0]};
    VariantSet back;std::uint64_t hash=0;
    Expect(DecodeList(values,1,&back,&hash)==ListState::ok && back.count==3 && hash==v.hash,"it decodes");
    for(int i=0;i<3;++i)Expect(back.ref[i]==set.ref[i],"the same variants, in order");
    // Empty: still a list (nothing needed).
    VariantSet none;ListValues e;EncodeList(none,&e);
    values[0]=e.value[0];
    Expect(DecodeList(values,1,&back,&hash)==ListState::ok && back.count==0,"an empty list decodes as nothing needed");

    // Long: split over parts, each within Coop's limits, the first ones listed.
    for(const int soldiers:{60,150,260,500}) {
        const VariantSet big=BigSet(soldiers);
        ListValues l;EncodeList(big,&l);
        bool fits=l.parts>=1 && l.parts<=kListParts;
        for(int p=0;p<l.parts;++p)fits=fits && std::strlen(l.value[p])<=kValueMost && Printable(l.value[p]);
        Expect(fits,"every part printable, <= 999 bytes, at most kListParts");
        const char* parts[kListParts]{};
        for(int p=0;p<l.parts;++p)parts[p]=l.value[p];
        VariantSet got;std::uint64_t h=0;
        const ListState st=DecodeList(parts,l.parts,&got,&h);
        bool same=st==ListState::ok && got.count==l.listed && h==l.hash;
        for(int i=0;same && i<l.listed;++i)same=got.ref[i]==big.ref[i];
        Expect(same,"a long list decodes to its listed prefix");
        if(soldiers<=150)Expect(l.listed==big.count,"150 soldiers and 2 vehicles fit");
        if(soldiers==500)Expect(l.listed<big.count && l.parts==kListParts,"500 do not: the rest left out, all parts used");
        if(soldiers==260 && l.parts>1) {
            // A part missing, or still the previous list's: waiting. Tampered: refused.
            Expect(DecodeList(parts,1,&got,&h)==ListState::waiting,"a part not there yet: waiting");
            std::string other=parts[1];other[0]=other[0]=='0' ? '1' : '0';
            const char* swapped[kListParts]={parts[0],other.c_str(),parts[2],parts[3]};
            Expect(DecodeList(swapped,l.parts,&got,&h)==ListState::waiting,"a part of another list: waiting");
            std::string tampered=parts[1];tampered[tampered.size()-2]=tampered[tampered.size()-2]=='A' ? 'B' : 'A';
            const char* bad[kListParts]={parts[0],tampered.c_str(),parts[2],parts[3]};
            Expect(DecodeList(bad,l.parts,&got,&h)==ListState::bad && got.count==0,"a tampered part: the whole list refused");
        }
    }
    // Hostile or broken headers.
    std::string good=v.value[0];
    const auto refusedValue=[&](const std::string& value,const char* what) {
        const char* vs[kListParts]={value.c_str()};
        VariantSet got;std::uint64_t h=0;
        Expect(DecodeList(vs,1,&got,&h)==ListState::bad && got.count==0,what);
    };
    refusedValue(good+",a1E3A8AX","a duplicate token: refused");
    refusedValue(good+",","an empty token: refused");
    refusedValue(good+",..\\..\\Windows\\x","a path among the tokens: refused");
    refusedValue("2"+good.substr(1),"another version: refused");
    std::string count=good;count[20]='4';
    refusedValue(count,"a count that is not the tokens': refused");
    std::string hashed=good;hashed[2]=hashed[2]=='0' ? '1' : '0';
    refusedValue(hashed,"a hash that is not the tokens': refused");
    refusedValue("1;0000000000000000;0;5;","more parts than there are keys: refused");
    refusedValue("1;0000000000000000;00;1;","a padded count: refused");
    refusedValue("garbage","junk: refused");
    const char* nothing[kListParts]{};
    VariantSet got;std::uint64_t h=0;
    Expect(DecodeList(nothing,1,&got,&h)==ListState::none,"no value: none");
}

void HaveAndPlan() {
    Have h;h.hash=0x0123456789ABCDEFull;h.missing=3;h.state=Ready::generating;
    char v[48];EncodeHave(h,v,sizeof(v));
    Expect(!std::strcmp(v,"1;0123456789ABCDEF;3;g"),"readiness as documented");
    Have back;
    Expect(ParseHave(v,&back) && back.hash==h.hash && back.missing==3 && back.state==Ready::generating,"it parses");
    const char* bad[]={"1;0123456789ABCDEF;0;g","1;0123456789ABCDEF;2;r","1;0123456789ABCDEF;1;w","1;0123456789ABCDEF;1;x",
                       "1;0123456789abcdef;1;g","1;0123456789ABCDEF;1;gg","2;0123456789ABCDEF;1;g","1;0123456789ABCDEF;-1;g",""};
    int refused=0;
    for(const char* b:bad)if(!ParseHave(b,&back))++refused;else std::printf("  accepted readiness %s\n",b);
    Expect(refused==static_cast<int>(std::size(bad)),"contradicting or malformed readiness refused");
    Expect(ParseHave("1;0000000000000000;0;w",&back) && back.state==Ready::noList,"no list yet");

    GenPlan p;
    Expect(!ShouldStart(p,1,0,Gen::idle,0),"nothing missing: no start");
    Expect(!ShouldStart(p,1,2,Gen::running,0),"one running: no start");
    Expect(!ShouldStart(p,1,2,Gen::missing,0),"no generator: no start");
    Expect(OwnReady(p,1,2,Gen::missing)==Ready::noGenerator,"no generator: said");
    Expect(ShouldStart(p,1,2,Gen::idle,0),"missing: start");
    Started(p,0);
    Expect(!ShouldStart(p,1,2,Gen::ok,1000),"not again within the gap");
    Expect(ShouldStart(p,1,2,Gen::ok,kStartGapMs),"ended ok yet still missing: once more after the gap");
    Started(p,kStartGapMs);
    Expect(!ShouldStart(p,1,2,Gen::ok,3*kStartGapMs) && OwnReady(p,1,2,Gen::ok)==Ready::failed,"then no more: failed");
    Expect(ShouldStart(p,2,1,Gen::ok,3*kStartGapMs),"another missing set: a fresh start");
    Started(p,3*kStartGapMs);
    Expect(!ShouldStart(p,2,1,Gen::failed,9*kStartGapMs) && OwnReady(p,2,1,Gen::failed)==Ready::failed,"failed: not retried for the same set");
    Expect(OwnReady(p,2,0,Gen::failed)==Ready::ready,"nothing missing: ready whatever the generator said");
}

void ResetLobby(int members) {
    lobby=FakeLobby{};
    for(int i=0;i<members;++i){FakeMember m;m.id="PUID"+std::to_string(i);m.ordinal=static_cast<std::uint32_t>(i);m.host=i==0;lobby.member.push_back(m);}
}

void Rooms() {
    // Host (member 1), a client with the plugin (2), a member without it (3: never ticks, publishes nothing).
    ResetLobby(3);
    static Machine host,client;host=Machine{};client=Machine{};
    host.config={L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO",L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",L"EDF6VC_LO_TANK_4000000000000C81.SGO",
                 L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",L"..\\..\\evil.SGO"};
    host.files={L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO",L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",L"EDF6VC_LO_TANK_4000000000000C81.SGO"};
    client.files={L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"};
    std::uint64_t now=100000;
    Tick(host,0,now);
    const char* list=ValueOf(0,kKeyList);
    Expect(list && !std::strncmp(list,"1;",2) && std::strstr(list,";3;1;a1E3A8AX,A1E3A8AX,14000000000000C81"),
           "the host lists its configuration's three files (duplicates once, a name its naming does not write dropped)");
    Expect(host.engine.Now().own==Ready::ready && ValueOf(0,kKeyHave) && std::strstr(ValueOf(0,kKeyHave),";0;r"),
           "the host has them: ready");
    Expect(host.pending.empty() && host.starts==0,"the host neither notes nor generates what it has");
    Tick(client,1,now+3);
    Expect(client.pending==std::vector<std::wstring>({L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO",L"EDF6VC_LO_TANK_4000000000000C81.SGO"}),
           "the client notes exactly the two it lacks on its pending list");
    Expect(client.starts==1 && client.engine.Now().own==Ready::generating,"and starts the generator once");
    Expect(ValueOf(1,kKeyHave) && std::strstr(ValueOf(1,kKeyHave),";2;g"),"its readiness: 2 missing, generating");
    Expect(HasLine(client,L"This PC: 2 files missing, generating"),"its line says so");
    const int publishes=lobby.publishes;
    Tick(client,1,now+500);
    Expect(lobby.publishes==publishes && client.starts==1 && client.pending.size()==2,"within the second: nothing done again");
    Tick(host,0,now+1000);
    Expect(HasLine(host,L"Member 2: 2 files missing, generating"),"the host sees the client generating");
    Expect(HasLine(host,L"Member 3 has no pre-sync (older All Forces or Coop)"),"and the member without the plugin");
    Expect(host.engine.State()==SyncState::waiting,"waiting");
    Expect(HasLine(host,L"成员 2 缺 2 个文件，生成中",hudtext::Lang::zhCN),"in Chinese too");
    Expect(HasLine(host,L"メンバー 2：2 個のファイルが不足、生成中",hudtext::Lang::ja),"in Japanese too");
    client.Finish();
    Tick(client,1,now+2000);
    Expect(client.engine.Now().own==Ready::ready && client.starts==1 && std::strstr(ValueOf(1,kKeyHave),";0;r"),"made: ready");
    lobby.member[2].present=false;   // the old member leaves
    Tick(host,0,now+2000);
    Expect(host.engine.State()==SyncState::ready && HasLine(host,L"All members ready (3 files)"),"everyone ready: said once");
    Expect(Lines(host).size()==1,"and only that");
    Tick(client,1,now+3000);
    Expect(client.engine.State()==SyncState::ready,"the client agrees");

    // The host changes its configuration: a new list; the client's readiness is about the old one until it rechecks.
    host.config.push_back(Name(Vehicle(VehicleBody::fighter,L"AAM_M:4,AAM_S:2")));
    Tick(host,0,now+4000);
    Expect(HasLine(host,L"This PC: 1 files missing, generating") && host.starts==1 && host.pending.empty(),
           "the host lacks its new file: its own generator, no pending list (its ini names it)");
    Expect(HasLine(host,L"Member 2: checking the list"),"the client's readiness is the old list's: checking");
    client.generatorFails=true;
    Tick(client,1,now+5000);
    Expect(client.pending.size()==3 && client.starts==1,"the client notes the new one at once; the generator not again within 5 s of the last start");
    Tick(client,1,now+6000);
    Expect(client.starts==2,"then it starts");
    client.Finish();
    Tick(client,1,now+7000);
    Tick(client,1,now+20000);
    Expect(client.starts==2 && client.engine.Now().own==Ready::failed,"failed: not retried for the same set");
    Expect(HasLine(client,L"This PC: 1 files missing, generation failed: FileNotFoundError: EDF6VC_JET_FIGHTER.SGO"),"the reason said");
    Tick(host,0,now+20000);
    Expect(HasLine(host,L"Member 2: 1 files missing, generation failed"),"the host sees it failed");

    // Leaving withdraws everything this machine published.
    lobby.current=1;client.engine.Leave(&kFakeApi);
    Expect(lobby.member[1].values.empty(),"the client's keys withdrawn");
    lobby.current=0;host.engine.Leave(&kFakeApi);
    Expect(lobby.member[0].values.empty(),"the host's keys withdrawn");
    Expect(keysSeen.size()<=5,"at most five extension keys (Coop allows 16 published a process)");
    Expect(lobby.refused==0,"Coop never refused a key or value");
}

void LongList() {
    ResetLobby(2);
    static Machine host,client;host=Machine{};client=Machine{};
    const VariantSet big=BigSet(200);
    for(int i=0;i<big.count;++i)host.config.push_back(Name(big.ref[i]));
    host.files=host.config;
    Tick(host,0,1000);
    Expect(ValueOf(0,"EDF6X_VCVAR1") && !ValueOf(0,"EDF6X_VCVAR3"),"202 files: several values, not every part");
    Tick(client,1,1000);
    Expect(client.engine.Now().list==ListState::ok && static_cast<int>(client.pending.size())==kNotesPerRoom,
           "the client reads every part and notes the first kNotesPerRoom (128) files: no more a room");
    Expect(client.engine.Now().ownMissing==big.count && client.engine.Now().ownCapped==big.count-kNotesPerRoom,
           "the rest counted missing, over the room's limit");
    Expect(HasLine(client,L"This PC: 74 files over this room's limit of 128: they come stock"),"said, with the limit");
    Expect(client.starts==1,"the generator started for the noted ones");
    client.Finish();
    Tick(client,1,1600);
    Tick(client,1,9000);
    Tick(client,1,20000);
    Expect(client.starts==1 && static_cast<int>(client.pending.size())==kNotesPerRoom,
           "made what it noted: never started again for files it may not make, nothing more noted");
    Expect(client.engine.Now().own==Ready::failed && std::strstr(ValueOf(1,kKeyHave),";74;f"),
           "its readiness: 74 missing, failed (the host keeps those stock)");
    Expect(!HasLine(client,L"This PC: 74 files missing, generating"),"not said to be generating them");
    // A new list in the same room: still no more than kNotesPerRoom notes this room.
    host.config.push_back(Name(Soldier(SupportWeapon::sniper,false,0x0A0B0C,kStockColour)));
    host.files=host.config;
    Tick(host,0,22000);
    Tick(client,1,22000);
    Expect(static_cast<int>(client.pending.size())==kNotesPerRoom,"a changed list in the same room: the room's limit holds");
    lobby.current=1;client.engine.Leave(&kFakeApi);
    Tick(client,1,24000);
    Expect(static_cast<int>(client.pending.size())==kNotesPerRoom+75,"a new room (left and back): a new limit, the other 75 noted");
    // Fewer: the parts no longer needed are withdrawn.
    host.config.resize(3);host.files=host.config;
    Tick(host,0,3000);
    Expect(!ValueOf(0,"EDF6X_VCVAR1"),"a shorter list: the extra parts withdrawn");
    Tick(client,1,3000);
    Expect(client.engine.Now().list==ListState::ok && client.engine.Target().count==3,"and the client follows");
}

// The host's list a client validated is what its next mission preloads (support_variants.h): kept when the room is left
// (a mission starting leaves it), replaced by the next room's tick, dropped on a bad / no list, on the host, by
// ForgetHostList (the switch turned off).
void MissionList() {
    ResetLobby(2);
    static Machine host,client;host=Machine{};client=Machine{};
    host.config={L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",L"EDF6VC_LO_TANK_4000000000000C81.SGO"};host.files=host.config;
    Tick(host,0,1000);
    Expect(!host.engine.HasHostList(),"the host keeps no host list (its own configuration is its list)");
    Tick(client,1,1000);
    Expect(client.engine.HasHostList() && client.engine.HostList().count==2 && client.engine.HostListHash()!=0,
           "the client keeps the validated list");
    lobby.current=1;client.engine.Leave(&kFakeApi);
    Expect(client.engine.HasHostList() && client.engine.HostList().count==2,"the room left (a mission starts): still there");
    client.engine.ForgetHostList();
    Expect(!client.engine.HasHostList() && client.engine.HostList().count==0 && !client.engine.HostListHash(),"forgotten on demand");
    Tick(client,1,3000);
    Expect(client.engine.HasHostList(),"the next tick in the room: back");
    lobby.current=0;Publish(kKeyList,"1;0000000000000000;1;1;..\\x");
    Tick(client,1,5000);
    Expect(!client.engine.HasHostList(),"the host's list turned bad: dropped (never a stale one)");
    lobby.member[0].values.clear();
    Tick(client,1,7000);
    Expect(!client.engine.HasHostList(),"no list (an older host): none");
}

// Coop's 4096-byte budget (every value of this process), its error codes, an older table without lastError, and its
// pacing (values seen by the others only when the merged update goes out).
void Budget() {
    // The longest list: within kListBudget in all, the readiness beside it within Coop's budget.
    const VariantSet big=BigSet(500);
    ListValues l;EncodeList(big,&l);
    std::size_t total=0;
    for(int p=0;p<l.parts;++p)total+=std::strlen(l.value[p]);
    Expect(total<=kListBudget && total+kHaveMost<=EDF6COOP_MEMBER_ATTRIBUTE_MAX_TOTAL_VALUE && l.listed<big.count && l.listed>200,
           "a 502-file list: listed as far as 3800 bytes go (over 200 files), the rest left out");
    ResetLobby(2);
    static Machine host;host=Machine{};
    for(int i=0;i<big.count;++i)host.config.push_back(Name(big.ref[i]));
    host.files=host.config;
    Tick(host,0,1000);
    std::size_t published=0;
    for(const auto& kv:lobby.member[0].values)published+=kv.second.size();
    Expect(lobby.refused==0 && published<=EDF6COOP_MEMBER_ATTRIBUTE_MAX_TOTAL_VALUE && ValueOf(0,kKeyHave),
           "the host publishes its longest list and its readiness within Coop's budget, nothing refused");
    Expect(host.engine.Now().leftOut==big.count-l.listed,"what did not fit is counted as left out (said, as before)");
    // Another plugin of this process takes most of the budget: our list is refused, said, logged, retried.
    lobby.current=0;
    host=Machine{};
    host.config={L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"};host.files=host.config;
    lobby.member[0].values.clear();
    Publish("EDF6X_OTHER",std::string(999,'a').c_str());Publish("EDF6X_OTHER2",std::string(999,'b').c_str());
    Publish("EDF6X_OTHER3",std::string(999,'c').c_str());Publish("EDF6X_OTHER4",std::string(999,'d').c_str());Publish("EDF6X_OTHER5",std::string(90,'e').c_str());
    const int refusedBefore=lobby.refused;
    Tick(host,0,3000);
    Expect(lobby.refused>refusedBefore && !ValueOf(0,kKeyList),"over the budget: Coop refuses the list");
    Expect(HasLine(host,L"EDF6Coop refused this PC's pre-sync value (error 5; 5 = its 4096-byte budget is full): the others do not see it"),
           "said on a line with its error (never lost silently)");
    bool logged=false;for(const auto& t:host.log)logged|=t.find("refused EDF6X_VCVARIANTS")!=std::string::npos && t.find("error 5")!=std::string::npos;
    Expect(logged,"and logged with the key and the error");
    lobby.current=0;Clear("EDF6X_OTHER");Clear("EDF6X_OTHER2");
    Tick(host,0,5000);
    Expect(ValueOf(0,kKeyList) && !HasLine(host,L"EDF6Coop refused this PC's pre-sync value (error 5; 5 = its 4096-byte budget is full): the others do not see it"),
           "room again: retried and published, the line gone");
    // An older Coop table (no lastError): still used; a refusal says error 0.
    ResetLobby(2);
    static Machine old;old=Machine{};old.config={L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"};old.files=old.config;
    lobby.current=0;
    Publish("EDF6X_OTHER",std::string(999,'a').c_str());Publish("EDF6X_OTHER2",std::string(999,'b').c_str());
    Publish("EDF6X_OTHER3",std::string(999,'c').c_str());Publish("EDF6X_OTHER4",std::string(999,'d').c_str());Publish("EDF6X_OTHER5",std::string(90,'e').c_str());
    Tick(old,0,1000,&kOldApi);
    Expect(old.engine.State()!=SyncState::unavailable && HasLine(old,L"EDF6Coop refused this PC's pre-sync value (error 0; 5 = its 4096-byte budget is full): the others do not see it"),
           "a table without lastError: used, never called past its end");
    // Coop's pacing: the others see a value only once the merged update is sent.
    ResetLobby(2);
    lobby.paced=true;lobby.Send();
    static Machine h,c;h=Machine{};c=Machine{};
    h.config={L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"};h.files=h.config;
    Tick(h,0,1000);
    Tick(c,1,1000);
    Expect(c.engine.Now().list==ListState::none && c.pending.empty(),"the host's list not sent yet: nothing for the client");
    lobby.Send();
    Tick(c,1,2000);
    Tick(h,0,2000);
    Expect(c.engine.Now().list==ListState::ok && c.pending.size()==1,"sent: the client reads it");
    Expect(HasLine(h,L"Member 2: checking the list"),"the client's readiness that went out is from before the list: checking");
    lobby.Send();
    Tick(h,0,3000);
    Expect(HasLine(h,L"Member 2: 1 files missing, generating"),"seen a send later");
    c.Finish();
    Tick(c,1,3000);
    Tick(h,0,4000);
    Expect(HasLine(h,L"Member 2: 1 files missing, generating"),"its newer value not sent yet: the last one still read");
    lobby.Send();
    Tick(h,0,5000);
    Expect(h.engine.State()==SyncState::ready,"sent: ready (nothing assumed visible at once)");
    lobby.paced=false;
}

void Hostile() {
    ResetLobby(2);
    static Machine client;client=Machine{};
    lobby.current=0;
    Publish(kKeyList,"1;0000000000000000;1;1;..\\..\\Windows\\System32\\x");
    Tick(client,1,1000);
    Expect(client.engine.Now().list==ListState::bad && client.pending.empty() && client.starts==0,
           "a hostile list: nothing noted, nothing started");
    Expect(HasLine(client,L"The host's file list is malformed: ignored"),"said");
    // A valid list from a member who is not the host: ignored.
    ResetLobby(3);
    client=Machine{};
    VariantSet set;set.Add(Soldier(SupportWeapon::sniper,false,0x123456,kStockColour));
    ListValues v;EncodeList(set,&v);
    lobby.current=2;Publish(kKeyList,v.value[0]);
    Tick(client,1,1000);
    Expect(client.engine.Now().list==ListState::none && client.pending.empty(),"a non-host's list: ignored");
    Expect(HasLine(client,L"The host has no pre-sync (older All Forces): missing files go on the pending list"),"the host has none: said");
}

void Migration() {
    ResetLobby(2);
    static Machine a,b;a=Machine{};b=Machine{};
    a.config={L"EDF6VC_NPC_LANCE_1E3A8A_X.SGO"};a.files=a.config;
    b.config={L"EDF6VC_NPC_SNIPER_X_FFFFFF.SGO"};b.files=b.config;
    Tick(a,0,1000);Tick(b,1,1000);
    Expect(b.pending.size()==1,"b notes a's file");
    lobby.member[0].host=false;lobby.member[1].host=true;   // a leaves the host's seat to b
    Tick(a,0,3000);Tick(b,1,3000);
    Expect(!ValueOf(0,kKeyList),"the old host withdraws its list");
    Expect(ValueOf(1,kKeyList) && std::strstr(ValueOf(1,kKeyList),";1;1;eXFFFFFF"),
           "the new host lists its own");
    Tick(a,0,5000);
    Expect(a.engine.Now().list==ListState::ok && a.pending==std::vector<std::wstring>({L"EDF6VC_NPC_SNIPER_X_FFFFFF.SGO"}),
           "and the old one now follows it");
}

void NoCoop() {
    static Machine m;m=Machine{};
    m.engine.Tick(nullptr,1000,EnvOf(m));
    Expect(m.engine.State()==SyncState::unavailable,"no member attribute table: unavailable");
    Expect(HasLine(m,L"EDF6Coop too old (no member attributes): room pre-sync off, missing files go on the pending list"),"said");
    Expect(m.starts==0 && m.pending.empty(),"and nothing else happens");
    ResetLobby(1);
    lobby.inRoom=false;
    static Machine out;out=Machine{};
    Tick(out,0,1000);
    Expect(out.engine.State()==SyncState::alone && Lines(out).empty() && lobby.member[0].values.empty(),"out of a room: nothing");
    lobby.inRoom=true;
    out.config={L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"};
    Tick(out,0,3000);
    Expect(out.engine.State()==SyncState::alone && out.starts==1,"alone in a room: still makes its own files");
    lobby.inRoom=false;
    Tick(out,0,5000);
    Expect(lobby.member[0].values.empty(),"the room gone: its keys withdrawn");
}
}  // namespace

int main() {
    NamesAndTokens();
    ListCoding();
    HaveAndPlan();
    Rooms();
    LongList();
    MissionList();
    Budget();
    Hostile();
    Migration();
    NoCoop();
    std::printf("room_sync: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
