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
    static_assert(kMode==0 && kPreset==1 && kFaction0==2 && kItems==2+crew::pvp_match::kFactionCount,"panel codes: mode, preset, one row a faction");
    static_assert(kItems==9,"seven factions");
    auto status=Snapshot();
    Check(Allowed(status,kMode) && Allowed(status,kPreset),"host can use the mode and the preset");
    for(int f=0;f<crew::pvp_match::kFactionCount;++f)Check(Allowed(status,kFaction0+f) && CodeFaction(kFaction0+f)==f && FactionCode(kFaction0+f),"host can pick every faction");
    Check(!Allowed(status,-1) && !Allowed(status,kItems) && !Allowed(status,kItems+5),"unsupported UI codes are refused");
    Check(CodeFaction(kMode)==-1 && CodeFaction(kPreset)==-1 && CodeFaction(kItems)==-1 && !FactionCode(kPreset),"only the faction rows map to a faction");
    auto stale=status;++stale.epoch;Check(!Same(status,stale),"world epoch is part of click identity");
    stale=status;++stale.revision;Check(!Same(status,stale),"authoritative revision is part of click identity");
    stale=status;strcpy_s(stale.localPuid,"replacement");Check(!Same(status,stale),"replacement local member invalidates rendered action");
    stale=status;stale.enabled=true;Check(!Same(status,stale),"mode toggle is tied to the displayed value");
    stale=status;stale.preset=static_cast<std::uint8_t>(crew::pvp_match::Preset::humansVsAliens);Check(!Same(status,stale),"preset is tied to the displayed value");
    stale=status;stale.local.faction=3;Check(!Same(status,stale),"the local faction is tied to the displayed value");
    stale=status;stale.local.role=1;stale.local.unit=7;Check(Same(status,stale),"role and unit are not part of the click identity");
    status.pending=true;Check(!Allowed(status,kFaction0) && !Allowed(status,kMode) && !Allowed(status,kPreset),"pending transaction disables all actions");status.pending=false;
    status.ready=false;Check(!Allowed(status,kMode) && !Allowed(status,kFaction0),"unready current world cannot mutate native teams");status.ready=true;
    status.epoch=0;Check(!Allowed(status,kFaction0),"no world identity: no action");status.epoch=123;
    status.localPuid[0]=0;Check(!Allowed(status,kFaction0),"no local member: no action");strcpy_s(status.localPuid,"member123");
    Pad pad;
    auto r=Step(pad,status,true,{true});
    Check(pad.focused && pad.held && r.consumed && r.action<0,"R3 focuses a collapsed visible panel without issuing an action");
    Check(pad.selected==kFaction0,"the pad starts on the first faction");
    r=Step(pad,status,true,{false,true});Check(pad.selected==kPreset && r.consumed,"D-pad up from the first faction reaches the preset");
    r=Step(pad,status,true,{false,true});
    Check(pad.selected==kMode && r.consumed,"host can select mode with D-pad up");
    r=Step(pad,status,true,{false,false,false,true,false,true});
    Check(r.action==kMode && pad.held,"A confirms the selected host action and captures centring");
    r=Step(pad,status,true,{false,false,false,false,true,true,true});
    Check(!pad.focused && pad.held && r.consumed && r.action<0,"B exits without confirming and retains both held buttons");
    r=Step(pad,status,true,{false,false,false,false,false,true});
    Check(!pad.focused && pad.held && r.consumed && r.action<0,"held A stays consumed after B release");
    r=Step(pad,status,true,{});Check(!pad.held && !r.consumed,"capture ends only when A and B are both released");
    Step(pad,status,true,{true});
    Step(pad,status,true,{false,false,false,false,true,false,true});
    r=Step(pad,status,true,{false,false,true,false,false,false,true});
    Check(!pad.focused && pad.held && r.consumed && r.action<0,"D-pad remains consumed while the B exit gesture drains");
    Step(pad,status,true,{});
    Step(pad,status,true,{true});
    r=Step(pad,status,true,{true,false,false,false,false,true});
    Check(!pad.focused && pad.held && r.consumed,"R3 exit retains a held A gesture");
    r=Step(pad,status,true,{false,true,false,false,false,true});
    Check(!pad.focused && pad.held && r.consumed && r.action<0,"D-pad remains consumed after R3 exits with A held");
    r=Step(pad,status,true,{});Check(!pad.held && !r.consumed,"R3 exit releases input after all gesture buttons lift");
    {   // The host's cycle: mode, preset, every faction, then round again.
        Pad cycle;Step(cycle,status,true,{true});cycle.selected=kMode;
        for(int i=1;i<=kItems;++i){Step(cycle,status,true,{false,false,true});Check(cycle.selected==i%kItems,"host D-pad down walks every row and wraps to the mode");}
        cycle.selected=kMode;Step(cycle,status,true,{false,true});Check(cycle.selected==kItems-1,"host D-pad up from the mode wraps to the last faction");
        for(int i=kItems-2;i>=kMode;--i){Step(cycle,status,true,{false,true});Check(cycle.selected==i,"host D-pad up walks every row");}
        cycle.selected=kPreset;r=Step(cycle,status,true,{false,false,false,true,false,true});
        Check(r.action==kPreset,"A on the preset row confirms it");
        cycle.selected=kFaction0+4;r=Step(cycle,status,true,{false,false,false,true,false,true});
        Check(r.action==kFaction0+4,"A on a faction row confirms that faction");
    }
    status=Snapshot(false);pad={};
    Check(!Allowed(status,kMode) && !Allowed(status,kPreset) && Allowed(status,kFaction0) && Allowed(status,kItems-1),"guest can pick a faction but cannot toggle the mode or set the preset");
    Step(pad,status,true,{true});Check(pad.selected==kFaction0,"a guest starts on the first faction");
    Step(pad,status,true,{false,true});Check(pad.selected==kItems-1,"guest up from the first faction wraps past the host-only rows");
    Step(pad,status,true,{false,false,true});Check(pad.selected==kFaction0,"guest down from the last faction wraps to the first, skipping the host-only rows");
    for(int i=1;i<crew::pvp_match::kFactionCount;++i){Step(pad,status,true,{false,false,true});Check(pad.selected==kFaction0+i,"guest D-pad walks the factions");}
    pad.selected=kMode;Step(pad,status,true,{});Check(pad.selected==kFaction0,"a guest's selection on a host-only row falls back to the first faction");
    pad.selected=kPreset;Step(pad,status,true,{});Check(pad.selected==kFaction0,"a guest's selection on the preset row falls back to the first faction");
    r=Step(pad,status,true,{false,false,false,true,false,true});Check(r.action==kFaction0,"guest A submits the selected faction only");
    status.pending=true;r=Step(pad,status,true,{false,false,false,false,false,true});
    Check(!pad.focused && pad.held && r.consumed && r.action<0,"new authoritative pending state closes focus without leaking held A");
    r=Step(pad,status,true,{false,false,true,false,false,true});
    Check(!pad.focused && pad.held && r.consumed && r.action<0,"pending state keeps consuming the original held gesture on later frames");
    r=Step(pad,status,false,{false,false,false,false,false,true});
    Check(!pad.focused && pad.held && r.consumed && r.action<0,"a disappearing panel retains the original held gesture until release");
    Step(pad,status,true,{});Check(!pad.held,"pending-state gesture releases normally");
    status.pending=false;Step(pad,status,true,{true});
    r=Step(pad,status,true,{false,false,false,false,false,false,false,true});
    Check(!pad.focused && r.consumed,"mouse takeover exits pad focus and consumes its final gesture");
    r=Step(pad,status,false,{true});Check(!pad.focused && !r.consumed,"R3 does not capture input when the panel is absent");
    status.epoch=0;Step(pad,status,true,{true});Check(!pad.focused,"missing world identity cannot focus actions");
    std::printf("map PVP input: %d checks passed\n",checks);
}
