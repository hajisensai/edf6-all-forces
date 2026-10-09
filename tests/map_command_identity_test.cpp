// The production selection and dispatch adapter with synthetic units. No game, window or input manipulation.
#include "../src/mapcmd.cpp"
#include <cstdlib>

namespace crew {
void SupportCallStatus(wchar_t* out,std::size_t capacity) noexcept {if(out && capacity)out[0]=0;}
int SupportCallCount() noexcept { return 3; }
const wchar_t* SupportCallName(int) noexcept { return L"Support"; }
SupportIcon SupportCallIcon(int) noexcept { return SupportIcon::jet; }
SupportVariant SupportCallVariant(int) noexcept { return SupportVariant::none; }
SupportReadiness SupportCallReadiness() noexcept { return {SupportReady::ready,0}; }
int supportCalls=0,supportChosen=-1;float supportTarget[3]{};
bool SupportCallAt(int index,const float* target,wchar_t* note,std::size_t capacity) noexcept {
    ++supportCalls;supportChosen=index;std::memcpy(supportTarget,target,12);
    _snwprintf_s(note,capacity,_TRUNCATE,L"support received");return true;
}
int payloadRequests=0,payloadSeat=-1,payloadEntry=-1;std::uint64_t payloadToken=0;
bool RequestPayloadSelection(std::uint64_t token,int seat,int entry) noexcept {
    ++payloadRequests;payloadToken=token;payloadSeat=seat;payloadEntry=entry;return true;
}
unsigned char* PlayerHuman() noexcept {return nullptr;}
const void* remoteAuthority=nullptr;bool allRemote=false,mapOnline=false;
bool IsOnlineAuthority(const void* object) noexcept {return !allRemote && object!=remoteAuthority;}
CommandNetworkResult networkReply{};unsigned networkSubmits=0,networkUnits=0,networkTotal=0;
std::uint32_t networkSlots[kCommandNetUnits]{};ObjRef networkIdentities[kCommandNetUnits]{};Command networkCommand{};
std::uint32_t SubmitMapCommand(const ObjRef&,const ObjRef* ids,unsigned count,const mapcmd::Command& command,const ObjRef&,
    wchar_t* note,std::size_t size,const std::uint32_t* slots,std::uint32_t total) noexcept {
    ++networkSubmits;networkUnits=count;networkTotal=total;networkCommand=command;
    for(unsigned i=0;i<count && i<kCommandNetUnits;++i){networkIdentities[i]=ids[i];networkSlots[i]=slots ? slots[i] : 0;}
    networkReply={};networkReply.request=91;networkReply.state=CommandNetworkState::pending;networkReply.count=count;
    _snwprintf_s(note,size,_TRUNCATE,L"queued");return 91;
}
bool ReadMapCommandNetworkResult(CommandNetworkResult* out) noexcept {*out=networkReply;return out->request!=0;}
bool MapCommandNetworkReady() noexcept {return true;}

ObjRef NpcMarkedIdentity() noexcept {return {};}
NpcCommandResult NpcSquadCommandForRequester(const ObjRef& id,const mapcmd::Command& command,const ObjRef&,const ObjRef&) noexcept {
    return SquadCommand(id.obj,command) ? NpcCommandResult{NpcCommandReason::none,1} : NpcCommandResult{NpcCommandReason::failed,0};
}
unsigned char* image=nullptr;
bool NpcDriver(const unsigned char* v) noexcept {
    if(!v || !SeatCount(v))return false;
    auto* seat=SeatAt(const_cast<unsigned char*>(v),0);const auto who=SeatRider(seat);
    if(who==Rider::dummy)return true;
    const auto* human=At<const unsigned char*>(seat,kSeatRider);
    return who==Rider::other && !AnyPlayerIn(seat) && human && At<const void*>(human,0)==image+0x17CDF28;
}

void Log(const char*,...) noexcept {}
bool InSession() noexcept { return mapOnline; }
bool NpcMarked() noexcept { return false; }
int CycleGuardFormation(const void*) noexcept {return -2;}
int CycleMarchFormation() noexcept {return 0;}
int SetGuardFormation(const void*,int) noexcept {return -2;}
int NpcGuardShape(const void*) noexcept {return -2;}
int SetMarchFormation(int) noexcept {return -2;}
int SplitSquad(const void*) noexcept {return -1;}
bool MergeSquads(const void*,const void*) noexcept {return false;}
bool NpcSweepToggle(const void* const*,int) noexcept {return false;}
bool NpcSweepOn() noexcept {return false;}
bool NpcPickupHealthToggle() noexcept {return false;}
bool NpcPickupHealthOn() noexcept {return false;}
int NpcMarchShape() noexcept {return 0;}
const wchar_t* FormationText(int) noexcept {return L"";}
bool NpcMarkEnemy(const void*,const float*,bool) noexcept { return false; }
bool VisitEnemiesOf(std::int32_t,EnemyVisitor,void*) noexcept { return true; }
PlayerFix player{};
const Config& Cfg() noexcept { static const Config c{};return c; }
float MapRay(const float*,const float*,float*) noexcept { return -1; }
float MapFloorRay(const float*,const float*,float*) noexcept { return -1; }
bool MapGroundNear(float,float,float,float*,bool) noexcept { return false; }
bool HeliSharesPost() noexcept { return false; }
int HeliCommandUnits(CommandUnit*,int) noexcept { return 0; }
int JetCommandUnits(CommandUnit*,int) noexcept { return 0; }
int GroundCommandUnits(CommandUnit*,int) noexcept { return 0; }
int TankCommandUnits(CommandUnit*,int) noexcept { return 0; }
int SquadCommandUnits(CommandUnit*,int) noexcept { return 0; }
int SquadRows(SquadRow*,int,SquadTally* tally) noexcept { if(tally)*tally=SquadTally{};return 0; }
bool HeliCommand(const void*,const Command&) noexcept { return false; }
bool JetCommand(const void*,const Command&) noexcept { return false; }
bool GroundCommand(const void*,const Command&) noexcept { return false; }
bool TankCommand(const void*,const Command&) noexcept { return false; }
bool SquadCommand(const void*,const Command&) noexcept { return false; }
}
namespace {
int checks=0;
void Check(bool b,const char* why) { ++checks;if(!b){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);} }
}
int main() {
    using namespace crew;
    unsigned char object[0x100]{},control[1]{},replacement[1]{};
    Put<const void*>(object,kSelfCtrl,control);
    Game g{};g.count=1;g.list[0]=Entry{CommandUnit{object,"NPC",{},false,{},false,nullptr},Owner::squad};
    const void* ids[]={object};g.sel.Add(object);RememberSelection(g);
    KeepSelection(g,ids);Check(g.sel.Has(object),"live selected identity retained");
    Put<const void*>(object,kSelfCtrl,replacement);
    KeepSelection(g,ids);Check(g.sel.n==0,"recycled address cannot inherit selection");
    g.sel.Add(object);RememberSelection(g);KeepSelection(g,ids);Check(g.sel.n==1,"replacement can be explicitly selected");
    int skipped=0;
    Check(Issue(g,Command{Order::follow,{}},&skipped)==0 && skipped==1,"runtime refusal counted as unable to command");
    g.count=0;KeepSelection(g,ids);Check(g.sel.n==0,"removed unit loses selection");
    std::printf("map_command_identity_test: %d checks passed\n",checks);
}
