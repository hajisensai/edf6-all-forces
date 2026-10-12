// The hit prediction (src/wastedfire.cpp, src/hit_predict.h) against the game's own hit-acceptance overrides, on a
// private EDF.dll mapping (no DllMain, nothing started): stand-in objects of each class the plugin asks (a teleportation
// ship, a drop boat, an anchor, a generator, a time ship, a moving fortress part, a monster), a predicted hit built as the
// plugin builds it, and the real override deciding. Then the plan (Assess) with its ray and lock points stood in.
#include "../src/wastedfire.cpp"
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace crew {
unsigned char* image=nullptr;
Config config{};
const Config& Cfg() noexcept { return config; }
void Log(const char*,...) noexcept {}
ULONGLONG nowMs=1000;
ULONGLONG GameMs() noexcept { return nowMs; }
bool IsSoldierClass(const void*) noexcept { return false; }
// The lock points and the ray the plan uses, as each case sets them.
struct Lock { const void* object; float at[3]; };
std::vector<Lock> locks;
bool VisitLockPoints(EnemyVisitor visit,void* ctx) noexcept { for(const auto& l:locks)visit(ctx,l.object,l.at);return true; }
float rayHit[3]{};std::uint32_t rayBody=0;bool rayOn=false;
float ObjectRay(const float* a,const float*,float* hit,std::uint32_t* body) noexcept {
    if(!rayOn)return -1.0f;
    std::memcpy(hit,rayHit,12);*body=rayBody;
    const float d[3]={rayHit[0]-a[0],rayHit[1]-a[1],rayHit[2]-a[2]};
    return std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
}
}  // namespace crew

namespace {
int checks=0,failures=0;
void Check(bool ok,const char* why) {
    ++checks;
    if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}
}
using namespace crew;
template<class T> void Put(void* o,std::size_t at,T v) { std::memcpy(static_cast<unsigned char*>(o)+at,&v,sizeof(T)); }
void SetVtable(unsigned char* o,std::uintptr_t vt) { const void* p=image+vt;std::memcpy(o,&p,8); }

// A box shape as the anchor's and the generator's overrides read it through 0x6BB5A0 / 0x6BB4A0: {shape, node} inline in
// the object; the shape type 1 (+8), its local centre at shape+[+0xC], its half extents at shape+[+0x10]; the node's world
// matrix rows at +0xB0..+0xE0 (here no turn, the box at `centre`).
struct alignas(16) Shape { unsigned char raw[0x40]; };
struct alignas(16) Node { unsigned char raw[0x100]; };
void MakeBox(unsigned char* o,std::size_t at,Shape& s,Node& n,const float* centre,const float* half) {
    std::memset(&s,0,sizeof(s));std::memset(&n,0,sizeof(n));
    Put<std::int32_t>(&s,8,1);Put<std::int32_t>(&s,0xC,0x20);Put<std::int32_t>(&s,0x10,0x30);
    const float local[4]={0,0,0,1},h[4]={half[0],half[1],half[2],0};
    std::memcpy(s.raw+0x20,local,16);std::memcpy(s.raw+0x30,h,16);
    const float m[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, centre[0],centre[1],centre[2],1};
    std::memcpy(n.raw+0xB0,m,64);
    Shape* sp=&s;Node* np=&n;
    std::memcpy(o+at,&sp,8);std::memcpy(o+at+8,&np,8);
}

// The teleportation ship's ragdoll as 0x6EA430 walks it: parts at ragdoll+0x78 (0xC0 apart, count +0x88), each part's body
// at part+0x50 with its id at +0xF0.
struct alignas(16) Body { unsigned char raw[0x100]; };
struct alignas(16) Part { unsigned char raw[0xC0]; };
void MakeRagdoll(unsigned char* o,Part* parts,Body* bodies,int n,std::uint32_t firstId) {
    for(int i=0;i<n;++i) {
        std::memset(&parts[i],0,sizeof(Part));std::memset(&bodies[i],0,sizeof(Body));
        Put<std::uint32_t>(&bodies[i],0xF0,firstId+static_cast<std::uint32_t>(i));
        Body* b=&bodies[i];std::memcpy(parts[i].raw+0x50,&b,8);
    }
    Put<Part*>(o,0xB60+0x78,parts);Put<std::uint64_t>(o,0xB60+0x88,static_cast<std::uint64_t>(n));
}

alignas(16) unsigned char ship[0x2000],boat[0x2000],anchor[0x2000],gen[0x2000],timeShip[0x2000],part[0x2000],monster[0x2000],soldier[0x400],other[0x2000];

void Classes() {
    const float at[3]={0,0,0};
    // A teleportation ship: the core part (body 101 of 100..102) taken while its damage window is open, the hull not; shut,
    // nothing; the invincible flag and the no-filter flag as the damage function has them.
    Part parts[3];Body bodies[3];
    SetVtable(ship,0x17C9DC0);MakeRagdoll(ship,parts,bodies,3,100);Put<std::int32_t>(ship,0xD8C,1);ship[0xD85]=1;
    Check(KindOf(ship)==Kind::carrier,"UfoCarrier508: the carrier override");
    Check(Accepts(ship,Kind::carrier,at,101),"ship, window open: a hit on the core part taken (the game's 0x4F7190)");
    Check(!Accepts(ship,Kind::carrier,at,100) && !Accepts(ship,Kind::carrier,at,102),"ship: a hit on the hull refused");
    Check(!Accepts(ship,Kind::carrier,at,0xFFFFFF),"ship: a hit on no body refused");
    ship[0xD85]=0;
    Check(!Accepts(ship,Kind::carrier,at,101),"ship, window shut: even the core refused");
    Check(!WindowOf(ship,Kind::carrier).open,"ship, window shut: shut");
    Put<std::uint32_t>(ship,kObjFlags,kNoHitFilter);
    Check(Accepts(ship,Kind::carrier,at,100),"ship with +0x380 0x800: the override skipped, every hit taken");
    Put<std::uint32_t>(ship,kObjFlags,kInvincible);
    Check(!WindowOf(ship,Kind::carrier).open,"invincible: shut");
    Put<std::uint32_t>(ship,kObjFlags,0);ship[0xD85]=1;ship[0xD84]=1;Put<std::int32_t>(ship,0xD90,90);
    const WindowNow w=WindowOf(ship,Kind::carrier);
    Check(w.open && std::fabs(w.leftMs-1500.0f)<1.0f,"ship, hatch open with 90 frames on its close timer: 1.5 s left");
    // The drop boat shares the override.
    SetVtable(boat,0x17C94D8);
    Check(KindOf(boat)==Kind::carrier,"DropBoat512: the carrier override");
    // An anchor (the teleportation tower): a hit inside its box taken, outside not; shut while a living ship shields it.
    Shape s1;Node n1;const float c[3]={10,20,30},half[3]={2,3,4};
    SetVtable(anchor,0x17B9DB8);MakeBox(anchor,0x1128,s1,n1,c,half);
    Check(KindOf(anchor)==Kind::anchor,"EDF6_TimeShip_Anchor: the anchor override");
    const float in[3]={11,22,33},out[3]={13,20,30};
    Check(Accepts(anchor,Kind::anchor,in,0),"anchor: a hit inside its box taken (the game's 0x3F6970)");
    Check(!Accepts(anchor,Kind::anchor,out,0),"anchor: a hit outside its box refused");
    Check(WindowOf(anchor,Kind::anchor).open,"anchor, no ship: open");
    alignas(16) static unsigned char ctrl[0x20]{};Put<std::int32_t>(ctrl,8,1);
    SetVtable(timeShip,0x17B93C0);
    Put<void*>(anchor,0x1160,timeShip);Put<void*>(anchor,0x1168,ctrl);anchor[0x1106]=0;
    Check(!WindowOf(anchor,Kind::anchor).open && !Accepts(anchor,Kind::anchor,in,0),"anchor, its ship alive and not freed: shut");
    anchor[0x1106]=1;
    Check(WindowOf(anchor,Kind::anchor).open && Accepts(anchor,Kind::anchor,in,0),"anchor freed (+0x1106), ship alive: open");
    Put<std::uint32_t>(timeShip,kObjFlags,kInvincible);
    Check(!WindowOf(anchor,Kind::anchor).open && !Accepts(anchor,Kind::anchor,in,0),"anchor whose ship is invincible: shut");
    Put<std::uint32_t>(timeShip,kObjFlags,0);Put<std::int32_t>(ctrl,8,0);
    Check(WindowOf(anchor,Kind::anchor).open,"anchor whose ship is gone: open");
    Put<std::uint32_t>(anchor,0x10A8,4);
    Check(!WindowOf(anchor,Kind::anchor).open,"anchor with +0x10A8 bit 2: shut");
    Put<std::uint32_t>(anchor,0x10A8,0);
    // A generator: its box.
    Shape s2;Node n2;
    SetVtable(gen,0x17BAEE0);MakeBox(gen,0xCA0,s2,n2,c,half);
    Check(KindOf(gen)==Kind::generator,"GeneratorPoll: the generator override");
    Check(Accepts(gen,Kind::generator,in,0) && !Accepts(gen,Kind::generator,out,0),"generator: inside its box only (the game's 0x401200)");
    // The timeShip ship itself never; a moving fortress part unless invincible; a monster and a plain object always.
    Check(KindOf(timeShip)==Kind::no && !Accepts(timeShip,Kind::no,at,0),"EDF6_TimeShip: never");
    SetVtable(part,0x17C7268);
    Check(KindOf(part)==Kind::fortressPart && Accepts(part,Kind::fortressPart,at,0),"MovingFortress_Battery: taken");
    Put<std::uint32_t>(part,kObjFlags,kInvincible);
    Check(!Accepts(part,Kind::fortressPart,at,0),"MovingFortress_Battery invincible: refused");
    SetVtable(monster,0x17C5628);
    Check(KindOf(monster)==Kind::yes,"Monster501: always");
    SetVtable(other,0x17C0BD0);   // EDF6_Martian: slot 16 the base's
    Check(KindOf(other)==Kind::yes,"a plain enemy: the base override (always)");
    SetVtable(other,0x17CACC0);   // UfoMother511CoreBigCannon: side effects, not asked
    Check(KindOf(other)==Kind::none,"the mothership's parts: not asked (their override sends network messages)");
}

// The plan: ray, lock points, window and timing.
const void* __fastcall BodyIsShip(std::uint32_t body) { return body>=100 && body<103 ? ship : body==7 ? other : nullptr; }
void Plans() {
    bodyObject=&BodyIsShip;
    std::memset(ship,0,sizeof(ship));
    Part parts[3];Body bodies[3];
    SetVtable(ship,0x17C9DC0);MakeRagdoll(ship,parts,bodies,3,100);Put<std::int32_t>(ship,0xD8C,1);
    const float pos[3]={0,100,0};std::memcpy(ship+kPosition,pos,12);
    SetVtable(soldier,0x17CDF28);
    const float eye[3]={0,1.5f,50};
    locks={{ship,{0,95,0}},{ship,{0,80,0}}};
    // Shut: skip, even with the ray on the core.
    rayOn=true;rayBody=101;std::memcpy(rayHit,locks[0].at,12);
    ResetWastedFire();
    Check(HoldFire(soldier,ship,eye,0.0f,0.0f,nullptr),"ship shut: held");
    Check(WastedFire(soldier,ship),"ship shut and no rhythm known: no target");
    // Open with timeShip on its timer, the ray meeting the core: fire, aimed at the first lock point.
    nowMs+=1000;ship[0xD84]=1;ship[0xD85]=1;Put<std::int32_t>(ship,0xD90,120);
    float aim[3]{};
    Check(!HoldFire(soldier,ship,eye,0.0f,0.0f,aim) && aim[1]==95.0f,"open, the core in line: fire at its lock point");
    // The hull in line whichever lock point: held.
    nowMs+=200;
    struct Seq { static const void* __fastcall Body(std::uint32_t) { return ship; } };
    bodyObject=&Seq::Body;
    rayBody=100;   // every ray: the hull
    Check(HoldFire(soldier,ship,eye,0.0f,0.0f,nullptr),"open, the hull in line for both lock points: held");
    // The round too slow for the timeShip left: 120 frames is 2 s; at 20 m/s over ~100 m it needs 5 s.
    nowMs+=200;rayBody=101;bodyObject=&BodyIsShip;
    Check(HoldFire(soldier,ship,eye,20.0f,0.0f,nullptr),"open but shut before a slow round gets there: held");
    nowMs+=200;
    Check(!HoldFire(soldier,ship,eye,1000.0f,0.0f,nullptr),"a fast round in timeShip: fire");
    // Another body first (something in the way): held.
    nowMs+=200;rayBody=7;
    Check(HoldFire(soldier,ship,eye,0.0f,0.0f,nullptr),"another object first in line: held");
    // A ray that meets nothing once the rays are known to meet objects: held (the round goes past).
    nowMs+=200;rayOn=false;
    Check(HoldFire(soldier,ship,eye,0.0f,0.0f,nullptr),"the ray meets nothing: held");
    // The rhythm: it shuts, then opens 4 s after its last opening; shut again: wait (aimed, held) when it is due.
    rayOn=true;rayBody=101;
    ship[0xD85]=0;nowMs+=1000;HoldFire(soldier,ship,eye,0.0f,0.0f,nullptr);
    const ULONGLONG lastOpen=nowMs+1000;
    nowMs=lastOpen;ship[0xD85]=1;HoldFire(soldier,ship,eye,0.0f,0.0f,nullptr);   // second opening: a period known
    nowMs+=500;ship[0xD85]=0;HoldFire(soldier,ship,eye,0.0f,0.0f,nullptr);
    nowMs+=1000;
    Check(HoldFire(soldier,ship,eye,0.0f,0.0f,nullptr) && !WastedFire(soldier,ship),
          "shut, due to open within the pre-aim: kept as the target (wait), trigger held");
    nowMs+=60000;
    Check(WastedFire(soldier,ship),"shut long past its rhythm: no target");
    // An ordinary enemy: always fire, no ray asked.
    rayOn=false;
    SetVtable(other,0x17C0BD0);Put<std::uint32_t>(other,kObjFlags,0);
    Check(!HoldFire(soldier,other,eye,0.0f,0.0f,nullptr),"an ordinary enemy: fire");
    Put<std::uint32_t>(other,kObjFlags,kInvincible);
    Check(HoldFire(soldier,other,eye,0.0f,0.0f,nullptr) && WastedFire(soldier,other),"an invincible enemy: held, no target");
    locks.clear();
}
}  // namespace

int main(int argc,char** argv) {
    if(argc<2 || !argv[1][0] || GetFileAttributesA(argv[1])==INVALID_FILE_ATTRIBUTES) {
        std::puts("SKIP: pass the supported EDF.dll path (private mapping, no DllMain)");return 77;
    }
    image=reinterpret_cast<unsigned char*>(LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES));
    if(!image){std::puts("FAIL: the DLL did not map");return 1;}
    const auto nt=reinterpret_cast<IMAGE_NT_HEADERS64*>(image+reinterpret_cast<IMAGE_DOS_HEADER*>(image)->e_lfanew);
    if(nt->FileHeader.TimeDateStamp!=0x678CCB46){std::puts("FAIL: not the supported EDF.dll");return 1;}
    Check(AcceptOk(),"the hit-acceptance overrides are the known code");
    Classes();
    Plans();
    std::printf("hit_accept_native_test: %d checks, %d failed\n",checks,failures);
    return failures ? 1 : 0;
}
