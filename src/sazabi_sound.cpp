// The Sazabi's sounds (sazabi_sound.h; docs/sound-re.md §10). sazabi.cpp says what sounds and where; this file hears it
// from the camera exactly as the ground vehicles' sounds are heard (vehsound.cpp): SoundAt's ears, distance and Doppler,
// vmix::Falloff, the air's dulling to vmix::kFarAt, the game's master x effect volume and the pause's silence (jetaudio.cpp
// Beat), the clips made in vsynth.h or the player's <dll name>_veh_sazabi_<name>.wav (jetaudio.cpp kClipName). The ini's
// VehicleSound switches them with the vehicles' (it keeps the camera's listener placed, jetsound.cpp JetSoundTick); each
// sound's group volume is vmix::kSzSfx's (VehicleEngineVolume, VehicleGunVolume, VehicleMissileVolume).
//  - A one-shot arrives d / 340 m/s late (game time: a pause holds it), as the vehicles' reports do.
//  - A loop is one voice for the player's Sazabi: set it each frame (its place, velocity and level), and SazabiSoundTick
//    brings its level up or down to it (vmix::SzFade: no click), letting it go out once a frame passes unset. The tick
//    alone owns the voices, so its order against the Sazabi's own frame does not matter (one frame's lag at most).
// Game thread only.
#include "sazabi_sound.h"
#include "crew.h"
#include "jetaudio.h"
#include "vehmix.h"
#include <cmath>
#include <cstdint>

namespace crew {
namespace {
constexpr int kSfxCount=static_cast<int>(SzSfx::count),kLoopCount=static_cast<int>(SzLoop::count);
using audio::kSazabiSfxClip; using audio::kSazabiLoopClip;
static_assert(sizeof(kSazabiSfxClip)/sizeof(kSazabiSfxClip[0])==kSfxCount && sizeof(kSazabiLoopClip)/sizeof(kSazabiLoopClip[0])==kLoopCount,
              "a clip for every Sazabi sound (jetaudio.h), in sazabi_sound.h's order");
constexpr int kPending=32;
constexpr float kMostDt=0.1f;      // s: a tick's step at most (a hitch does not cut a fade short)

struct Pending { int clip=-1; audio::Heard heard{}; ULONGLONG due=0; };
struct Loop {
    int voice=-1;                  // jetaudio.h OpenLoop, -1: none
    float gain=0.0f,want=0.0f,ratio=1.0f;
    SoundPlace place{};
    ULONGLONG setFrame=0;          // GameFrame of its last SazabiLoop, 0: never
};
Pending pending[kPending]{};
Loop loops[kLoopCount]{};
ULONGLONG tickFrame=0,tickMs=0;
std::uint32_t jitterState=0x5A2Au;

bool On() noexcept { return Cfg().enabled && Cfg().vehicleSound && SoundListening(); }
float GroupVolume(vmix::SzGroup g) noexcept {
    const float v=g==vmix::SzGroup::move ? Cfg().vehicleEngineVolume : g==vmix::SzGroup::gun ? Cfg().vehicleGunVolume :
                  Cfg().vehicleMissileVolume;
    return std::isfinite(v) && v>0.0f ? (v>4.0f ? 4.0f : v) : 0.0f;
}
// Gain at the sound's place for `s` at `extra` (its own level), 0 when not heard.
float GainAt(const vmix::SzSound& s,const SoundPlace& p,float extra) noexcept {
    return p.distance<vmix::kSzHear ? GroupVolume(s.group)*s.share*vmix::Falloff(p.distance,s.ref,s.fall)*extra : 0.0f;
}
audio::Heard HeardAt(const SoundPlace& p,float gain,float ratio) noexcept {
    return audio::Heard{gain*p.left,gain*p.right,ratio*p.doppler,p.distance/vmix::kFarAt};
}
// 1 +- up to `amount`, a different one each call.
float Jitter(float amount) noexcept {
    jitterState^=jitterState<<13;jitterState^=jitterState>>17;jitterState^=jitterState<<5;
    return 1.0f+amount*(static_cast<float>(jitterState)/2147483648.0f-1.0f);
}
void Close(Loop& l) noexcept {
    audio::CloseLoop(l.voice);
    l=Loop{};
}
void CloseAll() noexcept {
    for(auto& l:loops)Close(l);
    for(auto& q:pending)q.clip=-1;
}
// A loop's frame: its level eased to what was set this frame or the last (else to 0, then let go of), heard there.
void LoopStep(Loop& l,int clip,ULONGLONG frame,float dt) noexcept {
    if(!l.setFrame)return;
    const bool fresh=frame>=l.setFrame && frame-l.setFrame<=1;
    l.gain=vmix::SzFade(l.gain,fresh ? l.want : 0.0f,dt);
    if(!fresh && l.gain<=0.0f){Close(l);return;}
    if(l.voice<0)l.voice=audio::OpenLoop(clip);   // none free, or the clips not made yet: again next frame
    audio::SetLoop(l.voice,HeardAt(l.place,l.gain,l.ratio));
}
}  // namespace

void SazabiSfx(SzSfx which,const float* pos) noexcept {
    const int k=static_cast<int>(which);
    if(k<0 || k>=kSfxCount || !pos || !On() || !audio::ClipsReady())return;
    static constexpr float kStill[3]{};
    SoundPlace p{};
    if(!SoundAt(pos,kStill,&p))return;
    const vmix::SzSound& s=vmix::kSzSfx[k];
    const float gain=GainAt(s,p,1.0f);
    if(gain<0.002f)return;
    const audio::Heard h=HeardAt(p,gain,Jitter(s.jitter));
    const ULONGLONG due=GameMs()+static_cast<ULONGLONG>(p.distance/vmix::kSoundSpeed*1000.0f);
    if(due<=GameMs()){audio::PlayOnce(kSazabiSfxClip[k],h);return;}
    for(auto& q:pending)
        if(q.clip<0){q=Pending{kSazabiSfxClip[k],h,due};return;}
    // more on their way than kPending: the rest are not heard
}

void SazabiLoop(SzLoop which,const float* pos,const float* vel,float level) noexcept {
    const int k=static_cast<int>(which);
    if(k<0 || k>=kLoopCount || !pos || !On())return;
    static constexpr float kStill[3]{};
    Loop& l=loops[k];
    if(!SoundAt(pos,vel ? vel : kStill,&l.place))return;
    (void)audio::ClipsReady();   // the clips made on their thread from the first call on
    const vmix::SzLoopMix m=vmix::SazabiLoopMix(which,level);
    l.want=GainAt(vmix::kSzLoop[k],l.place,m.gain);
    l.ratio=m.ratio;
    l.setFrame=GameFrame();
}

void SazabiSoundTick() noexcept {
    const ULONGLONG frame=GameFrame();
    if(frame==tickFrame)return;
    const ULONGLONG ms=GameMs();
    const float dt=tickMs && ms>tickMs ? static_cast<float>(ms-tickMs)*0.001f : 0.0f;
    tickFrame=frame;tickMs=ms;
    if(!On()){CloseAll();return;}   // switched off, or no listener: nothing of ours can be heard
    for(auto& q:pending)if(q.clip>=0 && ms>=q.due){audio::PlayOnce(q.clip,q.heard);q.clip=-1;}
    for(int k=0;k<kLoopCount;++k)LoopStep(loops[k],kSazabiLoopClip[k],frame,dt<kMostDt ? dt : kMostDt);
}

void ResetSazabiSound() noexcept {
    CloseAll();
    tickFrame=tickMs=0;
}
}  // namespace crew
