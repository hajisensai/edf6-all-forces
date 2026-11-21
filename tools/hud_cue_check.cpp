// The HUD's switch cues (src/hud_cue.h) checked offline: a switch of the picked store or of EDF6AutoTurret's aim mode
// shows its banner for the given time and then not, the first value seen (boarding) shows none, a value back again
// shows anew; every plugin store and every stock round class gets the picture of its kind.
//   cmake --build build --target hud_cue_check && build\hud_cue_check.exe      (exit code 1 on a failure)
#include "../src/hud_cue.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int failures=0,cases=0;
void Check(bool ok,const char* what) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s\n",what);
}
}  // namespace

int main() {
    using namespace crew::hudcue;
    constexpr unsigned long long kMs=1500;
    Change c{};
    Check(!Changed(c,0,1000,kMs),"the first value seen (boarding): no banner");
    Check(!Changed(c,0,1100,kMs),"no switch: no banner");
    Check(Changed(c,1,2000,kMs),"a switch: the banner");
    Check(Changed(c,1,3499,kMs),"the banner kMs long");
    Check(!Changed(c,1,3500,kMs),"then gone");
    Check(Changed(c,0,4000,kMs),"switched back: the banner again");
    Check(Changed(c,1,4100,kMs) && Changed(c,1,5599,kMs) && !Changed(c,1,5600,kMs),"a switch during a banner starts it anew");
    Change unseen{};
    unseen.seen=false;
    Check(!Changed(unseen,3,10000,kMs),"forgotten (out of the aircraft) and seen again: no banner");

    // The plugin's stores (stores.inc names) by name; a name not known by its role.
    Check(StoreIconOf("AIM-9X",0)==StoreIcon::aamShort,"AIM-9X");
    Check(StoreIconOf("AIM-120",0)==StoreIcon::aam,"AIM-120");
    Check(StoreIconOf("AIM-54",0)==StoreIcon::aamLong,"AIM-54");
    Check(StoreIconOf("AGM-65",1)==StoreIcon::agm,"AGM-65");
    Check(StoreIconOf("AGM-114",1)==StoreIcon::agmLight,"AGM-114");
    Check(StoreIconOf("Mk 82",2)==StoreIcon::bomb,"Mk 82");
    Check(StoreIconOf("Hydra 70",3)==StoreIcon::rocket,"Hydra 70");
    Check(StoreIconOf("CANNON",2)==StoreIcon::gun && StoreIconOf("GATLING",2)==StoreIcon::gun,"the gunship's side guns: rounds");
    Check(StoreIconOf("NEW",1)==StoreIcon::agm && StoreIconOf("NEW",2)==StoreIcon::bomb && StoreIconOf("NEW",3)==StoreIcon::rocket &&
          StoreIconOf(nullptr,0)==StoreIcon::aam,"by role");
    Check(StoreIconOf("DRONES",5)==StoreIcon::drone,"drone deployment gets aircraft icon, not bomb");
    Check(StoreIconOf("SHELLS",4)==StoreIcon::gun && StoreIconOf("CHARGE",6)==StoreIcon::charge,"special gun and detonation roles retain their semantics");
    Check(ArmIconOf(1,false,1)==StoreIcon::energy && ArmIconOf(1,false,2)==StoreIcon::energy && ArmIconOf(1,false,3)==StoreIcon::energy,
          "laser, beam and maser are not drawn as machine-gun ammunition");
    // The stock rounds (vhud.h RoundKind: none 0, arc 1, rocket 2, homing 3).
    Check(ArmIconOf(1,false)==StoreIcon::gun && ArmIconOf(0,false)==StoreIcon::gun,"a gun's rounds");
    Check(ArmIconOf(1,false,0,true)==StoreIcon::shell,"a gun laid shot by shot (the 601's 140 mm cannon): one shell");
    Check(ArmIconOf(1,false,2,true)==StoreIcon::energy && ArmIconOf(3,false,0,true)==StoreIcon::aam,"  not over an energy weapon or a missile");
    Check(ArmIconOf(2,false)==StoreIcon::rocket,"rockets");
    Check(ArmIconOf(3,false)==StoreIcon::aam,"a homing missile");
    Check(ArmIconOf(1,true)==StoreIcon::bomb,"a lobbed round");

    // A cell's loading bar (vhud.cpp Reload / Cycle).
    Check(CellLoad(5,1.0f,1.8f,0.25f)==0.25f,"the next round going in: its share");
    Check(CellLoad(5,1.0f,-1.0f,1.0f)<0.0f,"ready (or a quick gun): no bar");
    Check(CellLoad(0,0.4f,-1.0f,1.0f)==0.4f && CellLoad(0,1.0f,-1.0f,1.0f)<0.0f,"the magazine reloading: its share; empty with no reload: no bar");
    Check(CellLoad(5,1.0f,0.1f,1.7f)==1.0f && CellLoad(0,-0.2f,-1.0f,1.0f)==0.0f,"  kept within 0..1");

    // The strip's left-to-right order by the muzzles' sides (+ left of the vehicle's middle, as the camera looks).
    {
        const float nix[2]={-2.1f,2.2f};   // the Nix lists its right arm's rifle first (screenshot 2026-10-12)
        int o[2];SideOrder(nix,2,o);
        Check(o[0]==1 && o[1]==0,"a Nix's rifles: the left arm's on the left, the right arm's on the right");
        const float tank[5]={0.0f,0.0f,0.0f,-0.3f,1.4f};   // main gun, its two stores, the coax (0.3 m right), a left launcher
        int t[5];SideOrder(tank,5,t);
        Check(t[0]==4 && t[1]==0 && t[2]==1 && t[3]==2 && t[4]==3,"a tank: the launcher out on the left first, the gun's stores in their R order");
        const float unknown[3]={0.0f,0.0f,0.0f};
        int u[3];SideOrder(unknown,3,u);
        Check(u[0]==0 && u[1]==1 && u[2]==2,"no muzzle read: as the seat lists them");
    }

    // The sides in the hull's frame (HullSide; the user, 2026-11-21: 「载具的挂载不要跟随视角变位置」). A tank whose turret
    // turns about a pivot 1 m behind and 0.3 m left of the hull's origin, carrying the main gun on the bore line (0.1 m
    // left of the pivot), a launcher 1.2 m left of the bore and a coax 0.3 m right of it; a hull machine gun 1.5 m right
    // of the origin, fixed. Turning the turret (and so the bore) any way keeps every side and the strip's order.
    {
        const float centre[3]={10.0f,2.0f,-4.0f},pivot[3]={10.3f,3.0f,-5.0f};   // the hull level, its nose +z, its left +x
        const float left[3]={1.0f,0.0f,0.0f},up[3]={0.0f,1.0f,0.0f};
        // In the turret's own frame (turned 0: across +x, along +z): offsets of the muzzles from the pivot.
        const float gun[3]={0.1f,0.2f,4.0f},launcher[3]={1.3f,0.6f,0.5f},coax[3]={-0.2f,0.1f,1.5f};
        const float hullGun[3]={8.5f,2.5f,-1.0f},hullBore[3]={0.0f,0.0f,1.0f};
        const float yaws[4]={0.0f,90.0f,180.0f,-90.0f};
        float first[4]={};int firstOrder[4]={};bool same=true,orderSame=true,hullSame=true;
        for(int y=0;y<4;++y) {
            const float ang=yaws[y]*3.14159265f/180.0f,co=std::cos(ang),si=std::sin(ang);
            // A yaw left by `ang` about the hull's up: the turret's across (+x) goes to (c, 0, -s), its along (+z) to (s, 0, c).
            const auto turn=[&](const float* o,float* world){
                world[0]=pivot[0]+o[0]*co+o[2]*si;world[1]=pivot[1]+o[1];world[2]=pivot[2]-o[0]*si+o[2]*co;
            };
            const float bore[3]={si,0.0f,co};
            float mg[3],ml[3],mc[3];turn(gun,mg);turn(launcher,ml);turn(coax,mc);
            float side[4]={HullSide(mg,bore,pivot,centre,left,up),HullSide(ml,bore,pivot,centre,left,up),
                           HullSide(mc,bore,pivot,centre,left,up),HullSide(hullGun,hullBore,nullptr,centre,left,up)};
            int order[4];SideOrder(side,4,order);
            if(y==0) {
                Check(std::fabs(side[0]-0.4f)<1e-4f && std::fabs(side[1]-1.6f)<1e-4f && std::fabs(side[2]-0.1f)<1e-4f,
                      "the turret's weapons: across the bore from the pivot plus the pivot's place on the hull");
                Check(std::fabs(side[3]+1.5f)<1e-4f,"a hull-fixed weapon: its muzzle across the hull from the origin");
                std::memcpy(first,side,sizeof first);std::memcpy(firstOrder,order,sizeof firstOrder);
                Check(order[0]==1 && order[1]==0 && order[2]==2 && order[3]==3,"the launcher left, the gun and coax middle, the hull gun right");
                continue;
            }
            for(int i=0;i<4;++i)if(std::fabs(side[i]-first[i])>1e-4f)(i==3 ? hullSame : same)=false;
            for(int i=0;i<4;++i)if(order[i]!=firstOrder[i])orderSame=false;
        }
        Check(same,"the turret turned 90 / 180 / -90: its weapons' sides stay");
        Check(hullSame,"the hull-fixed weapon's side stays");
        Check(orderSame,"so the strip's order stays");
        {   // What the bore-only reading did: the same gun turned 90 left read 1 m off (the pivot 1 m behind the origin).
            const float bore[3]={1.0f,0.0f,0.0f};
            const float mg[3]={pivot[0]+gun[2],pivot[1]+gun[1],pivot[2]-gun[0]};
            const float across[3]={bore[2],0.0f,-bore[0]};
            const float old=(mg[0]-centre[0])*across[0]+(mg[2]-centre[2])*across[2];
            Check(std::fabs(old-first[0])>0.5f && std::fabs(HullSide(mg,bore,pivot,centre,left,up)-first[0])<1e-4f,
                  "  where across the bore alone it moved by the pivot's offset");
        }
        // The view is no input: the camera turning round the vehicle (free look) cannot change a side (2026-10-21).
        // A bore straight up (a mortar): read across the hull's left.
        const float upBore[3]={0.0f,1.0f,0.0f},mortar[3]={10.9f,4.0f,-5.0f};
        Check(std::fabs(HullSide(mortar,upBore,pivot,centre,left,up)-0.9f)<1e-4f,"a bore straight up: across the hull's left");
        // A tilted hull (rolled, pitched): the frame is the hull's, the sides stay the same.
        const float ang=0.3f,co=std::cos(ang),si=std::sin(ang);   // rolled about the nose (+z): left (c, s, 0), up (-s, c, 0)
        const float rl[3]={co,si,0.0f},ru[3]={-si,co,0.0f};
        const auto roll=[&](const float* o,float* w){w[0]=centre[0]+o[0]*co-o[1]*si;w[1]=centre[1]+o[0]*si+o[1]*co;w[2]=centre[2]+o[2];};
        const float hp[3]={pivot[0]-centre[0],pivot[1]-centre[1],pivot[2]-centre[2]};
        const float hm[3]={hp[0]+gun[0],hp[1]+gun[1],hp[2]+gun[2]},hb[3]={0.0f,0.0f,1.0f};
        float wp[3],wm[3],wb[3]={hb[0]*co-hb[1]*si,hb[0]*si+hb[1]*co,hb[2]};roll(hp,wp);roll(hm,wm);
        Check(std::fabs(HullSide(wm,wb,wp,centre,rl,ru)-first[0])<1e-4f,"a rolled hull: the same side");
        const float zero[3]={};
        Check(HullSide(mortar,upBore,pivot,centre,zero,up)==0.0f,"no hull frame: unknown (0)");
    }
    {   // A Nix (2026-10-12): the arms hang off the torso, which the seat's yaw axis turns; the right arm listed first.
        const float centre[3]={0,0,0},pivot[3]={0,3,-0.6f},left[3]={1,0,0},up[3]={0,1,0};
        const float yaws[3]={0.0f,1.2f,-2.5f};
        bool kept=true;
        for(const float ang:yaws) {
            const float co=std::cos(ang),si=std::sin(ang),bore[3]={si,0,co};
            const float right[3]={pivot[0]-2.1f*co+1.0f*si,pivot[1],pivot[2]+2.1f*si+1.0f*co};
            const float leftArm[3]={pivot[0]+2.2f*co+1.0f*si,pivot[1],pivot[2]-2.2f*si+1.0f*co};
            const float side[2]={HullSide(right,bore,pivot,centre,left,up),HullSide(leftArm,bore,pivot,centre,left,up)};
            int o[2];SideOrder(side,2,o);
            if(!(o[0]==1 && o[1]==0))kept=false;
        }
        Check(kept,"a Nix's rifles: the left arm's on the left however the torso turns");
    }

    // The turret aim overlay follows the fire-control pick when the turret is laid by it (vhud.h sight / turret).
    Check(TurretOverlayOwned(0,0),"the stock main gun picked: the overlay");
    Check(TurretOverlayOwned(2,2),"a store on the main gun's holder picked (APFSDS / HE): the overlay, not only arm 0");
    Check(!TurretOverlayOwned(3,0),"a pick the turret does not turn with (the Titan's hull gatling): none");
    Check(!TurretOverlayOwned(-1,-1) && !TurretOverlayOwned(0,-1),"no pick, or no turret gun: none");

    // One list, one pick for the weapons screen, the loadout strips and the switch banner (SeatArms).
    {
        const float tank[3]={0.0f,0.0f,1.0f};   // the main gun, its HE store, a launcher out on the left
        SeatArms a=StockSeatArms(tank,3,1,0);
        Check(a.n==3 && a.rows[0].index==2 && a.rows[1].index==0 && a.rows[2].index==1,"the stock list is SideOrder's");
        Check(a.picked==2 && a.rows[a.picked].index==1,"the fire-control's selected arm is the picked row");
        a=StockSeatArms(tank,3,-1,0);
        Check(a.picked==1 && a.rows[a.picked].index==0,"no selected arm: the sight's (a door gunner), not none");
        a=StockSeatArms(tank,3,-1,-1);
        Check(a.picked>=0 && a.rows[a.picked].index==0,"neither: the first arm");
        a=StockSeatArms(tank,3,7,9);
        Check(a.picked>=0 && a.rows[a.picked].index==0,"picks out of range fall back to the first");
        Check(StockSeatArms(tank,0,0,0).n==0 && StockSeatArms(tank,0,0,0).picked<0,"no arms: no rows, no pick");
        a=JetSeatArms(3,1,true);
        Check(a.n==4 && a.rows[3].source==ArmSource::gun && a.picked==1 && a.rows[1].source==ArmSource::store,
              "a jet: its stores then its gun, the picked store");
        a=JetSeatArms(0,0,true);
        Check(a.n==1 && a.picked==0 && a.rows[0].source==ArmSource::gun,"no stores: the gun is the pick");
        Check(JetSeatArms(2,0,false).n==2,"no gun: stores only");
    }
    Check(CockpitRoundOfArm(1,false,0,true,crew::reticle::Shell::heat)==CockpitRound::heat
          && CockpitRoundOfArm(1,false,0,true,crew::reticle::Shell::canister)==CockpitRound::can
          && CockpitRoundOfArm(1,false,0,true,crew::reticle::Shell::none)==CockpitRound::gun
          && CockpitRoundOfArm(1,true,0,false,crew::reticle::Shell::none)==CockpitRound::he,
          "a stock weapon's round class by the sight's reticle::Shell tag");

    std::printf("hud_cue_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
