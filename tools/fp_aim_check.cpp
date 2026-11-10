// The first person's aim convergence and stock-action arms (src/fp_aim.h), offline:
//  - a gun 0.25 m right of and 0.35 m below the eye, its shot parallel to the view: the turned shot passes through the
//    point the view meets (the ground 6 m ahead on a down slope, a wall 3 m off, nothing: 1.5 km along the view);
//  - the turn is the least one: the roll and each row's length kept, the rows still orthogonal, the w column untouched;
//  - a hit closer than the muzzle (a wall at the nose) aims kMinAhead past the muzzle, never back;
//  - a gun laid off the view by its animation (mode 1: 12 degrees down while walking) is brought onto the point;
//  - refused (rows untouched): past kMostTurn, not finite, no direction; unchanged: already on it;
//  - NativeArms: every armed set's weapon hidden (an appeal), not when one is still shown, not with no weapon at all.
// Exit code 1 when one fails. cmake --build build --target fp_aim_check && build\fp_aim_check.exe
#include "../src/fp_aim.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
using namespace crew::fpaim;
int failures=0;

void Expect(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}

// The shortest distance from point p to the line through `from` along `dir` (unit), and how far along it p lies.
float LineGap(const float* from,const float* dir,const float* p,float* along) {
    const float d[3]={p[0]-from[0],p[1]-from[1],p[2]-from[2]};
    *along=Dot(d,dir);
    float c[3];Cross(d,dir,c);
    return std::sqrt(Dot(c,c));
}

// Rows (right, up, forward; w 0) from a forward and an up hint: right = up x forward (the game's rows: x right, y up, z
// ahead, so right x up = forward).
void Rows(const float* forward,const float* upHint,float* rows) {
    float f[3]={forward[0],forward[1],forward[2]};Unit3(f);
    float r[3];Cross(upHint,f,r);Unit3(r);
    float u[3];Cross(f,r,u);
    for(int i=0;i<3;++i){rows[i]=r[i];rows[4+i]=u[i];rows[8+i]=f[i];}
    rows[3]=rows[7]=rows[11]=0.0f;
}

bool Orthonormal(const float* rows) {
    for(int a=0;a<3;++a) {
        if(std::fabs(std::sqrt(Dot(rows+a*4,rows+a*4))-1.0f)>1e-4f)return false;
        for(int b=a+1;b<3;++b)if(std::fabs(Dot(rows+a*4,rows+b*4))>1e-4f)return false;
    }
    float c[3];Cross(rows,rows+4,c);
    return Dot(c,rows+8)>0.999f;   // still right-handed as before
}

// The shot's line through `point`: within `tol` m of it, ahead of the muzzle.
bool Through(const float* rows,const float* muzzle,const float* point,float tol) {
    float along=0.0f;
    const float gap=LineGap(muzzle,rows+8,point,&along);
    return gap<tol && along>0.0f;
}

void Parallax() {
    // Looking down a slope at 20 degrees: the eye at 1.6 m, the gun 0.25 m right (x), 0.35 m lower, 0.4 m ahead.
    const float pitch=-20.0f*3.14159265f/180.0f;
    const float dir[3]={0.0f,std::sin(pitch),std::cos(pitch)};
    const float eye[3]={0.0f,1.6f,0.0f};
    const float muzzle[3]={0.25f,1.25f,0.4f};
    const float up[3]={0.0f,1.0f,0.0f};
    float rows[12];Rows(dir,up,rows);
    float native[12];std::memcpy(native,rows,sizeof(rows));
    // The ground the view meets 6 m ahead along the view: the native shot (parallel) misses it by the offset.
    const float hit=6.0f;
    const float point[3]={eye[0]+dir[0]*hit,eye[1]+dir[1]*hit,eye[2]+dir[2]*hit};
    float along=0.0f;
    Expect(LineGap(muzzle,native+8,point,&along)>0.3f,"the native shot passes the crosshair's point by the gun's offset (> 0.3 m)");
    Expect(Converge(rows,muzzle,eye,dir,hit)==Result::turned,"the shot turns toward the point the view meets");
    Expect(Through(rows,muzzle,point,1e-3f),"the turned shot passes through that point (1 mm)");
    Expect(Orthonormal(rows),"the rows stay orthonormal and right-handed");
    float roll[3];Cross(native+8,rows+8,roll);
    // The least turn: the axis is perpendicular to both forwards; the right row's height change is only what the turn
    // about that axis gives (no extra roll): measured as the right row staying perpendicular to the plane's normal mix.
    float ax[3]={roll[0],roll[1],roll[2]};Unit3(ax);
    float before[3]={native[0],native[1],native[2]},after[3]={rows[0],rows[1],rows[2]};
    Expect(std::fabs(Dot(before,ax)-Dot(after,ax))<1e-4f && std::fabs(Dot(native+4,ax)-Dot(rows+4,ax))<1e-4f,
           "the least turn: the rows keep their parts along the turn's axis (no added roll)");
    Expect(rows[3]==0.0f && rows[7]==0.0f && rows[11]==0.0f,"the w column untouched");

    // The same with a wall 3 m off to the side of the view's line: the point it meets.
    float rows2[12];std::memcpy(rows2,native,sizeof(native));
    const float point3[3]={eye[0]+dir[0]*3.0f,eye[1]+dir[1]*3.0f,eye[2]+dir[2]*3.0f};
    Expect(Converge(rows2,muzzle,eye,dir,3.0f)==Result::turned && Through(rows2,muzzle,point3,1e-3f),"a wall 3 m along the view: the shot meets it at the crosshair");

    // No hit (the sky): the point 1.5 km along the view.
    float rows3[12];std::memcpy(rows3,native,sizeof(native));
    const float far3[3]={eye[0]+dir[0]*kFar,eye[1]+dir[1]*kFar,eye[2]+dir[2]*kFar};
    Expect(Converge(rows3,muzzle,eye,dir,-1.0f)==Result::turned && Through(rows3,muzzle,far3,0.05f),"nothing hit: the shot meets the view 1.5 km out");
    float t[3]={rows3[8]-native[8],rows3[9]-native[9],rows3[10]-native[10]};
    Expect(std::sqrt(Dot(t,t))<1e-3f,"...turned well under a milliradian (about the native line)");
}

void TooClose() {
    // A wall at the nose (0.2 m): the point is kMinAhead past the muzzle along the view, the shot still ahead.
    const float dir[3]={0.0f,0.0f,1.0f};
    const float eye[3]={0.0f,1.6f,0.0f};
    const float muzzle[3]={0.25f,1.25f,0.6f};
    const float up[3]={0.0f,1.0f,0.0f};
    float rows[12];Rows(dir,up,rows);
    float p[3];AimPoint(eye,dir,muzzle,0.2f,kFar,p);
    Expect(std::fabs(p[2]-(0.6f+kMinAhead))<1e-5f,"a hit at the nose: aimed kMinAhead past the muzzle along the view");
    Expect(Converge(rows,muzzle,eye,dir,0.2f)==Result::turned && rows[10]>0.9f,"...the shot still goes ahead (forward z > 0.9)");
}

void Animated() {
    // A mode-1 gun laid 12 degrees below the view by the walk's swing: brought onto the point 40 m out.
    const float dir[3]={0.0f,0.0f,1.0f};
    const float eye[3]={0.0f,1.6f,0.0f};
    const float muzzle[3]={0.2f,1.3f,0.5f};
    const float a=-12.0f*3.14159265f/180.0f;
    const float laid[3]={0.0f,std::sin(a),std::cos(a)};
    const float up[3]={0.0f,1.0f,0.0f};
    float rows[12];Rows(laid,up,rows);
    const float point[3]={0.0f,1.6f,40.0f};
    Expect(Converge(rows,muzzle,eye,dir,40.0f)==Result::turned && Through(rows,muzzle,point,1e-3f),"a gun laid 12 deg off by its animation hits the crosshair's point");
}

void Refusals() {
    const float dir[3]={0.0f,0.0f,1.0f};
    const float eye[3]={0.0f,1.6f,0.0f};
    const float up[3]={0.0f,1.0f,0.0f};
    // Past kMostTurn: a gun laid 60 degrees off.
    const float a=60.0f*3.14159265f/180.0f;
    const float laid[3]={std::sin(a),0.0f,std::cos(a)};
    float rows[12];Rows(laid,up,rows);
    float before[12];std::memcpy(before,rows,sizeof(rows));
    const float muzzle[3]={0.2f,1.3f,0.5f};
    Expect(Converge(rows,muzzle,eye,dir,50.0f)==Result::refused && std::memcmp(rows,before,sizeof(rows))==0,"a turn past 45 deg: refused, rows untouched");
    // Not finite.
    Rows(dir,up,rows);std::memcpy(before,rows,sizeof(rows));
    const float bad[3]={NAN,1.0f,0.0f};
    Expect(Converge(rows,bad,eye,dir,50.0f)==Result::refused && std::memcmp(rows,before,sizeof(rows))==0,"a muzzle not finite: refused, rows untouched");
    const float none[3]={0.0f,0.0f,0.0f};
    Expect(Converge(rows,muzzle,eye,none,50.0f)==Result::refused && std::memcmp(rows,before,sizeof(rows))==0,"no view direction: refused, rows untouched");
    // Already on it: the muzzle on the view's line.
    const float onLine[3]={0.0f,1.6f,0.5f};
    Expect(Converge(rows,onLine,eye,dir,50.0f)==Result::unchanged && std::memcmp(rows,before,sizeof(rows))==0,"a muzzle on the view's line: unchanged");
}

void Arms() {
    const bool armed2[2]={true,true},hiddenBoth[2]={true,true},hiddenOne[2]={true,false};
    Expect(NativeArms(2,armed2,hiddenBoth),"both sets' weapons hidden (an appeal): native arms");
    Expect(!NativeArms(2,armed2,hiddenOne),"one weapon still shown: the viewmodel");
    const bool armedOne[2]={true,false},hiddenFirst[2]={true,false};
    Expect(NativeArms(2,armedOne,hiddenFirst),"the only armed set hidden: native arms");
    const bool armedNone[2]={false,false},hiddenNone[2]={false,false};
    Expect(!NativeArms(2,armedNone,hiddenNone),"no weapon at all: not an action");
    Expect(!NativeArms(0,armedNone,hiddenNone),"no sets: not an action");
}
}  // namespace

int main() {
    Parallax();
    TooClose();
    Animated();
    Refusals();
    Arms();
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
