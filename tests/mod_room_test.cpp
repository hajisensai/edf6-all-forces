// Production EOS wrappers against recording endpoints, plus the original game's
// search ranges/invitation predicate in a private DLL mapping when supplied.
#include "../src/mod_room.cpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <cstdarg>
namespace crew { unsigned char* image=nullptr; std::string lastLog; void Log(const char* format,...) noexcept {char text[1024]{};va_list args;va_start(args,format);vsnprintf(text,sizeof(text),format,args);va_end(args);lastLog=text;} }
namespace {
int failures=0,joins=0,releases=0,result=-999,dependencyMode=5,notices=0,worldQueries=0;
std::wstring notice;
std::vector<crew::Data> writes;
crew::Data value{1,"SEARCH_TYPE",0x93,1};
crew::Attribute attribute{1,&value,0};
void Check(bool ok,const char* text){if(!ok){std::printf("FAIL %s\n",text);++failures;}}
int RecordPut(void*,const crew::Options* options){writes.push_back(*options->data);return 0;}
int RecordCopy(void*,const crew::IndexOptions*,crew::Attribute** out){*out=&attribute;return 0;}
int RecordRaw(void*,const crew::KeyOptions*,crew::Attribute** out){*out=&attribute;return 0;}
void RecordRelease(crew::Attribute*){++releases;}
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
    return nullptr;
}
void RecordNotice(const wchar_t* text) noexcept {++notices;notice=text;}
void RecordCreated(void*,const void*,void*,crew::Callback){++joins;}
}
int main(int argc,char** argv){
    using namespace crew;
    resolveCoop=&ResolveDependency;showDependencyProblem=&RecordNotice;create=&RecordCreated;
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
        value.value=type;before=joins;result=-999;enter(nullptr,&jo,reinterpret_cast<void*>(9),&RecordCallback);
        Check(joins==before && result==10,"invited incompatible profile refused before native join");
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
    Data other{1,"DIFFICULTY",2,1};const Options otherOptions{2,&other,0};writes.clear();publish(nullptr,&otherOptions);
    Check(writes.size()==1 && writes[0].value==2,"unrelated metadata untouched");
    // Both native entry points must reject missing/old/incomplete/uninitialized
    // Coop while a matching bridge is usable in the menu (world still absent).
    const auto make=base ? reinterpret_cast<crew::Create>(*Slot("EOS_Lobby_CreateLobby")) : &CreateRoom;
    for(int mode=0;mode<5;++mode){
        dependencyMode=mode;Check(!EDF6AF_RoomIsolationReady(),"Coop virtual admission sees failed online prerequisites");
        const auto oldJoins=joins,oldNotices=notices;result=-999;
        make(nullptr,nullptr,reinterpret_cast<void*>(9),&RecordCallback);
        Check(joins==oldJoins && result==10 && notices==oldNotices+1 && !notice.empty(),"dependency failure refuses create with visible explanation");
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
    std::printf("mod room wrappers: %s; real EDF search/invite: %s\n",failures?"FAIL":"PASS",native?"executed":"not supplied");
    if(argc>1 && *argv[1] && !native)return 1;
    return failures?1:0;
}
