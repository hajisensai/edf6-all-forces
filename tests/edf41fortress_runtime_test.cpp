// Execute the fortress's production installer and both patched vtable slots with Edf41Ai off and on. The stand-in
// image has native signatures and x64 bridges to a small world model; no game is loaded, launched or modified.
// This covers the M117 regression the action-table-only check cannot see: an AI option must not disable ObjectAction.
#include "../src/edf41fortress.cpp"

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <initializer_list>

namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
}  // namespace crew

namespace {
int checks=0,partsCalls=0,deathCalls=0,messageCalls=0,updateCalls=0;
unsigned lastMask=0;
int lastOn=-1,lastIndex=-1;
void* lastSelf=nullptr;
void* lastData=nullptr;
std::uint32_t lastMessage=0;
bool messageResult=false;
alignas(16) unsigned char marked[0x1B00]{};
alignas(16) unsigned char ordinary[0x1B00]{};
alignas(16) unsigned char clip[0x160]{};

void Check(bool ok,const char* what) {
    ++checks;
    if(!ok) { std::fprintf(stderr,"FAIL: %s\n",what); std::exit(1); }
}

template<class T> T& Field(unsigned char* object,std::size_t offset) {
    return *reinterpret_cast<T*>(object+offset);
}

void* __fastcall Key(void* sgo,const wchar_t* key) {
    Check(std::wcscmp(key,L"edf41_fortress")==0,"production key lookup uses the fortress marker");
    return sgo==marked+crew::kSgoAt ? marked : nullptr;
}
void __fastcall Parts(void* self,unsigned mask,int on) {
    ++partsCalls; lastSelf=self; lastMask=mask; lastOn=on;
}
void __fastcall Die(void* self) { ++deathCalls; lastSelf=self; }
std::uint64_t __fastcall Update(void* self,void* dt) {
    ++updateCalls; lastSelf=self; lastData=dt; return 0x123456789ABCDEF0ull;
}
bool __fastcall Message(void* self,std::uint32_t msg,void* data) {
    ++messageCalls; lastSelf=self; lastMessage=msg; lastData=data;
    if(data && (msg==crew::kMsgDrop || msg==crew::kMsgGenerator))
        lastIndex=*reinterpret_cast<int*>(static_cast<unsigned char*>(data)+(msg==crew::kMsgDrop ? crew::kDropIndexAt : 0));
    return messageResult;
}

template<class Fn> void Bridge(std::size_t rva,Fn fn) {
    unsigned char code[12]={0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};  // mov rax,target; jmp rax (preserves all arguments)
    const auto target=reinterpret_cast<std::uintptr_t>(fn);
    std::memcpy(code+2,&target,sizeof(target));
    std::memcpy(crew::image+rva,code,sizeof(code));
    Check(FlushInstructionCache(GetCurrentProcess(),crew::image+rva,sizeof(code))!=0,"flush the stand-in call bridge");
}

void Run(bool ai) {
    using namespace crew;
    config.edf41Ai=ai;
    partsCalls=deathCalls=messageCalls=updateCalls=0;
    gaits.clear();
    std::memset(marked,0,sizeof(marked));
    std::memset(ordinary,0,sizeof(ordinary));
    Field<char*>(marked,kCasAt+kPlayers)=reinterpret_cast<char*>(clip);
    Field<int>(marked,kStepChoiceAt)=99;
    Field<int>(ordinary,kStepChoiceAt)=99;
    auto* table=reinterpret_cast<void**>(image+kVtable);
    table[kSlotUpdate]=image+kUpdate;
    table[kSlotMessage]=image+kMessage;
    const unsigned char partsHead[8]={0x48,0x89,0x5C,0x24,0x08,0x48,0x89,0x6C};
    const unsigned char dieHead[8]={0x40,0x53,0x48,0x83,0xEC,0x30,0x80,0xB9};
    std::memcpy(image+kPartsSwitch,partsHead,sizeof(partsHead));
    std::memcpy(image+kDie,dieHead,sizeof(dieHead));
    Check(InstallEdf41Fortress(),"install the required fortress compatibility with either AI setting");
    Check(table[kSlotUpdate]==reinterpret_cast<void*>(&UpdateHook),"the update vtable entry is the production hook");
    Check(table[kSlotMessage]==reinterpret_cast<void*>(&MessageHook),"the message vtable entry is the production hook");
    Bridge(kPartsSwitch,&Parts);
    Bridge(kDie,&Die);
    const auto send=reinterpret_cast<MessageFn>(table[kSlotMessage]);
    for(int action=0;action<7;++action) {
        const int before=partsCalls;
        Check(send(marked,kMsgAction,&action),"a 4.1 action is answered as handled");
        Check(messageCalls==0,"a 4.1 action never reaches the class's incompatible action table");
        Check(lastSelf==marked,"the native operation receives the original fortress pointer");
        if(action<6) {
            Check(partsCalls==before+1 && lastMask==(1u<<(action/2)) && lastOn==(action%2==0 ? 1 : 0),
                  "the installed hook switches only the intended group with the native ABI");
        } else Check(deathCalls==1,"action 6 reaches the native death operation");
    }
    for(int action:{-1,7,0x7FFFFFFF}) {
        Check(send(marked,kMsgAction,&action),"unknown 4.1 actions remain handled");
        Check(partsCalls==6 && deathCalls==1 && messageCalls==0,"unknown actions cannot reach either native operation or class table");
    }
    int action=1;
    messageResult=false;
    Check(!send(ordinary,kMsgAction,&action),"an ordinary fortress keeps the class's false result");
    Check(messageCalls==1 && lastSelf==ordinary && lastMessage==kMsgAction && lastData==&action,
          "an ordinary fortress keeps the exact message arguments");
    messageResult=true;
    Check(send(ordinary,kMsgAction,&action),"an ordinary fortress keeps the class's true result");
    Check(send(marked,0x1234u,&action) && lastSelf==marked && lastMessage==0x1234u && lastData==&action,
          "unrelated messages on a marked fortress keep the class's arguments and result");
    Check(send(marked,kMsgAction,nullptr) && lastData==nullptr,"null message data preserves the class's contract");
    alignas(16) unsigned char drop[0x50]{};
    for(std::uint32_t msg:{kMsgDrop,kMsgGenerator}) {
        int* index=reinterpret_cast<int*>(drop+(msg==kMsgDrop ? kDropIndexAt : 0));
        *index=0;
        Check(send(marked,msg,drop),"generator compatibility preserves the class result");
        Check(lastSelf==marked && lastMessage==msg && lastData==drop && lastIndex==kCoreGenerator && *index==0,
              "generator compatibility routes the core and restores the sender's index with AI off or on");
        Check(send(ordinary,msg,drop) && lastIndex==0 && *index==0,"an ordinary generator message is unchanged");
    }
    const auto update=reinterpret_cast<UpdateFn>(table[kSlotUpdate]);
    int dt=42;
    Check(update(marked,&dt)==0x123456789ABCDEF0ull && updateCalls==1 && lastSelf==marked && lastData==&dt,
          "the update hook forwards the native arguments and full return value");
    Check(Field<int>(marked,kTargetModeAt)==1,"a converted fortress can stand even with optional AI disabled");
    Check(ai ? Field<int>(marked,kStepChoiceAt)!=99 : Field<int>(marked,kStepChoiceAt)==99,
          "only optional wander follows the AI setting");
    Check(ai ? gaits.size()==1 : gaits.empty(),"disabled wandering never creates a gait record");
    Check(update(ordinary,&dt)==0x123456789ABCDEF0ull && updateCalls==2,"ordinary updates still forward");
    Check(Field<int>(ordinary,kTargetModeAt)==0 && Field<int>(ordinary,kStepChoiceAt)==99,
          "ordinary fortress state is untouched");
}
}  // namespace

int main() {
    using namespace crew;
    constexpr std::size_t size=kVtable+0x100;
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"reserve the stand-in native image");
    Bridge(kKeyNode,&Key);
    Bridge(kUpdate,&Update);
    Bridge(kMessage,&Message);
    Run(false);
    Run(true);
    Check(VirtualFree(image,0,MEM_RELEASE)!=0,"release the stand-in image");
    image=nullptr;
    std::printf("%d fortress runtime checks passed\n",checks);
    return 0;
}
