// The out-of-game loadouts' pure logic (src/support_loadout.h): the soldier preset and vehicle pylon syntax, the 64-bit
// unit variants a host sends (every field a peer gets back checked), the generated files' names, the hello's filter,
// and the jets' target preference by their load (src/jet_internal.h LoadoutPrefer).
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
    SupportPreset p;wchar_t local[160];
    const bool parsed=ParseSupportPreset(text,seats,&p,why ? why : local,160);
    if(ok)*ok=parsed;
    return p;
}
VehicleLoadout Load(const wchar_t* text,VehicleBody body,bool* ok=nullptr,wchar_t* why=nullptr) {
    VehicleLoadout l;wchar_t local[200];
    const bool parsed=ParseVehicleLoadout(text,body,&l,why ? why : local,200);
    if(ok)*ok=parsed;
    return l;
}
// The jets' preference (a copy of src/jet_internal.h LoadoutPrefer, which needs the plugin's headers; tests/
// support_loadout_ini_test.py holds the two texts equal).
enum class Prefer { ground, air, any };
constexpr Prefer LoadoutPrefer(Prefer kind,int airRounds,int groundRounds) noexcept {
    return airRounds>0 && groundRounds<=0 ? Prefer::air : groundRounds>0 && airRounds<=0 ? Prefer::ground : kind;
}
}

int main() {
    // Kinds: every SupportWeapon by its ini name, any case.
    for(int i=0;i<kSupportWeaponCount;++i) {
        SupportWeapon k;const wchar_t* name=kSupportKindNames[i].name;
        Check(ParseSupportKind(name,name+std::wcslen(name),&k) && static_cast<int>(k)==i,"each kind parses to itself");
    }
    // Soldier presets.
    bool ok=false;wchar_t why[200];
    SupportPreset p=Preset(L"rifle@1E3A8A*2, rocket ，sniper@x:ffffff",4,&ok);
    Check(ok && p.count==4 && p.kind[0]==SupportWeapon::rifle && p.kind[2]==SupportWeapon::rocket && p.kind[3]==SupportWeapon::sniper,
          "kinds in seat order, *n repeats, full-width comma");
    Check(p.look[0].primary==0x1E3A8A && p.look[0].secondary==kStockColour && !p.look[2].On() && p.look[3].secondary==0xFFFFFF,
          "colours: primary, secondary, X = stock");
    p=Preset(L"",12,&ok);Check(ok && p.count==0,"empty: no preset, not an error");
    p=Preset(L"rifle*5",4,&ok,why);Check(!ok && p.count==0 && std::wcsstr(why,L"座位"),"more than the seats refused");
    const wchar_t* broken[]={L"rifle@12345",L"rifle@GGGGGG",L"rifle@123456:",L"rifle*0",L"bogus",L"rifle*13",L"@123456"};
    for(const wchar_t* text:broken){p=Preset(text,12,&ok);Check(!ok && p.count==0,"a malformed preset refused whole");}
    p=Preset(L"rifle@1E3A8A*2,rocket,sniper@X:FFFFFF",12,&ok);
    SupportLoadout load=PresetLoadout(p);
    load.soldier[0]=SupportWeapon::flame;
    Check(!PresetLookFor(p,load,0).On() && PresetLookFor(p,load,1)==p.look[1],"a seat changed on the panel: stock, others keep theirs");

    // Vehicle pylons. A tank: its main gun, then rounds beside it (the user: 一半ap一半he = one tank carrying both).
    VehicleLoadout l=Load(L"HE,AP:25",VehicleBody::tank,&ok);
    Check(ok && l.On() && !l.apGun && l.count==1 && l.store[0]==8 && kRoundCounts[l.rounds[0]]==25,"tank: HE gun and 25 AP rounds");
    l=Load(L"ap , AP:20 , GLM:4",VehicleBody::tank,&ok);
    Check(ok && l.apGun && l.count==2 && l.store[1]==10 && kRoundCounts[l.rounds[1]]==4,"tank: AP gun, AP rounds and gun-launched missiles");
    l=Load(L"HE",VehicleBody::tank,&ok);Check(ok && l.On() && l.count==0,"tank: the HE gun alone");
    // A jet: its pylons, the guns always (the user: 全带炸弹 / 全带导弹 / 带机炮).
    l=Load(L"MK82:6,MK82:6,MK82:4",VehicleBody::fighter,&ok);
    Check(ok && l.count==3 && l.store[0]==6 && kRoundCounts[l.rounds[2]]==4,"jet: all bombs");
    l=Load(L"aam_m:4,AAM_S:2",VehicleBody::strike,&ok);Check(ok && l.count==2 && l.store[0]==2,"jet: all missiles, any case");
    l=Load(L"GUNS",VehicleBody::multirole,&ok);Check(ok && l.On() && l.count==0,"jet: the guns alone");
    l=Load(L"",VehicleBody::strike,&ok);Check(ok && !l.On(),"empty: the vehicle as it always came");
    struct Bad { const wchar_t* text; VehicleBody body; const wchar_t* says; };
    const Bad bad[]={{L"AP:20",VehicleBody::tank,L"主炮"},{L"HE,MK82:6",VehicleBody::tank,L"坦克可挂"},
                     {L"AP:20",VehicleBody::fighter,L"飞机可挂"},{L"MK82:7",VehicleBody::fighter,L"数量"},
                     {L"MK82",VehicleBody::fighter,L"名称:数量"},{L"GUNS,MK82:6",VehicleBody::strike,L"GUNS"},
                     {L"HE,AP:20,AP:20,AP:20,AP:20,AP:20",VehicleBody::tank,L"4 个"},
                     {L"MK82:2,MK82:2,MK82:2,MK82:2,MK82:2,MK82:2",VehicleBody::strike,L"5 个"},
                     {L"HE",VehicleBody::none,L"没有"}};
    for(const auto& b:bad) {
        l=Load(b.text,b.body,&ok,why);
        Check(!ok && !l.On() && std::wcsstr(why,b.says),"a malformed loadout refused whole, the reason named");
    }
    Check(VehicleBodyOfKey(L"TANK_CREWED")==VehicleBody::tank && VehicleBodyOfKey(L"strike_f")==VehicleBody::strike &&
          VehicleBodyOfKey(L"HELI")==VehicleBody::none && VehicleBodyOfKey(L"SQUAD")==VehicleBody::none,"which entries take pylons");

    // Unit variants: what a host sends, and what a peer accepts back.
    const SupportLook look{0x1E3A8A,kStockColour};
    const auto lv=LookVariant(look);
    SupportLook back;
    Check(lv && !(lv&kVariantApplied) && LookOfVariant(lv|kVariantApplied,&back) && back==look,"a look round-trips, applied or not");
    Check(LookOfVariant(LookVariant(SupportLook{0,0xFFFFFF}),&back) && back.primary==0 && back.secondary==0xFFFFFF,"black is a colour");
    Check(!LookVariant(SupportLook{}) && !LookOfVariant(0,&back) && !LookOfVariant(lv|kVariantVehicle,&back) &&
          !LookOfVariant(lv|(1ull<<55),&back) && !LookOfVariant(1ull<<2,&back),"no look, a vehicle's, stray bits: refused");
    l=Load(L"HE,AP:25,GLM:4",VehicleBody::tank,&ok);
    const auto vv=LoadoutVariant(l);
    VehicleLoadout got;
    Check(vv && (vv&kVariantVehicle) && LoadoutOfVariant(vv|kVariantApplied,VehicleBody::tank,&got) && got.count==2 &&
          got.store[0]==8 && got.rounds[0]==l.rounds[0] && got.store[1]==10 && !got.apGun,"a loadout round-trips");
    Check(!LoadoutOfVariant(vv,VehicleBody::fighter,&got),"a tank's pylons are no jet's (AP on a jet refused)");
    Check(!LoadoutOfVariant(vv|(1ull<<60),VehicleBody::tank,&got) && !LoadoutOfVariant(lv,VehicleBody::tank,&got),
          "stray bits, a look: refused");
    Check(!LoadoutOfVariant(vv|(0x11ull<<(4+8*5)),VehicleBody::tank,&got),"a store past the pylon count refused");
    l=Load(L"GUNS",VehicleBody::fighter,&ok);
    Check(LoadoutVariant(l)==kVariantVehicle && LoadoutOfVariant(kVariantVehicle,VehicleBody::fighter,&got) && got.count==0,
          "guns alone is a loadout of its own");
    Check(!LoadoutOfVariant(kVariantVehicle|8u,VehicleBody::fighter,&got),"an AP gun on a jet refused");
    for(int code=1;code<=kLoadStoreCount;++code)Check(LoadStoreOf(code) && LoadStoreOf(code)->code==code,"store codes 1..n");

    // Generated files (tools/support_loadout.py writes the same names and decodes them).
    wchar_t name[128];
    Check(SupportLookFile(SupportWeapon::rifle,true,look,name,96) && !std::wcscmp(name,L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO"),"a leader's look file");
    Check(SupportLookFile(SupportWeapon::wingLance,false,look,name,96,true) &&
          !std::wcscmp(name,L"app:/object/edf6vc_npc_lance_1e3a8a_x.sgo"),"the CreateObject path is lower case under app:/object/");
    l=Load(L"HE,AP:25",VehicleBody::tank,&ok);
    Check(VehicleVariantFile(l,name,128) && !std::wcscmp(name,L"EDF6VC_LO_TANK_4000000000000C81.SGO"),"a tank's loadout file");
    l=Load(L"MK82:6,MK82:6",VehicleBody::fighter,&ok);
    Check(VehicleVariantFile(l,name,128) && !std::wcscmp(name,L"EDF6VC_LO_FIGHTER_4000000000056562.SGO"),"a jet's loadout file");
    Check(!VehicleVariantFile(VehicleLoadout{},name,128) && !name[0],"no loadout: no file");
    Check(!SupportLookFile(SupportWeapon::rifle,true,look,name,10) && !name[0],"too small a buffer: nothing");

    // The hello's filter: what is added is found, case-blind; a few others are not.
    unsigned char bloom[kVariantBloomBytes]{};
    const wchar_t* have[]={L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO",L"EDF6VC_LO_TANK_4000000000000C81.SGO",L"EDF6VC_LO_FIGHTER_4000000000056562.SGO"};
    for(const wchar_t* f:have)BloomAdd(bloom,VariantHash(f));
    for(const wchar_t* f:have)Check(BloomHas(bloom,VariantHash(f)),"a file added is in the filter");
    Check(VariantHash(L"edf6vc_lo_tank_4000000000000c81.sgo")==VariantHash(have[1]),"the hash is case-blind");
    Check(VariantHash(L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO")==0x69A64712A798737Full,"the hash's value (the installer's is the same)");
    int falses=0;
    for(int i=0;i<200;++i) {
        wchar_t other[64];_snwprintf_s(other,_TRUNCATE,L"EDF6VC_NPC_RIFLE_%06X_X.SGO",i);
        falses+=BloomHas(bloom,VariantHash(other));
    }
    Check(falses<=2,"other names are almost never in a filter of three");
    unsigned char empty[kVariantBloomBytes]{};
    Check(!BloomHas(empty,VariantHash(have[0])),"an older peer's empty filter has nothing");

    // The jets' preference by their load.
    Check(LoadoutPrefer(Prefer::air,0,6)==Prefer::ground,"a fighter with bombs only goes for the ground");
    Check(LoadoutPrefer(Prefer::ground,4,0)==Prefer::air,"a strike jet with air-to-air only goes for flyers");
    Check(LoadoutPrefer(Prefer::any,2,6)==Prefer::any && LoadoutPrefer(Prefer::air,0,0)==Prefer::air,"mixed or spent: its kind's");
    std::printf("support_loadout_test: %d checks passed\n",checks);
}
