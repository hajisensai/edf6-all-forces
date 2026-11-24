#include "../src/enemy_control_pose_codec.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <initializer_list>

namespace {
using crew::enemy_native::pose::Kind;
using crew::enemy_native::pose::State;
using crew::enemy_native::pose::Packet;
using crew::enemy_native::pose::Valid;
using crew::enemy_native::pose::Encode;
using crew::enemy_native::pose::Decode;
using crew::enemy_native::pose::kWireBytes;
constexpr std::size_t kFields=crew::enemy_native::pose::kHeaderBytes;
constexpr std::uint32_t kFrame=0xFEDCBA98u;
constexpr float kMotion[3]={0.25f,-0.5f,0.125f};
using Wire=std::array<unsigned char,kWireBytes>;
int checks=0;
void Check(bool good,const char* why,int line) {
    ++checks;
    if(!good) {std::fprintf(stderr,"FAIL line %d: %s\n",line,why);std::exit(1);}
}
#define CHECK(good,why) Check((good),(why),__LINE__)
std::uint32_t Bits(float value) {
    std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof bits);return bits;
}
void Put(Wire& wire,std::size_t at,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)wire[at+i]=static_cast<unsigned char>((value>>(8u*i))&0xFFu);
}
std::uint32_t Get(const Wire& wire,std::size_t at) {
    std::uint32_t value=0;
    for(unsigned i=0;i<4;++i)value|=static_cast<std::uint32_t>(wire[at+i])<<(8u*i);
    return value;
}
void Field(Wire& wire,std::size_t index,std::uint32_t bits) {Put(wire,kFields+index*4,bits);}
State Make(Kind kind) {
    State state{};state.kind=kind;state.profile=0x12345678u;
    state.position[0]=-123.5f;state.position[1]=0.25f;state.position[2]=999.0f;
    state.rotation[1]=0.6f;state.rotation[3]=0.8f;
    state.linearVelocity[0]=3.25f;state.linearVelocity[1]=-4.5f;state.linearVelocity[2]=5.75f;
    switch(kind) {
    case Kind::deiroi:
        state.targetAngles[0]=1.25f;state.targetAngles[1]=-2.5f;
        break;
    case Kind::large:
        state.currentAngles[0]=0.125f;state.currentAngles[1]=-0.25f;
        state.targetAngles[0]=1.25f;state.targetAngles[1]=-2.5f;
        state.accumulatedVelocity[0]=6.0f;state.accumulatedVelocity[1]=7.0f;state.accumulatedVelocity[2]=8.0f;
        state.auxiliaryRotation[0]=0.6f;state.auxiliaryRotation[3]=0.8f;
        state.rotationStep[3]=1.0f;state.rotationDelta[3]=1.0f;
        break;
    case Kind::timeShip:
        state.accumulatedVelocity[0]=6.0f;state.accumulatedVelocity[1]=7.0f;state.accumulatedVelocity[2]=8.0f;
        state.targetRotation[2]=0.6f;state.targetRotation[3]=0.8f;
        state.deformation[0]=1;state.deformation[5]=2;state.deformation[10]=3;state.deformation[15]=1;
        state.deformation[12]=4;state.deformation[13]=5;state.deformation[14]=6;
        state.phase=0.125f;state.scale=2.0f;
        break;
    case Kind::squid:
        for(std::size_t i=0;i<3;++i) {
            state.currentAngles[i]=static_cast<float>(i+1)*0.125f;
            state.targetAngles[i]=static_cast<float>(i+1)*-0.25f;
            state.accumulatedVelocity[i]=static_cast<float>(i+6);
        }
        break;
    case Kind::ufo515:
        state.currentAngles[0]=0.125f;state.currentAngles[1]=-0.25f;
        state.accumulatedVelocity[0]=6;state.accumulatedVelocity[1]=7;state.accumulatedVelocity[2]=8;
        state.bank=-0.75f;
        break;
    }
    return state;
}
Packet PacketOf(const State& state) {
    Packet packet{};packet.state=state;packet.senderFrame=kFrame;
    for(std::size_t i=0;i<3;++i)packet.motion[i]=kMotion[i];
    return packet;
}
Wire Encoded(const Packet& packet) {
    Wire wire{};CHECK(Encode(packet,wire.data(),wire.size()),"fixture encodes");return wire;
}
Wire Encoded(const State& state) {return Encoded(PacketOf(state));}
void Rejected(const Wire& wire,std::size_t bytes=kWireBytes) {
    Packet result=PacketOf(Make(Kind::squid));result.state.profile=0xABCDEF12u;
    const Wire before=Encoded(result);
    CHECK(!Decode(wire.data(),bytes,&result),"invalid packet rejected");
    CHECK(Encoded(result)==before,"decode failure preserves destination");
}
bool Used(Kind kind,std::size_t i) {
    if(i<=6 || (i>=13 && i<=15))return true;
    switch(kind) {
    case Kind::deiroi:return i==10 || i==11;
    case Kind::large:return i==7 || i==8 || i==10 || i==11 || (i>=16 && i<=18) || (i>=23 && i<=34);
    case Kind::timeShip:return (i>=16 && i<=22) || (i>=35 && i<=52);
    case Kind::squid:return (i>=7 && i<=12) || (i>=16 && i<=18);
    case Kind::ufo515:return i==7 || i==8 || (i>=16 && i<=18) || i==53;
    }
    return false;
}
void RoundTripsAndCanonicalFields() {
    const Kind kinds[]={Kind::deiroi,Kind::large,Kind::timeShip,Kind::squid,Kind::ufo515};
    for(Kind kind:kinds) {
        const State state=Make(kind);CHECK(Valid(state),"each native family state valid");
        const Wire wire=Encoded(state);
        CHECK(wire.size()==248,"specified complete frame size");
        CHECK(wire[0]=='P' && wire[1]=='O' && wire[2]=='S' && wire[3]=='E',"literal magic byte order");
        CHECK(Get(wire,4)==2 && Get(wire,8)==static_cast<std::uint32_t>(kind),"version and kind LE");
        CHECK(wire[12]==0x78 && wire[13]==0x56 && wire[14]==0x34 && wire[15]==0x12,"profile LE");
        CHECK(wire[16]==0x98 && wire[17]==0xBA && wire[18]==0xDC && wire[19]==0xFE,"sender frame LE after the profile");
        CHECK(Get(wire,20)==Bits(0.25f) && Get(wire,24)==Bits(-0.5f) && Get(wire,28)==Bits(0.125f),"motion XYZ after the sender frame");
        CHECK(wire[32]==0 && wire[33]==0 && wire[34]==0xF7 && wire[35]==0xC2,"negative binary32 LE");
        CHECK(Get(wire,36)==0x3E800000u && Get(wire,40)==Bits(999.0f),"three positions have canonical offsets");
        CHECK(Get(wire,kFields+4*4)==Bits(0.6f) && Get(wire,kFields+6*4)==Bits(0.8f),"quaternion offset and XYZW order");
        CHECK(Get(wire,kFields+13*4)==Bits(3.25f) && Get(wire,kFields+15*4)==Bits(5.75f),"actual velocity offset");
        Packet result{};CHECK(Decode(wire.data(),wire.size(),&result),"all five families round trip");
        CHECK(result.state.kind==kind && result.state.profile==state.profile,"round trip identity");
        CHECK(result.senderFrame==kFrame && result.motion[0]==kMotion[0] && result.motion[1]==kMotion[1] && result.motion[2]==kMotion[2],"round trip frame and motion");
        CHECK(Encoded(result)==wire,"round trip every encoded float bit");
        for(std::size_t i=0;i<54;++i) {
            if(!Used(kind,i)) {
                CHECK(Get(wire,kFields+i*4)==0,"unused field encoded positive zero");
                Wire dirty=wire;Field(dirty,i,Bits(1.0f));Rejected(dirty);
                dirty=wire;Field(dirty,i,0x80000000u);Rejected(dirty);
            } else {
                for(std::uint32_t nonfinite:{0x7FC00000u,0x7F800000u,0xFF800000u}) {
                    Wire dirty=wire;Field(dirty,i,nonfinite);Rejected(dirty);
                }
            }
        }
        Wire dirty=wire;Put(dirty,12,0);Rejected(dirty);
        dirty=wire;for(std::size_t i=0;i<4;++i)Field(dirty,3+i,0);Rejected(dirty);
        dirty=wire;Field(dirty,6,Bits(2.0f));Rejected(dirty);
    }
    // Family-specific independent offsets catch a field-order swap hidden by round trips.
    Wire large=Encoded(Make(Kind::large));
    CHECK(Get(large,kFields+7*4)==Bits(0.125f) && Get(large,kFields+10*4)==Bits(1.25f),"large current vs target angles");
    CHECK(Get(large,kFields+23*4)==Bits(0.6f) && Get(large,kFields+30*4)==Bits(1.0f) && Get(large,kFields+34*4)==Bits(1.0f),"large shell step delta offsets");
    Wire ship=Encoded(Make(Kind::timeShip));
    CHECK(Get(ship,kFields+21*4)==Bits(0.6f) && Get(ship,kFields+22*4)==Bits(0.8f),"ship target quaternion offset");
    CHECK(Get(ship,kFields+35*4)==Bits(1.0f) && Get(ship,kFields+40*4)==Bits(2.0f) && Get(ship,kFields+45*4)==Bits(3.0f),"deformation matrix row order");
    CHECK(Get(ship,kFields+51*4)==Bits(0.125f) && Get(ship,kFields+52*4)==Bits(2.0f),"ship phase and scale offsets");
    Wire squid=Encoded(Make(Kind::squid));
    CHECK(Get(squid,kFields+9*4)==Bits(0.375f) && Get(squid,kFields+12*4)==Bits(-0.75f),"squid all three native angles");
    Wire ufo=Encoded(Make(Kind::ufo515));CHECK(Get(ufo,kFields+53*4)==Bits(-0.75f),"ufo bank final field");
}
void MotionValidation() {
    const Wire wire=Encoded(Make(Kind::deiroi));
    for(std::size_t axis=0;axis<3;++axis) {
        for(std::uint32_t nonfinite:{0x7FC00000u,0x7F800000u,0xFF800000u}) {Wire bad=wire;Put(bad,20+axis*4,nonfinite);Rejected(bad);}
        Wire bad=wire;Put(bad,20+axis*4,Bits(5.001f));Rejected(bad);
        bad=wire;Put(bad,20+axis*4,Bits(-5.001f));Rejected(bad);
    }
    Packet packet=PacketOf(Make(Kind::deiroi));packet.motion[0]=3;packet.motion[1]=4;packet.motion[2]=0;
    CHECK(crew::enemy_native::pose::Valid(packet),"motion length exactly 5 m per frame accepted");
    packet.motion[2]=0.01f;CHECK(!crew::enemy_native::pose::Valid(packet),"motion length over 5 m per frame rejected");
    packet.motion[0]=packet.motion[1]=packet.motion[2]=0;packet.senderFrame=0;
    Packet result{};CHECK(Decode(Encoded(packet).data(),kWireBytes,&result) && result.senderFrame==0,"zero frame and zero motion legal");
    packet.senderFrame=0xFFFFFFFFu;CHECK(Decode(Encoded(packet).data(),kWireBytes,&result) && result.senderFrame==0xFFFFFFFFu,"any u32 frame legal");
}
void ValidationEdges() {
    State state=Make(Kind::squid);
    state.position[0]=1.0e6f;state.linearVelocity[0]=1.0e7f;CHECK(Valid(state),"inclusive finite bounds");
    state.position[0]=1000001.0f;CHECK(!Valid(state),"position outside bound");
    state.position[0]=0;state.linearVelocity[0]=10000001.0f;CHECK(!Valid(state),"velocity outside bound");
    state=Make(Kind::squid);state.rotation[1]=0;state.rotation[3]=1.0004f;CHECK(Valid(state),"quaternion roundoff tolerance");
    state.rotation[3]=1.0006f;CHECK(!Valid(state),"quaternion outside tolerance");
    state=Make(Kind::large);
    for(unsigned which=0;which<3;++which) {
        State bad=state;float* quaternion=which==0 ? bad.auxiliaryRotation : which==1 ? bad.rotationStep : bad.rotationDelta;
        quaternion[3]=2;CHECK(!Valid(bad),"each large used quaternion normalized");
    }
    state=Make(Kind::timeShip);state.targetRotation[3]=2;CHECK(!Valid(state),"ship target quaternion normalized");
    for(std::size_t index:{std::size_t{3},std::size_t{7},std::size_t{11},std::size_t{15}}) {
        state=Make(Kind::timeShip);state.deformation[index]=index==15 ? 0.0f : 0.25f;
        CHECK(!Valid(state),"deformation must remain affine");
    }
    state=Make(Kind::deiroi);state.bank=-0.0f;CHECK(!Valid(state),"unused negative zero rejected before encoding");
    state=Make(Kind::deiroi);state.profile=0;CHECK(!Valid(state),"profile identity required");
    state=Make(Kind::deiroi);state.kind=static_cast<Kind>(0);CHECK(!Valid(state),"zero kind rejected");
    state.kind=static_cast<Kind>(6);CHECK(!Valid(state),"future kind rejected");
    state=Make(Kind::deiroi);state.linearVelocity[2]=std::numeric_limits<float>::quiet_NaN();CHECK(!Valid(state),"nonfinite rejected before encoding");
}
void FrameValidation() {
    const State state=Make(Kind::squid);const Wire wire=Encoded(state);
    for(std::size_t bytes=0;bytes<wire.size();++bytes)Rejected(wire,bytes);
    Rejected(wire,wire.size()+1);
    for(std::uint32_t version:{0u,1u,3u,0x02000000u}) {Wire bad=wire;Put(bad,4,version);Rejected(bad);}
    for(std::uint32_t kind:{0u,6u,0xFFFFFFFFu}) {Wire bad=wire;Put(bad,8,kind);Rejected(bad);}
    Wire bad=wire;bad[0]='X';Rejected(bad);
    bad=wire;for(std::size_t at=0;at<bad.size();at+=4) {
        const auto a=bad[at],c=bad[at+1];bad[at]=bad[at+3];bad[at+1]=bad[at+2];bad[at+2]=c;bad[at+3]=a;
    }
    Rejected(bad);
    Packet result{};CHECK(!Decode(nullptr,kWireBytes,&result),"null input rejected");
    CHECK(!Decode(wire.data(),wire.size(),nullptr),"null output rejected");
    const Packet packet=PacketOf(state);
    CHECK(!Encode(packet,nullptr,kWireBytes),"null encode buffer rejected");
    Wire untouched{};untouched.fill(0xA5);const Wire before=untouched;
    Packet invalid=packet;invalid.state.profile=0;
    CHECK(!Encode(invalid,untouched.data(),untouched.size()) && untouched==before,"invalid state leaves wire unchanged");
    CHECK(!Encode(packet,untouched.data(),untouched.size()-1) && untouched==before,"short output leaves wire unchanged");
    CHECK(!Encode(packet,untouched.data(),untouched.size()+1) && untouched==before,"oversize output rejects exact framing");
    // A version-1 frame (232 bytes, no frame / motion) is rejected at its own length as well.
    {std::array<unsigned char,232> v1{};for(std::size_t i=0;i<16;++i)v1[i]=wire[i];v1[4]=1;
        for(std::size_t i=0;i<216;++i)v1[16+i]=wire[32+i];
        CHECK(!Decode(v1.data(),v1.size(),&result),"version-1 frame rejected");}
}
} // namespace

int main() {
    RoundTripsAndCanonicalFields();ValidationEdges();MotionValidation();FrameValidation();
    std::printf("%d pose codec checks passed; all five families, canonical little endian, rejected malformed frames\n",checks);
    return 0;
}
