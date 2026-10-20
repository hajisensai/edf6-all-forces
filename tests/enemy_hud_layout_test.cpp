// Real production enemy-control card/HUD drawing through hud_view's private
// native render stand-ins. No game process or native object is accessed.
#define wmain hud_view_original_wmain
#include "../tools/hud_view.cpp"
#undef wmain
#include <cstdlib>
namespace {
int checks=0;
void Check(bool yes,const char* why) {++checks;if(!yes){std::printf("FAIL enemy HUD: %s\n",why);std::exit(1);}}
bool Cross(const Drew& a,const Drew& b) {
    return a.x0<b.x1-0.6f && b.x0<a.x1-0.6f && a.y0<b.y1-0.6f && b.y0<a.y1-0.6f;
}
bool TextHas(const wchar_t* part) { for(const auto& line:drew)if(line.text.find(part)!=std::wstring::npos)return true;return false; }
void CheckDrawing(float width,float height) {
    for(std::size_t i=0;i<drew.size();++i) {
        const auto& a=drew[i];if(a.text.empty())continue;
        Check(a.x0>=-0.6f && a.y0>=-0.6f && a.x1<=width+0.6f && a.y1<=height+0.6f,"recorded final text fits its viewport");
        for(std::size_t j=i+1;j<drew.size();++j)if(!drew[j].text.empty()) {
            if(Cross(a,drew[j]))std::printf("overlap %s / %s\n",Narrow(a.text).c_str(),Narrow(drew[j].text).c_str());
            Check(!Cross(a,drew[j]),"recorded final text has no row or header collisions");
        }
    }
}
void SetEnemy(enemy_control::Phase phase,bool health=true,int first=12) {
    sceneEnemy={};hasEnemyPanel=hasEnemyControl=true;sceneEnemy.visible=true;
    auto& snapshot=sceneEnemy.snapshot;snapshot.phase=phase;snapshot.revision=123;snapshot.lease=567;snapshot.request=890;
    snapshot.ownerHumanKey=0x11110000; snapshot.owner[0]=1;snapshot.target[0]=2;snapshot.profileHash=998877;
    snapshot.abilityCount=23;snapshot.controlledTeam=1;snapshot.sourceProtected=true;
    snapshot.health=12345;snapshot.maxHealth=67890;snapshot.healthKnown=health;
    const wchar_t* names[]={L"Large native enemy with a very long catalogue title",L"具有完整原生动作技能目录的超长名称敌方单位",L"具有完整原生動作技能目錄的超長名稱敵方單位",L"長いネイティブ動作スキル一覧を持つ巨大な敵ユニット"};
    wcscpy_s(snapshot.name,names[static_cast<int>(hudtext::InUse())]);
    sceneEnemy.first=first;sceneEnemy.count=(std::min)(enemyui::kPageRows,23-first);sceneEnemy.focused=true;sceneEnemy.padRow=sceneEnemy.count;
    for(int i=0;i<sceneEnemy.count;++i) {
        auto& ability=sceneEnemy.ability[i];ability.id=static_cast<unsigned>(100+first+i);ability.channel=static_cast<unsigned>(first+i);
        _snwprintf_s(ability.name,_countof(ability.name),_TRUNCATE,L"%ls %d",names[static_cast<int>(hudtext::InUse())],first+i+1);
        ability.state=i==1 ? enemy_control::AbilityState::cooldown : i==2 ? enemy_control::AbilityState::unavailable : enemy_control::AbilityState::native;
        ability.cooldownKnown=i==1;ability.cooldownSeconds=2.5f;
    }
    snapshot.selected=sceneEnemy.ability[0];snapshot.selectedAbility=snapshot.selected.id;
}
void RecordBegin() { drew.clear();recording=true;mapUiPanelCount=0;mapTip.on=false; }
void CardCase(int width,int height,enemy_control::Phase phase,bool health,bool pad,bool crowded,int first) {
    const float w=static_cast<float>(width),h=static_cast<float>(height);
    const float s=hudscale::FitMap(hudscale::Of(width,height,width,height,1.5f),w,h);
    SetEnemy(phase,health,first);MapTargetsNone();MapCommandReadout command{};command.pointer=!pad;
    command.px=w-20*s;command.py=220*s;
    const float top=206*s,bottom=crowded ? 250*s : h-180*s;
    float panelBottom=0;
    RecordBegin();Measured([&](Text* text,Line* lines,int* at){
        panelBottom=MapEnemyPanel(image,nullptr,text,w,s,top,bottom,pad,command,lines,at);
    },s);recording=false;
    CheckDrawing(w,h);Check(sceneEnemyVisible,"rendered card exposes its immutable snapshot");
    const bool active=phase==enemy_control::Phase::active;
    const int wanted=active ? sceneEnemy.count+3 : 1;
    Check(sceneEnemyCount==wanted,"active and pending expose actual attack rows, pages and exit (no fire row)");
    Check(sceneEnemyHitSnapshot.revision==123 && sceneEnemyHitSnapshot.lease==567 && sceneEnemyHitSnapshot.request==890 &&
        sceneEnemyHitSnapshot.ownerHumanKey==0x11110000 && sceneEnemyHitSnapshot.target[0]==2,"click ledger carries displayed owner, target, lease, request and revision");
    bool exit=false,begin=false;int skills=0;
    for(int i=0;i<sceneEnemyCount;++i) {
        const auto* r=sceneEnemyRects[i];Check(r[0]>=0 && r[1]>=0 && r[2]<=w+0.5f && r[3]<=panelBottom+0.5f,"control hit is inside viewport and its card");
        exit=exit || sceneEnemyCodes[i]==enemyui::kExit;begin=begin || sceneEnemyCodes[i]==enemyui::kBegin;
        if(sceneEnemyCodes[i]>=0){Check(sceneEnemyCodes[i]>=first && sceneEnemyCodes[i]<first+sceneEnemy.count,"skill buttons keep full-catalogue ordinal");++skills;}
    }
    Check(exit && !begin,"active and pending retain a real return action without starting a replacement control");
    if(active) {
        Check(skills==sceneEnemy.count,"every native skill in the current full page has an action");
        if(health)Check(TextHas(L"12345") && TextHas(L"67890"),"target actual current and maximum HP are rendered");
        else Check(TextHas(Tr(Tx::enemyControlHealthUnavailable)),"unknown target health is explicitly unavailable");
        Check(TextHas(Tr(Tx::enemyControlSourceSafe)),"active card explains source protection from true snapshot state");
    }
    MapTargetsNone();Check(!sceneEnemyVisible && !sceneEnemyCount,"map close drops all enemy action targets");
}
void InspectCase(int width,int height) {
    const float w=static_cast<float>(width),h=static_cast<float>(height),s=hudscale::FitMap(1.5f,w,h),top=206*s,bottom=h-180*s;
    SetEnemy(enemy_control::Phase::idle);sceneEnemy.focused=false;sceneEnemy.padRow=-1;
    MapTargetsNone();MapCommandReadout command{};float end=0;
    RecordBegin();Measured([&](Text* text,Line* lines,int* at){end=MapEnemyPanel(image,nullptr,text,w,s,top,bottom,false,command,lines,at);},s);recording=false;
    CheckDrawing(w,h);Check(sceneEnemyVisible && sceneEnemyCount==1 && sceneEnemyCodes[0]==enemyui::kFold && std::fabs(end-top-28*s)<0.2f,
        "folded idle card is one clickable title without permanently consuming source loadout space");
    command.pointer=true;command.px=w-30*s;command.py=top+12*s;
    RecordBegin();Measured([&](Text* text,Line* lines,int* at){end=MapEnemyPanel(image,nullptr,text,w,s,top,bottom,false,command,lines,at);},s);recording=false;
    CheckDrawing(w,h);Check(sceneEnemyCount==1 && std::fabs(end-top-28*s)<0.2f,"hovering the title never unfolds the card by itself");
    sceneEnemy.expanded=true;
    RecordBegin();Measured([&](Text* text,Line* lines,int* at){end=MapEnemyPanel(image,nullptr,text,w,s,top,bottom,false,command,lines,at);},s);recording=false;
    CheckDrawing(w,h);
    bool begin=false,prev=false,next=false,fold=false;int beginAt=-1;
    for(int i=0;i<sceneEnemyCount;++i) {
        begin=begin || sceneEnemyCodes[i]==enemyui::kBegin;if(sceneEnemyCodes[i]==enemyui::kBegin)beginAt=i;
        prev=prev || sceneEnemyCodes[i]==enemyui::kTargetPrev;next=next || sceneEnemyCodes[i]==enemyui::kTargetNext;fold=fold || sceneEnemyCodes[i]==enemyui::kFold;
    }
    Check(sceneEnemyCount==4 && begin && prev && next && fold,"the unfolded card offers control, the target arrows and its fold title");
    command.px=(sceneEnemyRects[beginAt][0]+sceneEnemyRects[beginAt][2])*0.5f;command.py=(sceneEnemyRects[beginAt][1]+sceneEnemyRects[beginAt][3])*0.5f;
    Measured([&](Text* text,Line* lines,int* at){MapEnemyPanel(image,nullptr,text,w,s,top,bottom,false,command,lines,at);},s);
    Check(sceneEnemyCount==4 && sceneEnemyHitSnapshot.target[0]==2,"moving from title to control button keeps the target and the unfolded card");
    sceneEnemy.snapshot.reason=enemy_control::Reason::building;
    RecordBegin();Measured([&](Text* text,Line* lines,int* at){MapEnemyPanel(image,nullptr,text,w,s,top,bottom,false,command,lines,at);},s);recording=false;
    bool refusedBegin=false;for(int i=0;i<sceneEnemyCount;++i)refusedBegin=refusedBegin || sceneEnemyCodes[i]==enemyui::kBegin;
    CheckDrawing(w,h);Check(sceneEnemyVisible && sceneEnemyCount==3 && !refusedBegin && TextHas(L"Fixture native unavailable"),"excluded buildings expose a concrete native refusal without a control action");
    MapTargetsNone();
}
void HudCase(int uiW,int uiH,int width,int height,float user,bool health) {
    const float w=static_cast<float>(width),h=static_cast<float>(height),s=hudscale::Of(uiW,uiH,width,height,user);
    SetEnemy(enemy_control::Phase::active,health);passPlayerRefKey=sceneEnemy.snapshot.ownerHumanKey;passPlayerSoldierKey=0;
    RecordBegin();Measured([&](Text* text,Line* lines,int* at){EnemyControlHud(image,nullptr,text,w,h,s,lines,at);},s);recording=false;
    CheckDrawing(w,h);
    // The enemy's HUD where the player's was: its name top left, each attack's key -> name bottom right.
    bool nameTopLeft=false,keysBottomRight=false;int attackNames=0;
    wchar_t title[256]{};_snwprintf_s(title,_countof(title),_TRUNCATE,Tr(Tx::enemyControlActive),sceneEnemy.snapshot.name);
    for(const auto& line:drew) {
        if(line.text==title)nameTopLeft=line.x0<w*0.5f && line.y1<h*0.5f;
        if(line.text==std::wstring(Tr(Tx::enemyKeyMouseLeft))+L" / RT")keysBottomRight=line.x0>w*0.45f && line.y0>h*0.25f;
        for(int i=0;i<sceneEnemy.count;++i)if(line.text==sceneEnemy.ability[i].name)++attackNames;
    }
    Check(nameTopLeft,"the controlled enemy's name is in the armor gauge's corner");
    Check(keysBottomRight,"each attack's own key and pad button on the right, where the weapon gauges were");
    Check(TextHas(Tr(Tx::enemyKeyMouseLeft)) && attackNames>0,"keys are listed with their attacks");
    Check(TextHas(Tr(Tx::enemyControlAttacks)) && TextHas(Tr(Tx::enemyControlPlayHint)),"the attack block is titled and says how to use it");
    if(health)Check(TextHas(L"12345") && TextHas(L"67890"),"HUD health comes from target numbers rather than protected source");
    else Check(TextHas(Tr(Tx::enemyControlHealthUnavailable)),"HUD unknown target HP never borrows source health");
    passPlayerRefKey=0x22220000;RecordBegin();Measured([&](Text* text,Line* lines,int* at){EnemyControlHud(image,nullptr,text,w,h,s,lines,at);},s);recording=false;
    Check(drew.empty(),"another local player's viewport never displays this owner's enemy HUD");
}
void StackCase(int width,int height,bool active) {
    const float w=static_cast<float>(width),h=static_cast<float>(height),s=hudscale::FitMap(1.5f,w,h);
    SetEnemy(active ? enemy_control::Phase::active : enemy_control::Phase::idle);
    sceneEnemy.focused=false;sceneEnemy.padRow=-1;
    hasMap=true;MapReadout map{};map.ownerHumanKey=sceneEnemy.snapshot.ownerHumanKey;
    map.mapKey='M';sceneCmd={};sceneCmd.allowed=true;sceneCmd.pointer=true;
    sceneCmd.placeOn=sceneCmd.placeOpen=sceneCmd.placeOnline=true;
    sceneCmd.placeCategory=static_cast<int>(debugspawn::Category::enemy41);
    sceneCmd.supportArmed=sceneCmd.composeEntry=sceneCmd.placeArmed=-1;
    for(auto& state:sceneCmd.placeState)state=debugspawn::RowState::ready;
    scenePvp={};scenePvp.ready=scenePvp.host=true;scenePvp.epoch=123;scenePvp.revision=456;strcpy_s(scenePvp.localPuid,"layout-owner");
    hasMapPayload=true;sceneMapPayload={};sceneMapPayload.count=kMostPayload;sceneMapPayload.selectionToken=1;
    for(auto& entry:sceneMapPayload.entry){entry.selectable=true;wcscpy_s(entry.name,L"Native payload long name");}
    float vp[16]{};vp[0]=vp[5]=vp[10]=vp[15]=1;
    scenePvpOpen=false;RecordBegin();Measured([&](Text* text,Line* lines,int* at){MapCommands(image,nullptr,text,vp,w,h,s,map,lines,at);},s);recording=false;
    bool found=false;
    for(const auto& line:drew)if(line.text==Tr(Tx::pvpExpand)) {
        sceneCmd.px=(line.x0+line.x1)*0.5f;sceneCmd.py=(line.y0+line.y1)*0.5f;found=true;break;
    }
    Check(found,"real PVP tab remains discoverable below the enemy card");
    scenePvpOpen=found;   // its title clicked (kPvpFold): the panel opens by a click, never by the pointer passing
    RecordBegin();Measured([&](Text* text,Line* lines,int* at){MapCommands(image,nullptr,text,vp,w,h,s,map,lines,at);},s);recording=false;
    CheckDrawing(w,h);
    if(!(sceneEnemyVisible && (active ? sceneEnemyCount>0 : sceneEnemyCount==1) && scenePvpCount==4 && scenePlaceHitCount>0))std::printf("stack %dx%d active=%d enemy=%d pvp=%d place=%d payload=%d lang=%d pointer=%.1f/%.1f\n",width,height,active,sceneEnemyCount,scenePvpCount,scenePlaceHitCount,scenePayloadButtonCount,static_cast<int>(hudtext::InUse()),sceneCmd.px,sceneCmd.py);
    Check(sceneEnemyVisible && (active ? sceneEnemyCount>0 : sceneEnemyCount==1) && scenePvpCount==4 && scenePlaceHitCount>0,"enemy/PVP/placement actions remain reachable in the production stack");
    Check(scenePayloadButtonCount==(active ? 0 : kMostPayload),"active enemy skills replace paused source loadout while inspection retains payload choices");
    const auto overlaps=[](const float* a,const float* b){return a[0]<b[2] && b[0]<a[2] && a[1]<b[3] && b[1]<a[3];};
    for(int i=0;i<sceneEnemyCount;++i) {
        for(int j=0;j<scenePvpCount;++j)Check(!overlaps(sceneEnemyRects[i],scenePvpRects[j]),"enemy actions never cover PVP actions");
        for(int j=0;j<scenePlaceHitCount;++j)Check(!overlaps(sceneEnemyRects[i],scenePlaceHits[j]),"enemy actions never cover placement actions");
        for(int j=0;j<scenePayloadButtonCount;++j)Check(!overlaps(sceneEnemyRects[i],scenePayloadButtons[j]),"enemy inspection never covers source payload actions");
    }
    hasMapPayload=hasMap=false;sceneCmd={};scenePvp={};MapTargetsNone();
}
void OwnerMapCase(int width,int height) {
    const float w=static_cast<float>(width),h=static_cast<float>(height),s=hudscale::FitMap(1,w,h);
    SetEnemy(enemy_control::Phase::active);hasMap=true;sceneMap={};sceneMap.ownerHumanKey=sceneEnemy.snapshot.ownerHumanKey;
    sceneMap.height=200;sceneMap.pitch=1.0f;sceneMap.mapKey='M';sceneCmd={};sceneCmd.allowed=true;sceneCmd.pointer=true;
    sceneCmd.supportArmed=sceneCmd.composeEntry=sceneCmd.placeArmed=-1;
    sceneStatsView={};float vp[16]{};MapCamera(sceneMap,w,h,vp);
    passPlayerRefKey=sceneMap.ownerHumanKey;passPlayerSoldierKey=0;
    Measured([&](Text* text,Line* lines,int* at){Check(MapScreen(image,nullptr,text,vp,w,h,s,lines,at),"map owner executes real production map draw");},s);
    Check(sceneEnemyVisible && sceneEnemyCount>0,"owner map publishes actual enemy button targets");
    const unsigned views=sceneMapViewWrites;const int hits=sceneEnemyCount;const auto identity=sceneEnemyHitSnapshot;
    passPlayerRefKey=0x22220000;
    Measured([&](Text* text,Line* lines,int* at){Check(!MapScreen(image,nullptr,text,vp,w,h,s,lines,at),"foreign viewport declines this map before drawing");},s);
    Check(sceneMapViewWrites==views && sceneEnemyCount==hits && sceneEnemyHitSnapshot.ownerHumanKey==identity.ownerHumanKey,
        "foreign viewport neither overwrites map view nor clears owner hit identity");
    hasMap=false;MapTargetsNone();
}
}
int main() {
    image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2200000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(image!=nullptr,"private fake native image allocated");
    static unsigned char fontMgr[0x200]{},drawer[16]{};
    *reinterpret_cast<void**>(image+kQuadDrawer)=drawer;*reinterpret_cast<void**>(image+kFontMgr)=fontMgr;
    Jump(kQuad,reinterpret_cast<const void*>(&QuadRec));Jump(kTextMake,reinterpret_cast<const void*>(&MakeRec));
    Jump(kTextBegin,reinterpret_cast<const void*>(&BeginRec));Jump(kTextMeasure,reinterpret_cast<const void*>(&MeasureRec));
    Jump(kTextDraw,reinterpret_cast<const void*>(&DrawRec));Jump(kTextEnd,reinterpret_cast<const void*>(&EndRec));Jump(kTextFree,reinterpret_cast<const void*>(&FreeRec));
    quadOk=textOk=uiOk=true;
    const int sizes[][4]={{1920,1080,1920,1080},{1280,720,1280,720},{1920,1080,960,540},{1920,1080,960,1080},{1920,1080,1920,540},{3840,2160,3840,2160}};
    for(int lang=0;lang<hudtext::kLangs;++lang) {
        hudtext::Use(static_cast<hudtext::Lang>(lang));
        for(const auto& size:sizes) {
            for(bool pad:{false,true})for(bool hp:{false,true}) {
                CardCase(size[2],size[3],enemy_control::Phase::active,hp,pad,false,12);
                CardCase(size[2],size[3],enemy_control::Phase::active,hp,pad,true,18);
            }
            CardCase(size[2],size[3],enemy_control::Phase::pending,false,false,false,12);
            CardCase(size[2],size[3],enemy_control::Phase::restoring,false,true,false,12);
            for(float user:{1.0f,1.5f})for(bool hp:{false,true})HudCase(size[0],size[1],size[2],size[3],user,hp);
            InspectCase(size[2],size[3]);
            StackCase(size[2],size[3],false);StackCase(size[2],size[3],true);
            OwnerMapCase(size[2],size[3]);
        }
    }
    Check(activeTextRenderers==0,"every production text renderer is released");
    VirtualFree(image,0,MEM_RELEASE);image=nullptr;
    std::printf("enemy production HUD layout: %d checks passed\n",checks);return 0;
}
