#pragma once
// Where a sound is heard from the camera: the pure part of jetsound.cpp's SoundAt (the jets' engines, the ground
// vehicles' sounds, the Sazabi's, the ultimate strike's recordings), checked offline by tools/sound_place_check.cpp.
//
// The listener is the sound system's listener 0 (jetsound.cpp kListeners): its position (cam[0..2]), then the four rows
// of its world matrix's inverse at cam+4 (a point's camera coordinates p.x R1 + p.y R2 + p.z R3 + R4; +x is the camera's
// left). The Doppler ratio is (c - v_listener.u) / (c - v_source.u), u the unit line from the sound to the camera.
//
// The contract (2026-10-20 smoke test: the ultimate's recordings passed no velocity, Place read through it every cue, the
// show's step faulted C0000005 each time, src/ultimate.cpp Sound): `pos` and `cam` must point at their floats; `vel` and
// `camVel` may be nullptr, a still sound / a still listener (no Doppler from that side).
#include <cmath>
#include <cstring>

namespace crew {

// Its gain in each ear (equal power, the spread kept), its distance (m), the Doppler ratio between it and the camera,
// the unit line from it to the camera (zero within 1 m).
struct SoundPlace { float left,right,distance,doppler; float toCamera[3]; };

namespace soundplace {
constexpr float kSpread=0.75f;                  // how far a sound to one side leaves the other ear
constexpr float kSoundSpeed=340.0f;
constexpr float kMaxClosing=0.8f*kSoundSpeed;   // the Doppler ratio's speeds along the line, clamped short of c
constexpr int kListenerFloats=20;               // the position, a pad, the four rows (jetsound.cpp copies the entry)

inline float Dot(const float* a,const float* b) noexcept { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline float Closing(const float* v,const float* u) noexcept {
    if(!v)return 0.0f;
    const float s=Dot(v,u);
    return s>kMaxClosing ? kMaxClosing : s<-kMaxClosing ? -kMaxClosing : s;
}

inline SoundPlace Place(const float* cam,const float* camVel,const float* pos,const float* vel) noexcept {
    const float* r=cam+4;   // the inverse's rows
    float local[3];
    for(int c=0;c<3;++c)local[c]=pos[0]*r[c]+pos[1]*r[4+c]+pos[2]*r[8+c]+r[12+c];
    const float d=std::sqrt(Dot(local,local));
    const float right=d>1.0f ? -local[0]/d : 0.0f;
    SoundPlace p{};
    p.left=std::sqrt(0.5f*(1.0f-kSpread*right));p.right=std::sqrt(0.5f*(1.0f+kSpread*right));
    p.distance=d;p.doppler=1.0f;
    float u[3]={cam[0]-pos[0],cam[1]-pos[1],cam[2]-pos[2]};
    const float n=std::sqrt(Dot(u,u));
    if(n>1.0f) {
        for(auto& c:u)c/=n;
        const float vs=Closing(vel,u),vl=Closing(camVel,u);   // the source closing in; the listener going away
        p.doppler=(kSoundSpeed-vl)/(kSoundSpeed-vs);
        std::memcpy(p.toCamera,u,12);
    }
    return p;
}
}  // namespace soundplace
}  // namespace crew
