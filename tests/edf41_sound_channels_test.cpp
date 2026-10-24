#include "../src/edf41_sound_channels.h"
#include "../src/edf41_sound_fallback.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {
struct Handle { int instance=0; };
struct Action { char kind; int id; int fade; };
int failed=0;
void Check(bool ok,const char* what) { if(!ok) { ++failed; std::printf("FAIL %s\n",what); } }
struct Backend {
    int next=0,unowned=0;
    bool fail=false;
    std::vector<Action> actions;
    bool Play(Handle* h) {
        if(!h) { ++unowned; return !fail; }
        h->instance=++next;
        actions.push_back({'P',h->instance,0});
        return !fail;
    }
    void Stop(Handle& h,int fade) {
        if(h.instance)actions.push_back({'S',h.instance,fade});
        h.instance=0;
    }
    void Move(Handle& h) { actions.push_back({'M',h.instance,0}); }
    int Stops() const { int n=0; for(const Action& a:actions)n+=a.kind=='S'; return n; }
};
}
int main() {
    const auto& cue=crew::edf41sound::kFallbackCues[0];
    std::vector<std::wstring> looked;
    bool nativeSource=false,sourceBank=false,nativeTarget=true;
    int imports=0;
    auto stockCue=[&](const wchar_t* name) {
        looked.emplace_back(name);
        return (nativeSource && std::wcscmp(name,cue.source)==0) ||
               (nativeTarget && std::wcscmp(name,cue.target)==0);
    };
    auto importedCue=[&](const wchar_t* name) {
        ++imports;
        return sourceBank && std::wcscmp(name,cue.source)==0;
    };
    Check(crew::edf41sound::LookupCue(cue.source,stockCue,importedCue) &&
          looked.size()==2 && looked.back()==cue.target,"missing optional bank uses the resident finite fallback");
    looked.clear(); sourceBank=true;
    Check(crew::edf41sound::LookupCue(cue.source,stockCue,importedCue) && looked.size()==1,
          "an available imported bank keeps its exact source sound");
    looked.clear(); nativeSource=true;
    const int beforeImports=imports;
    Check(crew::edf41sound::LookupCue(cue.source,stockCue,importedCue) && looked.size()==1 && imports==beforeImports,
          "native lookup wins without touching imported or fallback audio");
    looked.clear(); nativeSource=false; sourceBank=false; nativeTarget=false;
    Check(!crew::edf41sound::LookupCue(cue.source,stockCue,importedCue) && looked.size()==2,
          "a failed imported load and unavailable fallback fail without recursion");
    looked.clear(); nativeTarget=true;
    Check(!crew::edf41sound::LookupCue(L"unknown_4_1_cue",stockCue,importedCue) && looked.size()==1,
          "unknown cues are not replaced by unrelated audio");
    for(const auto& entry:crew::edf41sound::kFallbackCues)
        Check(crew::edf41sound::FallbackName(entry.source)==entry.target,"every generated entry is reachable by binary search");
    Check(!crew::edf41sound::FallbackName(nullptr),"a null cue has no fallback");
    using crew::edf41sound::Channels;
    using crew::edf41sound::GunSounds;
    Backend b;
    auto play=[&](Handle* h) { return b.Play(h); };
    auto stop=[&](Handle& h,int fade) { b.Stop(h,fade); };
    auto active=[](const Handle& h) { return h.instance!=0; };
    Channels<Handle> channels;
    Check(channels.Start(-1,play,stop) && channels.Empty() && b.unowned==1,"no channel is an independent one-shot");
    channels.Start(0,play,stop);
    channels.Start(0,play,stop);
    Check(b.actions.size()==3 && b.actions[0].kind=='P' && b.actions[1].kind=='S' &&
          b.actions[1].id==1 && b.actions[1].fade==16 && b.actions[2].kind=='P',"a reused channel stops its old sound before starting another");
    channels.Start(1,play,stop);
    channels.StopOne(0,16,stop);
    Check(!channels.Playing(0,active) && channels.Playing(1,active),"se_stop stops only the authored channel");
    channels.MoveAll([&](Handle& h) { b.Move(h); });
    Check(b.actions.back().kind=='M' && b.actions.back().id==3,"owned sounds follow their object");
    channels.StopAll(0,stop);
    const int stops=b.Stops();
    channels.StopAll(0,stop);
    Check(channels.Empty() && b.Stops()==stops,"state exit, destruction and shutdown release once");
    b.fail=true;
    Check(!channels.Start(0,play,stop) && channels.Empty() && b.actions.back().kind=='S' &&
          b.actions.back().fade==0,"partial playback failure cannot orphan a channel");
    b.fail=false;
    GunSounds<Handle> gun;
    gun.Charge(play,stop);
    gun.LeaveCharge(stop);
    Check(b.actions.back().kind=='S' && b.actions.back().fade==0,"leaving charge stops its independent handle");
    const int before=b.next;
    for(int i=0;i<100;++i)gun.Shot(true,play,active,stop);
    Check(b.next==before+1,"an infinite shot starts once across a burst");
    gun.MoveAll([&](Handle& h) { b.Move(h); });
    Check(b.actions.back().kind=='M',"the sustained shot follows its muzzles");
    const int interrupted=b.next;
    gun.Shot(true,play,[](const Handle&) { return false; },stop);
    Check(b.next==interrupted+1,"a backend-finished loop is restarted by the next authored round");
    gun.LeaveFire(stop);
    Check(b.actions.back().kind=='S' && b.actions.back().fade==0,"leaving fire stops the sustained shot");
    const int shots=b.unowned,finiteStops=b.Stops();
    for(int i=0;i<4;++i)gun.Shot(false,play,active,stop);
    gun.LeaveFire(stop);
    Check(b.unowned==shots+4 && b.Stops()==finiteStops,"finite shots fire every round and their tails finish naturally");
    GunSounds<Handle> other;
    gun.Charge(play,stop);
    gun.Shot(true,play,active,stop);
    other.Shot(true,play,active,stop);
    const int releaseStops=b.Stops();
    gun.Release(stop);
    Check(b.Stops()==releaseStops+2,"death releases both charge and shot");
    const int separate=b.next;
    other.Shot(true,play,active,stop);
    Check(b.next==separate,"one object's release leaves another object's loop alive");
    other.Release(stop);
    b.fail=true;
    gun.Shot(true,play,active,stop);
    b.fail=false;
    const int retry=b.next;
    gun.Shot(true,play,active,stop);
    Check(b.next==retry+1,"a failed loop can start on a later authored round");
    gun.Release(stop);
    struct Key { float time; int kind,channel; };
    struct Clip { int state,clip; std::vector<Key> events; };
    const std::vector<Clip> clips={{7,0,{{0.0f,0,0},{440.0f,1,0},{525.0f,0,1},{976.0f,1,1}}},
                                   {8,0,{{0.0f,0,0},{189.0f,1,0}}},
                                   {9,0,{{0.0f,0,2}}},{9,1,{{20.0f,1,2}}}};
    crew::edf41sound::Timeline timeline;
    Channels<Handle> timed;
    auto cancel=[&] { timed.StopAll(16,stop); };
    auto fire=[&](const Key& key) {
        if(key.kind)timed.StopOne(key.channel,16,stop);
        else timed.Start(key.channel,play,stop);
    };
    timeline.Step(7,0.0f,clips,fire,cancel);
    const int started=b.next;
    timeline.Step(7,0.0f,clips,fire,cancel);
    Check(b.next==started,"reentrant samples of the same CAS time cannot replay time-zero keys");
    timeline.Step(7,1000.0f,clips,fire,cancel);
    Check(timed.Empty() && b.next==started+1,"a skipped frame crosses both authored stop events and the second channel start");
    timeline.Step(7,0.0f,clips,fire,cancel);
    Check(timed.Playing(0,active),"a full animation rewind starts its first channel once");
    const int beforeCancel=b.Stops();
    timeline.Step(8,0.0f,clips,fire,cancel);
    Check(b.Stops()==beforeCancel+1 && timed.Playing(0,active),"a state change stops the previous clip before the new start");
    timeline.Step(8,189.0f,clips,fire,cancel);
    Check(timed.Empty(),"cannon appearance stops at the source MAB's 189 units");
    timeline.Step(9,10.0f,clips,fire,cancel);
    const int continued=b.Stops();
    timeline.Step(9,0.0f,clips,fire,cancel);
    Check(b.Stops()==continued && timed.Playing(2,active),"the next clip of one state retains its owned channel");
    timeline.Step(9,20.0f,clips,fire,cancel);
    Check(timed.Empty(),"a later clip can stop its preceding clip's channel");
    timeline.Reset();
    b.fail=true;
    timeline.Step(7,0.0f,clips,fire,cancel);
    Check(timed.Empty(),"a partial start failure on an animation key leaves no orphan");
    b.fail=false;
    timeline.Step(7,0.0f,clips,fire,cancel);
    const int failedKey=b.next;
    timeline.Step(7,1.0f,clips,fire,cancel);
    Check(b.next==failedKey,"a failed authored key is not retried by a repeated clock sample");
    cancel();
    timeline.Reset();
    if(!failed)std::puts("EDF4.1 sound channel and gun lifecycle checks passed");
    return failed ? 1 : 0;
}
