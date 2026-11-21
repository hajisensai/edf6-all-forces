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
#include <map>
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
    std::string ownPlayer;   // its EDF6X_VCPVEH value (player_vehicle_logic.h OwnValue); empty: none
    // Its configuration line (member_config.h's, which the engine only carries): `line` when set, else its files' names
    // joined by ','; `leftOut` the entries the writer says did not fit. A line it reads names the files lineSets gives
    // for it, else its ','-separated names; one with "BAD" in it does not read (member_config.h refuses it).
    std::string line;int leftOut=0;
    std::map<std::string,std::vector<std::wstring>> lineSets;
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
std::size_t OwnConfig(void* c,char* out,std::size_t capacity,int* leftOut) noexcept {
    const auto* m=static_cast<Machine*>(c);
    std::string line=m->line;
    if(line.empty())
        for(const auto& f:m->config){if(!line.empty())line+=',';for(wchar_t ch:f)line+=static_cast<char>(ch&0x7F);}
    if(line.size()+1>capacity)line.resize(capacity-1);
    std::snprintf(out,capacity,"%s",line.c_str());
    if(leftOut)*leftOut=m->leftOut;
    return line.size();
}
bool MemberVariants(void* c,const char* line,void (*each)(void*,const wchar_t*),void* set) noexcept {
    const auto* m=static_cast<Machine*>(c);
    if(std::strstr(line,"BAD"))return false;
    std::vector<std::wstring> names;
    if(const auto it=m->lineSets.find(line);it!=m->lineSets.end())names=it->second;
    else for(const char* s=line;*s;) {
        const char* end=s;while(*end && *end!=',')++end;
        std::wstring n;for(const char* k=s;k<end;++k)n+=static_cast<wchar_t>(*k);
        if(!n.empty())names.push_back(n);
        s=*end ? end+1 : end;
    }
    if(each)for(const auto& n:names)each(set,n.c_str());
    return true;
}
bool OwnPlayer(void* c,char* value,std::size_t capacity) noexcept {
    const auto* m=static_cast<Machine*>(c);
    if(m->ownPlayer.empty())return false;
    std::snprintf(value,capacity,"%s",m->ownPlayer.c_str());
    return true;
}
Env EnvOf(Machine& m) {
    Env e;
    e.context=&m;e.fileThere=FileThere;e.noteMissing=NoteMissing;e.startGeneration=StartGeneration;e.generation=Generation;
    e.configured=Configured;e.ownConfig=OwnConfig;e.memberVariants=MemberVariants;e.log=LogLine;e.ownPlayer=OwnPlayer;
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
    int round=0,looklessRefused=0;
    for(int k=0;k<kSupportWeaponCount;++k)for(int leader=0;leader<2;++leader)for(auto p:colours)for(auto s:colours) {
        if(p==kStockColour && s==kStockColour)continue;
        const VariantRef r=Soldier(static_cast<SupportWeapon>(k),leader!=0,p,s);
        wchar_t file[96];VariantRef back;char t[24];VariantRef fromToken;
        if(!SupportKindHasLooks(static_cast<SupportWeapon>(k))) {   // the X-Morph infantry: no template to recolour, no file, no token
            looklessRefused+=!VariantFileOf(r,file,96) && !TokenOf(r,t,sizeof(t));
            continue;
        }
        const bool ok=VariantFileOf(r,file,96) && ParseVariantFile(file,&back) && back==r &&
                      TokenOf(r,t,sizeof(t)) && ParseToken(t,std::strlen(t),&fromToken) && fromToken==r;
        if(ok)++round;
        else std::printf("  soldier round trip failed: kind %d leader %d %06X %06X\n",k,leader,p,s);
    }
    Expect(round==(kSupportWeaponCount-1)*2*24 && looklessRefused==2*24,
           "every soldier kind with looks x leader x colour form round-trips name and token; the X-Morph infantry names no file");
    Expect(!ParseVariantFile(L"EDF6VC_NPC_XMINFANTRY_1E3A8A_X.SGO",nullptr) && !ParseVariantFile(L"EDF6VC_NPC_XMINFANTRY_L_X_X_AHANDGRENADE01.SGO",nullptr) &&
           !ParseToken("o1E3A8AX",8,nullptr),"a colour or backpack of the X-Morph infantry is refused by name and token");
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

    // Backpack weapons (npc_pack_table.h; docs/npc-backpack-re.md): a soldier's pack in its name and its token.
    int packs=0,fits=0;
    for(int k=0;k<kSupportWeaponCount;++k)for(const auto& p:kPackWeapons)for(int leader=0;leader<2;++leader) {
        SupportLook look;look.pack=p.code;if(leader)look.primary=0x1E3A8A;
        VariantRef r;r.sub=static_cast<std::uint8_t>(k);r.leader=leader!=0;r.bits=LookVariant(look);
        wchar_t file[96];VariantRef back;VariantRef fromToken;
        const bool fit=SupportKindHasLooks(static_cast<SupportWeapon>(k)) && PackFits(p.code,SupportSoldierClass(static_cast<SupportWeapon>(k)));
        fits+=fit;
        const bool named=VariantFileOf(r,file,96)>0;
        if(fit==(named && ParseVariantFile(file,&back) && back==r && TokenOf(r,t,sizeof(t)) && ParseToken(t,std::strlen(t),&fromToken) &&
                 fromToken==r))++packs;
        else std::printf("  pack round trip wrong: kind %d pack %d\n",k,p.code);
    }
    Expect(packs==kSupportWeaponCount*kPackWeaponCount*2 && fits==(5*45+5*29)*2,
           "a pack its class carries round-trips name and token; one it cannot carry (or the X-Morph infantry) names no file");
    SupportLook grenade;grenade.pack=1;
    VariantRef g;g.sub=static_cast<std::uint8_t>(SupportWeapon::rifle);g.bits=LookVariant(grenade);
    TokenOf(g,t,sizeof(t));
    Expect(!std::strcmp(t,"aXX+01") && Name(g)==L"EDF6VC_NPC_RIFLE_X_X_AHANDGRENADE01.SGO","doc example: a rifle with an MG11");
    const wchar_t* badPackNames[]={L"EDF6VC_NPC_RIFLE_X_X_AWEAPON261.SGO",L"EDF6VC_NPC_RIFLE_X_X_PWEAPON216.SGO",
        L"EDF6VC_NPC_CANNON_X_X_AHANDGRENADE01.SGO",L"EDF6VC_NPC_RIFLE_X_X_.SGO",L"EDF6VC_NPC_RIFLE_X_X_ahandgrenade01.SGO",
        L"EDF6VC_NPC_RIFLE_X_X_AHANDGRENADE01_AHANDGRENADE01.SGO"};
    refused=0;
    for(const wchar_t* n:badPackNames)if(!ParseVariantFile(n,nullptr))++refused;
    Expect(refused==static_cast<int>(std::size(badPackNames)),"a pack its kind cannot carry, a left-out one, a malformed one: refused");
    const char* badPackTokens[]={"aXX+00","aXX+4B","aXX+3B","aXX+1","aXX+011","aXX-01","KXX+01","aXX+0g"};
    refused=0;
    for(const char* s:badPackTokens)if(!ParseToken(s,std::strlen(s),nullptr))++refused;
    Expect(refused==static_cast<int>(std::size(badPackTokens)),"a token with no / an unknown / a misfit pack: refused");
}

VariantSet BigSet(int soldiers) {
    VariantSet set;
    for(int i=0;i<soldiers;++i)
        set.Add(Soldier(static_cast<SupportWeapon>(i%(kSupportWeaponCount-1)),(i/(kSupportWeaponCount-1))%2!=0,0x010101*(i+1),i%3 ? 0xABCDEF : kStockColour));
    set.Add(Vehicle(VehicleBody::tank,L"HE,AP:25"));set.Add(Vehicle(VehicleBody::fighter,L"AAM_M:4,AAM_S:2"));
    return set;
}
bool Printable(const char* v) { return ValidValue(v); }

// A member's configuration line (member_config.h writes and reads its content; the engine only carries it).
void ConfigCoding() {
    const std::string small="Q=shotgun|P0=rifle@1E3A8A*2,rocket";
    ConfigValues v;
    Expect(EncodeConfig(small.c_str(),&v) && v.parts==1,"a short line is one value");
    char expect[160];
    std::snprintf(expect,sizeof(expect),"1;%016llX;1;%s",static_cast<unsigned long long>(v.hash),small.c_str());
    Expect(!std::strcmp(v.value[0],expect),"the value as documented: \"1;<hash>;<parts>;<line>\"");
    const char* values[kConfigParts]={v.value[0]};
    static char line[kConfigLineMost+1];std::uint64_t hash=0;
    Expect(DecodeConfig(values,1,line,&hash)==ListState::ok && small==line && hash==v.hash,"it decodes to the same line");
    // Empty: still a configuration (nothing but defaults).
    ConfigValues e;
    Expect(EncodeConfig("",&e) && e.parts==1,"an empty line encodes");
    values[0]=e.value[0];
    Expect(DecodeConfig(values,1,line,&hash)==ListState::ok && !line[0],"and decodes as defaults");
    // Long: split over parts, each within Coop's limits; the longest line fits the budget with its headers.
    for(const std::size_t length:{900ul,1500ul,2900ul,static_cast<unsigned long>(kConfigLineMost)}) {
        std::string big;
        for(std::size_t i=0;big.size()<length;++i)big+=static_cast<char>('A'+i%26);
        big.resize(length);
        ConfigValues l;
        Expect(EncodeConfig(big.c_str(),&l),"a line up to kConfigLineMost encodes");
        std::size_t total=0;bool fits=l.parts>=1 && l.parts<=kConfigParts;
        for(int p=0;p<l.parts;++p){fits=fits && std::strlen(l.value[p])<=kValueMost && Printable(l.value[p]);total+=std::strlen(l.value[p]);}
        Expect(fits && total<=kConfigBudget,"every part printable, <= 999 bytes, at most kConfigParts, within the budget");
        const char* parts[kConfigParts]{};
        for(int p=0;p<l.parts;++p)parts[p]=l.value[p];
        Expect(DecodeConfig(parts,l.parts,line,&hash)==ListState::ok && big==line && hash==l.hash,"and decodes whole");
        if(l.parts>1) {
            Expect(DecodeConfig(parts,1,line,&hash)==ListState::waiting && !line[0],"a part not there yet: waiting");
            std::string other=parts[1];other[0]=other[0]=='0' ? '1' : '0';
            const char* swapped[kConfigParts]={parts[0],other.c_str(),parts[2],parts[3]};
            Expect(DecodeConfig(swapped,l.parts,line,&hash)==ListState::waiting,"a part of another line: waiting");
            std::string tampered=parts[1];tampered[tampered.size()-2]=tampered[tampered.size()-2]=='A' ? 'B' : 'A';
            const char* bad[kConfigParts]={parts[0],tampered.c_str(),parts[2],parts[3]};
            Expect(DecodeConfig(bad,l.parts,line,&hash)==ListState::bad && !line[0],"a tampered part: the whole line refused");
        }
    }
    std::string tooLong(kConfigLineMost+1,'x');
    ConfigValues t;
    Expect(!EncodeConfig(tooLong.c_str(),&t) && !EncodeConfig("tab\there",&t),"a line past the budget or not printable: not encoded");
    // Hostile or broken headers.
    const std::string good=v.value[0];
    const auto refusedValue=[&](const std::string& value,const char* what) {
        const char* vs[kConfigParts]={value.c_str()};
        Expect(DecodeConfig(vs,1,line,&hash)==ListState::bad && !line[0],what);
    };
    refusedValue(good+"x","a line that is not the hash's: refused");
    refusedValue("2"+good.substr(1),"another version: refused");
    std::string hashed=good;hashed[2]=hashed[2]=='0' ? '1' : '0';
    refusedValue(hashed,"a hash that is not the line's: refused");
    refusedValue("1;0000000000000000;5;","more parts than there are keys: refused");
    refusedValue("1;0000000000000000;01;","a padded count: refused");
    refusedValue("garbage","junk: refused");
    const char* nothing[kConfigParts]{};
    Expect(DecodeConfig(nothing,1,line,&hash)==ListState::none,"no value: none");
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
    Expect(ParseHave("1;0000000000000000;0;w",&back) && back.state==Ready::noList,"no set yet");

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
std::string LineOf(const Machine& m) {
    std::string out;
    for(const auto& f:m.config){if(!out.empty())out+=',';for(wchar_t c:f)out+=static_cast<char>(c&0x7F);}
    return out;
}
// The other members' lines this machine keeps for the host's plans (room_sync.cpp RoomMemberConfig).
std::string KeptLine(const Machine& m,const char* id) {
    static MemberLine copy[kMembersMost];
    m.engine.CopyMembers(copy,kMembersMost);
    for(const auto& l:copy)if(l.id[0] && l.ok && !std::strcmp(l.id,id))return l.line;
    return "-";
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
    const char* cfg=ValueOf(0,kKeyConfig);
    Expect(cfg && !std::strncmp(cfg,"1;",2) && std::strstr(cfg,";1;EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO,"),"the host publishes its configuration line");
    Expect(host.engine.Target().count==3,"its room set: its configuration's three files (duplicates once, a name its naming does not write dropped)");
    Expect(host.engine.Now().own==Ready::ready && ValueOf(0,kKeyHave) && std::strstr(ValueOf(0,kKeyHave),";0;r"),
           "the host has them: ready");
    Expect(host.pending.empty() && host.starts==0,"the host neither notes nor generates what it has");
    Tick(client,1,now+3);
    Expect(ValueOf(1,kKeyConfig) && std::strlen(ValueOf(1,kKeyConfig))==kConfigHeader,"the client publishes its (empty) configuration too");
    Expect(client.pending==std::vector<std::wstring>({L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO",L"EDF6VC_LO_TANK_4000000000000C81.SGO"}),
           "the client notes exactly the two of the host's it lacks on its pending list");
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
    Expect(KeptLine(host,"PUID1").empty() && KeptLine(client,"PUID0")==LineOf(host),"each keeps the other's line for the plans");
    client.Finish();
    Tick(client,1,now+2000);
    Expect(client.engine.Now().own==Ready::ready && client.starts==1 && std::strstr(ValueOf(1,kKeyHave),";0;r"),"made: ready");
    lobby.member[2].present=false;   // the old member leaves
    Tick(host,0,now+2000);
    Expect(host.engine.State()==SyncState::ready && HasLine(host,L"All members ready (3 files)"),"everyone ready: said once");
    Expect(Lines(host).size()==1,"and only that");
    Tick(client,1,now+3000);
    Expect(client.engine.State()==SyncState::ready,"the client agrees");

    // The client configures its own support in the room (2026-11-21): the host generates the client's files too, by
    // its pending list (its own ini does not name them), and both report ready for the same, larger set.
    const std::wstring sniper=L"EDF6VC_NPC_SNIPER_X_FFFFFF.SGO";
    client.config={sniper};client.files.push_back(sniper);
    Tick(client,1,now+4000);
    Tick(host,0,now+5000);
    Expect(host.pending==std::vector<std::wstring>({sniper}) && host.starts==1,"the host notes the client's file and generates it");
    Expect(host.engine.Target().count==4 && KeptLine(host,"PUID1")==LineOf(client),"its room set holds it; the client's line kept");
    Expect(HasLine(host,L"This PC: 1 files missing, generating") && host.engine.State()==SyncState::waiting,
           "the host says it is making it (the client, which has it, is already ready for the new set)");
    host.Finish();
    Tick(host,0,now+6000);
    Tick(client,1,now+7000);
    Tick(host,0,now+8000);
    Expect(host.engine.State()==SyncState::ready && HasLine(host,L"All members ready (4 files)"),"made: everyone ready for the 4");
    Expect(client.pending.size()==2,"the client did not note its own file (its ini names it)");

    // The host changes its configuration: a new set; the client's readiness is about the old one until it rechecks.
    host.config.push_back(Name(Vehicle(VehicleBody::fighter,L"AAM_M:4,AAM_S:2")));
    const int hostStarts=host.starts;
    Tick(host,0,now+11000);
    Expect(HasLine(host,L"This PC: 1 files missing, generating") && host.starts==hostStarts+1 && host.pending.size()==1,
           "the host lacks its new file: its own generator, not its pending list (its ini names it)");
    Expect(HasLine(host,L"Member 2: checking the list"),"the client's readiness is the old set's: checking");
    client.generatorFails=true;
    Tick(client,1,now+12000);
    Expect(client.pending.size()==3 && client.starts==2,"the client notes the new one and starts its generator");
    client.Finish();
    Tick(client,1,now+13000);
    Tick(client,1,now+30000);
    Expect(client.starts==2 && client.engine.Now().own==Ready::failed,"failed: not retried for the same set");
    Expect(HasLine(client,L"This PC: 1 files missing, generation failed: FileNotFoundError: EDF6VC_JET_FIGHTER.SGO"),"the reason said");
    Tick(host,0,now+30000);
    Expect(HasLine(host,L"Member 2: 1 files missing, generation failed"),"the host sees it failed");

    // Leaving withdraws everything this machine published, and keeps the others' lines and the set for the mission.
    lobby.current=1;client.engine.Leave(&kFakeApi);
    Expect(lobby.member[1].values.empty(),"the client's keys withdrawn");
    lobby.current=0;host.engine.Leave(&kFakeApi);
    Expect(lobby.member[0].values.empty(),"the host's keys withdrawn");
    Expect(KeptLine(host,"PUID1")==LineOf(client),"the client's line kept for the mission's plans");
    Expect(keysSeen.size()<=kConfigParts+2,"the configuration, readiness and player loadouts fit Coop's 16-key limit");
    Expect(lobby.refused==0,"Coop never refused a key or value");
}

// Another member's line that does not read (Env::memberVariants: member_config.h refuses it): said, nothing noted from it,
// nothing kept for the plans (its calls use the defaults).
void BadMember() {
    ResetLobby(2);
    static Machine host;host=Machine{};
    host.config={L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"};host.files=host.config;
    lobby.current=1;
    ConfigValues v;EncodeConfig("BAD=..\\..\\Windows\\x",&v);
    Publish(kKeyConfig,v.value[0]);
    Tick(host,0,1000);
    Expect(host.pending.empty() && host.starts==0 && host.engine.Target().count==1,"a line that does not read: nothing noted, nothing started");
    Expect(HasLine(host,L"Member 2's loadout does not read: their calls use the defaults"),"said");
    Expect(KeptLine(host,"PUID1")=="-","and nothing kept for the plans");
    bool logged=false;for(const auto& t:host.log)logged|=t.find("configuration does not read")!=std::string::npos;
    Expect(logged,"logged");
    // A tampered value (hash wrong): the same.
    lobby.current=1;Publish(kKeyConfig,"1;0000000000000000;1;EDF6VC_NPC_SNIPER_X_FFFFFF.SGO");
    Tick(host,0,3000);
    Expect(host.pending.empty() && HasLine(host,L"Member 2's loadout does not read: their calls use the defaults"),"a tampered value: refused whole");
}

// A member's line over several parts, replaced part by part: waiting while incomplete, the last complete one standing.
void PartsInFlight() {
    ResetLobby(2);
    static Machine host;host=Machine{};
    std::string line;
    const VariantSet big=BigSet(60);
    std::vector<std::wstring> names;
    for(int i=0;i<big.count;++i){names.push_back(Name(big.ref[i]));if(i)line+=',';for(wchar_t c:names.back())line+=static_cast<char>(c);}
    ConfigValues v;
    Expect(EncodeConfig(line.c_str(),&v) && v.parts>1,"a 62-file line takes several parts");
    lobby.current=1;Publish(kKeyConfig,v.value[0]);   // the header first: its parts not there yet
    Tick(host,0,1000);
    Expect(host.engine.Now().list==ListState::waiting && HasLine(host,L"Waiting for the members' loadouts") && host.pending.empty(),
           "a line whose parts are on their way: waiting, nothing noted");
    lobby.current=1;
    for(int p=1;p<v.parts;++p){char key[33];PartKey(p,key,sizeof(key));Publish(key,v.value[p]);}
    Tick(host,0,3000);
    Expect(host.engine.Now().list==ListState::ok && static_cast<int>(host.pending.size())==big.count,"complete: read, every file noted");
    // Replaced: the new header before its parts: the last complete line stands meanwhile.
    std::string shorter=line+",EDF6VC_NPC_SNIPER_X_FFFFFF.SGO";
    ConfigValues w;EncodeConfig(shorter.c_str(),&w);
    lobby.current=1;Publish(kKeyConfig,w.value[0]);
    Tick(host,0,5000);
    Expect(host.engine.Now().list==ListState::ok && host.engine.Target().count==big.count && KeptLine(host,"PUID1")==line,
           "a line being replaced: the last complete one stands");
}

void LongList() {
    ResetLobby(2);
    static Machine host,client;host=Machine{};client=Machine{};
    const VariantSet big=BigSet(200);
    for(int i=0;i<big.count;++i)host.config.push_back(Name(big.ref[i]));
    host.files=host.config;
    host.line="BIG";client.lineSets["BIG"]=host.config;   // the line stands for 202 files (a full configuration's)
    Tick(host,0,1000);
    Tick(client,1,1000);
    Expect(client.engine.Now().list==ListState::ok && static_cast<int>(client.pending.size())==kNotesPerRoom,
           "the client reads the line and notes the first kNotesPerRoom (128) files: no more a room");
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
           "its readiness: 74 missing, failed (those calls come stock)");
    // A changed line in the same room: still no more than kNotesPerRoom notes this room.
    host.config.push_back(Name(Soldier(SupportWeapon::sniper,false,0x0A0B0C,kStockColour)));
    host.files=host.config;host.line="BIG2";client.lineSets["BIG2"]=host.config;
    Tick(host,0,22000);
    Tick(client,1,22000);
    Expect(static_cast<int>(client.pending.size())==kNotesPerRoom,"a changed line in the same room: the room's limit holds");
    lobby.current=1;client.engine.Leave(&kFakeApi);
    Tick(client,1,24000);
    Expect(static_cast<int>(client.pending.size())==kNotesPerRoom+75,"a new room (left and back): a new limit, the other 75 noted");
    // Too long a configuration: the writer leaves entries out, said.
    host.line.clear();host.config={L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"};host.leftOut=3;
    Tick(host,0,26000);
    Expect(HasLine(host,L"3 of this PC's loadout entries did not fit (too long): those calls use the defaults"),"entries left out: said");
}

// The room's set is what the next mission preloads (support_variants.h): kept when the room is left (a mission starting
// leaves it), replaced by the next room's complete tick, dropped by ForgetRoomList (the switch turned off). The host
// keeps one too now (it plans every member's calls with their files).
void MissionList() {
    ResetLobby(2);
    static Machine host,client;host=Machine{};client=Machine{};
    host.config={L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO",L"EDF6VC_LO_TANK_4000000000000C81.SGO"};host.files=host.config;
    client.config={L"EDF6VC_NPC_SNIPER_X_FFFFFF.SGO"};client.files=client.config;
    Tick(host,0,1000);Tick(client,1,1000);Tick(host,0,2000);
    Expect(host.engine.HasRoomList() && host.engine.RoomList().count==3 && host.engine.RoomListHash()!=0,
           "the host keeps the room's set: its own and the client's files");
    Expect(client.engine.HasRoomList() && client.engine.RoomList().count==3,"the client the same set");
    Expect(host.engine.RoomListHash()==client.engine.RoomListHash(),"the same hash on both");
    lobby.current=1;client.engine.Leave(&kFakeApi);
    Expect(client.engine.HasRoomList() && client.engine.RoomList().count==3 && KeptLine(client,"PUID0")==LineOf(host),
           "the room left (a mission starts): still there, the host's line too");
    client.engine.ForgetRoomList();
    Expect(!client.engine.HasRoomList() && client.engine.RoomList().count==0 && !client.engine.RoomListHash() &&
           KeptLine(client,"PUID0")=="-","forgotten on demand, the lines too");
    Tick(client,1,3000);
    Expect(client.engine.HasRoomList() && KeptLine(client,"PUID0")==LineOf(host),"the next tick in the room: back");
}

// Coop's 4096-byte budget (every value of this process), its error codes, an older table without lastError, and its
// pacing (values seen by the others only when the merged update goes out).
void Budget() {
    // The longest line, the readiness and the player loadouts beside it: within Coop's budget, nothing refused.
    ResetLobby(2);
    static Machine host;host=Machine{};
    host.line=std::string(kConfigLineMost,'P');
    host.ownPlayer="1;"+std::string(playerveh::kValueMost-2,'6');
    Tick(host,0,1000);
    std::size_t published=0;
    for(const auto& kv:lobby.member[0].values)published+=kv.second.size();
    Expect(lobby.refused==0 && published<=EDF6COOP_MEMBER_ATTRIBUTE_MAX_TOTAL_VALUE && ValueOf(0,kKeyHave) && ValueOf(0,"EDF6X_VCCFG3"),
           "the longest line in every part, its readiness and the player loadouts within Coop's budget");
    // Fewer: the parts no longer needed are withdrawn.
    host.line="short";
    Tick(host,0,3000);
    Expect(!ValueOf(0,"EDF6X_VCCFG1") && !ValueOf(0,"EDF6X_VCCFG3"),"a shorter line: the extra parts withdrawn");
    // Another plugin of this process takes most of the budget: our line is refused, said, logged, retried.
    lobby.current=0;
    host=Machine{};
    host.config={L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO"};host.files=host.config;
    lobby.member[0].values.clear();
    Publish("EDF6X_OTHER",std::string(999,'a').c_str());Publish("EDF6X_OTHER2",std::string(999,'b').c_str());
    Publish("EDF6X_OTHER3",std::string(999,'c').c_str());Publish("EDF6X_OTHER4",std::string(999,'d').c_str());Publish("EDF6X_OTHER5",std::string(90,'e').c_str());
    const int refusedBefore=lobby.refused;
    Tick(host,0,5000);
    Expect(lobby.refused>refusedBefore && !ValueOf(0,kKeyConfig),"over the budget: Coop refuses the line");
    Expect(HasLine(host,L"EDF6Coop refused this PC's pre-sync value (error 5; 5 = its 4096-byte budget is full): the others do not see it"),
           "said on a line with its error (never lost silently)");
    bool logged=false;for(const auto& t:host.log)logged|=t.find("refused EDF6X_VCCFG")!=std::string::npos && t.find("error 5")!=std::string::npos;
    Expect(logged,"and logged with the key and the error");
    lobby.current=0;Clear("EDF6X_OTHER");Clear("EDF6X_OTHER2");
    Tick(host,0,7000);
    Expect(ValueOf(0,kKeyConfig) && !HasLine(host,L"EDF6Coop refused this PC's pre-sync value (error 5; 5 = its 4096-byte budget is full): the others do not see it"),
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
    Expect(c.engine.Target().count==0 && c.pending.empty(),"the host's line not sent yet: nothing for the client");
    lobby.Send();
    Tick(c,1,2000);
    Tick(h,0,2000);
    Expect(c.engine.Now().list==ListState::ok && c.pending.size()==1,"sent: the client reads it");
    Expect(HasLine(h,L"Member 2: checking the list"),"the client's readiness that went out is from before the line: checking");
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

// The host's seat changing changes nothing: every member reads every member's line, whoever hosts.
void Migration() {
    ResetLobby(2);
    static Machine a,b;a=Machine{};b=Machine{};
    a.config={L"EDF6VC_NPC_LANCE_1E3A8A_X.SGO"};a.files=a.config;
    b.config={L"EDF6VC_NPC_SNIPER_X_FFFFFF.SGO"};b.files=b.config;
    Tick(a,0,1000);Tick(b,1,1000);Tick(a,0,2000);
    Expect(b.pending==std::vector<std::wstring>({L"EDF6VC_NPC_LANCE_1E3A8A_X.SGO"}) &&
           a.pending==std::vector<std::wstring>({L"EDF6VC_NPC_SNIPER_X_FFFFFF.SGO"}),"each notes the other's file");
    lobby.member[0].host=false;lobby.member[1].host=true;   // a leaves the host's seat to b
    Tick(a,0,3000);Tick(b,1,3000);
    Expect(ValueOf(0,kKeyConfig) && ValueOf(1,kKeyConfig) && a.engine.Target().count==2 && b.engine.Target().count==2,
           "both still publish and read each other's");
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
// Every member's own player loadouts (EDF6X_VCPVEH, player_vehicle_logic.h): published by each, read and validated by
// the others, their files generated like the host list's (the host too: they are not in its ini), kept for the mission.
void PlayerLoadouts() {
    ResetLobby(3);
    static Machine host,client,third;host=Machine{};client=Machine{};third=Machine{};
    VehicleLoadout own[kPlayerVehicleCount]{};
    wchar_t why[160];
    ParseVehicleLoadout(L"MK82:12,AAM_S:2",VehicleBody::pfighter,&own[0],why,160);
    char value[playerveh::kValueMost+1];
    Expect(playerveh::OwnValue(own,kPlayerVehicleCount,value,sizeof(value)),"the client's own value");
    client.ownPlayer=value;
    VariantRef mine;mine.vehicle=true;mine.sub=static_cast<std::uint8_t>(VehicleBody::pfighter);mine.bits=LoadoutVariant(own[0]);
    const std::wstring file=Name(mine);
    Expect(file.rfind(L"EDF6VC_LO_PFIGHTER_",0)==0,"its file: a loaded player fighter");
    client.files={file};   // its own (its ini names it: its generator made it)
    third.ownPlayer="1;6zzzz";   // junk from a member: ignored whole
    std::uint64_t now=500000;
    Tick(host,0,now);Tick(client,1,now+1);Tick(third,2,now+2);
    Expect(ValueOf(1,playerveh::kKey) && std::string(ValueOf(1,playerveh::kKey))==value,"the client publishes its value");
    Expect(!ValueOf(0,playerveh::kKey),"the host has none: publishes none");
    Tick(host,0,now+1000);
    Expect(host.engine.MemberPlayerVehicles().count==1 && host.engine.MemberPlayerVehicles().Has(mine),
           "the host reads the client's loaded jet (the junk member's skipped)");
    const std::uint64_t version=host.engine.MemberPlayersVersion();
    Expect(version!=0,"the set changing bumps its version (room_sync.cpp copies it for other threads then)");
    Expect(host.pending==std::vector<std::wstring>({file}) && host.starts==1,"the host notes the file and generates it (no ini of its own names it)");
    Expect(ValueOf(0,kKeyHave) && std::strstr(ValueOf(0,kKeyHave),";0;r"),"its readiness for the host list is not changed by it");
    bool logged=false;for(const auto& l:host.log)logged|=l.find("player loadouts do not read")!=std::string::npos;
    Expect(logged,"the bad value is logged once");
    Tick(client,1,now+1001);
    Expect(client.pending.empty() && client.engine.MemberPlayerVehicles().count==0,"the client does not note its own");
    host.Finish();
    Tick(host,0,now+2000);
    Expect(host.starts==1 && host.Has(file),"made, not generated again");
    Expect(host.engine.MemberPlayersVersion()==version && host.engine.MemberPlayerVehicles().Has(mine),
           "the same set read again: the version unchanged (nothing to copy)");
    // Kept for the mission once the room is left; the key withdrawn.
    lobby.current=1;client.engine.Leave(&kFakeApi);
    Expect(!ValueOf(1,playerveh::kKey),"the client's value withdrawn when it leaves");
    lobby.current=0;host.engine.Leave(&kFakeApi);
    Expect(host.engine.MemberPlayerVehicles().count==1,"the host keeps the room's player loadouts for the mission");
    // A member's loadout removed: withdrawn, the others stop listing it.
    ResetLobby(2);
    client.ownPlayer=value;
    Tick(client,1,now+5000);Tick(host,0,now+5001);
    Expect(host.engine.MemberPlayerVehicles().count==1,"listed again in a new room");
    const std::uint64_t listedAgain=host.engine.MemberPlayersVersion();
    client.ownPlayer.clear();
    Tick(client,1,now+7000);Tick(host,0,now+7001);
    Expect(!ValueOf(1,playerveh::kKey) && host.engine.MemberPlayerVehicles().count==0,"none any more: withdrawn, no longer listed");
    Expect(host.engine.MemberPlayersVersion()!=listedAgain,"emptied: the version bumped, the copy emptied too");
}
}  // namespace

int main() {
    NamesAndTokens();
    ConfigCoding();
    HaveAndPlan();
    Rooms();
    BadMember();
    PartsInFlight();
    LongList();
    MissionList();
    Budget();
    Migration();
    NoCoop();
    PlayerLoadouts();
    std::printf("room_sync: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
