#include "../src/enemy_control_net_input.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <cstring>

namespace {
int checks=0;
void Check(bool ok,const char* why) {++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
}
int main() {
    namespace en=crew::enemy_control_net;
    using crew::enemy_control::Input;
    static_assert(en::kInputBytes==132,"fixed semantic codec independent of native Input padding");
    Input input;input.move[0]=1;input.move[1]=-1;input.look[0]=0.25f;
    input.absoluteAim=true;input.aimYaw=3.0f;input.aimPitch=-1.2f;
    input.active[0]={0x10203040u,1};for(unsigned i=1;i<7;++i)input.active[i]={1000u+i,1};input.active[7]={0xFFFFFFFFu,2};
    input.focusRef[0]=0x71;input.focusRef[24]=0x42;
    input.sequence=0xFFFFFFFFFFFFFFFFull;input.seed=0xFFFFFFFFFFFFFFFFull;
    unsigned char bytes[en::kInputBytes]{};Input decoded;
    Check(en::EncodeInput(input,bytes,sizeof(bytes)),"all eight simultaneous native channels have fixed canonical room");
    Check(bytes[0]==4 && !bytes[1] && !bytes[2] && !bytes[3],"explicit input version and reserved zero bytes");
    Check(bytes[36]==0x40 && bytes[37]==0x30 && bytes[38]==0x20 && bytes[39]==0x10,"stable ability ID is little endian, never a pointer");
    Check(en::DecodeInput(bytes,sizeof(bytes),&decoded),"canonical payload roundtrip");
    Check(decoded.move[0]==1 && decoded.move[1]==-1 && decoded.look[0]==0.25f && decoded.active[7].id==0xFFFFFFFFu,
          "semantic axes and unlimited-catalog stable IDs survive the codec");
    Check(decoded.absoluteAim && decoded.aimYaw==3.0f && decoded.aimPitch==-1.2f,"absolute aim survives different native frame cadences");
    Check(bytes[24]==1 && !bytes[25] && !bytes[26] && !bytes[27],"aim flags use a canonical uint32 mask");
    Check(decoded.focusRef[0]==0x71 && decoded.focusRef[24]==0x42 && bytes[100]==0x71 && bytes[124]==0x42,"full native focus identity has explicit canonical 32-byte room after skill channels");
    Check(!decoded.sequence && !decoded.seed,"controller-provided metadata is absent; host sequence/seed must be supplied by envelope");
    Check(en::SameChannels(input,decoded) && en::HasPress(decoded),"skill press/hold channels are tracked independently of look samples");
    input.look[0]=-0.5f;Check(en::SameChannels(input,decoded),"look-only motion can merge at transport sample rate");
    input.active[0]=input.active[7];input.active[7]={};Check(!en::SameChannels(input,decoded),"skill release is detected and must send immediately");
    Input invalid=decoded;invalid.move[0]=std::numeric_limits<float>::quiet_NaN();Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"NaN cannot reach native synthetic input");
    invalid=decoded;invalid.look[1]=std::numeric_limits<float>::infinity();Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"infinity cannot reach native aim");
    invalid=decoded;invalid.move[2]=1.001f;Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"move axes are normalized by contract");
    invalid=decoded;invalid.look[0]=-1.001f;Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"look axes are normalized by contract");
    invalid=decoded;invalid.aimYaw=std::numeric_limits<float>::quiet_NaN();Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"absolute yaw rejects NaN");
    invalid=decoded;invalid.aimPitch=std::numeric_limits<float>::infinity();Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"absolute pitch rejects infinity");
    invalid=decoded;invalid.aimYaw=3.142f;Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"absolute yaw stays in canonical radians");
    invalid=decoded;invalid.aimPitch=-1.201f;Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"absolute pitch stays within native view limits");
    invalid=decoded;invalid.absoluteAim=false;Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"disabled absolute aim cannot retain hidden angle state");
    invalid.aimYaw=invalid.aimPitch=0;Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"relative axes or skills cannot bypass absolute aim validation");
    invalid={};Check(en::EncodeInput(invalid,bytes,sizeof(bytes)),"only a complete neutral sample may omit absolute aim");
    invalid=decoded;invalid.focusRef[20]=1;Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"native target padding is canonical and cannot carry arbitrary bits");
    // An enemy player as the focus (PvP): its mission slot (net_object_ref.h), the tag at bytes 20..23.
    {Input player=decoded;std::memset(player.focusRef,0,32);Check(crew::netref::MakePlayer(3,player.focusRef),"slot reference");
     Input back;Check(en::EncodeInput(player,bytes,sizeof(bytes)) && en::DecodeInput(bytes,sizeof(bytes),&back) &&
                      !std::memcmp(back.focusRef,player.focusRef,32),"a player's focus reference round trips");
     player.focusRef[21]^=1;Check(!en::EncodeInput(player,bytes,sizeof(bytes)),"a broken player tag is no focus");
     Check(en::EncodeInput(decoded,bytes,sizeof(bytes)),"re-encode");bytes[0]=3;
     Check(!en::DecodeInput(bytes,sizeof(bytes),&back),"input version 3 (a NetId-only focus) is left out");}
    invalid=decoded;invalid.active[1]={0,1};Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"absent ability cannot claim a held phase");
    invalid=decoded;invalid.active[1]={9,0};Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"nonzero ability must have an explicit supported phase");
    invalid=decoded;invalid.active[1]={9,3};Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"unknown phase cannot become a raw native key value");
    invalid=decoded;invalid.active[1]=invalid.active[0];Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"one sample cannot duplicate an ability ID across native channels");
    invalid=decoded;invalid.active[1]={};Check(!en::EncodeInput(invalid,bytes,sizeof(bytes)),"holes cannot hide later skill channels after the canonical list ended");
    Check(!en::DecodeInput(nullptr,sizeof(bytes),&decoded) && !decoded.sequence && !decoded.active[0].id,"failure clears the complete output");
    Check(en::EncodeInput(input,bytes,sizeof(bytes)),"restore valid release payload");
    for(std::size_t n=0;n<sizeof(bytes);++n)Check(!en::DecodeInput(bytes,n,&decoded),"truncated payload is never partially accepted");
    Check(!en::DecodeInput(bytes,sizeof(bytes)+1,&decoded),"trailing bytes are refused");
    for(unsigned offset=0;offset<4;++offset) {
        auto changed=bytes[offset];bytes[offset]=1;
        Check(!en::DecodeInput(bytes,sizeof(bytes),&decoded),"version/reserved-byte mismatch is refused");bytes[offset]=changed;
    }
    unsigned char corrupt[en::kInputBytes]{};std::memcpy(corrupt,bytes,sizeof(bytes));
    const unsigned nan=0x7FC00000u;std::memcpy(corrupt+4,&nan,4);
    Check(!en::DecodeInput(corrupt,sizeof(corrupt),&decoded),"wire NaN is revalidated at receive boundary");
    std::memcpy(corrupt,bytes,sizeof(bytes));corrupt[24]=2;
    Check(!en::DecodeInput(corrupt,sizeof(corrupt),&decoded),"unknown absolute aim flag is refused");
    std::memcpy(corrupt,bytes,sizeof(bytes));corrupt[27]=1;
    Check(!en::DecodeInput(corrupt,sizeof(corrupt),&decoded),"high aim flag bits are refused");
    std::memcpy(corrupt,bytes,sizeof(bytes));corrupt[24]=0;
    Check(!en::DecodeInput(corrupt,sizeof(corrupt),&decoded),"disabled aim wire fields must be neutral");
    std::memcpy(corrupt,bytes,sizeof(bytes));std::memcpy(corrupt+28,&nan,4);
    Check(!en::DecodeInput(corrupt,sizeof(corrupt),&decoded),"absolute aim wire floats are revalidated");
    std::printf("enemy control semantic input: %d checks passed\n",checks);
}
