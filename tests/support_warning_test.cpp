// src/support_warning_logic.h offline: every barrage's warned area holds every round it plans with its blast, the ultimate's
// its whole storm and rings, a flight's route runs from its entry through the point (a sortie's on past it along its pass),
// the carpet's strip holds the bombs' band.
#include "../src/support_warning_logic.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
int checks=0;
void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){std::fprintf(stderr,"FAIL: %s\n",what);std::exit(1);}
}
float Flat(const float* a,const float* b) { return std::hypot(a[0]-b[0],a[2]-b[2]); }
}  // namespace

int main() {
    using namespace crew;
    const float target[3]={120.0f,35.0f,-40.0f};
    const float headings[2][3]={{0.0f,0.0f,1.0f},{0.6f,0.0f,-0.8f}};
    static barrage::Round rounds[barrage::kMaxRounds];
    for(const auto& heading:headings)for(int k=0;k<barrage::kKinds;++k) {
        warning::Circle c;
        Check(warning::BarrageArea(k,target,heading,&c),"every kind has an area");
        barrage::Setup s;
        for(int i=0;i<3;++i){s.target[i]=target[i];s.heading[i]=heading[i];}
        s.seed=barrage::Seed(target,heading,k);s.worldHalf=1.0e6f;
        const float y=target[1];
        const int n=barrage::Plan(static_cast<barrage::Kind>(k),s,[y](float,float,float){return y;},rounds,barrage::kMaxRounds);
        Check(n>0,"the plan has rounds");
        float largest=0.0f;
        for(int i=0;i<n;++i) {
            Check(Flat(rounds[i].to,c.centre)+rounds[i].blast<=c.radius+0.01f,"every round's point and blast inside the area");
            largest=std::fmax(largest,rounds[i].blast);
        }
        Check(c.radius>=largest,"the area is at least one blast across");
        Check(c.centre[1]==target[1],"on the point's plane");
        Check(c.radius<600.0f,"no area wider than a barrage reaches");
    }
    {warning::Circle c;
     Check(warning::BarrageArea(6,target,headings[0],&c) && Flat(c.centre,target)<0.5f &&
           std::fabs(c.radius-barrage::kSpecs[6].blast)<0.5f,"the heavy mortar's one round: its blast round the point");
     Check(!warning::BarrageArea(-1,target,headings[0],&c) && !warning::BarrageArea(barrage::kKinds,target,headings[0],&c),
           "no area for an unknown kind");}
    {warning::Circle c;
     Check(warning::BarrageArea(4,target,headings[0],&c) && c.centre[2]>target[2]+50.0f,
           "the creeping barrage's area lies ahead of the point, along its heading");}
    Check(warning::UltimateRadius()==ultimate::kRadius+ultimate::kRingBlast,"the ultimate's: its storm and its outer ring's blast");

    // A ring: every point on it.
    {warning::Circle c;c.centre[0]=10;c.centre[1]=2;c.centre[2]=-3;c.radius=50;
     float pts[48][3];warning::Ring(c,48,pts);
     bool on=true;for(auto& p:pts)on=on && std::fabs(Flat(p,c.centre)-50.0f)<0.01f && p[1]==2;
     Check(on,"a ring's points on its circle and plane");}

    // A route: from the entry to the point, a sortie's on past it.
    const float from[3]={-2000.0f,400.0f,-40.0f};
    {float pts[4][3];
     Check(warning::Route(from,target,0.0f,pts)==2 && !std::memcmp(pts[0],from,12) && !std::memcmp(pts[1],target,12),
           "a guard's route: the entry, then the point");
     Check(warning::Route(from,target,sortie::kPassBeyond,pts)==3,"a pass: the entry, the point, where it leaves");
     Check(std::fabs(Flat(pts[1],target))<0.5f,"the pass goes through the point (its entry in line)");
     Check(std::fabs(Flat(pts[2],pts[1])-sortie::kPassBeyond)<0.5f && pts[2][0]>pts[1][0],"...on along its heading by kPassBeyond");
     const float aside[3]={-2000.0f,400.0f,500.0f};
     warning::Route(aside,target,sortie::kPassBeyond,pts);
     Check(Flat(pts[1],target)<=sortie::kMaxOffset+0.5f,"an entry off the line: the pass beside the point, at most kMaxOffset");}

    // The carpet: its corners kCarpetHalf + blast along, kMaxOffset + blast across.
    {float c[4][3];warning::CarpetStrip(from,target,20.0f,c);
     const float along=Flat(c[0],c[1]),across=Flat(c[1],c[2]);
     Check(std::fabs(along-2.0f*(sortie::kCarpetHalf+20.0f))<0.5f && std::fabs(across-2.0f*(sortie::kMaxOffset+20.0f))<0.5f,
           "the strip's sides");
     float mid[3]={0,0,0};for(auto& p:c)for(int i=0;i<3;++i)mid[i]+=p[i]*0.25f;
     Check(Flat(mid,target)<0.5f,"centred on the point");}
    Check(warning::BarrageMs(30)==1000 && warning::BarrageMs(600)==10000,"a barrage's warning lasts its rounds, a moment at least");
    std::printf("support warning: %d checks passed\n",checks);
    return 0;
}
