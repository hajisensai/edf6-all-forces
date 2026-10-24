// Invert the actual native player-control movement axes. The engine consumes
// body-local movement: Walking MoveTo first rotates goal-position by -yaw
// (33E48E..33E4E7), then the native step rotates it back (53D893..53D89C).
#include "enemy_control_native.h"
#include "crew.h"
#include "memory.h"
#include <cmath>

namespace crew::enemy_native {
namespace {
float DotMove(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
bool UnitMove(float* v) noexcept {
    const float n=DotMove(v,v);
    if(!std::isfinite(n) || n<1e-8f)return false;
    const float f=1.0f/std::sqrt(n);for(int i=0;i<3;++i)v[i]*=f;
    return true;
}
void CrossMove(const float* a,const float* b,float* out) noexcept {
    out[0]=a[1]*b[2]-a[2]*b[1];out[1]=a[2]*b[0]-a[0]*b[2];out[2]=a[0]*b[1]-a[1]*b[0];
}
float LimitMove(float x) noexcept { return x<-1.0f ? -1.0f : x>1.0f ? 1.0f : x; }
bool BasisMove(const NativeLease& lease,float* right,float* up,float* forward) noexcept {
    std::size_t yawAt=0,quaternionAt=0;
    switch(lease.profile->inputRva) {
        case 0x343DD0:yawAt=0x1444;break;
        case 0x444E90:case 0x461590:case 0x462E30:case 0x4A6780:yawAt=0x10E4;break;
        case 0x4A8FD0:case 0x42D560:quaternionAt=0xE30;break;
        case 0x4E0010:quaternionAt=0xE10;break;
        case 0x3EE300:quaternionAt=0xFB0;break;
        case 0x31ED90:
            // Deiroi's leg target builder 3227C0..32296B adds scaled +F10
            // directly to world foot targets; the body yaw F44 is not applied.
            right[0]=1;right[1]=0;right[2]=0;up[0]=0;up[1]=1;up[2]=0;
            forward[0]=0;forward[1]=0;forward[2]=1;return true;
        case 0x30E300:yawAt=0x1084;break;
        case 0x368310:yawAt=0xFC4;break;
        case 0x3A4AF0:yawAt=0x1754;break;
        case 0x3B3ED0:yawAt=0x10C4;break;
        case 0x3C0850:yawAt=0x1004;break;
        case 0x3D60F0:yawAt=0xFC4;break;
        case 0x3E2140:yawAt=0x12D4;break;
        case 0x4ACD70:case 0x4BC8B0:yawAt=0x1504;break;
        case 0x4F9540:case 0x4F74D0:yawAt=0x5F4;break;
        case 0x52EF70:case 0x5371C0: {
            // These two native readers themselves multiply movement by the
            // actor's rows (52F358..37D / 5375B8..5DC), with no separate quaternion.
            const auto matrix=reinterpret_cast<const float*>(lease.object+0x60);
            for(int i=0;i<3;++i){right[i]=matrix[i];up[i]=matrix[4+i];forward[i]=matrix[8+i];}
            return UnitMove(right) && UnitMove(up) && UnitMove(forward);
        }
        default:return false;
    }
    if(yawAt) {
        if(!Readable(lease.object+yawAt,4))return false;
        const float yaw=At<float>(lease.object,yawAt);
        if(!std::isfinite(yaw))return false;
        const float sine=std::sin(yaw),cosine=std::cos(yaw);
        right[0]=cosine;right[1]=0;right[2]=-sine;
        up[0]=0;up[1]=1;up[2]=0;
        forward[0]=sine;forward[1]=0;forward[2]=cosine;return true;
    }
    if(!Readable(lease.object+quaternionAt,16))return false;
    double x=At<float>(lease.object,quaternionAt),y=At<float>(lease.object,quaternionAt+4),
           z=At<float>(lease.object,quaternionAt+8),w=At<float>(lease.object,quaternionAt+12);
    const double norm=x*x+y*y+z*z+w*w;
    if(!std::isfinite(norm) || norm<1e-8)return false;
    const double scale=1.0/std::sqrt(norm);x*=scale;y*=scale;z*=scale;w*=scale;
    right[0]=static_cast<float>(1-2*(y*y+z*z));right[1]=static_cast<float>(2*(x*y+z*w));right[2]=static_cast<float>(2*(x*z-y*w));
    up[0]=static_cast<float>(2*(x*y-z*w));up[1]=static_cast<float>(1-2*(x*x+z*z));up[2]=static_cast<float>(2*(y*z+x*w));
    forward[0]=static_cast<float>(2*(x*z+y*w));forward[1]=static_cast<float>(2*(y*z-x*w));forward[2]=static_cast<float>(1-2*(x*x+y*y));
    return true;
}

}
bool WorldMoveToPad(const NativeLease& lease,const float movement[3],float cameraYaw,Input* out) noexcept {
    __try {
        if(!out || !movement || !lease.profile || !lease.object || !lease.control ||
            !Readable(lease.control,12) || At<LONG>(lease.control,8)<=0 ||
            !Readable(lease.object,0x355) || At<const void*>(lease.object,0x30)!=lease.control ||
            (lease.object[0x18]&4) || lease.object[0x2E8] || !std::isfinite(cameraYaw))return false;
        for(unsigned i=0;i<3;++i)if(!std::isfinite(movement[i]))return false;
        Input next=*out;
        if(!(lease.profile->axes&1)) {
            next.right=next.forward=next.vertical=0.0f;*out=next;return true;
        }
        float right[3],up[3],forward[3];
        if(!BasisMove(lease,right,up,forward))return false;
        // A wall-clinging creature's movement remains on its native tangent
        // plane. If the camera points along its normal, preserve native forward.
        float cameraForward[3]={std::sin(cameraYaw),0.0f,std::cos(cameraYaw)};
        const float along=DotMove(cameraForward,up);
        for(int i=0;i<3;++i)cameraForward[i]-=up[i]*along;
        if(!UnitMove(cameraForward))for(int i=0;i<3;++i)cameraForward[i]=forward[i];
        // EDF's +X is the left of +Z (enemy_control_camera.h): the screen's right is
        // forward x up, not up x forward (that was the left: A/D and the stick swapped).
        float cameraRight[3];CrossMove(cameraForward,up,cameraRight);
        if(!UnitMove(cameraRight))return false;
        float world[3];
        const bool vertical=(lease.profile->axes&8)!=0;
        for(int i=0;i<3;++i)world[i]=cameraRight[i]*LimitMove(movement[0])+cameraForward[i]*LimitMove(movement[2]);
        if(vertical)world[1]+=LimitMove(movement[1]);
        const float length2=DotMove(world,world);
        if(length2>1.0f)UnitMove(world);
        // Deiroi/Nephila consume +F10 directly; every other admitted reader
        // negates X/Z explicitly or scales the native vector by a negative gain.
        const float sign=lease.profile->inputRva==0x31ED90 || lease.profile->inputRva==0x4E0010 ? 1.0f : -1.0f;
        next.right=LimitMove(sign*DotMove(world,right));next.forward=LimitMove(sign*DotMove(world,forward));
        next.vertical=vertical ? LimitMove(DotMove(world,up)) : 0.0f;
        // These two readers construct Y as pad[E4]-pad[CC], the opposite of
        // FlyingDyno/UFO507/UFO515. Squid uses verified held buttons in RunInput.
        if(lease.profile->inputRva==0x3D60F0 || lease.profile->inputRva==0x3EE300)next.vertical=-next.vertical;
        *out=next;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
} // namespace crew::enemy_native
