// The out-of-game loadouts' pure logic (src/support_loadout.h): the preset and round-mix syntax, the per-tank
// allocation of a ratio (the user: "一半ap一半he"), the generated files' names, and the soldier ids' look bits.
#include "../src/support_loadout.h"
#include <cstdio>
#include <cstdlib>
#include <cwchar>

namespace {
int checks=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}
}
using namespace crew;
SupportPreset Preset(const wchar_t* text,int seats,bool* ok=nullptr,wchar_t* why=nullptr) {
    SupportPreset p;wchar_t local[128];
    const bool parsed=ParseSupportPreset(text,seats,&p,why ? why : local,128);
    if(ok)*ok=parsed;
    return p;
}
RoundMix Mix(const wchar_t* text,bool* ok=nullptr) {
    RoundMix m;wchar_t why[128];
    const bool parsed=ParseRoundMix(text,&m,why,128);
    if(ok)*ok=parsed;
    return m;
}
int Count(const RoundMix& m,TankRound round,unsigned tanks) {
    int n=0;
    for(unsigned k=0;k<tanks;++k)n+=PickRound(m,k)==round;
    return n;
}
}

int main() {
    // Kinds: every SupportWeapon by its ini name, any case; the Rangers' as the older ini weapons spell them.
    for(int i=0;i<kSupportWeaponCount;++i) {
        SupportWeapon k;const wchar_t* name=kSupportKindNames[i].name;
        Check(ParseSupportKind(name,name+std::wcslen(name),&k) && static_cast<int>(k)==i,"each kind parses to itself");
    }
    {
        SupportWeapon k;const wchar_t up[]=L"  PileBanker ";
        Check(ParseSupportKind(up,up+std::wcslen(up),&k) && k==SupportWeapon::fencerPileBanker,"case and spaces ignored");
        const wchar_t bad[]=L"gatling";
        Check(!ParseSupportKind(bad,bad+std::wcslen(bad),&k),"an unknown kind refused");
    }
    // Presets.
    bool ok=false;wchar_t why[128];
    SupportPreset p=Preset(L"rifle@1E3A8A*2, rocket ，sniper@x:ffffff",4,&ok);
    Check(ok && p.count==4 && p.kind[0]==SupportWeapon::rifle && p.kind[1]==SupportWeapon::rifle && p.kind[2]==SupportWeapon::rocket &&
          p.kind[3]==SupportWeapon::sniper,"kinds in seat order, *n repeats, full-width comma");
    Check(p.look[0].primary==0x1E3A8A && p.look[0].secondary==kStockColour && p.look[1]==p.look[0] && !p.look[2].On() &&
          p.look[3].primary==kStockColour && p.look[3].secondary==0xFFFFFF && p.look[3].On(),"colours: primary, secondary, X = stock");
    p=Preset(L"",12,&ok);Check(ok && p.count==0,"empty: no preset (the entry's own load), not an error");
    p=Preset(L"lance*4,cannon*4,rifle*4",12,&ok);Check(ok && p.count==12,"a platoon of three classes");
    p=Preset(L"lance*4,cannon*4,rifle*5",12,&ok,why);Check(!ok && p.count==0 && std::wcsstr(why,L"12"),"thirteen refused");
    p=Preset(L"rifle*5",4,&ok,why);Check(!ok && p.count==0 && std::wcsstr(why,L"座位"),"more than the entry's seats refused");
    p=Preset(L"rifle",0,&ok,why);Check(!ok && std::wcsstr(why,L"没有"),"an entry without seats takes no preset");
    const wchar_t* broken[]={L"rifle@12345",L"rifle@GGGGGG",L"rifle@123456:",L"rifle*0",L"rifle*x",L"bogus",L"rifle*13",L"@123456"};
    for(const wchar_t* text:broken){p=Preset(text,12,&ok);Check(!ok && p.count==0,"a malformed preset refused whole");}
    p=Preset(L"rifle@1E3A8A*2,rocket,sniper@X:FFFFFF",12,&ok);
    SupportLoadout load=PresetLoadout(p);
    Check(load.count==4 && load.soldier[2]==SupportWeapon::rocket,"the preset as a composed load");
    Check(PresetLookFor(p,load,0)==p.look[0] && PresetLookFor(p,load,3)==p.look[3],"each seat its colours");
    load.soldier[0]=SupportWeapon::flame;
    Check(!PresetLookFor(p,load,0).On() && PresetLookFor(p,load,1)==p.look[1],"a seat changed on the panel: stock, others keep theirs");
    Check(!PresetLookFor(p,load,7).On(),"past the preset: stock");

    // Tank rounds. Empty: every tank HE, the stock gun.
    RoundMix m=Mix(L"",&ok);
    Check(ok && m.count==0 && PickRound(m,0)==TankRound::he && PickRound(m,9)==TankRound::he,"no setting: HE (stock)");
    m=Mix(L"ap",&ok);Check(ok && Count(m,TankRound::ap,10)==10,"AP: every tank AP (the user: 坦克全选ap弹)");
    m=Mix(L"AP,HE,HE",&ok);
    Check(ok && !m.ratio && PickRound(m,0)==TankRound::ap && PickRound(m,1)==TankRound::he && PickRound(m,2)==TankRound::he &&
          PickRound(m,3)==TankRound::ap,"a per-tank list, repeating");
    m=Mix(L"AP*2,HE",&ok);Check(ok && m.count==3 && PickRound(m,1)==TankRound::ap && PickRound(m,2)==TankRound::he,"*n in a list");
    // 50/50 (the user: 一半ap一半he): AP,HE,AP,HE... and after any number of tanks the halves differ by one at most.
    m=Mix(L"AP:1,HE:1",&ok);
    Check(ok && m.ratio && PickRound(m,0)==TankRound::ap && PickRound(m,1)==TankRound::he && PickRound(m,2)==TankRound::ap,
          "1:1 alternates, the first named first");
    for(unsigned n=1;n<=40;++n) {
        const int ap=Count(m,TankRound::ap,n);
        Check(ap==static_cast<int>((n+1)/2),"1:1 over n tanks: ceil(n/2) AP, the rest HE");
    }
    m=Mix(L"AP:50%,HE:50%",&ok);Check(ok && Count(m,TankRound::ap,10)==5,"percent weights: 5 of 10");
    // Any ratio: after every prefix of n tanks each round is within one tank of n·w/W.
    const wchar_t* ratios[]={L"AP:30,HE:70",L"AP:1,HE:3",L"HE:2,AP:5",L"AP:7,HE:8",L"AP:999,HE:1"};
    for(const wchar_t* text:ratios) {
        m=Mix(text,&ok);Check(ok && m.ratio,"a ratio parses");
        long long total=0,apWeight=0;
        for(int i=0;i<m.count;++i){total+=m.weight[i];if(m.round[i]==TankRound::ap)apWeight+=m.weight[i];}
        int ap=0;
        for(unsigned n=1;n<=60;++n) {
            ap+=PickRound(m,n-1)==TankRound::ap;
            const double share=static_cast<double>(n)*static_cast<double>(apWeight)/static_cast<double>(total);
            Check(ap>share-1.0 && ap<share+1.0,"each prefix of tanks within one tank of the ratio");
        }
    }
    m=Mix(L"AP:30,HE:70",&ok);Check(Count(m,TankRound::ap,10)==3,"30:70 over 10 tanks: 3 AP");
    const wchar_t* badMixes[]={L"AP,HE:1",L"AP:0,HE:1",L"AP:1001",L"APFSDS",L"AP*17",L"AP:1,HE",L"AP*2:1"};
    for(const wchar_t* text:badMixes){m=Mix(text,&ok);Check(!ok && m.count==0,"a malformed round mix refused whole");}

    // Generated files (tools/support_loadout.py writes the same names).
    wchar_t name[96];
    SupportLook look{0x1E3A8A,kStockColour};
    Check(SupportLookFile(SupportWeapon::rifle,true,look,name,96) && !std::wcscmp(name,L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO"),"a leader's look file");
    Check(SupportLookFile(SupportWeapon::fencerPileBanker,false,SupportLook{kStockColour,0x0A0B0C},name,96) &&
          !std::wcscmp(name,L"EDF6VC_NPC_PILEBANKER_X_0A0B0C.SGO"),"a member's, the primary stock");
    Check(SupportLookFile(SupportWeapon::wingLance,false,look,name,96,true) &&
          !std::wcscmp(name,L"app:/object/edf6vc_npc_lance_1e3a8a_x.sgo"),"the CreateObject path is lower case under app:/object/");
    Check(!SupportLookFile(SupportWeapon::rifle,true,look,name,10) && !name[0],"too small a buffer: nothing");
    Check(!std::wcscmp(kSupportTankApFile,L"EDF6VC_SUPPORT_TANK_AP.SGO"),"the AP tank's file");

    // Soldier ids: the look in bits 12-15 keeps the kind and the leader; aircraft / vehicles are not soldiers.
    const auto id=SupportSoldierResource(SupportWeapon::fencerShotgun,true)|(3u<<kSupportLookShift);
    Check(IsSupportSoldierResource(id) && SupportSoldierWeapon(id)==SupportWeapon::fencerShotgun && IsSupportLeaderResource(id) &&
          SupportSoldierLook(id)==3,"a coloured leader's id");
    Check(SupportSoldierLook(SupportSoldierResource(SupportWeapon::rifle,false))==0,"a stock soldier has look 0");
    Check(!IsSupportSoldierResource(kSupportAircraftResource+kSupportRangerResource) &&
          !IsSupportSoldierResource(kSupportVehicleResource+kSupportLeaderResource),"aircraft / vehicle ids stay out of the soldiers");
    Check(!IsSupportSoldierResource((static_cast<std::uint32_t>(kSupportWeaponCount)<<8)|kSupportRangerResource),"a kind past the table");
    std::printf("support_loadout_test: %d checks passed\n",checks);
}
