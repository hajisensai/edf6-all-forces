// Guest timeline for the host's enemy-control pose samples (net-9). Pure: no game access, tested offline
// (tests/enemy_control_pose_interp_test.cpp).
//
// Only the plugin's own fallback pose (the five classes enemy_control_native_pose.cpp KindOf names) goes through here:
// that path hard-writes the native body. The verified classes stay on the game's own sync (0x77C2C0 reader, 0x77C8E0
// soft correction) and are never sampled or written by the plugin.
//
// The host stamps each sample with its GameFrame() and the motion it measured (MeasureMotion). The guest buffers
// samples (Track::Accept, receive callback, no native access) and once per logic frame (Track::Sample, from
// EnemyControlNetworkFrame) reads the timeline at render = localFrame - offset - kDelayFrames:
//  - offset is localFrame - senderFrame of the fastest sample seen; a slower sample lets it creep up by
//    kClockCreepFrames only, so one late batch does not push the whole timeline back;
//  - between two samples: position and native linear velocity lerp, the physical orientation nlerps (short arc),
//    ufo515's bank lerps; every other field (targets, integrators, deformation, current angles whose units/wrap are
//    unproven) holds the earlier sample until render passes the later one;
//  - past the newest sample: position extrapolates with its motion for at most kMaxExtrapolateFrames, then holds;
//  - a new sample that moves the target leaves the visible pose where it was and carries the difference as a residual
//    that decays with time constant kTauFrames; a residual over max(kSnapDistance, |motion| * kSnapMotionFrames) is
//    dropped (the body is put on the target at once), one under kSettle metres is cleared.
#pragma once
#include "enemy_control_pose_codec.h"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace crew::enemy_native::pose_interp {

inline constexpr double kDelayFrames=2;           // one host sample interval (enemy_control_net.cpp kSampleFrames)
inline constexpr double kMaxExtrapolateFrames=6;  // 100 ms, three sample intervals
inline constexpr double kTauFrames=6;             // residual time constant: 100 ms, about 14 frames to 10 %
inline constexpr double kClockCreepFrames=0.125;  // offset rise per slower sample
inline constexpr double kSettle=0.01;             // metres; the game's own settle threshold (0x1765A00)
inline constexpr double kSettleAngle=1.0e-3;      // radians
inline constexpr double kSnapDistance=8.0,kSnapMotionFrames=15.0;
inline constexpr std::uint64_t kMotionGapFrames=15;
inline constexpr double kTeleportDistance=20.0;
inline constexpr std::size_t kRing=4,kPending=8;

// Host side, one per lease: the previous sample's position and frame.
struct SendTrack {
    bool has=false;
    std::uint64_t frame=0;
    float position[3]{};
};

// Displacement per logic frame since this lease's previous sample. Zero for the first sample, after a gap of more than
// kMotionGapFrames, for a move over kTeleportDistance, or a step faster than pose::kMaxMotion: those are a teleport or
// a resumed stream, not motion to extrapolate.
inline void MeasureMotion(SendTrack& track,const float (&position)[3],std::uint64_t frame,float (&motion)[3]) noexcept {
    for(float& value:motion)value=0;
    if(track.has && frame>track.frame && frame-track.frame<=kMotionGapFrames) {
        const double frames=static_cast<double>(frame-track.frame);
        double step[3]{},lengthSquared=0;
        for(std::size_t i=0;i<3;++i) {
            step[i]=static_cast<double>(position[i])-track.position[i];
            lengthSquared+=step[i]*step[i];
        }
        if(std::isfinite(lengthSquared) && lengthSquared<=kTeleportDistance*kTeleportDistance) {
            for(std::size_t i=0;i<3;++i)motion[i]=static_cast<float>(step[i]/frames);
            if(!pose::ValidMotion(motion))for(float& value:motion)value=0;
        }
    }
    track.has=true;track.frame=frame;
    for(std::size_t i=0;i<3;++i)track.position[i]=position[i];
}

namespace detail {
struct Quat {double x=0,y=0,z=0,w=1;};
inline Quat Load(const float (&q)[4]) noexcept {return {q[0],q[1],q[2],q[3]};}
inline Quat Mul(const Quat& a,const Quat& b) noexcept {
    return {a.w*b.x+a.x*b.w+a.y*b.z-a.z*b.y,
            a.w*b.y-a.x*b.z+a.y*b.w+a.z*b.x,
            a.w*b.z+a.x*b.y-a.y*b.x+a.z*b.w,
            a.w*b.w-a.x*b.x-a.y*b.y-a.z*b.z};
}
inline Quat Conj(const Quat& q) noexcept {return {-q.x,-q.y,-q.z,q.w};}
inline Quat Normalized(const Quat& q) noexcept {
    const double length=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w);
    if(!(length>0) || !std::isfinite(length))return {};
    return {q.x/length,q.y/length,q.z/length,q.w/length};
}
inline double Angle(const Quat& q) noexcept {
    const double v=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z);
    return 2*std::atan2(v,std::fabs(q.w));
}
// Rotation by `factor` of q's angle about q's axis (factor 0: identity, 1: q).
inline Quat Scale(Quat q,double factor) noexcept {
    if(q.w<0)q={-q.x,-q.y,-q.z,-q.w};
    const double v=std::sqrt(q.x*q.x+q.y*q.y+q.z*q.z);
    if(!(v>0))return {};
    const double half=std::atan2(v,q.w)*factor,s=std::sin(half)/v;
    return {q.x*s,q.y*s,q.z*s,std::cos(half)};
}
inline void Store(const Quat& q,float (&out)[4]) noexcept {
    out[0]=static_cast<float>(q.x);out[1]=static_cast<float>(q.y);out[2]=static_cast<float>(q.z);out[3]=static_cast<float>(q.w);
}
inline void Lerp3(const float (&a)[3],const float (&b)[3],double t,float (&out)[3]) noexcept {
    for(std::size_t i=0;i<3;++i)out[i]=static_cast<float>(a[i]+(static_cast<double>(b[i])-a[i])*t);
}
// Short-arc normalized lerp.
inline void Nlerp(const float (&a)[4],const float (&b)[4],double t,float (&out)[4]) noexcept {
    double dot=0;for(std::size_t i=0;i<4;++i)dot+=static_cast<double>(a[i])*b[i];
    const double sign=dot<0 ? -1.0 : 1.0;
    Quat q{a[0]*(1-t)+sign*b[0]*t,a[1]*(1-t)+sign*b[1]*t,a[2]*(1-t)+sign*b[2]*t,a[3]*(1-t)+sign*b[3]*t};
    Store(Normalized(q),out);
}
} // namespace detail

// Per-field blend of two samples of one kind/profile at t in [0, 1): see the file comment for the field classes.
inline pose::State Blend(const pose::State& a,const pose::State& b,double t) noexcept {
    pose::State out=a;
    detail::Lerp3(a.position,b.position,t,out.position);
    detail::Nlerp(a.rotation,b.rotation,t,out.rotation);
    detail::Lerp3(a.linearVelocity,b.linearVelocity,t,out.linearVelocity);
    if(a.kind==pose::Kind::ufo515)out.bank=static_cast<float>(a.bank+(static_cast<double>(b.bank)-a.bank)*t);
    return out;
}

enum class Output {none,ready,invalid};

class Track {
public:
    void Reset() noexcept {*this=Track{};}
    bool Empty() const noexcept {return !count_ && !pendingCount_;}

    // Receive side: buffers one decoded sample, touches nothing native. A sample not newer than the last accepted one
    // (u32 wrap-aware) is dropped and returns false. A different kind/profile restarts the whole timeline.
    bool Accept(const pose::Packet& packet,std::uint64_t localFrame) noexcept {
        if(hasSender_ && (packet.state.kind!=kind_ || packet.state.profile!=profile_))Reset();
        if(hasSender_) {
            const auto delta=static_cast<std::int32_t>(packet.senderFrame-lastSender_);
            if(delta<=0)return false;
            unwrapped_+=delta;
        } else {
            hasSender_=true;unwrapped_=packet.senderFrame;kind_=packet.state.kind;profile_=packet.state.profile;
        }
        lastSender_=packet.senderFrame;
        if(pendingCount_==kPending) {
            for(std::size_t i=1;i<kPending;++i)pending_[i-1]=pending_[i];
            --pendingCount_;
        }
        auto& entry=pending_[pendingCount_++];
        entry.state=packet.state;entry.frame=unwrapped_;entry.arrival=localFrame;
        for(std::size_t i=0;i<3;++i)entry.motion[i]=packet.motion[i];
        return true;
    }

    // Game thread, once per logic frame: the pose to write this frame. none: no sample yet. invalid: the blended
    // pose failed pose::Valid (out unchanged; the caller counts it and writes nothing).
    Output Sample(std::uint64_t localFrame,pose::State* out) noexcept {
        if(Empty() || !out)return Output::none;
        if(hasOutput_ && localFrame>lastFrame_) {
            const double factor=std::exp(-static_cast<double>(localFrame-lastFrame_)/kTauFrames);
            for(double& value:residual_)value*=factor;
            residualRotation_=detail::Scale(residualRotation_,factor);
        }
        if(pendingCount_) {
            pose::State before{};const bool carry=hasOutput_ && Target(localFrame,&before);
            Merge();
            pose::State after{};Target(localFrame,&after);
            if(carry) {
                for(std::size_t i=0;i<3;++i)residual_[i]+=static_cast<double>(before.position[i])-after.position[i];
                residualRotation_=detail::Normalized(detail::Mul(detail::Mul(residualRotation_,detail::Load(before.rotation)),
                    detail::Conj(detail::Load(after.rotation))));
            }
        }
        const double distance=std::sqrt(residual_[0]*residual_[0]+residual_[1]*residual_[1]+residual_[2]*residual_[2]);
        const auto& motion=ring_[count_-1].motion;
        const double speed=std::sqrt(static_cast<double>(motion[0])*motion[0]+static_cast<double>(motion[1])*motion[1]+
            static_cast<double>(motion[2])*motion[2]);
        const double snap=kSnapDistance>speed*kSnapMotionFrames ? kSnapDistance : speed*kSnapMotionFrames;
        if(!std::isfinite(distance) || distance>snap) {
            for(double& value:residual_)value=0;
            residualRotation_={};
        } else if(distance<kSettle) {
            for(double& value:residual_)value=0;
        }
        if(detail::Angle(residualRotation_)<kSettleAngle)residualRotation_={};
        pose::State result{};Target(localFrame,&result);
        for(std::size_t i=0;i<3;++i)result.position[i]=static_cast<float>(result.position[i]+residual_[i]);
        detail::Store(detail::Normalized(detail::Mul(residualRotation_,detail::Load(result.rotation))),result.rotation);
        hasOutput_=true;lastFrame_=localFrame;
        if(!pose::Valid(result))return Output::invalid;
        *out=result;return Output::ready;
    }

    // Exposed for tests.
    double Offset() const noexcept {return offset_;}
    double Residual() const noexcept {
        return std::sqrt(residual_[0]*residual_[0]+residual_[1]*residual_[1]+residual_[2]*residual_[2]);
    }

private:
    struct Entry {
        pose::State state{};
        std::int64_t frame=0;
        std::uint64_t arrival=0;
        float motion[3]{};
    };
    void Merge() noexcept {
        for(std::size_t p=0;p<pendingCount_;++p) {
            const Entry& entry=pending_[p];
            const double candidate=static_cast<double>(entry.arrival)-static_cast<double>(entry.frame);
            if(!hasOffset_ || candidate<offset_)offset_=candidate;
            else offset_=candidate<offset_+kClockCreepFrames ? candidate : offset_+kClockCreepFrames;
            hasOffset_=true;
            if(count_==kRing) {
                for(std::size_t i=1;i<kRing;++i)ring_[i-1]=ring_[i];
                --count_;
            }
            ring_[count_++]=entry;
        }
        pendingCount_=0;
    }
    bool Target(std::uint64_t localFrame,pose::State* out) const noexcept {
        if(!count_)return false;
        const double render=static_cast<double>(localFrame)-offset_-kDelayFrames;
        const Entry& oldest=ring_[0];
        const Entry& newest=ring_[count_-1];
        if(render<=static_cast<double>(oldest.frame)) {*out=oldest.state;return true;}
        if(render>=static_cast<double>(newest.frame)) {
            double ahead=render-static_cast<double>(newest.frame);
            if(ahead>kMaxExtrapolateFrames)ahead=kMaxExtrapolateFrames;
            *out=newest.state;
            for(std::size_t i=0;i<3;++i)out->position[i]=static_cast<float>(newest.state.position[i]+newest.motion[i]*ahead);
            return true;
        }
        for(std::size_t i=0;i+1<count_;++i) {
            const Entry& a=ring_[i];
            const Entry& b=ring_[i+1];
            if(render>=static_cast<double>(a.frame) && render<static_cast<double>(b.frame)) {
                *out=Blend(a.state,b.state,(render-static_cast<double>(a.frame))/static_cast<double>(b.frame-a.frame));
                return true;
            }
        }
        *out=newest.state;return true;
    }

    Entry ring_[kRing]{};
    Entry pending_[kPending]{};
    std::size_t count_=0,pendingCount_=0;
    bool hasSender_=false,hasOffset_=false,hasOutput_=false;
    std::uint32_t lastSender_=0;
    std::int64_t unwrapped_=0;
    pose::Kind kind_=pose::Kind::deiroi;
    std::uint32_t profile_=0;
    double offset_=0;
    std::uint64_t lastFrame_=0;
    double residual_[3]{};
    detail::Quat residualRotation_{};
};

} // namespace crew::enemy_native::pose_interp
