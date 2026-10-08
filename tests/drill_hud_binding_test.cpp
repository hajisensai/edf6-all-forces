// Include the production HUD with its existing draw stand-ins; inspect the text builder used by BOTH drill views.
#define wmain ExistingHudViewMain
#include "../tools/hud_view.cpp"
#undef wmain
int wmain() {
    using namespace crew;
    hudtext::Use(hudtext::Lang::en);
    DrillCue cue{};Line line{};int failed=0;
    const auto check=[&](bool ok,const char* why){if(!ok){++failed;std::printf("FAIL: %s\n",why);}};
    config.drillLaunch=true;config.drillLaunchKey='T';cue.keys=true;
    DrillLaunchHint(line,cue);check(std::wcsstr(line.text,L"[T]")!=nullptr,"changed keyboard binding appears instead of hard-coded R");
    line={};cue.keys=false;config.drillLaunchButton=0x04;
    DrillLaunchHint(line,cue);check(std::wcsstr(line.text,L"[X]")!=nullptr,"actual EDF pad mask appears instead of hard-coded Y");
    line={};config.drillLaunchButton=0x18;
    DrillLaunchHint(line,cue);check(std::wcsstr(line.text,L"[Y/LB]")!=nullptr,"multiple allowed pad buttons follow input mask");
    line={};config.drillLaunch=false;DrillLaunchHint(line,cue);
    check(!line.text[0],"disabled launch is not advertised as available");
    line={};config.drillLaunch=true;config.drillLaunchKey=VK_RBUTTON;cue.keys=true;DrillLaunchHint(line,cue);
    check(std::wcsstr(line.text,Tr(Tx::mouseRight))!=nullptr,"mouse binding uses the same supported key naming");
    std::printf("drill_hud_binding_test: 5 checks, %d failures\n",failed);return failed ? 1 : 0;
}
