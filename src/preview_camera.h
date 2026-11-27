// Where the room / HQ 3D preview stands on screen and how it is looked at (docs/room-3d-preview-re.md section 6.2). Pure.
//
// The view's camera never moves: its Draw 0x93C370 writes an identity camera matrix and the fov (+0x260, MENUMODELSETTING
// camera_fov 0.6, vertical) every frame, the Ui3DSpace is the whole screen (no viewport rect), the camera looks down -Z
// (H). Everything is the model's world matrix, built by the view's update at 0x93D073 from its yaw +0x2A4 and translation
// +0x2B0 and handed to SetWorldMatrix 0x1100B90 at 0x93D1A1 (row vectors: v' = v * M). So a placement is a model matrix:
// the model turned about its centre by the camera's yaw / pitch, then moved so its centre projects onto the rect's centre
// at a distance where its size fits the rect's height.
#pragma once
#include <cmath>

namespace crew::roompreview {

inline constexpr float kDesignW=1920.f,kDesignH=1080.f;   // the UI's design units (the whole screen)
inline constexpr float kViewFov=0.6f;                       // MENUMODELSETTING camera_fov (vertical, radians)

// A rect in design units (top-left origin, y down).
struct ViewRect { float x=0,y=0,w=0,h=0; };

// How the model is looked at. yaw / pitch in radians (pitch > 0: seen from above); distance a multiple of the distance
// at which the model's size exactly fills the rect's height (1 fits, < 1 closer); lift the aimed point above the model's
// centre as a fraction of its size (a close-up aims at the chest). followStand: the stand's own yaw (+0x2A4, the
// player's turn input) is added to yaw (free); otherwise yaw is absolute.
struct ViewCamera { float yaw=0,pitch=0,distance=1.f,lift=0; bool followStand=true; };

enum class CameraPreset { free,front,side,top,closeUp };
inline constexpr int kCameraPresetCount=5;
inline constexpr ViewCamera kCameraPresets[kCameraPresetCount]={
    {0.f,0.15f,1.10f,0.f,true},                 // 自由: the stand turns with the player's input
    {0.f,0.10f,1.10f,0.f,false},                // 正面
    {1.5707963f,0.10f,1.10f,0.f,false},         // 侧面
    {0.f,1.2f,1.15f,0.f,false},                 // 俯视 (not straight down: the model stays readable)
    {0.f,0.05f,0.50f,0.30f,false},              // 特写 (upper body)
};
constexpr const ViewCamera& PresetOf(CameraPreset p) noexcept { return kCameraPresets[static_cast<int>(p)]; }

// The model: its size (soldier ~1.8 m; a vehicle its length) and its centre's height above the model origin (the feet).
struct ModelFrame { float size=1.8f,centreY=0.9f; };

// 16-byte aligned: SetWorldMatrix copies it with movaps (0x1100B90, H).
struct alignas(16) Matrix4 { float m[4][4]{}; };

// 自由 turned by hand (the viewport dragged, the wheel over it, the right stick): the free preset until it is first
// moved, then an absolute camera (followStand off: the stand's own turn, a vehicle's turntable, no longer moves it) that
// starts where the free one looked (`standYaw`: the yaw the view drew the model with, YawOf) so nothing jumps. Kept
// while another preset is chosen: back on 自由, the view the player left.
inline constexpr float kLookPitchMin=-0.35f,kLookPitchMax=1.35f;      // from a little below to almost straight down
inline constexpr float kLookDistanceMin=0.45f,kLookDistanceMax=2.2f;
inline constexpr float kLookRadPerUnit=0.008f;   // a drag: radians a design unit (the viewport's width ~ 5 radians)
inline constexpr float kLookStickRad=2.4f;       // the stick full over: radians a second
inline constexpr float kLookZoomStep=0.88f;      // a wheel notch away from the user: this much closer
struct FreeLook {
    float yaw=0.f,pitch=0.f,distance=1.f;
    bool moved=false;
};
// The yaw of a model matrix Place built (its column 0 is (cos yaw, 0, sin yaw): Rx leaves it as Ry made it).
inline float YawOf(const Matrix4& m) noexcept { return std::atan2(m.m[2][0],m.m[0][0]); }
inline ViewCamera CameraOf(const FreeLook& l) noexcept {
    const ViewCamera& free=kCameraPresets[0];
    return l.moved ? ViewCamera{l.yaw,l.pitch,l.distance,free.lift,false} : free;
}
namespace detail {
inline void StartLook(FreeLook* l,float standYaw) noexcept {
    if(l->moved)return;
    const ViewCamera& free=kCameraPresets[0];
    l->yaw=standYaw+free.yaw;l->pitch=free.pitch;l->distance=free.distance;l->moved=true;
}
inline float WrapAngle(float a) noexcept {
    constexpr float kPi=3.14159265f;
    while(a>kPi)a-=2*kPi;
    while(a<-kPi)a+=2*kPi;
    return a;
}
}  // namespace detail
// Turned by `dYaw` / `dPitch` radians (pitch > 0: more from above), the pitch held in its range.
inline void Turn(FreeLook* l,float dYaw,float dPitch,float standYaw) noexcept {
    if(!l || (dYaw==0.f && dPitch==0.f))return;
    detail::StartLook(l,standYaw);
    l->yaw=detail::WrapAngle(l->yaw+dYaw);
    const float p=l->pitch+dPitch;
    l->pitch=p<kLookPitchMin ? kLookPitchMin : p>kLookPitchMax ? kLookPitchMax : p;
}
// `notches` of the wheel (+ away from the user: closer), the distance held in its range.
inline void Zoom(FreeLook* l,int notches,float standYaw) noexcept {
    if(!l || !notches)return;
    detail::StartLook(l,standYaw);
    float d=l->distance;
    for(int i=0;i<notches && i<20;++i)d*=kLookZoomStep;
    for(int i=0;i<-notches && i<20;++i)d/=kLookZoomStep;
    l->distance=d<kLookDistanceMin ? kLookDistanceMin : d>kLookDistanceMax ? kLookDistanceMax : d;
}

// The rect's centre in NDC (x right, y up) on a screen of the given aspect (width / height). The design space is
// stretched over the whole screen, as the UI is.
struct Ndc { float x=0,y=0; };
inline Ndc NdcOf(const ViewRect& r) noexcept {
    return {(r.x+r.w*0.5f)/kDesignW*2.f-1.f,1.f-(r.y+r.h*0.5f)/kDesignH*2.f};
}

// Projects a view-space point (camera at the origin looking down -Z) to NDC; the inverse of Place's translation.
inline Ndc Project(const float p[3],float aspect,float fov=kViewFov) noexcept {
    const float t=std::tan(fov*0.5f),d=-p[2];
    return {p[0]/(d*t*aspect),p[1]/(d*t)};
}

// The distance from the camera at which a model of `size` fills the rect's height, times camera.distance.
inline float DistanceOf(const ViewRect& r,const ViewCamera& c,const ModelFrame& f,float fov=kViewFov) noexcept {
    const float frac=r.h>0 ? r.h/kDesignH : 1.f;
    return f.size/(frac*2.f*std::tan(fov*0.5f))*c.distance;
}

// The model matrix: v' = (v - pivot) * Ry(yaw) * Rx(pitch) + centre, pivot = (0, centreY + lift*size, 0), centre on the
// ray through the rect's centre at DistanceOf. Ry as the game builds it (row0 (c,0,-s), row2 (s,0,c), H); Rx tilts the
// model's top toward the camera for pitch > 0. standYaw: the view's +0x2A4 (used when followStand).
inline Matrix4 Place(const ViewRect& r,const ViewCamera& c,const ModelFrame& f,float standYaw,float aspect,
                     float fov=kViewFov) noexcept {
    const float yaw=c.followStand ? standYaw+c.yaw : c.yaw;
    const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(c.pitch),sp=std::sin(c.pitch);
    // R = Ry * Rx (row vectors).
    const float ry[3][3]={{cy,0,-sy},{0,1,0},{sy,0,cy}};
    const float rx[3][3]={{1,0,0},{0,cp,sp},{0,-sp,cp}};
    Matrix4 out;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){float s=0;for(int k=0;k<3;++k)s+=ry[i][k]*rx[k][j];out.m[i][j]=s;}
    const Ndc n=NdcOf(r);
    const float d=DistanceOf(r,c,f,fov),t=std::tan(fov*0.5f);
    const float centre[3]={n.x*d*t*aspect,n.y*d*t,-d};
    const float pivotY=f.centreY+c.lift*f.size;
    for(int j=0;j<3;++j)out.m[3][j]=centre[j]-pivotY*out.m[1][j];
    out.m[3][3]=1.f;
    return out;
}

// Where a model-space point lands on screen in design units (the squad ring / labels; the inverse of NdcOf).
inline void ScreenOf(const Matrix4& m,const float p[3],float aspect,float out[2],float fov=kViewFov) noexcept {
    float v[3];
    for(int j=0;j<3;++j)v[j]=p[0]*m.m[0][j]+p[1]*m.m[1][j]+p[2]*m.m[2][j]+m.m[3][j];
    const Ndc n=Project(v,aspect,fov);
    out[0]=(n.x+1.f)*0.5f*kDesignW;
    out[1]=(1.f-n.y)*0.5f*kDesignH;
}

// --- The squad side by side (docs/room-3d-preview-re.md section 6.3): soldier i of n stands in the i-th of n equal
// columns of the viewport, each placed by Place as one soldier in its own rect (the same camera, the same size: a
// soldier fills its column's height). The page draws each one's floor ring at FeetOf of the same rect, so the ring and
// the model meet by construction (the feet do not move with the yaw: Place's row 1 has no yaw in it).
inline constexpr int kSquadMost=4;   // the views at once: the game's own index-0 view and three the preview makes
inline ViewRect SquadRectOf(const ViewRect& r,int n,int i) noexcept {
    if(n<1)n=1;
    if(n>kSquadMost)n=kSquadMost;
    if(i<0 || i>=n)return ViewRect{};
    const float w=r.w/static_cast<float>(n);
    return ViewRect{r.x+w*static_cast<float>(i),r.y,w,r.h};
}
// Where a soldier placed in `r` has its feet on screen (design units).
inline void FeetOf(const ViewRect& r,const ViewCamera& c,float aspect,float out[2]) noexcept {
    const Matrix4 m=Place(r,c,ModelFrame{},0.f,aspect);
    const float origin[3]={0,0,0};
    ScreenOf(m,origin,aspect,out);
}
// The soldier column under (x, y) (-1: outside the viewport).
inline int SquadSlotAt(const ViewRect& r,int n,float x,float y) noexcept {
    if(n<1 || r.w<=0 || x<r.x || x>=r.x+r.w || y<r.y || y>=r.y+r.h)return -1;
    if(n>kSquadMost)n=kSquadMost;
    const int i=static_cast<int>((x-r.x)/(r.w/static_cast<float>(n)));
    return i<n ? i : n-1;
}

}  // namespace crew::roompreview
