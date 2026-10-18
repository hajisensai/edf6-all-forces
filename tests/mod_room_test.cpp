// Production EOS wrappers against recording endpoints, plus the original game's
// search ranges/invitation predicate in a private DLL mapping when supplied.
#include "../src/mod_room.cpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <cstdarg>
#include <map>
namespace crew { unsigned char* image=nullptr; std::string lastLog; void Log(const char* format,...) noexcept {char text[1024]{};va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);lastLog=text;}
// This machine's installed packs (src/edf5campaign.cpp in the plugin): the fixture's table.
packs::Table localPacks{};
const packs::Pack* InstalledMissionPack(std::int64_t id) noexcept { return localPacks.Find(id); } }
namespace {
int failures=0,joins=0,releases=0,result=-999,dependencyMode=5,notices=0,worldQueries=0,rawResult=0,roomReleases=0,packNotices=0;
bool rawAttribute=true;
std::wstring depNotice,packNotice;
std::vector<crew::Data> writes;
std::vector<std::string> texts;   // the string attributes published, copied while the call lasts (EOS copies them)
crew::Data value{1,"SEARCH_TYPE",0x93,1};
crew::Attribute attribute{1,&value,0};
// The room's other attributes as EOS hands them to CopyAttributeByKey (DLC, AF_PACK); SEARCH_TYPE is `attribute`.
std::map<std::string,crew::Data> room;
std::map<std::string,std::string> roomText;
crew::Attribute roomAttribute{1,nullptr,0};
crew::Data roomData{};
void Check(bool ok,const char* text){if(!ok){std::printf("FAIL %s\n",text);++failures;}}
int RecordPut(void*,const crew::Options* options){
    writes.push_back(*options->data);
    if(options->data->type==3)texts.push_back(options->data->text ? options->data->text : "");
    return 0;
}
int RecordCopy(void*,const crew::IndexOptions*,crew::Attribute** out){*out=&attribute;return 0;}
int RecordRaw(void*,const crew::KeyOptions* key,crew::Attribute** out){
    if(!std::strcmp(key->key,"SEARCH_TYPE")){*out=rawAttribute ? &attribute : nullptr;return rawResult;}
    const auto at=room.find(key->key);
    if(at==room.end()){*out=nullptr;return 13;}   // EOS_NotFound
    roomData=at->second;
    if(roomData.type==3)roomData.text=roomText[key->key].c_str();
    roomAttribute.data=&roomData;*out=&roomAttribute;
    return 0;
}
void RecordRelease(crew::Attribute* a){if(a==&roomAttribute)++roomReleases;else ++releases;}
void RecordJoined(void*,const crew::JoinOptions*,void*,crew::Callback){++joins;}
void RecordCallback(const crew::Info* info){result=info->result;Check(info->client==reinterpret_cast<void*>(9),"refusal preserves client data");}
}
namespace {
uint32_t DummySnapshot(EDF6CoopSnapshot*){++worldQueries;return 0;}
uint32_t DummyPeer(uint64_t,uint32_t,EDF6CoopPeer*){++worldQueries;return 0;}
uint32_t DummySend(uint64_t,const EDF6CoopPeer*,const void*,uint32_t){return 0;}
uint32_t DummyPoll(uint64_t,EDF6CoopPeer*,void*,uint32_t,uint32_t*){return 0;}
uint32_t BridgeApi(uint32_t version,uint32_t size,EDF6CoopExtensionApi* out){
    Check(version==1 && size==sizeof(*out),"menu requests extension ABI v1");
    if(dependencyMode==1)return 0;
    *out={sizeof(*out),1,&DummySnapshot,&DummyPeer,&DummySend,&DummyPoll};
    if(dependencyMode==4)out->poll=nullptr;
    return 1;
}
uint32_t Admission(){return dependencyMode==3 ? 0u : 1u;}
uint32_t Identity(int32_t,EDF6CoopPeer*){++worldQueries;return 0;}
FARPROC ResolveDependency(const char* name) noexcept {
    if(dependencyMode==0)return nullptr;
    if(!std::strcmp(name,"EDF6CoopGetExtensionApi"))return reinterpret_cast<FARPROC>(&BridgeApi);
    if(!std::strcmp(name,"EDF6Coop_MissionAdmissionReady"))return reinterpret_cast<FARPROC>(&Admission);
    if(!std::strcmp(name,"EDF6Coop_ResolveMissionPlayerPuid"))return dependencyMode==2 ? nullptr : reinterpret_cast<FARPROC>(&Identity);
    // Only looked up (never called) by the room check: any non-null address stands for the export.
    if(!std::strcmp(name,"EDF6CoopGetMemberAttributeApi"))return dependencyMode==6 ? nullptr : reinterpret_cast<FARPROC>(&Admission);
    return nullptr;
}
void RecordNotice(const wchar_t* text) noexcept {++notices;depNotice=text;}
std::wstring packNoticeTitle;
void RecordPackNotice(const wchar_t* title,const wchar_t* text) noexcept {++packNotices;packNoticeTitle=title;packNotice=text;}
std::uint64_t fakeNow=1000;
std::uint64_t FakeNow() noexcept {return fakeNow;}
void RoomContent(std::int64_t dlc){room["DLC"]=crew::Data{1,"DLC",{dlc},1};}
void RoomPack(const char* line){
    if(!line){room.erase("AF_PACK");return;}
    room["AF_PACK"]=crew::Data{1,"AF_PACK",{0},3};roomText["AF_PACK"]=line;
}
void RecordCreated(void*,const void*,void*,crew::Callback){++joins;}
}
// The mission pack identity (mission_packs.h): the host's DLC write carries AF_PACK on one of our packs; a guest is
// let through only with the same definition and every row the host can choose, refused before JoinLobby otherwise
// (with a dialog naming the pack), and stock / other mods' rooms are untouched.
void PackIdentity(crew::Put publish,crew::Join enter){
    using namespace crew;
    ready.store(true);dependencyMode=5;
    packs::Pack story{},dlc{};
    Check(packs::Make(69,"EDF41","bc133623395bd534",9,"000",story),"a 4.1 story pack, nothing missing");
    Check(packs::Make(65,"EDF5","1a74064786830772",5,"00",dlc),"an EDF5 story pack");
    localPacks=packs::Table{};localPacks.packs[69-packs::kBase]=story;localPacks.packs[65-packs::kBase]=dlc;
    // host: the content id's write publishes the pack's line beside it, the same visibility
    char line[packs::kLineSize]{};
    Check(packs::Format(story,line) && std::string(line)=="1;69;EDF41;bc133623395bd534;9;000","the line's form");
    Data content{1,"DLC",{69},1};const Options options{2,&content,0};
    writes.clear();texts.clear();
    Check(publish(nullptr,&options)==0 && writes.size()==2,"pack room: DLC and AF_PACK published");
    Check(writes.size()==2 && !std::strcmp(writes[0].key,"DLC") && writes[0].value==69 && !std::strcmp(writes[1].key,"AF_PACK")
          && writes[1].type==3 && texts.size()==1 && texts[0]==line,"AF_PACK is this machine's line for the room's pack");
    for(const std::int64_t stock:{0,1,2,7}) {   // EDF6's own contents, another mod's id: nothing added
        content.value=stock;writes.clear();
        Check(publish(nullptr,&options)==0 && writes.size()==1 && writes[0].value==stock,"stock / other content: DLC alone");
    }
    content.value=70;writes.clear();
    Check(publish(nullptr,&options)==0 && writes.size()==1,"a pack not installed here: no identity to publish");
    Check(lastLog.find("has not installed")!=std::string::npos,"...and logged");
    // guest
    const JoinOptions jo{4,reinterpret_cast<void*>(1),nullptr};
    value.value=kAllForcesRoomPrefix+0x93;rawResult=0;rawAttribute=true;
    const auto Join=[&](const char* why,bool admitted,int wantNotices) {
        const int before=joins,notes=packNotices;result=-999;
        enter(nullptr,&jo,reinterpret_cast<void*>(9),&RecordCallback);
        Check(admitted ? joins==before+1 && result==-999 : joins==before && result==10,why);
        Check(packNotices==notes+wantNotices,why);
    };
    RoomContent(69);RoomPack(line);
    Join("same pack definition, every row here: native join",true,0);
    Check(lastLog.find("join allowed: route=eos")!=std::string::npos,"the admitted join is logged as before");
    RoomPack("1;69;EDF41;0000000000000000;9;000");
    Join("another definition (another version): refused before JoinLobby",false,1);
    Check(lastLog.find("reason=pack-definition-differs")!=std::string::npos && packNotice.find(L"EDF4.1 本篇")!=std::wstring::npos
          && packNotice.find(L"版本不同")!=std::wstring::npos,"the dialog names the pack and the difference");
    RoomPack("1;69;EDF41;bc133623395bd534;8;00");
    Join("another row count: refused",false,1);
    RoomPack(nullptr);
    Join("an older host without the identity: refused",false,1);
    Check(lastLog.find("reason=pack-identity-missing")!=std::string::npos,"identity missing has its own reason");
    RoomPack("1;65;EDF5;1a74064786830772;5;00");
    Join("the line of another pack than the room's id: refused",false,1);
    RoomPack("garbage");
    Join("a malformed line: refused",false,1);
    // rows: the guest needs every row the host can choose; the host lacking more is fine
    localPacks.packs[69-packs::kBase].missing[0]=0x24;   // rows 2 and 5 are placeholders here
    RoomPack(line);
    Join("rows the host can choose are placeholders here: refused",false,1);
    Check(lastLog.find("reason=pack-rows-missing-here")!=std::string::npos && lastLog.find("missing_here=2 first=2")!=std::string::npos
          && packNotice.find(L"2 关缺资源（第 3 关等）")!=std::wstring::npos,"how many rows, and the first one, said");
    RoomPack("1;69;EDF41;bc133623395bd534;9;421");   // the host lacks rows 2, 5 and 8
    Join("the host lacks those rows and more: admitted",true,0);
    localPacks.packs[69-packs::kBase]=story;
    RoomContent(70);RoomPack("1;70;EDF41MP01;b76b4f5bf6906b9d;26;0000000");
    Join("a pack not installed here (past the native ownership check): refused",false,1);
    Check(packNotice.find(L"本机没有安装")!=std::wstring::npos,"not installed is said so");
    for(const std::int64_t stock:{0,1,2,7}) {
        RoomContent(stock);RoomPack(nullptr);
        Join("stock content (and other mods' ids) is none of this check's business",true,0);
    }
    room.erase("DLC");
    Join("a room whose content id cannot be read: refused",false,1);
    Check(lastLog.find("reason=content-read-failed")!=std::string::npos && packNotice.find(L"无法读取房间的任务包")!=std::wstring::npos,
          "...with its own reason and dialog");
    room["DLC"]=crew::Data{1,"DLC",{0},2};   // not an int64
    Join("a content id of another type: refused the same way",false,1);
    Check(lastLog.find("reason=content-read-failed")!=std::string::npos,"...with the same reason");
    RoomContent(0);
    // EDF6Coop's remembered rooms: the export answers the same, and a refusal shows the EOS route's dialog (on the
    // dialog thread in the game; the recorder here), not again for an unchanged refusal within a minute (Coop asks
    // at every room search), again after that and for another refusal
    fakeNow+=61000;   // past the minute of the same refusals the EOS joins above showed (one message, either route)
    int notes=packNotices;
    Check(EDF6AF_RoomPackCompatible(69,line)==1 && EDF6AF_RoomPackCompatible(0,nullptr)==1 && packNotices==notes,
          "export: same pack / stock, no dialog");
    Check(EDF6AF_RoomPackCompatible(69,nullptr)==0 && packNotices==notes+1 && packNotice.find(L"没有发布任务包身份")!=std::wstring::npos
          && packNoticeTitle==L"全军出击：无法加入联机房间","export refusal: the same dialog as the EOS route");
    Check(lastLog.find("ROOM pack check (EDF6Coop): refused reason=pack-identity-missing content=69")!=std::string::npos,"...logged");
    Check(EDF6AF_RoomPackCompatible(69,nullptr)==0 && packNotices==notes+1,"the same refusal again at once: no second dialog");
    Check(lastLog.find("already shown")!=std::string::npos,"...said in the log");
    fakeNow+=59000;
    Check(EDF6AF_RoomPackCompatible(69,nullptr)==0 && packNotices==notes+1,"...nor within the minute");
    fakeNow+=2000;
    Check(EDF6AF_RoomPackCompatible(69,nullptr)==0 && packNotices==notes+2,"after it: shown again");
    Check(EDF6AF_RoomPackCompatible(69,"1;69;EDF41;0000000000000000;9;000")==0 && packNotices==notes+3
          && packNotice.find(L"版本不同")!=std::wstring::npos,"another refusal: its own dialog");
    Check(EDF6AF_RoomPackCompatible(71,nullptr)==0 && packNotices==notes+4 && packNotice.find(L"本机没有安装")!=std::wstring::npos,
          "not installed: its own dialog");
    // the EOS route's refusals are the player's own tries: each shown (the last one closed), whatever the minute
    RoomContent(69);RoomPack(nullptr);
    Join("an older host, twice in a row",false,1);
    Join("...each try its dialog",false,1);
    RoomContent(0);
    // the wire's mask round trip and bounds
    packs::Pack big{};
    char mask[129]{};
    for(int i=0;i<128;++i)mask[i]=i==127 ? 'f' : '0';
    Check(packs::Make(65,"EDF5","1a74064786830772",512,mask,big) && big.Missing(511) && big.Missing(508) && !big.Missing(507)
          && big.MissingCount()==4,"512 rows, the last four missing");
    char wire[packs::kLineSize]{};
    packs::Pack back{};
    Check(packs::Format(big,wire) && packs::Parse(wire,back) && back.MissingCount()==4 && back.Missing(511),"the longest line round-trips");
    Check(!packs::Make(65,"EDF5","1a74064786830772",5,"08",big),"a missing bit past the last row is refused");
    Check(!packs::Make(65,"EDF5","1a74064786830772",5,"0",big) && !packs::Make(65,"EDF5","1a7406478683077",5,"00",big)
          && !packs::Make(64,"EDF5","1a74064786830772",5,"00",big) && !packs::Make(65,"ED;5","1a74064786830772",5,"00",big),
          "wrong mask length, short digest, an id not ours, a separator in the tag: refused");
    // the mission select's rows: placeholders shown locked (3), the rest left to the scan
    std::vector<ported::ListRow> rows(6);
    for(std::size_t i=0;i<rows.size();++i){rows[i]=ported::ListRow{};rows[i].row=static_cast<std::int32_t>(i);rows[i].state=i ? 4 : 1;}
    packs::Pack held{};
    Check(packs::Make(66,"EDF5DLC1","557e4c47c116ff41",6,"22",held),"rows 1 and 5 placeholders");
    Check(packs::LockPlaceholders(rows.data(),rows.size(),held)==2 && rows[1].state==3 && rows[5].state==3 && rows[0].state==1
          && rows[2].state==4,"placeholders locked, the others as the select left them");
}
// A save marked with its layout and moved to the rows now, once, decided by the save itself (mission_packs.h
// MigrateModeSave, the save as the game holds it at GS+0x57A4: +0 current row, +8 clear[10][512], +0x14A0 a byte per
// row, the marker at row 508 of that block).
void SaveMigration(){
    using namespace crew::packs;
    Pack p{};
    Check(Make(69,"EDF41","bc133623395bd534",6,"00",p) && p.layout==0,"a pack's identity carries no save layout");
    Check(SetLayout(p,"3e12cdf5",1) && p.layout==0x3e12cdf5u && p.stableRows==1,"Layout and StableRows");
    Check(AddFrom(p,0,"4:0,2,3,5") && p.fromCount==1 && p.From(0) && p.From(0)->rows==4 && p.From(0)->row[1]==2,"Legacy parsed");
    Pack bad=p;
    Check(!AddFrom(bad,0,"4:0,2,3,5") && !AddFrom(bad,0x11,"4:0,2,3") && !AddFrom(bad,0x11,"3:0,2,6")
          && !AddFrom(bad,0x11,"2:0,-2") && !AddFrom(bad,0x11,"x") && !AddFrom(bad,p.layout,"1:0") && bad.fromCount==1
          && AddFrom(bad,0x11,"") && bad.fromCount==1,
          "a layout twice, a count off, a row past the pack, a bad row, the pack's own layout: refused; empty: none");
    Check(AddFrom(bad,0x11,"1:0") && AddFrom(bad,0x12,"1:0") && AddFrom(bad,0x13,"1:0") && !AddFrom(bad,0x14,"1:0")
          && bad.fromCount==kMaxFrom,"at most kMaxFrom layouts");
    Check(!SetLayout(bad,"00000000",0) && !SetLayout(bad,"3e12cdf",0) && !SetLayout(bad,"3e12cdfg",0)
          && !SetLayout(bad,"3e12cdf5",7) && !SetLayout(bad,"3e12cdf5",-1) && !SetLayout(bad,"00000011",0)
          && bad.layout==0x3e12cdf5u,"a zero, short, non-hex layout, stable rows past the pack, an older one's id: refused");
    std::vector<unsigned char> save(kModeSaveSize);
    unsigned char* const s=save.data();
    const auto clear=[](std::vector<unsigned char>& v,int c,int r)->unsigned char&{
        return v[kClear+static_cast<std::size_t>(c)*512+static_cast<std::size_t>(r)];};
    const auto marker=[](const std::vector<unsigned char>& v){std::uint32_t m=0;std::memcpy(&m,v.data()+kMarker,4);return m;};
    // the old save: rows 0..3 were M001, M002, M004, M006 (M003 and M005 left out); class 0 cleared all four, class 3
    // row 1 on two difficulties; the per-row byte of old row 2; the current row old 3
    for(int r=0;r<4;++r)clear(save,0,r)=1;
    clear(save,3,1)=3;s[kRowBytes+2]=4;s[0]=3;
    Check(HighestUsedRow(s)==3,"the highest row with anything: 3");
    Check(MigrateModeSave(s,p)==Migration::kRemapped,"unmarked, past the stable rows, fitting this machine's record: moved");
    Check(clear(save,0,0)==1 && clear(save,0,1)==0 && clear(save,0,2)==1 && clear(save,0,3)==1 && clear(save,0,4)==0
          && clear(save,0,5)==1,"class 0's clears on their missions");
    Check(clear(save,3,2)==3 && clear(save,3,1)==0,"class 3's clear moved with its mission");
    Check(s[kRowBytes+3]==4 && s[kRowBytes+2]==0 && s[0]==5,"the per-row byte and the current row moved");
    Check(marker(save)==p.layout,"marked with the layout");
    const std::vector<unsigned char> once=save;
    Check(MigrateModeSave(s,p)==Migration::kAlready && save==once,"a marked save is never moved again");
    // an unmarked save the record does not fit (a row past its four): which rows it had cannot be told
    std::vector<unsigned char> past(kModeSaveSize);
    clear(past,0,0)=1;clear(past,0,4)=1;
    const std::vector<unsigned char> pastBefore=past;
    Check(MigrateModeSave(past.data(),p)==Migration::kUndetermined && past==pastBefore && marker(past)==0,
          "unmarked, past the record's rows: left exactly as it is, unmarked");
    // no record at all (an older install uninstalled before this one): only a save within the stable rows is marked
    Pack plain{};
    Check(Make(65,"EDF5","1a74064786830772",5,"00",plain) && SetLayout(plain,"a1c87b26",2),"a pack without Legacy");
    std::vector<unsigned char> fresh(kModeSaveSize);
    Check(MigrateModeSave(fresh.data(),plain)==Migration::kMarked && marker(fresh)==0xa1c87b26u,"a new save: marked");
    std::vector<unsigned char> early(kModeSaveSize);
    clear(early,0,0)=1;clear(early,2,1)=1;early[0]=1;early[kRowBytes+1]=2;
    std::vector<unsigned char> earlyBefore=early;
    Check(MigrateModeSave(early.data(),plain)==Migration::kMarked && clear(early,0,0)==1 && clear(early,2,1)==1 && early[0]==1
          && early[kRowBytes+1]==2,"progress within the stable rows: marked, nothing moved");
    std::memcpy(early.data()+kMarker,earlyBefore.data()+kMarker,4);
    Check(early==earlyBefore,"...nothing but the marker written");
    std::vector<unsigned char> late(kModeSaveSize);
    clear(late,0,0)=1;clear(late,0,1)=1;clear(late,0,2)=1;
    const std::vector<unsigned char> lateBefore=late;
    Check(MigrateModeSave(late.data(),plain)==Migration::kUndetermined && late==lateBefore,
          "progress past the stable rows without a record: left as it is, unmarked (no guess made permanent)");
    std::vector<unsigned char> current(kModeSaveSize);
    current[0]=3;
    Check(MigrateModeSave(current.data(),plain)==Migration::kUndetermined,"the current row counts as progress too");
    // marked by an older release's layout: moved straight to the rows now by that layout's own map, whichever it is
    Pack chain{};
    Check(Make(66,"EDF5DLC1","557e4c47c116ff41",6,"00",chain) && SetLayout(chain,"0000c3c3",0)
          && AddFrom(chain,0xa1a1a1a1u,"4:0,1,3,5") && AddFrom(chain,0xb2b2b2b2u,"5:0,2,1,3,5"),"two older released layouts");
    std::vector<unsigned char> v1(kModeSaveSize),v2(kModeSaveSize);
    const std::uint32_t l1=0xa1a1a1a1u,l2=0xb2b2b2b2u;
    std::memcpy(v1.data()+kMarker,&l1,4);std::memcpy(v2.data()+kMarker,&l2,4);
    clear(v1,0,2)=1;clear(v1,0,3)=2;v1[0]=3;
    clear(v2,0,1)=1;clear(v2,0,2)=4;v2[0]=2;
    Check(MigrateModeSave(v1.data(),chain)==Migration::kRemapped && clear(v1,0,3)==1 && clear(v1,0,5)==2 && clear(v1,0,2)==0
          && v1[0]==5 && marker(v1)==0xc3c3u,"the first layout's save: by its map, marked now");
    Check(MigrateModeSave(v2.data(),chain)==Migration::kRemapped && clear(v2,0,2)==1 && clear(v2,0,1)==4 && v2[0]==1
          && marker(v2)==0xc3c3u,"the second layout's save: by its own map");
    Check(MigrateModeSave(v1.data(),chain)==Migration::kAlready,"...and never again");
    std::vector<unsigned char> overfull(kModeSaveSize);
    std::memcpy(overfull.data()+kMarker,&l1,4);
    clear(overfull,0,4)=1;   // a row the first layout did not have
    const std::vector<unsigned char> overBefore=overfull;
    Check(MigrateModeSave(overfull.data(),chain)==Migration::kForeign && overfull==overBefore,"a save past its layout's rows: untouched");
    std::vector<unsigned char> foreign=once;
    const std::uint32_t other=0x1234;std::memcpy(foreign.data()+kMarker,&other,4);
    const std::vector<unsigned char> foreignBefore=foreign;
    Check(MigrateModeSave(foreign.data(),p)==Migration::kForeign && foreign==foreignBefore,"a layout without a map: left as it is");
    std::vector<unsigned char> dropped(kModeSaveSize);
    Pack gone=p;
    gone.fromCount=0;
    Check(AddFrom(gone,0,"2:-1,1"),"a row no longer in the pack");
    gone.stableRows=0;
    dropped[kClear]=1;dropped[kClear+1]=2;dropped[0]=0;
    Check(MigrateModeSave(dropped.data(),gone)==Migration::kRemapped && dropped[kClear]==0 && dropped[kClear+1]==2 && dropped[0]==0,
          "its clear dropped, the current row back to 0");
    Pack big{};
    char mask[129]{};for(int i=0;i<128;++i)mask[i]='0';
    Check(Make(65,"EDF5","1a74064786830772",512,mask,big) && SetLayout(big,"0000abcd",0) && MigrateModeSave(fresh.data(),big)==Migration::kNone,
          "a pack with more rows than the marker leaves room for: untouched");
    Pack none{};
    Check(Make(65,"EDF5","1a74064786830772",5,"00",none) && MigrateModeSave(fresh.data(),none)==Migration::kNone,"no layout: untouched");
}
int main(int argc,char** argv){
    using namespace crew;
    resolveCoop=&ResolveDependency;showDependencyProblem=&RecordNotice;crew::notice::show=&RecordPackNotice;crew::notice::nowMs=&FakeNow;create=&RecordCreated;
    RoomContent(0);   // every room the game publishes has its content id: EDF6's story here
    add=&RecordPut;filter=&RecordPut;copy=&RecordCopy;rawCopy=&RecordRaw;release=&RecordRelease;join=&RecordJoined;
    const JoinOptions jo{4,reinterpret_cast<void*>(1),nullptr};
    const IndexOptions index{1,0};
    const auto native=argc>1 && *argv[1] ? LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES) : nullptr;
    using Ranges=std::uint64_t(*)(int);
    using Accept=bool(*)(std::int64_t);
    const auto base=reinterpret_cast<unsigned char*>(native);
    if(base){
        const auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        const auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
        Check(nt->FileHeader.TimeDateStamp==0x678CCB46,"native profile");
        if(failures)return 1;
    }
    ready.store(true);
    Put publish=&Add,search=&Filter;
    crew::Copy read=&CopyAttribute;
    crew::Join enter=&JoinRoom;
    if(base){
        image=base;
        const char* names[]={"EOS_LobbyModification_AddAttribute","EOS_LobbySearch_SetParameter","EOS_LobbyDetails_CopyAttributeByIndex","EOS_Lobby_JoinLobby","EOS_Lobby_CreateLobby"};
        void* recorders[]={reinterpret_cast<void*>(&RecordPut),reinterpret_cast<void*>(&RecordPut),reinterpret_cast<void*>(&RecordCopy),reinterpret_cast<void*>(&RecordJoined),reinterpret_cast<void*>(&RecordCreated)};
        for(unsigned i=0;i<5;++i){auto slot=Slot(names[i]);Check(slot!=nullptr,"real EDF import exists");
            if(slot)Check(edf::PatchVtableSlot(slot,*slot,recorders[i]),"recording endpoint installed in private IAT");}
        Check(InstallHooks(),"production isolation installs into real EDF IAT");
        publish=reinterpret_cast<Put>(*Slot(names[0]));search=reinterpret_cast<Put>(*Slot(names[1]));
        read=reinterpret_cast<crew::Copy>(*Slot(names[2]));enter=reinterpret_cast<crew::Join>(*Slot(names[3]));
    }
    for(const auto type:{0x91,0x92,0x93,0x94,0x12,0x13,0x14,0x15}) {
        value.value=type;const Options options{2,&value,0};writes.clear();
        Check(publish(nullptr,&options)==0 && writes.size()==2,"publish marker and native search field");
        const auto wire=writes.back().value;
        Check(wire==kAllForcesRoomPrefix+type && value.value==type,"outgoing type encoded without changing caller memory");
        Check(writes.front().value==1 && !std::strcmp(writes.front().key,"AF_PROFILE"),"persistent profile marker");
        for(const auto pair:{std::pair<std::int64_t,std::int64_t>{0x91,0x94},{0x12,0x94}}){
            writes.clear();Data lo{1,"SEARCH_TYPE",pair.first,1},hi{1,"SEARCH_TYPE",pair.second,1};
            const Options low{1,&lo,3},high{1,&hi,2};
            search(nullptr,&low);search(nullptr,&high);
            Check(wire>=writes[0].value && wire<=writes[1].value ? type>=pair.first && type<=pair.second : type<pair.first || type>pair.second,"native range partition matches capacity family");
            Check(!(wire>=pair.first && wire<=pair.second),"unmodded query excludes mod host");
            Check(!(type>=writes[0].value && type<=writes[1].value),"mod query excludes unmodded host");
        }
        value.value=wire;Attribute* output=nullptr;read(nullptr,&index,&output);
        Check(value.value==type,"game reads original room kind");
        value.value=wire;int before=joins;enter(nullptr,&jo,reinterpret_cast<void*>(9),&RecordCallback);
        Check(joins==before+1,"same profile reaches native join");
        Check(lastLog.find("join allowed: route=eos SEARCH_TYPE=")!=std::string::npos,"EOS handoff is visible");
        value.value=type;before=joins;result=-999;enter(nullptr,&jo,reinterpret_cast<void*>(9),&RecordCallback);
        Check(joins==before && result==10,"invited incompatible profile refused before native join");
        Check(lastLog.find("reason=incompatible-search-type")!=std::string::npos,"profile refusal has a distinct reason");
        if(base){
            const auto accept=reinterpret_cast<Accept>(base+0x749AC0);
            Check(!accept(wire),"real vanilla invitation predicate refuses All Forces");
            for(int kind=0;kind<4;++kind){const auto range=reinterpret_cast<Ranges>(base+0x74AC50)(kind);
                Check(!(wire>=static_cast<std::uint32_t>(range) && wire<=static_cast<std::uint32_t>(range>>32)),"real vanilla search range excludes All Forces");}
        }
    }
    value.value=kAllForcesRoomPrefix+0x100+0x93;const auto before=joins;
    enter(nullptr,&jo,reinterpret_cast<void*>(9),&RecordCallback);
    Check(joins==before,"unknown future profile refused");
    Check(releases==17,"copied EOS attributes released on accept and refusal");
    rawResult=4;const auto beforeReadFailure=joins;
    enter(nullptr,&jo,reinterpret_cast<void*>(9),&RecordCallback);
    Check(joins==beforeReadFailure && result==10 && lastLog.find("reason=search-type-read-failed")!=std::string::npos
          && lastLog.find("attribute_result=4")!=std::string::npos,"SDK attribute error is separate from profile mismatch");
    rawResult=0;rawAttribute=false;
    enter(nullptr,&jo,reinterpret_cast<void*>(9),&RecordCallback);
    Check(joins==beforeReadFailure && lastLog.find("reason=search-type-invalid")!=std::string::npos,"missing attribute diagnosed");
    rawAttribute=true;
    Data other{1,"DIFFICULTY",2,1};const Options otherOptions{2,&other,0};writes.clear();publish(nullptr,&otherOptions);
    Check(writes.size()==1 && writes[0].value==2,"unrelated metadata untouched");
    // Both native entry points must reject missing/old/incomplete/uninitialized
    // Coop while a matching bridge is usable in the menu (world still absent).
    const auto make=base ? reinterpret_cast<crew::Create>(*Slot("EOS_Lobby_CreateLobby")) : &CreateRoom;
    for(int mode:{0,1,2,3,4,6}){   // 6: an EDF6Coop without the member attributes (the EDF4.1 outfits' sync)
        dependencyMode=mode;Check(!EDF6AF_RoomIsolationReady(),"Coop virtual admission sees failed online prerequisites");
        const auto oldJoins=joins,oldNotices=notices;result=-999;
        make(nullptr,nullptr,reinterpret_cast<void*>(9),&RecordCallback);
        Check(joins==oldJoins && result==10 && notices==oldNotices+1 && !depNotice.empty(),"dependency failure refuses create with visible explanation");
        value.value=kAllForcesRoomPrefix+0x93;result=-999;
        enter(nullptr,&jo,reinterpret_cast<void*>(9),&RecordCallback);
        Check(joins==oldJoins && result==10 && notices==oldNotices+2,"dependency failure refuses join with visible explanation");
        Check(crew::lastLog.find("matching EDF6Coop required")!=std::string::npos,"dependency failure logged explicitly");
    }
    dependencyMode=0;Check(!EDF6AF_RoomIsolationReady(),"AF loaded before Coop refuses until dependency appears");
    dependencyMode=5;Check(EDF6AF_RoomIsolationReady(),"late Coop load is re-evaluated without cached refusal");
    dependencyMode=3;Check(!EDF6AF_RoomIsolationReady(),"Coop loaded before admission hook cannot permit a room yet");
    dependencyMode=5;Check(EDF6AF_RoomIsolationReady(),"late admission initialization unlocks without mission readiness");
    const auto beforeMenu=joins;
    make(nullptr,nullptr,reinterpret_cast<void*>(9),&RecordCallback);
    value.value=kAllForcesRoomPrefix+0x93;enter(nullptr,&jo,reinterpret_cast<void*>(9),&RecordCallback);
    Check(joins==beforeMenu+2,"matching installed Coop permits menu create and join");
    Check(worldQueries==0,"menu gate never queries world readiness or mission identity");
    ready.store(false);result=-999;
    const auto deniedBefore=joins;
    CreateRoom(nullptr,nullptr,reinterpret_cast<void*>(9),&RecordCallback);
    Check(joins==deniedBefore && result==10,"failed namespace setup blocks room creation");
    result=-999;value.value=kAllForcesRoomPrefix+0x93;
    enter(nullptr,&jo,reinterpret_cast<void*>(9),&RecordCallback);
    Check(joins==deniedBefore && result==10,"failed namespace setup blocks invitations");
    Check(lastLog.find("reason=namespace-not-ready")!=std::string::npos,"incomplete namespace setup has its own diagnostic");
    Check(roomReleases>0,"every EOS join read the room's content id, and released it");
    PackIdentity(publish,enter);
    SaveMigration();
    std::printf("mod room wrappers: %s; real EDF search/invite: %s\n",failures?"FAIL":"PASS",native?"executed":"not supplied");
    if(argc>1 && *argv[1] && !native)return 1;
    return failures?1:0;
}
