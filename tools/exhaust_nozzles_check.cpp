// The nozzles read off a model (src/exhaust_nozzles.h) offline, on stand-in bone records laid out as the game's
// (0x110 bytes each: name, parent, local, world, half extents):
//  - a model's own bones nozzle_0, nozzle_1 are found by name, each with its parent's index, its local matrix and its
//    flame (the half extents); a set ends at the first missing number; a nonsense parent or flame ends it there;
//  - a model without them that is a stock model (kStockNozzles: its own bone and bone count) gets the stock table's, on
//    the bone they hang on; the plugin's multirole (a `bomber401` bone too, more bones) without nozzle bones gets none;
//  - NozzleWorld puts a nozzle where the game will draw it this frame: a carrier flown frame by frame (the records posed
//    the frame before, the pod tilted in the input step) against the nozzle's true pose (local x pod's local now x the
//    body now), turning, climbing and tilting its pods; without the pod's local of the frame before, the flame is off by
//    the tilt of one frame (checked to be so, so the check sees what it guards).
// cmake --build build --target exhaust_nozzles_check && build\exhaust_nozzles_check.exe   (exit 1 on a failure)
#include "../src/exhaust_nozzles.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
namespace ex=crew::exhaust;
int failures=0;
void Expect(bool ok,const char* what) {
    std::printf("%s %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}

struct Bone { const wchar_t* name; int parent; float local[16]; float half[2]; };
const float kIdent[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};

// The records of `bones`, their world matrices posed from their locals on `root` (row vectors: local x parent's world).
std::vector<unsigned char> Records(const std::vector<Bone>& bones,const float* root) {
    std::vector<unsigned char> out(bones.size()*ex::kRecStride,0);
    std::vector<float> world(bones.size()*16);
    for(std::size_t i=0;i<bones.size();++i) {
        unsigned char* r=out.data()+i*ex::kRecStride;
        const wchar_t* n=bones[i].name;
        std::memcpy(r+ex::kRecName,&n,sizeof n);
        const std::int32_t p=bones[i].parent;
        std::memcpy(r+ex::kRecParent,&p,4);
        std::memcpy(r+ex::kRecLocal,bones[i].local,64);
        std::memcpy(r+ex::kRecHalf,bones[i].half,8);
        ex::Mul(bones[i].local,p<0 ? root : &world[static_cast<std::size_t>(p)*16],&world[i*16]);
        std::memcpy(r+ex::kRecWorld,&world[i*16],64);
    }
    return out;
}
bool Readable(const wchar_t* n) { return n!=nullptr; }

void Translated(float* m,float x,float y,float z) { std::memcpy(m,kIdent,64);m[12]=x;m[13]=y;m[14]=z; }
// The model's frame turned pi about y at (x, y, z): a flame leaving along the parent's -z.
void Turned(float* m,float x,float y,float z) { Translated(m,x,y,z);m[0]=-1.0f;m[10]=-1.0f; }
// Rx(a) x bind (body506.cpp HingePose): the pod tilted `a` about its local x.
void Hinge(const float* bind,float a,float* out) {
    const float c=std::cos(a),s=std::sin(a);
    for(int x=0;x<4;++x){out[x]=bind[x];out[4+x]=c*bind[4+x]+s*bind[8+x];out[8+x]=-s*bind[4+x]+c*bind[8+x];out[12+x]=bind[12+x];}
}
float Dist(const float* a,const float* b) {
    return std::sqrt((a[12]-b[12])*(a[12]-b[12])+(a[13]-b[13])*(a[13]-b[13])+(a[14]-b[14])*(a[14]-b[14]));
}
float AxisOff(const float* a,const float* b) {   // 1 - cos between the two +z rows
    return 1.0f-(a[8]*b[8]+a[9]*b[9]+a[10]*b[10]);
}

void ModelBones() {
    std::vector<Bone> b(5);
    b[0]={L"mdl",-1,{},{0,0}};std::memcpy(b[0].local,kIdent,64);
    b[1]={L"body",0,{},{0,0}};std::memcpy(b[1].local,kIdent,64);
    b[2]={L"boosterF_l",1,{},{0,0}};Translated(b[2].local,21.7f,9.06f,4.23f);
    b[3]={L"nozzle_1",1,{},{9.2f,1.84f}};Turned(b[3].local,-3.58f,2.33f,-12.0f);
    b[4]={L"nozzle_0",2,{},{56.0f,16.0f}};Turned(b[4].local,3.44f,-0.064f,-9.52f);
    const auto r=Records(b,kIdent);
    const ex::NozzleSet s=ex::FindNozzles(r.data(),5,Readable);
    Expect(s.count==2 && !s.stock,"a model's nozzle_0 and nozzle_1 are found, in number order");
    Expect(s.parent[0]==2 && s.parent[1]==1,"each on the bone it hangs on (nozzle_0 on the pod, nozzle_1 on the body)");
    Expect(std::memcmp(s.local[0],b[4].local,64)==0 && std::memcmp(s.local[1],b[3].local,64)==0,"each its own local matrix");
    Expect(s.size[0][0]==56.0f && s.size[0][1]==16.0f && s.size[1][0]==9.2f && s.size[1][1]==1.84f,
           "each its flame's length and width from its half extents");

    auto gap=b;gap[3].name=L"nozzle_2";   // nozzle_0, nozzle_2: the set ends at the missing 1
    Expect(ex::FindNozzles(Records(gap,kIdent).data(),5,Readable).count==1,"a set ends at the first missing number");
    auto flat=b;flat[3].half[1]=0.0f;     // nozzle_1 with no width
    Expect(ex::FindNozzles(Records(flat,kIdent).data(),5,Readable).count==1,"a nozzle with no flame ends the set there");
    auto longer=b;longer[4].name=L"nozzle_0b";
    Expect(ex::FindNozzles(Records(longer,kIdent).data(),5,Readable).count==0,"only the exact name is a nozzle");
}

void StockModels() {
    std::vector<Bone> stock(2);
    stock[0]={L"mdl",-1,{},{0,0}};std::memcpy(stock[0].local,kIdent,64);
    stock[1]={L"bomber401",0,{},{0,0}};std::memcpy(stock[1].local,kIdent,64);
    const auto r=Records(stock,kIdent);
    const ex::NozzleSet s=ex::FindNozzles(r.data(),2,Readable);
    const ex::StockNozzles* want=nullptr;
    for(const auto& m:ex::kStockNozzles)if(std::wcscmp(m.model,L"bomber401")==0)want=&m;
    Expect(want!=nullptr,"nozzles_gen.h has the stock bomber401");
    Expect(want && s.stock && s.count==want->count && s.parent[0]==1 && std::memcmp(s.local[0],want->local[0],64)==0 &&
           s.size[1][0]==want->size[1][0],"the stock bomber401 (2 bones) gets its table's nozzles on its bomber401 bone");

    std::vector<Bone> multirole(5);   // the plugin's multirole: bomber401 skinned, its gear, no nozzle bones (an old install)
    const wchar_t* names[5]={L"mdl",L"bomber401",L"gear_nose",L"gear_main_l",L"gear_main_r"};
    for(int i=0;i<5;++i){multirole[i]={names[i],i==0 ? -1 : (i==1 ? 0 : 1),{},{0,0}};std::memcpy(multirole[i].local,kIdent,64);}
    Expect(ex::FindNozzles(Records(multirole,kIdent).data(),5,Readable).count==0,
           "a model with a bomber401 bone but other bones is not the stock bomber401: no table nozzles");

    std::vector<Bone> s5012(3);
    s5012[0]={L"mdl",-1,{},{0,0}};s5012[1]={L"bomber501_2",0,{},{0,0}};s5012[2]={L"bomber501",1,{},{0,0}};
    for(auto& x:s5012)std::memcpy(x.local,kIdent,64);
    const ex::NozzleSet t=ex::FindNozzles(Records(s5012,kIdent).data(),3,Readable);
    Expect(t.stock && t.count==2 && t.parent[0]==2,"the stock bomber501_2 gets its nozzles on bomber501 (the bone under its node)");
}

// The body (row vectors) at time `s`: flying `speed` m/s along its nose, turning `yaw` rad/s, climbing `pitch` rad/s.
void Body(float s,float speed,float yaw,float pitch,float* m) {
    const float y=yaw*s,p=pitch*s,cy=std::cos(y),sy=std::sin(y),cp=std::cos(p),sp=std::sin(p);
    const float rx[16]={1,0,0,0, 0,cp,sp,0, 0,-sp,cp,0, 0,0,0,1};
    const float ry[16]={cy,0,-sy,0, 0,1,0,0, sy,0,cy,0, 0,0,0,1};
    ex::Mul(rx,ry,m);
    m[12]=speed*s*m[8];m[13]=300.0f+speed*s*m[9];m[14]=speed*s*m[10];
}

// A carrier flown frame by frame in the game's order: the input step tilts the pod (its local now) and places the
// flame from the records (posed the frame before); the game then poses the records from the body now and draws them.
void Carrier(float speed,float yaw,float pitch,float tiltRate,float* worst,float* worstOld) {
    float bodyLocal[16],podBind[16],nozzle[16];
    std::memcpy(bodyLocal,kIdent,64);
    Translated(podBind,21.7f,9.06f,4.23f);
    Turned(nozzle,3.44f,-0.064f,-9.52f);
    const float dt=1.0f/60.0f;
    ex::BodyTrack track;
    float podWas[16],podNow[16],bodyWas[16],podWorldRec[16];
    Body(0.0f,speed,yaw,pitch,bodyWas);
    Hinge(podBind,0.0f,podWas);
    {float w[16];ex::Mul(bodyLocal,bodyWas,w);ex::Mul(podWas,w,podWorldRec);}
    ex::Observe(track,1,bodyWas);
    *worst=0.0f;*worstOld=0.0f;
    for(int f=2;f<600;++f) {
        float body[16];
        Body((f-1)*dt,speed,yaw,pitch,body);                          // the physics moved it since the records were posed
        Hinge(podBind,-std::fmin(1.9f,tiltRate*f*dt),podNow);         // the input step tilts the pod (Thrusters)
        ex::Observe(track,static_cast<std::uint64_t>(f),body);
        float got[16],old[16];
        ex::NozzleWorld(nozzle,podNow,podWas,podWorldRec,track,got);
        ex::NozzleWorld(nozzle,podNow,nullptr,podWorldRec,track,old);  // the record's tilt (the parent's local not followed)
        float truth[16],podWorld[16];
        ex::Mul(podNow,body,podWorld);
        ex::Mul(nozzle,podWorld,truth);                                // drawn this frame
        *worst=std::fmax(*worst,std::fmax(Dist(got,truth),AxisOff(got,truth)*100.0f));
        *worstOld=std::fmax(*worstOld,Dist(old,truth));
        std::memcpy(podWorldRec,podWorld,64);                          // the game poses the records now
        std::memcpy(podWas,podNow,64);
    }
}

void Placement() {
    struct Case { const char* what; float speed,yaw,pitch,tilt; };
    const Case cases[]={{"hovering, tilting its pods",0.0f,0.0f,0.0f,1.0f},{"cruising at 120 m/s, pods tilting",120.0f,0.0f,0.0f,1.0f},
                        {"turning 20 deg/s and climbing 5 deg/s at 120 m/s, pods tilting",120.0f,0.35f,0.087f,1.0f},
                        {"turning at 245 m/s, pods still",245.0f,0.35f,0.0f,0.0f}};
    for(const auto& c:cases) {
        float worst,old;
        Carrier(c.speed,c.yaw,c.pitch,c.tilt,&worst,&old);
        char line[200];
        std::snprintf(line,sizeof line,"the flame on its nozzle as drawn, %s (worst %.4f m; the record's tilt alone %.3f m)",c.what,worst,old);
        Expect(worst<1e-3f,line);
        if(c.tilt>0.0f) {
            std::snprintf(line,sizeof line,"...and without the pod's tilt of this frame it would be off (%.3f m > 0.1 m)",old);
            Expect(old>0.1f,line);
        }
    }
}
}  // namespace

int main() {
    ModelBones();
    StockModels();
    Placement();
    std::printf("%s\n",failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
