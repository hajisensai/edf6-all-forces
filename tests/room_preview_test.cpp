// The room / HQ 3D preview's pure logic (src/room_preview_logic.h) offline: every soldier kind's menu SGO, player class
// and weapons; the colours (stock = the template's, 0xRRGGBB as the generator writes them); the Desc copy rewritten byte
// for byte (the path and nothing past the weapon count left alone); the camera shift applied, re-based when the game
// rewrites a point and restored exactly; and the planner's frames: lock once, build once per model, recolour only the
// changed channel of the soldier on display, wait for a pending build before putting the player back, unlock last.
// Exit code 1 when one fails.  cmake --build build --target room_preview_test && build\room_preview_test.exe
#include "../src/room_preview_logic.h"
#include <cstdio>
#include <cwchar>

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
    for(int i=0;i<kSupportWeaponCount;++i) {
        const int c=SupportSoldierClass(Kind(i));
        Expect(MenuClassOf(Kind(i))==(c==0 ? 0 : c==1 ? 1 : 3),"class: Ranger 0, Wing Diver 1, Fencer 3");
        for(int l=0;l<2;++l) {
            const bool leader=l==1;
            const wchar_t* file=MenuFileOf(Kind(i),leader);
            Expect((file!=nullptr)==(c==0),"only Rangers need a generated file");
            Expect(!std::wcscmp(MenuPathOf(Kind(i),leader,false),c==0 ? kMenuRangerStock : c==1 ? kMenuWingDiver : kMenuFencer),
                   "without the file: stock menu SGOs");
            if(c==0)Expect(!std::wcscmp(MenuPathOf(Kind(i),leader,true),
                                         leader && Kind(i)!=SupportWeapon::rifle && Kind(i)!=SupportWeapon::xmInfantry ?
                                             kMenuRangerLeader : kMenuRanger),"Rangers: the head-only leader file (not the rifle's, not the X-Morph infantry's)");
        }
        const MenuWeapons w=MenuWeaponsOf(Kind(i));
        Expect(w.count==(c==2 ? 2 : 1),"weapons: one, a Fencer's shield and weapon");
        if(c==2)Expect(!std::wcscmp(w.name[0],L"hShield01"),"Fencer: the shield is weapons[0] (left)");
    }
    Expect(!std::wcscmp(MenuWeaponsOf(SupportWeapon::rifle).name[0],L"aWeapon003"),"rifle row name");
    Expect(!std::wcscmp(MenuWeaponsOf(SupportWeapon::fencerPileBanker).name[1],L"hPileBanker01"),"pile banker row name");
    Expect(MenuWeaponsOf(SupportWeapon::count).count==0,"no weapons for an invalid kind");
    // The X-Morph infantry previews as the stock rifle Ranger (both bodies: member and leader).
    for(int l=0;l<2;++l) {
        const bool leader=l==1;
        Expect(MenuClassOf(SupportWeapon::xmInfantry)==kRangerClass,"X-Morph infantry: a Ranger's class");
        Expect(!HeadOnlyLeader(SupportWeapon::xmInfantry,leader),"X-Morph infantry: no head-only leader");
        Expect(MenuFileOf(SupportWeapon::xmInfantry,leader)==MenuFileOf(SupportWeapon::rifle,leader) &&
               !std::wcscmp(MenuPathOf(SupportWeapon::xmInfantry,leader,true),MenuPathOf(SupportWeapon::rifle,leader,true)),
               "X-Morph infantry: the rifle Ranger's menu file and path");
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
    Expect(o.weaponCount==2 && o.weapon[0]==747 && o.weapon[1]==803,"both rows, shield first");
    RewriteDesc(d,o);
    Expect(d[kDescPath]==0xCD && d[0x1F]==0xCD,"the path (wstring) untouched");
    Expect(Int(d,kDescModelType)==3 && Int(d,kDescClass)==3,"Fencer: model type and class 3 (dual)");
    Expect(Float(d,kDescColour0)==0x10/255.f && Float(d,kDescColour0+4)==0x20/255.f && Float(d,kDescColour0+12)==1.f,"colour 0");
    Expect(Float(d,kDescColour1)==StockOf(SupportWeapon::fencerMiddleCannon,false).c[1].v[0],"colour 1 stock");
    Expect(Int(d,kDescOutfit)==kSentinelOutfit,"the sentinel outfit");
    Expect(Int(d,kDescWeaponCount)==2 && Int(d,kDescSelected)==0,"count and selected slot");
    Expect(Int(d,kDescWeapons)==747 && Int(d,kDescWeapons+4)==803,"weapons");
    Expect(d[kDescWeapons+8]==0xCD && d[kDescSize-1]==0xCD,"nothing past the count");
    o.weaponCount=99;RewriteDesc(d,o);
    Expect(Int(d,kDescWeaponCount)==kDescWeaponsMost,"count capped at six");
}

void Camera() {
    float p[4]={1.f,2.f,3.f,4.f};
    float* x[4]={&p[0],&p[1],&p[2],&p[3]};
    CameraShift s;
    s.Apply(x,-0.5f);
    Expect(p[0]==0.5f && p[3]==3.5f,"shifted once");
    s.Apply(x,-0.5f);
    Expect(p[0]==0.5f,"not again next frame");
    p[1]=10.f;   // the game wrote a new base
    s.Apply(x,-0.5f);
    Expect(p[1]==9.5f && p[0]==0.5f,"a rewritten point re-based");
    s.Restore(x,-0.5f);
    Expect(p[0]==1.f && p[1]==10.f && p[3]==4.f,"restored exactly");
    s.Restore(x,-0.5f);
    Expect(p[0]==1.f,"restore twice: once");
    s.Apply(x,-0.5f);p[2]=7.f;s.Restore(x,-0.5f);
    Expect(p[2]==7.f && p[0]==1.f,"a point the game rewrote is left as the game wrote it");
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
}  // namespace

int main() {
    Models();
    Colours();
    Desc();
    Camera();
    Planner();
    std::printf("%d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
