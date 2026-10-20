// Where a sound is heard from the camera, offline (src/sound_place.h, jetsound.cpp SoundAt's arithmetic): a still sound
// (no velocity: the ultimate strike's recordings, the 2026-10-20 smoke test's fault every cue) is placed and Dopplered by
// the listener's motion alone, as a sound given a zero velocity is; a sound closing in is heard higher, one going away
// lower, the speeds clamped short of c; the ears' gains pan to the side the sound is on; no velocity from either side is
// read when it is not given.
//
//   cmake --build build --target sound_place_check && build\sound_place_check.exe
#include "../src/sound_place.h"
#include <cmath>
#include <cstdio>

namespace {
int failures=0;
void Check(bool ok,const char* what) {
    std::printf("%s  %s\n",ok ? "ok  " : "FAIL",what);
    if(!ok)++failures;
}
bool Near(float a,float b,float eps=1e-5f) { return std::fabs(a-b)<=eps; }
bool Same(const crew::SoundPlace& a,const crew::SoundPlace& b) {
    return Near(a.left,b.left) && Near(a.right,b.right) && Near(a.distance,b.distance) && Near(a.doppler,b.doppler) &&
           Near(a.toCamera[0],b.toCamera[0]) && Near(a.toCamera[1],b.toCamera[1]) && Near(a.toCamera[2],b.toCamera[2]);
}
// A listener at `at` looking along +z, unrotated: the inverse's rows the identity, its last row -at.
void Listener(float* cam,float x,float y,float z) {
    for(int i=0;i<crew::soundplace::kListenerFloats;++i)cam[i]=0.0f;
    cam[0]=x;cam[1]=y;cam[2]=z;
    float* r=cam+4;
    r[0]=r[5]=r[10]=r[15]=1.0f;
    r[12]=-x;r[13]=-y;r[14]=-z;
}
}  // namespace

int main() {
    using namespace crew::soundplace;
    float cam[kListenerFloats];
    Listener(cam,0.0f,0.0f,0.0f);
    const float still[3]={0.0f,0.0f,0.0f},ahead[3]={0.0f,0.0f,100.0f};
    const float camStill[3]={0.0f,0.0f,0.0f},camAway[3]={0.0f,0.0f,-50.0f};

    // The contract: no velocity is a still sound, the same place as a zero velocity (the ultimate's Sound passes none).
    Check(Same(Place(cam,camStill,ahead,nullptr),Place(cam,camStill,ahead,still)),"no velocity: as a still sound");
    Check(Same(Place(cam,camAway,ahead,nullptr),Place(cam,camAway,ahead,still)),"no velocity: as a still sound, the camera moving");
    Check(Same(Place(cam,nullptr,ahead,nullptr),Place(cam,camStill,ahead,still)),"no velocity either side: no Doppler");
    const crew::SoundPlace s=Place(cam,nullptr,ahead,nullptr);
    Check(Near(s.doppler,1.0f) && Near(s.distance,100.0f) && Near(s.toCamera[2],-1.0f),"a still sound ahead: 100 m, ratio 1");
    Check(Near(s.left,s.right),"a sound straight ahead: the same in both ears");

    // Doppler: closing in higher, going away lower; the listener going away lowers it; clamped short of c.
    const float closing[3]={0.0f,0.0f,-100.0f},leaving[3]={0.0f,0.0f,100.0f},fast[3]={0.0f,0.0f,-1e6f};
    Check(Place(cam,nullptr,ahead,closing).doppler>1.0f,"a sound closing in: higher");
    Check(Place(cam,nullptr,ahead,leaving).doppler<1.0f,"a sound going away: lower");
    Check(Place(cam,camAway,ahead,nullptr).doppler<1.0f,"the listener going away from a still sound: lower");
    Check(Near(Place(cam,nullptr,ahead,fast).doppler,kSoundSpeed/(kSoundSpeed-kMaxClosing),1e-4f),
          "a sound past the speed of sound: clamped (finite)");

    // The ears: +x is the camera's left (the game's pan atan2(-x, z)).
    const float left[3]={100.0f,0.0f,0.0f},right[3]={-100.0f,0.0f,0.0f};
    const crew::SoundPlace l=Place(cam,nullptr,left,nullptr),r=Place(cam,nullptr,right,nullptr);
    Check(l.left>l.right && r.right>r.left && Near(l.left,r.right) && Near(l.left*l.left+l.right*l.right,1.0f),
          "a sound to one side: louder in that ear, equal power");
    Check(l.right>0.0f,"the far ear keeps some (the spread)");

    // At the camera: no line to it, no Doppler, both ears.
    const float here[3]={0.0f,0.0f,0.5f};
    const crew::SoundPlace h=Place(cam,camAway,here,closing);
    Check(Near(h.doppler,1.0f) && Near(h.left,h.right) && h.toCamera[0]==0.0f && h.toCamera[1]==0.0f && h.toCamera[2]==0.0f,
          "within 1 m of the camera: ratio 1, both ears, no line");

    // A listener elsewhere: the place is relative to it.
    Listener(cam,1000.0f,50.0f,-300.0f);
    const float there[3]={1000.0f,50.0f,-200.0f};
    Check(Near(Place(cam,nullptr,there,nullptr).distance,100.0f,1e-3f),"a listener elsewhere: the distance from it");

    std::printf("%s\n",failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}
