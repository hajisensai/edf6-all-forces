// The play area's walls (src/playarea.h) offline: the lane search for the ground's edge, the walls from it, their turn
// against the old square's (playerjet.cpp WallTurn before 2026-10-06), and a jet flown straight out and then held turning
// out at the walls, on a stock map's ground and on the big map's (seams and all): it must never be over the void.
// cmake --build build --target play_area_check && build\play_area_check.exe
#include "playarea.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace crew;

namespace {
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}

// A map's ground: the union of rectangles (x0, x1, z0, z1).
struct Rect { float x0,x1,z0,z1; };
struct Ground {
    const Rect* r; int n;
    bool At(float x,float z) const {
        for(int i=0;i<n;++i)if(x>=r[i].x0 && x<=r[i].x1 && z>=r[i].z0 && z<=r[i].z1)return true;
        return false;
    }
    // Within the ground's outer hull (the big map's seams between its blocks are inside it: floored, FloorClear).
    bool Within(float x,float z) const {
        float x0=1e9f,x1=-1e9f,z0=1e9f,z1=-1e9f;
        for(int i=0;i<n;++i){x0=std::fmin(x0,r[i].x0);x1=std::fmax(x1,r[i].x1);z0=std::fmin(z0,r[i].z0);z1=std::fmax(z1,r[i].z1);}
        return x>=x0 && x<=x1 && z>=z0 && z<=z1;
    }
};
// A stock map: the test range's ground (bigworld probe on the user's 15:45 log: ground within about +-1500 m, none at 2000).
const Rect kStock[]={{-1750.0f,1750.0f,-1750.0f,1250.0f}};
// The big map (tools/make_bigmap.py): nine stock blocks 3500 m apart, each its ground; z seams of 500 m between them.
Rect kBig[9];

// The plugin's measure (playarea.cpp MeasureSide / PlayAreaTick) on a synthetic ground.
PlayArea Measure(const Ground& g,float square,float worldHalf,float* edges) {
    const float c[2]={0.0f,0.0f};
    for(int s=0;s<4;++s) {
        const int k=s/2;
        const float out=s%2 ? -1.0f : 1.0f;
        float lanes[area::kLanes];
        for(int l=0;l<area::kLanes;++l) {
            const float across=c[1-k]+static_cast<float>(l-area::kLanes/2)*area::kLaneGap;
            lanes[l]=area::LaneEdge([&](float d){const float along=c[k]+out*d;return k==0 ? g.At(along,across) : g.At(across,along);},worldHalf);
        }
        edges[s]=area::MedianEdge(lanes);
    }
    return area::Combine(c,edges,square);
}

// playerjet.cpp WallTurn as it was (the play edge's square), the reference for EdgeTurn on a square.
constexpr float kBuffer=800.0f,kWallIn=0.3f;
void OldWallTurn(float edge,const float* pos,float* dir) {
    for(int i=0;i<3;i+=2) {
        const float out=pos[i]>0.0f ? 1.0f : -1.0f,away=dir[i]*out;
        const float most=area::Clamp((edge-std::fabs(pos[i]))/kBuffer,-kWallIn,1.0f);
        if(away<=most)continue;
        const int o=2-i;
        const float flat=std::sqrt(dir[0]*dir[0]+dir[2]*dir[2]);
        if(flat<1e-4f)continue;
        const float target=most*flat,along=std::sqrt(std::fmax(flat*flat-target*target,0.0f));
        float sense=dir[o];
        if(std::fabs(sense)<1e-3f) {
            float right[3]={-dir[2],0.0f,dir[0]};
            const float l=std::sqrt(right[0]*right[0]+right[2]*right[2]);
            if(l>0.0f){right[0]/=l;right[2]/=l;}else{right[0]=-1.0f;right[2]=0.0f;}
            sense=right[o];
        }
        dir[i]=out*target;
        dir[o]=(sense>=0.0f ? 1.0f : -1.0f)*along;
    }
    const float l=std::sqrt(dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2]);
    if(l>1e-6f){dir[0]/=l;dir[1]/=l;dir[2]/=l;}else{dir[0]=0.0f;dir[1]=0.0f;dir[2]=1.0f;}
}

// A jet flown out: from the centre at `speed` m/s along heading `head`, `turnOut` rad/s of the player's turn always toward
// straight out of the nearest wall (the worst the stick can do), the walls' turn after it (as Air does), `seconds` long.
// Returns the farthest it got past the ground (m over the void; <= 0: never), the AREA states seen in order.
struct Flight { float overVoid,pastWall; bool warnedFirst; int warned,turned; float outAtTurn; };
Flight FlyOut(const Ground& g,const PlayArea& a,float speed,float head,float turnOut,float seconds) {
    const float dt=1.0f/60.0f;
    float pos[3]={0.0f,300.0f,0.0f},dir[3]={std::sin(head),0.0f,std::cos(head)};
    Flight f{-1e9f,-1e9f,false,0,0,0.0f};
    bool seen1=false;
    for(int n=0;n<static_cast<int>(seconds/dt);++n) {
        // the stick: turn toward straight out of the nearest wall (x or z, whichever is nearer)
        const float rx=std::fmin(a.hi[0]-pos[0],pos[0]-a.lo[0]),rz=std::fmin(a.hi[1]-pos[2],pos[2]-a.lo[1]);
        float want[3]={0.0f,0.0f,0.0f};
        if(rx<rz)want[0]=a.hi[0]-pos[0]<pos[0]-a.lo[0] ? 1.0f : -1.0f;else want[2]=a.hi[1]-pos[2]<pos[2]-a.lo[1] ? 1.0f : -1.0f;
        const float cross=dir[2]*want[0]-dir[0]*want[2],dot=dir[0]*want[0]+dir[2]*want[2];
        const float turn=area::Clamp(std::atan2(cross,dot),-turnOut*dt,turnOut*dt);
        const float c=std::cos(turn),s=std::sin(turn),x=dir[0],z=dir[2];
        dir[0]=x*c+z*s;dir[2]=-x*s+z*c;
        float was[3]={dir[0],dir[1],dir[2]};
        const bool bent=area::EdgeTurn(a,pos,dir,kBuffer,kWallIn);
        const int state=bent ? 2 : area::EdgeState(a,pos,was,kBuffer+500.0f);
        if(state==1){seen1=true;++f.warned;}
        if(state==2) {
            if(!f.turned) {   // the first bend: after the caution, or so shallow the caution never applied
                f.warnedFirst=seen1;
                const float rooms[4]={a.hi[0]-pos[0],pos[0]-a.lo[0],a.hi[1]-pos[2],pos[2]-a.lo[1]},outs[4]={was[0],-was[0],was[2],-was[2]};
                for(int w=0;w<4;++w)if(rooms[w]<kBuffer)f.outAtTurn=std::fmax(f.outAtTurn,outs[w]);
            }
            ++f.turned;
        }
        for(int i=0;i<3;++i)pos[i]+=dir[i]*speed*dt;
        const float past=std::fmax(std::fmax(pos[0]-a.hi[0],a.lo[0]-pos[0]),std::fmax(pos[2]-a.hi[1],a.lo[1]-pos[2]));
        f.pastWall=std::fmax(f.pastWall,past);
        if(!g.At(pos[0],pos[2]) && !g.Within(pos[0],pos[2]))f.overVoid=std::fmax(f.overVoid,1.0f);   // past the map, not a seam
    }
    return f;
}
}  // namespace

int main() {
    for(int i=0;i<3;++i)for(int k=0;k<3;++k) {
        const float cx=(i-1)*3500.0f,cz=(k-1)*3500.0f;
        kBig[i*3+k]=Rect{cx-1750.0f,cx+1750.0f,cz-1750.0f,cz+1250.0f};
    }
    const Ground stock{kStock,1},big{kBig,9};

    // 1. The lane search.
    auto edgeOf=[](const Ground& g,float lane){return area::LaneEdge([&](float d){return g.At(d,lane);},6000.0f);};
    const float e1=edgeOf(stock,0.0f);
    std::printf("     stock lane +x edge %.1f (ground to 1750)\n",e1);
    Check(e1<=1750.0f && e1>1750.0f-area::kFine,"stock map: the lane finds the ground's edge within kFine");
    const float e2=area::LaneEdge([&](float d){return big.At(0.0f,d);},6000.0f);
    std::printf("     big map lane +z edge %.1f (blocks to 4750, a 500 m seam at 1250..1750)\n",e2);
    Check(e2<=4750.0f && e2>4750.0f-area::kFine,"big map: the lane crosses the 500 m seam and finds the outer edge");
    const Rect wide[]={{-1750.0f,1750.0f,-100.0f,100.0f},{3500.0f,5000.0f,-100.0f,100.0f}};
    const Ground gap{wide,2};
    const float e3=edgeOf(gap,0.0f);
    Check(e3<=1750.0f && e3>1750.0f-area::kFine,"a void wider than kGapMost ends the lane at the near ground");
    const Rect none[]={{9000.0f,9100.0f,0.0f,1.0f}};
    Check(edgeOf(Ground{none,1},0.0f)<0.0f,"no ground near the centre: -1");

    // 2. The median.
    const float lanesA[area::kLanes]={1700.0f,1750.0f,5000.0f,1745.0f,1748.0f};
    Check(area::MedianEdge(lanesA)==1748.0f,"one lane through a pier far out does not move the edge");
    const float lanesB[area::kLanes]={-1.0f,-1.0f,1700.0f,-1.0f,1750.0f};
    Check(area::MedianEdge(lanesB)<0.0f,"fewer than half the lanes found ground: unknown");

    // 3. The walls.
    float edges[4];
    const PlayArea sa=Measure(stock,2400.0f,3000.0f,edges);
    std::printf("     stock walls x %.0f..%.0f z %.0f..%.0f (edges %.0f %.0f %.0f %.0f)\n",sa.lo[0],sa.hi[0],sa.lo[1],sa.hi[1],
                edges[0],edges[1],edges[2],edges[3]);
    Check(sa.ground && sa.hi[0]<=1750.0f-area::kVoidMargin+0.1f && sa.lo[1]>=-1750.0f+area::kVoidMargin-0.1f &&
          sa.hi[1]<=1250.0f-area::kVoidMargin+0.1f,"stock map: the walls kVoidMargin inside the ground, not at the +-2400 square");
    const PlayArea ba=Measure(big,5250.0f,6000.0f,edges);
    std::printf("     big map walls x %.0f..%.0f z %.0f..%.0f\n",ba.lo[0],ba.hi[0],ba.lo[1],ba.hi[1]);
    Check(ba.hi[0]>5000.0f && ba.hi[0]<=5250.0f-area::kVoidMargin+0.1f && ba.hi[1]<=4750.0f-area::kVoidMargin+0.1f,
          "big map (BigWorld 6000): the walls at its outer ground edge, the blocks all in");
    const float c0[2]={0.0f,0.0f},unknownX[4]={-1.0f,1750.0f,1750.0f,1750.0f},farOut[4]={9000.0f,9000.0f,1750.0f,1750.0f};
    const PlayArea ux=area::Combine(c0,unknownX,2400.0f),fo=area::Combine(c0,farOut,2400.0f);
    Check(ux.hi[0]==2400.0f && ux.lo[0]==-2400.0f && ux.hi[1]==1600.0f,"a side with no ground keeps the square on that axis only");
    Check(fo.hi[0]==2400.0f && fo.lo[0]==-2400.0f,"ground past the physics square: the square's walls (never past +-PlayEdge)");

    // 4. EdgeTurn on the old square is the old WallTurn.
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.0f,1.0f);
    const PlayArea sq{{-2400.0f,-2400.0f},{2400.0f,2400.0f},false};
    float worst=0.0f;
    for(int n=0;n<200000;++n) {
        const float pos[3]={u(rng)*2700.0f,0.0f,u(rng)*2700.0f};
        float d[3]={u(rng),u(rng)*0.5f,u(rng)};
        const float l=std::sqrt(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]);
        if(l<1e-3f)continue;
        for(float& x:d)x/=l;
        float a[3]={d[0],d[1],d[2]},b[3]={d[0],d[1],d[2]};
        OldWallTurn(2400.0f,pos,a);
        area::EdgeTurn(sq,pos,b,kBuffer,kWallIn);
        for(int i=0;i<3;++i)worst=std::fmax(worst,std::fabs(a[i]-b[i]));
    }
    std::printf("     EdgeTurn vs the old WallTurn on the +-2400 square: worst difference %.2e over 200000 paths\n",worst);
    Check(worst<1e-5f,"on a square centred at 0, EdgeTurn bends every path exactly as the old WallTurn");

    // 4b. A void within the walls (a seam) is floored; past them, or measured with no floor, it stays a void.
    PlayArea fl=ba;fl.floor=-29.0f;fl.hasFloor=true;
    const float noGround=-1e9f,inSeam[3]={0.0f,120.0f,1500.0f},outside[3]={0.0f,120.0f,5200.0f},onGround[3]={0.0f,120.0f,0.0f};
    Check(area::FloorClear(fl,inSeam,noGround,noGround)==149.0f,"over a seam within the walls: 149 m over the floor at -29");
    Check(area::FloorClear(fl,outside,noGround,noGround)==noGround,"past the walls: no floor made up");
    Check(area::FloorClear(fl,onGround,80.0f,noGround)==80.0f,"over ground: the map ray's own clearance");
    PlayArea nf=fl;nf.hasFloor=false;
    Check(area::FloorClear(nf,inSeam,noGround,noGround)==noGround,"no floor measured: none made up");

    // 5. Flown out: never over the void, the caution before the turn.
    struct Case { const char* name; const Ground* g; const PlayArea* a; float speed,turn; };
    const Case cases[]={{"stock, fighter 260 m/s, stick held out",&stock,&sa,260.0f,0.6f},
                        {"stock, steepest dive 340 m/s, stick held out",&stock,&sa,340.0f,1.2f},
                        {"stock, gunship 145 m/s, stick held out",&stock,&sa,145.0f,0.12f},
                        {"big map, fighter 260 m/s, stick held out",&big,&ba,260.0f,0.6f}};
    for(const Case& c:cases) {
        float over=-1e9f,past=-1e9f;
        int warned=0,turned=0;
        float silent=0.0f;   // the steepest way out at a wall that bent it with no caution before
        for(int h=0;h<72;++h) {
            const Flight f=FlyOut(*c.g,*c.a,c.speed,h*0.0872665f,c.turn,180.0f);
            over=std::fmax(over,f.overVoid);past=std::fmax(past,f.pastWall);
            if(!f.warnedFirst)silent=std::fmax(silent,f.outAtTurn);
            warned+=f.warned;turned+=f.turned;
        }
        std::printf("     %s: 72 headings x 180 s, most past a wall %.1f m, over the void %s; AREA caution %d frames, turned %d;"
                    " the steepest bend with no caution first %.2f\n",c.name,past,over>0.0f ? "YES" : "never",warned,turned,silent);
        char what[160];
        std::snprintf(what,sizeof(what),"%s: never past the map's ground, never past a wall by more than two frames",c.name);
        Check(over<=0.0f && past<=2.0f*c.speed/60.0f,what);
        std::snprintf(what,sizeof(what),"%s: the AREA caution shows before the walls turn it (unless it only grazes them)",c.name);
        Check(warned>0 && silent<=area::kHeadingOut+1e-4f,what);
    }
    std::printf(failures ? "%d FAILED\n" : "all passed\n",failures);
    return failures ? 1 : 0;
}
