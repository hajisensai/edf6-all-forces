// Production map HUD contracts without the game: immutable PVP action identities, host/guest permissions,
// collapsed and inactive background capture, payload/placement avoidance, viewport fitting in four languages,
// and thermal status that names the configured key, effective image and explicit optical fallback.
//
// Compile this file alone: it includes tools/hud_view.cpp and therefore src/hud.cpp with the existing offline
// recorders. Rename that renderer's wmain below; do not add either included .cpp as another target source.
// Dependencies match hud_view: target include dirs src, common, third_party/EDFModLoader, build/generated;
// link edf6common and the normal Windows platform libraries. C++17, /MT /W4 /WX /utf-8 /permissive-.
// The native drawing calls target a private stand-in image and never access or launch a running game.
#define wmain hud_view_original_wmain
#include "../tools/hud_view.cpp"
#undef wmain
#include <cstdlib>
namespace {
int taskChecks=0;
void TaskCheck(bool ok,const char* why) { ++taskChecks;if(!ok){std::printf("FAIL PVP HUD: %s\n",why);std::exit(1);} }
bool Intersects(const float* a,const float* b) { return a[0]<b[2] && b[0]<a[2] && a[1]<b[3] && b[1]<a[3]; }
void PvpCase(int width,int height,bool payload,int kind) {
    MapTargetsNone();mapUiPanelCount=0;mapTip.on=false;
    const float w=static_cast<float>(width),h=static_cast<float>(height);
    const float scale=hudscale::FitMap(hudscale::Of(width,height,width,height,1.5f),w,h);
    scenePvp=PvpStatus{};scenePvp.ready=kind!=4;scenePvp.host=kind!=3 && kind!=6;
    scenePvp.enabled=kind==6;scenePvp.pending=kind==2;scenePvp.epoch=987654321;scenePvp.revision=1357;
    scenePvp.localTeam=kind==6 ? 1 : 2;strcpy_s(scenePvp.localPuid,kind==6 ? "guest8-puid" : "fixture-puid");
    scenePvp.result=scenePvp.pending ? pvp_protocol::Result::Pending : pvp_protocol::Result::Idle;
    const bool pad=kind==5;scenePvpPadPick=pad ? kPvpTeam1 : -1;
    scenePvpOpen=kind!=0 && !pad;   // the title clicked open (kPvpFold); the pad's R3 focus opens it by itself
    MapCommandReadout command{};command.pointer=kind!=0 && !pad;
    command.placeOn=true;command.placeOpen=true;command.placeOnline=true;
    command.placeCategory=static_cast<int>(debugspawn::Category::enemy41);
    for(auto& state:command.placeState)state=debugspawn::RowState::ready;
    hasMapPayload=payload;sceneMapPayload=PayloadReadout{};
    if(payload){sceneMapPayload.count=kMostPayload;sceneMapPayload.selectionToken=111;
        for(auto& entry:sceneMapPayload.entry){entry.selectable=true;wcscpy_s(entry.name,L"Payload");}}
    float pvpTop=206.0f*scale,pvpBottom=0;
    const float bottom=h-180.0f*scale;
    Measured([&](Text* text,Line* lines,int* at){
        const float payloadBottom=MapPayloadPanel(image,nullptr,text,w,scale,pad,command,lines,at);
        pvpTop=std::fmax(pvpTop,payloadBottom+10.0f*scale);
        command.px=w-200.0f*scale;command.py=pvpTop+12.0f*scale;
        pvpBottom=MapPvpPanel(image,nullptr,text,w,scale,pvpTop,bottom,pad,command,lines,at);
        const int pvpLast=*at;
        MapPlacePanel(image,nullptr,text,w,scale,pvpBottom+8.0f*scale,bottom,pad,command,lines,at);
        for(int i=0;i<*at;++i)if(lines[i].text[0]) {
            const auto& line=lines[i];
            TaskCheck(line.x>=-0.5f && line.y>=-0.5f && line.x+line.w<=w+0.5f && line.y+line.h<=h+0.5f,"labels stay in the viewport");
            if(i<pvpLast)for(int j=i+1;j<pvpLast;++j)if(lines[j].text[0]) {
                const auto& other=lines[j];
                TaskCheck(line.x+line.w<=other.x+0.5f || other.x+other.w<=line.x+0.5f ||
                          line.y+line.h<=other.y+0.5f || other.y+other.h<=line.y+0.5f,"PVP and payload labels do not overlap");
            }
        }
    },scale);
    const int rowsExpected=kind==0 || kind==2 || kind==4 ? 0 : kind==3 || kind==6 ? 2 : 3;
    const int expected=rowsExpected+(command.pointer ? 1 : 0);   // with the pointer the title row folds it (kPvpFold)
    TaskCheck(scenePvpVisible && scenePvpCount==expected,"collapsed/not-ready/pending/host/guest expose the correct actions");
    int folds=0;for(int i=0;i<scenePvpCount;++i)folds+=scenePvpCodes[i]==kPvpFold;
    TaskCheck(folds==(command.pointer ? 1 : 0),"the title row (and only it) folds the panel, and only with the pointer");
    TaskCheck(scenePvpSnapshot.epoch==scenePvp.epoch && scenePvpSnapshot.revision==scenePvp.revision &&
              !std::strcmp(scenePvpSnapshot.localPuid,scenePvp.localPuid),"hit ledger keeps the displayed world, revision and member");
    TaskCheck(std::fabs(pvpBottom-pvpTop-(kind==0 ? 28.0f : 146.0f)*scale)<0.1f,"a click or pad focus expands the tab without changing its anchor");
    for(int i=0;i<scenePvpCount;++i) {
        const auto* rect=scenePvpRects[i];
        TaskCheck(rect[1]>=pvpTop && rect[3]<=pvpBottom,"PVP hit lies inside its captured background");
        if(!scenePvp.host)TaskCheck(scenePvpCodes[i]!=kPvpModeToggle,"guests cannot toggle mode");
        for(int j=0;j<scenePayloadButtonCount;++j)TaskCheck(!Intersects(rect,scenePayloadButtons[j]),"PVP never covers a payload action");
        for(int j=0;j<scenePlaceHitCount;++j)TaskCheck(!Intersects(rect,scenePlaceHits[j]),"PVP never covers a placement action");
    }
    TaskCheck(mapUiPanelCount>=2,"backgrounds capture inactive and expanded UI areas");
    if(!pad)TaskCheck(scenePlaceHitCount>0,"placement remains reachable below PVP even with eight payload rows");
    MapTargetsNone();TaskCheck(!scenePvpVisible && !scenePvpCount,"closing the map clears PVP hit state");
}
void ThermalCases(int width,int height) {
    const float w=static_cast<float>(width),h=static_cast<float>(height),scale=h/1080.0f;
    for(int mode=0;mode<3;++mode) {
        thermal::Status status{};status.eligible=true;status.installed=true;
        status.effective=status.requested=static_cast<thermal::Mode>(mode);status.reason=thermal::Reason::active;
        config.thermalKey=VK_F3;Line controls{};ThermalControlText(controls,status);
        TaskCheck(wcsstr(controls.text,L"F3")!=nullptr,"thermal controls show the configured binding");
        TaskCheck(wcsstr(controls.text,ThermalModeWord(status.effective))!=nullptr,"thermal controls show the effective mode");
        Measured([&](Text* text,Line* lines,int* at){
            ThermalControlHint(text,w,h,scale,status,lines,at);
            VehicleSensorReadout sensor{};sensor.kind=VehicleSensorKind::optical;sensor.range=1200;
            float vp[16]{};VehicleContacts(image,nullptr,text,vp,w,h,scale,sensor,status,lines,at);
            TaskCheck(*at==2,"thermal control and contact header both render");
            TaskCheck(wcsstr(lines[1].text,ThermalModeWord(status.effective))!=nullptr,"contact header follows effective imagery");
            for(int i=0;i<*at;++i)TaskCheck(lines[i].x>=0 && lines[i].x+lines[i].w<=w+0.5f,"thermal status fits viewport");
        },scale);
    }
    thermal::Status status{};status.eligible=true;status.requested=thermal::Mode::whiteHot;
    status.effective=thermal::Mode::optical;status.reason=thermal::Reason::unsupportedSurface;
    Line controls{};ThermalControlText(controls,status);
    TaskCheck(wcsstr(controls.text,Tr(Tx::thermalFallback))!=nullptr,"surface failure explicitly reports optical fallback");
    TaskCheck(wcsstr(controls.text,Tr(Tx::thermalWhiteHot))==nullptr,"failed thermal request never claims an active white-hot image");
    status.reason=thermal::Reason::notInSight;ThermalControlText(controls,status);
    TaskCheck(wcsstr(controls.text,Tr(Tx::thermalEnterSight))!=nullptr,"outside-sight reason explains how to enable thermal");
}
}
int main() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    static unsigned char fontMgr[0x200]{},drawer[16]{};
    *reinterpret_cast<void**>(image+kQuadDrawer)=drawer;*reinterpret_cast<void**>(image+kFontMgr)=fontMgr;
    Jump(kQuad,reinterpret_cast<const void*>(&QuadRec));Jump(kTextMake,reinterpret_cast<const void*>(&MakeRec));
    Jump(kTextBegin,reinterpret_cast<const void*>(&BeginRec));Jump(kTextMeasure,reinterpret_cast<const void*>(&MeasureRec));
    Jump(kTextDraw,reinterpret_cast<const void*>(&DrawRec));Jump(kTextEnd,reinterpret_cast<const void*>(&EndRec));
    Jump(kTextFree,reinterpret_cast<const void*>(&FreeRec));quadOk=textOk=uiOk=true;
    const int sizes[][2]={{1920,1080},{1280,720},{960,540},{960,1080},{3840,2160}};
    for(int lang=0;lang<hudtext::kLangs;++lang) {
        hudtext::Use(static_cast<hudtext::Lang>(lang));
        for(const auto& size:sizes){
            for(int kind=0;kind<7;++kind){PvpCase(size[0],size[1],false,kind);PvpCase(size[0],size[1],true,kind);}
            ThermalCases(size[0],size[1]);
        }
    }
    std::printf("PVP/thermal HUD: %d checks passed\n",taskChecks);return 0;
}
