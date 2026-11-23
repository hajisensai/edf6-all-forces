// src/preview_camera.h: the preview's placement in a design rect and its camera presets.
#include "../src/preview_camera.h"
#include <cstdio>
#include <cstdint>
#include <initializer_list>

using namespace crew::roompreview;

static int checks=0,failed=0;
static void Check(bool ok,const char* what) {
    ++checks;
    if(!ok){++failed;std::printf("FAIL %s\n",what);}
}
static bool Near(float a,float b,float eps=1e-3f) { return std::fabs(a-b)<eps; }

static void Ndcs() {
    const Ndc full=NdcOf({0,0,kDesignW,kDesignH});
    Check(Near(full.x,0) && Near(full.y,0),"the whole screen's centre is NDC 0,0");
    const Ndc right=NdcOf({1440,0,480,1080});
    Check(Near(right.x,0.75f) && Near(right.y,0),"the right quarter's centre is x 0.75");
    const Ndc top=NdcOf({0,0,1920,540});
    Check(Near(top.y,0.5f),"the top half's centre is y 0.5 (y up)");
}

// The stock placement checks the projection: MENUMODELSETTING camera_pos[0] = (1.1, -0.9, -3.5) with fov 0.6 at 16:9
// lands at NDC x ~0.57 (the soldier right of the HQ menu, as the game shows it).
static void StockProjection() {
    const float p[3]={1.1f,-0.9f,-3.5f};
    const Ndc n=Project(p,16.f/9.f);
    Check(Near(n.x,0.572f,0.005f),"camera_pos[0] projects to x ~0.57");
    Check(n.y<0 && n.y>-0.9f,"camera_pos[0] is below the centre (the feet)");
}

static void Placement() {
    const float aspect=16.f/9.f;
    const ViewRect rects[]={{1200,120,600,840},{0,0,1920,1080},{100,600,300,300},{800,100,200,900}};
    const ModelFrame frames[]={{1.8f,0.9f},{7.f,1.4f},{18.f,2.f}};
    for(const ViewRect& r:rects)for(const ModelFrame& f:frames)for(int p=0;p<=kCameraPresetCount;++p)for(float stand:{0.f,1.f,-2.5f}) {
        const ViewCamera flat{0.3f,0,0.8f,0,true};
        const ViewCamera& c=p<kCameraPresetCount ? kCameraPresets[p] : flat;
        const Matrix4 m=Place(r,c,f,stand,aspect);
        Check(reinterpret_cast<std::uintptr_t>(&m)%16==0,"the matrix is 16-byte aligned (movaps)");
        // The aimed point (the pivot) lands on the rect's centre.
        const float pivot[3]={0,f.centreY+c.lift*f.size,0};
        float s[2];
        ScreenOf(m,pivot,aspect,s);
        Check(Near(s[0],r.x+r.w*0.5f,0.05f) && Near(s[1],r.y+r.h*0.5f,0.05f),"the pivot projects to the rect's centre");
        // Rotation is orthonormal (no scale: the model is not distorted).
        for(int i=0;i<3;++i)for(int j=0;j<3;++j) {
            float d=0;
            for(int k=0;k<3;++k)d+=m.m[i][k]*m.m[j][k];
            Check(Near(d,i==j ? 1.f : 0.f),"the rotation is orthonormal");
        }
        Check(m.m[0][3]==0 && m.m[1][3]==0 && m.m[2][3]==0 && m.m[3][3]==1,"the last column is (0,0,0,1)");
        Check(m.m[3][2]<0,"the model stands in front of the camera (-Z)");
        // The model's height fills camera.distance^-1 of the rect at pitch 0 (the vertical extent at the centre's depth).
        if(c.pitch==0.f && c.lift==0.f) {
            float top[3]={0,f.centreY+f.size*0.5f,0},bottom[3]={0,f.centreY-f.size*0.5f,0},a[2],b[2];
            ScreenOf(m,top,aspect,a);ScreenOf(m,bottom,aspect,b);
            Check(Near((b[1]-a[1])*c.distance,r.h,0.5f),"the model's size fills the rect's height / distance");
        }
    }
}

static void Presets() {
    Check(PresetOf(CameraPreset::free).followStand,"自由 follows the stand's yaw");
    for(CameraPreset p:{CameraPreset::front,CameraPreset::side,CameraPreset::top,CameraPreset::closeUp})
        Check(!PresetOf(p).followStand,"the fixed presets are absolute");
    Check(Near(PresetOf(CameraPreset::side).yaw-PresetOf(CameraPreset::front).yaw,1.5707963f),"侧面 is 正面 turned 90 degrees");
    Check(PresetOf(CameraPreset::top).pitch>1.f,"俯视 looks down");
    Check(PresetOf(CameraPreset::closeUp).distance<PresetOf(CameraPreset::front).distance && PresetOf(CameraPreset::closeUp).lift>0,
          "特写 is closer and aims above the centre");
    // Following the stand: the stand's yaw turns the model as the game's own matrix does (row0 (c,0,-s), row2 (s,0,c)).
    const ViewCamera flat{0,0,1,0,true};
    const Matrix4 m=Place({0,0,1920,1080},flat,{},0.7f,16.f/9.f);
    Check(Near(m.m[0][0],std::cos(0.7f)) && Near(m.m[0][2],-std::sin(0.7f)) && Near(m.m[2][0],std::sin(0.7f)),
          "pitch 0 is the game's own yaw matrix");
    // Pitch > 0 brings the model's top toward the camera (+Z): seen from above.
    const ViewCamera down{0,0.5f,1,0,false};
    const Matrix4 t=Place({0,0,1920,1080},down,{},0,16.f/9.f);
    Check(t.m[1][2]>0,"pitch > 0 tilts the top toward the camera");
}

int main() {
    Ndcs();
    StockProjection();
    Placement();
    Presets();
    std::printf("preview_camera_test: %d/%d checks passed\n",checks-failed,checks);
    return failed ? 1 : 0;
}
