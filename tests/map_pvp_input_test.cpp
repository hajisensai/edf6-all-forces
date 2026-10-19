#include "../src/map_pvp_input.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checks=0;
void Check(bool ok,const char* text) {++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
crew::PvpStatus Snapshot(bool host=true) {
    crew::PvpStatus status;status.ready=true;status.host=host;status.epoch=123;status.revision=9;
    strcpy_s(status.localPuid,"member123");return status;
}
}
int main() {
    using namespace crew::pvpmenu;
    auto status=Snapshot();
    Check(Allowed(status,kMode) && Allowed(status,kTeam1) && Allowed(status,kTeam2),"host can use exactly mode and verified opposing teams");
    Check(!Allowed(status,-1) && !Allowed(status,3),"unsupported team or UI code is refused");
    auto stale=status;++stale.epoch;Check(!Same(status,stale),"world epoch is part of click identity");
    stale=status;++stale.revision;Check(!Same(status,stale),"authoritative revision is part of click identity");
    stale=status;strcpy_s(stale.localPuid,"replacement");Check(!Same(status,stale),"replacement local member invalidates rendered action");
    stale=status;stale.enabled=true;Check(!Same(status,stale),"mode toggle is tied to the displayed value");
    status.pending=true;Check(!Allowed(status,kTeam1),"pending transaction disables all actions");status.pending=false;
    status.ready=false;Check(!Allowed(status,kMode),"unready current world cannot mutate native teams");status.ready=true;
    Pad pad;
    auto r=Step(pad,status,true,{true});
    Check(pad.focused && pad.held && r.consumed && r.action<0,"R3 focuses a collapsed visible panel without issuing an action");
    r=Step(pad,status,true,{false,true});
    Check(pad.selected==kMode && r.consumed,"host can select mode with D-pad up");
    r=Step(pad,status,true,{false,false,false,true,false,true});
    Check(r.action==kMode && pad.held,"A confirms the selected host action and captures centring");
    r=Step(pad,status,true,{false,false,false,false,true,true,true});
    Check(!pad.focused && pad.held && r.consumed && r.action<0,"B exits without confirming and retains both held buttons");
    r=Step(pad,status,true,{false,false,false,false,false,true});
    Check(!pad.focused && pad.held && r.action<0,"held A stays captured after B release");
    Step(pad,status,true,{});Check(!pad.held,"capture ends only when A and B are both released");
    status=Snapshot(false);pad={};
    Check(!Allowed(status,kMode) && Allowed(status,kTeam1),"guest can select its own team but cannot toggle the room mode");
    Step(pad,status,true,{true});
    Step(pad,status,true,{false,true});Check(pad.selected==kTeam2,"guest navigation skips the host-only mode action");
    Step(pad,status,true,{false,false,true});Check(pad.selected==kTeam1,"guest D-pad cycles its two teams");
    r=Step(pad,status,true,{false,false,false,true,false,true});Check(r.action==kTeam1,"guest A submits the selected team only");
    status.pending=true;r=Step(pad,status,true,{false,false,false,false,false,true});
    Check(!pad.focused && pad.held && r.consumed && r.action<0,"new authoritative pending state closes focus without leaking held A");
    Step(pad,status,true,{});Check(!pad.held,"pending-state gesture releases normally");
    status.pending=false;Step(pad,status,true,{true});
    r=Step(pad,status,true,{false,false,false,false,false,false,false,true});
    Check(!pad.focused && r.consumed,"mouse takeover exits pad focus and consumes its final gesture");
    r=Step(pad,status,false,{true});Check(!pad.focused && !r.consumed,"R3 does not capture input when the panel is absent");
    status.epoch=0;Step(pad,status,true,{true});Check(!pad.focused,"missing world identity cannot focus actions");
    std::printf("map PVP input: %d checks passed\n",checks);
}
