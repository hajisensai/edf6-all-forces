// The plugin's online channels (src/plugin_net_logic.h) and their codec (src/wire.h): no game, no transport.
//  - registration: no receive, no magic, a magic taken, bounds that make no sense refused; a full registry refuses more;
//  - routing: by magic (every channel has one: the PvP channel's AFPV too); none of theirs left to the support parser (Route::none);
//  - drops: another version, a size out of bounds (every peer's messages read: no per-feature bits since net-6);
//  - joins: a channel with a joined callback is told once per peer, again after Reset;
//  - PUIDs and origins;
//  - wire.h: field order, sticky failure, finite floats, canonical IDs;
//  - the room watch: which changes reset every channel, once.
#include "../src/plugin_net_logic.h"
#include "../src/pvp_protocol.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <utility>

using namespace crew::plugin_net;
namespace {
int checks=0,failures=0;
void Check(bool ok,const char* what) { ++checks;if(!ok){++failures;std::printf("FAIL %s\n",what);} }

constexpr std::uint32_t kA=0x41414141u,kB=0x42424242u,kC=0x43434343u;
int gotA=0,gotB=0,gotC=0,gotPvp=0;
std::uint32_t lastPeer=0;
bool ReceiveA(std::uint32_t p,const char*,const void*,std::size_t,std::uint64_t) noexcept { ++gotA;lastPeer=p;return true; }
bool ReceiveB(std::uint32_t p,const char*,const void*,std::size_t,std::uint64_t) noexcept { ++gotB;lastPeer=p;return true; }
bool ReceiveC(std::uint32_t,const char*,const void*,std::size_t,std::uint64_t) noexcept { ++gotC;return true; }
bool ReceivePvp(std::uint32_t,const char*,const void*,std::size_t,std::uint64_t) noexcept { ++gotPvp;return true; }
std::vector<std::pair<std::size_t,std::uint32_t>> joins;
void JoinedB(std::uint32_t) noexcept {}

std::vector<unsigned char> Message(std::uint32_t magic,std::uint32_t version,std::size_t size) {
    std::vector<unsigned char> m(size,0);
    crew::wire::Writer w(m.data(),m.size());w.U32(magic).U32(version);
    return m;
}

void RegistryChecks() {
    Registry reg;
    Channel a;a.name="a";a.magic=kA;a.receive=&ReceiveA;            // an older channel: its own versions, every peer
    Channel b;b.name="b";b.magic=kB;b.version=3;b.minSize=b.maxSize=64;b.receive=&ReceiveB;b.joined=&JoinedB;
    // The PvP channel as support_net.cpp registers it: its magic, the protocol's own size bounds.
    Channel pvp;pvp.name="pvp";pvp.magic=pvp_protocol::kMagic;pvp.minSize=32;pvp.maxSize=pvp_protocol::kMaxWireBytes;pvp.receive=&ReceivePvp;
    Check(reg.Register(a) && reg.Register(b) && reg.Register(pvp),"three channels registered");
    Channel dup=a;dup.receive=&ReceiveC;
    Check(!reg.Register(dup),"a magic taken is refused");
    Channel none;none.receive=&ReceiveC;
    Check(!reg.Register(none),"no magic is refused (every channel has one)");
    Channel noReceive;noReceive.magic=kC;
    Check(!reg.Register(noReceive),"no receive is refused");
    Channel badSize;badSize.magic=kC;badSize.receive=&ReceiveC;badSize.minSize=100;badSize.maxSize=50;
    Check(!reg.Register(badSize),"bounds that make no sense are refused");
    badSize.minSize=8;badSize.maxSize=kMaxPayload+1;
    Check(!reg.Register(badSize),"a bound past the payload is refused");
    Check(reg.Count()==3,"only the good ones");

    auto ma=Message(kA,99,20);
    Check(reg.Dispatch(1,"p1",ma.data(),ma.size(),0)==Route::delivered && gotA==1,"a channel without a version takes any");
    auto mb=Message(kB,3,64);
    Check(reg.Dispatch(1,"p1",mb.data(),mb.size(),0)==Route::delivered && gotB==1 && lastPeer==1,"its version: delivered");
    Check(reg.Dispatch(2,"p2",mb.data(),mb.size(),0)==Route::delivered && gotB==2 && lastPeer==2,"any peer's: delivered");
    auto old=Message(kB,2,64);
    Check(reg.Dispatch(1,"p1",old.data(),old.size(),0)==Route::dropped && gotB==2,"another version: the channel's, dropped");
    auto small=Message(kB,3,63);
    Check(reg.Dispatch(1,"p1",small.data(),small.size(),0)==Route::dropped && gotB==2,"a size out of bounds: dropped");
    {
        pvp_protocol::Message hello{};hello.kind=pvp_protocol::Kind::Hello;
        unsigned char bytes[pvp_protocol::kMaxWireBytes];
        const std::size_t n=pvp_protocol::wire::Encode(hello,bytes,sizeof(bytes));
        Check(n>=32 && !std::memcmp(bytes,"AFPV",4),"the PvP wire starts with its magic");
        Check(reg.Dispatch(3,"p3",bytes,n,0)==Route::delivered && gotPvp==1,"a PvP message routed by its magic");
        Check(reg.Dispatch(3,"p3",bytes,31,0)==Route::dropped && gotPvp==1,"shorter than a PvP header: the channel's, dropped");
    }
    auto unknown=Message(0x54525053u,2,176);   // support_net's own (support_protocol.h kMagic): none of the channels'
    Check(reg.Dispatch(1,"p1",unknown.data(),unknown.size(),0)==Route::none,"none of theirs: left to the support parser");
    Check(reg.Dispatch(1,"p1",ma.data(),3,0)==Route::none,"too short to have a magic: none");
    Check(reg.Dispatch(0,"",nullptr,0,0)==Route::none,"no bytes: none");

    // State messages (EDF6Coop extension ABI 2): only a channel's kinds it takes as state; never left to the support parser.
    Check(reg.Dispatch(1,"p1",mb.data(),mb.size(),0,true)==Route::dropped && gotB==2,"a state to a channel taking none: dropped");
    Check(reg.Dispatch(1,"p1",unknown.data(),unknown.size(),0,true)==Route::dropped,"a state of no channel: dropped, not none");
    Check(reg.Dispatch(1,"p1",ma.data(),3,0,true)==Route::dropped,"a state too short for a magic: dropped");
    Registry st;
    Channel s;s.name="s";s.magic=kC;s.version=1;s.minSize=s.maxSize=16;s.receive=&ReceiveC;
    s.takesState=[](const void* p,std::size_t n) noexcept { return n>8 && static_cast<const unsigned char*>(p)[8]==1; };
    Check(st.Register(s),"a channel taking states");
    auto s1=Message(kC,1,16);s1[8]=1;
    auto s2=Message(kC,1,16);s2[8]=2;
    Check(st.Dispatch(1,"p1",s1.data(),s1.size(),0,true)==Route::delivered && gotC==1,"its state kind: delivered");
    Check(st.Dispatch(1,"p1",s2.data(),s2.size(),0,true)==Route::dropped && gotC==1,"another kind as a state: dropped");
    Check(st.Dispatch(1,"p1",s2.data(),s2.size(),0)==Route::delivered && gotC==2,"the same kind reliable: delivered");
    auto s3=Message(kC,1,15);s3[8]=1;
    Check(st.Dispatch(1,"p1",s3.data(),s3.size(),0,true)==Route::dropped && gotC==2,"a state out of bounds: dropped");
    Check(StateKey(StateStream::enemyPose,0)==1024u && StateKey(StateStream::ultimateKeep,5)==4u*1024u+5u,"the key rule");
    Check(StateKey(StateStream::enemyPose,kStateObjects)==0u && StateKey(static_cast<StateStream>(0),1)==0u &&
          StateKey(static_cast<StateStream>(kStateStreams),0)==0u,"an object or stream out of range: no key");
    Check(StateKey(static_cast<StateStream>(kStateStreams-1),kStateObjects-1)<65536u,"the largest key under 65536");

    // Joins: each channel with a joined callback, once per peer of the room, again after Reset.
    Peers peers;peers.Reset();
    joins.clear();
    auto record=[&](std::size_t i,std::uint32_t p) noexcept {joins.emplace_back(i,p);};
    peers.Joins(reg,2,record);
    Check(joins.size()==2 && joins[0]==std::make_pair(std::size_t{1},1u) && joins[1]==std::make_pair(std::size_t{1},2u),
          "the one channel with joined told of each peer");
    peers.Joins(reg,2,record);
    Check(joins.size()==2,"told once");
    peers.Joins(reg,3,record);
    Check(joins.size()==3 && joins[2].second==3,"a third peer: told of it alone");
    peers.Reset();peers.Joins(reg,3,record);
    Check(joins.size()==6,"a new room: told again");
    peers.Reset();joins.clear();peers.Joins(reg,kMaxPeers+5,record);
    Check(joins.size()==kMaxPeers && joins.back().second==kMaxPeers,"never past kMaxPeers");

    Registry full;
    bool all=true;
    for(std::uint32_t i=0;i<kMaxChannels;++i){Channel c;c.magic=0x1000u+i;c.receive=&ReceiveC;all=all && full.Register(c);}
    Channel more;more.magic=0x9999u;more.receive=&ReceiveC;
    Check(all && !full.Register(more),"a full registry refuses more");
}

void PuidChecks() {
    char longPuid[kPuidChars+2];std::memset(longPuid,'a',sizeof(longPuid)-1);longPuid[sizeof(longPuid)-1]=0;
    Check(!ValidPuid(longPuid) && ValidPuid("a") && !ValidPuid("a b") && !ValidPuid("") && !ValidPuid(nullptr),
          "a PUID: 1..64 printable characters");
    Check(Origin(0,0)==1u && (Origin(6,0)&1u) && Origin(7,0)==7u,"an origin is never zero");
}

void WireChecks() {
    unsigned char b[64];
    unsigned char id[32]{};id[0]=1;id[31]=9;
    crew::wire::Writer w(b,sizeof(b));
    const float v[3]={1.5f,-2.0f,3.25f};
    w.U32(0xA1B2C3D4u).U16(0x1234).U8(7).Id(id).V3(v);
    Check(w.Ok() && w.Used()==4+2+1+32+12,"the writer's fields in order");
    Check(b[0]==0xD4 && b[3]==0xA1 && b[4]==0x34 && b[5]==0x12 && b[6]==7,"little-endian");
    crew::wire::Reader r(b,w.Used());
    unsigned char idBack[32];float vb[3];
    const std::uint32_t u=r.U32();const std::uint16_t h=r.U16();const std::uint8_t c=r.U8();r.Id(idBack);r.V3(vb);
    Check(r.Ok() && u==0xA1B2C3D4u && h==0x1234 && c==7 && !std::memcmp(idBack,id,32) && vb[2]==3.25f,"the reader reads them back");
    r.U8();
    Check(!r.Ok() && r.U32()==0,"a read past the end fails, and stays failed");
    unsigned char badId[32]{};badId[21]=1;
    crew::wire::Writer w2(b,sizeof(b));w2.Id(badId);
    Check(!w2.Ok(),"a non-canonical ID is not written");
    std::memcpy(b,badId,32);
    crew::wire::Reader r2(b,32);r2.Id(idBack);
    Check(!r2.Ok() && idBack[21]==0,"a non-canonical ID is not read");
    crew::wire::Writer w3(b,sizeof(b));w3.F(std::nanf(""));
    Check(!w3.Ok(),"a float not finite is not written");
    const float big=2.0e7f;std::memcpy(b,&big,4);
    crew::wire::Reader r3(b,4);r3.F();
    Check(!r3.Ok(),"a float past a world's coordinates is not read");
    crew::wire::Writer w4(b,8);w4.U32(1).U32(2).U32(3);
    Check(!w4.Ok(),"a write past the end fails");
    Check(crew::wire::HeadWord(b,8,1)==2 && crew::wire::HeadWord(b,7,1)==0,"the head words");
}

// The room watch (2026-10-20 review): a member joining or leaving, another mission, a room left: every channel's reset
// once; a short drop of the same room: the peers forgotten (told `joined` again) but no channel reset; offline frames
// and a first room: none.
int resetsA=0,resetsB=0;
void ResetA() noexcept { ++resetsA; }
void ResetB() noexcept { ++resetsB; }
void RoomChecks() {
    Registry reg;
    Channel a;a.name="a";a.magic=kA;a.receive=&ReceiveA;a.reset=&ResetA;
    Channel b;b.name="b";b.magic=kB;b.receive=&ReceiveB;b.reset=&ResetB;
    Channel c;c.name="c";c.magic=kC;c.receive=&ReceiveC;   // no reset: skipped
    Check(reg.Register(a) && reg.Register(b) && reg.Register(c),"three channels");
    RoomWatch w;
    const auto see=[&](const crew::CommandNetworkContext& ctx,bool up) {
        const RoomStep s=w.See(ctx,up);
        if(s.resetChannels)reg.ResetAll();
        return s;
    };
    crew::CommandNetworkContext off{};
    for(int i=0;i<5;++i){const RoomStep s=see(off,false);Check(s.forgetPeers && !s.resetChannels,"offline: no channel reset, frame after frame");}
    crew::CommandNetworkContext room{};room.ready=true;room.epoch=9;room.peerCount=2;room.hostPeer=1;room.localPuid="me";
    RoomStep s=see(room,true);
    Check(s.forgetPeers && !s.resetChannels && !resetsA,"the first room: the peers new, nothing to reset");
    s=see(room,true);Check(!s.forgetPeers && !s.resetChannels,"the same room: nothing");
    crew::CommandNetworkContext joined=room;joined.peerCount=3;
    s=see(joined,true);
    Check(s.forgetPeers && s.resetChannels && resetsA==1 && resetsB==1,"a member joined: every channel reset, once");
    s=see(joined,true);Check(!s.resetChannels && resetsA==1,"...and not again the next frame");
    s=see(room,true);Check(s.resetChannels && resetsA==2,"a member left: reset");
    s=see(room,false);Check(s.forgetPeers && !s.resetChannels && resetsA==2,"a short drop of the same room: peers forgotten, no reset");
    s=see(room,true);Check(s.forgetPeers==false && !s.resetChannels && resetsA==2,"back in the same room: no reset");
    crew::CommandNetworkContext host=room;host.host=true;host.hostPeer=0;
    s=see(host,true);Check(s.resetChannels && resetsA==3,"this machine the host now: reset");
    crew::CommandNetworkContext other=host;other.localPuid="someone";
    s=see(other,true);Check(s.resetChannels && resetsA==4,"another local PUID: reset");
    crew::CommandNetworkContext mission=other;mission.epoch=10;
    s=see(mission,true);Check(s.resetChannels && resetsA==5,"another epoch: reset");
    s=see(off,false);Check(s.resetChannels && resetsA==6 && resetsB==6,"the room left: reset");
    for(int i=0;i<3;++i){s=see(off,false);Check(!s.resetChannels,"offline after it: no more");}
    Check(resetsA==6 && resetsB==6,"each reset reached every channel that has one");
    w.Reset();s=see(room,true);Check(!s.resetChannels && resetsA==6,"after the transport's whole reset the next room is a first");
}
}  // namespace

int main() {
    RegistryChecks();
    PuidChecks();
    WireChecks();
    RoomChecks();
    std::printf("plugin_net: %d checks, %d failures\n",checks,failures);
    return failures ? 1 : 0;
}
