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
    // A Fencer's two hands (#R:L, 2026-11-21; docs/feature-npc-weapon-tier.md 2.5): weapon rows of Fencer hand weapons, both;
    // tests/support_loadout_ini_test.py has the installer's side of each verdict.
    p=Preset(L"pilebanker#715:773*2, cannon@1E3A8A# 803 : 690 ,cannon",4,&ok);
    Check(ok && p.count==4 && p.hands[0]==FencerHands{715,773} && p.hands[1]==p.hands[0] && p.hands[2]==FencerHands{803,690} &&
          !p.hands[3].On() && p.look[2].primary==0x1E3A8A && !p.look[0].On(),"hands parse, after the colours, a look unchanged by them");
    const wchar_t* badHands[]={L"rifle#715:773",L"lance#715:773",L"pilebanker#715",L"pilebanker#715:",L"pilebanker#:773",L"pilebanker#715:26",
                               L"pilebanker#26:773",L"pilebanker#x:773",L"pilebanker#715:773@FFFFFF",L"xminfantry#715:773",
                               L"pilebanker#715:773+aHandGrenade01",L"pilebanker#123456:773",
                               L"pilebanker#７１５:７７３",L"pilebanker#715:7²",L"pilebanker#715:773*２"};
    for(const wchar_t* text:badHands){p=Preset(text,4,&ok,why);Check(!ok && p.count==0 && why[0],"a malformed hand refused whole, the reason named");}
    p=Preset(L"rifle#715:773",4,&ok,why);Check(!ok && std::wcsstr(why,L"重装"),"hands on a Ranger: only a Fencer takes them");
    p=Preset(L"pilebanker#715:773,rifle",12,&ok);
    {SupportLoadout l=PresetLoadout(p);
     Check(PresetHandsFor(p,l,0)==FencerHands{715,773} && !PresetHandsFor(p,l,1).On(),"a seat's hands");
     l.soldier[0]=SupportWeapon::fencerCannon;
     Check(!PresetHandsFor(p,l,0).On(),"a seat changed on the panel: the requester's own pick");}
    Check(HandsFit(FencerHands{715,773},SupportWeapon::fencerShotgun) && !HandsFit(FencerHands{715,773},SupportWeapon::rifle) &&
          !HandsFit(FencerHands{715,-1},SupportWeapon::fencerShotgun) && HandsFit(FencerHands{},SupportWeapon::rifle) &&
          !HandsFit(FencerHands{-1,773},SupportWeapon::fencerShotgun),"HandsFit: both Fencer hand weapons on a Fencer, or none");
    p=Preset(L"rifle@1E3A8A*2,rocket,sniper@X:FFFFFF",12,&ok);
    SupportLoadout load=PresetLoadout(p);
    load.soldier[0]=SupportWeapon::flame;
    Check(!PresetLookFor(p,load,0).On() && PresetLookFor(p,load,1)==p.look[1],"a seat changed on the panel: stock, others keep theirs");
    // The X-Morph infantry not installed (support_call.h SupportKindInstalled; no model pack, the plugin's start state):
    // a preset naming it is refused whole, the reason named; the stock kinds are always there.
    SetSupportKindInstalled(SupportWeapon::xmInfantry,false);
    Check(!SupportKindInstalled(SupportWeapon::xmInfantry) && SupportKindInstalled(SupportWeapon::rifle) &&
          SupportKindInstalled(SupportWeapon::fencerShotgun),"stock kinds installed, the X-Morph infantry only when its bodies are");
    p=Preset(L"rifle*3,xminfantry",12,&ok,why);
    Check(!ok && p.count==0 && std::wcsstr(why,L"未安装"),"an uninstalled kind in a preset: refused whole, the reason named");
    {
        SupportLoadout xm{};xm.count=2;xm.soldier[1]=SupportWeapon::xmInfantry;
        Check(!SupportLoadoutInstalled(xm),"a load holding it is not installed");
        xm.soldier[1]=SupportWeapon::wingLance;
        Check(SupportLoadoutInstalled(xm),"a stock load is");
    }
    SetSupportKindInstalled(SupportWeapon::xmInfantry,true);   // the model pack installed
    // The X-Morph infantry: the kind alone (its model has no change-colour material, no stock template to recolour).
    p=Preset(L"xminfantry*4,rifle",12,&ok);
    Check(ok && p.count==5 && p.kind[0]==SupportWeapon::xmInfantry && p.kind[3]==SupportWeapon::xmInfantry && !p.look[0].On() &&
          SupportSoldierClass(SupportWeapon::xmInfantry)==0 && !SupportKindHasLooks(SupportWeapon::xmInfantry) &&
          SupportKindHasLooks(SupportWeapon::rifle),"xminfantry parses: a Ranger's class, no looks");
    p=Preset(L"xminfantry@1E3A8A",12,&ok,why);
    Check(!ok && p.count==0 && std::wcsstr(why,L"X-Morph 步兵没有涂装与背包"),"a colour on the X-Morph infantry refused, the reason named");
    p=Preset(L"xminfantry+aHandGrenade01",12,&ok,why);
    Check(!ok && p.count==0 && std::wcsstr(why,L"X-Morph 步兵没有涂装与背包"),"a backpack on the X-Morph infantry refused, the reason named");
    p=Preset(L"rifle,xminfantry@X",12,&ok);Check(!ok && p.count==0,"even an explicit stock colour is refused: no looks at all");
    {
        wchar_t file[96];SupportLook look;look.primary=0x1E3A8A;
        Check(SupportLookFile(SupportWeapon::xmInfantry,false,look,file,96)==0 && !file[0] &&
              SupportLookFile(SupportWeapon::xmInfantry,true,SupportLook{},file,96)==0,"no look file is named for the X-Morph infantry");
        int files=0;
        p=Preset(L"xminfantry*4",12,&ok);
        ForEachPresetVariant(p,[&](const wchar_t*) noexcept {++files;});
        Check(files==0,"no variant to preload for the X-Morph infantry");
        Check(SupportLookFile(SupportWeapon::rifle,false,look,file,96)>0,"a kind with looks still names its file");
    }
    Check(PackSupportLoadout(PresetLoadout(p))!=0 && kSupportWeaponCount==15 &&
          SupportSoldierResource(SupportWeapon::xmInfantry,true)==((14u<<8)|2u),"it rides the 4-bit packing as kind 14");

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
    // The weight a jet may carry (2026-10-25, "合理的改，不是完全随心所欲"): 40 % of its clean mass; the tank not weighed.
    // The clean masses (pylib/vcobjects.py JET_MASSES, their real prototypes'): strike 21.6 t, fighter 19.3 t,
    // interceptor 27.2 t, multirole 21.2 t.
    Check(PayloadBudgetKg(VehicleBody::strike)==8640 && PayloadBudgetKg(VehicleBody::fighter)==7720 &&
          PayloadBudgetKg(VehicleBody::interceptor)==10880 && PayloadBudgetKg(VehicleBody::multirole)==8480 &&
          PayloadBudgetKg(VehicleBody::tank)==0 && PayloadBudgetKg(VehicleBody::none)==0,"each body's budget");
    l=Load(L"AAM_L:40,AAM_L:40,AAM_L:40,AAM_L:40,AAM_L:40",VehicleBody::interceptor,&ok,why);
    Check(!ok && !l.On() && std::wcsstr(why,L"90000 kg") && std::wcsstr(why,L"10880 kg"),"200 AIM-54 refused, the weight and the budget named");
    l=Load(L"MK82:40",VehicleBody::strike,&ok,why);Check(!ok && std::wcsstr(why,L"9200 kg"),"40 Mk 82 (9.2 t) over the strike jet's 8.64 t");
    l=Load(L"MK82:25,MK82:12",VehicleBody::strike,&ok);Check(ok && LoadoutMassKg(l)==8510,"37 Mk 82 (8.51 t) within it");
    l=Load(L"AAM_L:12,AAM_M:10,RKT:25",VehicleBody::fighter,&ok);
    Check(ok && LoadoutMassKg(l)==7720,"exactly the budget is within it");
    l=Load(L"AP,AP:40,AP:40,GLM:40,HE:40",VehicleBody::tank,&ok);Check(ok && l.count==4,"the tank is not weighed: its 4 pylons");
    {   // A peer's / a host list's variant over the weight decodes to nothing (never generated, never applied).
        VehicleLoadout over{};over.body=VehicleBody::interceptor;over.count=1;over.store[0]=3;over.rounds[0]=static_cast<std::uint8_t>(RoundCode(40));
        VehicleLoadout got{};
        Check(!WithinPayload(over) && !LoadoutOfVariant(LoadoutVariant(over),VehicleBody::interceptor,&got) && !got.On(),
              "an over-weight variant is no loadout");
        over.rounds[0]=static_cast<std::uint8_t>(RoundCode(12));   // 5.4 t
        Check(WithinPayload(over) && LoadoutOfVariant(LoadoutVariant(over),VehicleBody::interceptor,&got) && got.count==1,
              "within the weight it decodes");
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
          !LookOfVariant(lv|(1ull<<58),&back) && !LookOfVariant(1ull<<2,&back),"no look, a vehicle's, stray bits: refused");
    // A backpack weapon (npc_pack_table.h; docs/npc-backpack-re.md): bits 50-57 its code, an unknown code refused.
    SupportLook packed=look;packed.pack=59;
    Check(LookOfVariant(LookVariant(packed),&back) && back==packed && ((LookVariant(packed)>>50)&0xFF)==59,"a look with a pack round-trips");
    Check(!LookOfVariant(lv|(75ull<<50),&back) && !(LookVariant(look)>>50),"an unknown pack code refused; none: no pack bits");
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

    // Crew colours (stage 5): SupportCrewColour[_<KEY>|_MISSION]=RRGGBB[:RRGGBB], X stock; empty: the default's.
    {
        CrewColour c;wchar_t cwhy[160];
        Check(ParseCrewColour(L"",&c,cwhy,160) && !c.set && ParseCrewColour(L"  ",&c,cwhy,160) && !c.set,"empty: the default applies");
        Check(ParseCrewColour(L"1e3a8a",&c,cwhy,160) && c.set && c.look.primary==0x1E3A8A && c.look.secondary==kStockColour,"a primary");
        Check(ParseCrewColour(L" X : FFFFFF ",&c,cwhy,160) && c.set && c.look.primary==kStockColour && c.look.secondary==0xFFFFFF,
              "a secondary alone (X the primary), spaces trimmed");
        Check(ParseCrewColour(L"x",&c,cwhy,160) && c.set && !c.look.On(),"X alone: stock, whatever the default");
        const wchar_t* cbad[]={L"12345",L"1E3A8A:",L"GGGGGG",L"1E3A8A,FFFFFF",L"@1E3A8A",L"1E3A8A:FFFFFF:FFFFFF",L"rifle"};
        for(const wchar_t* t:cbad)
            Check(!ParseCrewColour(t,&c,cwhy,160) && !c.set && std::wcsstr(cwhy,L"乘员颜色"),"a cbad crew colour refused, the reason named");
        CrewColour own{},def{};def.set=true;def.look.primary=0x101010;
        Check(CrewLookOf(own,def).primary==0x101010,"no own colour: the default's");
        own.set=true;
        Check(!CrewLookOf(own,def).On(),"its own X: stock, over the default");
        own.look.secondary=0xFFFFFF;
        Check(CrewLookOf(own,def).secondary==0xFFFFFF && CrewLookOf(own,def).primary==kStockColour,"its own colours");
        Check(!CrewLookOf(CrewColour{},CrewColour{}).On(),"nothing set: stock");
        Check(!HasCrew(L"SQUAD") && !HasCrew(L"platoon") && HasCrew(L"TANK_CREWED") && HasCrew(L"SQUAD_HELI") && HasCrew(kCrewMissionKey) &&
              !HasCrew(nullptr),"every entry but the infantry squad and platoon brings a crew");
        Check(IsGroundCrewKey(L"TANK_CREWED") && IsGroundCrewKey(L"transport_delivery") && IsGroundCrewKey(L"TRUCK_CREWED") &&
              !IsGroundCrewKey(L"SQUAD_HELI") && !IsGroundCrewKey(L"STRIKE_F") && !IsGroundCrewKey(L"TANK") &&
              !IsGroundCrewKey(nullptr),"ground crews are the tank / transport / truck entries (VEHICLE_CREW_KEYS)");
        Check(!IsGroundCrewKey(L"TANK_AIRDROP") && !IsGroundCrewKey(L"TRANSPORT_AIRDROP") && !IsGroundCrewKey(L"truck_airdrop") &&
              !IsGroundCrewKey(L"TANK_CREWED_X") && !IsGroundCrewKey(L"TANK_") && !IsGroundCrewKey(L""),
              "the container deliveries' crew is the transport helicopter's pilot: an aircraft crew, not ground (no prefix match)");
        CrewColour a{},b2{};a.look.primary=1;
        Check(a==b2,"two crews not set are equal whatever their stale look");
        // A crew wears the same file a squad soldier of its kind would: a member's, the mission's leader's _L.
        wchar_t file[96];SupportLook clook{0x1E3A8A,kStockColour};
        Check(SupportLookFile(SupportWeapon::rifle,false,clook,file,96) && !std::wcscmp(file,L"EDF6VC_NPC_RIFLE_1E3A8A_X.SGO") &&
              SupportLookFile(SupportWeapon::rifle,true,clook,file,96) && !std::wcscmp(file,L"EDF6VC_NPC_RIFLE_L_1E3A8A_X.SGO") &&
              SupportLookFile(SupportWeapon::sniper,false,clook,file,96) && !std::wcscmp(file,L"EDF6VC_NPC_SNIPER_1E3A8A_X.SGO"),
              "a crew's files: its kind's template, recoloured");
        // On the wire a crew seat's own 64-bit variant: a clook, exactly a squad soldier's.
        SupportLook cback;
        Check(LookOfVariant(LookVariant(clook)|kVariantApplied,&cback) && cback==clook,"a crew seat's clook travels in its own variant");
    }

    // The mech (src/mech_loadout.h, docs/feature-2026-11-21-mech-custom.md): request, frame, four slots, core.
    {
        bool mok=false;wchar_t mwhy[200];
        VehicleLoadout m=Load(L"eweapon378, NIX_RED ,EMLASER,EMLASER,PBEAM,MIRAGE,-",VehicleBody::mech,&mok,mwhy);
        Check(mok && m.body==VehicleBody::mech && m.count==1 && m.mech.request>=0 && !std::wcscmp(kMechRequests[m.mech.request].token,L"EWEAPON378") &&
              !std::wcscmp(kMechFrames[m.mech.frame].token,L"NIX_RED") && m.mech.core==-1 && m.mech.weapon[3]>=0,
              "a mech parses (any case, spaces), - the request's own core");
        wchar_t mback[160];
        Check(FormatMechLoadout(m.mech,mback,160) && !std::wcscmp(mback,L"EWEAPON378,NIX_RED,EMLASER,EMLASER,PBEAM,MIRAGE,-"),"and formats back");
        VehicleLoadout again;
        Check(LoadoutOfVariant(LoadoutVariant(m)|kVariantApplied,VehicleBody::mech,&again) && again.mech==m.mech &&
              (LoadoutVariant(m)&(kVariantVehicle|kVariantMech))==(kVariantVehicle|kVariantMech),"a mech's variant round trip");
        Check(LoadoutVariant(m)==0x600000185CB2C1D8ull,"its variant: tests/mech_parts_test.py holds the same number (pylib/mech_parts.py)");
        Check(!LoadoutOfVariant(LoadoutVariant(m),VehicleBody::rfighter,&again),"a mech's variant is no jet's");
        Check(!LoadoutOfVariant(LoadoutVariant(Load(L"AAM_S:2",VehicleBody::rfighter)),VehicleBody::mech,&again),"a jet's is no mech's");
        wchar_t mfile[96];
        Check(VehicleVariantFile(m,mfile,96) && !std::wcsncmp(mfile,L"EDF6VC_LO_MECH_6",16),"a mech's file");
        struct Bad { const wchar_t* text; const wchar_t* says; };
        const Bad mbad[]={
            {L"EWEAPON356,-,SABER,-,-,-,-",L"等级"},              // a level 4.25 saber on the level 0.1 Nix A
            {L"EWEAPON378,-,PBEAM,-,-,-,-",L"右臂装不了"},        // a shoulder weapon on an arm
            {L"EWEAPON378,-,-,-,MIRAGE,-,-",L"右肩装不了"},       // the Mirage pod has only a left file
            {L"EWEAPON378,-,-,-,-,-,-",L"至少"},                  // no weapon
            {L"EWEAPON378,-,EMLASER,-,-,-",L"7 项"},              // six fields
            {L"EWEAPON378,-,EMLASER,-,-,-,-,-",L"7 项"},          // eight
            {L"EWEAPON999,-,EMLASER,-,-,-,-",L"机甲请求"},
            {L"-,-,EMLASER,-,-,-,-",L"底座请求"},
            {L"EWEAPON378,PINK,EMLASER,-,-,-,-",L"机体"},
            {L"EWEAPON356,-,MACHINEGUN,-,-,-,MPACK_A_WEAPON075",L"核心"},
        };
        for(const Bad& b:mbad) {
            const VehicleLoadout x=Load(b.text,VehicleBody::mech,&mok,mwhy);
            Check(!mok && !x.On() && std::wcsstr(mwhy,b.says),"a malformed mech refused whole, the reason named");
        }
        Check(Load(L"",VehicleBody::mech,&mok).body==VehicleBody::none && mok,"empty: stock");
        // Every stock request as it comes is a valid loadout; MechWithRequest keeps only what the new level allows.
        for(int i=0;i<kMechRequestCount;++i)Check(MechValid(MechStock(i)),"each stock request is a valid mech");
        MechLoadout hi=Load(L"MPACK_B_WEAPON109,-,HYPERLASER,MACHINEGUN,-,-,MPACK_A_WEAPON076",VehicleBody::mech).mech;
        const MechLoadout lo=MechWithRequest(hi,1);   // EWEAPON356, level 0.1
        Check(MechValid(lo) && lo.weapon[0]==-1 && lo.weapon[1]>=0 && lo.core==-1,"a lower request clears what is above it");
        // The summon's identification: the vehicle file and the level, unique among the stock requests.
        Check(MechRequestOf(L"app:/Object/v612_nix.sgo",kMechRequests[m.mech.request].level)==m.mech.request &&
              MechRequestOf(L"app:/Object/v612_nix_black.sgo",kMechRequests[m.mech.request].level)<0,"a request by its vehicle and level");
        for(int i=0;i<kMechRequestCount;++i)
            for(int j=0;j<i;++j)Check(kMechRequests[i].level!=kMechRequests[j].level || _wcsicmp(kMechRequests[i].vehicle,kMechRequests[j].vehicle),
                                      "no two stock requests share a vehicle and a level");
    }

    // The jets' preference by their load.
    Check(LoadoutPrefer(Prefer::air,0,6)==Prefer::ground,"a fighter with bombs only goes for the ground");
    Check(LoadoutPrefer(Prefer::ground,4,0)==Prefer::air,"a strike jet with air-to-air only goes for flyers");
    Check(LoadoutPrefer(Prefer::any,2,6)==Prefer::any && LoadoutPrefer(Prefer::air,0,0)==Prefer::air,"mixed or spent: its kind's");
    std::printf("support_loadout_test: %d checks passed\n",checks);
}
