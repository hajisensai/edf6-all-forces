// Production resolver + ReadNativeObjectId; private scene only, no game/DllMain/installation.
// Optional argv[1] executes the real EDF.dll all-team walker, with only its mutex services stubbed.
#include "../src/command_identity.cpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace crew {
unsigned char* image=nullptr;
bool IsSoldierClass(const void* o) noexcept {return Readable(o,8) && At<unsigned>(o,0)==0x534F4C44;}
const Config& Cfg() noexcept {static const Config cfg{};return cfg;}
void Log(const char*,...) noexcept {}
bool InSession() noexcept {return false;}
bool OnlineHostOnly() noexcept {return false;}
}
namespace {
using namespace crew;
using namespace crew::command_identity;
int checks=0,walks=0,mutation=0;
unsigned char actors[19][0x1EE0]{},ctrls[19][16]{},netctrls[19][16]{},entries[19][0x28]{};
unsigned char user[0x50]{},userctrl[16]{},mgr[0x50]{},teams[7*0x38]{},status[0x15000]{};
unsigned char ids[19][32]{},heads[7][0x30]{},nodes[19][0x30]{};
unsigned sceneCount=19;
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
void __fastcall Walk(void*,Visitor* v) {
    ++walks;
    for(unsigned i=0;i<sceneCount;++i)Visit(v,actors[i]);
    Visit(v,actors[0]); // same object in more than one team is not an ambiguous scene match
    if(mutation==1)Put<void*>(image,kTeamManager,nullptr);
    if(mutation==2)Put<void*>(image,kGameStatus,nullptr);
    if(mutation==3)Put<unsigned>(status,0x14FF8,2);
    if(mutation==4)Put<void*>(mgr,0x38,nullptr);
    if(mutation==5)Put<void*>(actors[1],kSelfCtrl,ctrls[18]);
    if(mutation==6)entries[1][8]^=1;
    if(mutation==7)Put<void*>(user,0x18,nullptr);
    if(mutation==8)Put<LONG>(userctrl,8,0);
}
void Patch(unsigned rva,const void* bytes,std::size_t size) {
    DWORD old=0;Check(VirtualProtect(image+rva,size,PAGE_EXECUTE_READWRITE,&old)!=0,"private image protect");
    std::memcpy(image+rva,bytes,size);FlushInstructionCache(GetCurrentProcess(),image+rva,size);
}
void Reset() {
    mutation=0;sceneCount=19;
    std::memset(actors,0,sizeof actors);std::memset(ctrls,0,sizeof ctrls);
    std::memset(netctrls,0,sizeof netctrls);std::memset(entries,0,sizeof entries);
    std::memset(user,0,sizeof user);std::memset(userctrl,0,sizeof userctrl);
    std::memset(ids,0,sizeof ids);
    Put<void*>(image,kTeamManager,mgr);Put<void*>(image,kGameStatus,status);
    Put<void*>(mgr,0x38,teams);Put<unsigned>(status,0x14FF8,1);
    for(unsigned i=0;i<19;++i) {
        Put<unsigned>(actors[i],0,0x534F4C44);Put<void*>(actors[i],kSelf,actors[i]);
        Put<void*>(actors[i],kSelfCtrl,ctrls[i]);Put<LONG>(ctrls[i],8,1);
        Put<unsigned>(actors[i],0x128,i%2 ? 1u : 2u);Put<void*>(actors[i],0x130,netctrls[i]);
        Put<LONG>(netctrls[i],0,1);Put<void*>(netctrls[i],8,entries[i]);
        ids[i][0]=static_cast<unsigned char>(i+1);ids[i][12]=5;ids[i][24]=0xAB;
        std::memcpy(entries[i]+8,ids[i],32);std::memset(entries[i]+28,0xA5,4);
    }
    actors[0][edf::kHumanPlayer]=1;Put<void*>(actors[0],edf::kHumanPad,user);
    Put<void*>(actors[0],0x1ED0,user);Put<void*>(actors[0],0x1ED8,userctrl);
    Put<LONG>(userctrl,8,1);Put<void*>(user,0x18,user);Put<int>(user,0x48,0);
}
CommandIdentities out{};
bool Resolve(unsigned count=16,const unsigned char* focus=ids[17]) {
    return ResolveCommandIdentities(ids[0],ids+1,count,focus,&out);
}
void Rejected(const char* why) {
    out.requester={actors[0],ctrls[0]};out.count=9;out.requesterPuid=user;
    Check(!Resolve() && !out.requester && !out.count && !out.requesterPuid,why);
}
void Native(const char* path) {
    auto mod=LoadLibraryExA(path,nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Check(mod!=nullptr,"map EDF.dll without entry point");image=reinterpret_cast<unsigned char*>(mod);
    const auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image+dos->e_lfanew);
    Check(nt->FileHeader.TimeDateStamp==0x678CCB46 && nt->FileHeader.Machine==IMAGE_FILE_MACHINE_AMD64,"supported EDF.dll");
    Check(Matches(kEnumAllTeams,kEnumSignature,sizeof kEnumSignature),"real walker profile");
    const unsigned char ret=0xC3;Patch(0x50BC0,&ret,1);Patch(0x50BE0,&ret,1);
    Reset();
    // Native std::map successor walks all nineteen real tree nodes over all seven teams.
    for(unsigned t=0;t<7;++t) {
        auto* h=heads[t];const unsigned first=t;unsigned last=t;
        while(last+7<19)last+=7;
        Put<void*>(teams,t*0x38,h);Put<void*>(h,0,nodes[first]);Put<void*>(h,8,nodes[first]);
        Put<void*>(h,16,nodes[last]);h[0x19]=1;
        for(unsigned i=t;i<19;i+=7) {
            auto* n=nodes[i];Put<void*>(n,0,h);Put<void*>(n,8,i==t ? h : nodes[i-7]);
            Put<void*>(n,16,i+7<19 ? nodes[i+7] : h);Put<void*>(n,0x20,actors[i]);
        }
    }
    Check(Resolve() && out.requester.obj==actors[0] && out.units[15].obj==actors[16] &&
        out.focus.obj==actors[17] && out.requesterPuid==user,"production resolver through actual seven-team native walker");
    Check(out.units[0].ctrl==ctrls[1],"native traversal returns real remote object's weak identity");
    std::memcpy(entries[18]+8,entries[1]+8,32);Rejected("native scene collision fails closed");
    std::printf("PASS private EDF.dll walker + production canonical ID reader; no game/EOS execution\n");
    FreeLibrary(mod);image=nullptr;
}
}
int main(int argc,char** argv) {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x20C0000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"fake image allocated");
    std::memcpy(image+kEnumAllTeams,kEnumSignature,sizeof kEnumSignature);
    // Complete signature's ADD RSP,0, undo its three pushes, then tail-call the private walker.
    unsigned char jump[]={0xC4,0,0x41,0x5E,0x5F,0x5E,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xE0};
    const auto fn=&Walk;std::memcpy(jump+8,&fn,8);std::memcpy(image+kEnumAllTeams+16,jump,sizeof jump);
    Reset();Check(Resolve() && walks==1 && out.count==16 && out.units[15].obj==actors[16] &&
        out.focus.obj==actors[17] && out.requesterPuid==user,"single walk resolves requester, 16 remote/local units and focus");
    Check(At<LONG>(ctrls[0],8)==1 && At<LONG>(ctrls[1],8)==1 && At<LONG>(userctrl,8)==1 &&
        At<LONG>(netctrls[0],0)==1,"borrowed identity resolution does not acquire or release native references");
    Put<void*>(actors[0],edf::kHumanPad,nullptr);Put<unsigned>(actors[0],0x128,1);
    Check(Resolve(),"remote player requester needs no local input-pad pointer");Reset();
    unsigned char got[32]{};Check(ReadNativeObjectId(actors[1],got) && !std::memcmp(got,ids[1],32),"production ID reader zeros dirty native padding");
    Check(Resolve(0,nullptr) && !out.count && !out.focus,"requester-only commands allowed");
    Check(!Resolve(17),"capacity rejected");
    Check(!ResolveCommandIdentities(ids[0],nullptr,1,nullptr,&out),"missing units rejected");
    Check(!ResolveCommandIdentities(ids[0],ids+1,1,nullptr,nullptr),"missing output rejected");
    ids[1][20]=1;Rejected("noncanonical padding rejected");Reset();
    std::memset(ids[0],0,32);Rejected("zero requester rejected");Reset();
    std::memset(ids[1],0,32);Rejected("zero unit rejected");Reset();
    std::memset(ids[17],0,32);Rejected("zero supplied focus rejected");Reset();
    std::memcpy(ids[2],ids[1],32);Rejected("duplicate units rejected");Reset();
    std::memcpy(ids[1],ids[0],32);Rejected("requester repeated as unit rejected");Reset();
    std::memcpy(ids[17],ids[1],32);Rejected("focus repeated as unit rejected");Reset();
    std::memcpy(entries[18]+8,entries[1]+8,32);Rejected("two scene objects with same ID rejected");Reset();
    sceneCount=17;Rejected("missing focus rejected");Reset();
    for(unsigned i: {0u,1u,17u}) {
        actors[i][kDead]=1;Rejected("dead requested actor rejected");Reset();
        actors[i][0x18]=4;Rejected("deleted requested actor rejected");Reset();
        Put<LONG>(ctrls[i],8,0);Rejected("expired object rejected");Reset();
        Put<LONG>(netctrls[i],0,0);Rejected("expired native identity rejected");Reset();
    }
    actors[0][edf::kHumanPlayer]=0;Rejected("NPC with User fields cannot impersonate player");Reset();
    Put<unsigned>(actors[0],0,0);Rejected("non-soldier player rejected");Reset();
    Put<void*>(actors[0],0x1ED0,nullptr);Rejected("player without User rejected");Reset();
    Put<LONG>(userctrl,8,0);Rejected("expired User rejected");Reset();
    Put<int>(user,0x48,-1);Rejected("lobby-only player rejected");Reset();
    Put<int>(user,0x48,1);Rejected("mission index outside expected count rejected");Reset();
    Put<unsigned>(status,0x14FF8,0);Rejected("no current players rejected");Reset();
    Put<void*>(user,0x18,nullptr);Rejected("missing PUID rejected");Reset();
    for(int m=1;m<=8;++m){mutation=m;Rejected("snapshot mutation rejected");Reset();}
    image[kEnumAllTeams]^=1;Rejected("unsupported native walker rejected");
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    if(argc>1)Native(argv[1]);
    std::printf("command_identity_test: %d checks passed\n",checks);
}
