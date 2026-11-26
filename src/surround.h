#pragma once
// Where a sound goes among the speakers (docs/feature-wt-sound.md): the pure part of jetaudio.cpp's output matrices,
// checked offline by tools/surround_check.cpp. Any layout the mastering voice reports (its channel mask: 2.0, 2.1, quad,
// 5.1, 7.1, ...): the speakers around the listener get the sound panned between the two either side of it at equal power;
// a near or wide sound (its spread, 0 a point .. 1 all around) blends toward all of them equally, the power kept; the LFE
// channel only gets the low layers sent to it (Heard::sub), never a share of the rest. A stereo recording is two sources,
// its left and right channels either side of its direction, as far apart as its spread.
//
// The direction is the camera's (sound_place.h): `side` +1 to its right, `front` +1 ahead (the listener's frame's +x is
// its left, +z ahead: firstperson.cpp's camera rows). Angles are degrees clockwise from ahead.
#include <cmath>
#include <cstdint>

namespace crew::surround {
constexpr int kMostChannels=18;   // XAUDIO2_MAX_AUDIO_CHANNELS... more than any channel mask names
constexpr float kPi=3.14159265f;
// The channel mask's speakers (ksmedia.h SPEAKER_*), in the order their channels come.
constexpr std::uint32_t kFrontLeft=0x1,kFrontRight=0x2,kFrontCenter=0x4,kLowFrequency=0x8,kBackLeft=0x10,kBackRight=0x20,
                        kFrontLeftOfCenter=0x40,kFrontRightOfCenter=0x80,kBackCenter=0x100,kSideLeft=0x200,
                        kSideRight=0x400;
// The usual masks for a channel count when the device names none (0).
constexpr std::uint32_t kStereo=kFrontLeft|kFrontRight,kQuad=kStereo|kBackLeft|kBackRight,
                        k51=kStereo|kFrontCenter|kLowFrequency|kBackLeft|kBackRight,k71=k51|kSideLeft|kSideRight;

struct Layout {
    int channels=0;
    int ring=0;                 // the speakers around the listener, by angle
    int ringChannel[kMostChannels]{};
    float ringAngle[kMostChannels]{};
    int lfe=-1;                 // the LFE's channel, -1: none
};

inline float Wrap(float a) noexcept {   // into [0, 360)
    a=std::fmod(a,360.0f);
    return a<0.0f ? a+360.0f : a;
}

// The speakers of `mask` over `channels` channels (the mask's bits in order; a channel past them, or a top speaker, is
// left silent). No mask: the usual one for the count; a single channel: it is the ring.
inline Layout LayoutOf(std::uint32_t mask,int channels) noexcept {
    Layout l{};
    l.channels=channels<1 ? 1 : channels>kMostChannels ? kMostChannels : channels;
    if(l.channels==1){l.ring=1;l.ringChannel[0]=0;l.ringAngle[0]=0.0f;return l;}
    if(!mask)mask=l.channels>=8 ? k71 : l.channels>=6 ? k51 : l.channels>=4 ? kQuad : kStereo;
    const bool quad=(mask&(kBackLeft|kBackRight)) && !(mask&(kFrontCenter|kSideLeft|kSideRight));
    const bool sevens=(mask&(kSideLeft|kSideRight)) && (mask&(kBackLeft|kBackRight));
    int channel=0;
    for(int bit=0;bit<32 && channel<l.channels;++bit) {
        const std::uint32_t s=1u<<bit;
        if(!(mask&s))continue;
        float a=1000.0f;   // not one around the listener
        switch(s) {
        case kFrontLeft: a=quad ? -45.0f : -30.0f; break;
        case kFrontRight: a=quad ? 45.0f : 30.0f; break;
        case kFrontCenter: a=0.0f; break;
        case kLowFrequency: l.lfe=channel; break;
        case kBackLeft: a=quad ? -135.0f : sevens ? -150.0f : -110.0f; break;
        case kBackRight: a=quad ? 135.0f : sevens ? 150.0f : 110.0f; break;
        case kFrontLeftOfCenter: a=-15.0f; break;
        case kFrontRightOfCenter: a=15.0f; break;
        case kBackCenter: a=180.0f; break;
        case kSideLeft: a=sevens ? -90.0f : -110.0f; break;
        case kSideRight: a=sevens ? 90.0f : 110.0f; break;
        default: break;   // the top speakers: no height here
        }
        if(a<999.0f) {
            // kept sorted by angle (insertion: a few speakers)
            const float w=Wrap(a);
            int at=l.ring;
            while(at>0 && l.ringAngle[at-1]>w){l.ringAngle[at]=l.ringAngle[at-1];l.ringChannel[at]=l.ringChannel[at-1];--at;}
            l.ringAngle[at]=w;l.ringChannel[at]=channel;++l.ring;
        }
        ++channel;
    }
    if(!l.ring){l.ring=1;l.ringChannel[0]=0;l.ringAngle[0]=0.0f;}
    return l;
}

// A point source at `angle`: its gain on each ring speaker (`g`, by ring index), the two either side at equal power
// (sin / cos of the way between them; across a gap wider than a half circle, by the sound's side). One speaker: all of
// it there.
inline void PointGains(const Layout& l,float angle,float* g) noexcept {
    for(int i=0;i<l.ring;++i)g[i]=0.0f;
    if(l.ring==1){g[0]=1.0f;return;}
    const float a=Wrap(angle);
    for(int i=0;i<l.ring;++i) {
        const int j=(i+1)%l.ring;
        const float from=l.ringAngle[i];
        const float span=Wrap(l.ringAngle[j]-from)>0.0f ? Wrap(l.ringAngle[j]-from) : 360.0f;
        const float into=Wrap(a-from);
        if(into<=span) {
            float t=into/span;
            if(span>180.0f) {
                // No speaker behind (stereo: the gap from one front speaker round to the other): a sound in it is heard
                // by its side, as a pair of ears would, not by how far round the back it is: wholly in the near
                // speaker until its side turns past the gap's edges', across to the other behind.
                const float half=0.5f*span*kPi/180.0f,off=(into-0.5f*span)*kPi/180.0f;
                const float lateral=std::sin(off)/std::sin(half);
                t=0.5f*((lateral<-1.0f ? -1.0f : lateral>1.0f ? 1.0f : lateral)+1.0f);
            }
            g[i]=std::cos(0.5f*kPi*t);g[j]+=std::sin(0.5f*kPi*t);
            return;
        }
    }
    g[0]=1.0f;
}

// The power-kept blend of a point source's gains toward every ring speaker's equal share.
inline void Blend(const Layout& l,float spread,float* g) noexcept {
    const float s=spread<0.0f ? 0.0f : spread>1.0f ? 1.0f : spread;
    const float even=1.0f/static_cast<float>(l.ring);
    for(int i=0;i<l.ring;++i)g[i]=std::sqrt((1.0f-s)*g[i]*g[i]+s*even);
}

inline float AngleOf(float side,float front) noexcept {
    return side==0.0f && front==0.0f ? 0.0f : std::atan2(side,front)*180.0f/kPi;
}

// What a sound is heard as: its gain, its direction from the camera, its spread; `sub`: a low layer, sent to the LFE
// channel alone (with no LFE channel, to every speaker equally: nothing is dropped).
struct Placing { float gain,side,front,spread; bool sub; };

// The output matrix of a voice of `inputs` channels (1, or 2: a stereo recording) into the layout's channels, as XAudio2
// takes it (SetOutputMatrix: level of input s in output d at m[s + inputs * d]); `m` has l.channels * inputs floats.
inline void Matrix(const Layout& l,const Placing& p,int inputs,float* m) noexcept {
    const int in=inputs==2 ? 2 : 1;
    for(int i=0;i<l.channels*in;++i)m[i]=0.0f;
    const float gain=std::isfinite(p.gain) && p.gain>0.0f ? p.gain : 0.0f;
    if(gain==0.0f)return;
    const float each=in==2 ? 0.70710678f : 1.0f;   // the two channels' power together a mono one's
    if(p.sub && l.lfe>=0) {
        for(int s=0;s<in;++s)m[s+in*l.lfe]=gain*each;
        return;
    }
    const float spread=p.sub ? 1.0f : std::isfinite(p.spread) ? p.spread : 0.0f;
    const float centre=AngleOf(std::isfinite(p.side) ? p.side : 0.0f,std::isfinite(p.front) ? p.front : 1.0f);
    const float apart=in==2 ? 90.0f*(spread<0.0f ? 0.0f : spread>1.0f ? 1.0f : spread) : 0.0f;   // each side of it
    float g[kMostChannels];
    for(int s=0;s<in;++s) {
        PointGains(l,centre+(in==2 ? (s==0 ? -apart : apart) : 0.0f),g);
        Blend(l,spread,g);
        for(int i=0;i<l.ring;++i)m[s+in*l.ringChannel[i]]=gain*each*g[i];
    }
}

// How wide a sound of `size` m is heard from `distance` m off, at least `least` (0..1 of all around): all around at its
// middle, half at `size` off, narrowing as 1 / d farther (FMOD's sound size and minimum extent, approximated).
inline float SpreadAt(float distance,float size,float least) noexcept {
    const float d=distance>0.0f && std::isfinite(distance) ? distance : 0.0f;
    const float s=size>0.0f ? size/(d+size) : 0.0f;
    return s>least ? s : least;
}
}  // namespace crew::surround
