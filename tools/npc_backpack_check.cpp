// The NPCs' backpack weapons offline (docs/npc-backpack-re.md): the table (src/npc_pack_table.h), the preset grammar,
// variant and file name that carry one (src/support_loadout.h), and when a soldier uses one (src/npc_backpack.h): the low
// arc a throw leaves on (checked by flying it), its group, reach, flight time and own blast, the friends where it lands and
// on its way, a direct weapon's reach, the look, the pace.
//   cmake --build build --target npc_backpack_check && build\npc_backpack_check.exe      (exit code 1 on a failure)
#include "../src/npc_backpack.h"
#include "../src/support_loadout.h"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <initializer_list>

namespace {
using namespace crew;
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
bool Near(float a,float b,float tol) { return std::fabs(a-b)<=tol; }

// The table: one code each, 1.. in order (a code once given is never reused: the wire carries it), one name each, the
// Ranger's 20 / 23 thrown, the Wing Diver's 110 thrown and 111 / 116 direct, an area only for the 23s.
void Table() {
    Check(kPackWeaponCount==74,"74 backpack weapons",kPackWeaponCount);
    for(int i=0;i<kPackWeaponCount;++i) {
        const PackWeapon& p=kPackWeapons[i];
        Check(p.code==i+1,"codes 1.. in order",p.code,i+1);
        Check(PackOf(p.code)==&p,"PackOf finds it",p.code);
        Check(PackNamed(p.sgo,p.sgo+std::wcslen(p.sgo))==&p,"PackNamed finds it",p.code);
        for(int j=i+1;j<kPackWeaponCount;++j)Check(_wcsicmp(p.sgo,kPackWeapons[j].sgo)!=0,"one name each",i,j);
        const bool ranger=p.category==20 || p.category==23,wing=p.category==110 || p.category==111 || p.category==116;
        Check(ranger ? p.soldierClass==0 : wing && p.soldierClass==1,"its class from its category",p.code,p.category);
        Check((p.use==PackUse::thrown)==(p.category==20 || p.category==23 || p.category==110),"thrown or direct by category",p.code);
        Check((p.area>0.0f)==(p.category==23),"an area only for a burning / gas round",p.code);
        Check(p.lift>=0.0f && p.lift<0.2f,"the lift a FireVector's",p.code,p.lift);
        Check(p.label && *p.label,"a label",p.code);
    }
    Check(!PackOf(0) && !PackOf(75) && !PackOf(255),"no code 0 / past the table");
    const wchar_t* mixed=L"AHANDGRENADE01";
    Check(PackNamed(mixed,mixed+14)==PackOf(1),"a name in any case");
    Check(!PackNamed(mixed,mixed+13),"not a prefix");
    const wchar_t* reverser=L"aWeapon261";
    Check(!PackNamed(reverser,reverser+10),"the Aerial Reversers (they heal) are left out");
    const wchar_t* bomb=L"aWeapon270";
    Check(!PackNamed(bomb,bomb+10),"the C bombs (a second press) are left out");
    Check(PackFits(0,2) && PackFits(1,0) && !PackFits(1,1) && !PackFits(59,0) && PackFits(59,1) && !PackFits(1,2),"which class carries which");
    // The menu's cycle: none, then its class's in table order, back to none; a Fencer's only none.
    for(int cls=0;cls<2;++cls) {
        int code=0,seen=0;
        do{code=CyclePack(code,cls,1);++seen;Check(code==0 || PackOf(code)->soldierClass==cls,"the cycle stays in its class",cls,code);}
        while(code && seen<200);
        Check(seen==(cls==0 ? 45 : 29)+1,"the cycle goes round every one once",cls,seen);
        Check(CyclePack(0,cls,-1)!=0 && CyclePack(CyclePack(0,cls,-1),cls,1)==0,"backwards from none: its last",cls);
    }
    Check(CyclePack(0,2,1)==0 && CyclePack(5,2,1)==0,"a Fencer has none");
}

// The preset grammar, the variant's bits and the file's name.
void Grammar() {
    SupportPreset p;wchar_t why[96];
    Check(ParseSupportPreset(L"rifle+aHandGrenade01*2,lance@1E3A8A+pweapon216,cannon",4,&p,why,96) && p.count==4,"a preset with packs");
    Check(p.look[0].pack==1 && p.look[1].pack==1 && !p.look[0].Coloured() && p.look[0].On(),"a pack alone: On, not coloured");
    Check(p.look[2].pack==59 && p.look[2].primary==0x1E3A8A && p.look[3].pack==0 && !p.look[3].On(),"colours and a pack");
    for(const wchar_t* bad:{L"cannon+aHandGrenade01",L"rifle+pWeapon216",L"lance+aHandGrenade01",L"rifle+bogus",L"rifle+",
                            L"rifle+aWeapon261",L"rifle@GGGGGG+aHandGrenade01"}) {
        Check(!ParseSupportPreset(bad,4,&p,why,96) && p.count==0 && why[0],"a wrong pack refused with a reason");
    }
    SupportLook l;l.pack=74;l.primary=0x123456;
    const std::uint64_t v=LookVariant(l);
    Check(((v>>50)&0xFF)==74 && !(v>>58),"the pack in bits 50-57");
    SupportLook back;
    Check(LookOfVariant(v|kVariantApplied,&back) && back==l,"its variant decodes back");
    SupportLook only;only.pack=1;
    Check(LookOfVariant(LookVariant(only),&back) && back==only && !back.Coloured(),"a pack without colours is a variant");
    Check(!LookOfVariant(75ull<<50,&back) && !LookOfVariant(1ull<<58,&back) && !LookOfVariant(1ull<<61,&back),"no unknown code, no bit past 57");
    SupportLook stock;Check(LookVariant(stock)==0,"stock: no variant");
    wchar_t file[96],path[128];
    Check(SupportLookFile(SupportWeapon::rifle,true,only,file,96)>0 && !std::wcscmp(file,L"EDF6VC_NPC_RIFLE_L_X_X_AHANDGRENADE01.SGO"),
          "a pack's file name");
    Check(SupportLookFile(SupportWeapon::rifle,false,l,file,96)==0 && !file[0] && SupportLookFile(SupportWeapon::wingLance,false,l,file,96)>0,"a Wing Diver pack on a Ranger: no file");
    SupportLook saber;saber.pack=59;saber.primary=0x1E3A8A;
    Check(SupportLookFile(SupportWeapon::wingThunderBow,true,saber,path,128,true)>0 &&
          !std::wcscmp(path,L"app:/object/edf6vc_npc_thunderbow_l_1e3a8a_x_pweapon216.sgo"),"its CreateObject path");
    SupportLook longest;longest.primary=0xFFFFFF;longest.secondary=0xFFFFFF;longest.pack=46;
    Check(SupportLookFile(SupportWeapon::wingThunderBow,true,longest,file,96)>0 && std::wcslen(file)<64,"the longest name fits",static_cast<double>(std::wcslen(file)));
    Check(SupportLookFile(SupportWeapon::fencerCannon,false,only,file,96)==0,"a Fencer: never a pack file");
}

// The arc: the elevation LobPitch gives, flown in small steps, lands where it was meant to.
void Arcs() {
    using npcpack::LobPitch;
    for(float speed:{20.0f,36.0f,60.0f,144.0f})
        for(float gravity:{0.0f,1.8375f,14.7f,29.4f})
            for(float horiz:{3.0f,12.0f,30.0f,70.0f})
                for(float rise:{-8.0f,0.0f,6.0f}) {
                    float pitch,flight;
                    if(!LobPitch(horiz,rise,speed,gravity,&pitch,&flight)) {
                        const float most=gravity>0.0f ? speed*speed/gravity : 1e9f;
                        Check(horiz>most*0.3f,"out of reach only far off",horiz,most);
                        continue;
                    }
                    const float vx=speed*std::cos(pitch),vy=speed*std::sin(pitch);
                    const float dt=flight/2000.0f;
                    float x=0.0f,y=0.0f,uy=vy;
                    for(int i=0;i<2000;++i){x+=vx*dt;y+=uy*dt-0.5f*gravity*dt*dt;uy-=gravity*dt;}
                    Check(Near(x,horiz,0.02f) && Near(y,rise,0.05f+0.002f*horiz),"the arc lands on it",x-horiz,y-rise);
                    Check(pitch>=std::atan2(rise,horiz)-1e-4f && pitch<=(1.5707963f+std::atan2(rise,horiz))*0.5f+1e-3f,"the low arc: above the line, below the arcs' middle",pitch);
                }
    float pitch,flight;
    Check(LobPitch(10.0f,10.0f,30.0f,0.0f,&pitch,&flight) && Near(pitch,0.785398f,1e-4f),"no gravity: the straight line");
    Check(!LobPitch(200.0f,0.0f,36.0f,14.7f,&pitch,&flight),"farther than v^2/g: none");
    Check(!LobPitch(0.0f,0.0f,36.0f,14.7f,&pitch,&flight) && !LobPitch(10.0f,0.0f,0.0f,14.7f,&pitch,&flight),"no distance / no speed: none");
}

// When: a group or a large one, on the ground, in reach, not too near, not too long a flight; a direct weapon in reach.
void When() {
    using namespace npcpack;
    const Weapon mg11{PackUse::thrown,360.0f,5.0f,0.0f,36.0f,14.7f,0.125f,true};   // MG11: 0.6 m/frame, 600 frames
    float pitch=0.0f;
    Check(Fits(mg11,Target{25.0f,25.0f,0.0f,3,false},&pitch) && pitch>0.0f,"a group at 25 m: thrown, up",pitch);
    Check(!Fits(mg11,Target{25.0f,25.0f,0.0f,1,false},&pitch),"one small one: not worth a grenade");
    Check(Fits(mg11,Target{25.0f,25.0f,0.0f,1,true},&pitch),"one large one: worth it");
    Check(!Fits(mg11,Target{40.0f,20.0f,34.0f,4,false},&pitch),"a flyer overhead: never thrown at");
    Check(!Fits(mg11,Target{8.0f,8.0f,0.0f,4,false},&pitch),"inside its own blast's margin: not thrown",Nearest(mg11));
    Check(Nearest(mg11)==9.5f && ThrowReach(mg11)==90.0f,"MG11: from 9.5 m, to 2.5 s of flight (90 m)",Nearest(mg11),ThrowReach(mg11));
    Check(!Fits(mg11,Target{95.0f,95.0f,0.0f,4,false},&pitch),"past its flight's reach: not thrown");
    Check(!Fits(mg11,Target{80.0f,80.0f,0.0f,4,false},&pitch),"in reach but 2.6 s of flight (33 deg at 80 m): not thrown");
    Weapon empty=mg11;empty.ready=false;
    Check(!Fits(empty,Target{25.0f,25.0f,0.0f,3,false},&pitch),"no round: nothing");
    const Weapon blink{PackUse::thrown,60.0f,7.0f,0.0f,60.0f,1.8375f,0.0f,true};   // Blink Ball: 1 m/frame, 60 frames
    Check(Fits(blink,Target{40.0f,40.0f,-3.0f,2,false},&pitch) && ThrowReach(blink)==60.0f,"a Blink Ball at 40 m",pitch);
    Check(!Fits(blink,Target{65.0f,65.0f,0.0f,5,false},&pitch),"past a Blink Ball's 60 m");
    const Weapon thermite{PackUse::thrown,360.0f,0.0f,8.0f,36.0f,14.7f,0.125f,true};
    Check(Nearest(thermite)==14.0f && !Fits(thermite,Target{12.0f,12.0f,0.0f,3,false},&pitch),"a thermite round keeps its fire off itself");
    Check(GroupRadius(thermite)==8.0f && GroupRadius(Weapon{PackUse::thrown,0,15.0f,0,1,1,0,true})==15.0f,"the group's radius");
    const Weapon saber{PackUse::direct,23.1f,0.0f,0.0f,198.0f,0.0f,0.0f,true};   // Handy Saber: 3.3 m/frame, 7 frames
    Check(Fits(saber,Target{20.0f,20.0f,0.0f,1,false},&pitch),"a saber at 20 m (its reach 23 m)");
    Check(!Fits(saber,Target{21.5f,21.5f,0.0f,1,false},&pitch),"past 0.9 of its reach");
    Check(Fits(saber,Target{15.0f,5.0f,14.0f,1,false},&pitch),"a saber at a flyer within reach");
}

void Looks() {
    using namespace npcpack;
    const float from[3]={0.0f,1.5f,0.0f},to[3]={0.0f,1.5f,30.0f};
    float dir[3];
    ThrowLook(from,to,0.5f,0.125f,dir);
    Check(Near(std::atan2(dir[1],std::sqrt(dir[0]*dir[0]+dir[2]*dir[2])),0.5f-std::atan(0.125f),1e-5f) && dir[2]>0.0f && Near(dir[0],0.0f,1e-6f),
          "the look: the arc's elevation less the lift");
    // npc_logic.h's convention: a negative view pitch looks up.
    const float e=0.5f-std::atan(0.125f);
    Check(AimOn(PackUse::thrown,-e,0.0f,dir,30.0f) && !AimOn(PackUse::thrown,-e+0.08f,0.0f,dir,30.0f),"on the throw within its cone");
    const float flat[3]={0.0f,0.0f,1.0f};
    Check(AimOn(PackUse::direct,0.0f,0.05f,flat,10.0f) && !AimOn(PackUse::direct,0.0f,0.2f,flat,10.0f),"a direct look within 1.5 m at 10 m");
    ThrowLook(from,from,0.5f,0.0f,dir);
    Check(dir[1]==1.0f,"no way to it: straight up (never aimed at: Fits refused it)");
}

void Friends() {
    using namespace npcpack;
    const float at[3]={0.0f,0.0f,30.0f};
    npc::Friend near{{0.0f,0.0f,24.0f},0.8f},far{{0.0f,0.0f,10.0f},0.8f};
    Check(!LandingClear(at,5.0f,&near,1),"a friend 6 m from a 5 m blast's landing: held");
    Check(LandingClear(at,5.0f,&far,1) && LandingClear(at,0.0f,&near,1),"one 20 m off, or no radius: clear");
    Check(Rested(PackUse::thrown,false,0) && !Rested(PackUse::thrown,true,3999) && Rested(PackUse::thrown,true,4000) &&
          !Rested(PackUse::direct,true,699) && Rested(PackUse::direct,true,700),"the pace");
}

void WholeFlight() {
    using namespace npcpack;
    const Weapon grenade{PackUse::thrown,360.0f,5.0f,0.0f,36.0f,14.7f,0.125f,true};
    const float from[3]={0.0f,1.5f,0.0f},to[3]={0.0f,1.5f,40.0f};
    float pitch=0.0f,flight=0.0f;
    Check(LobPitch(40.0f,0.0f,grenade.speed,grenade.gravity,&pitch,&flight),"a forty metre throw has an arc");
    int rays=0;float last[3]{},longest=0.0f;
    const auto open=[&](const float* a,const float* b) noexcept {
        ++rays;const float length=npc::Dist(a,b);if(length>longest)longest=length;
        for(int k=0;k<3;++k)last[k]=b[k];return false;
    };
    Check(ArcClear(grenade,from,to,pitch,0.1f,nullptr,0,open) && rays>10 && longest<=kArcStep+0.001f && npc::Dist(last,to)<0.01f,
          "the map is traced all the way to the landing, in bounded segments",rays,longest);
    const auto wall=[](const float* a,const float* b) noexcept {return a[2]<12.0f && b[2]>=12.0f;};
    Check(!ArcClear(grenade,from,to,pitch,0.1f,nullptr,0,wall),"a wall twelve metres away stops the throw (past the old six metre test)");
    // A low wall the real arc clears is safe; a straight eye-to-target ray would incorrectly forbid this throw.
    const auto lowWall=[](const float* a,const float* b) noexcept {
        if(a[2]>=20.0f || b[2]<20.0f)return false;
        const float t=(20.0f-a[2])/(b[2]-a[2]);return a[1]+(b[1]-a[1])*t<2.0f;
    };
    Check(ArcClear(grenade,from,to,pitch,0.1f,nullptr,0,lowWall),"the real arc clears a low wall on the straight sight line");
    const float t=flight*0.5f;
    npc::Friend middle{{0.0f,from[1]+grenade.speed*std::sin(pitch)*t-0.5f*grenade.gravity*t*t,20.0f},0.1f};
    float look[3];ThrowLook(from,to,pitch,grenade.lift,look);
    const float oldEnd[3]={from[0]+look[0]*6.0f,from[1]+look[1]*6.0f,from[2]+look[2]*6.0f};
    Check(LandingClear(to,grenade.blast,&middle,1) && npc::ShotClear(from,oldEnd,0.1f,0.0f,&middle,1),
          "a friend midway along the real arc escapes the old near / landing tests");
    Check(!ArcClear(grenade,from,to,pitch,0.1f,&middle,1,open),"the whole actual arc protects a friend midway along it (FireVector lift restored)");
    npc::Friend beside=middle;beside.pos[0]=3.0f;
    Check(ArcClear(grenade,from,to,pitch,0.1f,&beside,1,open),"a friend safely beside the arc does not prevent a throw");
    npc::Friend landing{{0.0f,1.5f,35.0f},0.8f};
    Check(!ArcClear(grenade,from,to,pitch,0.1f,&landing,1,open),"the landing blast is checked as well as the arc");
}
}  // namespace

int main() {
    Table();
    Grammar();
    Arcs();
    When();
    Looks();
    Friends();
    WholeFlight();
    std::printf("npc_backpack_check: %d cases, %d failures\n",cases,failures);
    return failures ? 1 : 0;
}
