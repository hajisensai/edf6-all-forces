// Production resolver + ReadNativeObjectId; private scene only, no game/DllMain/installation.
// Optional argv[1] executes the real EDF.dll all-team walker, with only its mutex services stubbed.
#include "../src/command_identity.cpp"
#include "../src/net_object_resolve.cpp"
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
using namespace crew::netobj;
int checks=0,walks=0,mutation=0;
unsigned char actors[19][0x1EE0]{},ctrls[19][16]{},netctrls[19][16]{},entries[19][0x28]{};
unsigned char user[0x50]{},userctrl[16]{},user2[0x50]{},userctrl2[16]{},mgr[0x50]{},teams[team_native::kTeamCapacity*0x38]{},status[0x15000]{};
unsigned char ids[19][32]{},heads[7][0x30]{},nodes[19][0x30]{};
unsigned char requester0[32]{};   // actors[0]'s requester: mission slot 0 (net_object_ref.h)
unsigned sceneCount=19;
void Check(bool ok,const char* why){++checks;if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
// Each object through the visitor's own slot 1, as the native walker calls it (Visit, or the mark resolver's VisitMark).
void VisitThrough(void* v,const unsigned char* object) {
    reinterpret_cast<void(__fastcall*)(void*,const unsigned char*)>(const_cast<void*>((*static_cast<const void* const* const*>(v))[1]))(v,object);
}
void __fastcall Walk(void*,void* v) {
    ++walks;
    for(unsigned i=0;i<sceneCount;++i)VisitThrough(v,actors[i]);
    VisitThrough(v,actors[0]); // same object in more than one team is not an ambiguous scene match
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
    std::memset(user2,0,sizeof user2);std::memset(userctrl2,0,sizeof userctrl2);
    std::memset(ids,0,sizeof ids);netref::MakePlayer(0,requester0);
    Put<void*>(image,kTeamManager,mgr);Put<void*>(image,kGameStatus,status);
    Put<void*>(mgr,0x38,teams);Put<std::uint64_t>(mgr,0x48,7);Put<unsigned>(status,0x14FF8,1);
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
    return ResolveCommandIdentities(requester0,ids+1,count,focus,&out);
}
void Rejected(const char* why) {
    out.requester={actors[0],ctrls[0]};out.count=9;out.requesterPuid=user;
    Check(!Resolve() && !out.requester && !out.count && !out.requesterPuid,why);
}
// A second player (actors[1]) in mission slot `slot`, its own User.
void SecondPlayer(int slot) {
    actors[1][edf::kHumanPlayer]=1;Put<void*>(actors[1],0x1ED0,user2);Put<void*>(actors[1],0x1ED8,userctrl2);
    Put<LONG>(userctrl2,8,1);Put<void*>(user2,0x18,user2);Put<int>(user2,0x48,slot);Put<unsigned>(status,0x14FF8,2);
}
// A support plan's requester is the player's mission slot (net_object_ref.h): it finds that player on any machine,
// whatever NetId the player has there (a stock mission's are the script thread's count on each machine, 2026-10-31).
void PlayerRequesters() {
    unsigned char requester[32]{},again[32]{};int slot=-1;
    Check(netref::MakePlayer(1,requester) && netref::IsPlayer(requester,&slot) && slot==1,"slot requester round trip");
    Check(requester[20]==0x41 && requester[21]==0x46 && requester[22]==0x50 && requester[23]==0x52 && !netref::IsNative(requester),
          "a player's reference: its tag where a NetId is always zero");
    Check(!netref::IsPlayer(ids[0],nullptr),"a native NetId is not a slot requester");
    Check(!netref::MakePlayer(-1,again) && !netref::MakePlayer(netref::kPlayerSlots,again),"slot outside EDF6Coop's slots rejected");
    std::memcpy(again,requester,32);again[8]=1;Check(!netref::IsPlayer(again,nullptr),"slot requester with other bytes rejected");
    std::memcpy(again,requester,32);again[0]=0;again[1]=4;Check(!netref::IsPlayer(again,nullptr),"slot 1024 rejected");
    MarkIdentities m{};
    netref::MakePlayer(0,requester);
    Check(ResolveMarkIdentities(requester,nullptr,&m) && m.player.obj==actors[0] && m.slot==0 && m.puid==user,"slot 0 finds its player");
    Check(!ResolveMarkIdentities(ids[0],nullptr,&m) && !m.player,"a NetId marker names no player");
    Check(ResolveMarkIdentities(ids[0],ids[2],&m) && !m.player && m.target.obj==actors[2],"a NetId marker: the target still found");
    SecondPlayer(1);
    // The host's player: another NetId on this machine than on the host's (here the same as the local player's).
    std::memcpy(entries[1]+8,entries[0]+8,32);
    netref::MakePlayer(1,requester);
    Check(ResolveMarkIdentities(requester,nullptr,&m) && m.player.obj==actors[1] && m.slot==1 && m.puid==user2,
          "slot 1 finds its player whatever its NetId");
    netref::MakePlayer(0,requester);
    Check(ResolveMarkIdentities(requester,nullptr,&m) && m.player.obj==actors[0] && m.puid==user,"slot 0 is not taken by the other");
    Put<int>(user2,0x48,0);
    Check(ResolveMarkIdentities(requester,nullptr,&m) && !m.player && m.slot==-1 && !m.puid,"two players of one slot: none");
    Put<int>(user2,0x48,2);netref::MakePlayer(2,requester);
    Check(ResolveMarkIdentities(requester,nullptr,&m) && !m.player,"slot outside the mission's players: none");
    Put<int>(user2,0x48,1);actors[1][kDead]=1;netref::MakePlayer(1,requester);
    Check(ResolveMarkIdentities(requester,nullptr,&m) && !m.player,"dead player: none");
    // NetLife::present: a fallen player is still named (an ultimate's caller, a grabber that let go as it fell).
    NetResolved r{};
    Check(ResolveNetRef(requester,NetLife::present,&r) && r.found && r.dead && r.obj.obj==actors[1] && r.slot==1 && r.puid==user2,
          "dead player, present: found, dead");
    Check(ResolveNetRef(requester,NetLife::live,&r) && !r.found && r.slot==1,"dead player, live: not found, its slot told");
    Check(EncodeNetRef(ObjRef{actors[1],ctrls[1]},again) && !std::memcmp(again,requester,32),"a dead player's reference is its slot");
    Check(!PlayerRequesterOf(ObjRef{actors[1],ctrls[1]},again) && !again[0],"a dead player has no live requester");
    // One of the slot dead, one alive (a body and its respawn): the live one.
    Put<int>(user,0x48,1);
    Check(ResolveNetRef(requester,NetLife::present,&r) && r.found && !r.dead && r.obj.obj==actors[0] && r.puid==user,
          "one dead, one live of a slot: the live one");
    actors[1][kDead]=0;
    Check(ResolveNetRef(requester,NetLife::present,&r) && !r.found && r.slot==1,"two live players of a slot: none");
    actors[0][kDead]=1;actors[1][kDead]=1;
    Check(ResolveNetRef(requester,NetLife::present,&r) && !r.found,"two dead players of a slot, none live: none");
    actors[0][kDead]=0;actors[1][kDead]=0;Put<int>(user,0x48,0);
    actors[1][0x18]=4;
    Check(ResolveNetRef(requester,NetLife::present,&r) && !r.found && r.slot==1 && !r.puid,"its object deleted: not found, its slot told");
    actors[1][0x18]=0;
    // A player as a target (a teammate grabbed, a player marked): found by its slot whatever its NetId here.
    MarkIdentities t{};
    Check(ResolveGrabIdentities(nullptr,requester,&t) && t.target.obj==actors[1],"a player target found by its slot");
    Check(EncodeNetRef(ObjRef{actors[2],ctrls[2]},again) && !std::memcmp(again,ids[2],32),"anything else: its NetId");
    {
        const unsigned char* refs[3]={requester,ids[2],nullptr};
        const NetLife lives[3]={NetLife::live,NetLife::live,NetLife::live};
        NetResolved got[3]{};
        Check(ResolveNetRefs(refs,lives,3,got) && got[0].obj.obj==actors[1] && got[1].obj.obj==actors[2] && !got[2].found &&
              got[2].slot==-1,"one walk, a player, a NetId and none");
        unsigned char bad[32]{};std::memcpy(bad,requester,32);bad[9]=1;refs[2]=bad;
        Check(!ResolveNetRefs(refs,lives,3,got) && !got[0].found,"an invalid reference: no walk, nothing found");
    }
    Check(PlayerRequesterOf(ObjRef{actors[1],ctrls[1]},again) && !std::memcmp(again,requester,32),"a live player's requester is its slot");
    Check(!PlayerRequesterOf(ObjRef{actors[1],ctrls[2]},again) && !again[0],"a stale reference has no requester");
    Check(!PlayerRequesterOf(ObjRef{actors[2],ctrls[2]},again) && !again[0],"a soldier that is no player has no requester");
    Reset();
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
    // The walk's row bound (cmp rsi,imm32): stock 7 rows here, the capacity's rows below.
    const unsigned char bound[]={0x48,0x81,0xFE,0x88,0x01,0x00,0x00};std::memcpy(image+team_walk::kWalkBound,bound,sizeof bound);
    Reset();Check(Resolve() && walks==1 && out.count==16 && out.units[15].obj==actors[16] &&
        out.focus.obj==actors[17] && out.requesterPuid==user,"single walk resolves requester, 16 remote/local units and focus");
    Check(At<LONG>(ctrls[0],8)==1 && At<LONG>(ctrls[1],8)==1 && At<LONG>(userctrl,8)==1 &&
        At<LONG>(netctrls[0],0)==1,"borrowed identity resolution does not acquire or release native references");
    Put<void*>(actors[0],edf::kHumanPad,nullptr);Put<unsigned>(actors[0],0x128,1);
    Check(Resolve(),"remote player requester needs no local input-pad pointer");Reset();
    unsigned char got[32]{};Check(ReadNativeObjectId(actors[1],got) && !std::memcmp(got,ids[1],32),"production ID reader zeros dirty native padding");
    Check(Resolve(0,nullptr) && !out.count && !out.focus,"requester-only commands allowed");
    Check(!Resolve(17),"capacity rejected");
    Check(!ResolveCommandIdentities(requester0,nullptr,1,nullptr,&out),"missing units rejected");
    Check(!ResolveCommandIdentities(requester0,ids+1,1,nullptr,nullptr),"missing output rejected");
    ids[1][20]=1;Rejected("noncanonical padding rejected");Reset();
    std::memset(requester0,0,32);Rejected("zero requester rejected");Reset();
    // The requester is its mission slot alone (net_object_ref.h): its NetId names no player on another machine.
    std::memcpy(requester0,ids[0],32);Rejected("a NetId requester rejected");Reset();
    netref::MakePlayer(1,requester0);Rejected("a slot no player holds rejected");Reset();
    std::memcpy(entries[0]+8,ids[18],32);Check(Resolve() && out.requester.obj==actors[0],"the requester found whatever its NetId here");Reset();
    SecondPlayer(1);netref::MakePlayer(1,requester0);
    Check(ResolveCommandIdentities(requester0,ids+2,15,ids[17],&out) && out.requester.obj==actors[1] && out.requesterPuid==user2 &&
          out.units[0].obj==actors[2],"the other player's slot finds it");Reset();
    SecondPlayer(0);Rejected("two players of the requester's slot rejected");Reset();
    // The focus may be a player (an enemy player in PvP): by its slot.
    SecondPlayer(1);netref::MakePlayer(1,ids[17]);
    Check(ResolveCommandIdentities(requester0,ids+2,15,ids[17],&out) && out.focus.obj==actors[1],"a player focus found by its slot");
    Reset();
    SecondPlayer(1);netref::MakePlayer(1,ids[17]);
    Check(!ResolveCommandIdentities(requester0,ids+1,16,ids[17],&out),"one object named as a unit and as the focus rejected");
    Reset();
    SecondPlayer(1);netref::MakePlayer(1,ids[1]);Rejected("a player as a unit rejected");Reset();
    std::memcpy(ids[1],requester0,32);Rejected("requester's ID repeated as unit rejected");Reset();
    std::memset(ids[1],0,32);Rejected("zero unit rejected");Reset();
    std::memset(ids[17],0,32);Rejected("zero supplied focus rejected");Reset();
    std::memcpy(ids[2],ids[1],32);Rejected("duplicate units rejected");Reset();
    std::memcpy(ids[1],ids[0],32);Rejected("the requester repeated as a unit by its NetId rejected");Reset();
    std::memcpy(ids[17],ids[1],32);Rejected("focus repeated as unit rejected");Reset();
    std::memcpy(entries[18]+8,entries[1]+8,32);Rejected("two scene objects with same ID rejected");Reset();
    sceneCount=17;Rejected("missing focus rejected");Reset();
    for(unsigned i: {0u,1u,17u}) {
        actors[i][kDead]=1;Rejected("dead requested actor rejected");Reset();
        actors[i][0x18]=4;Rejected("deleted requested actor rejected");Reset();
        Put<LONG>(ctrls[i],8,0);Rejected("expired object rejected");Reset();
        if(i){Put<LONG>(netctrls[i],0,0);Rejected("expired native identity rejected");Reset();}   // the requester's goes unread
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
    // The rows checked before the walk are the rows it walks: its bound, the manager's live count, both.
    Put<std::uint32_t>(image,team_walk::kWalkBound+3,team_native::kCapRows);Rejected("walk bound past the manager's 7 rows rejected");
    Put<std::uint64_t>(mgr,0x48,team_native::kTeamCapacity);
    Check(Resolve() && out.count==16,"installed capacity: a 16-row walk over a 16-row manager resolves");
    Put<std::uint64_t>(mgr,0x48,team_native::kTeamCapacity+1);Rejected("rows beyond the capacity rejected");
    Put<std::uint32_t>(image,team_walk::kWalkBound+3,0x188);Reset();
    Put<std::uint32_t>(image,team_walk::kWalkBound+3,0x18C);Rejected("walk bound not whole rows rejected");
    Put<std::uint32_t>(image,team_walk::kWalkBound+3,0x188);Reset();
    image[team_walk::kWalkBound]^=1;Rejected("unrecognized walk bound rejected");image[team_walk::kWalkBound]^=1;Reset();
    image[kEnumAllTeams]^=1;Rejected("unsupported native walker rejected");image[kEnumAllTeams]^=1;Reset();
    PlayerRequesters();
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    if(argc>1 && argv[1][0])Native(argv[1]);
    else std::puts("SKIP optional native walker: no EDF.dll supplied; production resolver checks still ran");
    std::printf("command_identity_test: %d checks passed\n",checks);
}
