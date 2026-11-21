// The world text's math (src/world_text.h, docs/world-text-re.md §5 / §6) checked offline, through cameras of both
// handednesses (the test's D3D-like one and the game's, whose right looking north is east, -X: map_cam.h Right):
//  - the camera's frame read off a viewProj (plain, off-centre / jittered, rolled, zoomed, the map's), its eye;
//  - an upright plate made facing the camera reads left to right and top down there, its normal level and fixed: the
//    facings cache hands the same plate back whatever the camera does later;
//  - seen at a slant its corners go through the viewProj in perspective (the edge nearer the camera the taller);
//  - seen from behind its face is the side nearest the camera and it still reads left to right, over the same span;
//  - its layers: the face nearest the camera, each next one further (drawn last to first: the nearer over the further);
//  - the map's grid labels lie on the ground north up and read from above;
//  - the size: metres fixed between the bounds, held at kFarLeast / kNearMost of the 2D height on the screen (a slanted
//    plate grown for its foreshortening), the font scale the nominal one on the grid whatever the depth;
//  - the facings cache: at most kFacingSlots, one not asked for in kForgetMs made anew and given up first;
//  - a point behind the camera or a viewProj without perspective refused; the glow and the shimmer.
//   cmake --build build --target world_text_check && build\world_text_check.exe      (exit code 1 on a failure)
#include "../src/world_text.h"
#include <cstdio>
#include <initializer_list>

namespace {
using namespace crew::worldtext;
int failures=0,cases=0;
void Check(bool ok,const char* what,double a=0.0,double b=0.0) {
    ++cases;
    if(ok)return;
    ++failures;
    std::printf("FAIL %s (%g, %g)\n",what,a,b);
}
void Unit(float* v) { const float l=std::sqrt(Dot(v,v));for(int i=0;i<3;++i)v[i]/=l; }

// A camera's viewProj (row vectors, D3D depth 0..1): at `eye` looking along `look`, `up` the world's up (rolled by `roll`
// rad), vertical field of view `fov` rad on a `w` x `h` screen; `jx`, `jy` an off-centre shift (clip x / y += j * w: a TAA
// jitter, a split screen's off-centre frustum). `game`: the game's handedness (its screen's right the mirror of this one's).
void Camera(const float* eye,const float* lookIn,float roll,float fov,float w,float h,float jx,float jy,float* vp,bool game=false) {
    float f[3]={lookIn[0],lookIn[1],lookIn[2]};Unit(f);
    const float worldUp[3]={0.0f,1.0f,0.0f};
    float r[3],u[3];
    Cross(worldUp,f,r);
    if(Dot(r,r)<1e-8f){const float z[3]={0.0f,0.0f,1.0f};Cross(z,f,r);}   // straight down: north up the screen
    Unit(r);Cross(f,r,u);
    const float c=std::cos(roll),s=std::sin(roll);
    float rr[3],uu[3];
    for(int i=0;i<3;++i){rr[i]=r[i]*c+u[i]*s;uu[i]=u[i]*c-r[i]*s;}
    if(game)for(float& v:rr)v=-v;
    const float fy=1.0f/std::tan(fov*0.5f),fx=fy*h/w,zn=0.5f,zf=20000.0f,A=zf/(zf-zn),B=-zn*A;
    for(int i=0;i<3;++i){vp[i*4]=rr[i]*fx+f[i]*jx;vp[i*4+1]=uu[i]*fy+f[i]*jy;vp[i*4+2]=f[i]*A;vp[i*4+3]=f[i];}
    vp[12]=-Dot(eye,rr)*fx-Dot(eye,f)*jx;vp[13]=-Dot(eye,uu)*fy-Dot(eye,f)*jy;vp[14]=-Dot(eye,f)*A+B;vp[15]=-Dot(eye,f);
}
// A camera `dist` m from `at`, `yawDeg` round it (0: south of it looking north) and `pitchDeg` above it, looking at it.
void Orbit(const float* at,float dist,float yawDeg,float pitchDeg,float* eye,float* vp,bool game,float w=1920.0f,float h=1080.0f) {
    const float y=yawDeg*0.0174532925f,p=pitchDeg*0.0174532925f;
    eye[0]=at[0]-std::sin(y)*std::cos(p)*dist;eye[1]=at[1]+std::sin(p)*dist;eye[2]=at[2]-std::cos(y)*std::cos(p)*dist;
    const float look[3]={at[0]-eye[0],at[1]-eye[1],at[2]-eye[2]};
    Camera(eye,look,0.0f,0.785398f,w,h,0.0f,0.0f,vp,game);
}

// The frame through one camera at one point: unit, square axes, a metre's pixels as the projection's, the eye found.
void FrameChecks(const char* name,const float* vp,float w,float h,const float* p,const float* eye) {
    Frame f{};
    const bool ok=FrameAt(vp,w,h,p,&f);
    Check(ok,name);
    if(!ok)return;
    Check(std::fabs(Dot(f.right,f.right)-1.0f)<1e-4f && std::fabs(Dot(f.down,f.down)-1.0f)<1e-4f,"frame axes unit");
    Check(std::fabs(Dot(f.right,f.down))<1e-4f && std::fabs(Dot(f.right,f.forward))<1e-4f && std::fabs(Dot(f.down,f.forward))<1e-4f,
          "frame axes square");
    float ax=0.0f,ay=0.0f,bx=0.0f,by=0.0f,cx=0.0f,cy=0.0f;
    const float pr[3]={p[0]+f.right[0],p[1]+f.right[1],p[2]+f.right[2]};
    const float pd[3]={p[0]+f.down[0],p[1]+f.down[1],p[2]+f.down[2]};
    Check(ToScreen(vp,w,h,p,&ax,&ay) && ToScreen(vp,w,h,pr,&bx,&by) && ToScreen(vp,w,h,pd,&cx,&cy),"point on the screen");
    Check(std::fabs((bx-ax)-f.pxX)<f.pxX*1e-3f+1e-3f && std::fabs(by-ay)<f.pxX*1e-3f+1e-3f,"a metre right",bx-ax,f.pxX);
    Check(std::fabs((cy-ay)-f.pxY)<f.pxY*1e-3f+1e-3f && std::fabs(cx-ax)<f.pxY*1e-3f+1e-3f,"a metre down",cy-ay,f.pxY);
    float e[3];
    Check(EyeOf(vp,e) && std::fabs(e[0]-eye[0])<0.02f && std::fabs(e[1]-eye[1])<0.02f && std::fabs(e[2]-eye[2])<0.02f,"the eye",e[0]-eye[0],e[2]-eye[2]);
}

// A label of `w` x `h` glyph px laid out on plate `s` at `at` as hud.cpp's WorldLabel / DrawWorld lay it out (`mpp` m a
// glyph pixel, `layers`), seen through `vp` from `eye`. Its face's corners on the screen: [0] top left, [1] top right,
// [2] bottom left, [3] bottom right (of the text as it reads). False: a corner behind the camera.
struct Seen { float x[4],y[4]; Plate p; };
bool See(const Standee& s,const float* at,float w,float h,int align,float mpp,int layers,const float* vp,const float* eye,Seen* out,
         float sw=1920.0f,float sh=1080.0f) {
    Lay(s,at,w,h,align,mpp,layers,Behind(s,at,eye),&out->p);
    float m[16];LayerMatrix(out->p,0,m);
    const float corner[4][2]={{0.0f,0.0f},{w,0.0f},{0.0f,h},{w,h}};
    for(int i=0;i<4;++i) {
        float q[3];Local(m,corner[i][0],corner[i][1],q);
        if(!ToScreen(vp,sw,sh,q,&out->x[i],&out->y[i]))return false;
    }
    return true;
}
// It reads: left to right along both edges, top over bottom on both sides.
bool Reads(const Seen& v) { return v.x[1]>v.x[0] && v.x[3]>v.x[2] && v.y[2]>v.y[0] && v.y[3]>v.y[1]; }
// Its layers: the face nearest the camera, each next one further (its middle's distance from the eye: the layers are
// parallel planes, the further along the eye's ray under the nearer).
bool LayersBackward(const Plate& p,float w,float h,int layers,const float* vp) {
    float last=-1e30f,eye[3]{};
    EyeOf(vp,eye);
    for(int k=0;k<layers;++k) {
        float m[16],q[3];LayerMatrix(p,k,m);Local(m,w*0.5f,h*0.5f,q);
        const float d=std::sqrt((q[0]-eye[0])*(q[0]-eye[0])+(q[1]-eye[1])*(q[1]-eye[1])+(q[2]-eye[2])*(q[2]-eye[2]));
        if(!(d>last))return false;
        last=d;
    }
    return true;
}

void UprightChecks(bool game) {
    const char* const hand=game ? "game-handed" : "D3D-handed";
    const float at[3]={40.0f,2.0f,90.0f};
    const float w=180.0f*0.4375f,h=32.0f*0.4375f;   // a label as the game measures it at fd 0.4375
    float eye[3],vp[16];
    // Made facing a camera south-west of it, a little above.
    Orbit(at,30.0f,25.0f,8.0f,eye,vp,game);
    FrameChecks(hand,vp,1920.0f,1080.0f,at,eye);
    Frame f{};
    Check(FrameAt(vp,1920.0f,1080.0f,at,&f) && f.hand==(game ? -1.0f : 1.0f),"handedness read off the viewProj",f.hand);
    Standee made{};
    Check(Upright(f,&made),"upright made");
    Check(std::fabs(made.normal[1])<1e-6f && std::fabs(Dot(made.normal,made.normal)-1.0f)<1e-5f && made.down[1]==-1.0f &&
          std::fabs(Dot(made.right,made.normal))<1e-6f && std::fabs(made.right[1])<1e-6f,"upright: level normal, down -Y");
    Check(!Behind(made,at,eye),"made facing the camera");
    Facings cache{};
    const std::uint64_t key=Key(3,42);
    Standee s=FacingOf(cache,key,1000,made);
    const float mpp=SizeFactor(f.depth,kViewRef,Facing(f,s))*0.42f/0.4375f/f.pxY;
    Seen v;
    for(int align=0;align<3;++align) {
        Check(See(s,at,w,h,align,mpp,8,vp,eye,&v) && Reads(v),"reads where it was made",v.x[1]-v.x[0],v.y[2]-v.y[0]);
        Check(LayersBackward(v.p,w,h,8,vp),"layers: the face nearest");
    }
    // Square on, at the reference: the 2D height on the screen (it was made facing level; the camera 8 degrees up).
    See(s,at,w,h,1,mpp,8,vp,eye,&v);
    const float h2d=32.0f*0.42f,shown=v.y[2]-v.y[0];
    Check(shown>h2d*0.95f && shown<h2d*1.05f,"its 2D height square on at the reference",shown,h2d);
    // The camera walks round to 55 degrees on its right side: the same plate (its normal fixed), now in perspective.
    const float made0[3]={s.normal[0],s.normal[1],s.normal[2]};
    float eye2[3],vp2[16];
    Orbit(at,22.0f,25.0f+55.0f,4.0f,eye2,vp2,game);
    Frame f2{};FrameAt(vp2,1920.0f,1080.0f,at,&f2);
    Standee other{};Upright(f2,&other);
    s=FacingOf(cache,key,1400,other);
    Check(std::fabs(Dot(s.normal,made0)-1.0f)<1e-6f && std::fabs(Dot(other.normal,made0))<0.9f,"normal fixed as the camera turns");
    Check(See(s,at,w*3.0f,h,1,mpp,8,vp2,eye2,&v) && Reads(v),"reads at 55 degrees");
    // The edge nearer the camera the taller on the screen (the perspective, not an affine flattening).
    {
        float m[16],l[3],r[3];LayerMatrix(v.p,0,m);Local(m,0.0f,h*0.5f,l);Local(m,w*3.0f,h*0.5f,r);
        const float dl=DepthOf(vp2,l),dr=DepthOf(vp2,r),hl=v.y[2]-v.y[0],hr=v.y[3]-v.y[1];
        Check(std::fabs(dl-dr)>0.3f && ((dl<dr)==(hl>hr)),"the nearer edge taller",hl,hr);
        Check(std::fabs(hl/hr-dr/dl)<0.03f,"edge heights as 1 / depth",hl/hr,dr/dl);
    }
    Check(LayersBackward(v.p,w,h,8,vp2),"layers at 55 degrees: the face nearest");
    // From behind (160 degrees round): the face the side nearest the camera, reading left to right, the same span.
    float eye3[3],vp3[16];
    Orbit(at,25.0f,25.0f+160.0f,6.0f,eye3,vp3,game);
    s=FacingOf(cache,key,1800,other);
    Check(Behind(s,at,eye3),"behind it");
    Seen b;
    Check(See(s,at,w,h,0,mpp,8,vp3,eye3,&b) && b.p.back && Reads(b),"reads from behind",b.x[1]-b.x[0],b.y[2]-b.y[0]);
    Check(LayersBackward(b.p,w,h,8,vp3),"layers from behind: the face nearest");
    {   // The same span along the plate's right as seen from the front, the face at the back of the thickness.
        Plate front;Lay(s,at,w,h,0,mpp,8,false,&front);
        float mf[16],mb[16],f0[3],f1[3],b0[3],b1[3];
        LayerMatrix(front,0,mf);LayerMatrix(b.p,0,mb);
        Local(mf,0.0f,0.0f,f0);Local(mf,w,0.0f,f1);Local(mb,0.0f,0.0f,b0);Local(mb,w,0.0f,b1);
        const float sf0=Dot(f0,s.right),sf1=Dot(f1,s.right),sb0=Dot(b0,s.right),sb1=Dot(b1,s.right);
        const float thick=kThickness*h*mpp;
        Check(std::fabs(sb0-sf1)<1e-3f && std::fabs(sb1-sf0)<1e-3f,"from behind: the same span",sb0-sf1,sb1-sf0);
        Check(std::fabs((Dot(f0,s.normal)-Dot(b0,s.normal))-thick)<1e-4f,"from behind: the face at the back",Dot(f0,s.normal)-Dot(b0,s.normal),thick);
        float mlast[16],q[3];LayerMatrix(front,7,mlast);Local(mlast,0.0f,0.0f,q);
        Check(std::fabs((Dot(f0,s.normal)-Dot(q,s.normal))-thick)<1e-4f,"layers span the thickness",Dot(f0,s.normal)-Dot(q,s.normal),thick);
    }
    // Nearly edge-on: still the same plate, its layers' edges apart on the screen (its thickness shows).
    float eye4[3],vp4[16];
    Orbit(at,12.0f,25.0f+88.0f,3.0f,eye4,vp4,game);
    {
        Plate p;Lay(s,at,w,h,1,mpp,8,Behind(s,at,eye4),&p);
        float m0[16],m7[16],a0[3],a7[3],x0=0,y0=0,x7=0,y7=0;
        LayerMatrix(p,0,m0);LayerMatrix(p,7,m7);Local(m0,0.0f,0.0f,a0);Local(m7,0.0f,0.0f,a7);
        ToScreen(vp4,1920.0f,1080.0f,a0,&x0,&y0);ToScreen(vp4,1920.0f,1080.0f,a7,&x7,&y7);
        Check(std::fabs(x7-x0)>2.0f,"edge-on: the layers apart",x7-x0);
    }
}

void GroundChecks(bool game) {
    const float at[3]={120.0f,0.0f,-60.0f};
    const float w=150.0f*0.3125f,h=32.0f*0.3125f;
    // The map: straight down from 1500 m (north up the screen), and 60 degrees down looking north from 800 m.
    float eye[3]={at[0],1500.0f,at[2]},vp[16];
    const float down[3]={0.0f,-1.0f,0.0f};
    Camera(eye,down,0.0f,0.785398f,1920.0f,1080.0f,0.0f,0.0f,vp,game);
    Frame f{};
    Check(FrameAt(vp,1920.0f,1080.0f,at,&f),"map straight down: frame");
    const Standee g=Ground(f.hand);
    Check(g.down[2]==-1.0f && g.normal[1]==1.0f && std::fabs(g.right[0])==1.0f,"ground: down south, normal up");
    Check(g.right[0]==(game ? -1.0f : 1.0f),"ground: right east in the game's handedness (map_cam.h Right)",g.right[0]);
    const float mpp=SizeFactor(f.depth,1500.0f,Facing(f,g))*0.3f/0.3125f/f.pxY;
    Seen v;
    Check(See(g,at,w,h,0,mpp,6,vp,eye,&v) && Reads(v) && !v.p.back,"map straight down: reads, north up",v.x[1]-v.x[0],v.y[2]-v.y[0]);
    {   // Its top is north: the local up (-y) along +Z.
        float m[16],t[3],b[3];LayerMatrix(v.p,0,m);Local(m,0.0f,0.0f,t);Local(m,0.0f,h,b);
        Check(t[2]>b[2] && std::fabs(t[0]-b[0])<1e-5f && std::fabs(t[1]-b[1])<1e-5f,"its top north");
    }
    Check(LayersBackward(v.p,w,h,6,vp),"ground layers: the face nearest (on top)");
    Orbit(at,800.0f,0.0f,60.0f,eye,vp,game);
    FrameAt(vp,1920.0f,1080.0f,at,&f);
    Check(Ground(f.hand).right[0]==g.right[0],"the same ground plate from any map camera");
    Check(See(g,at,w,h,1,mpp,6,vp,eye,&v) && Reads(v),"map 60 degrees: reads");
    Check(std::fabs(Facing(f,g)-0.866f)<0.01f,"map 60 degrees: the plate's height foreshortened",Facing(f,g));
}

void SizeChecks() {
    // The size: metres fixed (the perspective) between the bounds, held at both ends of the height on the screen.
    Check(std::fabs(SizeFactor(kViewRef,kViewRef,1.0f)-1.0f)<1e-6f,"at the reference: the 2D size");
    // Its metres (the factor x the depth: the factor is against the 2D size in the plane facing the camera) fixed between
    // the depths where the perspective alone keeps its height within the bounds.
    for(float d=kViewRef/kNearMost*1.01f;d<kViewRef/kFarLeast*0.99f;d+=0.5f)
        Check(std::fabs(SizeFactor(d,kViewRef,1.0f)*d-kViewRef)<1e-3f,"between the bounds: its metres as at the reference",d);
    for(float facing=0.1f;facing<=1.0f;facing+=0.15f)for(float d=0.5f;d<8000.0f;d*=1.3f) {
        const float k=SizeFactor(d,kViewRef,facing);
        const float fa=facing<kLeastFacing ? kLeastFacing : facing;
        const float shown=k*fa;   // its height on the screen against the 2D one: the factor x the share showing
        Check(shown>=kFarLeast-1e-4f && shown<=kNearMost+1e-4f,"its height on the screen held",d,shown);
        // The metres (factor x depth) never shrink going further off: near large, far small on the screen, never smaller.
        Check(SizeFactor(d*1.3f,kViewRef,facing)*d*1.3f>=k*d-1e-3f,"its metres never shrink further off",d,k);
    }
    Check(SizeFactor(5000.0f,kViewRef,1.0f)==kFarLeast && SizeFactor(1.0f,kViewRef,1.0f)==kNearMost,"held at both ends");
    // The font scale: the nominal one on the grid whatever the depth; the rest in the matrix.
    const float step=1.0f/32.0f;
    for(float want=0.01f;want<3.0f;want+=0.0173f) {
        const Fit a=FontFor(want,0.85f,step),b=FontFor(want,1.8f,step),c=FontFor(want,4.0f,step);
        const float units=a.fd/step;
        Check(std::fabs(units-std::round(units))<1e-4f && a.fd>=step,"fd on the grid",want,a.fd);
        Check(a.fd==b.fd && b.fd==c.fd,"fd the same at any depth",a.fd,c.fd);
        Check(std::fabs(b.fd*b.unitPx-want*1.8f)<1e-5f,"fd x unitPx is the size wanted",want,b.fd*b.unitPx);
        if(want>=step)Check(std::fabs(a.fd-want)<=step*0.5f+1e-6f,"nearest grid point",want,a.fd);
    }
    Check(Snap(0.3f,step)==Snap(0.3001f,step) && Snap(0.0f,step)==step,"snap");
    // A plate square on far off and near: its height on the screen at the bounds of the 2D one.
    const float at[3]={0.0f,0.0f,0.0f};
    for(float d : {300.0f,2000.0f,5.0f}) {
        float eye[3],vp[16];Orbit(at,d,0.0f,0.0f,eye,vp,true);
        Frame f{};FrameAt(vp,1920.0f,1080.0f,at,&f);
        Standee s{};Upright(f,&s);
        const Fit fit=FontFor(0.42f,SizeFactor(f.depth,kViewRef,Facing(f,s)),step);
        Seen v;
        See(s,at,180.0f*fit.fd,32.0f*fit.fd,1,fit.unitPx/f.pxY,LayersFor(f.depth,1),vp,eye,&v);
        const float k=(v.y[2]-v.y[0])/(32.0f*0.42f);
        Check(k>=kFarLeast*0.99f && k<=kNearMost*1.01f,"square on: its height on the screen held",d,k);
    }
    // Layers: fewer far off and with many labels, never fewer than the least; their colours darker and fainter going back.
    int last=kLayersMost+1;
    for(float d=1.0f;d<4000.0f;d*=1.5f){const int n=LayersFor(d,1);Check(n<=last && n>=kLayersLeast,"layers fewer far off",d,n);last=n;}
    last=kLayersMost+1;
    for(int labels=1;labels<300;labels+=7){const int n=LayersFor(10.0f,labels);Check(n<=last && n>=kLayersLeast,"layers fewer with many",labels,n);last=n;}
    Check(LayersFor(10.0f,1)==kLayersMost,"near and alone: the most layers");
    const float cyan[4]={0.3f,0.9f,1.0f,0.9f};
    float prev[4],c[4];
    LayerColour(cyan,0,8,prev);
    Check(prev[0]==cyan[0] && prev[3]==cyan[3],"the face its own colour");
    for(int k=1;k<8;++k) {
        LayerColour(cyan,k,8,c);
        Check(c[1]<prev[1] && c[3]<prev[3] && c[3]>0.0f,"layers darker and fainter going back",k,c[3]);
        for(int i=0;i<4;++i)prev[i]=c[i];
    }
}

void CacheChecks() {
    Standee a{},b{};
    a.normal[0]=1.0f;b.normal[2]=1.0f;
    Facings c{};
    // Kept while asked for; made anew after kForgetMs away.
    Check(FacingOf(c,Key(1,7),100,a).normal[0]==1.0f,"cache: made");
    Check(FacingOf(c,Key(1,7),100+kForgetMs,b).normal[0]==1.0f,"cache: kept while asked for");
    Check(FacingOf(c,Key(1,7),100+kForgetMs*2+1,b).normal[2]==1.0f,"cache: made anew after long away");
    Check(FacingsKept(c,100+kForgetMs*2+1)==1 && FacingsKept(c,100+kForgetMs*3+2)==0,"cache: forgotten");
    // At most kFacingSlots: one more given the slot asked for longest ago; the newer ones kept.
    Facings d{};
    for(int i=0;i<kFacingSlots;++i)FacingOf(d,Key(2,static_cast<std::uint64_t>(i)),1000+static_cast<unsigned long long>(i),a);
    Check(FacingsKept(d,1000+kFacingSlots)==kFacingSlots,"cache: full");
    FacingOf(d,Key(2,9999),2000,b);
    Check(FacingsKept(d,2000)==kFacingSlots,"cache: never more than its slots");
    Check(FacingOf(d,Key(2,1),2001,b).normal[0]==1.0f,"cache: a newer one kept");
    Check(FacingOf(d,Key(2,0),2002,b).normal[2]==1.0f,"cache: the oldest given up");
    // Keys: apart by kind and by what; a place's cell steady within it.
    const float p[3]={10.2f,1.0f,-33.7f},q[3]={11.9f,1.5f,-32.1f},r[3]={14.1f,1.0f,-33.7f};
    Check(Key(1,5)!=Key(2,5) && Key(1,5)!=Key(1,6) && Key(1,0)!=0,"keys apart");
    Check(Cell(p,4.0f)==Cell(q,4.0f) && Cell(p,4.0f)!=Cell(r,4.0f),"a place's cell");
}
}  // namespace

int main() {
    const float w=1920.0f,h=1080.0f,fov=3.14159265f/4.0f;
    float vp[16];
    for(int game=0;game<2;++game) {
        const bool g=game!=0;
        // The frame through odd cameras: off-centre (TAA jitter / a split screen), rolled, zoomed, the map's.
        const float eye[3]={10.0f,3.0f,-20.0f},look[3]={0.3f,-0.1f,1.0f},atRef[3]={18.0f,1.0f,8.0f},far1[3]={160.0f,-20.0f,480.0f};
        Camera(eye,look,0.0f,fov,w,h,0.0f,0.0f,vp,g);FrameChecks("view",vp,w,h,atRef,eye);FrameChecks("view far",vp,w,h,far1,eye);
        Camera(eye,look,0.0f,fov,w,h*0.5f,0.0007f,-0.0004f,vp,g);FrameChecks("jittered",vp,w,h*0.5f,atRef,eye);
        Camera(eye,look,0.35f,fov,w,h,0.0f,0.0f,vp,g);FrameChecks("rolled",vp,w,h,atRef,eye);
        Camera(eye,look,0.0f,fov/6.0f,w,h,0.0f,0.0f,vp,g);FrameChecks("zoomed",vp,w,h,far1,eye);
        UprightChecks(g);
        GroundChecks(g);
        // Upright from straight above (the map looking down): still a plate, facing the screen's bottom.
        const float top[3]={0.0f,1500.0f,0.0f},down[3]={0.0f,-1.0f,0.0f},ground1[3]={60.0f,0.0f,40.0f};
        Camera(top,down,0.0f,fov,w,h,0.0f,0.0f,vp,g);
        Frame f{};Standee s{};
        Check(FrameAt(vp,w,h,ground1,&f) && Upright(f,&s) && std::fabs(s.normal[1])<1e-6f,"upright from straight above");
    }
    SizeChecks();
    CacheChecks();
    // Refused: behind the camera, a viewProj without perspective, none.
    const float eye[3]={10.0f,3.0f,-20.0f},look[3]={0.3f,-0.1f,1.0f},atRef[3]={18.0f,1.0f,8.0f};
    Camera(eye,look,0.0f,fov,w,h,0.0f,0.0f,vp);
    Frame f{};
    const float behind[3]={eye[0]-0.3f*10.0f,eye[1],eye[2]-10.0f};
    Check(!FrameAt(vp,w,h,behind,&f),"behind the camera");
    float ortho[16]={2.0f/w,0.0f,0.0f,0.0f, 0.0f,-2.0f/h,0.0f,0.0f, 0.0f,0.0f,1.0f,0.0f, -1.0f,1.0f,0.0f,1.0f};
    float e[3];
    Check(!FrameAt(ortho,w,h,atRef,&f) && !EyeOf(ortho,e),"an orthographic projection (the HUD's own)");
    const float zero[16]{};
    Check(!FrameAt(zero,w,h,atRef,&f) && !EyeOf(zero,e),"no viewProj");
    // The glow and the shimmer.
    const float amber[4]={1.0f,0.7f,0.15f,0.95f};
    float g[4];Glow(amber,g);
    Check(g[0]==1.0f && g[1]>amber[1] && g[2]>amber[2] && std::fabs(g[3]-amber[3]*kGlowAlpha)<1e-6f,"glow: lighter, fainter");
    float least=2.0f,most=-1.0f;
    for(unsigned long long ms=0;ms<3000;ms+=7){const float a=Shimmer(ms,0.3f);least=a<least ? a : least;most=a>most ? a : most;}
    Check(least>=1.0f-kShimmer-1e-4f && most<=1.0f+1e-6f && most-least>kShimmer*0.9f,"shimmer range",least,most);
    const float p0=PhaseOf(atRef);
    Check(p0>=0.0f && p0<1.0f,"phase",p0);

    std::printf(failures ? "world text: %d of %d FAILED\n" : "world text: %d checks, all good\n",failures ? failures : cases,cases);
    return failures ? 1 : 0;
}
