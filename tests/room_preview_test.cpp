// The room / HQ 3D preview's pure logic (src/room_preview_logic.h) offline: every soldier kind's menu SGO, player class
// and weapons; the colours (stock = the template's, 0xRRGGBB as the generator writes them); the Desc copy rewritten byte
// for byte (the path and nothing past the weapon count left alone); the camera shift applied, re-based when the game
// rewrites a point and restored exactly; and the planner's frames: lock once, build once per model, recolour only the
// changed channel of the soldier on display, wait for a pending build before putting the player back, unlock last.
// Exit code 1 when one fails.  cmake --build build --target room_preview_test && build\room_preview_test.exe
#include "../src/room_preview_logic.h"
#include <cstdio>
#include <cwchar>
#include <cwctype>

namespace {
using namespace crew;
using namespace crew::roompreview;
int failures=0,checks=0;
void Expect(bool ok,const char* what) {
    ++checks;
    if(!ok){++failures;std::printf("FAIL %s\n",what);}
}
SupportWeapon Kind(int i) { return static_cast<SupportWeapon>(i); }
std::int32_t Int(const unsigned char* b,std::size_t off) { std::int32_t v;std::memcpy(&v,b+off,4);return v; }
float Float(const unsigned char* b,std::size_t off) { float v;std::memcpy(&v,b+off,4);return v; }

void Models() {
    wchar_t f[96],q[96];
    for(int i=0;i<kSupportWeaponCount;++i) {
        const int c=SupportSoldierClass(Kind(i));
        Expect(MenuClassOf(Kind(i))==(c==0 ? 0 : c==1 ? 1 : 3),"class: Ranger 0, Wing Diver 1, Fencer 3");
        const wchar_t* t=kSoldierTemplates[i];
        Expect(!std::wcsncmp(t,c==0 ? L"N601_" : c==1 ? L"N606_" : L"N607_",5),"the template is the class's (N601 / N606 / N607)");
        for(int l=0;l<2;++l) {
            const bool leader=l==1;
            const std::size_t nf=SoldierFileOf(Kind(i),leader,false,f,96),nq=SoldierFileOf(Kind(i),leader,true,q,96);
            Expect(nf>0 && nq>0,"every kind has its unified file");
            Expect(!std::wcsncmp(f,L"OBJECT\\",7) && !std::wcsncmp(q,L"app:/object/",12),"under Mods/OBJECT; an app:/object path");
            Expect(nf-7==nq-12,"the file and the path the same name");
            bool same=true;
            for(std::size_t k=0;k<nf-7;++k)same=same && std::towlower(f[7+k])==q[12+k];
            Expect(same,"the path is the file lower-cased");
            Expect(!std::wcsncmp(f+7,t,std::wcslen(t)),"named by the template");
            Expect(!std::wcscmp(f+nf-(leader ? 11 : 4),leader ? L"_LEADER.SGO" : L".SGO"),"the leader's _LEADER file");
        }
        const MenuWeapons w=MenuWeaponsOf(Kind(i));
        Expect(w.count==(c==2 ? 2 : 1),"weapons: one, a Fencer's shield and weapon");
        if(c==2)Expect(!std::wcscmp(w.name[1],L"hShield01") && std::wcscmp(w.name[0],L"hShield01"),
                       "Fencer: weapons[0] the right-hand weapon, [1] the shield (the battle soldier_weapon_slot)");
    }
    Expect(SoldierFileOf(SupportWeapon::count,false,false,f,96)==0 && f[0]==0,"no kind: none");
    Expect(SoldierFileOf(SupportWeapon::rifle,true,true,f,10)==0 && f[0]==0,"too small: none");
    Expect(SoldierFileOf(SupportWeapon::rifle,false,false,f,96)>0 && !std::wcscmp(f,L"OBJECT\\N601_COMMON_RANGER_AF.SGO"),"rifle file");
    Expect(SoldierFileOf(SupportWeapon::wingLance,true,true,f,96)>0 && !std::wcscmp(f,L"app:/object/n606_aipalewing_lance_leader.sgo"),
           "Wing Diver leader path");
    Expect(!std::wcscmp(MenuWeaponsOf(SupportWeapon::rifle).name[0],L"aWeapon003"),"rifle row name");
    Expect(!std::wcscmp(MenuWeaponsOf(SupportWeapon::fencerPileBanker).name[0],L"hPileBanker01"),"pile banker row name");
    Expect(MenuWeaponsOf(SupportWeapon::count).count==0,"no weapons for an invalid kind");
    // The X-Morph infantry previews as the stock rifle Ranger (both bodies: member and leader).
    for(int l=0;l<2;++l) {
        const bool leader=l==1;
        Expect(MenuClassOf(SupportWeapon::xmInfantry)==kRangerClass,"X-Morph infantry: a Ranger's class");
        SoldierFileOf(SupportWeapon::xmInfantry,leader,true,f,96);SoldierFileOf(SupportWeapon::rifle,leader,true,q,96);
        Expect(!std::wcscmp(f,q),"X-Morph infantry: the rifle Ranger's unified file");
    }
    Expect(!std::wcscmp(MenuWeaponsOf(SupportWeapon::xmInfantry).name[0],MenuWeaponsOf(SupportWeapon::rifle).name[0]),
           "X-Morph infantry: the rifle's weapon");
}

void Colours() {
    const Rgba stock{{0.1f,0.2f,0.3f,1.f}};
    const Rgba c=ColourOf(0xFF8000,stock);
    Expect(c.v[0]==1.f && c.v[1]==128.f/255.f && c.v[2]==0.f && c.v[3]==1.f,"0xRRGGBB to float4, alpha 1");
    Expect(SameRgba(ColourOf(kStockColour,stock),stock.v),"stock: the template's");
    Expect(SameRgba(StockOf(SupportWeapon::rocket,true).c[0],kRangerLeaderHead.c[0].v),"Ranger leaders: head colours");
    Expect(SameRgba(StockOf(SupportWeapon::rifle,true).c[0],kRangerLeaderBody.c[0].v),"Rifle leader: body colours");
    for(int l=0;l<2;++l)
        for(int ch=0;ch<2;++ch)
            Expect(SameRgba(StockOf(SupportWeapon::xmInfantry,l==1).c[ch],StockOf(SupportWeapon::rifle,l==1).c[ch].v),
                   "X-Morph infantry: the rifle Ranger's stock colours, member and leader");
    Expect(SameRgba(StockOf(SupportWeapon::wingLaser,true).c[0],StockOf(SupportWeapon::wingLaser,false).c[0].v),
           "Wing Diver leader: as the soldier (the face tint is no channel)");
    Expect(SameRgba(StockOf(SupportWeapon::count,false).c[0],kRangerStock.c[0].v),"invalid kind: no read past the table");
}

void Desc() {
    unsigned char d[kDescSize];
    std::memset(d,0xCD,sizeof(d));
    const auto row=[](const wchar_t* n) noexcept {return !std::wcscmp(n,L"hShield01") ? 747 : !std::wcscmp(n,L"hCannon01") ? -1 : 803;};
    DescOverride o=OverrideOf(SupportWeapon::fencerCannon,false,SupportLook{0x102030,kStockColour},row);
    Expect(o.weaponCount==1 && o.weapon[0]==747,"a row not in the table is left out");
    o=OverrideOf(SupportWeapon::fencerMiddleCannon,false,SupportLook{0x102030,kStockColour},row);
    Expect(o.weaponCount==2 && o.weapon[0]==803 && o.weapon[1]==747,"both rows, the shield second");
    RewriteDesc(d,o);
    Expect(d[kDescPath]==0xCD && d[0x1F]==0xCD,"the path (wstring) untouched");
    Expect(Int(d,kDescModelType)==3 && Int(d,kDescClass)==3,"Fencer: model type and class 3 (dual)");
    Expect(Float(d,kDescColour0)==0x10/255.f && Float(d,kDescColour0+4)==0x20/255.f && Float(d,kDescColour0+12)==1.f,"colour 0");
    Expect(Float(d,kDescColour1)==StockOf(SupportWeapon::fencerMiddleCannon,false).c[1].v[0],"colour 1 stock");
    Expect(Int(d,kDescOutfit)==kSentinelOutfit,"the sentinel outfit");
    Expect(Int(d,kDescWeaponCount)==2 && Int(d,kDescSelected)==0,"count and selected slot");
    Expect(Int(d,kDescWeapons)==803 && Int(d,kDescWeapons+4)==747,"weapons");
    Expect(d[kDescWeapons+8]==0xCD && d[kDescSize-1]==0xCD,"nothing past the count");
    o.weaponCount=99;RewriteDesc(d,o);
    Expect(Int(d,kDescWeaponCount)==kDescWeaponsMost,"count capped at six");
}

void Camera() {
    float pt[4][4]={{1.f,2.f,3.f,9.f},{2.f,2.f,2.f,9.f},{3.f,0.f,-1.f,9.f},{4.f,1.f,1.f,9.f}};
    float* x[4]={pt[0],pt[1],pt[2],pt[3]};
    CameraShift s;
    s.Apply(x,-0.5f);
    Expect(pt[0][0]==0.5f && pt[3][0]==3.5f && pt[0][1]==2.f && pt[0][3]==9.f,"shifted once, y / z / w kept");
    s.Apply(x,-0.5f);
    Expect(pt[0][0]==0.5f,"not again next frame");
    pt[1][0]=10.f;   // the game wrote a new base
    s.Apply(x,-0.5f);
    Expect(pt[1][0]==9.5f && pt[0][0]==0.5f,"a rewritten point re-based");
    s.Restore(x);
    Expect(pt[0][0]==1.f && pt[1][0]==10.f && pt[3][0]==4.f,"restored exactly");
    s.Restore(x);
    Expect(pt[0][0]==1.f,"restore twice: once");
    s.Apply(x,-0.5f);pt[2][0]=7.f;s.Restore(x);
    Expect(pt[2][0]==7.f && pt[0][0]==1.f,"a point the game rewrote is left as the game wrote it");
    // A vehicle: shifted, then the xyz scaled (farther along the same line of sight); back to a soldier: unscaled.
    s.Apply(x,-0.5f,3.f);
    Expect(pt[0][0]==1.5f && pt[0][1]==6.f && pt[0][2]==9.f && pt[0][3]==9.f,"vehicle: (x+dx, y, z) * scale, w kept");
    s.Apply(x,-0.5f,3.f);
    Expect(pt[0][0]==1.5f && pt[0][1]==6.f,"not scaled twice");
    s.Apply(x,-0.5f,1.f);
    Expect(pt[0][0]==0.5f && pt[0][1]==2.f && pt[0][2]==3.f,"back to a soldier: from the base");
    s.Apply(x,-0.5f,3.f);s.Restore(x);
    Expect(pt[0][0]==1.f && pt[0][1]==2.f && pt[0][2]==3.f && pt[1][0]==10.f,"restored from the scaled placement exactly");
    Expect(VehicleScale(VehicleBody::tank)==kTankScale && VehicleScale(VehicleBody::strike)==kJetScale &&
           VehicleScale(VehicleBody::rmultirole)==kJetScale && VehicleScale(VehicleBody::mech)==kTankScale &&
           VehicleScale(VehicleBody::none)==1.f,"scale by body (the mech framed as the tank)");
    Expect(Turned(1.f,0.5f)==1.5f && Turned(6.28f,0.01f)<0.01f && Turned(0.f,-0.01f)>6.f,"the turntable wraps");
    // The turntable: the stand's yaw saved when a vehicle first turns, put back once none does (soldier / closed).
    Turntable t;
    Expect(t.Hold(0.25f,false)==0.25f && !t.on,"no vehicle: the yaw held as it is");
    float y=t.Hold(0.25f,true);
    Expect(t.on && y==Turned(0.25f,kVehicleTurn),"a vehicle: turns from the stand's yaw");
    for(int i=0;i<10;++i)y=t.Hold(y,true);
    Expect(y!=0.25f && t.base==0.25f,"keeps turning, the stand's yaw kept");
    Expect(t.Hold(y,false)==0.25f && !t.on,"a soldier again (or the menu closed): the stand's yaw back");
    Expect(t.Hold(0.5f,false)==0.5f,"then held as it is");
    y=t.Hold(0.5f,true);
    Expect(t.base==0.5f,"another vehicle later: saved afresh");
}

void Vehicles() {
    wchar_t b[96];
    Expect(VehicleUnified(VehicleBody::tank) && VehicleFileOf(VehicleBody::tank,false,b,96)>0 &&
           !std::wcscmp(b,L"OBJECT\\V505_TANK_MISSION.SGO"),"the tank's base: unified (AutoTurret's install), previewed");
    Expect(VehicleUnified(VehicleBody::mech) && VehicleFileOf(VehicleBody::mech,true,b,96)>0 &&
           !std::wcscmp(b,L"app:/object/v504_begaruta.sgo"),"the mech's base too (tools/menu_preview.py)");
    Expect(!VehicleJet(VehicleBody::tank) && !VehicleJet(VehicleBody::mech) && VehicleJet(VehicleBody::strike),
           "only the jets need the jet menu CAS");
    // A loadout: the variant the battle spawns (support_loadout.h VehicleVariantFile), never the base.
    VehicleLoadout tank{};tank.body=VehicleBody::tank;tank.apGun=true;
    wchar_t lo[96],want[96];
    Expect(VehicleVariantFile(tank,lo,96)>0 && VehicleFileOf(VehicleBody::tank,tank,false,b,96)>0 && !std::wcsncmp(b,L"OBJECT\\",7) &&
           !std::wcscmp(b+7,lo),"a loaded tank: its EDF6VC_LO_ file");
    Expect(VehicleVariantFile(tank,want,96,true)>0 && VehicleFileOf(VehicleBody::tank,tank,true,b,96)>0 && !std::wcscmp(b,want),
           "and its app:/ path, as the battle names it");
    VehicleLoadout mech{};mech.body=VehicleBody::mech;mech.count=1;
    for(int r=kMechRequestCount-1;r>=0 && !mech.mech.On();--r) {   // a request with four weapons, on another frame
        MechLoadout m=MechStock(r);m.frame=(MechFrameOf(m)+1)%kMechFrameCount;
        bool four=true;
        for(int i=0;i<kMechSlots;++i)four=four && m.weapon[i]>=0;
        if(four && MechValid(m))mech.mech=m;
    }
    Expect(mech.mech.On(),"a four-weapon mech on another frame");
    Expect(VehicleVariantFile(mech,lo,96)>0 && VehicleFileOf(VehicleBody::mech,mech,false,b,96)>0 && !std::wcscmp(b+7,lo) &&
           !std::wcsncmp(lo,L"EDF6VC_LO_MECH_",15),"a loaded mech: frame + core + four weapons, the battle's file");
    Expect(VehicleFileOf(VehicleBody::strike,tank,false,b,96)==0 && b[0]==0,"a loadout of another body: none");
    Expect(VehicleFileOf(VehicleBody::rmultirole,false,b,96)>0 && !std::wcscmp(b,L"OBJECT\\EDF6VC_FLY_MULTIROLE.SGO"),"jet file under Mods");
    Expect(VehicleFileOf(VehicleBody::strike,true,b,96)>0 && !std::wcscmp(b,L"app:/object/edf6vc_dn_support_strike.sgo"),"path");
    Expect(VehicleFileOf(VehicleBody::none,true,b,96)==0 && b[0]==0,"no body: none");
    Expect(VehicleFileOf(VehicleBody::strike,true,b,10)==0 && b[0]==0,"too small: none");
    Expect(!std::wcscmp(kJetMenuCas,L"MENUOBJECT\\EDF6VC_MENU_V506_HELI.CAS"),"the jets' menu CAS");
    for(const auto& row:kVehicleBodies) {
        wchar_t f[96],q[96];
        const bool unified=!row.tank && row.body!=VehicleBody::mech;
        Expect(VehicleUnified(row.body) && VehicleJet(row.body)==unified,"every body unified, the jets on the jet CAS");
        Expect(VehicleFileOf(row.body,false,f,96)>0 && VehicleFileOf(row.body,true,q,96)>0,"every body has a file");
        Expect(!std::wcscmp(f+7,row.base) && std::wcslen(q+12)==std::wcslen(row.base),"the file and the path name its base");
    }
    const DescOverride o=VehicleOverride();
    Expect(o.cls==kRangerClass && o.weaponCount==0,"a vehicle: Ranger class, no weapons");
    unsigned char d[kDescSize];std::memset(d,0xCD,sizeof(d));
    RewriteDesc(d,o);
    Expect(Int(d,kDescWeaponCount)==0 && Int(d,kDescOutfit)==kSentinelOutfit && d[kDescWeapons]==0xCD,"its Desc: no weapons written");
    // The planner: a vehicle is its own model (one build), no colour messages; soldier -> vehicle -> soldier rebuilds.
    Plan p;Want w;Shown s;float c0[4]={},c1[4]={};s.colour[0]=c0;s.colour[1]=c1;
    w.on=true;w.kind=SupportWeapon::rifle;
    Actions a=Step(p,w,s);
    Expect(a.lock && a.buildSoldier,"a soldier first");
    w.body=VehicleBody::tank;
    a=Step(p,w,s);
    Expect(a.buildSoldier && !a.lock,"the tank: its own build");
    s.cls=0;s.outfit=kSentinelOutfit;c0[0]=0.5f;
    a=Step(p,w,s);
    Expect(!a.Any(),"the tank shown: no recolour");
    w.body=VehicleBody::strike;
    a=Step(p,w,s);
    Expect(a.buildSoldier,"another body: rebuilt");
    w.body=VehicleBody::none;
    a=Step(p,w,s);
    Expect(a.buildSoldier,"back to the soldier: rebuilt");
    Expect(ModelKey(Want{true,SupportWeapon::rifle,false,VehicleBody::tank})!=ModelKey(SupportWeapon::rifle,false) &&
           ShownClassOf(Want{true,SupportWeapon::fencerCannon,false,VehicleBody::tank})==kRangerClass,"vehicle keys and class");
}

void Planner() {
    Plan p;Want w;Shown s;
    float c0[4]={},c1[4]={};
    s.colour[0]=c0;s.colour[1]=c1;s.cls=0;s.outfit=2;   // the player's Ranger
    Actions a=Step(p,w,s);
    Expect(!a.Any(),"idle: nothing");
    w.on=true;w.kind=SupportWeapon::fencerCannon;w.colour[0]=Rgba{{1,0,0,1}};w.colour[1]=Rgba{{0,1,0,1}};
    a=Step(p,w,s);
    Expect(a.lock && a.buildSoldier && !a.recolour[0],"open: lock and build");
    a=Step(p,w,s);
    Expect(!a.Any(),"the build not shown yet: nothing");
    s.cls=3;s.outfit=kSentinelOutfit;std::memcpy(c0,w.colour[0].v,16);std::memcpy(c1,w.colour[1].v,16);
    a=Step(p,w,s);
    Expect(!a.Any(),"shown as wanted: nothing");
    w.colour[1]=Rgba{{0,0,1,1}};
    a=Step(p,w,s);
    Expect(!a.buildSoldier && !a.recolour[0] && a.recolour[1],"colour change: msg 5 for that channel only");
    std::memcpy(c1,w.colour[1].v,16);
    w.kind=SupportWeapon::rifle;
    a=Step(p,w,s);
    Expect(a.buildSoldier && !a.lock,"another model: one rebuild, still locked");
    a=Step(p,w,s);
    Expect(!a.Any(),"the soldier shown is the old class: no recolour");
    w.on=false;s.pending=true;
    a=Step(p,w,s);
    Expect(!a.Any(),"close with a build pending: wait");
    s.pending=false;
    a=Step(p,w,s);
    Expect(a.buildPlayer && a.unlock,"close: the player back, then unlock");
    a=Step(p,w,s);
    Expect(!a.Any(),"closed: nothing");
    w.on=true;Step(p,w,s);w.on=false;s.pending=true;
    for(unsigned i=0;i<kMostWaitFrames;++i)a=Step(p,w,s);
    Expect(!a.Any(),"waits kMostWaitFrames");
    a=Step(p,w,s);
    Expect(a.buildPlayer && a.unlock,"then gives up waiting");
    w.on=true;Step(p,w,s);w.on=false;Step(p,w,s);w.on=true;
    a=Step(p,w,s);
    Expect(!a.Any(),"reopened the same soldier while waiting: no rebuild");
}
// The squad's other views (section 6.3): build on the first frame, hidden until the soldier is shown, the constructor's
// player build (or a late one) answered by one rebuild after kSquadRebuildFrames, recolour only what differs, a new
// soldier rebuilt at once; and how many views the preview keeps.
void Squad() {
    SquadPlan p;Want w;Shown s;
    float c0[4]={},c1[4]={};
    s.colour[0]=c0;s.colour[1]=c1;s.cls=0;s.outfit=2;   // what the constructor's own build shows: player 0's Ranger
    w.on=true;w.kind=SupportWeapon::fencerCannon;w.colour[0]=Rgba{{1,0,0,1}};w.colour[1]=Rgba{{0,1,0,1}};
    SquadActions a=SquadStep(p,w,s);
    Expect(a.build && !a.reveal,"first frame: build, still hidden");
    s.pending=true;
    for(unsigned i=0;i<kSquadRebuildFrames*2;++i)a=SquadStep(p,w,s);
    Expect(!a.build && !a.reveal,"our build not taken yet: wait, hidden, never rebuilt");
    s.pending=false;
    for(unsigned i=1;i<kSquadRebuildFrames;++i){a=SquadStep(p,w,s);Expect(!a.build && !a.reveal,"the player's model shown: hidden, waiting");}
    a=SquadStep(p,w,s);
    Expect(a.build && !a.reveal,"still the player's model after kSquadRebuildFrames: built again (a late constructor build)");
    s.cls=3;s.outfit=kSentinelOutfit;std::memcpy(c0,w.colour[0].v,16);
    a=SquadStep(p,w,s);
    Expect(a.reveal && !a.build && !a.recolour[0] && a.recolour[1],"the soldier shown: revealed, the differing channel sent");
    std::memcpy(c1,w.colour[1].v,16);
    a=SquadStep(p,w,s);
    Expect(a.reveal && !a.build && !a.recolour[0] && !a.recolour[1],"as wanted: shown, nothing sent");
    w.kind=SupportWeapon::rifle;
    a=SquadStep(p,w,s);
    Expect(a.build && !a.reveal,"another soldier: rebuilt at once, hidden");
    s.cls=0;s.outfit=kSentinelOutfit;
    a=SquadStep(p,w,s);
    Expect(a.reveal,"the Ranger shown: revealed");
    Expect(ExtraViewsWanted(true,4,true,true)==3 && ExtraViewsWanted(true,9,true,true)==3 && ExtraViewsWanted(true,2,true,true)==1,
           "three views at most beside the game's own");
    Expect(!ExtraViewsWanted(true,1,true,true) && !ExtraViewsWanted(false,4,true,true) && !ExtraViewsWanted(true,4,false,true) &&
           !ExtraViewsWanted(true,4,true,false),"none: one soldier, no squad, not placed, or the game's view not on a soldier");
}
}  // namespace

int main() {
    Models();
    Colours();
    Desc();
    Camera();
    Vehicles();
    Planner();
    Squad();
    std::printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
