#pragma once
// 4.1 MAB se / se_stop ownership (EDF41.exe 0x321F50 / 0x3220E0): a missing channel is -1
// and plays independently; a nonnegative channel first stops its old sound, then owns the new handle.
// No time limit: only the source stop event, replacement, state exit, or object lifecycle ends it.
#include <cmath>
#include <map>

namespace crew { namespace edf41sound {
template<class Handle> class Channels {
public:
    template<class Play,class Stop> bool Start(int channel,Play play,Stop stop) {
        if(channel<0)return play(nullptr);
        StopOne(channel,16,stop);
        Handle& handle=held_[channel];
        if(play(&handle))return true;
        stop(handle,0);       // a backend may have started a handle before reporting failure
        held_.erase(channel);
        return false;
    }
    template<class Active> bool Playing(int channel,Active active) const {
        const auto found=held_.find(channel);
        return found!=held_.end() && active(found->second);
    }
    template<class Stop> void StopOne(int channel,int fade,Stop stop) {
        const auto found=held_.find(channel);
        if(found==held_.end())return;
        stop(found->second,fade);
        held_.erase(found);
    }
    template<class Stop> void StopAll(int fade,Stop stop) {
        for(auto& entry:held_)stop(entry.second,fade);
        held_.clear();
    }
    template<class Move> void MoveAll(Move move) {
        for(auto& entry:held_)move(entry.second);
    }
    bool Empty() const { return held_.empty(); }
private:
    std::map<int,Handle> held_;
};
// The animation clock is sampled before and after state-machine updates. Repeated samples
// cannot replay a key; skipped frames still dispatch every crossed source event in order.
class Timeline {
public:
    void Reset() { state_=-2; clip_=0; time_=-1.0f; }
    template<class Clips,class Fire,class Cancel> void Step(int state,float time,const Clips& clips,Fire fire,Cancel cancel) {
        if(!std::isfinite(time) || time<0.0f)return;
        const bool changed=state!=state_;
        const bool rewind=!changed && time<time_;
        if(changed || rewind) {
            const int next=changed ? 0 : clip_+1;
            bool exists=false;
            for(const auto& clip:clips)if(clip.state==state && clip.clip==next)exists=true;
            // A multi-clip state's next clip keeps its channels; a state change or a full
            // rewind cancels the prior animation before its new time-zero events start.
            if(state_!=-2 && (changed || !exists))cancel();
            clip_=exists ? next : 0;
            state_=state;
            time_=-1.0f;
        }
        for(const auto& clip:clips)
            if(clip.state==state_ && clip.clip==clip_)
                for(const auto& event:clip.events)
                    if(event.time>time_ && event.time<=time)fire(event);
        time_=time;
    }
private:
    int state_=-2,clip_=0;
    float time_=-1.0f;
};

// UfoRobo's source gun group has charge and shot handles. Charge is stopped on leaving G1;
// only a looping shot is kept across rounds, and it stops on leaving G2. Finite shot tails
// are independent of the next round and finish naturally (EDF41.exe 0x27B400..0x27C182).
template<class Handle> class GunSounds {
public:
    template<class Play,class Stop> void Charge(Play play,Stop stop) { held_.Start(0,play,stop); }
    template<class Play,class Active,class Stop> void Shot(bool loop,Play play,Active active,Stop stop) {
        if(!loop) { play(nullptr); return; }
        if(!held_.Playing(1,active))held_.Start(1,play,stop);
    }
    template<class Stop> void LeaveCharge(Stop stop) { held_.StopOne(0,0,stop); }
    template<class Stop> void LeaveFire(Stop stop) { held_.StopOne(1,0,stop); }
    template<class Stop> void Release(Stop stop) { held_.StopAll(0,stop); }
    template<class Move> void MoveAll(Move move) { held_.MoveAll(move); }
    bool Empty() const { return held_.Empty(); }
private:
    Channels<Handle> held_;
};
}} // namespace crew::edf41sound
