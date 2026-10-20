// The ultimate strike's shows as this machine keeps them, the pure part (src/ultimate.cpp drives them; tests/ultimate_net_test.cpp
// runs this): no game.
//  - Table: several shows at once, each in its own slot by its key {origin, serial} (the caller's machine's run and the
//    show's number in it): this machine's player's own and every other machine's player's (ultimate_net_protocol.h). A
//    second call while one plays is a second show, never refused for the first (before 2026-10-20 one global show refused
//    it after the call had spent its points). The keys of the shows ended lately are kept, so a late keep of one never
//    starts it again.
//  - SkyRefs: the dark sky is one for all the shows: the first to darken it takes the snapshot of the sky as it was, the
//    last to let go brings it back; one starting while the sky is still on its way back keeps that snapshot (a snapshot
//    then would be of a sky still half dark).
//  - SkyStillOurs: whether a light value is still the one the shows last faded it to (a mission's own fade since then:
//    not, and it is left alone when the sky comes back).
//  - PlanTick and Seek: which frames a tick plays. A few frames behind (a hitch) are all played, so every ring of blasts
//    goes off; further behind (a show joined late, a long stall) the show seeks: its lasting state (the circles, the beams,
//    the sky) is set for the frame it seeks to (ultimate_plan.h's functions of the frame) and the one-off events of the
//    frames between (bolts, blasts, flashes, sounds, fires) are dropped.
#pragma once
#include "ultimate_plan.h"
#include <cmath>
#include <cstdint>

namespace crew::ultimate {
constexpr int kMaxShows=6;

struct Key {
    std::uint32_t origin=0,serial=0;
    bool operator==(const Key& o) const noexcept { return origin==o.origin && serial==o.serial; }
    explicit operator bool() const noexcept { return origin!=0 && serial!=0; }
};

// What a show message (start / keep / end) does here.
enum class Act : std::uint8_t {
    none,     // nothing: an end of a show not played here, a keep or start of one ended here already
    begin,    // a show not played here yet: start it (at its frame)
    follow,   // the show played here: catch up if behind
    stop,     // the show played here ended by its caller
};

class Table {
public:
    struct Head { bool on=false; bool remote=false; std::uint32_t peer=0; Key key{}; };
    int Find(const Key& k) const noexcept {
        if(!k)return -1;
        for(int i=0;i<kMaxShows;++i)if(head_[i].on && head_[i].key==k)return i;
        return -1;
    }
    // The slot for show `k` (its own if it has one), or -1 when every slot is taken.
    int Claim(const Key& k,bool remote,std::uint32_t peer) noexcept {
        if(!k)return -1;
        int i=Find(k);
        if(i<0)for(int j=0;j<kMaxShows && i<0;++j)if(!head_[j].on)i=j;
        if(i<0)return -1;
        head_[i]=Head{true,remote,peer,k};
        return i;
    }
    // Slot `i` freed, its key kept among the ended ones.
    void Release(int i) noexcept {
        if(i<0 || i>=kMaxShows || !head_[i].on)return;
        Ended(head_[i].key);
        head_[i]=Head{};
    }
    void Ended(const Key& k) noexcept {
        if(!k || WasEnded(k))return;
        ended_[endedNext_]=k;endedNext_=(endedNext_+1)%kEnded;
    }
    bool WasEnded(const Key& k) const noexcept {
        for(const Key& e:ended_)if(e && e==k)return true;
        return false;
    }
    Act Receive(const Key& k,bool end) const noexcept {
        if(!k)return Act::none;
        if(Find(k)>=0)return end ? Act::stop : Act::follow;
        return end || WasEnded(k) ? Act::none : Act::begin;
    }
    int Count() const noexcept { int n=0; for(const Head& h:head_)n+=h.on ? 1 : 0; return n; }
    const Head& At(int i) const noexcept { return head_[i>=0 && i<kMaxShows ? i : 0]; }
    void SetPeer(int i,std::uint32_t peer) noexcept { if(i>=0 && i<kMaxShows && head_[i].on)head_[i].peer=peer; }
    void Reset() noexcept { for(Head& h:head_)h=Head{}; for(Key& e:ended_)e=Key{}; endedNext_=0; }
private:
    static constexpr int kEnded=32;
    Head head_[kMaxShows]{};
    Key ended_[kEnded]{};
    int endedNext_=0;
};

// The dark sky's holders. `now` in game frames.
class SkyRefs {
public:
    // A show darkens the sky: true when the snapshot is to be taken now (no other show holds it and none is coming back:
    // the sky is as the mission has it); false: the snapshot there is the one to fade back to.
    bool Acquire(std::uint64_t now) noexcept {
        const bool fresh=holders_==0 && !(snapped_ && now<restoreEnd_);
        if(fresh)snapped_=false;
        ++holders_;
        return fresh;
    }
    void Snapped(bool ok) noexcept { snapped_=ok; }
    // A show lets go of the dark sky: true when it was the last (bring the sky back now, over `backFrames`).
    bool Release(std::uint64_t now,std::uint64_t backFrames) noexcept {
        if(holders_<=0)return false;
        if(--holders_)return false;
        restoreEnd_=now+backFrames;
        return true;
    }
    bool HasSnapshot() const noexcept { return snapped_; }
    int Holders() const noexcept { return holders_; }
    void Reset() noexcept { holders_=0;snapped_=false;restoreEnd_=0; }
private:
    int holders_=0;
    bool snapped_=false;
    std::uint64_t restoreEnd_=0;
};

// Whether the first `n` components of a light value are still the ones the shows last faded it to (`wrote`): within 2 %
// (and 0.01) of each. A fade done lands on its target (the manager's last step writes it: 推测, the fade's end is not read
// back here), so a value that moved off it was faded by someone else, the mission's script.
inline bool SkyStillOurs(const float* now,const float* wrote,int n) noexcept {
    for(int i=0;i<n;++i) {
        const float w=wrote[i],d=std::fabs(now[i]-w),tol=0.01f+0.02f*std::fabs(w);
        if(!(d<=tol))return false;
    }
    return true;
}

// The frames a tick plays: `from`..`to` (none when to < from); `seek`: `from` is sought to, the frames before it dropped.
constexpr int kMostCatchUp=60;   // a second: a stall up to this is played out frame by frame (every ring goes off)
struct TickPlan { bool seek; int from,to; };
inline TickPlan PlanTick(int done,int frame) noexcept {
    if(frame>kAfterEnd)frame=kAfterEnd;
    if(frame<=done)return TickPlan{false,done+1,done};
    if(frame-done>kMostCatchUp)return TickPlan{true,frame,frame};
    return TickPlan{false,done+1,frame};
}

// What lasts at show frame `frame` (Seek sets it; the frame's own events are the frame's): the dark sky, the main beam.
// The circles, the shock waves, the aim and gather beams are the frame's functions already (CircleAt, WaveAt, AimAt,
// GatherAt: made when missing).
struct Lasting { bool skyDark; bool beam; };
inline Lasting LastingAt(int frame) noexcept {
    Lasting l{};
    l.skyDark=frame>=0 && frame<kSkyBackAt;
    l.beam=frame>=kStormEnd && frame<kCannonEnd && BeamScale(frame-kStormEnd)>0.0f;
    return l;
}
// The seconds the darkening takes from frame `frame` on: what is left of the charge's kSkyDarkSec, at least a moment.
inline float DarkSecFrom(int frame) noexcept {
    const float left=kSkyDarkSec-static_cast<float>(frame)/kFps;
    return left>0.5f ? left : 0.5f;
}

// A show heard from another machine: where this machine starts it (the frame the caller had played when it sent, and the
// time the message took: kLeadFrames, an estimate: 推测, the transport's latency is not measured), and whether a show
// played here is behind its caller by more than kResync frames (then it seeks forward; ahead, it plays on: a show never
// goes back).
constexpr int kLeadFrames=6;   // ~100 ms
constexpr int kResync=30;
inline int StartFrame(int sent) noexcept {
    const int f=sent+kLeadFrames;
    return f<0 ? 0 : f>kAfterEnd ? kAfterEnd : f;
}
inline bool Behind(int done,int sent) noexcept { return StartFrame(sent)-done>kResync; }
// An end heard at the caller's frame `sent` for a show played here up to `done`: the frame to seek to (the after: the
// rounds gone, the circles closing, the sky coming back), or -1 to play on (the caller's show ran to its end, or this one
// is past the cannon already).
inline int EndSeek(int done,int sent) noexcept {
    if(sent>=kAfterEnd || done>=kCannonEnd)return -1;
    return kCannonEnd;
}
}  // namespace crew::ultimate
