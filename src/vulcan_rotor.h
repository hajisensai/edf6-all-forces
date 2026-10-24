// M163's visual rotor (the quad Vulcan's four: pylib/quadvulcan_model.py ROTORS). Native firing state is a shot
// activity source, not a keyboard latch: empty belts/reload/remote operators must not invent firing.
#pragma once
#include <cmath>
#include <cstdint>

namespace crew {
bool InstallVulcanRotor() noexcept;
void ResetVulcanRotors() noexcept;
namespace vulcanrotor {
constexpr wchar_t kBone[]=L"edf6vc_m61_rotor";
// Every rotor a vehicle may have: the M163's one (kBone), the quad Vulcan's three more (pylib/quadvulcan_model.py ROTORS).
// The game's bone lookup (body506.cpp BoneRecord506) compares the first 16 characters only, and kBone is 16 long: the
// others differ within those 16 ("edf6vc_m61_rotor_2" found the M163's rotor, so three of the quad's four never spun).
constexpr const wchar_t* kBones[]={kBone,L"edf6vc_m61_rot2",L"edf6vc_m61_rot3",L"edf6vc_m61_rot4"};
constexpr int kMostBones=sizeof(kBones)/sizeof(kBones[0]);
constexpr bool SameFirst16(const wchar_t* a,const wchar_t* b) noexcept {
    for(int i=0;i<16;++i){if(a[i]!=b[i])return false;if(!a[i])return true;}
    return true;
}
constexpr bool Apart16() noexcept {
    for(int i=0;i<kMostBones;++i)for(int k=i+1;k<kMostBones;++k)if(SameFirst16(kBones[i],kBones[k]))return false;
    return true;
}
static_assert(Apart16(),"every rotor bone is told apart within the 16 characters BoneRecord506 compares");
// The M61s' weapon files (tools/make_tracked_combat.py: the M163's two belts, the quad's HE pair and AP pair, the
// Phalanx's pair, EDF6VC_CIWS_PHALANX_*): one
// of their holders shooting spins every rotor.
constexpr const wchar_t* kGunPrefixes[]={L"EDF6VC_VULCAN_",L"EDF6VC_QUADVULCAN_",L"EDF6VC_QVULCAN_",L"EDF6VC_CIWS_"};
constexpr int kMostHolders=8;
constexpr float kPi=3.14159265358979323846f,kTau=2*kPi;
// M163's 3000 rounds/min cyclic mode / six barrels = 500 rotor rpm.
// At a 60 Hz update this is 50 degrees, not a symmetry-aliasing 60 degrees.
constexpr float kRunSpeed=500.0f*kTau/60.0f;
constexpr float kRiseSec=0.08f,kFallSec=0.25f;
constexpr std::uint64_t kActivityMs=80;
struct Reading { const void* weapon=nullptr;std::int32_t ammo=0,state=0,received=0,played=0; };
struct Sample { Reading prior{};bool known=false; };
inline bool Observe(Sample& sample,const Reading& now) noexcept {
    const bool same=sample.known && sample.prior.weapon==now.weapon;
    const bool ammo=same && now.ammo>=0 && now.ammo<sample.prior.ammo;
    // Same native received/pending-replay test as vehmix::RemoteShot. A new
    // weapon's historical counters are a baseline, never a spin backlog.
    const bool remote=same && now.received>sample.prior.received && now.received>now.played;
    const bool firing=now.state==1 && now.ammo>0;
    sample.prior=now;sample.known=now.weapon!=nullptr;
    return now.weapon && (ammo || remote || firing);
}
struct Motion { float phase=0.0f,speed=0.0f;std::uint64_t lastShot=0;bool active=false; };
inline void Step(Motion& motion,bool shot,std::uint64_t now,float seconds) noexcept {
    if(shot){motion.lastShot=now;motion.active=true;}
    if(!std::isfinite(seconds) || seconds<0.0f || seconds>0.25f){motion.speed=0.0f;return;}
    const bool firing=motion.active && now>=motion.lastShot && now-motion.lastShot<=kActivityMs;
    const float target=firing ? kRunSpeed : 0.0f;
    const float step=kRunSpeed/(firing ? kRiseSec : kFallSec)*seconds;
    const float before=motion.speed;
    if(target>motion.speed)motion.speed=motion.speed+step>target ? target : motion.speed+step;
    else motion.speed=motion.speed-step<target ? target : motion.speed-step;
    motion.phase=std::fmod(motion.phase+(before+motion.speed)*0.5f*seconds,kTau);
    if(motion.phase<0)motion.phase+=kTau;
}
inline void Local(const float* bind,float phase,float* out) noexcept {
    const float c=std::cos(phase),s=std::sin(phase);
    for(int k=0;k<4;++k){out[k]=c*bind[k]+s*bind[k+4];out[k+4]=-s*bind[k]+c*bind[k+4];}
    for(int k=8;k<16;++k)out[k]=bind[k];
}
inline void Multiply(const float* a,const float* b,float* out) noexcept {
    for(int row=0;row<4;++row)for(int col=0;col<4;++col){
        float value=0;for(int k=0;k<4;++k)value+=a[row*4+k]*b[k*4+col];out[row*4+col]=value;
    }
}
// The two half-idler bones are siblings with different bind pivots. At live
// mode 1 AFR does not update H, so preserve the same skinned motion as G by
// Hbind * inverse(Gbind) * GposedWorld after the native wheel world writer.
// Shared parent makes this local-bind difference equal the world-bind difference.
// Death skips this and mode 4 owns both halves.
inline bool Follow(const float* driver,const float* follower,const float* posed,float* out) noexcept {
    for(int i=0;i<16;++i)if(!std::isfinite(driver[i]) || !std::isfinite(follower[i]) || !std::isfinite(posed[i]))return false;
    for(int row=0;row<3;++row)for(int col=0;col<3;++col){
        float dot=0;for(int k=0;k<3;++k)dot+=driver[row*4+k]*driver[col*4+k];
        if(std::fabs(dot-(row==col ? 1.0f : 0.0f))>0.001f)return false;
    }
    if(std::fabs(driver[3])+std::fabs(driver[7])+std::fabs(driver[11])+std::fabs(driver[15]-1)>0.001f)return false;
    float inverse[16]{};inverse[15]=1;
    for(int row=0;row<3;++row)for(int col=0;col<3;++col)inverse[row*4+col]=driver[col*4+row];
    for(int col=0;col<3;++col)for(int k=0;k<3;++k)inverse[12+col]-=driver[12+k]*inverse[k*4+col];
    float delta[16];Multiply(follower,inverse,delta);Multiply(delta,posed,out);
    return true;
}

}  // namespace vulcanrotor
}  // namespace crew
