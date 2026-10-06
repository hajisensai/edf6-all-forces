// The production selection and dispatch adapter with synthetic units. No game, window or input manipulation.
#include "../src/mapcmd.cpp"
#include <cstdlib>

namespace crew {
unsigned char* image=nullptr;
void Log(const char*,...) noexcept {}
bool InSession() noexcept { return false; }
bool NpcMarked() noexcept { return false; }
float MapRay(const float*,const float*,float*) noexcept { return -1; }
bool HeliSharesPost() noexcept { return false; }
int HeliCommandUnits(CommandUnit*,int) noexcept { return 0; }
int JetCommandUnits(CommandUnit*,int) noexcept { return 0; }
int GroundCommandUnits(CommandUnit*,int) noexcept { return 0; }
int TankCommandUnits(CommandUnit*,int) noexcept { return 0; }
int SquadCommandUnits(CommandUnit*,int) noexcept { return 0; }
int SquadRows(SquadRow*,int) noexcept { return 0; }
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
