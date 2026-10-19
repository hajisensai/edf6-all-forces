#include "../src/enemy_ui_logic.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
namespace {
int checks=0;
void Check(bool yes,const char* message) {++checks;if(!yes){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
crew::enemyui::Model MakeModel(bool active,int skills=0) {
    return {{9,3,123,88,77},true,active,skills};
}
}
int main() {
    using namespace crew::enemyui;
    State state;auto model=MakeModel(false);
    auto result=Step(state,model,{true});
    Check(state.focused && state.held && result.consumed && result.action==kNone,"L3 enters visible card without executing an action");
    result=Step(state,model,{false,false,false,false,false,true,false,true});
    Check(result.action==kBegin && result.consumed,"A requests control only after an explicit confirm edge");
    result=Step(state,model,{false,false,false,false,false,false,true,true,true});
    Check(!state.focused && state.held && result.action==kNone,"B leaves card while held buttons remain owned");
    result=Step(state,model,{false,false,false,false,false,false,false,true});
    Check(result.consumed && state.held,"A does not leak to map centring after B release");
    result=Step(state,model,{});Check(!state.held && !result.consumed,"gesture drains when both buttons are released");
    model=MakeModel(true,20);state={};Step(state,model,{true});
    result=Step(state,model,{false,false,false,false,false,true,false,true});
    Check(result.action==0,"first skill selects stable full-list ordinal");
    Step(state,model,{false,false,false,false,true});
    Check(state.first==6 && state.row==0,"D-pad right pages through six skills");
    result=Step(state,model,{false,false,false,false,false,true});Check(result.action==6,"second page does not alias its first skill to zero");
    Step(state,model,{false,false,false,true});Check(state.first==0,"left page restores first list page");
    Step(state,model,{false,true});Check(state.row==7,"up from first skill selects explicit return-to-player action");
    result=Step(state,model,{false,false,false,false,false,true});Check(result.action==kExit,"return row issues exit rather than an ability");
    Step(state,model,{false,true});result=Step(state,model,{false,false,false,false,false,true});
    Check(result.action==kFire,"activation has its own row, separated from selecting a skill");
    Page(state,model,-1);Check(state.first==18 && Count(model,state.first)==2,"last partial page is reachable without phantom skills");
    state.row=2;Check(Action(model,state.first,state.row)==kFire,"partial-page action begins after actual row count");
    Page(state,model,1);Check(state.first==0,"pagination wraps over exact page count");
    state.row=4;++model.identity.target;result=Step(state,model,{false,false,false,false,false,true,false,true});
    Check(!state.focused && state.first==0 && state.row==0 && result.action==kNone,"replacement target cancels current confirmation and resets page");
    Step(state,model,{true});++model.identity.owner;result=Step(state,model,{false,false,false,false,false,true,false,true});
    Check(!state.focused && result.action==kNone,"different local owner cannot inherit a rendered confirm gesture");
    Step(state,model,{true});++model.identity.world;result=Step(state,model,{false,false,false,false,false,true,false,true});
    Check(!state.focused && result.action==kNone,"world change invalidates card identity");
    Step(state,model,{true});++model.identity.token;result=Step(state,model,{false,false,false,false,false,true,false,true});
    Check(!state.focused && result.action==kNone,"control lease revision invalidates stale action");
    Step(state,model,{true});result=Step(state,model,{false,false,false,false,false,false,false,false,false,true});
    Check(!state.focused && result.consumed,"mouse takeover consumes last pad gesture");
    model.visible=false;result=Step(state,model,{true});Check(!state.focused && !result.consumed,"hidden card leaves map input untouched");
    model=MakeModel(true,0);state={};Step(state,model,{true});
    Check(Rows(model,0)==2 && Action(model,0,0)==kFire && Action(model,0,1)==kExit,"empty current skill list retains explicit exit");
    Page(state,model,1);Check(state.first==0,"empty skill list cannot divide by zero");
    model=MakeModel(true,4096);state={};Step(state,model,{true});
    for(int i=0;i<682;++i)Page(state,model,1);
    Check(state.first==4092 && Count(model,state.first)==4,"large native skill catalogs remain completely reachable");
    Check(!Same({1,2,3,4,5},{1,2,3,4,6}),"profile identity protects same target against replaced skill catalogs");
    model=MakeModel(false);model.cancellable=true;state={};Step(state,model,{true});
    result=Step(state,model,{false,false,false,false,false,true});
    Check(result.action==kExit,"pending or restoring control keeps explicit return action reachable with A");
    model=MakeModel(false);model.cancellable=true;state={};Step(state,model,{true});
    ++model.identity.request;result=Step(state,model,{false,false,false,false,false,true,false,true});
    Check(!state.focused && result.action==kNone,"replacement online request cancels a pending confirm gesture");
    bool firing=false;
    Check(!HoldFire(firing,false,true,true,true,true),"held A entering USE from SELECT never starts a skill");
    Check(HoldFire(firing,true,true,true,true,true),"new A press on USE starts the held native skill");
    Check(HoldFire(firing,false,true,true,true,true),"unchanged held gesture remains active");
    Check(!HoldFire(firing,false,true,true,false,true),"navigating off USE immediately releases the skill");
    Check(!HoldFire(firing,false,true,true,true,true),"moving back to USE while still holding A does not retrigger");
    HoldFire(firing,true,true,true,true,true);
    Check(!HoldFire(firing,false,true,true,true,false),"target or selected-skill change releases an existing gesture");
    HoldFire(firing,true,true,true,true,true);
    Check(!HoldFire(firing,false,true,false,true,true),"focus loss releases a held skill");
    HoldFire(firing,true,true,true,true,true);
    Check(!HoldFire(firing,false,false,true,true,true),"A release reaches the native skill release lifecycle");
    model.skills=0x7FFFFFFF;state.first=0;Page(state,model,-1);
    Check(state.first==2147483646,"malformed large skill count cannot overflow pagination arithmetic");
    Check(!HealthAvailable(false,100.0f,100.0f),"unknown target HP never reuses plausible source-soldier values");
    Check(HealthAvailable(true,50.0f,100.0f),"verified target current and maximum HP are available");
    Check(!HealthAvailable(true,50.0f,0.0f),"zero maximum HP is unavailable rather than a fake health pool");
    Check(!HealthAvailable(true,-1.0f,100.0f),"negative native health sentinel is unavailable");
    Check(!HealthAvailable(true,std::numeric_limits<float>::quiet_NaN(),100.0f),"non-finite current HP cannot reach HUD formatting");
    Check(!HealthAvailable(true,50.0f,std::numeric_limits<float>::infinity()),"non-finite maximum HP is unavailable");
    Check(HealthAvailable(true,0.0f,100.0f),"real zero target HP remains a valid health readout");
    Check(HealthAvailable(true,120.0f,100.0f),"real native overheal is shown without fabricating a clamped value");
    std::printf("enemy control UI: %d checks passed\n",checks);
}
