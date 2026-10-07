// The squads' formations (src/formation.h) checked offline:
//  - every shape, for 1 to 16 soldiers: nobody on the anchor or on another soldier (at least `spacing` x 0.7 apart),
//    and each shape where it says it is: the column behind the point, the wedge back and out at 45 degrees, the vee
//    forward, the line abreast, the echelons back to their side, the diamond round the point, the perimeter a ring of
//    soldiers `spacing` apart (never tighter than `spacing` from the centre);
//  - the world slot: turning the heading turns the slots with it (distances kept), and the right is the game's right;
//  - the heading follows the anchor's walk, not its standing turns;
//  - bounding overwatch swaps its halves when the movers are all there, or after kBoundMost, never with no movers;
//  - a soldier with no headway gives its slot up for kRestMs, then tries again;
//  - the key's cycles go round both lists and back to stock.
//   cmake --build build --target formation_check && build\formation_check.exe      (exit code 1 on a failure)
#include "../src/formation.h"
#include <cstdio>

namespace {
using namespace npc::formation;
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
bool Near(float a,float b,float tol) { return std::fabs(a-b)<=tol; }

void Shapes() {
    const float sp=5.0f;
    for(int si=1;si<kShapes;++si) {
        const Shape s=static_cast<Shape>(si);
        for(int n=1;n<=16;++n) {
            float x[16],z[16];
            for(int k=0;k<n;++k)Check(Offset(s,k,n,sp,&x[k],&z[k]),Name(s),k,n);
            for(int k=0;k<n;++k) {
                Check(std::sqrt(x[k]*x[k]+z[k]*z[k])>=sp*0.7f-1e-3f,"nobody on the anchor",si,k);
                for(int j=0;j<k;++j)Check(std::sqrt((x[k]-x[j])*(x[k]-x[j])+(z[k]-z[j])*(z[k]-z[j]))>=sp*0.7f-1e-3f,"nobody on another",si,k*100+j);
            }
            for(int k=0;k<n;++k) {
                switch(s) {
                case Shape::column: Check(x[k]==0.0f && z[k]<0.0f,"the column behind",k,z[k]);break;
                case Shape::staggered: Check(Near(std::fabs(x[k]),sp*0.5f,1e-4f) && z[k]<0.0f,"the staggered files behind",k,x[k]);break;
                case Shape::wedge: case Shape::bounding: Check(z[k]<0.0f && Near(std::fabs(x[k]),-z[k],1e-4f),"the wedge back at 45",k,x[k]);break;
                case Shape::vee: Check(z[k]>0.0f && Near(std::fabs(x[k]),z[k],1e-4f),"the vee forward at 45",k,x[k]);break;
                case Shape::line: Check(z[k]==0.0f && x[k]!=0.0f,"the line abreast",k,x[k]);break;
                case Shape::echelonLeft: Check(x[k]<0.0f && z[k]<0.0f,"echelon left back left",k,x[k]);break;
                case Shape::echelonRight: Check(x[k]>0.0f && z[k]<0.0f,"echelon right back right",k,x[k]);break;
                case Shape::perimeter: {
                    const float r=std::sqrt(x[k]*x[k]+z[k]*z[k]),r0=std::sqrt(x[0]*x[0]+z[0]*z[0]);
                    Check(Near(r,r0,1e-3f) && r>=sp-1e-3f,"the perimeter one ring",k,r);
                    if(n>=7 && k>0)Check(Near(std::sqrt((x[k]-x[k-1])*(x[k]-x[k-1])+(z[k]-z[k-1])*(z[k]-z[k-1])),sp,0.25f),
                                         "the perimeter's soldiers spacing apart",k,n);
                    break;
                }
                default: break;
                }
            }
            // The line and the wedge take both sides, the first left (x < 0).
            if(n>=2 && (s==Shape::line || s==Shape::wedge))Check(x[0]<0.0f && x[1]>0.0f,"left then right",si,n);
            if(s==Shape::perimeter)Check(Near(x[0],0.0f,1e-4f) && z[0]>0.0f,"the perimeter's first ahead (the threat)",x[0],z[0]);
        }
    }
    float x,z;
    Check(!Offset(Shape::stock,0,4,sp,&x,&z) && !Offset(Shape::wedge,-1,4,sp,&x,&z) && !Offset(Shape::wedge,0,4,0.0f,&x,&z),
          "stock, a negative place or no spacing: no slot");
}

void Worlds() {
    const float at[3]={100.0f,7.0f,-40.0f};
    // Heading +z: the right is the game's -x (its cameras' level right (-f.z, 0, f.x)).
    const float north[2]={0.0f,1.0f};
    float p[3];
    World(at,north,2.0f,0.0f,p);
    Check(Near(p[0],98.0f,1e-4f) && Near(p[2],-40.0f,1e-4f) && p[1]==7.0f,"right of +z is -x",p[0],p[2]);
    World(at,north,0.0f,3.0f,p);
    Check(Near(p[2],-37.0f,1e-4f),"forward along the heading",p[2]);
    // Turned: distances kept, the slot turned with the heading.
    for(float a=0.0f;a<6.3f;a+=0.5f) {
        const float f[2]={std::sin(a),std::cos(a)};
        float q[3];World(at,f,3.0f,-4.0f,q);
        const float d=std::sqrt((q[0]-at[0])*(q[0]-at[0])+(q[2]-at[2])*(q[2]-at[2]));
        Check(Near(d,5.0f,1e-3f),"turning keeps the distance",a,d);
        // Back along the heading by 4, right by 3: the dot with the heading is -4.
        Check(Near((q[0]-at[0])*f[0]+(q[2]-at[2])*f[1],-4.0f,1e-3f),"turning keeps the slot's place",a);
    }
}

void Headings() {
    Heading h{};
    const float start[3]={0.0f,0.0f,0.0f},east[2]={1.0f,0.0f};
    Track(h,start,east,2.0f);
    Check(h.set && h.fwd[0]==1.0f && h.fwd[1]==0.0f,"the first heading is the one given");
    const float step[3]={0.0f,0.0f,1.5f};
    Track(h,step,east,2.0f);
    Check(h.fwd[0]==1.0f,"under a step: kept",h.fwd[0]);
    const float walked[3]={0.0f,0.0f,3.0f};
    Track(h,walked,east,2.0f);
    Check(Near(h.fwd[1],1.0f,1e-5f) && Near(h.fwd[0],0.0f,1e-5f),"walked 3 m north: north",h.fwd[0],h.fwd[1]);
    Heading z{};const float none[2]={0.0f,0.0f};
    Track(z,start,none,2.0f);
    Check(z.fwd[1]==1.0f,"no initial way: +z");
}

void Bounds() {
    Bound b{};
    Check(!Step(b,3,3,1000) && b.since==1000 && b.moving==0,"the first step starts the bound");
    Check(!Step(b,3,1,2000) && b.moving==0,"movers still on their way: no swap");
    Check(Step(b,3,0,3000) && b.moving==1 && b.since==3000,"all there: the halves swap");
    Check(!Step(b,0,0,4000) && b.moving==1,"no movers (all fell): no swap but by time");
    Check(Step(b,2,2,3000+kBoundMost) && b.moving==0,"the bound too long: swapped anyway");
    Check(TeamOf(0)==0 && TeamOf(1)==1 && TeamOf(6)==0,"the halves: even and odd");
}

void Stuck() {
    Progress p{};
    Check(!GiveUp(p,20.0f,0),"starts walking");
    Check(!GiveUp(p,15.0f,1000),"headway: on");
    Check(!GiveUp(p,14.9f,3500),"too little headway, not yet kStuckMs since the last");
    Check(GiveUp(p,14.9f,1000+kStuckMs) && p.restUntil==1000+kStuckMs+kRestMs,"no headway for kStuckMs: given up",static_cast<double>(p.restUntil));
    Check(GiveUp(p,14.9f,1000+kStuckMs+kRestMs-1),"resting");
    Check(!GiveUp(p,14.9f,1000+kStuckMs+kRestMs) && p.restUntil==0,"rested: tries again");
    Check(!GiveUp(p,1.0f,99999),"there: never given up");
}

void Cycles() {
    Shape s=Shape::stock;
    int n=0;
    do{s=Next(s,false);++n;}while(s!=Shape::stock && n<50);
    Check(n==static_cast<int>(sizeof(kMarch)/sizeof(kMarch[0])),"the march cycle goes round",n);
    s=Shape::stock;n=0;
    do{s=Next(s,true);++n;}while(s!=Shape::stock && n<50);
    Check(n==static_cast<int>(sizeof(kGuard)/sizeof(kGuard[0])),"the guard cycle goes round",n);
    Check(FromInt(-1)==Shape::stock && FromInt(kShapes)==Shape::stock && FromInt(3)==Shape::wedge,"ini values");
}
}  // namespace

int main() {
    Shapes();
    Worlds();
    Headings();
    Bounds();
    Stuck();
    Cycles();
    std::printf(failures ? "formation_check: %d of %d FAILED\n" : "formation_check: all %d ok\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
